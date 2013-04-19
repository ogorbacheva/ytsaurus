#pragma once

#include "public.h"
#include "token.h"

namespace NYT {
namespace NYson {

////////////////////////////////////////////////////////////////////////////////

class TStatelessLexer
{
public:
    TStatelessLexer();

    ~TStatelessLexer();

    size_t GetToken(const TStringBuf& data, TToken* token);

private:
    class TImpl;
    THolder<TImpl> Impl;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYson
} // namespace NYT
