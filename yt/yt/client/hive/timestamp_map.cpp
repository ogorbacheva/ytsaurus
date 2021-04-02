#include "timestamp_map.h"

#include <yt/yt/core/misc/serialize.h>

#include <yt/yt_proto/yt/client/hive/proto/timestamp_map.pb.h>

namespace NYT::NHiveClient {

using namespace NObjectClient;

////////////////////////////////////////////////////////////////////////////////

TTimestamp TTimestampMap::GetTimestamp(TCellTag cellTag) const
{
    for (auto [someCellTag, someTimestamp] : Timestamps) {
        if (someCellTag == cellTag) {
            return someTimestamp;
        }
    }
    YT_ABORT();
}

void TTimestampMap::Persist(const TStreamPersistenceContext& context)
{
    using NYT::Persist;
    Persist<TVectorSerializer<TTupleSerializer<std::pair<TCellTag, TTimestamp>, 2>>>(
        context,
        Timestamps);
}

void ToProto(NProto::TTimestampMap* protoMap, const TTimestampMap& map)
{
    protoMap->clear_cell_tags();
    protoMap->clear_timestamps();
    for (auto [cellTag, timestamp] : map.Timestamps) {
        protoMap->add_cell_tags(cellTag);
        protoMap->add_timestamps(timestamp);
    }
}

void FromProto(TTimestampMap* map, const NProto::TTimestampMap& protoMap)
{
    map->Timestamps.clear();
    YT_VERIFY(protoMap.cell_tags_size() == protoMap.timestamps_size());
    for (int index = 0; index < protoMap.cell_tags_size(); ++index) {
        map->Timestamps.emplace_back(
            protoMap.cell_tags(index),
            protoMap.timestamps(index));
    }
}

void FormatValue(TStringBuilderBase* builder, const TTimestampMap& map, TStringBuf /*spec*/)
{
    builder->AppendChar('{');
    bool first = true;
    for (auto [cellTag, timestamp] : map.Timestamps) {
        if (!first) {
            builder->AppendString(TStringBuf(", "));
        }
        builder->AppendFormat("%v => %llx", cellTag, timestamp);
        first = false;
    }
    builder->AppendChar('}');
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NHiveClient
