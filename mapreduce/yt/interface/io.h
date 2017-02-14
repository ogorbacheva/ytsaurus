#pragma once

#include "fwd.h"
#include "common.h"
#include "node.h"
#include "mpl.h"

#include <contrib/libs/protobuf/message.h>

#include <util/stream/input.h>
#include <util/stream/output.h>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

struct INodeReaderImpl;
struct IYaMRReaderImpl;
struct IProtoReaderImpl;
struct INodeWriterImpl;
struct IYaMRWriterImpl;
struct IProtoWriterImpl;

////////////////////////////////////////////////////////////////////////////////

class TIOException
    : public yexception
{ };

///////////////////////////////////////////////////////////////////////////////

class IFileReader
    : public TThrRefBase
    , public TInputStream
{ };

class IFileWriter
    : public TThrRefBase
    , public TOutputStream
{ };

////////////////////////////////////////////////////////////////////////////////

template <class T, class>
class TTableReader
    : public TThrRefBase
{
public:
    const T& GetRow() const; // may be a template function
    bool IsValid() const;
    ui32 GetTableIndex() const;
    ui64 GetRowIndex() const;
    void Next();
};

////////////////////////////////////////////////////////////////////////////////

template <class T, class>
class TTableRangesReader
    : public TThrRefBase
{
public:
    TTableReader<T>& GetRange();
    bool IsValid() const;
    void Next();
};

////////////////////////////////////////////////////////////////////////////////

template <class T, class>
class TTableWriter
    : public TThrRefBase
{
public:
    void AddRow(const T& row); // may be a template function
    void Finish();
};

////////////////////////////////////////////////////////////////////////////////

struct TYaMRRow
{
    TStringBuf Key;
    TStringBuf SubKey;
    TStringBuf Value;
};

////////////////////////////////////////////////////////////////////////////////

template <class TDerived>
struct TIOOptions
{
    using TSelf = TDerived;

    FLUENT_FIELD_OPTION(TNode, Config);
};

struct TFileReaderOptions
    : public TIOOptions<TFileReaderOptions>
{ };

struct TFileWriterOptions
    : public TIOOptions<TFileWriterOptions>
{ };

struct TTableReaderOptions
    : public TIOOptions<TTableReaderOptions>
{
    FLUENT_FIELD_DEFAULT(size_t, SizeLimit, 4 << 20);
};

struct TTableWriterOptions
    : public TIOOptions<TTableWriterOptions>
{ };

////////////////////////////////////////////////////////////////////////////////

class IIOClient
{
public:
    virtual IFileReaderPtr CreateFileReader(
        const TRichYPath& path,
        const TFileReaderOptions& options = TFileReaderOptions()) = 0;

    virtual IFileWriterPtr CreateFileWriter(
        const TRichYPath& path,
        const TFileWriterOptions& options = TFileWriterOptions()) = 0;

    template <class T>
    TTableReaderPtr<T> CreateTableReader(
        const TRichYPath& path,
        const TTableReaderOptions& options = TTableReaderOptions());

    template <class T>
    TTableWriterPtr<T> CreateTableWriter(
        const TRichYPath& path,
        const TTableWriterOptions& options = TTableWriterOptions());

private:
    virtual TIntrusivePtr<INodeReaderImpl> CreateNodeReader(
        const TRichYPath& path, const TTableReaderOptions& options) = 0;

    virtual TIntrusivePtr<IYaMRReaderImpl> CreateYaMRReader(
        const TRichYPath& path, const TTableReaderOptions& options) = 0;

    virtual TIntrusivePtr<IProtoReaderImpl> CreateProtoReader(
        const TRichYPath& path,
        const TTableReaderOptions& options,
        const ::google::protobuf::Message* prototype) = 0;

    virtual TIntrusivePtr<INodeWriterImpl> CreateNodeWriter(
        const TRichYPath& path, const TTableWriterOptions& options) = 0;

    virtual TIntrusivePtr<IYaMRWriterImpl> CreateYaMRWriter(
        const TRichYPath& path, const TTableWriterOptions& options) = 0;

    virtual TIntrusivePtr<IProtoWriterImpl> CreateProtoWriter(
        const TRichYPath& path,
        const TTableWriterOptions& options,
        const ::google::protobuf::Message* prototype) = 0;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT

#define IO_INL_H_
#include "io-inl.h"
#undef IO_INL_H_

