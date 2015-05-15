#pragma once

#include "functions.h"
#include "builtin_functions.h"

namespace NYT {
namespace NQueryClient {

////////////////////////////////////////////////////////////////////////////////

DEFINE_ENUM(ECallingConvention,
    (Simple)
    (UnversionedValue)
);

struct ICallingConvention
    : public TRefCounted
{
    virtual TCodegenExpression MakeCodegenFunctionCall(
        std::vector<TCodegenExpression> codegenArgs,
        std::function<Value*(std::vector<Value*>, TCGContext&)> codegenBody,
        EValueType type,
        const Stroka& name) const = 0;

    virtual void CheckResultType(
        const Stroka& functionName,
        Type* llvmType,
        TType resultType,
        TCGContext& builder) const = 0;
};

DEFINE_REFCOUNTED_TYPE(ICallingConvention);

class TUnversionedValueCallingConvention
    : public ICallingConvention
{
public:
    TUnversionedValueCallingConvention(int repeatedArgIndex);

    virtual TCodegenExpression MakeCodegenFunctionCall(
        std::vector<TCodegenExpression> codegenArgs,
        std::function<Value*(std::vector<Value*>, TCGContext&)> codegenBody,
        EValueType type,
        const Stroka& name) const override;

    void CheckResultType(
        const Stroka& functionName,
        Type* llvmType,
        TType resultType,
        TCGContext& builder) const override;

private:
    int RepeatedArgIndex_;
};

class TSimpleCallingConvention
    : public ICallingConvention
{
public:
    virtual TCodegenExpression MakeCodegenFunctionCall(
        std::vector<TCodegenExpression> codegenArgs,
        std::function<Value*(std::vector<Value*>, TCGContext&)> codegenBody,
        EValueType type,
        const Stroka& name) const override;

    void CheckResultType(
        const Stroka& functionName,
        Type* llvmType,
        TType resultType,
        TCGContext& builder) const override;
};

////////////////////////////////////////////////////////////////////////////////

class TUserDefinedFunction
    : public TTypedFunction
    , public TUniversalRangeFunction
{
public:
    TUserDefinedFunction(
        const Stroka& functionName,
        std::vector<TType> argumentTypes,
        TType resultType,
        TSharedRef implementationFile,
        ECallingConvention callingConvention);

    TUserDefinedFunction(
        const Stroka& functionName,
        std::vector<TType> argumentTypes,
        TType repeatedArgType,
        TType resultType,
        TSharedRef implementationFile);

    virtual TCodegenExpression MakeCodegenExpr(
        std::vector<TCodegenExpression> codegenArgs,
        EValueType type,
        const Stroka& name) const override;

private:
    Stroka FunctionName_;
    TSharedRef ImplementationFile_;
    TType ResultType_;
    std::vector<TType> ArgumentTypes_;
    ICallingConventionPtr CallingConvention_;

    TUserDefinedFunction(
        const Stroka& functionName,
        std::vector<TType> argumentTypes,
        TType repeatedArgType,
        TType resultType,
        TSharedRef implementationFile,
        ICallingConventionPtr callingConvention);

    void CheckCallee(
        Function* callee,
        TCGContext& builder,
        std::vector<Value*> argumentValues) const;
};

////////////////////////////////////////////////////////////////////////////////

class TUserDefinedAggregateFunction
    : public IAggregateFunctionDescriptor
{
public:
    TUserDefinedAggregateFunction(
        const Stroka& aggregateName,
        TType argumentType,
        TType resultType,
        EValueType stateType,
        TSharedRef implementationFile,
        ECallingConvention callingConvention);

    virtual Stroka GetName() const override;

    virtual const TCodegenAggregate MakeCodegenAggregate(
        EValueType type,
        const Stroka& name) const override;

    virtual EValueType GetStateType(
        EValueType type) const override;

    virtual EValueType InferResultType(
        EValueType argumentType,
        const TStringBuf& source) const override;

private:
    Stroka AggregateName_;
    TType ArgumentType_;
    TType ResultType_;
    EValueType StateType_;
    TSharedRef ImplementationFile_;
    ICallingConventionPtr CallingConvention_;

    TUserDefinedAggregateFunction(
        const Stroka& aggregateName,
        TType argumentType,
        TType resultType,
        EValueType stateType,
        TSharedRef implementationFile,
        ICallingConventionPtr callingConvention);
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NQueryClient
} // namespace NYT
