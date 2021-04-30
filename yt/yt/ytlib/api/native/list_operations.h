#pragma once

#include "public.h"

#include <yt/yt/client/api/client.h>
#include <yt/yt/client/api/operation_archive_schema.h>

#include <yt/yt/client/object_client/public.h>

namespace NYT::NApi::NNative {

////////////////////////////////////////////////////////////////////////////////

DECLARE_REFCOUNTED_CLASS(TListOperationsFilter)

////////////////////////////////////////////////////////////////////////////////

class TListOperationsCountingFilter
{
public:
    TListOperationsCountingFilter() = default;

    explicit TListOperationsCountingFilter(const TListOperationsOptions& options);

    bool Filter(
        const std::optional<std::vector<TString>>& pools,
        TStringBuf user,
        NScheduler::EOperationState state,
        NScheduler::EOperationType type,
        i64 count);
    bool FilterByFailedJobs(bool hasFailedJobs, i64 count);

    void MergeFrom(const TListOperationsCountingFilter& otherFilter);

public:
    THashMap<TString, i64> PoolCounts;
    THashMap<TString, i64> UserCounts;
    TEnumIndexedVector<NScheduler::EOperationState, i64> StateCounts;
    TEnumIndexedVector<NScheduler::EOperationType, i64> TypeCounts;
    i64 FailedJobsCount = 0;

private:
    // NB: we have to use pointer instead of reference since
    // default constructor is needed in this class.
    const TListOperationsOptions* Options_ = nullptr;
};

class TListOperationsFilter
    : public TRefCounted
{
public:
    struct TBriefProgress
    {
        bool HasFailedJobs;
        TInstant BuildTime;
    };

    struct TLightOperation
    {
    public:
        [[nodiscard]] NObjectClient::TOperationId GetId() const;
        void UpdateBriefProgress(TStringBuf briefProgressYson);
        void SetYson(TString yson);

    private:
        NObjectClient::TOperationId Id_;
        TInstant StartTime_;
        TBriefProgress BriefProgress_;
        TString Yson_;

    private:
        friend class TFilteringConsumer;
        friend class TListOperationsFilter;
    };

public:
    // NB: Each element of |operations| vector are assumed to be
    // YSON lists containing operations in format "id with attributes"
    // (as returned from Cypress "list" command).
    TListOperationsFilter(
        std::vector<NYson::TYsonString> operationsResponses,
        TListOperationsCountingFilter* countingFilter,
        const TListOperationsOptions& options,
        const IInvokerPtr& invoker,
        const NLogging::TLogger& logger);

    template <typename TFunction>
    void ForEachOperationImmutable(TFunction function) const;

    template <typename TFunction>
    void ForEachOperationMutable(TFunction function);

    [[nodiscard]] std::vector<TOperation> BuildOperations(const THashSet<TString>& attributes) const;

    [[nodiscard]] i64 GetCount() const;

    // Confirms that |brief_progress| field is relevant and filtration by it can be applied.
    void OnBriefProgressFinished();

private:
    TListOperationsCountingFilter* CountingFilter_;
    const TListOperationsOptions& Options_;
    const IInvokerPtr Invoker_;
    const NLogging::TLogger Logger;
    std::vector<TLightOperation> LightOperations_;

    struct TParseResult
    {
        std::vector<TLightOperation> Operations;
        TListOperationsCountingFilter CountingFilter;
    };

    void ParseResponses(std::vector<NYson::TYsonString> operationsResponses);

    TParseResult ParseOperationsYson(NYson::TYsonString operationsYson) const;
};

DEFINE_REFCOUNTED_TYPE(TListOperationsFilter)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NApi::NNative

#define LIST_OPERATIONS_INL_H
#include "list_operations-inl.h"
#undef LIST_OPERATIONS_INL_H
