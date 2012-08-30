#include "helpers.h"

#include <ytlib/misc/error.h>

#include <ytlib/ytree/yson_format.h>
#include <ytlib/ytree/yson_string.h>
#include <ytlib/ytree/token.h>

namespace NYT {

using namespace NYTree;

namespace NFormats {

////////////////////////////////////////////////////////////////////////////////

void TFormatsConsumerBase::OnRaw(const TStringBuf& yson, EYsonType type)
{
    // On raw is called only for values in table

    if (type != EYsonType::Node) {
        YUNIMPLEMENTED();
    }

    NYTree::TLexer Lexer;
    
    Lexer.Reset();
    Lexer.Read(yson);
    Lexer.Finish();

    YCHECK(Lexer.GetState() == TLexer::EState::Terminal);
    auto token = Lexer.GetToken();
    switch(token.GetType()) {
        case ETokenType::String:
            OnStringScalar(token.GetStringValue());
            break;

        case ETokenType::Integer:
            OnIntegerScalar(token.GetIntegerValue());
            break;

        case ETokenType::Double:
            OnDoubleScalar(token.GetDoubleValue());
            break;

        case EntityToken:
            THROW_ERROR_EXCEPTION("Enitites are not supported as values in table");
            break;

        case BeginListToken:
            THROW_ERROR_EXCEPTION("Lists are not supported as values in table");
            break;

        case BeginMapToken:
            THROW_ERROR_EXCEPTION("Maps are not supported as values in table");
            break;

        case BeginAttributesToken:
            THROW_ERROR_EXCEPTION("Attributes are not supported as values in table");
            break;

        default:
            YUNREACHABLE();
    }
}

////////////////////////////////////////////////////////////////////////////////
            
} // namespace NFormats
} // namespace NYT
