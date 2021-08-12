#include "helpers.h"
#include "config.h"
#include "table.h"

#include <yt/yt/ytlib/chunk_client/data_source.h>

#include <yt/yt/client/table_client/row_buffer.h>

namespace NYT::NControllerAgent::NControllers {

using namespace NChunkClient;
using namespace NChunkPools;
using namespace NTableClient;
using namespace NYTree;
using namespace NYson;

////////////////////////////////////////////////////////////////////////////////

TBoundaryKeys BuildBoundaryKeysFromOutputResult(
    const NScheduler::NProto::TOutputResult& boundaryKeys,
    const TStreamDescriptor& streamDescriptor,
    const TRowBufferPtr& rowBuffer)
{
    YT_VERIFY(!boundaryKeys.empty());
    YT_VERIFY(boundaryKeys.sorted());
    YT_VERIFY(!streamDescriptor.TableWriterOptions->ValidateUniqueKeys || boundaryKeys.unique_keys());

    auto trimAndCaptureKey = [&] (const TLegacyOwningKey& key) {
        int limit = streamDescriptor.TableUploadOptions.TableSchema->GetKeyColumnCount();
        if (key.GetCount() > limit) {
            // NB: This can happen for a teleported chunk from a table with a wider key in sorted (but not unique_keys) mode.
            YT_VERIFY(!streamDescriptor.TableWriterOptions->ValidateUniqueKeys);
            return TKey::FromRowUnchecked(rowBuffer->CaptureRow(MakeRange(key.Begin(), limit)), limit);
        } else {
            return TKey::FromRowUnchecked(rowBuffer->CaptureRow(MakeRange(key.Begin(), key.GetCount())), key.GetCount());
        }
    };

    return TBoundaryKeys {
        trimAndCaptureKey(FromProto<TLegacyOwningKey>(boundaryKeys.min())),
        trimAndCaptureKey(FromProto<TLegacyOwningKey>(boundaryKeys.max())),
    };
}

////////////////////////////////////////////////////////////////////////////////

TDataSourceDirectoryPtr BuildDataSourceDirectoryFromInputTables(const std::vector<TInputTablePtr>& inputTables)
{
    auto dataSourceDirectory = New<TDataSourceDirectory>();
    for (const auto& inputTable : inputTables) {
        auto dataSource = (inputTable->Dynamic && inputTable->Schema->IsSorted())
            ? MakeVersionedDataSource(
                inputTable->GetPath(),
                inputTable->Schema,
                inputTable->Path.GetColumns(),
                inputTable->OmittedInaccessibleColumns,
                inputTable->Path.GetTimestamp().value_or(AsyncLastCommittedTimestamp),
                inputTable->Path.GetRetentionTimestamp().value_or(NullTimestamp),
                inputTable->ColumnRenameDescriptors)
            : MakeUnversionedDataSource(
                inputTable->GetPath(),
                inputTable->Schema,
                inputTable->Path.GetColumns(),
                inputTable->OmittedInaccessibleColumns,
                inputTable->ColumnRenameDescriptors);

        dataSource.SetForeign(inputTable->IsForeign());
        dataSourceDirectory->DataSources().push_back(dataSource);
    }

    return dataSourceDirectory;
}

////////////////////////////////////////////////////////////////////////////////

void TControllerFeatures::AddSingular(TStringBuf name, double value)
{
    Features_[name] += value;
}

void TControllerFeatures::AddCounted(TStringBuf name, double value)
{
    TString sumFeature{name};
    sumFeature += ".sum";
    Features_[sumFeature] += value;
    TString countFeature{name};
    countFeature += ".count";
    Features_[countFeature] += 1;
}

void Serialize(const TControllerFeatures& features, NYson::IYsonConsumer* consumer)
{
    BuildYsonFluently(consumer).BeginMap()
        .Item("tags").Value(features.Tags_)
        .Item("features").Value(features.Features_)
    .EndMap();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NControllerAgent::NControllers
