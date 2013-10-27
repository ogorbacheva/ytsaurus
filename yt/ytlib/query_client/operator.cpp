#include "ast.h"
#include "ast_visitor.h"
#include "query_context.h"

#include <yt/ytlib/query_client/operator.pb.h>

#include <core/misc/protobuf_helpers.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wswitch-enum"

namespace NYT {
namespace NQueryClient {

////////////////////////////////////////////////////////////////////////////////

using NYT::ToProto;
using NYT::FromProto;

////////////////////////////////////////////////////////////////////////////////

void ToProto(NProto::TOperator* serialized, const TOperator* original)
{
    serialized->set_kind(original->GetKind());

    switch (original->GetKind()) {

    case EOperatorKind::Scan: {
        auto* op = original->As<TScanOperator>();
        auto* proto = serialized->MutableExtension(NProto::TScanOperator::scan_operator);
        proto->set_table_index(op->GetTableIndex());
        ToProto(proto->mutable_data_split(), op->DataSplit());
        break;
    }

    case EOperatorKind::Union: {
        auto* op = original->As<TUnionOperator>();
        auto* proto = serialized->MutableExtension(NProto::TUnionOperator::union_operator);
        ToProto(proto->mutable_sources(), op->Sources());
        break;
    }

    case EOperatorKind::Filter: {
        auto* op = original->As<TFilterOperator>();
        auto* proto = serialized->MutableExtension(NProto::TFilterOperator::filter_operator);
        ToProto(proto->mutable_source(), op->GetSource());
        ToProto(proto->mutable_predicate(), op->GetPredicate());
        break;
    }

    case EOperatorKind::Project: {
        auto* op = original->As<TProjectOperator>();
        auto* proto = serialized->MutableExtension(NProto::TProjectOperator::project_operator);
        ToProto(proto->mutable_source(), op->GetSource());
        ToProto(proto->mutable_projections(), op->Projections());
        break;
    }

    default:
        YUNREACHABLE();
    }

}

const TOperator* FromProto(const NProto::TOperator& serialized, TQueryContext* context)
{
    const TOperator* result = nullptr;

    switch (EOperatorKind(serialized.kind())) {

    case EOperatorKind::Scan: {
        auto data = serialized.GetExtension(NProto::TScanOperator::scan_operator);
        auto typedResult = new (context) TScanOperator(
            context,
            data.table_index());
        FromProto(&typedResult->DataSplit(), data.data_split());
        YASSERT(!result);
        result = typedResult;
        break;
    }

    case EOperatorKind::Union: {
        auto data = serialized.GetExtension(NProto::TUnionOperator::union_operator);
        auto typedResult = new (context) TUnionOperator(context);
        typedResult->Sources().reserve(data.sources_size());
        for (int i = 0; i < data.sources_size(); ++i) {
            typedResult->Sources().push_back(
                FromProto(data.sources(i), context));
        }
        YASSERT(!result);
        result = typedResult;
        break;
    }

    case EOperatorKind::Filter: {
        auto data = serialized.GetExtension(NProto::TFilterOperator::filter_operator);
        auto typedResult = new (context) TFilterOperator(
            context,
            FromProto(data.source(), context));
        typedResult->SetPredicate(FromProto(data.predicate(), context));
        YASSERT(!result);
        result = typedResult;
        break;
    }

    case EOperatorKind::Project: {
        auto data = serialized.GetExtension(NProto::TProjectOperator::project_operator);
        auto typedResult = new (context) TProjectOperator(
            context,
            FromProto(data.source(), context));
        typedResult->Projections().reserve(data.projections_size());
        for (int i = 0; i < data.projections_size(); ++i) {
            typedResult->Projections().push_back(
                FromProto(data.projections(i), context));
        }
        YASSERT(!result);
        result = typedResult;
        break;
    }

    default:
        YUNREACHABLE();
    }

    YCHECK(result);
    return result;

}

////////////////////////////////////////////////////////////////////////////////

} // namespace NQueryClient
} // namespace NYT

#pragma GCC diagnostic pop

