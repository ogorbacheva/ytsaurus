#pragma once

namespace NYT {
namespace NCGroup {

////////////////////////////////////////////////////////////////////////////////

class TNonOwningCGroup;
class TCGroup;
class TCpuAccounting;
class TBlockIO;
class TMemory;

////////////////////////////////////////////////////////////////////////////////

bool IsValidCGroupType(const Stroka& type);

////////////////////////////////////////////////////////////////////////////////

} // namespace NCGroup
} // namespace NYT
