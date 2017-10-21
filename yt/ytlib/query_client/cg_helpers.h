#pragma once

#include "cg_ir_builder.h"
#include "cg_types.h"

#include <yt/core/codegen/module.h>
#include <yt/core/codegen/llvm_migrate_helpers.h>

namespace NYT {
namespace NQueryClient {

// Import extensively used LLVM types.
using llvm::BasicBlock;
using llvm::Constant;
using llvm::ConstantFP;
using llvm::ConstantInt;
using llvm::ConstantPointerNull;
using llvm::Function;
using llvm::FunctionType;
using llvm::Instruction;
using llvm::PHINode;
using llvm::PointerType;
using llvm::Twine;
using llvm::Type;
using llvm::TypeBuilder;
using llvm::Value;
using llvm::StringRef;

using NCodegen::TCGModulePtr;

////////////////////////////////////////////////////////////////////////////////

StringRef ToStringRef(const TString& stroka);
StringRef ToStringRef(const TSharedRef& sharedRef);

////////////////////////////////////////////////////////////////////////////////

class TCGIRBuilderPtr
{
protected:
    TCGIRBuilder* const Builder_;

public:
    explicit TCGIRBuilderPtr(TCGIRBuilder* builder)
        : Builder_(builder)
    { }

    TCGIRBuilder* operator->() const noexcept
    {
        return Builder_;
    }

    TCGIRBuilder* GetBuilder() const noexcept
    {
        return Builder_;
    }

};

////////////////////////////////////////////////////////////////////////////////

Value* CodegenValuesPtrFromRow(const TCGIRBuilderPtr& builder, Value* row);

typedef TypeBuilder<TValue, false> TTypeBuilder;
typedef TypeBuilder<TValueData, false> TDataTypeBuilder;

class TCGValue
{
private:
    Value* IsNull_;
    Value* Length_;
    Value* Data_;
    EValueType StaticType_;
    std::string Name_;

    TCGValue(Value* isNull, Value* length, Value* data, EValueType staticType, Twine name)
        : IsNull_(isNull)
        , Length_(length)
        , Data_(data)
        , StaticType_(staticType)
        , Name_(name.str())
    {
        YCHECK(
            StaticType_ == EValueType::Int64 ||
            StaticType_ == EValueType::Uint64 ||
            StaticType_ == EValueType::Double ||
            StaticType_ == EValueType::Boolean ||
            StaticType_ == EValueType::String ||
            StaticType_ == EValueType::Any);
    }

public:
    TCGValue(const TCGValue& other) = default;

    TCGValue(TCGValue&& other)
        : IsNull_(other.IsNull_)
        , Length_(other.Length_)
        , Data_(other.Data_)
        , StaticType_(other.StaticType_)
        , Name_(std::move(other.Name_))
    {
        other.Reset();
    }

    TCGValue& operator=(TCGValue&& other)
    {
        IsNull_ = other.IsNull_;
        Length_ = other.Length_;
        Data_ = other.Data_;
        StaticType_ = other.StaticType_;

        other.Reset();

        return *this;
    }

    TCGValue&& Steal()
    {
        return std::move(*this);
    }

    void Reset()
    {
        IsNull_ = nullptr;
        Length_ = nullptr;
        Data_ = nullptr;
        StaticType_ = EValueType::TheBottom;
    }

    EValueType GetStaticType() const
    {
        return StaticType_;
    }

    static TCGValue CreateFromValue(
        TCGIRBuilderPtr& builder,
        Value* isNull,
        Value* length,
        Value* data,
        EValueType staticType,
        Twine name = Twine())
    {
        YCHECK(isNull->getType() == builder->getInt1Ty());
        if (IsStringLikeType(staticType)) {
            YCHECK(length->getType() == TTypeBuilder::TLength::get(builder->getContext()));
        }
        YCHECK(data->getType() == TDataTypeBuilder::get(builder->getContext(), staticType));
        return TCGValue(isNull, length, data, staticType, name);
    }

    static TCGValue CreateFromRowValues(
        TCGIRBuilderPtr& builder,
        Value* rowValues,
        int index,
        EValueType staticType,
        Twine name = Twine())
    {
        auto valuePtr = builder->CreateConstInBoundsGEP1_32(
            nullptr,
            rowValues,
            index,
            name + ".valuePtr");

        return CreateFromLlvmValue(
            builder,
            valuePtr,
            true,
            staticType,
            name);
    }

    static TCGValue CreateFromLlvmValue(
        TCGIRBuilderPtr& builder,
        Value* valuePtr,
        bool nullbale,
        EValueType staticType,
        Twine name = Twine())
    {
        Value* isNull = builder->getFalse();
        if (nullbale) {
            Value* type = builder->CreateLoad(
                builder->CreateConstInBoundsGEP2_32(
                    nullptr, valuePtr, 0, TTypeBuilder::Type, name + ".type"));

            isNull = builder->CreateICmpEQ(
                type,
                ConstantInt::get(type->getType(), static_cast<int>(EValueType::Null)),
                name + ".isNull");
        }


        Value* length = nullptr;
        if (IsStringLikeType(staticType)) {
            length = builder->CreateLoad(
                builder->CreateConstInBoundsGEP2_32(
                    nullptr, valuePtr, 0, TTypeBuilder::Length, name + ".length"));
        }

        Value* data = builder->CreateLoad(
            builder->CreateConstInBoundsGEP2_32(
                nullptr, valuePtr, 0, TTypeBuilder::Data, name + ".data"));

        Value* castedData = nullptr;
        Type* targetType = TDataTypeBuilder::get(builder->getContext(), staticType);

        if (targetType->isPointerTy()) {
            castedData = builder->CreateIntToPtr(data,
                targetType,
                name + ".data");
        } else if (targetType->isFloatingPointTy()) {
            castedData = builder->CreateBitCast(data,
                targetType,
                name + ".data");
        } else {
            castedData = builder->CreateIntCast(data,
                targetType,
                false,
                name + ".data");
        }

        return CreateFromValue(builder, isNull, length, castedData, staticType, name);
    }

    static TCGValue CreateFromLlvmValue(
        TCGIRBuilderPtr& builder,
        Value* valuePtr,
        EValueType staticType,
        Twine name = Twine())
    {
        return CreateFromLlvmValue(builder, valuePtr, true, staticType, name);
    }

    static TCGValue CreateNull(
        TCGIRBuilderPtr& builder,
        EValueType staticType,
        Twine name = Twine())
    {
        Value* length = nullptr;
        if (IsStringLikeType(staticType)) {
            length = llvm::UndefValue::get(TTypeBuilder::TLength::get(builder->getContext()));
        }

        return CreateFromValue(
            builder,
            builder->getTrue(),
            length,
            llvm::UndefValue::get(TDataTypeBuilder::get(builder->getContext(), staticType)),
            staticType,
            name);
    }

    void StoreToValues(TCGIRBuilderPtr& builder, Value* values, int index)
    {
        auto name = values->getName();

        auto valuePtr = builder->CreateConstInBoundsGEP1_32(
            nullptr,
            values,
            index,
            Twine(name).concat(".at.") + Twine(index));

        StoreToValue(builder, valuePtr);
    }

    void StoreToValues(TCGIRBuilderPtr& builder, Value* values, int index, ui16 id)
    {
        auto name = values->getName();

        auto valuePtr = builder->CreateConstInBoundsGEP1_32(
            nullptr,
            values,
            index,
            Twine(name).concat(".at.") + Twine(index));

        StoreToValue(builder, valuePtr, id);
    }

    void StoreToValue(TCGIRBuilderPtr& builder, Value* valuePtr, ui16 id, Twine nameTwine = "")
    {
        StoreToValue(builder, valuePtr, nameTwine);
        builder->CreateStore(
            builder->getInt16(id),
            builder->CreateStructGEP(nullptr, valuePtr, TTypeBuilder::Id, nameTwine + ".idPtr"));
    }

    void StoreToValue(TCGIRBuilderPtr& builder, Value* valuePtr, Twine nameTwine = "")
    {
        const auto& type = TypeBuilder<NTableClient::TUnversionedValue, false>::TType::get(builder->getContext());
        builder->CreateStore(
            builder->CreateSelect(
                IsNull(builder),
                ConstantInt::get(type, static_cast<int>(EValueType::Null)),
                ConstantInt::get(type, static_cast<int>(StaticType_))),
            builder->CreateStructGEP(nullptr, valuePtr, TTypeBuilder::Type, nameTwine + ".typePtr"));

        if (IsStringLikeType(StaticType_)) {
            builder->CreateStore(
                Length_,
                builder->CreateStructGEP(nullptr, valuePtr, TTypeBuilder::Length, nameTwine + ".lengthPtr"));
        }

        Value* data = nullptr;
        auto targetType = TDataTypeBuilder::get(builder->getContext());

        if (Data_->getType()->isPointerTy()) {
            data = builder->CreatePtrToInt(Data_, targetType);
        } else if (Data_->getType()->isFloatingPointTy()) {
            data = builder->CreateBitCast(Data_, targetType);
        } else {
            data = builder->CreateIntCast(Data_, targetType, false);
        }

        builder->CreateStore(
            data,
            builder->CreateStructGEP(nullptr, valuePtr, TTypeBuilder::Data, nameTwine + ".dataPtr"));

    }

    Value* IsNull(TCGIRBuilderPtr& builder)
    {
        return IsNull_;
    }

    Value* GetLength(TCGIRBuilderPtr& builder)
    {
        return Length_;
    }

    Value* GetData(TCGIRBuilderPtr& builder)
    {
        return Data_;
    }

    TCGValue Cast(TCGIRBuilderPtr& builder, EValueType dest)
    {
        if (dest == StaticType_) {
            return *this;
        }

        auto value = GetData(builder);

        Value* result;
        if (dest == EValueType::Int64) {
            auto destType = TDataTypeBuilder::TInt64::get(builder->getContext());
            if (StaticType_ == EValueType::Uint64 || StaticType_ == EValueType::Boolean) {
                result = builder->CreateIntCast(value, destType, false);
            } else if (StaticType_ == EValueType::Double) {
                result = builder->CreateFPToSI(value, destType);
            } else {
                Y_UNREACHABLE();
            }
        } else if (dest == EValueType::Uint64) {
            // signed/unsigned are equal to llvm
            auto destType = TDataTypeBuilder::TInt64::get(builder->getContext());
            if (StaticType_ == EValueType::Int64 || StaticType_ == EValueType::Boolean) {
                result = builder->CreateIntCast(value, destType, true);
            } else if (StaticType_ == EValueType::Double) {
                result = builder->CreateFPToUI(value, destType);
            } else {
                Y_UNREACHABLE();
            }
        } else if (dest == EValueType::Double) {
            auto destType = TDataTypeBuilder::TDouble::get(builder->getContext());
            if (StaticType_ == EValueType::Uint64) {
                result = builder->CreateUIToFP(value, destType);
            } else if (StaticType_ == EValueType::Int64) {
                result = builder->CreateSIToFP(value, destType);
            } else {
                Y_UNREACHABLE();
            }
        } else {
            Y_UNREACHABLE();
        }

        return CreateFromValue(
            builder,
            IsNull(builder),
            IsStringLikeType(StaticType_)
                ? GetLength(builder)
                : nullptr,
            result,
            dest);
    }
};

////////////////////////////////////////////////////////////////////////////////

class TCGBaseContext
    : public TCGIRBuilderPtr
{
public:
    const TCGModulePtr Module;

    TCGBaseContext(
        const TCGIRBuilderPtr& base,
        const TCGModulePtr& module)
        : TCGIRBuilderPtr(base)
        , Module(module)
    { }

    TCGBaseContext(
        const TCGIRBuilderPtr& base,
        const TCGBaseContext& other)
        : TCGIRBuilderPtr(base)
        , Module(other.Module)
    { }

};

class TCGOpaqueValuesContext
    : public TCGBaseContext
{
public:
    TCGOpaqueValuesContext(
        const TCGBaseContext& base,
        Value* opaqueValues)
        : TCGBaseContext(base)
        , OpaqueValues_(opaqueValues)
    { }

    TCGOpaqueValuesContext(
        const TCGBaseContext& base,
        const TCGOpaqueValuesContext& other)
        : TCGBaseContext(base)
        , OpaqueValues_(other.OpaqueValues_)

    { }

    Value* GetOpaqueValues() const
    {
        return Builder_->ViaClosure(OpaqueValues_, "opaqueValues");
    }

    Value* GetOpaqueValue(size_t index) const
    {
        Value* opaqueValues = GetOpaqueValues();
        return Builder_->CreateLoad(
            Builder_->CreateConstGEP1_32(opaqueValues, index),
            "opaqueValues." + Twine(index));
    }

private:
    Value* const OpaqueValues_;

};

struct TCodegenFragmentInfo;
struct TCodegenFragmentInfos;

class TCGExprData
{
public:
    typedef TypeBuilder<TExpressionClosure, false> TClosureTypeBuilder;

    const TCodegenFragmentInfos& ExpressionFragments;

    Value* const Buffer;
    Value* const RowValues;
    Value* const ExpressionClosurePtr;

    TCGExprData(
        const TCodegenFragmentInfos& expressionFragments,
        Value* buffer,
        Value* rowValues,
        Value* expressionClosurePtr)
        : ExpressionFragments(expressionFragments)
        , Buffer(buffer)
        , RowValues(rowValues)
        , ExpressionClosurePtr(expressionClosurePtr)
    { }

};

class TCGExprContext
    : public TCGOpaqueValuesContext
    , public TCGExprData
{
public:
    TCGExprContext(
        const TCGOpaqueValuesContext& base,
        TCGExprData exprData)
        : TCGOpaqueValuesContext(base)
        , TCGExprData(exprData)
    { }

    TCGExprContext(
        const TCGOpaqueValuesContext& base,
        const TCGExprContext& other)
        : TCGOpaqueValuesContext(base)
        , TCGExprData(other)
    { }

    static TCGExprContext Make(
        const TCGBaseContext& builder,
        const TCodegenFragmentInfos& fragmentInfos,
        Value* expressionClosure);

    static TCGExprContext Make(
        const TCGOpaqueValuesContext& builder,
        const TCodegenFragmentInfos& fragmentInfos,
        Value* row,
        Value* buffer);

    Value* GetExpressionClosurePtr();

    Value* GetFragmentResult(size_t index) const;
    Value* GetFragmentFlag(size_t index) const;
};

struct TCGContext;

typedef std::function<void(TCGContext& builder, Value* row)> TCodegenConsumer;

class TCGOperatorContext
    : public TCGOpaqueValuesContext
{
protected:
    Value* const ExecutionContext_;
    std::vector<std::shared_ptr<TCodegenConsumer>>* Consumers_ = nullptr;

public:
    TCGOperatorContext(
        const TCGOpaqueValuesContext& base,
        Value* executionContext,
        std::vector<std::shared_ptr<TCodegenConsumer>>* consumers)
        : TCGOpaqueValuesContext(base)
        , ExecutionContext_(executionContext)
        , Consumers_(consumers)
    { }

    TCGOperatorContext(
        const TCGOpaqueValuesContext& base,
        const TCGOperatorContext& other)
        : TCGOpaqueValuesContext(base)
        , ExecutionContext_(other.ExecutionContext_)
        , Consumers_(other.Consumers_)
    { }

    Value* GetExecutionContext() const
    {
        return Builder_->ViaClosure(ExecutionContext_, "executionContext");
    }

    TCodegenConsumer& operator[] (size_t index) const;

};

struct TCGContext
    : public TCGOperatorContext
{
    TCGContext(
        const TCGOperatorContext& base,
        Value* buffer)
        : TCGOperatorContext(base)
        , Buffer(buffer)
    { }

    Value* const Buffer;

};

////////////////////////////////////////////////////////////////////////////////
TCGValue MakePhi(
    TCGIRBuilderPtr& builder,
    BasicBlock* thenBB,
    BasicBlock* elseBB,
    BasicBlock* endBB,
    TCGValue thenValue,
    TCGValue elseValue,
    Twine name = Twine());

Value* MakePhi(
    TCGIRBuilderPtr& builder,
    BasicBlock* thenBB,
    BasicBlock* elseBB,
    BasicBlock* endBB,
    Value* thenValue,
    Value* elseValue,
    Twine name = Twine());

template <class TBuilder, class TResult>
TResult CodegenIf(
    TBuilder& builder,
    Value* condition,
    const std::function<TResult(TBuilder& builder)>& thenCodegen,
    const std::function<TResult(TBuilder& builder)>& elseCodegen,
    Twine name = Twine())
{
    if (llvm::Constant* constantCondition = llvm::dyn_cast<llvm::Constant>(condition)) {
        if (constantCondition->isNullValue()) {
            return elseCodegen(builder);
        } else {
            return thenCodegen(builder);
        }
    }

    auto* thenBB = builder->CreateBBHere("then");
    auto* elseBB = builder->CreateBBHere("else");
    auto* endBB = builder->CreateBBHere("end");

    builder->CreateCondBr(condition, thenBB, elseBB);

    builder->SetInsertPoint(thenBB);
    auto thenValue = thenCodegen(builder);
    thenBB = builder->GetInsertBlock();

    builder->SetInsertPoint(elseBB);
    auto elseValue = elseCodegen(builder);
    elseBB = builder->GetInsertBlock();

    builder->SetInsertPoint(endBB);

    auto result = MakePhi(builder, thenBB, elseBB, endBB, thenValue, elseValue, name);

    builder->SetInsertPoint(thenBB);
    builder->CreateBr(endBB);

    builder->SetInsertPoint(elseBB);
    builder->CreateBr(endBB);

    builder->SetInsertPoint(endBB);

    return result;
}

template <class TBuilder>
void CodegenIf(
    TBuilder& builder,
    Value* condition,
    const std::function<void(TBuilder& builder)>& thenCodegen,
    const std::function<void(TBuilder& builder)>& elseCodegen)
{
    if (llvm::Constant* constantCondition = llvm::dyn_cast<llvm::Constant>(condition)) {
        if (constantCondition->isNullValue()) {
            elseCodegen(builder);
        } else {
            thenCodegen(builder);
        }

        return;
    }

    auto* thenBB = builder->CreateBBHere("then");
    auto* elseBB = builder->CreateBBHere("else");
    auto* endBB = builder->CreateBBHere("end");

    builder->CreateCondBr(condition, thenBB, elseBB);

    builder->SetInsertPoint(thenBB);
    thenCodegen(builder);
    builder->CreateBr(endBB);
    thenBB = builder->GetInsertBlock();

    builder->SetInsertPoint(elseBB);
    elseCodegen(builder);
    builder->CreateBr(endBB);
    elseBB = builder->GetInsertBlock();

    builder->SetInsertPoint(endBB);
}

template <class TBuilder>
void CodegenIf(
    TBuilder& builder,
    Value* condition,
    const std::function<void(TBuilder& builder)>& thenCodegen)
{
    if (llvm::Constant* constantCondition = llvm::dyn_cast<llvm::Constant>(condition)) {
        if (!constantCondition->isNullValue()) {
            thenCodegen(builder);
        }

        return;
    }
    auto* thenBB = builder->CreateBBHere("then");
    auto* endBB = builder->CreateBBHere("end");

    builder->CreateCondBr(condition, thenBB, endBB);

    builder->SetInsertPoint(thenBB);
    thenCodegen(builder);
    builder->CreateBr(endBB);
    thenBB = builder->GetInsertBlock();

    builder->SetInsertPoint(endBB);
}

////////////////////////////////////////////////////////////////////////////////

template <class TSequence>
struct TApplyCallback;

template <unsigned... Indexes>
struct TApplyCallback<NMpl::TSequence<Indexes...>>
{
    template <class TBody, class TBuilder>
    static void Do(TBody&& body, TBuilder&& builder, Value* argsArray[sizeof...(Indexes)])
    {
        body(builder, argsArray[Indexes]...);
    }
};

////////////////////////////////////////////////////////////////////////////////

struct TLlvmClosure
{
    Value* ClosurePtr;
    llvm::Function* Function;
};

template <class TSignature>
struct TClosureFunctionDefiner;

template <class TResult, class... TArgs>
struct TClosureFunctionDefiner<TResult(TArgs...)>
{
    typedef typename NMpl::TGenerateSequence<sizeof...(TArgs)>::TType TIndexesPack;

    template <class TBody>
    static TLlvmClosure Do(const TCGModulePtr& module, TCGOperatorContext& parentBuilder, TBody&& body, llvm::Twine name)
    {
        Function* function = Function::Create(
            TypeBuilder<TResult(void**, TArgs...), false>::get(module->GetModule()->getContext()),
            Function::ExternalLinkage,
            name,
            module->GetModule());

        function->addFnAttr(llvm::Attribute::AttrKind::UWTable);

        auto args = function->arg_begin();
        Value* closurePtr = ConvertToPointer(args++); closurePtr->setName("closure");

        Value* argsArray[sizeof...(TArgs)];
        size_t index = 0;
        while (args != function->arg_end()) {
            argsArray[index++] = ConvertToPointer(args++);
        }
        YCHECK(index == sizeof...(TArgs));

        TCGIRBuilder baseBuilder(function, parentBuilder.GetBuilder(), closurePtr);
        TCGOperatorContext builder(
            TCGOpaqueValuesContext(
                TCGBaseContext(
                    TCGIRBuilderPtr(&baseBuilder),
                    parentBuilder),
                parentBuilder),
            parentBuilder);
        TApplyCallback<TIndexesPack>::template Do(std::forward<TBody>(body), builder, argsArray);

        return TLlvmClosure{builder->GetClosure(), function};
    }
};

template <class TSignature, class TBody>
TLlvmClosure MakeClosure(TCGOperatorContext& builder, llvm::Twine name, TBody&& body)
{
    return TClosureFunctionDefiner<TSignature>::Do(
        builder.Module,
        builder,
        std::forward<TBody>(body),
        name);
}

template <class TSignature>
struct TFunctionDefiner;

template <class TResult, class... TArgs>
struct TFunctionDefiner<TResult(TArgs...)>
{
    typedef typename NMpl::TGenerateSequence<sizeof...(TArgs)>::TType TIndexesPack;

    template <class TBody>
    static Function* Do(const TCGModulePtr& module, TBody&& body, llvm::Twine name)
    {
        auto& llvmContext = module->GetModule()->getContext();
        Function* function =  Function::Create(
            FunctionType::get(
                TypeBuilder<TResult, false>::get(llvmContext),
                {
                    TypeBuilder<TArgs, false>::get(llvmContext)...
                },
                false),
            Function::ExternalLinkage,
            name,
            module->GetModule());

        function->addFnAttr(llvm::Attribute::AttrKind::UWTable);

        auto args = function->arg_begin();
        Value* argsArray[sizeof...(TArgs)];
        size_t index = 0;
        while (args != function->arg_end()) {
            argsArray[index++] = ConvertToPointer(args++);
        }
        YCHECK(index == sizeof...(TArgs));

        TCGIRBuilder builder(function);
        TCGBaseContext context(TCGIRBuilderPtr(&builder), module);

        TApplyCallback<TIndexesPack>::template Do(std::forward<TBody>(body), context, argsArray);

        return function;
    }
};

template <class TSignature, class TBody>
Function* MakeFunction(const TCGModulePtr& module, llvm::Twine name, TBody&& body)
{
    auto function = TFunctionDefiner<TSignature>::Do(
        module,
        std::forward<TBody>(body),
        name);

    return function;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NQueryClient
} // namespace NYT
