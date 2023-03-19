#pragma once

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

//===- NativeTypeTypedef.h - info about typedef ------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPETYPEDEF_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPETYPEDEF_H

#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/DebugInfo/PDB/Native/NativeRawSymbol.h"
#include "llvm/DebugInfo/PDB/Native/NativeSession.h"

namespace llvm {
namespace pdb {

class NativeTypeTypedef : public NativeRawSymbol {
public:
  // Create a pointer record for a non-simple type.
  NativeTypeTypedef(NativeSession &Session, SymIndexId Id,
                    codeview::UDTSym Typedef);

  ~NativeTypeTypedef() override;

  void dump(raw_ostream &OS, int Indent, PdbSymbolIdField ShowIdFields,
            PdbSymbolIdField RecurseIdFields) const override;

  std::string getName() const override;
  SymIndexId getTypeId() const override;

protected:
  codeview::UDTSym Record;
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEPOINTER_H

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
