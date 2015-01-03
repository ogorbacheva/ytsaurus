#pragma once

#include "token.h"

namespace NYT {
namespace NYson {

////////////////////////////////////////////////////////////////////////////////

//! Indicates the beginning of a list.
const ETokenType BeginListToken = ETokenType::LeftBracket;
//! Indicates the end of a list.
const ETokenType EndListToken = ETokenType::RightBracket;

//! Indicates the beginning of a map.
const ETokenType BeginMapToken = ETokenType::LeftBrace;
//! Indicates the end of a map.
const ETokenType EndMapToken = ETokenType::RightBrace;

//! Indicates the beginning of an attribute map.
const ETokenType BeginAttributesToken = ETokenType::LeftAngle;
//! Indicates the end of an attribute map.
const ETokenType EndAttributesToken = ETokenType::RightAngle;

//! Separates items in lists.
const ETokenType ListItemSeparatorToken = ETokenType::Semicolon;
//! Separates items in maps, attributes.
const ETokenType KeyedItemSeparatorToken = ETokenType::Semicolon;
//! Separates keys from values in maps.
const ETokenType KeyValueSeparatorToken = ETokenType::Equals;

//! Indicates an entity.
const ETokenType EntityToken = ETokenType::Hash;

////////////////////////////////////////////////////////////////////////////////

} // namespace NYson
} // namespace NYT

