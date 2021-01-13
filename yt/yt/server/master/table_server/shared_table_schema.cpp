#include "shared_table_schema.h"

#include <yt/core/rpc/dispatcher.h>

#include <yt/core/ytree/fluent.h>

namespace NYT::NTableServer {

using namespace NTableClient;
using namespace NYson;
using namespace NYTree;

////////////////////////////////////////////////////////////////////////////////

const TTableSchema TSharedTableSchemaRegistry::EmptyTableSchema;
const TFuture<TYsonString> TSharedTableSchemaRegistry::EmptyYsonTableSchema = MakeFuture(BuildYsonStringFluently().Value(EmptyTableSchema));

size_t TSharedTableSchemaRegistry::GetSize() const
{
    return Registry_.size();
}

void TSharedTableSchemaRegistry::Clear()
{
    Registry_.clear();
}

TSharedTableSchemaPtr TSharedTableSchemaRegistry::GetSchema(TTableSchema&& tableSchema)
{
    if (tableSchema == EmptyTableSchema) {
        return nullptr;
    }

    auto it = Registry_.find(tableSchema);
    if (it != Registry_.end()) {
        return *it;
    }
    auto result = New<TSharedTableSchema>(std::move(tableSchema), this);
    Registry_.insert(result.Get());
    return result;
}

TSharedTableSchemaPtr TSharedTableSchemaRegistry::GetSchema(const TTableSchema& tableSchema)
{
    if (tableSchema == EmptyTableSchema) {
        return nullptr;
    }

    auto it = Registry_.find(tableSchema);
    if (it != Registry_.end()) {
        return *it;
    }
    auto result = New<TSharedTableSchema>(tableSchema, this);
    Registry_.insert(result.Get());
    return result;
}

void TSharedTableSchemaRegistry::DropSchema(TSharedTableSchema *sharedTableSchema)
{
    Registry_.erase(sharedTableSchema);
}

////////////////////////////////////////////////////////////////////////////////

TSharedTableSchema::TSharedTableSchema(TTableSchema tableSchema, const TSharedTableSchemaRegistryPtr& registry)
    : TableSchema_(std::move(tableSchema))
    , TableSchemaHash_(THash<TTableSchema>()(TableSchema_))
    , Registry_(registry)
{ }

TSharedTableSchema::~TSharedTableSchema()
{
    Registry_->DropSchema(this);
}

const NTableClient::TTableSchema& TSharedTableSchema::GetTableSchema() const
{
    return TableSchema_;
}

const TFuture<NYson::TYsonString>& TSharedTableSchema::GetYsonTableSchema() const
{
    if (!MemoizedYsonTableSchema_) {
        const auto& rpcInvoker = NRpc::TDispatcher::Get()->GetHeavyInvoker();
        MemoizedYsonTableSchema_ = BIND([this, this_ = MakeStrong(this)] {
            return BuildYsonStringFluently().Value(GetTableSchema());
        })
        .AsyncVia(rpcInvoker)
        .Run();
    }
    return MemoizedYsonTableSchema_;
}

size_t TSharedTableSchema::GetTableSchemaHash() const
{
    return TableSchemaHash_;
}

////////////////////////////////////////////////////////////////////////////////

size_t TSharedTableSchemaRegistry::TSharedTableSchemaHash::operator()(const TSharedTableSchema *sharedTableSchema) const
{
    return sharedTableSchema->GetTableSchemaHash();
}

size_t TSharedTableSchemaRegistry::TSharedTableSchemaHash::operator()(const TTableSchema &tableSchema) const
{
    return THash<TTableSchema>()(tableSchema);
}

////////////////////////////////////////////////////////////////////////////////

bool TSharedTableSchemaRegistry::TSharedTableSchemaEqual::operator()(
    const TSharedTableSchema *lhs,
    const TSharedTableSchema *rhs) const
{
    return lhs == rhs;
}

bool TSharedTableSchemaRegistry::TSharedTableSchemaEqual::operator()(
    const TSharedTableSchema *lhs,
    const TTableSchema &rhs) const
{
    return lhs->GetTableSchema() == rhs;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTableServer
