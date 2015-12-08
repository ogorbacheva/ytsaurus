#pragma once

#include "public.h"
#include "config.h"
#include "helpers.h"
#include "yamr_table.h"
#include "schemaless_writer_adapter.h"

#include <yt/ytlib/table_client/public.h>

#include <yt/core/misc/blob_output.h>
#include <yt/core/misc/nullable.h>

namespace NYT {
namespace NFormats {

////////////////////////////////////////////////////////////////////////////////

class TSchemalessWriterForYamrBase
    : public TSchemalessFormatWriterBase
{
public:
    TSchemalessWriterForYamrBase(
        NTableClient::TNameTablePtr nameTable,
        NConcurrency::IAsyncOutputStreamPtr output,
        bool enableContextSaving,
<<<<<<< HEAD
        TControlAttributesConfigPtr controlAttributesConfig,
        int keyColumnCount,
        TYamrFormatConfigBasePtr config);

=======
        bool enableKeySwitch,
        int keyColumnCount,
        TYamrFormatConfigBasePtr config);

    // ISchemalessFormatWriter overrides.
    virtual void WriteTableIndex(i32 tableIndex) override;
    virtual void WriteRangeIndex(i32 rangeIndex) override;
    virtual void WriteRowIndex(i64 rowIndex) override;

>>>>>>> origin/prestable/0.17.4
protected:
    TYamrFormatConfigBasePtr Config_;

    void WriteInLenvalMode(const TStringBuf& value);
    
    void EscapeAndWrite(const TStringBuf& value, TLookupTable stops, TEscapeTable escapes);
<<<<<<< HEAD

    virtual void WriteTableIndex(i64 tableIndex) override;
    virtual void WriteRangeIndex(i64 rangeIndex) override;
    virtual void WriteRowIndex(i64 rowIndex) override;
=======
>>>>>>> origin/prestable/0.17.4
};

DEFINE_REFCOUNTED_TYPE(TSchemalessWriterForYamrBase)

////////////////////////////////////////////////////////////////////////////////

} // namespace NFormats
} // namespace NYT
