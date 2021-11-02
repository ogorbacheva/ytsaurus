#include "error.h"
#include "serialize.h"

#include <yt/yt/core/concurrency/scheduler.h>

#include <yt/yt_proto/yt/core/misc/proto/error.pb.h>

#include <yt/yt/core/misc/protobuf_helpers.h>
#include <yt/yt/core/misc/proc.h>

#include <yt/yt/core/net/local_address.h>

#include <yt/yt/core/tracing/trace_context.h>

#include <yt/yt/core/yson/tokenizer.h>

#include <yt/yt/core/ytree/convert.h>
#include <yt/yt/core/ytree/fluent.h>

#include <yt/yt/core/net/address.h>

#include <util/system/error.h>

namespace NYT {

using namespace NYTree;
using namespace NYson;

using NYT::FromProto;
using NYT::ToProto;

////////////////////////////////////////////////////////////////////////////////

void TErrorCode::Save(TStreamSaveContext& context) const
{
    NYT::Save(context, Value_);
}

void TErrorCode::Load(TStreamLoadContext& context)
{
    NYT::Load(context, Value_);
}

void FormatValue(TStringBuilderBase* builder, TErrorCode code, TStringBuf spec)
{
    FormatValue(builder, static_cast<int>(code), spec);
}

TString ToString(TErrorCode code)
{
    return ToStringViaBuilder(code);
}

////////////////////////////////////////////////////////////////////////////////

class TError::TImpl
{
public:
    TImpl()
        : Code_(NYT::EErrorCode::OK)
    { }

    TImpl(const TError::TImpl& other)
        : Code_(other.Code_)
        , Message_(other.Message_)
        , Host_(other.Host_)
        , HostHolder_(other.HostHolder_)
        , Datetime_(other.Datetime_)
        , Pid_(other.Pid_)
        , Tid_(other.Tid_)
        , Fid_(other.Fid_)
        , TraceId_(other.TraceId_)
        , SpanId_(other.SpanId_)
        , Attributes_(other.Attributes_ ? other.Attributes_->Clone() : nullptr)
        , InnerErrors_(other.InnerErrors_)
    { }

    explicit TImpl(TString message)
        : Code_(NYT::EErrorCode::Generic)
        , Message_(std::move(message))
    {
        CaptureOriginAttributes();
    }

    TImpl(TErrorCode code, TString message)
        : Code_(code)
        , Message_(std::move(message))
    {
        if (!IsOK()) {
            CaptureOriginAttributes();
        }
    }

    TErrorCode GetCode() const
    {
        return Code_;
    }

    void SetCode(TErrorCode code)
    {
        Code_ = code;
    }

    const TString& GetMessage() const
    {
        return Message_;
    }

    void SetMessage(TString message)
    {
        Message_ = std::move(message);
    }

    bool HasOriginAttributes() const
    {
        return Host_.operator bool();
    }

    TStringBuf GetHost() const
    {
        return Host_;
    }

    bool HasDatetime() const
    {
        return Datetime_ != TInstant();
    }

    TInstant GetDatetime() const
    {
        return Datetime_;
    }

    void SetDatetime(TInstant datetime)
    {
        Datetime_ = datetime;
    }

    TProcessId GetPid() const
    {
        return Pid_;
    }

    NConcurrency::TThreadId GetTid() const
    {
        return Tid_;
    }

    NConcurrency::TFiberId GetFid() const
    {
        return Fid_;
    }

    bool HasTracingAttributes() const
    {
        return TraceId_ != NTracing::InvalidTraceId;
    }

    NTracing::TTraceId GetTraceId() const
    {
        return TraceId_;
    }

    NTracing::TSpanId GetSpanId() const
    {
        return SpanId_;
    }

    const IAttributeDictionary& Attributes() const
    {
        if (!Attributes_) {
            return EmptyAttributes();
        }
        return *Attributes_;
    }

    IAttributeDictionary* MutableAttributes()
    {
        if (!Attributes_) {
            Attributes_ = CreateEphemeralAttributes();
        }
        return Attributes_.Get();
    }

    bool HasAttributes() const
    {
        return Attributes_.operator bool();
    }

    void SetAttributes(NYTree::IAttributeDictionaryPtr attributes)
    {
        Attributes_ = std::move(attributes);
        ExtractSystemAttributes();
    }

    const std::vector<TError>& InnerErrors() const
    {
        return InnerErrors_;
    }

    std::vector<TError>* MutableInnerErrors()
    {
        return &InnerErrors_;
    }

    bool IsOK() const
    {
        return Code_ == NYT::EErrorCode::OK;
    }

private:
    TErrorCode Code_;
    TString Message_;
    // Most errors are local; for these Host_ refers to a static buffer and HostHolder_ is not used.
    // This saves one allocation on TError construction.
    TStringBuf Host_;
    TString HostHolder_;
    TInstant Datetime_;
    TProcessId Pid_ = 0;
    NConcurrency::TThreadId Tid_ = NConcurrency::InvalidThreadId;
    NConcurrency::TFiberId Fid_ = NConcurrency::InvalidFiberId;
    NTracing::TTraceId TraceId_;
    NTracing::TSpanId SpanId_;
    NYTree::IAttributeDictionaryPtr Attributes_;
    std::vector<TError> InnerErrors_;


    void CaptureOriginAttributes()
    {
        Host_ = NNet::ReadLocalHostName();
        Datetime_ = TInstant::Now();
        Pid_ = GetPID();
        Tid_ = TThread::CurrentThreadId();
        Fid_ = NConcurrency::GetCurrentFiberId();
        if (const auto* traceContext = NTracing::GetCurrentTraceContext()) {
            TraceId_ = traceContext->GetTraceId();
            SpanId_ = traceContext->GetSpanId();
        }
    }

    void ExtractSystemAttributes()
    {
        if (!Attributes_) {
            return;
        }

        static const TString HostKey("host");
        HostHolder_ = Attributes_->GetAndRemove<TString>(HostKey, TString());
        Host_ = HostHolder_.empty() ? TStringBuf() : HostHolder_;

        static const TString DatetimeKey("datetime");
        Datetime_ = Attributes_->GetAndRemove<TInstant>(DatetimeKey, TInstant());

        static const TString PidKey("pid");
        Pid_ = Attributes_->GetAndRemove<TProcessId>(PidKey, 0);

        static const TString TidKey("tid");
        Tid_ = Attributes_->GetAndRemove<NConcurrency::TThreadId>(TidKey, NConcurrency::InvalidThreadId);

        static const TString FidKey("fid");
        Fid_ = Attributes_->GetAndRemove<NConcurrency::TFiberId>(FidKey, NConcurrency::InvalidFiberId);

        static const TString TraceIdKey("trace_id");
        // COMPAT(babenko): some older versions use uint64 for trace id.
        try {
            TraceId_ = Attributes_->GetAndRemove<NTracing::TTraceId>(TraceIdKey, NTracing::InvalidTraceId);
        } catch (const std::exception&) {
            TraceId_ = NTracing::TTraceId(Attributes_->GetAndRemove<ui64>(TraceIdKey, 0), 0);
        }

        static const TString SpanIdKey("span_id");
        SpanId_ = Attributes_->GetAndRemove<NTracing::TSpanId>(SpanIdKey, NTracing::InvalidSpanId);
    }
};

////////////////////////////////////////////////////////////////////////////////

TError::TErrorOr() = default;

TError::~TErrorOr() = default;

TError::TErrorOr(const TError& other)
{
    if (!other.IsOK()) {
        Impl_ = std::make_unique<TImpl>(*other.Impl_);
    }
}

TError::TErrorOr(TError&& other) noexcept
    : Impl_(std::move(other.Impl_))
{ }

TError::TErrorOr(const std::exception& ex)
{
    const auto* errorEx = dynamic_cast<const TErrorException*>(&ex);
    if (errorEx) {
        *this = errorEx->Error();
    } else {
        *this = TError(NYT::EErrorCode::Generic, ex.what());
    }
}

TError::TErrorOr(TString message)
    : Impl_(std::make_unique<TImpl>(std::move(message)))
{ }

TError::TErrorOr(TErrorCode code, TString message)
    : Impl_(std::make_unique<TImpl>(code, std::move(message)))
{ }

TError& TError::operator = (const TError& other)
{
    if (other.IsOK()) {
        Impl_.reset();
    } else {
        Impl_ = std::make_unique<TImpl>(*other.Impl_);
    }
    return *this;
}

TError& TError::operator = (TError&& other) noexcept
{
    Impl_ = std::move(other.Impl_);
    return *this;
}

TError TError::FromSystem()
{
    return FromSystem(LastSystemError());
}

TError TError::FromSystem(int error)
{
    return TError(LinuxErrorCodeBase + error, LastSystemErrorText(error)) <<
        TErrorAttribute("errno", error);
}

TError TError::FromSystem(const TSystemError& error)
{
    return FromSystem(error.Status());
}

TErrorCode TError::GetCode() const
{
    if (!Impl_) {
        return NYT::EErrorCode::OK;
    }
    return Impl_->GetCode();
}

TError& TError::SetCode(TErrorCode code)
{
    MakeMutable();
    Impl_->SetCode(code);
    return *this;
}

TErrorCode TError::GetNonTrivialCode() const
{
    if (!Impl_) {
        return NYT::EErrorCode::OK;
    }

    if (GetCode() != NYT::EErrorCode::Generic) {
        return GetCode();
    }

    for (const auto& innerError : InnerErrors()) {
        auto innerCode = innerError.GetNonTrivialCode();
        if (innerCode != NYT::EErrorCode::Generic) {
            return innerCode;
        }
    }

    return GetCode();
}

THashSet<TErrorCode> TError::GetDistinctNonTrivialErrorCodes() const
{
    THashSet<TErrorCode> result;
    TraverseError(*this, [&result] (const TError& error, int /*depth*/) {
        if (auto errorCode = error.GetCode(); errorCode != NYT::EErrorCode::OK) {
            result.insert(errorCode);
        }
    });
    return result;
}

const TString& TError::GetMessage() const
{
    if (!Impl_) {
        static const TString Result;
        return Result;
    }
    return Impl_->GetMessage();
}

TError& TError::SetMessage(TString message)
{
    MakeMutable();
    Impl_->SetMessage(std::move(message));
    return *this;
}

bool TError::HasOriginAttributes() const
{
    if (!Impl_) {
        return false;
    }
    return Impl_->HasOriginAttributes();
}

TStringBuf TError::GetHost() const
{
    if (!Impl_) {
        return {};
    }
    return Impl_->GetHost();
}

bool TError::HasDatetime() const
{
    if (!Impl_) {
        return false;
    }
    return Impl_->HasDatetime();
}

TInstant TError::GetDatetime() const
{
    if (!Impl_) {
        return {};
    }
    return Impl_->GetDatetime();
}

TProcessId TError::GetPid() const
{
    if (!Impl_) {
        return 0;
    }
    return Impl_->GetPid();
}

NConcurrency::TThreadId TError::GetTid() const
{
    if (!Impl_) {
        return NConcurrency::InvalidThreadId;
    }
    return Impl_->GetTid();
}

NConcurrency::TFiberId TError::GetFid() const
{
    if (!Impl_) {
        return NConcurrency::InvalidFiberId;
    }
    return Impl_->GetFid();
}

bool TError::HasTracingAttributes() const
{
    if (!Impl_) {
        return false;
    }
    return Impl_->HasTracingAttributes();
}

NTracing::TTraceId TError::GetTraceId() const
{
    if (!Impl_) {
        return NTracing::InvalidTraceId;
    }
    return Impl_->GetTraceId();
}

NTracing::TSpanId TError::GetSpanId() const
{
    if (!Impl_) {
        return NTracing::InvalidSpanId;
    }
    return Impl_->GetSpanId();
}

const IAttributeDictionary& TError::Attributes() const
{
    if (!Impl_) {
        return EmptyAttributes();
    }
    return Impl_->Attributes();
}

IAttributeDictionary* TError::MutableAttributes()
{
    MakeMutable();
    return Impl_->MutableAttributes();
}

const std::vector<TError>& TError::InnerErrors() const
{
    if (!Impl_) {
        static const std::vector<TError> Result;
        return Result;
    }
    return Impl_->InnerErrors();
}

std::vector<TError>* TError::MutableInnerErrors()
{
    MakeMutable();
    return Impl_->MutableInnerErrors();
}

TError TError::Sanitize() const
{
    if (!Impl_) {
        return {};
    }

    auto result = std::make_unique<TImpl>();
    result->SetCode(GetCode());
    result->SetMessage(GetMessage());
    if (Impl_->HasAttributes()) {
        result->SetAttributes(Impl_->Attributes().Clone());
    }
    for (const auto& innerError : Impl_->InnerErrors()) {
        result->MutableInnerErrors()->push_back(innerError.Sanitize());
    }

    return TError(std::move(result));
}

TError TError::Sanitize(TInstant datetime) const
{
    if (!Impl_) {
        return TError();
    }

    auto result = std::make_unique<TImpl>();
    result->SetCode(GetCode());
    result->SetMessage(GetMessage());
    result->SetDatetime(datetime);
    if (Impl_->HasAttributes()) {
        result->SetAttributes(Impl_->Attributes().Clone());
    }
    for (const auto& innerError : Impl_->InnerErrors()) {
        result->MutableInnerErrors()->push_back(innerError.Sanitize(datetime));
    }

    return TError(std::move(result));
}

TError TError::Truncate(int maxInnerErrorCount, i64 stringLimit) const
{
    static const TString InnerErrorsTruncatedKey("inner_errors_truncated");

    if (!Impl_) {
        return TError();
    }

    auto truncateInnerError = [=] (const TError& innerError) {
        return innerError.Truncate(maxInnerErrorCount, stringLimit);
    };

    auto truncateString = [stringLimit] (TString string) {
        if (std::ssize(string) > stringLimit) {
            return Format("%v...<message truncated>", string.substr(0, stringLimit));
        }
        return string;
    };

    auto truncateAttributes = [stringLimit] (const IAttributeDictionary& attributes) {
        auto clonedAttributes = attributes.Clone();
        for (const auto& key : clonedAttributes->ListKeys()) {
            if (std::ssize(clonedAttributes->FindYson(key).AsStringBuf()) > stringLimit) {
                clonedAttributes->SetYson(
                    key,
                    BuildYsonStringFluently()
                        .Value("...<attribute truncated>..."));
            }
        }
        return clonedAttributes;
    };

    auto result = std::make_unique<TImpl>();
    result->SetCode(GetCode());
    result->SetMessage(truncateString(GetMessage()));
    if (Impl_->HasAttributes()) {
        result->SetAttributes(truncateAttributes(Attributes()));
    }

    if (std::ssize(InnerErrors()) <= maxInnerErrorCount) {
        for (const auto& innerError : InnerErrors()) {
            result->MutableInnerErrors()->push_back(truncateInnerError(innerError));
        }
    } else {
        result->MutableAttributes()->Set(InnerErrorsTruncatedKey, true);
        for (int i = 0; i + 1 < maxInnerErrorCount; ++i) {
            result->MutableInnerErrors()->push_back(truncateInnerError(InnerErrors()[i]));
        }
        result->MutableInnerErrors()->push_back(truncateInnerError(InnerErrors().back()));
    }

    return TError(std::move(result));
}

bool TError::IsOK() const
{
    if (!Impl_) {
        return true;
    }
    return Impl_->IsOK();
}

void TError::ThrowOnError() const
{
    if (!IsOK()) {
        THROW_ERROR *this;
    }
}

TError TError::Wrap() const
{
    return *this;
}

Y_WEAK TString GetErrorSkeleton(const TError& /*error*/)
{
    // Proper implementation resides in yt/yt/library/error_skeleton/skeleton.cpp.
    THROW_ERROR_EXCEPTION("Error skeleton implementation library is not linked; consider PEERDIR'ing yt/yt/library/error_skeleton");
}

TString TError::GetSkeleton() const
{
    return GetErrorSkeleton(*this);
}

void TError::Save(TStreamSaveContext& context) const
{
    using NYT::Save;

    if (!Impl_) {
        // Fast path.
        Save(context, TErrorCode(NYT::EErrorCode::OK)); // code
        Save(context, TStringBuf());                    // message
        Save(context, IAttributeDictionaryPtr());       // attributes
        Save(context, std::vector<TError>());           // inner errors
        return;
    }

    Save(context, GetCode());
    Save(context, GetMessage());

    // Cf. TAttributeDictionaryValueSerializer.
    auto attributePairs = Attributes().ListPairs();
    size_t attributeCount = attributePairs.size();
    if (HasOriginAttributes()) {
        attributeCount += 4;
    }
    if (HasDatetime()) {
        attributeCount += 1;
    }
    if (HasTracingAttributes()) {
        attributeCount += 2;
    }

    if (attributeCount > 0) {
        // Cf. TAttributeDictionaryRefSerializer.
        Save(context, true);

        TSizeSerializer::Save(context, attributeCount);

        auto saveAttribute = [&] (const TString& key, const auto& value) {
            Save(context, key);
            Save(context, ConvertToYsonString(value));
        };

        if (HasOriginAttributes()) {
            static const TString HostKey("host");
            saveAttribute(HostKey, GetHost());

            static const TString PidKey("pid");
            saveAttribute(PidKey, GetPid());

            static const TString TidKey("tid");
            saveAttribute(TidKey, GetTid());

            static const TString FidKey("fid");
            saveAttribute(FidKey, GetFid());
        }

        if (HasDatetime()) {
            static const TString DatetimeKey("datetime");
            saveAttribute(DatetimeKey, GetDatetime());
        }

        if (HasTracingAttributes()) {
            static const TString TraceIdKey("trace_id");
            saveAttribute(TraceIdKey, GetTraceId());

            static const TString SpanIdKey("span_id");
            saveAttribute(SpanIdKey, GetSpanId());
        }

        std::sort(attributePairs.begin(), attributePairs.end(), [] (const auto& lhs, const auto& rhs) {
            return lhs.first < rhs.first;
        });
        for (const auto& [key, value] : attributePairs) {
            Save(context, key);
            Save(context, value);
        }
    } else {
        Save(context, false);
    }

    Save(context, InnerErrors());
}

void TError::Load(TStreamLoadContext& context)
{
    Impl_.reset();

    using NYT::Load;

    auto code = Load<TErrorCode>(context);
    auto message = Load<TString>(context);

    IAttributeDictionaryPtr attributes;
    if (Load<bool>(context)) {
        attributes = CreateEphemeralAttributes();
        TAttributeDictionarySerializer::LoadNonNull(context, attributes);
    }

    auto innerErrors = Load<std::vector<TError>>(context);

    if (code == NYT::EErrorCode::OK) {
        // Fast path.
        // Note that there were no allocations above.
        return;
    }

    auto impl = std::make_unique<TImpl>();
    impl->SetCode(code);
    impl->SetMessage(std::move(message));
    impl->SetAttributes(std::move(attributes));
    *impl->MutableInnerErrors() = std::move(innerErrors);
    Impl_ = std::move(impl);
}

std::optional<TError> TError::FindMatching(TErrorCode code) const
{
    if (!Impl_) {
        return {};
    }

    if (GetCode() == code) {
        return *this;
    }

    for (const auto& innerError : InnerErrors()) {
        auto innerResult = innerError.FindMatching(code);
        if (innerResult) {
            return innerResult;
        }
    }

    return {};
}

TError::TErrorOr(std::unique_ptr<TImpl> impl)
    : Impl_(std::move(impl))
{ }

void TError::MakeMutable()
{
    if (!Impl_) {
        Impl_ = std::make_unique<TImpl>();
    }
}

////////////////////////////////////////////////////////////////////////////////

namespace {

void AppendIndent(TStringBuilderBase* builer, int indent)
{
    builer->AppendChar(' ', indent);
}

void AppendAttribute(TStringBuilderBase* builder, const TString& key, const TString& value, int indent)
{
    AppendIndent(builder, indent + 4);
    builder->AppendFormat("%-15s %s", key, value);
    builder->AppendChar('\n');
}

void AppendError(TStringBuilderBase* builder, const TError& error, int indent)
{
    if (error.IsOK()) {
        builder->AppendString("OK");
        return;
    }

    AppendIndent(builder, indent);
    builder->AppendString(error.GetMessage());
    builder->AppendChar('\n');

    if (error.GetCode() != NYT::EErrorCode::Generic) {
        AppendAttribute(builder, "code", ToString(static_cast<int>(error.GetCode())), indent);
    }

    // Pretty-print origin.
    if (error.HasOriginAttributes()) {
        AppendAttribute(
            builder,
            "origin",
            Format("%v (pid %v, tid %llx, fid %llx)",
                error.GetHost(),
                error.GetPid(),
                error.GetTid(),
                error.GetFid()),
            indent);
    }

    if (error.HasDatetime()) {
        AppendAttribute(
            builder,
            "datetime",
            Format("%v", error.GetDatetime()),
            indent);
    }

    for (const auto& [key, value] : error.Attributes().ListPairs()) {
        TTokenizer tokenizer(value.AsStringBuf());
        YT_VERIFY(tokenizer.ParseNext());
        switch (tokenizer.GetCurrentType()) {
            case ETokenType::String:
                AppendAttribute(builder, key, TString(tokenizer.CurrentToken().GetStringValue()), indent);
                break;
            case ETokenType::Int64:
                AppendAttribute(builder, key, ToString(tokenizer.CurrentToken().GetInt64Value()), indent);
                break;
            case ETokenType::Uint64:
                AppendAttribute(builder, key, ToString(tokenizer.CurrentToken().GetUint64Value()), indent);
                break;
            case ETokenType::Double:
                AppendAttribute(builder, key, ToString(tokenizer.CurrentToken().GetDoubleValue()), indent);
                break;
            case ETokenType::Boolean:
                AppendAttribute(builder, key, TString(FormatBool(tokenizer.CurrentToken().GetBooleanValue())), indent);
                break;
            default:
                AppendAttribute(builder, key, ConvertToYsonString(value, EYsonFormat::Text).ToString(), indent);
                break;
        }
    }

    for (const auto& innerError : error.InnerErrors()) {
        builder->AppendChar('\n');
        AppendError(builder, innerError, indent + 2);
    }
}

} // namespace

bool operator == (const TError& lhs, const TError& rhs)
{
    if (!lhs.Impl_ && !rhs.Impl_) {
        return true;
    }
    return
        lhs.GetCode() == rhs.GetCode() &&
        lhs.GetMessage() == rhs.GetMessage() &&
        lhs.GetHost() == rhs.GetHost() &&
        lhs.GetDatetime() == rhs.GetDatetime() &&
        lhs.GetPid() == rhs.GetPid() &&
        lhs.GetTid() == rhs.GetTid() &&
        lhs.GetFid() == rhs.GetFid() &&
        lhs.GetTraceId() == rhs.GetTraceId() &&
        lhs.GetSpanId() == rhs.GetSpanId() &&
        lhs.Attributes() == rhs.Attributes() &&
        lhs.InnerErrors() == rhs.InnerErrors();
}

bool operator != (const TError& lhs, const TError& rhs)
{
    return !(lhs == rhs);
}

void FormatValue(TStringBuilderBase* builder, const TError& error, TStringBuf /*spec*/)
{
    AppendError(builder, error, 0);
}

TString ToString(const TError& error)
{
    TStringBuilder builder;
    AppendError(&builder, error, 0);
    return builder.Flush();
}

void ToProto(NYT::NProto::TError* protoError, const TError& error)
{
    if (!error.Impl_) {
        protoError->set_code(static_cast<int>(NYT::EErrorCode::OK));
        protoError->clear_message();
        return;
    }

    protoError->set_code(error.GetCode());
    protoError->set_message(error.GetMessage());

    protoError->clear_attributes();
    if (error.Impl_->HasAttributes()) {
        ToProto(protoError->mutable_attributes(), error.Attributes());
    }

    auto addAttribute = [&] (const TString& key, const auto& value) {
        auto* protoItem = protoError->mutable_attributes()->add_attributes();
        protoItem->set_key(key);
        protoItem->set_value(ConvertToYsonString(value).ToString());
    };

    if (error.HasOriginAttributes()) {
        static const TString HostKey("host");
        addAttribute(HostKey, error.GetHost());

        static const TString PidKey("pid");
        addAttribute(PidKey, error.GetPid());

        static const TString TidKey("tid");
        addAttribute(TidKey, error.GetTid());

        static const TString FidKey("fid");
        addAttribute(FidKey, error.GetFid());
    }

    if (error.HasDatetime()) {
        static const TString DatetimeKey("datetime");
        addAttribute(DatetimeKey, error.GetDatetime());
    }

    if (error.HasTracingAttributes()) {
        static const TString TraceIdKey("trace_id");
        addAttribute(TraceIdKey, error.GetTraceId());

        static const TString SpanIdKey("span_id");
        addAttribute(SpanIdKey, error.GetSpanId());
    }

    protoError->clear_inner_errors();
    for (const auto& innerError : error.InnerErrors()) {
        ToProto(protoError->add_inner_errors(), innerError);
    }
}

void FromProto(TError* error, const NYT::NProto::TError& protoError)
{
    *error = {};

    if (protoError.code() == static_cast<int>(NYT::EErrorCode::OK)) {
        return;
    }

    error->SetCode(protoError.code());
    error->SetMessage(protoError.message());
    if (protoError.has_attributes()) {
        error->Impl_->SetAttributes(FromProto(protoError.attributes()));
    } else {
        error->Impl_->SetAttributes(nullptr);
    }
    *error->MutableInnerErrors() = FromProto<std::vector<TError>>(protoError.inner_errors());
}

void TraverseError(const TError& error, const TErrorVisitor& visitor, int depth)
{
    visitor(error, depth);
    for (const auto& inner : error.InnerErrors()) {
        TraverseError(inner, visitor, depth + 1);
    }
}

namespace {

// Errors whose depth exceeds |ErrorSerializationDepthLimit| are serialized
// as children of their ancestor on depth |ErrorSerializationDepthLimit - 1|.
void SerializeInnerErrors(TFluentMap fluent, const TError& error, int depth)
{
    if (depth >= ErrorSerializationDepthLimit) {
        // Ignore deep inner errors.
        return;
    }

    auto visit = [&] (auto fluent, const TError& error, int depth) {
        fluent
            .Item().Do([&] (auto fluent) {
                Serialize(error, fluent.GetConsumer(), /* valueProduce */ nullptr, depth);
            });
    };

    fluent
        .Item("inner_errors").DoListFor(error.InnerErrors(), [&] (auto fluent, const TError& innerError) {
            if (depth < ErrorSerializationDepthLimit - 1) {
                visit(fluent, innerError, depth + 1);
            } else {
                YT_VERIFY(depth == ErrorSerializationDepthLimit - 1);
                TraverseError(
                    innerError,
                    [&] (const TError& e, int depth) {
                        visit(fluent, e, depth);
                    },
                    depth + 1);
            }
        });
}

} // namespace

void Serialize(
    const TError& error,
    IYsonConsumer* consumer,
    const std::function<void(IYsonConsumer*)>* valueProducer,
    int depth)
{
    BuildYsonFluently(consumer)
        .BeginMap()
            .Item("code").Value(error.GetCode())
            .Item("message").Value(error.GetMessage())
            .Item("attributes").DoMap([&] (auto fluent) {
                if (error.HasOriginAttributes()) {
                    fluent
                        .Item("host").Value(error.GetHost())
                        .Item("pid").Value(error.GetPid())
                        .Item("tid").Value(error.GetTid())
                        .Item("fid").Value(error.GetFid());
                }
                if (error.HasOriginAttributes()) {
                    fluent
                        .Item("datetime").Value(error.GetDatetime());
                }
                if (error.HasTracingAttributes()) {
                    fluent
                        .Item("trace_id").Value(error.GetTraceId())
                        .Item("span_id").Value(error.GetSpanId());
                }
                if (depth > ErrorSerializationDepthLimit) {
                    fluent
                        .Item("original_error_depth").Value(depth);
                }
                for (const auto& [key, value] : error.Attributes().ListPairs()) {
                    fluent
                        .Item(key).Value(value);
                }
            })
            .DoIf(!error.InnerErrors().empty(), [&] (auto fluent) {
                SerializeInnerErrors(fluent, error, depth);
            })
            .DoIf(valueProducer != nullptr, [&] (auto fluent) {
                auto* consumer = fluent.GetConsumer();
                // NB: we are forced to deal with a bare consumer here because
                // we can't use void(TFluentMap) in a function signature as it
                // will lead to the inclusion of fluent.h in error.h and a cyclic
                // inclusion error.h -> fluent.h -> callback.h -> error.h
                consumer->OnKeyedItem(TStringBuf("value"));
                (*valueProducer)(consumer);
            })
        .EndMap();

}

void Deserialize(TError& error, const NYTree::INodePtr& node)
{
    error = {};

    auto mapNode = node->AsMap();

    static const TString CodeKey("code");
    auto code = TErrorCode(mapNode->GetChildOrThrow(CodeKey)->GetValue<i64>());
    if (code == NYT::EErrorCode::OK) {
        return;
    }

    auto result = std::make_unique<TError::TImpl>();
    result->SetCode(code);

    static const TString MessageKey("message");
    result->SetMessage(mapNode->GetChildOrThrow(MessageKey)->GetValue<TString>());

    static const TString AttributesKey("attributes");
    result->SetAttributes(IAttributeDictionary::FromMap(mapNode->GetChildOrThrow(AttributesKey)->AsMap()));

    static const TString InnerErrorsKey("inner_errors");
    if (auto innerErrorsNode = mapNode->FindChild(InnerErrorsKey)) {
        for (const auto& innerErrorNode : innerErrorsNode->AsList()->GetChildren()) {
            result->MutableInnerErrors()->push_back(ConvertTo<TError>(innerErrorNode));
        }
    }

    error = TError(std::move(result));
}

////////////////////////////////////////////////////////////////////////////////

TError operator << (TError error, const TErrorAttribute& attribute)
{
    error.MutableAttributes()->SetYson(attribute.Key, attribute.Value);
    return error;
}

TError operator << (TError error, const std::vector<TErrorAttribute>& attributes)
{
    for (const auto& attribute : attributes) {
        error.MutableAttributes()->SetYson(attribute.Key, attribute.Value);
    }
    return error;
}

TError operator << (TError error, const TError& innerError)
{
    error.MutableInnerErrors()->push_back(innerError);
    return error;
}

TError operator << (TError error, TError&& innerError)
{
    error.MutableInnerErrors()->push_back(std::move(innerError));
    return error;
}

TError operator << (TError error, const std::vector<TError>& innerErrors)
{
    error.MutableInnerErrors()->insert(
        error.MutableInnerErrors()->end(),
        innerErrors.begin(),
        innerErrors.end());
    return error;
}

TError operator << (TError error, std::vector<TError>&& innerErrors)
{
    error.MutableInnerErrors()->insert(
        error.MutableInnerErrors()->end(),
        std::make_move_iterator(innerErrors.begin()),
        std::make_move_iterator(innerErrors.end()));
    return error;
}

TError operator << (TError error, const NYTree::IAttributeDictionary& attributes)
{
    error.MutableAttributes()->MergeFrom(attributes);
    return error;
}

////////////////////////////////////////////////////////////////////////////////

const char* TErrorException::what() const noexcept
{
    if (CachedWhat_.empty()) {
        CachedWhat_ = ToString(Error_);
    }
    return CachedWhat_.data();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
