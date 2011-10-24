#include "stdafx.h"
#include "common.h"

#include "yson_writer.h"
#include "yson_format.h"

#include "../misc/serialize.h"

namespace NYT {
namespace NYTree {

////////////////////////////////////////////////////////////////////////////////
    
TYsonWriter::TYsonWriter(TOutputStream* stream, bool isBinary)
    : Stream(stream)
    , IsFirstItem(false)
    , IsEmptyEntity(false)
    , Indent(0)
    , IsBinary(isBinary)
{ }

void TYsonWriter::WriteIndent()
{
    for (int i = 0; i < IndentSize * Indent; ++i) {
        Stream->Write(' ');
    }
}

void TYsonWriter::WriteStringScalar(const Stroka& value)
{
    if (IsBinary) {
        Stream->Write(StringMarker);
        WriteVarInt32(static_cast<i32>(value.length()), Stream);
        Stream->Write(&value, value.length());
    } else {
        // TODO: escaping
        Stream->Write('"');
        Stream->Write(value);
        Stream->Write('"');
    }
}

void TYsonWriter::WriteMapItem(const Stroka& name)
{
    CollectionItem(MapItemSeparator);
    WriteStringScalar(name);
    Stream->Write(' ');
    Stream->Write(KeyValueSeparator);
    Stream->Write(' ');
    IsFirstItem = false;
}

void TYsonWriter::BeginCollection(char openBracket)
{
    Stream->Write(openBracket);
    IsFirstItem = true;
}

void TYsonWriter::CollectionItem(char separator)
{
    if (IsFirstItem) {
        Stream->Write('\n');
        ++Indent;
    } else {
        Stream->Write(separator);
        Stream->Write('\n');
    }
    if (!IsBinary) {
        WriteIndent();
    }
    IsFirstItem = false;
}

void TYsonWriter::EndCollection(char closeBracket)
{
    if (!IsFirstItem) {
        Stream->Write('\n');
        --Indent;
        if (!IsBinary) {
            WriteIndent();
        }
    }
    Stream->Write(closeBracket);
    IsFirstItem = false;
}


void TYsonWriter::OnStringScalar(const Stroka& value, bool hasAttributes)
{
    // TODO: binary?
    WriteStringScalar(value);
    if (hasAttributes) {
        Stream->Write(' ');
    }
}

void TYsonWriter::OnInt64Scalar(i64 value, bool hasAttributes)
{
    UNUSED(hasAttributes);
    if (IsBinary) {
        Stream->Write(Int64Marker);
        WriteVarInt64(value, Stream);
    } else {
        Stream->Write(ToString(value));
    }
    if (hasAttributes) {
        Stream->Write(' ');
    }
}

void TYsonWriter::OnDoubleScalar(double value, bool hasAttributes)
{
    UNUSED(hasAttributes);
    if (IsBinary) {
        Stream->Write(DoubleMarker);
        Stream->Write(&value, sizeof(double));
    } else {
        Stream->Write(ToString(value));
    }
    if (hasAttributes) {
        Stream->Write(' ');
    }
}

void TYsonWriter::OnEntity(bool hasAttributes)
{
    if (!hasAttributes) {
        Stream->Write(BeginAttributesSymbol);
        Stream->Write(EndAttributesSymbol);
    }
}

void TYsonWriter::OnBeginList()
{
    BeginCollection(BeginListSymbol);
}

void TYsonWriter::OnListItem()
{
    CollectionItem(ListItemSeparator);
}

void TYsonWriter::OnEndList(bool hasAttributes)
{
    EndCollection(EndListSymbol);
    if (hasAttributes) {
        Stream->Write(' ');
    }
}

void TYsonWriter::OnBeginMap()
{
    BeginCollection(BeginMapSymbol);
}

void TYsonWriter::OnMapItem(const Stroka& name)
{
    WriteMapItem(name);
}

void TYsonWriter::OnEndMap(bool hasAttributes)
{
    EndCollection(EndMapSymbol);
    if (hasAttributes) {
        Stream->Write(' ');
    }
}

void TYsonWriter::OnBeginAttributes()
{
    BeginCollection(BeginAttributesSymbol);
}

void TYsonWriter::OnAttributesItem(const Stroka& name)
{
    WriteMapItem(name);
}

void TYsonWriter::OnEndAttributes()
{
    EndCollection(EndAttributesSymbol);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYTree
} // namespace NYT
