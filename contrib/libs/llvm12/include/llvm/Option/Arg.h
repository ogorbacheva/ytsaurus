#pragma once

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

//===- Arg.h - Parsed Argument Classes --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Defines the llvm::Arg class for parsed arguments.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_OPTION_ARG_H
#define LLVM_OPTION_ARG_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Option/Option.h"
#include <string>

namespace llvm {

class raw_ostream;

namespace opt {

class ArgList;

/// A concrete instance of a particular driver option.
///
/// The Arg class encodes just enough information to be able to
/// derive the argument values efficiently.
class Arg {
private:
  /// The option this argument is an instance of.
  const Option Opt;

  /// The argument this argument was derived from (during tool chain
  /// argument translation), if any.
  const Arg *BaseArg;

  /// How this instance of the option was spelled.
  StringRef Spelling;

  /// The index at which this argument appears in the containing
  /// ArgList.
  unsigned Index;

  /// Was this argument used to effect compilation?
  ///
  /// This is used for generating "argument unused" diagnostics.
  mutable unsigned Claimed : 1;

  /// Does this argument own its values?
  mutable unsigned OwnsValues : 1;

  /// The argument values, as C strings.
  SmallVector<const char *, 2> Values;

  /// If this arg was created through an alias, this is the original alias arg.
  /// For example, *this might be "-finput-charset=utf-8" and Alias might
  /// point to an arg representing "/source-charset:utf-8".
  std::unique_ptr<Arg> Alias;

public:
  Arg(const Option Opt, StringRef Spelling, unsigned Index,
      const Arg *BaseArg = nullptr);
  Arg(const Option Opt, StringRef Spelling, unsigned Index,
      const char *Value0, const Arg *BaseArg = nullptr);
  Arg(const Option Opt, StringRef Spelling, unsigned Index,
      const char *Value0, const char *Value1, const Arg *BaseArg = nullptr);
  Arg(const Arg &) = delete;
  Arg &operator=(const Arg &) = delete;
  ~Arg();

  const Option &getOption() const { return Opt; }

  /// Returns the used prefix and name of the option:
  /// For `--foo=bar`, returns `--foo=`.
  /// This is often the wrong function to call:
  /// * Use `getValue()` to get `bar`.
  /// * Use `getAsString()` to get a string suitable for printing an Arg in
  ///   a diagnostic.
  StringRef getSpelling() const { return Spelling; }

  unsigned getIndex() const { return Index; }

  /// Return the base argument which generated this arg.
  ///
  /// This is either the argument itself or the argument it was
  /// derived from during tool chain specific argument translation.
  const Arg &getBaseArg() const {
    return BaseArg ? *BaseArg : *this;
  }
  void setBaseArg(const Arg *BaseArg) { this->BaseArg = BaseArg; }

  /// Args are converted to their unaliased form.  For args that originally
  /// came from an alias, this returns the alias the arg was produced from.
  const Arg* getAlias() const { return Alias.get(); }
  void setAlias(std::unique_ptr<Arg> Alias) { this->Alias = std::move(Alias); }

  bool getOwnsValues() const { return OwnsValues; }
  void setOwnsValues(bool Value) const { OwnsValues = Value; }

  bool isClaimed() const { return getBaseArg().Claimed; }

  /// Set the Arg claimed bit.
  void claim() const { getBaseArg().Claimed = true; }

  unsigned getNumValues() const { return Values.size(); }

  const char *getValue(unsigned N = 0) const {
    return Values[N];
  }

  SmallVectorImpl<const char *> &getValues() { return Values; }
  const SmallVectorImpl<const char *> &getValues() const { return Values; }

  bool containsValue(StringRef Value) const {
    for (unsigned i = 0, e = getNumValues(); i != e; ++i)
      if (Values[i] == Value)
        return true;
    return false;
  }

  /// Append the argument onto the given array as strings.
  void render(const ArgList &Args, ArgStringList &Output) const;

  /// Append the argument, render as an input, onto the given
  /// array as strings.
  ///
  /// The distinction is that some options only render their values
  /// when rendered as a input (e.g., Xlinker).
  void renderAsInput(const ArgList &Args, ArgStringList &Output) const;

  void print(raw_ostream &O) const;
  void dump() const;

  /// Return a formatted version of the argument and its values, for
  /// diagnostics. Since this is for diagnostics, if this Arg was produced
  /// through an alias, this returns the string representation of the alias
  /// that the user wrote.
  std::string getAsString(const ArgList &Args) const;
};

} // end namespace opt

} // end namespace llvm

#endif // LLVM_OPTION_ARG_H

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
