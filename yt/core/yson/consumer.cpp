#include "consumer.h"
#include "string.h"
#include "parser.h"

namespace NYT {
namespace NYson {

////////////////////////////////////////////////////////////////////////////////

void IYsonConsumer::OnRaw(const TYsonString& yson)
{
    OnRaw(yson.GetData(), yson.GetType());
}

////////////////////////////////////////////////////////////////////////////////

void TYsonConsumerBase::OnRaw(TStringBuf str, EYsonType type)
{
    ParseYsonStringBuffer(str, type, this);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYson
} // namespace NYT
