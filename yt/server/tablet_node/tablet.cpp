#include "stdafx.h"
#include "tablet.h"
#include "automaton.h"
#include "store_manager.h"

#include <core/misc/serialize.h>
#include <core/misc/protobuf_helpers.h>

#include <ytlib/new_table_client/schema.h>
#include <ytlib/new_table_client/name_table.h>
#include <ytlib/new_table_client/chunk_meta.pb.h>

#include <ytlib/tablet_client/config.h>

namespace NYT {
namespace NTabletNode {

using namespace NHydra;
using namespace NVersionedTableClient;
using namespace NTabletClient;

////////////////////////////////////////////////////////////////////////////////

TTablet::TTablet(const TTabletId& id)
    : Id_(id)
{ }

TTablet::TTablet(
    const TTabletId& id,
    const TTableSchema& schema,
    const TKeyColumns& keyColumns,
    NTabletClient::TTableMountConfigPtr config)
    : Id_(id)
    , Schema_(schema)
    , KeyColumns_(keyColumns)
    , Config_(config)
    , NameTable_(TNameTable::FromSchema(Schema_))
{ }

TTablet::~TTablet()
{ }

void TTablet::Save(TSaveContext& context) const
{
    using NYT::Save;

    Save(context, Id_);
    Save(context, NYT::ToProto<NVersionedTableClient::NProto::TTableSchemaExt>(Schema_));
    Save(context, KeyColumns_);

    // TODO(babenko)
}

void TTablet::Load(TLoadContext& context)
{
    using NYT::Load;

    Load(context, Id_);
    Schema_ = FromProto<TTableSchema>(Load<NVersionedTableClient::NProto::TTableSchemaExt>(context));
    Load(context, KeyColumns_);

    // TODO(babenko)
}

const TTabletId& TTablet::GetId() const
{
    return Id_;
}

const TTableSchema& TTablet::Schema() const
{
    return Schema_;
}

const TKeyColumns& TTablet::KeyColumns() const
{
    return KeyColumns_;
}

const TTableMountConfigPtr& TTablet::GetConfig() const
{
    return Config_;
}

const TNameTablePtr& TTablet::GetNameTable() const
{
    return NameTable_;
}

const TStoreManagerPtr& TTablet::GetStoreManager() const
{
    return StoreManager_;
}

void TTablet::SetStoreManager(TStoreManagerPtr manager)
{
    StoreManager_ = manager;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NTabletNode
} // namespace NYT

