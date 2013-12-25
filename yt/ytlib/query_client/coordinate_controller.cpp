#include "coordinate_controller.h"
#include "evaluate_controller.h"

#include "private.h"
#include "helpers.h"

#include "plan_node.h"
#include "plan_visitor.h"
#include "plan_helpers.h"

#include "graphviz.h"

#include <ytlib/new_table_client/reader.h>
#include <ytlib/new_table_client/writer.h>
#include <ytlib/new_table_client/schema.h>
#include <ytlib/new_table_client/unversioned_row.h>

#include <ytlib/object_client/helpers.h>

#include <core/concurrency/fiber.h>

#include <core/misc/protobuf_helpers.h>

namespace NYT {
namespace NQueryClient {

using namespace NConcurrency;
using namespace NObjectClient;
using namespace NVersionedTableClient;

////////////////////////////////////////////////////////////////////////////////

TCoordinateController::TCoordinateController(
    ICoordinateCallbacks* callbacks,
    const TPlanFragment& fragment)
    : Callbacks_(callbacks)
    , Fragment_(fragment)
    , Logger(QueryClientLogger)
{
    Logger.AddTag(Sprintf(
        "FragmendId: %s",
        ~ToString(Fragment_.Guid())));
}

TCoordinateController::~TCoordinateController()
{ }

int TCoordinateController::GetPeerIndex(const TDataSplit& dataSplit)
{
    auto objectId = GetObjectIdFromDataSplit(dataSplit);
    YCHECK(TypeFromId(objectId) == EObjectType::QueryPlan);
    return CounterFromId(objectId);
}

IReaderPtr TCoordinateController::GetReader(const TDataSplit& dataSplit)
{
    auto objectId = GetObjectIdFromDataSplit(dataSplit);
    LOG_DEBUG("Creating reader for %s", ~ToString(objectId));
    switch (TypeFromId(objectId)) {
        case EObjectType::QueryPlan: {
            auto peerIndex = CounterFromId(objectId);
            YASSERT(peerIndex < Peers_.size());
            auto peer = std::get<1>(Peers_[peerIndex]);
            YASSERT(peer);
            return peer;
        }
        default:
            return GetCallbacks()->GetReader(dataSplit);
    }
}

TError TCoordinateController::Run()
{
    try {
        LOG_DEBUG("Coordinating plan fragment");

        SplitFurther();
        PushdownFilters();
        PushdownGroups();
        PushdownProjects();
        DistributeToPeers();
        InitializeReaders();

        return TError();
    } catch (const std::exception& ex) {
        auto error = TError("Failed to coordinate plan fragment") << ex;
        LOG_ERROR(error);
        return error;
    }
}

TPlanFragment TCoordinateController::GetCoordinatorFragment() const
{
    return Fragment_;
}

std::vector<TPlanFragment> TCoordinateController::GetPeerFragments() const
{
    std::vector<TPlanFragment> result;
    result.reserve(Peers_.size());
    for (const auto& peer : Peers_) {
        result.emplace_back(std::get<0>(peer));
    }
    return result;
}

void TCoordinateController::SplitFurther()
{
    LOG_DEBUG("Splitting inputs");
    // Rewrite
    //   S
    // to
    //   U -> { S1 ... Sk }
    Rewrite(
    [this] (TPlanContext* context, const TOperator* op) -> const TOperator* {
        if (auto* scanOp = op->As<TScanOperator>()) {
            auto objectId = GetObjectIdFromDataSplit(scanOp->DataSplit());
            if (GetCallbacks()->CanSplit(scanOp->DataSplit())) {
                LOG_DEBUG("Splitting input %s", ~ToString(objectId));
            } else {
                return scanOp;
            }

            auto dataSplitsOrError = WaitFor(
                GetCallbacks()->SplitFurther(scanOp->DataSplit()));
            auto dataSplits = dataSplitsOrError.GetValueOrThrow();
            LOG_DEBUG(
                "Got %" PRISZT " splits for input %s",
                dataSplits.size(),
                ~ToString(objectId));

            if (dataSplits.size() == 0) {
                THROW_ERROR_EXCEPTION("Input %s is empty", ~ToString(objectId));
            } else {
                auto* unionOp = new (context) TUnionOperator(context);
                for (const auto& dataSplit : dataSplits) {
                    auto* splittedScanOp = new (context) TScanOperator(context);
                    splittedScanOp->DataSplit() = dataSplit;
                    unionOp->Sources().push_back(splittedScanOp);
                }
                return unionOp;
            }
        }
        return op;
    });
}

void TCoordinateController::PushdownFilters()
{
    LOG_DEBUG("Pushing down filter operators");
    Rewrite(
    [] (TPlanContext* context, const TOperator* op) -> const TOperator* {
        // Rewrite
        //   F -> U -> { O1 ... Ok }
        // to
        //   U -> { F -> O1 ... F -> Ok }
        if (auto* filterOp = op->As<TFilterOperator>()) {
            if (auto* unionOp = filterOp->GetSource()->As<TUnionOperator>()) {
                auto* newUnionOp = new (context) TUnionOperator(context);
                newUnionOp->Sources().reserve(unionOp->Sources().size());
                for (const auto& sourceOp : unionOp->Sources()) {
                    auto* newFilterOp = filterOp->Clone(context)->As<TFilterOperator>();
                    newFilterOp->SetSource(sourceOp);
                    newUnionOp->Sources().push_back(newFilterOp);
                }
                return newUnionOp;
            }
        }
        return op;
    });
}

void TCoordinateController::PushdownProjects()
{
    LOG_DEBUG("Pushing down project operators");
    Rewrite(
    [this] (TPlanContext* context, const TOperator* op) -> const TOperator* {
        // Rewrite
        //   P -> U -> { O1 ... Ok }
        // to
        //   U -> { P -> O1 ... P -> Ok }
        if (auto* projectOp = op->As<TProjectOperator>()) {
            if (auto* unionOp = projectOp->GetSource()->As<TUnionOperator>()) {
                auto* newUnionOp = new (context) TUnionOperator(context);
                newUnionOp->Sources().reserve(unionOp->Sources().size());
                for (const auto& sourceOp : unionOp->Sources()) {
                    auto* newProjectOp = projectOp->Clone(context)->As<TProjectOperator>();
                    newProjectOp->SetSource(sourceOp);
                    newUnionOp->Sources().push_back(newProjectOp);
                }
                return newUnionOp;
            }
        }
        return op;
    });
}

void TCoordinateController::PushdownGroups()
{
    LOG_DEBUG("Pushing down group operators");
    Rewrite(
    [this] (TPlanContext* context, const TOperator* op) -> const TOperator* {
        // Rewrite
        //   G -> U -> { O1 ... Ok }
        // to
        //   G -> U -> { G -> O1 ... G -> Ok }
        if (auto* groupOp = op->As<TGroupOperator>()) {
            if (auto* unionOp = groupOp->GetSource()->As<TUnionOperator>()) {
                auto* newUnionOp = new (context) TUnionOperator(context);
                newUnionOp->Sources().reserve(unionOp->Sources().size());
                for (const auto& source : unionOp->Sources()) {
                    auto clonedGroupOp = new (context) TGroupOperator(
                        context,
                        source);
                    clonedGroupOp->GroupItems() = groupOp->GroupItems();
                    clonedGroupOp->AggregateItems() = groupOp->AggregateItems();
                    newUnionOp->Sources().push_back(clonedGroupOp);
                }

                auto* finalGroupOp = new (context) TGroupOperator(context, newUnionOp);

                auto& finalGroupItems = finalGroupOp->GroupItems();
                for (const auto& groupItem : groupOp->GroupItems()) {
                    auto referenceExpr = new (context) TReferenceExpression(
                        context,
                        NullSourceLocation,
                        groupItem.Name);
                    finalGroupItems.push_back(TNamedExpression(
                        referenceExpr,
                        groupItem.Name));
                }

                auto& finalAggregateItems = finalGroupOp->AggregateItems();
                for (const auto& aggregateItem : groupOp->AggregateItems()) {
                    auto referenceExpr = new (context) TReferenceExpression(
                        context, 
                        NullSourceLocation, 
                        aggregateItem.Name);
                    finalAggregateItems.push_back(TAggregateItem(
                        referenceExpr,
                        aggregateItem.AggregateFunction,
                        aggregateItem.Name));
                }

                return finalGroupOp;
            }
        }
        return op;
    });
}

void TCoordinateController::DistributeToPeers()
{
    LOG_DEBUG("Distributing plan to peers");
    YCHECK(Peers_.empty());

    int numberOfScanOperators = 0;
    Visit(GetHead(), [&] (const TOperator* op) {
        if (op->IsA<TScanOperator>()) {
            ++numberOfScanOperators;
        }
    });

    LOG_DEBUG("Got %d scan operators in plan fragment", numberOfScanOperators);
    if (numberOfScanOperators == 0) {
        LOG_DEBUG("Nothing to distribute");
        return;
    }

    Rewrite(
    [this] (TPlanContext* context, const TOperator* op) -> const TOperator* {
        // Rewrite
        //   U -> { O1 ... Ok }
        // to
        //   U -> { S1 ... Sk } && S1 -> O1, ..., Sk -> Ok
        auto* unionOp = op->As<TUnionOperator>();
        if (!unionOp) {
            return op;
        }

        auto* facadeUnionOp = new (GetContext()) TUnionOperator(GetContext());

        for (const auto& sourceOp : unionOp->Sources()) {
            auto fragment = TPlanFragment(GetContext(), sourceOp);
            LOG_DEBUG("Created subfragment (SubFragmentId: %s)", ~ToString(fragment.Guid()));

            auto inferredKeyRange = InferKeyRange(fragment.GetHead());
            if (IsEmpty(inferredKeyRange)) {
                LOG_DEBUG("Subfragment is empty (SubFragmentId: %s)", ~ToString(fragment.Guid()));
                continue;
            } else {
                LOG_DEBUG("Inferred key range %s ... %s (SubFragmentId: %s)",
                    ~ToString(inferredKeyRange.first),
                    ~ToString(inferredKeyRange.second),
                    ~ToString(fragment.Guid()));
            }

            fragment.Rewrite(
            [&] (TPlanContext* context, const TOperator* op) -> const TOperator* {
                auto* scanOp = op->As<TScanOperator>();
                if (!scanOp) {
                    return op;
                }
                if (!IsSorted(scanOp->DataSplit())) {
                    return op;
                }

                auto* clonedScanOp = scanOp->Clone(context)->As<TScanOperator>();
                auto& clonedDataSplit = clonedScanOp->DataSplit();
                SetBothBounds(&clonedDataSplit, Intersect(
                    GetBothBoundsFromDataSplit(clonedDataSplit),
                    inferredKeyRange));
                return clonedScanOp;
            });

            Peers_.emplace_back(fragment, nullptr);

            auto* facadeScanOp = new (GetContext()) TScanOperator(GetContext());
            auto* facadeDataSplit = &facadeScanOp->DataSplit();

            SetObjectId(
                facadeDataSplit,
                MakeId(EObjectType::QueryPlan, 0xBABE, Peers_.size() - 1, 0));
            SetTableSchema(facadeDataSplit, sourceOp->GetTableSchema());
            SetKeyColumns(facadeDataSplit, sourceOp->GetKeyColumns());
            SetBothBounds(facadeDataSplit, inferredKeyRange);

            facadeUnionOp->Sources().push_back(facadeScanOp);
        }

        return facadeUnionOp;
    });

    LOG_DEBUG("Distributed %" PRISZT " subfragments to peers", Peers_.size());
}

void TCoordinateController::InitializeReaders()
{
    for (auto& peer : Peers_) {
        std::get<1>(peer) = GetCallbacks()->Delegate(
            std::get<0>(peer),
            GetHeaviestSplit(std::get<0>(peer).GetHead()));
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NQueryClient
} // namespace NYT

