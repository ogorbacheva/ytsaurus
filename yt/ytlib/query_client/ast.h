#pragma once

#include "public.h"

#include "plan_fragment_common.h"

#include <core/misc/variant.h>

namespace NYT {
namespace NQueryClient {
namespace NAst {

////////////////////////////////////////////////////////////////////////////////

typedef TVariant<i64, ui64, double, bool, Stroka> TLiteralValue;
typedef std::vector<TLiteralValue> TLiteralValueList;
typedef std::vector<std::vector<TLiteralValue>> TLiteralValueTupleList;
<<<<<<< HEAD

TStringBuf GetSource(TSourceLocation sourceLocation, const TStringBuf& source);
=======
>>>>>>> prestable/0.17.3

struct TExpression
    : public TIntrinsicRefCounted
{
    explicit TExpression(const TSourceLocation& sourceLocation)
        : SourceLocation(sourceLocation)
    { }

    template <class TDerived>
    const TDerived* As() const
    {
        return dynamic_cast<const TDerived*>(this);
    }

    template <class TDerived>
    TDerived* As()
    {
        return dynamic_cast<TDerived*>(this);
    }

    TStringBuf GetSource(const TStringBuf& source) const;

    TSourceLocation SourceLocation;
};

DECLARE_REFCOUNTED_STRUCT(TExpression)
DEFINE_REFCOUNTED_TYPE(TExpression)

typedef std::vector<TExpressionPtr> TExpressionList;
typedef TNullable<TExpressionList> TNullableExpressionList;

template <class T, class... TArgs>
TExpressionList MakeExpr(TArgs&&... args)
{
    return TExpressionList(1, New<T>(args...));
}

struct TLiteralExpression
    : public TExpression
{
    TLiteralExpression(
        const TSourceLocation& sourceLocation,
        TLiteralValue value)
        : TExpression(sourceLocation)
        , Value(std::move(value))
    { }

    TLiteralValue Value;
};

struct TReferenceExpression
    : public TExpression
{
    TReferenceExpression(
        const TSourceLocation& sourceLocation,
        TStringBuf columnName,
        TStringBuf tableName = TStringBuf())
        : TExpression(sourceLocation)
        , ColumnName(columnName)
        , TableName(tableName)
    { }

    Stroka ColumnName;
<<<<<<< HEAD
    Stroka TableName;
};

DECLARE_REFCOUNTED_STRUCT(TReferenceExpression)
DEFINE_REFCOUNTED_TYPE(TReferenceExpression)
=======
};

struct TCommaExpression
    : public TExpression
{
    TCommaExpression(
        const TSourceLocation& sourceLocation,
        TExpressionPtr lhs,
        TExpressionPtr rhs)
        : TExpression(sourceLocation)
        , Lhs(std::move(lhs))
        , Rhs(std::move(rhs))
    { }

    TExpressionPtr Lhs;
    TExpressionPtr Rhs;
};
>>>>>>> prestable/0.17.3

struct TFunctionExpression
    : public TExpression
{
    TFunctionExpression(
        const TSourceLocation& sourceLocation,
        const TStringBuf& functionName,
        TExpressionList arguments)
        : TExpression(sourceLocation)
        , FunctionName(functionName)
        , Arguments(std::move(arguments))
    { }

    Stroka FunctionName;
<<<<<<< HEAD
    TExpressionList Arguments;
=======
    TExpressionPtr Arguments;
>>>>>>> prestable/0.17.3
};

struct TUnaryOpExpression
    : public TExpression
{
    TUnaryOpExpression(
        const TSourceLocation& sourceLocation,
        EUnaryOp opcode,
        TExpressionList operand)
        : TExpression(sourceLocation)
        , Opcode(opcode)
        , Operand(std::move(operand))
    { }

    EUnaryOp Opcode;
<<<<<<< HEAD
    TExpressionList Operand;
=======
    TExpressionPtr Operand;
>>>>>>> prestable/0.17.3
};

struct TBinaryOpExpression
    : public TExpression
{
    TBinaryOpExpression(
        const TSourceLocation& sourceLocation,
        EBinaryOp opcode,
        TExpressionList lhs,
        TExpressionList rhs)
        : TExpression(sourceLocation)
        , Opcode(opcode)
        , Lhs(std::move(lhs))
        , Rhs(std::move(rhs))
    { }

    EBinaryOp Opcode;
<<<<<<< HEAD
    TExpressionList Lhs;
    TExpressionList Rhs;
=======
    TExpressionPtr Lhs;
    TExpressionPtr Rhs;
>>>>>>> prestable/0.17.3
};

struct TInExpression
    : public TExpression
{
    TInExpression(
        const TSourceLocation& sourceLocation,
<<<<<<< HEAD
        TExpressionList expression,
=======
        TExpressionPtr expression,
>>>>>>> prestable/0.17.3
        const TLiteralValueTupleList& values)
        : TExpression(sourceLocation)
        , Expr(std::move(expression))
        , Values(values)
    { }

<<<<<<< HEAD
    TExpressionList Expr;
=======
    TExpressionPtr Expr;
>>>>>>> prestable/0.17.3
    TLiteralValueTupleList Values;
};

Stroka FormatColumn(const TStringBuf& name, const TStringBuf& tableName = TStringBuf());
Stroka InferName(const TExpressionList& exprs, bool omitValues = false);
Stroka InferName(const TExpression* expr, bool omitValues = false);

////////////////////////////////////////////////////////////////////////////////

typedef std::vector<TReferenceExpressionPtr> TIdentifierList;
typedef TNullable<TIdentifierList> TNullableIdentifierList;

typedef std::vector<std::pair<TExpressionList, bool>> TOrderExpressionList;

struct TTableDescriptor
{
    TTableDescriptor()
    { }

    TTableDescriptor(
        const Stroka& path,
        const Stroka& alias)
        : Path(path)
        , Alias(alias)
    { }

    Stroka Path;
    Stroka Alias;
};

struct TQuery
{
    TTableDescriptor Table;

    struct TJoin
    {
        TJoin(
            bool isLeft,
            const TTableDescriptor& table,
            const TIdentifierList& fields)
            : IsLeft(isLeft)
            , Table(table)
            , Fields(fields)
        { }

        TJoin(
            bool isLeft,
            const TTableDescriptor& table,
            const TExpressionList& left,
            const TExpressionList& right)
            : IsLeft(isLeft)
            , Table(table)
            , Left(left)
            , Right(right)
        { }

        bool IsLeft;
        TTableDescriptor Table;
        TIdentifierList Fields;

        TExpressionList Left;
        TExpressionList Right;
    };

    std::vector<TJoin> Joins;

    TNullableExpressionList SelectExprs;
    TNullableExpressionList WherePredicate;
    TNullableExpressionList GroupExprs;
    TNullableExpressionList HavingPredicate;

    TOrderExpressionList OrderExpressions;

    i64 Limit = 0;
};

typedef yhash_map<Stroka, TExpressionPtr> TAliasMap;

typedef std::pair<TVariant<TQuery, TExpressionPtr>, TAliasMap> TAstHead;

////////////////////////////////////////////////////////////////////////////////

} // namespace NAst
} // namespace NQueryClient
} // namespace NYT
