#pragma once

#include "fwd.h"
#include "node.h"

#include <util/generic/guid.h>
#include <util/generic/maybe.h>
#include <util/generic/ptr.h>
#include <util/generic/type_name.h>
#include <util/generic/vector.h>

#include <initializer_list>
#include <type_traits>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

#define FLUENT_FIELD(type, name) \
    type name##_; \
    TSelf& name(const type& value) \
    { \
        name##_ = value; \
        return static_cast<TSelf&>(*this); \
    }

#define FLUENT_FIELD_OPTION(type, name) \
    TMaybe<type> name##_; \
    TSelf& name(const type& value) \
    { \
        name##_ = value; \
        return static_cast<TSelf&>(*this); \
    }

#define FLUENT_FIELD_DEFAULT(type, name, defaultValue) \
    type name##_ = defaultValue; \
    TSelf& name(const type& value) \
    { \
        name##_ = value; \
        return static_cast<TSelf&>(*this); \
    }

#define FLUENT_VECTOR_FIELD(type, name) \
    yvector<type> name##s_; \
    TSelf& Add##name(const type& value) \
    { \
        name##s_.push_back(value); \
        return static_cast<TSelf&>(*this);\
    }

////////////////////////////////////////////////////////////////////////////////

template <class T>
struct TKeyBase
{
    TKeyBase()
    { }

    TKeyBase(const TKeyBase& rhs)
    {
        Parts_ = rhs.Parts_;
    }

    TKeyBase& operator=(const TKeyBase& rhs)
    {
        Parts_ = rhs.Parts_;
        return *this;
    }

    TKeyBase(TKeyBase&& rhs)
    {
        Parts_ = std::move(rhs.Parts_);
    }

    TKeyBase& operator=(TKeyBase&& rhs)
    {
        Parts_ = std::move(rhs.Parts_);
        return *this;
    }

    template<class U>
    TKeyBase(std::initializer_list<U> il)
    {
        Parts_.assign(il.begin(), il.end());
    }

    template <class U, class... TArgs, std::enable_if_t<std::is_convertible<U, T>::value, int> = 0>
    TKeyBase(U&& arg, TArgs&&... args)
    {
        Add(arg, std::forward<TArgs>(args)...);
    }

    TKeyBase(yvector<T> args)
        : Parts_(std::move(args))
    { }

    bool operator==(const TKeyBase& rhs) const {
        return Parts_ == rhs.Parts_;
    }

    template <class U, class... TArgs>
    void Add(U&& part, TArgs&&... args)
    {
        Parts_.push_back(std::forward<U>(part));
        Add(std::forward<TArgs>(args)...);
    }

    void Add()
    { }

    yvector<T> Parts_;
};

////////////////////////////////////////////////////////////////////////////////

enum EValueType : int
{
    VT_INT64,
    VT_UINT64,
    VT_DOUBLE,
    VT_BOOLEAN,
    VT_STRING,
    VT_ANY
};

enum ESortOrder : int
{
    SO_ASCENDING,
    SO_DESCENDING
};

struct TColumnSchema
{
    using TSelf = TColumnSchema;

    FLUENT_FIELD(Stroka, Name);
    FLUENT_FIELD(EValueType, Type);
    FLUENT_FIELD_OPTION(ESortOrder, SortOrder);
    FLUENT_FIELD_OPTION(Stroka, Lock);
    FLUENT_FIELD_OPTION(Stroka, Expression);
    FLUENT_FIELD_OPTION(Stroka, Aggregate);
    FLUENT_FIELD_OPTION(Stroka, Group);
};

struct TTableSchema
{
    using TSelf = TTableSchema;

    FLUENT_VECTOR_FIELD(TColumnSchema, Column);
    FLUENT_FIELD_DEFAULT(bool, Strict, true);
    FLUENT_FIELD_DEFAULT(bool, UniqueKeys, false);
};

////////////////////////////////////////////////////////////////////////////////

struct TReadLimit
{
    using TSelf = TReadLimit;

    FLUENT_FIELD_OPTION(TKey, Key);
    FLUENT_FIELD_OPTION(i64, RowIndex);
    FLUENT_FIELD_OPTION(i64, Offset);
};

struct TReadRange
{
    using TSelf = TReadRange;

    FLUENT_FIELD(TReadLimit, LowerLimit);
    FLUENT_FIELD(TReadLimit, UpperLimit);
    FLUENT_FIELD(TReadLimit, Exact);

    static TReadRange FromRowIndexes(i64 lowerLimit, i64 upperLimit)
    {
        return TReadRange()
            .LowerLimit(TReadLimit().RowIndex(lowerLimit))
            .UpperLimit(TReadLimit().RowIndex(upperLimit));
    }
};

struct TRichYPath
{
    using TSelf = TRichYPath;

    FLUENT_FIELD(TYPath, Path);

    FLUENT_FIELD_OPTION(bool, Append);
    FLUENT_FIELD(TKeyColumns, SortedBy);

    FLUENT_VECTOR_FIELD(TReadRange, Range);
    FLUENT_FIELD(TKeyColumns, Columns);

    FLUENT_FIELD_OPTION(bool, Teleport);
    FLUENT_FIELD_OPTION(bool, Primary);
    FLUENT_FIELD_OPTION(bool, Foreign);
    FLUENT_FIELD_OPTION(i64, RowCountLimit);

    FLUENT_FIELD_OPTION(Stroka, FileName);
    FLUENT_FIELD_OPTION(bool, Executable);
    FLUENT_FIELD_OPTION(TNode, Format);
    FLUENT_FIELD_OPTION(TTableSchema, Schema);

    // Timestamp of dynamic table.
    // NOTE: it is _not_ unix timestamp
    // (instead it's transaction timestamp, that is more complex structure).
    FLUENT_FIELD_OPTION(i64, Timestamp);

    TRichYPath()
    { }

    TRichYPath(const char* path)
        : Path_(path)
    { }

    TRichYPath(const TYPath& path)
        : Path_(path)
    { }
};

struct TAttributeFilter
{
    using TSelf = TAttributeFilter;

    FLUENT_VECTOR_FIELD(Stroka, Attribute);
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
