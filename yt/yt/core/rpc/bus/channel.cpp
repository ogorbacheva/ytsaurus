#include "channel.h"

#include <yt/yt/core/actions/future.h>

#include <yt/yt/core/rpc/client.h>
#include <yt/yt/core/rpc/dispatcher.h>
#include <yt/yt/core/rpc/message.h>
#include <yt/yt/core/rpc/stream.h>
#include <yt/yt/core/rpc/private.h>

#include <yt/yt/core/bus/bus.h>

#include <yt/yt/core/bus/tcp/config.h>
#include <yt/yt/core/bus/tcp/client.h>

#include <yt/yt/core/concurrency/delayed_executor.h>
#include <yt/yt/core/concurrency/spinlock.h>
#include <yt/yt/core/concurrency/thread_affinity.h>

#include <yt/yt/core/misc/singleton.h>
#include <yt/yt/core/misc/tls_cache.h>
#include <yt/yt/core/misc/finally.h>
#include <yt/yt/core/misc/atomic_object.h>

#include <yt/yt/core/profiling/timing.h>

#include <yt/yt_proto/yt/core/rpc/proto/rpc.pb.h>

#include <yt/yt/library/profiling/sensor.h>

#include <array>

namespace NYT::NRpc::NBus {

using namespace NYT::NBus;
using namespace NYPath;
using namespace NYTree;
using namespace NYson;
using namespace NConcurrency;
using namespace NYTAlloc;

using NYT::FromProto;
using NYT::ToProto;

////////////////////////////////////////////////////////////////////////////////

static const auto& Logger = RpcClientLogger;

////////////////////////////////////////////////////////////////////////////////

class TBusChannel
    : public IChannel
{
public:
    explicit TBusChannel(IBusClientPtr client)
        : Client_(std::move(client))
        , NetworkId_(TDispatcher::Get()->GetNetworkId(Client_->GetNetworkName()))
    {
        YT_VERIFY(Client_);
    }

    virtual const TString& GetEndpointDescription() const override
    {
        return Client_->GetEndpointDescription();
    }

    virtual const IAttributeDictionary& GetEndpointAttributes() const override
    {
        return Client_->GetEndpointAttributes();
    }

    virtual TNetworkId GetNetworkId() const override
    {
        return NetworkId_;
    }

    virtual IClientRequestControlPtr Send(
        IClientRequestPtr request,
        IClientResponseHandlerPtr responseHandler,
        const TSendOptions& options) override
    {
        VERIFY_THREAD_AFFINITY_ANY();

        TSessionPtr session;

        try {
            session = GetOrCreateSession(options.MultiplexingBand);
        } catch (const std::exception& ex) {
            responseHandler->HandleError(TError(ex));
            return nullptr;
        }

        return session->SendRequest(
            std::move(request),
            std::move(responseHandler),
            options);
    }

    virtual void Terminate(const TError& error) override
    {
        YT_VERIFY(!error.IsOK());
        VERIFY_THREAD_AFFINITY_ANY();

        if (TerminationFlag_.exchange(true)) {
            return;
        }

        TerminationError_.Store(error);

        std::vector<TSessionPtr> sessions;
        for (auto& bucket : Buckets_) {
            auto guard = WriterGuard(bucket.Lock);

            if (bucket.Session) {
                sessions.push_back(bucket.Session);
                bucket.Session.Reset();
            }

            bucket.Terminated = true;
        }

        for (const auto& session : sessions) {
            session->Terminate(error);
        }

        Terminated_.Fire(error);
    }

    virtual void SubscribeTerminated(const TCallback<void(const TError&)>& callback) override
    {
        Terminated_.Subscribe(callback);
    }

    virtual void UnsubscribeTerminated(const TCallback<void(const TError&)>& callback) override
    {
        Terminated_.Unsubscribe(callback);
    }

private:
    class TSession;
    using TSessionPtr = TIntrusivePtr<TSession>;

    class TClientRequestControl;
    using TClientRequestControlPtr = TIntrusivePtr<TClientRequestControl>;

    const IBusClientPtr Client_;
    const TNetworkId NetworkId_;

    TSingleShotCallbackList<void(const TError&)> Terminated_;

    struct TBandBucket
    {
        YT_DECLARE_SPINLOCK(TReaderWriterSpinLock, Lock);
        TSessionPtr Session;
        bool Terminated = false;
    };

    TEnumIndexedVector<EMultiplexingBand, TBandBucket> Buckets_;

    std::atomic<bool> TerminationFlag_ = false;
    TAtomicObject<TError> TerminationError_;

    TSessionPtr GetOrCreateSession(EMultiplexingBand band)
    {
        auto& bucket = Buckets_[band];

        // Fast path.
        {
            auto guard = ReaderGuard(bucket.Lock);

            if (bucket.Session) {
                return bucket.Session;
            }
        }

        IBusPtr bus;
        TSessionPtr session;

        // Slow path.
        {
            auto networkId = TDispatcher::Get()->GetNetworkId(Client_->GetNetworkName());
            auto guard = WriterGuard(bucket.Lock);

            if (bucket.Session) {
                return bucket.Session;
            }

            if (bucket.Terminated) {
                guard.Release();
                THROW_ERROR_EXCEPTION(NRpc::EErrorCode::TransportError, "Channel terminated")
                    << TerminationError_.Load();
            }

            session = New<TSession>(band, networkId);

            auto messageHandler = New<TMessageHandler>(session);
            bus = Client_->CreateBus(messageHandler);

            session->Initialize(bus);

            bucket.Session = session;
        }

        bus->SubscribeTerminated(BIND(
            &TBusChannel::OnBusTerminated,
            MakeWeak(this),
            MakeWeak(session),
            band));

        return session;
    }

    void OnBusTerminated(const TWeakPtr<TSession>& session, EMultiplexingBand band, const TError& error)
    {
        auto session_ = session.Lock();
        if (!session_) {
            return;
        }

        auto& bucket = Buckets_[band];

        {
            auto guard = WriterGuard(bucket.Lock);

            if (bucket.Session == session_) {
                bucket.Session.Reset();
            }
        }

        session_->Terminate(error);
    }

    //! Provides a weak wrapper around a session and breaks the cycle
    //! between the session and its underlying bus.
    class TMessageHandler
        : public IMessageHandler
    {
    public:
        explicit TMessageHandler(TSessionPtr session)
            : Session_(std::move(session))
        { }

        virtual void HandleMessage(TSharedRefArray message, IBusPtr replyBus) noexcept override
        {
            auto session_ = Session_.Lock();
            if (session_) {
                session_->HandleMessage(std::move(message), std::move(replyBus));
            }
        }

    private:
        const TWeakPtr<TSession> Session_;

    };

    //! Directs requests sent via a channel to go through its underlying bus.
    //! Terminates when the underlying bus does so.
    class TSession
        : public IMessageHandler
    {
    public:
        TSession(
            EMultiplexingBand band,
            TNetworkId networkId)
            : TosLevel_(TDispatcher::Get()->GetTosLevelForBand(band, networkId))
        { }

        void Initialize(IBusPtr bus)
        {
            YT_ASSERT(bus);
            Bus_ = std::move(bus);
            Bus_->SetTosLevel(TosLevel_);
        }

        void Terminate(const TError& error)
        {
            YT_VERIFY(!error.IsOK());

            if (TerminationFlag_.exchange(true)) {
                return;
            }

            TerminationError_.Store(error);

            std::vector<std::tuple<TClientRequestControlPtr, IClientResponseHandlerPtr>> existingRequests;

            // Mark the channel as terminated to disallow any further usage.
            for (auto& bucket : RequestBuckets_) {
                auto guard = Guard(bucket.Lock);

                bucket.Terminated = true;

                existingRequests.reserve(bucket.ActiveRequestMap.size());
                for (auto& [requestId, requestControl] : bucket.ActiveRequestMap) {
                    auto responseHandler = requestControl->Finalize(guard);
                    existingRequests.emplace_back(std::move(requestControl), std::move(responseHandler));
                }

                bucket.ActiveRequestMap.clear();
            }

            for (const auto& existingRequest : existingRequests) {
                NotifyError(
                    std::get<0>(existingRequest),
                    std::get<1>(existingRequest),
                    TStringBuf("Request failed due to channel termination"),
                    error);
            }
        }

        IClientRequestControlPtr SendRequest(
            IClientRequestPtr request,
            IClientResponseHandlerPtr responseHandler,
            const TSendOptions& options)
        {
            YT_VERIFY(request);
            YT_VERIFY(responseHandler);
            VERIFY_THREAD_AFFINITY_ANY();

            auto requestControl = New<TClientRequestControl>(
                this,
                request,
                options,
                std::move(responseHandler));

            auto& header = request->Header();
            header.set_start_time(ToProto<i64>(TInstant::Now()));

            {
                // NB: Requests without timeout are rare but may occur.
                // For these requests we still need to register a timeout cookie with TDelayedExecutor
                // since this also provides proper cleanup and cancelation when global shutdown happens.
                auto effectiveTimeout = options.Timeout.value_or(TDuration::Hours(24));
                auto timeoutCookie = TDelayedExecutor::Submit(
                    BIND(&TSession::HandleTimeout, MakeWeak(this), requestControl),
                    effectiveTimeout,
                    TDispatcher::Get()->GetHeavyInvoker());
                requestControl->SetTimeoutCookie(std::move(timeoutCookie));
            }

            if (options.Timeout) {
                header.set_timeout(ToProto<i64>(*options.Timeout));
            } else {
                header.clear_timeout();
            }

            if (request->IsHeavy()) {
                BIND(&IClientRequest::Serialize, request)
                    .AsyncVia(TDispatcher::Get()->GetHeavyInvoker())
                    .Run()
                    .Subscribe(BIND(
                        &TSession::OnRequestSerialized,
                        MakeStrong(this),
                        requestControl,
                        options));
            } else {
                try {
                    auto requestMessage = request->Serialize();
                    OnRequestSerialized(
                        requestControl,
                        options,
                        std::move(requestMessage));
                } catch (const std::exception& ex) {
                    OnRequestSerialized(
                        requestControl,
                        options,
                        TError(ex));
                }
            }

            return requestControl;
        }

        void Cancel(const TClientRequestControlPtr& requestControl)
        {
            VERIFY_THREAD_AFFINITY_ANY();

            auto requestId = requestControl->GetRequestId();
            auto* bucket = GetBucketForRequest(requestId);

            IClientResponseHandlerPtr responseHandler;
            {
                auto guard = Guard(bucket->Lock);

                auto it = bucket->ActiveRequestMap.find(requestId);
                if (it == bucket->ActiveRequestMap.end()) {
                    YT_LOG_DEBUG("Attempt to cancel an unknown request, ignored (RequestId: %v)",
                        requestId);
                    return;
                }

                if (requestControl != it->second) {
                    YT_LOG_DEBUG("Attempt to cancel a resent request, ignored (RequestId: %v)",
                        requestId);
                    return;
                }

                requestControl->ProfileCancel();
                responseHandler = requestControl->Finalize(guard);
                bucket->ActiveRequestMap.erase(it);
            }

            // YT-1639: Avoid long chain of recursive calls.
            thread_local int Depth = 0;
            constexpr int MaxDepth = 10;
            if (Depth < MaxDepth) {
                ++Depth;
                NotifyError(
                    requestControl,
                    responseHandler,
                    TStringBuf("Request canceled"),
                    TError(NYT::EErrorCode::Canceled, "Request canceled"));
                --Depth;
            } else {
                TDispatcher::Get()->GetHeavyInvoker()->Invoke(BIND(
                    &TSession::NotifyError,
                    MakeStrong(this),
                    requestControl,
                    responseHandler,
                    TStringBuf("Request canceled"),
                    TError(NYT::EErrorCode::Canceled, "Request canceled")));
            }

            if (TerminationFlag_.load()) {
                return;
            }

            const auto& realmId = requestControl->GetRealmId();
            const auto& service = requestControl->GetService();
            const auto& method = requestControl->GetMethod();

            NProto::TRequestCancelationHeader header;
            ToProto(header.mutable_request_id(), requestId);
            header.set_service(service);
            header.set_method(method);
            if (realmId) {
                ToProto(header.mutable_realm_id(), realmId);
            }

            auto message = CreateRequestCancelationMessage(header);
            Bus_->Send(std::move(message), NBus::TSendOptions(EDeliveryTrackingLevel::None));
        }

        TFuture<void> SendStreamingPayload(
            const TClientRequestControlPtr& requestControl,
            const TStreamingPayload& payload)
        {
            VERIFY_THREAD_AFFINITY_ANY();

            if (TerminationFlag_.load()) {
                return MakeFuture(TError(NRpc::EErrorCode::TransportError, "Session is terminated"));
            }

            auto requestId = requestControl->GetRequestId();
            const auto& realmId = requestControl->GetRealmId();
            const auto& service = requestControl->GetService();
            const auto& method = requestControl->GetMethod();

            NProto::TStreamingPayloadHeader header;
            ToProto(header.mutable_request_id(), requestId);
            header.set_service(service);
            header.set_method(method);
            if (realmId) {
                ToProto(header.mutable_realm_id(), realmId);
            }
            header.set_sequence_number(payload.SequenceNumber);
            header.set_codec(static_cast<int>(payload.Codec));
            header.set_memory_zone(static_cast<int>(payload.MemoryZone));

            auto message = CreateStreamingPayloadMessage(header, payload.Attachments);
            NBus::TSendOptions options;
            options.TrackingLevel = EDeliveryTrackingLevel::Full;
            options.MemoryZone = payload.MemoryZone;
            return Bus_->Send(std::move(message), options);
        }

        TFuture<void> SendStreamingFeedback(
            const TClientRequestControlPtr& requestControl,
            const TStreamingFeedback& feedback)
        {
            VERIFY_THREAD_AFFINITY_ANY();

            if (TerminationFlag_.load()) {
                return MakeFuture(TError(NRpc::EErrorCode::TransportError, "Session is terminated"));
            }

            auto requestId = requestControl->GetRequestId();
            const auto& realmId = requestControl->GetRealmId();
            const auto& service = requestControl->GetService();
            const auto& method = requestControl->GetMethod();

            NProto::TStreamingFeedbackHeader header;
            ToProto(header.mutable_request_id(), requestId);
            header.set_service(service);
            header.set_method(method);
            if (realmId) {
                ToProto(header.mutable_realm_id(), realmId);
            }
            header.set_read_position(feedback.ReadPosition);

            auto message = CreateStreamingFeedbackMessage(header);
            NBus::TSendOptions options;
            options.TrackingLevel = EDeliveryTrackingLevel::Full;
            return Bus_->Send(std::move(message), options);
        }

        void HandleTimeout(const TClientRequestControlPtr& requestControl, bool aborted)
        {
            VERIFY_THREAD_AFFINITY_ANY();

            auto requestId = requestControl->GetRequestId();
            auto* bucket = GetBucketForRequest(requestId);

            IClientResponseHandlerPtr responseHandler;
            {
                auto guard = Guard(bucket->Lock);

                if (!requestControl->IsActive(guard)) {
                    return;
                }

                auto it = bucket->ActiveRequestMap.find(requestId);
                if (it != bucket->ActiveRequestMap.end() && requestControl == it->second) {
                    bucket->ActiveRequestMap.erase(it);
                } else {
                    YT_LOG_DEBUG("Timeout occurred for an unknown or resent request (RequestId: %v)",
                        requestId);
                }

                requestControl->ProfileTimeout();
                responseHandler = requestControl->Finalize(guard);
            }

            NotifyError(
                requestControl,
                responseHandler,
                TStringBuf("Request timed out"),
                TError(NYT::EErrorCode::Timeout, aborted
                    ? "Request timed out or timer was aborted"
                    : "Request timed out"));
        }

        void HandleAcknowledgementTimeout(const TClientRequestControlPtr& requestControl, bool aborted)
        {
            VERIFY_THREAD_AFFINITY_ANY();

            auto requestId = requestControl->GetRequestId();
            auto* bucket = GetBucketForRequest(requestId);

            IClientResponseHandlerPtr responseHandler;
            {
                auto guard = Guard(bucket->Lock);

                if (!requestControl->IsActive(guard)) {
                    return;
                }

                auto it = bucket->ActiveRequestMap.find(requestId);
                if (it != bucket->ActiveRequestMap.end() && requestControl == it->second) {
                    bucket->ActiveRequestMap.erase(it);
                } else {
                    YT_LOG_DEBUG("Acknowledgement timeout occurred for an unknown or resent request (RequestId: %v)",
                        requestId);
                }

                requestControl->ProfileTimeout();
                responseHandler = requestControl->Finalize(guard);
            }

            if (aborted) {
                return;
            }

            auto error = TError(NYT::EErrorCode::Timeout, "Request acknowledgement timed out");

            NotifyError(
                requestControl,
                responseHandler,
                TStringBuf("Request acknowledgement timed out"),
                error);

            if (TerminationFlag_.load()) {
                return;
            }

            Bus_->Terminate(error);
        }

        virtual void HandleMessage(TSharedRefArray message, IBusPtr /*replyBus*/) noexcept override
        {
            VERIFY_THREAD_AFFINITY_ANY();

            auto messageType = GetMessageType(message);
            switch (messageType) {
                case EMessageType::Response:
                    OnResponseMessage(std::move(message));
                    break;

                case EMessageType::StreamingPayload:
                    OnStreamingPayloadMessage(std::move(message));
                    break;

                case EMessageType::StreamingFeedback:
                    OnStreamingFeedbackMessage(std::move(message));
                    break;

                default:
                    YT_LOG_ERROR("Incoming message has invalid type, ignored (Type: %x)",
                        static_cast<ui32>(messageType));
                    break;
            }
        }

        //! Cached method metadata.
        struct TMethodMetadata
        {
            NProfiling::TEventTimer AckTimeCounter;
            NProfiling::TEventTimer ReplyTimeCounter;
            NProfiling::TEventTimer TimeoutTimeCounter;
            NProfiling::TEventTimer CancelTimeCounter;
            NProfiling::TEventTimer TotalTimeCounter;

            NProfiling::TCounter RequestCounter;
            NProfiling::TCounter RequestMessageBodySizeCounter;
            NProfiling::TCounter RequestMessageAttachmentSizeCounter;
            NProfiling::TCounter ResponseMessageBodySizeCounter;
            NProfiling::TCounter ResponseMessageAttachmentSizeCounter;
        };

        struct TMethodMetadataProfilingTrait
        {
            using TKey = std::pair<TString, TString>;
            using TValue = TMethodMetadata;

            static TKey ToKey(const TString& service, const TString& method)
            {
                return std::make_pair(service, method);
            }

            static TValue ToValue(const TString& service, const TString& method)
            {
                TMethodMetadata metadata;

                auto profiler = RpcClientProfiler
                    .WithHot()
                    .WithTag("yt_service", service)
                    .WithTag("method", method, -1);

                metadata.AckTimeCounter = profiler.Timer("/request_time/ack");
                metadata.ReplyTimeCounter = profiler.Timer("/request_time/reply");
                metadata.TimeoutTimeCounter = profiler.Timer("/request_time/timeout");
                metadata.CancelTimeCounter = profiler.Timer("/request_time/cancel");
                metadata.TotalTimeCounter = profiler.Timer("/request_time/total");
                metadata.RequestCounter = profiler.Counter("/request_count");
                metadata.RequestMessageBodySizeCounter = profiler.Counter("/request_message_body_bytes");
                metadata.RequestMessageAttachmentSizeCounter = profiler.Counter("/request_message_attachment_bytes");
                metadata.ResponseMessageBodySizeCounter = profiler.Counter("/response_message_body_bytes");
                metadata.ResponseMessageAttachmentSizeCounter = profiler.Counter("/response_message_attachment_bytes");

                return metadata;
            }
        };

        TMethodMetadata* GetMethodMetadata(const TString& service, const TString& method)
        {
            return &GetLocallyGloballyCachedValue<TMethodMetadataProfilingTrait>(service, method);
        }

    private:
        const TTosLevel TosLevel_;

        IBusPtr Bus_;

        struct TBucket
        {
            YT_DECLARE_SPINLOCK(TAdaptiveLock, Lock);
            IBusPtr Bus;
            bool Terminated = false;
            THashMap<TRequestId, TClientRequestControlPtr> ActiveRequestMap;
        };

        static constexpr size_t BucketCount = 64;

        std::array<TBucket, BucketCount> RequestBuckets_;

        std::atomic<bool> TerminationFlag_ = false;
        TAtomicObject<TError> TerminationError_;


        TBucket* GetBucketForRequest(TRequestId requestId)
        {
            return &RequestBuckets_[requestId.Parts32[0] % BucketCount];
        }

        IClientResponseHandlerPtr FindResponseHandler(TRequestId requestId)
        {
            VERIFY_THREAD_AFFINITY_ANY();

            auto* bucket = GetBucketForRequest(requestId);
            auto guard = Guard(bucket->Lock);

            auto it = bucket->ActiveRequestMap.find(requestId);
            if (it == bucket->ActiveRequestMap.end()) {
                return nullptr;
            }

            const auto& requestControl = it->second;
            return requestControl->GetResponseHandler(guard);
        }


        void OnRequestSerialized(
            const TClientRequestControlPtr& requestControl,
            const TSendOptions& options,
            TErrorOr<TSharedRefArray> requestMessageOrError)
        {
            VERIFY_THREAD_AFFINITY_ANY();

            if (requestMessageOrError.IsOK()) {
                auto requestMessageError = CheckBusMessageLimits(requestMessageOrError.Value());
                if (!requestMessageError.IsOK()){
                    requestMessageOrError = requestMessageError;
                }
            }

            auto requestId = requestControl->GetRequestId();
            auto* bucket = GetBucketForRequest(requestId);

            TClientRequestControlPtr existingRequestControl;
            IClientResponseHandlerPtr existingResponseHandler;
            {
                auto guard = Guard(bucket->Lock);

                if (!requestControl->IsActive(guard)) {
                    return;
                }

                if (!requestMessageOrError.IsOK()) {
                    auto responseHandler = requestControl->Finalize(guard);
                    guard.Release();

                    NotifyError(
                        requestControl,
                        responseHandler,
                        TStringBuf("Request serialization failed"),
                        TError(NRpc::EErrorCode::TransportError, "Request serialization failed")
                            << requestMessageOrError);
                    return;
                }

                if (bucket->Terminated) {
                    auto responseHandler = requestControl->Finalize(guard);
                    guard.Release();

                    NotifyError(
                        requestControl,
                        responseHandler,
                        TStringBuf("Request is dropped because channel is terminated"),
                        TError(NRpc::EErrorCode::TransportError, "Channel terminated")
                            << TerminationError_.Load());
                    return;
                }

                // NB: We're OK with duplicate request ids.
                auto [it, inserted] = bucket->ActiveRequestMap.emplace(requestId, requestControl);
                if (!inserted) {
                    existingRequestControl = std::move(it->second);
                    existingResponseHandler = existingRequestControl->Finalize(guard);
                    it->second = requestControl;
                }

                if (options.AcknowledgementTimeout) {
                    auto timeoutCookie = TDelayedExecutor::Submit(
                        BIND(&TSession::HandleAcknowledgementTimeout, MakeWeak(this), requestControl),
                        *options.AcknowledgementTimeout,
                        TDispatcher::Get()->GetHeavyInvoker());
                    requestControl->SetAcknowledgementTimeoutCookie(std::move(timeoutCookie));
                }
            }

            if (existingResponseHandler) {
                NotifyError(
                    existingRequestControl,
                    existingResponseHandler,
                    "Request resent",
                    TError(NRpc::EErrorCode::TransportError, "Request resent"));
            }

            if (options.SendDelay) {
                Sleep(*options.SendDelay);
            }

            const auto& requestMessage = requestMessageOrError.Value();

            NBus::TSendOptions busOptions;
            busOptions.TrackingLevel = options.AcknowledgementTimeout
                ? EDeliveryTrackingLevel::Full
                : EDeliveryTrackingLevel::ErrorOnly;
            busOptions.ChecksummedPartCount = options.GenerateAttachmentChecksums
                ? NBus::TSendOptions::AllParts
                : 2; // RPC header + request body
            busOptions.MemoryZone = options.MemoryZone;
            Bus_->Send(requestMessage, busOptions).Subscribe(BIND(
                &TSession::OnAcknowledgement,
                MakeStrong(this),
                options.AcknowledgementTimeout.has_value(),
                requestId));

            requestControl->ProfileRequest(requestMessage);

            YT_LOG_DEBUG("Request sent (RequestId: %v, Method: %v.%v, Timeout: %v, TrackingLevel: %v, "
                "ChecksummedPartCount: %v, MultiplexingBand: %v, Endpoint: %v, BodySize: %v, AttachmentsSize: %v)",
                requestId,
                requestControl->GetService(),
                requestControl->GetMethod(),
                requestControl->GetTimeout(),
                busOptions.TrackingLevel,
                busOptions.ChecksummedPartCount,
                options.MultiplexingBand,
                Bus_->GetEndpointDescription(),
                GetMessageBodySize(requestMessage),
                GetTotalMessageAttachmentSize(requestMessage));
        }


        void OnResponseMessage(TSharedRefArray message)
        {
            NProto::TResponseHeader header;
            if (!TryParseResponseHeader(message, &header)) {
                YT_LOG_ERROR("Error parsing response header");
                return;
            }

            auto requestId = FromProto<TRequestId>(header.request_id());
            auto* bucket = GetBucketForRequest(requestId);

            TClientRequestControlPtr requestControl;
            IClientResponseHandlerPtr responseHandler;
            {
                auto guard = Guard(bucket->Lock);

                if (bucket->Terminated) {
                    YT_LOG_WARNING("Response received via a terminated channel (RequestId: %v)",
                        requestId);
                    return;
                }

                auto it = bucket->ActiveRequestMap.find(requestId);
                if (it == bucket->ActiveRequestMap.end()) {
                    // This may happen when the other party responds to an already timed-out request.
                    YT_LOG_DEBUG("Response for an incorrect or obsolete request received (RequestId: %v)",
                        requestId);
                    return;
                }

                requestControl = std::move(it->second);
                requestControl->ProfileReply(message);
                responseHandler = requestControl->Finalize(guard);
                bucket->ActiveRequestMap.erase(it);
            }

            {
                TError error;
                if (header.has_error()) {
                    error = FromProto<TError>(header.error());
                }
                if (error.IsOK()) {
                    NotifyResponse(
                        requestId,
                        requestControl,
                        responseHandler,
                        std::move(message));
                } else {
                    if (error.GetCode() == EErrorCode::PoisonPill) {
                        YT_LOG_FATAL(error, "Poison pill received");
                    }
                    NotifyError(
                        requestControl,
                        responseHandler,
                        TStringBuf("Request failed"),
                        error);
                }
            }
        }

        void OnStreamingPayloadMessage(TSharedRefArray message)
        {
            NProto::TStreamingPayloadHeader header;
            if (!ParseStreamingPayloadHeader(message, &header)) {
                YT_LOG_ERROR("Error parsing streaming payload header");
                return;
            }

            auto requestId = FromProto<TRequestId>(header.request_id());
            auto sequenceNumber = header.sequence_number();
            auto attachments = std::vector<TSharedRef>(message.Begin() + 1, message.End());

            auto responseHandler = FindResponseHandler(requestId);
            if (!responseHandler) {
                YT_LOG_ERROR("Received streaming payload for an unknown request; ignored (RequestId: %v)",
                    requestId);
                return;
            }

            if (attachments.empty()) {
                responseHandler->HandleError(TError(
                    NRpc::EErrorCode::ProtocolError,
                    "Streaming payload without attachments"));
                return;
            }

            NCompression::ECodec codec;
            int intCodec = header.codec();
            if (!TryEnumCast(intCodec, &codec)) {
                responseHandler->HandleError(TError(
                    NRpc::EErrorCode::ProtocolError,
                    "Streaming payload codec %v is not supported",
                    intCodec));
                return;
            }

            EMemoryZone memoryZone;
            int intMemoryZone = header.memory_zone();
            if (!TryEnumCast(intMemoryZone, &memoryZone)) {
                responseHandler->HandleError(TError(
                    NRpc::EErrorCode::ProtocolError,
                    "Streaming payload memory zone %v is not supported",
                    intMemoryZone));
                return;
            }

            YT_LOG_DEBUG("Response streaming payload received (RequestId: %v, SequenceNumber: %v, Sizes: %v, "
                "Codec: %v, MemoryZone: %v, Closed: %v)",
                requestId,
                sequenceNumber,
                MakeFormattableView(attachments, [] (auto* builder, const auto& attachment) {
                    builder->AppendFormat("%v", GetStreamingAttachmentSize(attachment));
                }),
                codec,
                memoryZone,
                !attachments.back());

            TStreamingPayload payload{
                codec,
                memoryZone,
                sequenceNumber,
                std::move(attachments)
            };
            responseHandler->HandleStreamingPayload(payload);
        }

        void OnStreamingFeedbackMessage(TSharedRefArray message)
        {
            NProto::TStreamingFeedbackHeader header;
            if (!ParseStreamingFeedbackHeader(message, &header)) {
                YT_LOG_ERROR("Error parsing streaming feedback header");
                return;
            }

            auto requestId = FromProto<TRequestId>(header.request_id());
            auto readPosition = header.read_position();

            auto responseHandler = FindResponseHandler(requestId);
            if (!responseHandler) {
                YT_LOG_ERROR("Received streaming payload for an unknown request; ignored (RequestId: %v)",
                    requestId);
                return;
            }

            YT_LOG_DEBUG("Response streaming feedback received (RequestId: %v, ReadPosition: %v)",
                requestId,
                readPosition);

            TStreamingFeedback feedback{
                readPosition
            };
            responseHandler->HandleStreamingFeedback(feedback);
        }

        void OnAcknowledgement(bool requestAcknowledgement, TRequestId requestId, const TError& error)
        {
            VERIFY_THREAD_AFFINITY_ANY();

            if (!requestAcknowledgement && error.IsOK()) {
                return;
            }

            auto* bucket = GetBucketForRequest(requestId);

            TClientRequestControlPtr requestControl;
            IClientResponseHandlerPtr responseHandler;
            {
                auto guard = Guard(bucket->Lock);

                auto it = bucket->ActiveRequestMap.find(requestId);
                if (it == bucket->ActiveRequestMap.end()) {
                    // This one may easily get the actual response before the acknowledgment.
                    YT_LOG_DEBUG(error, "Acknowledgment received for an unknown request, ignored (RequestId: %v)",
                        requestId);
                    return;
                }

                requestControl = it->second;
                requestControl->ResetAcknowledgementTimeoutCookie();
                requestControl->ProfileAcknowledgement();
                if (!error.IsOK()) {
                    responseHandler = requestControl->Finalize(guard);
                    bucket->ActiveRequestMap.erase(it);
                } else {
                    responseHandler = requestControl->GetResponseHandler(guard);
                }
            }

            if (error.IsOK()) {
                NotifyAcknowledgement(requestId, responseHandler);
            } else {
                NotifyError(
                    requestControl,
                    responseHandler,
                    TStringBuf("Request acknowledgment failed"),
                    TError(NRpc::EErrorCode::TransportError, "Request acknowledgment failed")
                         << error);
            }
        }

        void NotifyError(
            const TClientRequestControlPtr& requestControl,
            const IClientResponseHandlerPtr& responseHandler,
            TStringBuf reason,
            const TError& error) noexcept
        {
            YT_VERIFY(responseHandler);

            auto detailedError = error
                << TErrorAttribute("realm_id", requestControl->GetRealmId())
                << TErrorAttribute("service", requestControl->GetService())
                << TErrorAttribute("method", requestControl->GetMethod())
                << TErrorAttribute("request_id", requestControl->GetRequestId())
                << Bus_->GetEndpointAttributes();

            if (requestControl->GetTimeout()) {
                detailedError = detailedError
                    << TErrorAttribute("timeout", *requestControl->GetTimeout());
            }

            YT_LOG_DEBUG("%v (RequestId: %v)",
                reason,
                requestControl->GetRequestId());

            responseHandler->HandleError(detailedError);
        }

        void NotifyAcknowledgement(
            TRequestId requestId,
            const IClientResponseHandlerPtr& responseHandler) noexcept
        {
            YT_LOG_DEBUG("Request acknowledged (RequestId: %v)", requestId);

            responseHandler->HandleAcknowledgement();
        }

        void NotifyResponse(
            TRequestId requestId,
            const TClientRequestControlPtr& requestControl,
            const IClientResponseHandlerPtr& responseHandler,
            TSharedRefArray message) noexcept
        {
            YT_LOG_DEBUG("Response received (RequestId: %v, Method: %v.%v, TotalTime: %v)",
                requestId,
                requestControl->GetService(),
                requestControl->GetMethod(),
                requestControl->GetTotalTime());

            responseHandler->HandleResponse(std::move(message));
        }
    };

    //! Controls a sent request.
    class TClientRequestControl
        : public IClientRequestControl
    {
    public:
        TClientRequestControl(
            TSessionPtr session,
            IClientRequestPtr request,
            const TSendOptions& options,
            IClientResponseHandlerPtr responseHandler)
            : Session_(std::move(session))
            , RealmId_(request->GetRealmId())
            , Service_(request->GetService())
            , Method_(request->GetMethod())
            , RequestId_(request->GetRequestId())
            , Options_(options)
            , MethodMetadata_(Session_->GetMethodMetadata(Service_, Method_))
            , ResponseHandler_(std::move(responseHandler))
        { }

        ~TClientRequestControl()
        {
            TDelayedExecutor::CancelAndClear(TimeoutCookie_);
            TDelayedExecutor::CancelAndClear(AcknowledgementTimeoutCookie_);
        }

        TRealmId GetRealmId() const
        {
            return RealmId_;
        }

        const TString& GetService() const
        {
            return Service_;
        }

        const TString& GetMethod() const
        {
            return Method_;
        }

        TRequestId GetRequestId() const
        {
            return RequestId_;
        }

        std::optional<TDuration> GetTimeout() const
        {
            return Options_.Timeout;
        }

        TDuration GetTotalTime() const
        {
            return TotalTime_;
        }

        bool IsActive(const TSpinlockGuard<TAdaptiveLock>&) const
        {
            return static_cast<bool>(ResponseHandler_);
        }

        void SetTimeoutCookie(TDelayedExecutorCookie cookie)
        {
            YT_ASSERT(!TimeoutCookie_);
            TimeoutCookie_ = std::move(cookie);
        }

        void SetAcknowledgementTimeoutCookie(TDelayedExecutorCookie cookie)
        {
            YT_ASSERT(!AcknowledgementTimeoutCookie_);
            AcknowledgementTimeoutCookie_ = std::move(cookie);
        }

        void ResetAcknowledgementTimeoutCookie()
        {
            TDelayedExecutor::CancelAndClear(AcknowledgementTimeoutCookie_);
        }

        IClientResponseHandlerPtr GetResponseHandler(const TSpinlockGuard<TAdaptiveLock>&)
        {
            return ResponseHandler_;
        }

        IClientResponseHandlerPtr Finalize(const TSpinlockGuard<TAdaptiveLock>&)
        {
            TotalTime_ = DoProfile(MethodMetadata_->TotalTimeCounter);
            TDelayedExecutor::CancelAndClear(TimeoutCookie_);
            TDelayedExecutor::CancelAndClear(AcknowledgementTimeoutCookie_);
            return std::move(ResponseHandler_);
        }

        void ProfileRequest(const TSharedRefArray& requestMessage)
        {
            MethodMetadata_->RequestCounter.Increment();
            MethodMetadata_->RequestMessageBodySizeCounter.Increment(
                GetMessageBodySize(requestMessage));
            MethodMetadata_->RequestMessageAttachmentSizeCounter.Increment(
                GetTotalMessageAttachmentSize(requestMessage));
        }

        void ProfileReply(const TSharedRefArray& responseMessage)
        {
            DoProfile(MethodMetadata_->ReplyTimeCounter);

            MethodMetadata_->ResponseMessageBodySizeCounter.Increment(
                GetMessageBodySize(responseMessage));
            MethodMetadata_->ResponseMessageAttachmentSizeCounter.Increment(
                GetTotalMessageAttachmentSize(responseMessage));
        }

        void ProfileAcknowledgement()
        {
            DoProfile(MethodMetadata_->AckTimeCounter);
        }

        void ProfileCancel()
        {
            DoProfile(MethodMetadata_->CancelTimeCounter);
        }

        void ProfileTimeout()
        {
            DoProfile(MethodMetadata_->TimeoutTimeCounter);
        }

        // IClientRequestControl overrides
        virtual void Cancel() override
        {
            Session_->Cancel(this);
        }

        virtual TFuture<void> SendStreamingPayload(const TStreamingPayload& payload) override
        {
            return Session_->SendStreamingPayload(this, payload);
        }

        virtual TFuture<void> SendStreamingFeedback(const TStreamingFeedback& feedback) override
        {
            return Session_->SendStreamingFeedback(this, feedback);
        }

    private:
        const TSessionPtr Session_;
        const TRealmId RealmId_;
        const TString Service_;
        const TString Method_;
        const TRequestId RequestId_;
        const TSendOptions Options_;
        TSession::TMethodMetadata* const MethodMetadata_;

        TDelayedExecutorCookie TimeoutCookie_;
        TDelayedExecutorCookie AcknowledgementTimeoutCookie_;
        IClientResponseHandlerPtr ResponseHandler_;

        NProfiling::TWallTimer Timer_;
        TDuration TotalTime_;

        TDuration DoProfile(NProfiling::TEventTimer& counter)
        {
            auto elapsed = Timer_.GetElapsedTime();
            counter.Record(elapsed);
            return elapsed;
        }
    };
};

IChannelPtr CreateBusChannel(IBusClientPtr client)
{
    YT_VERIFY(client);

    return New<TBusChannel>(std::move(client));
}

////////////////////////////////////////////////////////////////////////////////

class TBusChannelFactory
    : public IChannelFactory
{
public:
    explicit TBusChannelFactory(TTcpBusConfigPtr config)
        : Config_(ConvertToNode(std::move(config)))
    { }

    virtual IChannelPtr CreateChannel(const TString& address) override
    {
        auto config = TTcpBusClientConfig::CreateTcp(address);
        config->Load(Config_, true, false);
        auto client = CreateTcpBusClient(std::move(config));
        return CreateBusChannel(std::move(client));
    }

    virtual IChannelPtr CreateChannel(const TAddressWithNetwork& addressWithNetwork) override
    {
        auto config = TTcpBusClientConfig::CreateTcp(addressWithNetwork.Address, addressWithNetwork.Network);
        config->Load(Config_, true, false);
        auto client = CreateTcpBusClient(std::move(config));
        return CreateBusChannel(std::move(client));
    }

private:
    const INodePtr Config_;
};

IChannelFactoryPtr CreateBusChannelFactory(TTcpBusConfigPtr config)
{
    return New<TBusChannelFactory>(std::move(config));
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NRpc::NBus
