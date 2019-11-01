#include "public.h"

namespace NYP::NServer::NObjects {

////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////

} // namespace NYP::NServer::NObjects

////////////////////////////////////////////////////////////////////////////////

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

template <>
TString FormatEnum<NYP::NServer::NObjects::EObjectType>(
    NYP::NServer::NObjects::EObjectType value,
    typename TEnumTraits<NYP::NServer::NObjects::EObjectType>::TType*)
{
    if (value == NYP::NServer::NObjects::EObjectType::IP4AddressPool) {
        return "ip4_address_pool";
    }
    return EncodeEnumValue(ToString(value));
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
