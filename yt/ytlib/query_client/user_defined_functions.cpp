#include "user_defined_functions.h"

#include "cg_fragment_compiler.h"
#include "plan_helpers.h"

#include <new_table_client/row_base.h>
#include <new_table_client/llvm_types.h>

#include <llvm/Object/ObjectFile.h>

#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_ostream.h>

#include <llvm/IRReader/IRReader.h>

#include <llvm/Linker/Linker.h>

using namespace llvm;

namespace NYT {
namespace NQueryClient {

////////////////////////////////////////////////////////////////////////////////

static const char* ExecutionContextStructName = "struct.TExecutionContext";

static const char* UnversionedValueStructName = "struct.TUnversionedValue";

Stroka ToString(llvm::Type* tp)
{
    std::string str;
    llvm::raw_string_ostream stream(str);
    tp->print(stream);
    return Stroka(stream.str());
}

Type* GetOpaqueType(
    TCGContext& builder,
    const char* name)
{
    auto existingType = builder.Module
        ->GetModule()
        ->getTypeByName(name);

    if (existingType) {
        return existingType;
    }

    return StructType::create(
        builder.getContext(),
        name);
}

void PushExecutionContext(
    TCGContext& builder,
    std::vector<Value*>& argumentValues)
{
    auto fullContext = builder.GetExecutionContextPtr();
    auto contextType = GetOpaqueType(builder, ExecutionContextStructName);
    auto contextStruct = builder.CreateBitCast(
        fullContext,
        PointerType::getUnqual(contextType));
    argumentValues.push_back(contextStruct);
}

////////////////////////////////////////////////////////////////////////////////

void ICallingConvention::CheckCallee(
    const Stroka& functionName,
    llvm::Function* callee,
    TCGContext& builder,
    std::vector<Value*> argumentValues,
    TType resultType) const
{
    if (!callee) {
        THROW_ERROR_EXCEPTION(
            "Could not find LLVM bitcode for function %Qv",
            functionName);
    } else if (callee->arg_size() != argumentValues.size()) {
        THROW_ERROR_EXCEPTION(
            "Wrong number of arguments in LLVM bitcode for function %Qv: expected %v, got %v",
            functionName,
            argumentValues.size(),
            callee->arg_size());
    }

    CheckResultType(
        functionName,
        callee->getReturnType(),
        resultType,
        builder);

    auto i = 1;
    auto expected = argumentValues.begin();
    for (
        auto actual = callee->arg_begin();
        expected != argumentValues.end();
        expected++, actual++, i++)
    {
        if (actual->getType() != (*expected)->getType()) {
            THROW_ERROR_EXCEPTION(
                "Wrong type for argument %v in LLVM bitcode for function %Qv: expected %Qv, got %Qv",
                i,
                functionName,
                ToString((*expected)->getType()),
                ToString(actual->getType()));
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

void PushArgument(
    TCGContext& builder,
    std::vector<Value*>& argumentValues,
    TCGValue argumentValue)
{
    argumentValues.push_back(argumentValue.GetData());
    if (IsStringLikeType(argumentValue.GetStaticType())) {
        argumentValues.push_back(argumentValue.GetLength());
    }
}

TCGValue PropagateNullArguments(
    std::vector<TCodegenExpression>& codegenArgs,
    std::vector<Value*>& argumentValues,
    std::function<Value*(std::vector<Value*>)> codegenBody,
    std::function<TCGValue(Value*)> codegenReturn,
    EValueType type,
    const Stroka& name,
    TCGContext& builder,
    Value* row)
{
    if (codegenArgs.empty()) {
        auto llvmResult = codegenBody(argumentValues);
        return codegenReturn(llvmResult);
    } else {
        auto currentArgValue = codegenArgs.back()(builder, row);
        codegenArgs.pop_back();

        PushArgument(builder, argumentValues, currentArgValue);

        return CodegenIf<TCGContext, TCGValue>(
            builder,
            currentArgValue.IsNull(),
            [&] (TCGContext& builder) {
                return TCGValue::CreateNull(builder, type);
            },
            [&] (TCGContext& builder) {
                return PropagateNullArguments(
                    codegenArgs,
                    argumentValues,
                    std::move(codegenBody),
                    std::move(codegenReturn),
                    type,
                    name,
                    builder,
                    row);
            },
            Twine(name.c_str()));
    }
}

TCodegenExpression TSimpleCallingConvention::MakeCodegenFunctionCall(
    std::vector<TCodegenExpression> codegenArgs,
    std::function<Value*(std::vector<Value*>, TCGContext&)> codegenBody,
    EValueType type,
    const Stroka& name) const
{
    return [
        this_ = MakeStrong(this),
        type,
        name,
        MOVE(codegenArgs),
        MOVE(codegenBody)
    ] (TCGContext& builder, Value* row) mutable {
        std::reverse(
            codegenArgs.begin(),
            codegenArgs.end());

        auto llvmArgs = std::vector<Value*>();
        PushExecutionContext(builder, llvmArgs);

        auto callUdf = [&codegenBody, &builder] (std::vector<Value*> argValues) {
            return codegenBody(argValues, builder);
        };

        std::function<TCGValue(Value*)> codegenReturn;
        if (IsStringLikeType(type)) {
            auto resultPointer = builder.CreateAlloca(
                TDataTypeBuilder::get(
                    builder.getContext(),
                    EValueType::String));
            llvmArgs.push_back(resultPointer);

            auto resultLength = builder.CreateAlloca(
                TTypeBuilder::TLength::get(builder.getContext()));
            llvmArgs.push_back(resultLength);

            codegenReturn = [
                &,
                resultLength,
                resultPointer
            ] (Value* llvmResult) {
                return TCGValue::CreateFromValue(
                    builder,
                    builder.getFalse(),
                    builder.CreateLoad(resultLength),
                    builder.CreateLoad(resultPointer),
                    type,
                    Twine(name.c_str()));
            };
        } else {
            codegenReturn = [&] (Value* llvmResult) {
                    return TCGValue::CreateFromValue(
                        builder,
                        builder.getFalse(),
                        nullptr,
                        llvmResult,
                        type);
            };
        }

        return PropagateNullArguments(
            codegenArgs,
            llvmArgs,
            callUdf,
            codegenReturn,
            type,
            name,
            builder,
            row);
    };
}

void TSimpleCallingConvention::CheckResultType(
    const Stroka& functionName,
    Type* llvmType,
    TType resultType,
    TCGContext& builder) const
{
    auto concreteResultType = resultType.As<EValueType>();
    auto expectedResultType = TDataTypeBuilder::get(
        builder.getContext(),
        concreteResultType);
    if (IsStringLikeType(concreteResultType) &&
        llvmType != builder.getVoidTy())
    {
        THROW_ERROR_EXCEPTION(
            "Wrong result type in LLVM bitcode for function %Qv: expected void, got %Qv",
            functionName,
            ToString(llvmType));
    } else if (!IsStringLikeType(concreteResultType) &&
        llvmType != expectedResultType)
    {
        THROW_ERROR_EXCEPTION(
            "Wrong result type in LLVM bitcode: expected %Qv, got %Qv",
            ToString(expectedResultType),
            ToString(llvmType));
    }
}

////////////////////////////////////////////////////////////////////////////////

TUnversionedValueCallingConvention::TUnversionedValueCallingConvention(
    int repeatedArgIndex)
    : RepeatedArgIndex_(repeatedArgIndex)
{ }

TCodegenExpression TUnversionedValueCallingConvention::MakeCodegenFunctionCall(
    std::vector<TCodegenExpression> codegenArgs,
    std::function<Value*(std::vector<Value*>, TCGContext&)> codegenBody,
    EValueType type,
    const Stroka& name) const
{
    return [=] (TCGContext& builder, Value* row) {
        auto unversionedValueType =
            llvm::TypeBuilder<TValue, false>::get(builder.getContext());
        auto unversionedValueOpaqueType = GetOpaqueType(
            builder,
            UnversionedValueStructName);

        auto argumentValues = std::vector<Value*>();

        PushExecutionContext(builder, argumentValues);

        auto resultPtr = builder.CreateAlloca(unversionedValueType);
        auto castedResultPtr = builder.CreateBitCast(
            resultPtr,
            PointerType::getUnqual(unversionedValueOpaqueType));
        argumentValues.push_back(castedResultPtr);

        int argIndex = 0;
        auto arg = codegenArgs.begin();
        for (;
            arg != codegenArgs.end() && argIndex != RepeatedArgIndex_;
            arg++, argIndex++) 
        {
            auto valuePtr = builder.CreateAlloca(unversionedValueType);
            auto cgValue = (*arg)(builder, row);
            cgValue.StoreToValue(builder, valuePtr, 0);

            auto castedValuePtr = builder.CreateBitCast(
                valuePtr,
                PointerType::getUnqual(unversionedValueOpaqueType));
            argumentValues.push_back(castedValuePtr);
        }

        if (argIndex == RepeatedArgIndex_) {
            auto varargSize = builder.getInt32(
                codegenArgs.size() - RepeatedArgIndex_);

            auto varargPtr = builder.CreateAlloca(
                unversionedValueType,
                varargSize);
            auto castedVarargPtr = builder.CreateBitCast(
                varargPtr,
                PointerType::getUnqual(unversionedValueOpaqueType));

            argumentValues.push_back(castedVarargPtr);
            argumentValues.push_back(varargSize);

            for (int varargIndex = 0; arg != codegenArgs.end(); arg++, varargIndex++) {
                auto valuePtr = builder.CreateConstGEP1_32(
                    varargPtr,
                    varargIndex);
                
                auto cgValue = (*arg)(builder, row);
                cgValue.StoreToValue(builder, valuePtr, 0);
            }
        }

        codegenBody(argumentValues, builder);

        return TCGValue::CreateFromLlvmValue(
            builder,
            resultPtr,
            type);
    };
}

void TUnversionedValueCallingConvention::CheckResultType(
    const Stroka& functionName,
    Type* llvmType,
    TType resultType,
    TCGContext& builder) const
{
    if (llvmType != builder.getVoidTy()) {
        THROW_ERROR_EXCEPTION(
            "Wrong result type in LLVM bitcode for function %Qv: expected void, got %Qv",
            functionName,
            ToString(llvmType));
    }
}

////////////////////////////////////////////////////////////////////////////////

TUserDefinedFunction::TUserDefinedFunction(
    const Stroka& functionName,
    const Stroka& symbolName,
    std::unordered_map<TTypeArgument, TUnionType> typeArgumentConstraints,
    std::vector<TType> argumentTypes,
    TType repeatedArgType,
    TType resultType,
    TSharedRef implementationFile,
    ICallingConventionPtr callingConvention)
    : TTypedFunction(
        functionName,
        typeArgumentConstraints,
        std::vector<TType>(argumentTypes.begin(), argumentTypes.end()),
        repeatedArgType,
        resultType)
    , FunctionName_(functionName)
    , SymbolName_(symbolName)
    , ImplementationFile_(implementationFile)
    , ResultType_(resultType)
    , ArgumentTypes_(argumentTypes)
    , CallingConvention_(callingConvention)
{ }

ICallingConventionPtr GetCallingConvention(
    ECallingConvention callingConvention,
    int repeatedArgIndex,
    TType repeatedArgType)
{
    switch (callingConvention) {
        case ECallingConvention::Simple:
            return New<TSimpleCallingConvention>();
        case ECallingConvention::UnversionedValue:
            if (repeatedArgType.TryAs<EValueType>()
                && repeatedArgType.As<EValueType>() == EValueType::Null)
            {
                return New<TUnversionedValueCallingConvention>(-1);
            } else {
                return New<TUnversionedValueCallingConvention>(repeatedArgIndex);
            }
        default:
            YUNREACHABLE();
    }
}

TUserDefinedFunction::TUserDefinedFunction(
    const Stroka& functionName,
    std::vector<TType> argumentTypes,
    TType resultType,
    TSharedRef implementationFile,
    ECallingConvention callingConvention)
    : TUserDefinedFunction(
        functionName,
        functionName,
        std::unordered_map<TTypeArgument, TUnionType>(),
        argumentTypes,
        EValueType::Null,
        resultType,
        implementationFile,
        GetCallingConvention(callingConvention, argumentTypes.size(), EValueType::Null))
{ }

TUserDefinedFunction::TUserDefinedFunction(
    const Stroka& functionName,
    std::unordered_map<TTypeArgument, TUnionType> typeArgumentConstraints,
    std::vector<TType> argumentTypes,
    TType repeatedArgType,
    TType resultType,
    TSharedRef implementationFile)
    : TUserDefinedFunction(
        functionName,
        functionName,
        typeArgumentConstraints,
        argumentTypes,
        repeatedArgType,
        resultType,
        implementationFile,
        GetCallingConvention(ECallingConvention::UnversionedValue, argumentTypes.size(), repeatedArgType))
{ }

TUserDefinedFunction::TUserDefinedFunction(
    const Stroka& functionName,
    const Stroka& symbolName,
    std::unordered_map<TTypeArgument, TUnionType> typeArgumentConstraints,
    std::vector<TType> argumentTypes,
    TType repeatedArgType,
    TType resultType,
    TSharedRef implementationFile)
    : TUserDefinedFunction(
        functionName,
        symbolName,
        typeArgumentConstraints,
        argumentTypes,
        repeatedArgType,
        resultType,
        implementationFile,
        GetCallingConvention(ECallingConvention::UnversionedValue, argumentTypes.size(), repeatedArgType))
{ }

Function* GetSharedObjectFunction(
    TCGContext& builder,
    const Stroka& functionName,
    const Stroka& symbolName,
    TSharedRef implementationFile,
    std::vector<Type*> argumentTypes,
    EValueType resultType)
{

    auto buffer = llvm::MemoryBufferRef(
        llvm::StringRef(implementationFile.Begin(), implementationFile.Size()),
        llvm::StringRef());
    auto objectFileOrError = llvm::object::ObjectFile::createObjectFile(buffer);

    if (!objectFileOrError) {
        return nullptr;
    }

    builder.Module->AddObjectFile(std::move(*objectFileOrError), functionName);
    auto functionType = FunctionType::get(
        TDataTypeBuilder::get(builder.getContext(), resultType),
        ArrayRef<Type*>(argumentTypes),
        false);
    return Function::Create(
        functionType,
        Function::ExternalLinkage,
        symbolName.c_str(),
        builder.Module->GetModule());
}

Function* GetLlvmBitcodeFunction(
    TCGContext& builder,
    const Stroka& functionName,
    const Stroka& symbolName,
    std::vector<Value*> argumentValues,
    TSharedRef implementationFile,
    TType resultType,
    ICallingConventionPtr callingConvention)
{
    auto module = builder.Module->GetModule();
    auto callee = module->getFunction(StringRef(symbolName));
    if (!callee) {
        auto diag = SMDiagnostic();
        auto buffer = MemoryBufferRef(
            StringRef(implementationFile.Begin(), implementationFile.Size()),
            StringRef("impl"));
        auto implModule = parseIR(buffer, diag, builder.getContext());

        if (!implModule) {
            return nullptr;
        }

        // Link two modules together, with the first module modified to be the
        // composite of the two input modules.
        auto linkError = Linker::LinkModules(module, implModule.get());

        if (linkError) {
            THROW_ERROR_EXCEPTION(
                "Error linking LLVM bitcode for function %Qv",
                functionName);
        }

        callee = module->getFunction(StringRef(symbolName));
        callingConvention->CheckCallee(
            functionName,
            callee,
            builder,
            argumentValues,
            resultType);
    }
    callee->addFnAttr(Attribute::AttrKind::AlwaysInline);
    return callee;
}

Function* GetLlvmFunction(
    TCGContext& builder,
    const Stroka& functionName,
    const Stroka& symbolName,
    std::vector<Value*> argumentValues,
    EValueType resultType,
    TSharedRef implementationFile,
    ICallingConventionPtr callingConvention)
{
    auto callee = GetLlvmBitcodeFunction(
        builder,
        functionName,
        symbolName,
        argumentValues,
        implementationFile,
        resultType,
        callingConvention);

    if (callee) {
        return callee;
    }

    std::vector<Type*> argumentLlvmTypes;
    for (auto value: argumentValues) {
        argumentLlvmTypes.push_back(value->getType());
    }

    callee = GetSharedObjectFunction(
        builder,
        functionName,
        symbolName,
        implementationFile,
        argumentLlvmTypes,
        resultType);

    if (callee) {
        return callee;
    }

    THROW_ERROR_EXCEPTION(
        "Error loading implementation file for function %Qv",
        functionName);
}

TCodegenExpression TUserDefinedFunction::MakeCodegenExpr(
    std::vector<TCodegenExpression> codegenArgs,
    EValueType type,
    const Stroka& name) const
{
    auto codegenBody = [
        this_ = MakeStrong(this),
        type
    ] (std::vector<Value*> argumentValues, TCGContext& builder) {
        auto callee = GetLlvmFunction(
            builder,
            this_->FunctionName_,
            this_->SymbolName_,
            argumentValues,
            type,
            this_->ImplementationFile_,
            this_->CallingConvention_);
        auto result = builder.CreateCall(callee, argumentValues);
        return result;
    };

    return CallingConvention_->MakeCodegenFunctionCall(
        codegenArgs,
        codegenBody,
        type,
        name);
}

////////////////////////////////////////////////////////////////////////////////

TUserDefinedAggregateFunction::TUserDefinedAggregateFunction(
    const Stroka& aggregateName,
    std::unordered_map<TTypeArgument, TUnionType> typeArgumentConstraints,
    TType argumentType,
    TType resultType,
    TType stateType,
    TSharedRef implementationFile,
    ICallingConventionPtr callingConvention)
    : AggregateName_(aggregateName)
    , TypeArgumentConstraints_(typeArgumentConstraints)
    , ArgumentType_(argumentType)
    , ResultType_(resultType)
    , StateType_(stateType)
    , ImplementationFile_(implementationFile)
    , CallingConvention_(callingConvention)
{ }

TUserDefinedAggregateFunction::TUserDefinedAggregateFunction(
    const Stroka& aggregateName,
    std::unordered_map<TTypeArgument, TUnionType> typeArgumentConstraints,
    TType argumentType,
    TType resultType,
    TType stateType,
    TSharedRef implementationFile,
    ECallingConvention callingConvention)
    : TUserDefinedAggregateFunction(
        aggregateName,
        typeArgumentConstraints,
        argumentType,
        resultType,
        stateType,
        implementationFile,
        GetCallingConvention(callingConvention, 1, EValueType::Null))
{ }

Stroka TUserDefinedAggregateFunction::GetName() const
{
    return AggregateName_;
}

const TCodegenAggregate TUserDefinedAggregateFunction::MakeCodegenAggregate(
    EValueType type,
    const Stroka& name) const
{
    auto resultType = InferResultType(type, "");
    auto makeCodegenBody = [
        this_ = MakeStrong(this),
        resultType
    ] (const Stroka& functionName) {
        return [
            this_,
            resultType,
            functionName
        ] (std::vector<Value*> argumentValues, TCGContext& builder) {
            auto callee = GetLlvmFunction(
                builder,
                functionName,
                functionName,
                argumentValues,
                resultType,
                this_->ImplementationFile_,
                this_->CallingConvention_);

            return builder.CreateCall(callee, argumentValues);
        };
    };

    TCodegenAggregate codegenAggregate;
    codegenAggregate.Initialize = [
        this_ = MakeStrong(this),
        type,
        name,
        makeCodegenBody
    ] (TCGContext& builder, Value* row) {
        auto stateType = this_->GetStateType(type);

        return this_->CallingConvention_->MakeCodegenFunctionCall(
            std::vector<TCodegenExpression>(),
            makeCodegenBody(this_->AggregateName_ + "_init"),
            stateType,
            name + "_init")(builder, row);
    };

    codegenAggregate.Update = [
        this_ = MakeStrong(this),
        type,
        name,
        makeCodegenBody
    ] (TCGContext& builder, Value* aggState, Value* newValue) {
        auto stateType = this_->GetStateType(type);
        auto codegenArgs = std::vector<TCodegenExpression>();
        codegenArgs.push_back([=] (TCGContext& builder, Value* row) {
            return TCGValue::CreateFromLlvmValue(
                builder,
                aggState,
                stateType);
        });
        codegenArgs.push_back([=] (TCGContext& builder, Value* row) {
            return TCGValue::CreateFromLlvmValue(
                builder,
                newValue,
                type);
        });

        return this_->CallingConvention_->MakeCodegenFunctionCall(
            codegenArgs,
            makeCodegenBody(this_->AggregateName_ + "_update"),
            stateType,
            name + "_update")(builder, aggState);
    };

    codegenAggregate.Merge = [
        this_ = MakeStrong(this),
        type,
        name,
        makeCodegenBody
    ] (TCGContext& builder, Value* dstAggState, Value* aggState) {
        auto stateType = this_->GetStateType(type);
        auto codegenArgs = std::vector<TCodegenExpression>();
        codegenArgs.push_back([=] (TCGContext& builder, Value* row) {
            return TCGValue::CreateFromLlvmValue(
                builder,
                dstAggState,
                stateType);
            });
        codegenArgs.push_back([=] (TCGContext& builder, Value* row) {
            return TCGValue::CreateFromLlvmValue(
                builder,
                aggState,
                stateType);
        });

        return this_->CallingConvention_->MakeCodegenFunctionCall(
            codegenArgs,
            makeCodegenBody(this_->AggregateName_ + "_merge"),
            stateType,
            name + "_merge")(builder, aggState);
    };

    codegenAggregate.Finalize = [
        this_ = MakeStrong(this),
        type,
        name,
        makeCodegenBody
    ] (TCGContext& builder, Value* aggState) {
        auto stateType = this_->GetStateType(type);
        auto codegenArgs = std::vector<TCodegenExpression>();
        codegenArgs.push_back([=] (TCGContext& builder, Value* row) {
            return TCGValue::CreateFromLlvmValue(
                builder,
                aggState,
                stateType);
        });

        return this_->CallingConvention_->MakeCodegenFunctionCall(
            codegenArgs,
            makeCodegenBody(this_->AggregateName_ + "_finalize"),
            type,
            name + "_finalize")(builder, aggState);
    };

    return codegenAggregate;
}

EValueType TUserDefinedAggregateFunction::GetStateType(
    EValueType type) const
{
    return TypingFunction(
        TypeArgumentConstraints_,
        std::vector<TType>{ArgumentType_},
        EValueType::Null,
        StateType_,
        AggregateName_,
        std::vector<EValueType>{type},
        TStringBuf());
}

EValueType TUserDefinedAggregateFunction::InferResultType(
    EValueType argumentType,
    const TStringBuf& source) const
{
    return TypingFunction(
        TypeArgumentConstraints_,
        std::vector<TType>{ArgumentType_},
        EValueType::Null,
        ResultType_,
        AggregateName_,
        std::vector<EValueType>{argumentType},
        source);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NQueryClient
} // namespace NYT
