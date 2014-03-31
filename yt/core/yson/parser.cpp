#include "stdafx.h"
#include "parser.h"
#include "consumer.h"
#include "format.h"
#include "parser_detail.h"

#include <core/misc/error.h>

#include <core/concurrency/coroutine.h>

namespace NYT {
namespace NYson {

using namespace NConcurrency;

////////////////////////////////////////////////////////////////////////////////

class TYsonParser::TImpl
{   
private:
    typedef TCoroutine<int(const char* begin, const char* end, bool finish)> TParserCoroutine;

    TParserCoroutine ParserCoroutine;

public:
    TImpl(
        IYsonConsumer* consumer, 
        EYsonType parsingMode = EYsonType::Node, 
        bool enableLinePositionInfo = false,
        TNullable<i64> memoryLimit = Null) 
    {
        ParserCoroutine.Reset(BIND([=] (TParserCoroutine& self, const char* begin, const char* end, bool finish) {
            ParseYsonStreamImpl<IYsonConsumer, TBlockReader<TParserCoroutine> >(
                TBlockReader<TParserCoroutine>(self, begin, end, finish),
                consumer,
                parsingMode,
                enableLinePositionInfo,
                memoryLimit);
        }));
    }

    void Read(const char* begin, const char* end, bool finish = false)
    {
        if (ParserCoroutine.GetState() != EFiberState::Terminated) {
            ParserCoroutine.Run(begin, end, finish);
        } else {
            THROW_ERROR_EXCEPTION("Input is already parsed");
        }
    }

    void Read(const TStringBuf& data, bool finish = false)
    {
        Read(data.begin(), data.end(), finish);
    }

    void Finish()
    {
        Read(0, 0, true);
    }
};

////////////////////////////////////////////////////////////////////////////////

TYsonParser::TYsonParser(
    IYsonConsumer *consumer,
    EYsonType type,
    bool enableLinePositionInfo,
    TNullable<i64> memoryLimit)
    : Impl(new TImpl(consumer, type, enableLinePositionInfo, memoryLimit))
{ }

TYsonParser::~TYsonParser()
{ }

void TYsonParser::Read(const TStringBuf& data)
{
    Impl->Read(data);
}

void TYsonParser::Finish()
{
    Impl->Finish();
}

////////////////////////////////////////////////////////////////////////////////

class TStatelessYsonParser::TImpl
{
private:
    std::unique_ptr<TStatelessYsonParserImplBase> Impl;

public:
    TImpl(
        IYsonConsumer *consumer,
        bool enableLinePositionInfo)
        : Impl(
            enableLinePositionInfo
            ? static_cast<TStatelessYsonParserImplBase*>(new TStatelessYsonParserImpl<IYsonConsumer, true>(consumer)) 
            : static_cast<TStatelessYsonParserImplBase*>(new TStatelessYsonParserImpl<IYsonConsumer, false>(consumer)))
    { }

    void Parse(const TStringBuf& data, EYsonType type = EYsonType::Node) 
    {
        Impl->Parse(data, type);
    }
};

////////////////////////////////////////////////////////////////////////////////

TStatelessYsonParser::TStatelessYsonParser(
    IYsonConsumer *consumer,
    bool enableLinePositionInfo)
    : Impl(new TImpl(consumer, enableLinePositionInfo))
{ }

TStatelessYsonParser::~TStatelessYsonParser()
{ }

void TStatelessYsonParser::Parse(const TStringBuf& data, EYsonType type)
{
    Impl->Parse(data, type);
}

////////////////////////////////////////////////////////////////////////////////

void ParseYsonStringBuffer(
    const TStringBuf& buffer,
    IYsonConsumer* consumer,
    EYsonType type,
    bool enableLinePositionInfo)
{
    ParseYsonStreamImpl<IYsonConsumer, TStringReader>(
        TStringReader(buffer.begin(), buffer.end()),
        consumer,
        type,
        enableLinePositionInfo);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYson
} // namespace NYT
