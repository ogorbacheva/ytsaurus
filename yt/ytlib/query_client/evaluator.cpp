#include "stdafx.h"
#include "evaluator.h"

#include "cg_fragment.h"
#include "cg_fragment_compiler.h"
#include "cg_routines.h"

#include "plan_fragment.h"
#include "plan_node.h"

#include "helpers.h"
#include "private.h"

#include <ytlib/new_table_client/schemaful_writer.h>
#include <ytlib/new_table_client/row_buffer.h>

#include <core/concurrency/scheduler.h>
#include <core/profiling/scoped_timer.h>

#include <core/misc/cache.h>

#include <core/logging/log.h>

#include <core/tracing/trace_context.h>

#include <llvm/ADT/FoldingSet.h>

#include <llvm/Support/Threading.h>
#include <llvm/Support/TargetSelect.h>

#include <mutex>

namespace NYT {
namespace NQueryClient {

using namespace NConcurrency;

static const auto& Logger = QueryClientLogger;

////////////////////////////////////////////////////////////////////////////////

void InitializeLlvmImpl()
{
    YCHECK(llvm::llvm_is_multithreaded());
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmParser();
    llvm::InitializeNativeTargetAsmPrinter();
}

void InitializeLlvm()
{
    static std::once_flag onceFlag;
    std::call_once(onceFlag, &InitializeLlvmImpl);
}

////////////////////////////////////////////////////////////////////////////////
// Folding profiler computes a strong structural hash used to cache query fragments.

DECLARE_ENUM(EFoldingObjectType,
    (ScanOp)
    (FilterOp)
    (GroupOp)
    (ProjectOp)

    (LiteralExpr)
    (ReferenceExpr)
    (FunctionExpr)
    (BinaryOpExpr)

    (NamedExpression)
    (AggregateItem)

    (TableSchema)
);

class TFoldingProfiler
{
public:
    explicit TFoldingProfiler(
        llvm::FoldingSetNodeID& id,
        TCGBinding& binding,
        TCGVariables& variables)
        : Id_(id)
        , Binding_(binding)
        , Variables_(variables)
    { }

    void Profile(const TOperator* op);
    void Profile(const TExpression* expr);
    void Profile(const TNamedExpression& namedExpression);
    void Profile(const TAggregateItem& aggregateItem);
    void Profile(const TTableSchema& tableSchema);

private:
    llvm::FoldingSetNodeID& Id_;
    TCGBinding& Binding_;
    TCGVariables& Variables_;

};

void TFoldingProfiler::Profile(const TOperator* op)
{
    switch (op->GetKind()) {

        case EOperatorKind::Scan: {
            const auto* scanOp = op->As<TScanOperator>();
            Id_.AddInteger(EFoldingObjectType::ScanOp);

            auto tableSchema = scanOp->GetTableSchema();
            
            Profile(tableSchema);
            
            auto dataSplits = scanOp->DataSplits();
            for (auto & dataSplit : dataSplits) {
                SetTableSchema(&dataSplit, tableSchema);
            }

            int index = Variables_.DataSplitsArray.size();
            Variables_.DataSplitsArray.push_back(dataSplits);
            Binding_.ScanOpToDataSplits[scanOp] = index;

            break;
        }

        case EOperatorKind::Filter: {
            const auto* filterOp = op->As<TFilterOperator>();
            Id_.AddInteger(EFoldingObjectType::FilterOp);

            Profile(filterOp->GetPredicate());
            Profile(filterOp->GetSource());

            break;
        }

        case EOperatorKind::Project: {
            const auto* projectOp = op->As<TProjectOperator>();
            Id_.AddInteger(EFoldingObjectType::ProjectOp);

            for (const auto& projection : projectOp->Projections()) {
                Profile(projection);
            }

            Profile(projectOp->GetSource());

            break;
        }

        case EOperatorKind::Group: {
            const auto* groupOp = op->As<TGroupOperator>();
            Id_.AddInteger(EFoldingObjectType::GroupOp);

            for (const auto& groupItem : groupOp->GroupItems()) {
                Profile(groupItem);
            }

            for (const auto& aggregateItem : groupOp->AggregateItems()) {
                Profile(aggregateItem);
            }

            Profile(groupOp->GetSource());

            break;
        }

        default:
            YUNREACHABLE();
    }
}

void TFoldingProfiler::Profile(const TExpression* expr)
{
    using NVersionedTableClient::MakeInt64Value;
    using NVersionedTableClient::MakeDoubleValue;
    using NVersionedTableClient::MakeStringValue;

    switch (expr->GetKind()) {

        case EExpressionKind::Literal: {
            const auto* literalExpr = expr->As<TLiteralExpression>();
            Id_.AddInteger(EFoldingObjectType::LiteralExpr);

            int index = Variables_.ConstantArray.size();
            Variables_.ConstantArray.push_back(literalExpr->GetValue());
            Binding_.NodeToConstantIndex[expr] = index;
            break;
        }

        case EExpressionKind::Reference: {
            const auto* referenceExpr = expr->As<TReferenceExpression>();
            Id_.AddInteger(EFoldingObjectType::ReferenceExpr);
            Id_.AddString(referenceExpr->GetColumnName().c_str());

            break;
        }

        case EExpressionKind::Function: {
            const auto* functionExpr = expr->As<TFunctionExpression>();
            Id_.AddInteger(EFoldingObjectType::FunctionExpr);
            Id_.AddString(functionExpr->GetFunctionName().c_str());

            for (const auto& argument : functionExpr->Arguments()) {
                Profile(argument);
            }

            break;
        }

        case EExpressionKind::BinaryOp: {
            const auto* binaryOp = expr->As<TBinaryOpExpression>();
            Id_.AddInteger(EFoldingObjectType::BinaryOpExpr);
            Id_.AddInteger(binaryOp->GetOpcode());

            Profile(binaryOp->GetLhs());
            Profile(binaryOp->GetRhs());

            break;
        }

        default:
            YUNREACHABLE();
    }
}

void TFoldingProfiler::Profile(const TTableSchema& tableSchema)
{
    Id_.AddInteger(EFoldingObjectType::TableSchema);
}

void TFoldingProfiler::Profile(const TNamedExpression& namedExpression)
{
    Id_.AddInteger(EFoldingObjectType::NamedExpression);
    Id_.AddString(namedExpression.Name.c_str());

    Profile(namedExpression.Expression);
}

void TFoldingProfiler::Profile(const TAggregateItem& aggregateItem)
{
    Id_.AddInteger(EFoldingObjectType::AggregateItem);
    Id_.AddInteger(aggregateItem.AggregateFunction);
    Id_.AddString(aggregateItem.Name.c_str());

    Profile(aggregateItem.Expression);
}

struct TFoldingHasher
{
    size_t operator ()(const llvm::FoldingSetNodeID& id) const
    {
        return id.ComputeHash();
    }
};

////////////////////////////////////////////////////////////////////////////////

class TCachedCGFragment
    : public TCacheValueBase<
        llvm::FoldingSetNodeID,
        TCachedCGFragment,
        TFoldingHasher>
    , public TCGFragment
{
public:
    explicit TCachedCGFragment(const llvm::FoldingSetNodeID& id)
        : TCacheValueBase(id)
        , TCGFragment()
    { }

};

class TEvaluator::TImpl
    : public TSizeLimitedCache<
        llvm::FoldingSetNodeID,
        TCachedCGFragment,
        TFoldingHasher>
{
public:
    explicit TImpl(const int maxCacheSize)
        : TSizeLimitedCache(maxCacheSize)
    {
        InitializeLlvm();
        RegisterCGRoutines();

        Compiler_ = CreateFragmentCompiler();

        CallCgFunctionPtr_ = &CallCgFunction;
    }

    TQueryStatistics Run(
        IEvaluateCallbacks* callbacks,
        const TPlanFragment& fragment,
        ISchemafulWriterPtr writer)
    {
        TRACE_CHILD("QueryClient", "Evaluate") {
            TRACE_ANNOTATION("fragment_id", fragment.Id());

            auto Logger = BuildLogger(fragment);

            TQueryStatistics statistics;
            TDuration wallTime;

            try {
                NProfiling::TAggregatingTimingGuard timingGuard(&wallTime);

                TCgFunction cgFunction;
                TCGVariables fragmentParams;

                std::tie(cgFunction, fragmentParams) = Codegen(fragment);

                // Make TRow from fragmentParams.ConstantArray.
                TChunkedMemoryPool memoryPool;
                auto constants = TRow::Allocate(&memoryPool, fragmentParams.ConstantArray.size());
                for (int i = 0; i < fragmentParams.ConstantArray.size(); ++i) {
                    constants[i] = fragmentParams.ConstantArray[i];
                }

                LOG_DEBUG("Evaluating plan fragment");

                LOG_DEBUG("Opening writer");
                {
                    NProfiling::TAggregatingTimingGuard timingGuard(&statistics.AsyncTime);
                    auto error = WaitFor(writer->Open(
                        fragment.GetHead()->GetTableSchema(),
                        fragment.GetHead()->GetKeyColumns()));
                    THROW_ERROR_EXCEPTION_IF_FAILED(error);
                }

                LOG_DEBUG("Writer opened");

                TRowBuffer rowBuffer;
                TChunkedMemoryPool scratchSpace;
                std::vector<TRow> batch;
                batch.reserve(MaxRowsPerWrite);

                TExecutionContext executionContext;
                executionContext.Callbacks = callbacks;
                executionContext.Context = fragment.GetContext().Get();
                executionContext.DataSplitsArray = &fragmentParams.DataSplitsArray;
                executionContext.RowBuffer = &rowBuffer;
                executionContext.ScratchSpace = &scratchSpace;
                executionContext.Writer = writer.Get();
                executionContext.Batch = &batch;
                executionContext.Statistics = &statistics;
                executionContext.InputRowLimit = fragment.GetContext()->GetInputRowLimit();
                executionContext.OutputRowLimit = fragment.GetContext()->GetOutputRowLimit();

                CallCgFunctionPtr_(cgFunction, constants, &executionContext);

                LOG_DEBUG("Flushing writer");
                if (!batch.empty()) {
                    if (!writer->Write(batch)) {
                        NProfiling::TAggregatingTimingGuard timingGuard(&statistics.AsyncTime);
                        auto error = WaitFor(writer->GetReadyEvent());
                        THROW_ERROR_EXCEPTION_IF_FAILED(error);
                    }
                }

                LOG_DEBUG("Closing writer");
                {
                    NProfiling::TAggregatingTimingGuard timingGuard(&statistics.AsyncTime);
                    auto error = WaitFor(writer->Close());
                    THROW_ERROR_EXCEPTION_IF_FAILED(error);
                }

                LOG_DEBUG("Finished evaluating plan fragment (RowBufferCapacity: %" PRISZT ", ScratchSpaceCapacity: %" PRISZT ")",
                    rowBuffer.GetCapacity(),
                    scratchSpace.GetCapacity());

            } catch (const std::exception& ex) {
                THROW_ERROR_EXCEPTION("Failed to evaluate plan fragment") << ex;
            }

            statistics.SyncTime = wallTime - statistics.AsyncTime;

            TRACE_ANNOTATION("rows_read", statistics.RowsRead);
            TRACE_ANNOTATION("rows_written", statistics.RowsWritten);
            TRACE_ANNOTATION("sync_time", statistics.SyncTime);
            TRACE_ANNOTATION("async_time", statistics.AsyncTime);
            TRACE_ANNOTATION("incomplete_input", statistics.IncompleteInput);
            TRACE_ANNOTATION("incomplete_output", statistics.IncompleteOutput);

            return statistics;
        }        
    }

private:
    std::pair<TCgFunction, TCGVariables> Codegen(const TPlanFragment& fragment)
    {
        llvm::FoldingSetNodeID id;
        TCGBinding binding;
        TCGVariables variables;

        TFoldingProfiler(id, binding, variables).Profile(fragment.GetHead());
        auto Logger = BuildLogger(fragment);

        TInsertCookie cookie(id);
        if (BeginInsert(&cookie)) {
            LOG_DEBUG("Codegen cache miss");
            try {
                TRACE_CHILD("QueryClient", "Compile") {
                    LOG_DEBUG("Started compiling fragment");
                    auto newCGFragment = New<TCachedCGFragment>(id);
                    newCGFragment->Embody(Compiler_(fragment, *newCGFragment, binding));
                    newCGFragment->GetCompiledBody();
                    LOG_DEBUG("Finished compiling fragment");
                    cookie.EndInsert(std::move(newCGFragment));
                }
            } catch (const std::exception& ex) {
                LOG_DEBUG(ex, "Failed to compile fragment");
                cookie.Cancel(ex);
            }
        } else {
            LOG_DEBUG("Codegen cache hit");
        }

        auto cgFragment = cookie.GetValue().Get().ValueOrThrow();
        auto cgFunction = cgFragment->GetCompiledBody();

        YCHECK(cgFunction);

        return std::make_pair(cgFunction, std::move(variables));
    }

    static void CallCgFunction(
        TCgFunction cgFunction,
        TRow constants,
        TExecutionContext* executionContext)
    {
#ifdef DEBUG
        int dummy;
        executionContext->StackSizeGuardHelper = reinterpret_cast<size_t>(&dummy);
#endif
        cgFunction(constants, executionContext);
    }

    void(* volatile CallCgFunctionPtr_)(
        TCgFunction cgFunction,
        TRow constants,
        TExecutionContext* executionContext);

private:
    TCGFragmentCompiler Compiler_;

    static NLog::TLogger BuildLogger(const TPlanFragment& fragment)
    {
        NLog::TLogger result(QueryClientLogger);
        result.AddTag("FragmentId: %v", fragment.Id());
        return result;
    }

};

////////////////////////////////////////////////////////////////////////////////

TEvaluator::TEvaluator()
    : Impl_(New<TEvaluator::TImpl>(100))
{ }

TEvaluator::~TEvaluator()
{ }

TQueryStatistics TEvaluator::Run(
    IEvaluateCallbacks* callbacks,
    const TPlanFragment& fragment,
    ISchemafulWriterPtr writer)
{
    return Impl_->Run(callbacks, fragment, std::move(writer));
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NQueryClient
} // namespace NYT
