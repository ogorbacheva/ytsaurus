#pragma once

#include <mapreduce/yt/interface/batch_request.h>
#include <mapreduce/yt/interface/fwd.h>
#include <mapreduce/yt/interface/node.h>

#include <mapreduce/yt/http/requests.h>

#include <library/threading/future/future.h>

#include <util/generic/ptr.h>
#include <util/generic/deque.h>

#include <exception>

namespace NYT {
namespace NDetail {

////////////////////////////////////////////////////////////////////////////////

struct IRetryPolicy;
struct TResponseInfo;
class TClient;
class TRawBatchRequest;
using TClientPtr = ::TIntrusivePtr<TClient>;

////////////////////////////////////////////////////////////////////////////////

class TBatchRequest
    : public IBatchRequest
{
public:
    TBatchRequest(const TTransactionId& defaultTransaction, ::TIntrusivePtr<TClient> client);

    ~TBatchRequest();

    virtual IBatchRequestBase& WithTransaction(const TTransactionId& transactionId) override;

    virtual NThreading::TFuture<TLockId> Create(
        const TYPath& path,
        ENodeType type,
        const TCreateOptions& options = TCreateOptions()) override;

    virtual NThreading::TFuture<void> Remove(
        const TYPath& path,
        const TRemoveOptions& options = TRemoveOptions()) override;

    virtual NThreading::TFuture<bool> Exists(const TYPath& path) override;

    virtual NThreading::TFuture<TNode> Get(
        const TYPath& path,
        const TGetOptions& options = TGetOptions()) override;

    virtual NThreading::TFuture<void> Set(
        const TYPath& path,
        const TNode& node,
        const TSetOptions& options = TSetOptions()) override;

    virtual NThreading::TFuture<TNode::TListType> List(
        const TYPath& path,
        const TListOptions& options = TListOptions()) override;

    virtual NThreading::TFuture<TNodeId> Copy(
        const TYPath& sourcePath,
        const TYPath& destinationPath,
        const TCopyOptions& options = TCopyOptions()) override;

    virtual NThreading::TFuture<TNodeId> Move(
        const TYPath& sourcePath,
        const TYPath& destinationPath,
        const TMoveOptions& options = TMoveOptions()) override;

    virtual NThreading::TFuture<TNodeId> Link(
        const TYPath& targetPath,
        const TYPath& linkPath,
        const TLinkOptions& options = TLinkOptions()) override;

    virtual NThreading::TFuture<ILockPtr> Lock(
        const TYPath& path,
        ELockMode mode,
        const TLockOptions& options = TLockOptions()) override;

    virtual NThreading::TFuture<TRichYPath> CanonizeYPath(const TRichYPath& path) override;

    virtual NThreading::TFuture<TTableColumnarStatistics> GetTableColumnarStatistics(const TRichYPath& path) override;

    virtual void ExecuteBatch(const TExecuteBatchOptions& executeBatch) override;

private:
    TBatchRequest(NDetail::TRawBatchRequest* impl, ::TIntrusivePtr<TClient> client);

private:
    TTransactionId DefaultTransaction_;
    ::TIntrusivePtr<NDetail::TRawBatchRequest> Impl_;
    THolder<TBatchRequest> TmpWithTransaction_;
    ::TIntrusivePtr<TClient> Client_;

private:
    friend class NYT::NDetail::TClient;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NDetail
} // namespace NYT
