#include "floating_point_column_reader.h"

#include "column_reader_detail.h"
#include "helpers.h"

#include <yt/yt/client/table_client/row_batch.h>
#include <yt/yt/client/table_client/logical_type.h>

#include <yt/yt/core/misc/bitmap.h>

namespace NYT::NTableChunkFormat {

using namespace NTableClient;
using namespace NProto;

////////////////////////////////////////////////////////////////////////////////

template <typename T>
class TFloatingPointValueExtractorBase
{
public:
    void ExtractValue(TUnversionedValue* value, i64 valueIndex, int id, bool aggregate) const
    {
        if (NullBitmap_[valueIndex]) {
            *value = MakeUnversionedSentinelValue(EValueType::Null, id, aggregate);
        } else {
            *value = MakeUnversionedDoubleValue(Values_[valueIndex], id, aggregate);
        }
    }

protected:
    TRange<T> Values_;
    TReadOnlyBitmap NullBitmap_;

    const char* InitValueReader(const char* ptr)
    {
        ui64 valueCount = *reinterpret_cast<const ui64*>(ptr);
        ptr += sizeof(ui64);

        Values_ = MakeRange(reinterpret_cast<const T*>(ptr), valueCount);
        ptr += sizeof(T) * valueCount;

        NullBitmap_ = TReadOnlyBitmap(Values_.end(), valueCount);
        ptr += AlignUp(NullBitmap_.GetByteSize(), SerializationAlignment);

        return ptr;
    }
};

////////////////////////////////////////////////////////////////////////////////

template <typename T>
class TDirectDenseVersionedFloatingPointValueExtractor
    : public TDenseVersionedValueExtractorBase
    , public TFloatingPointValueExtractorBase<T>
{
public:
    TDirectDenseVersionedFloatingPointValueExtractor(TRef data, const NProto::TSegmentMeta& meta, bool aggregate)
        : TDenseVersionedValueExtractorBase(meta, aggregate)
    {
        const char* ptr = data.Begin();
        ptr = InitDenseReader(ptr);
        ptr = this->InitValueReader(ptr);
        YT_VERIFY(ptr == data.End());
    }
};

////////////////////////////////////////////////////////////////////////////////

template <typename T>
class TDirectSparseVersionedFloatingPointValueExtractor
    : public TSparseVersionedValueExtractorBase
    , public TFloatingPointValueExtractorBase<T>
{
public:
    TDirectSparseVersionedFloatingPointValueExtractor(TRef data, const NProto::TSegmentMeta& meta, bool aggregate)
        : TSparseVersionedValueExtractorBase(meta, aggregate)
    {
        const char* ptr = data.Begin();
        ptr = InitSparseReader(ptr);
        ptr = this->InitValueReader(ptr);
        YT_VERIFY(ptr == data.End());
    }
};

////////////////////////////////////////////////////////////////////////////////

template <typename T>
class TVersionedFloatingPointColumnReader
    : public TVersionedColumnReaderBase
{
public:
    TVersionedFloatingPointColumnReader(const TColumnMeta& columnMeta, int columnId, bool aggregate)
        : TVersionedColumnReaderBase(columnMeta, columnId, aggregate)
    { }

private:
    virtual std::unique_ptr<IVersionedSegmentReader> CreateSegmentReader(int segmentIndex) override
    {
        using TDirectDenseReader = TDenseVersionedSegmentReader<TDirectDenseVersionedFloatingPointValueExtractor<T>>;
        using TDirectSparseReader = TSparseVersionedSegmentReader<TDirectSparseVersionedFloatingPointValueExtractor<T>>;

        const auto& meta = ColumnMeta_.segments(segmentIndex);
        auto dense = meta.HasExtension(TDenseVersionedSegmentMeta::dense_versioned_segment_meta);

        if (dense) {
            return DoCreateSegmentReader<TDirectDenseReader>(meta);
        } else {
            return DoCreateSegmentReader<TDirectSparseReader>(meta);
        }
    }
};

////////////////////////////////////////////////////////////////////////////////

template <typename T>
std::unique_ptr<IVersionedColumnReader> CreateVersionedFloatingPointColumnReader(
    const TColumnMeta& columnMeta,
    int columnId,
    bool aggregate)
{
    return std::make_unique<TVersionedFloatingPointColumnReader<T>>(
        columnMeta,
        columnId,
        aggregate);
}

////////////////////////////////////////////////////////////////////////////////

template <typename T>
class TUnversionedFloatingPointValueExtractor
    : public TFloatingPointValueExtractorBase<T>
{
public:
    TUnversionedFloatingPointValueExtractor(TRef data, const TSegmentMeta& meta)
    {
        const char* ptr = data.Begin();
        ptr = this->InitValueReader(data.Begin());
        YT_VERIFY(ptr == data.End());
    }

    int GetBatchColumnCount()
    {
        return 1;
    }

    void ReadColumnarBatch(
        i64 startRowIndex,
        i64 rowCount,
        TMutableRange<NTableClient::IUnversionedColumnarRowBatch::TColumn> columns)
    {
        YT_VERIFY(columns.size() == 1);
        auto& column = columns[0];
        ReadColumnarFloatingPointValues(
            &column,
            startRowIndex,
            rowCount,
            this->Values_);
        ReadColumnarNullBitmap(
            &column,
            startRowIndex,
            rowCount,
            this->NullBitmap_.GetData());
    }

    i64 EstimateDataWeight(i64 lowerRowIndex, i64 upperRowIndex)
    {
        return (upperRowIndex - lowerRowIndex) * sizeof(T);
    }
};

////////////////////////////////////////////////////////////////////////////////

template <typename T>
class TUnversionedFloatingPointColumnReader
    : public TUnversionedColumnReaderBase
{
public:
    TUnversionedFloatingPointColumnReader(
        const TColumnMeta& columnMeta,
        int columnIndex,
        int columnId,
        std::optional<ESortOrder> sortOrder)
        : TUnversionedColumnReaderBase(
            columnMeta,
            columnIndex,
            columnId,
            sortOrder)
    {
        static_assert(std::is_floating_point_v<T>);
    }

    virtual std::pair<i64, i64> GetEqualRange(
        const TUnversionedValue& value,
        i64 lowerRowIndex,
        i64 upperRowIndex) override
    {
        return DoGetEqualRange<EValueType::Double>(
            value,
            lowerRowIndex,
            upperRowIndex);
    }

private:
    virtual std::unique_ptr<IUnversionedSegmentReader> CreateSegmentReader(int segmentIndex, bool /* scan */) override
    {
        typedef TDenseUnversionedSegmentReader<
            EValueType::Double,
            TUnversionedFloatingPointValueExtractor<T>> TSegmentReader;

        const auto& meta = ColumnMeta_.segments(segmentIndex);
        return DoCreateSegmentReader<TSegmentReader>(meta);
    }
};

////////////////////////////////////////////////////////////////////////////////

template <typename T>
std::unique_ptr<IUnversionedColumnReader> CreateUnversionedFloatingPointColumnReader(
    const TColumnMeta& columnMeta,
    int columnIndex,
    int columnId,
    std::optional<ESortOrder> sortOrder)
{
    return std::make_unique<TUnversionedFloatingPointColumnReader<T>>(
        columnMeta,
        columnIndex,
        columnId,
        sortOrder);
}

////////////////////////////////////////////////////////////////////////////////

template
std::unique_ptr<IVersionedColumnReader> CreateVersionedFloatingPointColumnReader<float>(
    const NProto::TColumnMeta& columnMeta,
    int columnId,
    bool aggregate);

template
std::unique_ptr<IVersionedColumnReader> CreateVersionedFloatingPointColumnReader<double>(
    const NProto::TColumnMeta& columnMeta,
    int columnId,
    bool aggregate);

template
std::unique_ptr<IUnversionedColumnReader> CreateUnversionedFloatingPointColumnReader<float>(
    const NProto::TColumnMeta& columnMeta,
    int columnIndex,
    int columnId,
    std::optional<ESortOrder> sortOrder);

template
std::unique_ptr<IUnversionedColumnReader> CreateUnversionedFloatingPointColumnReader<double>(
    const NProto::TColumnMeta& columnMeta,
    int columnIndex,
    int columnId,
    std::optional<ESortOrder> sortOrder);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTableChunkFormat
