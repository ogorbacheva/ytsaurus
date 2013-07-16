#pragma once

#include "public.h"
#include "config.h"
#include "helpers.h"
#include "yamr_table.h"

#include <ytlib/table_client/public.h>

#include <ytlib/misc/blob_output.h>
#include <ytlib/misc/nullable.h>

namespace NYT {
namespace NFormats {

////////////////////////////////////////////////////////////////////////////////

//! Note: #TYamrWriter supports only tabular data
class TYamrWriter
    : public virtual TFormatsConsumerBase
{
public:
    explicit TYamrWriter(
        TOutputStream* stream,
        TYamrFormatConfigPtr config = New<TYamrFormatConfig>());

    ~TYamrWriter();

    // IYsonConsumer overrides.
    virtual void OnStringScalar(const TStringBuf& value) override;
    virtual void OnIntegerScalar(i64 value) override;
    virtual void OnDoubleScalar(double value) override;
    virtual void OnEntity() override;
    virtual void OnBeginList() override;
    virtual void OnListItem() override;
    virtual void OnEndList() override;
    virtual void OnBeginMap() override;
    virtual void OnKeyedItem(const TStringBuf& key) override;
    virtual void OnEndMap() override;
    virtual void OnBeginAttributes() override;
    virtual void OnEndAttributes() override;

private:
    DECLARE_ENUM(EState,
        (None)
        (ExpectColumnName)
        (ExpectValue)
        (ExpectAttributeName)
        (ExpectAttributeValue)
        (ExpectEndAttributes)
        (ExpectEntity)
    );

    DECLARE_ENUM(EValueType,
        (ExpectKey)
        (ExpectSubkey)
        (ExpectValue)
        (ExpectUnknown)
    );

    TOutputStream* Stream;
    TYamrFormatConfigPtr Config;

    TNullable<TStringBuf> Key;
    TNullable<TStringBuf> Subkey;
    TNullable<TStringBuf> Value;

    TYamrTable Table;

    EState State;
    EValueType ValueType;
    NTableClient::EControlAttribute ControlAttribute;

    void WriteRow();
    void WriteInLenvalMode(const TStringBuf& value);

    void EscapeAndWrite(const TStringBuf& value, bool inKey);
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NFormats
} // namespace NYT
