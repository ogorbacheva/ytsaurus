#pragma once

#include "token.h"

namespace NYT::NYPath {

////////////////////////////////////////////////////////////////////////////////

class TTokenizer
{
public:
    explicit TTokenizer(const TYPath& path);

    ETokenType Advance();

    ETokenType GetType() const;
    TStringBuf GetToken() const;
    TStringBuf GetPrefix() const;
    TStringBuf GetSuffix() const;
    TStringBuf GetInput() const;
    const TString& GetLiteralValue() const;

    void Expect(ETokenType expectedType);
    void Skip(ETokenType expectedType);
    void ThrowUnexpected();

private:
    const TYPath Path_;

    ETokenType Type_;
    ETokenType PreviousType_;
    TStringBuf Token_;
    TStringBuf Input_;
    TString LiteralValue_;

    void SetType(ETokenType type);
    const char* AdvanceEscaped(const char* current);
    static int ParseHexDigit(char ch, TStringBuf context);
    static void ThrowMalformedEscapeSequence(TStringBuf context);
};

////////////////////////////////////////////////////////////////////////////////

bool HasPrefix(const TYPath& fullPath, const TYPath& prefixPath);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NYPath
