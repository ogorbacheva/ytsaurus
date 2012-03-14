#ifndef CYPRESS_SERVICE_PROXY_INL_H_
#error "Direct inclusion of this file is not allowed, include cypress_service_proxy.h"
#endif

#include "cypress_ypath_proxy.h"

#include <ytlib/rpc/service.h>
#include <ytlib/rpc/client.h>

namespace NYT {
namespace NCypress {

////////////////////////////////////////////////////////////////////////////////

template <class TTypedResponse>
TIntrusivePtr<TTypedResponse> TCypressServiceProxy::TRspExecuteBatch::GetResponse(int index) const
{
    YASSERT(index >= 0 && index < GetSize());
    auto innerResponse = New<TTypedResponse>();
    int beginIndex = BeginPartIndexes[index];
    int endIndex = beginIndex + Body.part_counts(index);
    yvector<TSharedRef> innerParts(
        Attachments_.begin() + beginIndex,
        Attachments_.begin() + endIndex);
    auto innerMessage = NBus::CreateMessageFromParts(MoveRV(innerParts));
    innerResponse->Deserialize(~innerMessage);
    return innerResponse;
}

template <class TTypedResponse>
TIntrusivePtr<TTypedResponse> TCypressServiceProxy::TRspExecuteBatch::GetResponse(const Stroka& key) const
{
    YASSERT(!key.empty());
    auto range = KeyToIndexes.equal_range(key);
    auto it = range.first;
    int index = it->second;
    YASSERT(++it == range.second);
    return GetResponse(index);
}

template <class TTypedResponse>
std::vector< TIntrusivePtr<TTypedResponse> > TCypressServiceProxy::TRspExecuteBatch::GetResponses(const Stroka& key)    const
{
    std::yvector< TIntrusivePtr<TTypedResponse> > responses;
    if (key.empty()) {
        responses.reserve(GetSize());
        for (int index = 0; < index < GetSize(); ++index) {
            responses.push_back(GetResponse<TTypedResponse>(index));
        }
    } else {
        auto range = KeyToIndexes.equal_range(key);
        for (auto it = range.first; it != range.second; ++it) {
            responses.push_back(GetResponse<TTypedResponse>(it->second));
        }
    }
    return responses;
}

////////////////////////////////////////////////////////////////////////////////

template <class TTypedRequest>
TIntrusivePtr< TFuture< TIntrusivePtr<typename TTypedRequest::TTypedResponse> > >
TCypressServiceProxy::Execute(TTypedRequest* innerRequest)
{
    typedef typename TTypedRequest::TTypedResponse TTypedResponse;

    auto innerRequestMessage = innerRequest->Serialize();

    auto outerRequest = Execute();
    outerRequest->add_part_counts(innerRequestMessage->GetParts().ysize());
    outerRequest->Attachments() = innerRequestMessage->GetParts();

    return outerRequest->Invoke()->Apply(FromFunctor(
        [] (TRspExecute::TPtr outerResponse) -> TIntrusivePtr<TTypedResponse>
        {
            auto innerResponse = New<TTypedResponse>();
            auto error = outerResponse->GetError();
            if (error.IsOK()) {
                auto innerResponseMessage = NBus::CreateMessageFromParts(outerResponse->Attachments());
                innerResponse->Deserialize(~innerResponseMessage);
            } else if (NRpc::IsRpcError(error)) {
                innerResponse->SetError(error);
            } else {
                // TODO(babenko): should we be erasing the error code here?
                innerResponse->SetError(TError(outerResponse->GetError().GetMessage()));
            }
            return innerResponse;
        }));
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NCypress
} // namespace NYT
