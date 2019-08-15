#pragma once

#include "public.h"

#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/LLVMContext.h>
#include <climits>

namespace NYT::NCodegen {

/////////////////////////////////////////////////////////////////////////////

/// TypeBuilder - This provides a uniform API for looking up types
/// known at compile time.    To support cross-compilation, we define a
/// series of tag types in the llvm::types namespace, like i<N>,
/// ieee_float, ppc_fp128, etc.    TypeBuilder<T> allows T to be
/// any of these, a native ctx type (whose size may depend on the host
/// compiler), or a pointer, function, or struct type built out of
/// these.    TypeBuilder<T> removes native ctx types from this set
/// to guarantee that its result is suitable for cross-compilation.
/// We define the primitive types, pointer types, and functions up to
/// 5 arguments here, but to use this
/// struct with your own types,
/// you'll need to specialize it.    For example, say you want to call a
/// function defined externally as:
///
/// \code{.cpp}
///
///     struct MyType {
///         int32 a;
///         int32 *b;
///         void *array[1];    // Intended as a flexible array.
///     };
///     int8 AFunction(struct MyType *value);
///
/// \endcode
///
/// You'll want to use
///     Function::Create(TypeBuilder<types::i<8>(MyType*)>::get(), ...)
/// to declare the function, but when you first try this, your compiler will
/// complain that TypeBuilder<MyType>::get() doesn't exist. To fix this,
/// write:
///
/// \code{.cpp}
///
///     struct TypeBuilder<MyType>
///     {
///         static StructType *get(llvm::LLVMContext& ctx) {
///             // If you cache this result, be sure to cache it separately
///             // for each llvm::LLVMContext.
///             return StructType::get(
///                 TypeBuilder<types::i<32>>::get(ctx),
///                 TypeBuilder<types::i<32>*>::get(ctx),
///                 TypeBuilder<types::i<8>*[]>::get(ctx),
///                 nullptr);
///         }
///
///         // You may find this a convenient place to put some constants
///         // to help with getelementptr.    They don't have any effect on
///         // the operation of TypeBuilder.
///         enum Fields {
///             FIELD_A,
///             FIELD_B,
///             FIELD_ARRAY
///         };
///     }
///
/// \endcode
///
/// TypeBuilder cannot handle recursive types or types you only know at runtime.
/// If you try to give it a recursive type, it will deadlock, infinitely
/// recurse, or do something similarly undesirable.
template <class T>
struct TypeBuilder { };

// Types for use with cross-compilable TypeBuilders.    These correspond
// exactly with an LLVM-native type.
namespace types {
/// i<N> corresponds to the LLVM llvm::IntegerType with N bits.
template <ui32 num_bits>
struct i { };

// The following
// structes represent the LLVM floating types.

struct ieee_float { };

struct ieee_double { };

struct x86_fp80 { };

struct fp128 { };

struct ppc_fp128 { };
// X86 MMX.

struct x86_mmx { };
}    // namespace types

// LLVM doesn't have const or volatile types.
template <class T>
struct TypeBuilder<const T>
    : public TypeBuilder<T>
{ };

template <class T>
struct TypeBuilder<volatile T>
    : public TypeBuilder<T>
{ };

template <class T>
struct TypeBuilder<const volatile T>
    : public TypeBuilder<T>
{ };

// Pointers
template <class T>
struct TypeBuilder<T*>
{
    static llvm::PointerType *get(llvm::LLVMContext& ctx)
    {
        return llvm::PointerType::getUnqual(TypeBuilder<T>::get(ctx));
    }
};

/// There is no support for references
template <class T>
struct TypeBuilder<T&>
{ };

// Arrays
template <class T, size_t N>
struct TypeBuilder<T[N]>
{
    static llvm::ArrayType *get(llvm::LLVMContext& ctx)
    {
        return llvm::ArrayType::get(TypeBuilder<T>::get(ctx), N);
    }
};
/// LLVM uses an array of length 0 to represent an unknown-length array.
template <class T>
struct TypeBuilder<T[]>
{
    static llvm::ArrayType *get(llvm::LLVMContext& ctx)
    {
        return llvm::ArrayType::get(TypeBuilder<T>::get(ctx), 0);
    }
};

// Define the ctx integral types only for TypeBuilder<T>.
//
// ctx integral types do not have a defined size. It would be nice to use the
// stdint.h-defined typedefs that do have defined sizes, but we'd run into the
// following problem:
//
// On an ILP32 machine, stdint.h might define:
//
//     typedef int int32_t;
//     typedef long long int64_t;
//     typedef long size_t;
//
// If we defined TypeBuilder<int32_t> and TypeBuilder<int64_t>, then any use of
// TypeBuilder<size_t> would fail.    We couldn't define TypeBuilder<size_t> in
// addition to the defined-size types because we'd get duplicate definitions on
// platforms where stdint.h instead defines:
//
//     typedef int int32_t;
//     typedef long long int64_t;
//     typedef int size_t;
//
// So we define all the primitive ctx types and nothing else.
#define DEFINE_INTEGRAL_TYPEBUILDER(T) \
template <> \
struct TypeBuilder<T> \
{ \
public: \
    static llvm::IntegerType *get(llvm::LLVMContext& ctx) \
    { \
        return llvm::IntegerType::get(ctx, sizeof(T) * CHAR_BIT); \
    } \
};

DEFINE_INTEGRAL_TYPEBUILDER(char);
DEFINE_INTEGRAL_TYPEBUILDER(signed char);
DEFINE_INTEGRAL_TYPEBUILDER(unsigned char);
DEFINE_INTEGRAL_TYPEBUILDER(short);
DEFINE_INTEGRAL_TYPEBUILDER(unsigned short);
DEFINE_INTEGRAL_TYPEBUILDER(int);
DEFINE_INTEGRAL_TYPEBUILDER(unsigned int);
DEFINE_INTEGRAL_TYPEBUILDER(long);
DEFINE_INTEGRAL_TYPEBUILDER(unsigned long);
#ifdef _MSC_VER
DEFINE_INTEGRAL_TYPEBUILDER(__int64);
DEFINE_INTEGRAL_TYPEBUILDER(unsigned __int64);
#else /* _MSC_VER */
DEFINE_INTEGRAL_TYPEBUILDER(long long);
DEFINE_INTEGRAL_TYPEBUILDER(unsigned long long);
#endif /* _MSC_VER */
#undef DEFINE_INTEGRAL_TYPEBUILDER

template <uint32_t num_bits>

struct TypeBuilder<types::i<num_bits>>
{
    static llvm::IntegerType *get(llvm::LLVMContext& ctx)
    {
        return llvm::IntegerType::get(ctx, num_bits);
    }
};

template <>
struct TypeBuilder<float>
{
    static llvm::Type *get(llvm::LLVMContext& ctx)
    {
        return llvm::Type::getFloatTy(ctx);
    }
};

template <>
struct TypeBuilder<double>
{
    static llvm::Type *get(llvm::LLVMContext& ctx)
    {
        return llvm::Type::getDoubleTy(ctx);
    }
};

template <>
struct TypeBuilder<types::ieee_float>
{
    static llvm::Type *get(llvm::LLVMContext& ctx)
    {
        return llvm::Type::getFloatTy(ctx);
    }
};

template <>
struct TypeBuilder<types::ieee_double>
{
    static llvm::Type *get(llvm::LLVMContext& ctx)
    {
        return llvm::Type::getDoubleTy(ctx);
    }
};

template <>
struct TypeBuilder<types::x86_fp80>
{
    static llvm::Type *get(llvm::LLVMContext& ctx)
    {
        return llvm::Type::getX86_FP80Ty(ctx);
    }
};

template <>
struct TypeBuilder<types::fp128>
{
    static llvm::Type *get(llvm::LLVMContext& ctx)
    {
        return llvm::Type::getFP128Ty(ctx);
    }
};

template <>
struct TypeBuilder<types::ppc_fp128>
{
    static llvm::Type *get(llvm::LLVMContext& ctx)
    {
        return llvm::Type::getPPC_FP128Ty(ctx);
    }
};

template <>
struct TypeBuilder<types::x86_mmx>
{
    static llvm::Type *get(llvm::LLVMContext& ctx)
    {
        return llvm::Type::getX86_MMXTy(ctx);
    }
};

template <>
struct TypeBuilder<void>
{
    static llvm::Type *get(llvm::LLVMContext& ctx)
    {
        return llvm::Type::getVoidTy(ctx);
    }
};

/// void* is disallowed in LLVM types, but it occurs often enough in ctx code that
/// we special case it.
template <>
struct TypeBuilder<void*>
    : public TypeBuilder<types::i<8>*>
{ };

template <>
struct TypeBuilder<const void*>
    : public TypeBuilder<types::i<8>*>
{ };

template <>
struct TypeBuilder<volatile void*>
    : public TypeBuilder<types::i<8>*>
{ };

template <>
struct TypeBuilder<const volatile void*>
    : public TypeBuilder<types::i<8>*>
{ };

template <typename R, typename... As>
struct TypeBuilder<R(As...)>
{
    static llvm::FunctionType *get(llvm::LLVMContext& ctx)
    {
        llvm::Type* params[] = {TypeBuilder<As>::get(ctx)...};
        return llvm::FunctionType::get(TypeBuilder<R>::get(ctx), params, false);
    }
};

template <typename R, typename... As>
struct TypeBuilder<R(As..., ...)>
{
    static llvm::FunctionType *get(llvm::LLVMContext& ctx)
    {
        llvm::Type* params[] = {TypeBuilder<As>::get(ctx)...};
        return llvm::FunctionType::get(TypeBuilder<R>::get(ctx), params, true);
    }
};

/////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NCodegen
