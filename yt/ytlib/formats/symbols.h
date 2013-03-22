#pragma once

#include "public.h"

#include <vector>
#include <string>

// XXX(sandello): Define this to enable SSE4.2-baked symbol lookup.
#ifdef __SSE4_2__
#define _YT_USE_SSE42_
#endif

#ifdef _YT_USE_SSE42_
#include <nmmintrin.h>
#endif

namespace NYT {
namespace NFormats {

////////////////////////////////////////////////////////////////////////////////

class TLookupTable
{
public:
    TLookupTable();

    void Fill(const char* begin, const char* end);
    void Fill(const std::vector<char>& v);
    void Fill(const std::string& s);

    const char* FindNext(const char* begin, const char* end) const;

private:
#ifdef _YT_USE_SSE42_
#ifdef _MSC_VER
#define DECL_PREFIX __declspec(align(16))
#define DECL_SUFFIX
#else
#define DECL_PREFIX
#define DECL_SUFFIX __attribute__((aligned(16)))
#endif
    DECL_PREFIX __m128i Symbols DECL_SUFFIX;
    int SymbolCount;
#else
    bool Bitmap[256];
#endif

};

class TEscapeTable
{
public:
    explicit TEscapeTable(bool EscapeCarriageReturn);

    char Forward[256];
    char Backward[256];
};

void WriteEscaped(
    TOutputStream* stream,
    const TStringBuf& string,
    const TLookupTable& lookupTable,
    const TEscapeTable& escapeTable,
    char escapingSymbol);

////////////////////////////////////////////////////////////////////////////////

} // namespace NFormats
} // namespace NYT
