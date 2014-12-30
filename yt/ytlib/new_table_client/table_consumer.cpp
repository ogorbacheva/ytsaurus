#include "stdafx.h"

#include "table_consumer.h"

#include "name_table.h"
#include "schemaless_writer.h"

#include <core/concurrency/scheduler.h>

namespace NYT {
namespace NVersionedTableClient {

using namespace NConcurrency;
using namespace NTableClient;
using namespace NYson;

////////////////////////////////////////////////////////////////////////////////

const static i64 MaxBufferSize = (i64) 1 * 1024 * 1024;

////////////////////////////////////////////////////////////////////////////////

TBuildingValueConsumer::TBuildingValueConsumer(
    const TTableSchema& schema,
    const TKeyColumns& keyColumns)
    : Schema_(schema)
    , KeyColumns_(keyColumns)
    , NameTable_(TNameTable::FromSchema(Schema_))
    , WrittenFlags_(Schema_.Columns().size(), false)
{ }

const std::vector<TUnversionedOwningRow>& TBuildingValueConsumer::Rows() const
{
    return Rows_;
}

void TBuildingValueConsumer::SetTreatMissingAsNull(bool value)
{
    TreatMissingAsNull_ = value;
}

TNameTablePtr TBuildingValueConsumer::GetNameTable() const {
    return NameTable_;
}

bool TBuildingValueConsumer::GetAllowUnknownColumns() const {
    return false;
}

void TBuildingValueConsumer::OnBeginRow() {
    // Do nothing
}

void TBuildingValueConsumer::OnValue(const TUnversionedValue& value)
{
    const auto& schemaType = Schema_.Columns()[value.Id].Type;
    if (value.Type != schemaType) {
        THROW_ERROR TError("Invalid type of schema column %Qv: expected %Qlv, actual %Qlv",
            Schema_.Columns()[value.Id].Name,
            schemaType,
            value.Type) << TErrorAttribute("row_index", Rows_.size());
    }
    WrittenFlags_[value.Id] = true;
    Builder_.AddValue(value);
}

void TBuildingValueConsumer::OnEndRow() {
    for (int id = 0; id < WrittenFlags_.size(); ++id) {
        if (WrittenFlags_[id]) {
            WrittenFlags_[id] = false;
        } else if (TreatMissingAsNull_) {
            Builder_.AddValue(MakeUnversionedSentinelValue(EValueType::Null, id));
        }
    }

    std::sort(
        Builder_.BeginValues(),
        Builder_.EndValues(),
        [] (const TUnversionedValue& lhs, const TUnversionedValue& rhs) {
            return lhs.Id < rhs.Id;
        });
    Rows_.emplace_back(Builder_.FinishRow());
}

////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////

TTableConsumer::TTableConsumer(const std::vector<IValueConsumerPtr>& valueConsumers)
    : ValueConsumers_(valueConsumers)
    , ControlState_(EControlState::None)
    , ValueWriter_(&ValueBuffer_)
    , Depth_(0)
    , ColumnIndex_(0)
    , RowIndex_(0)
{
    YCHECK(!ValueConsumers_.empty());
    CurrentValueConsumer_ = ValueConsumers_.front().Get();
}

TTableConsumer::TTableConsumer(IValueConsumerPtr valueConsumer)
    : TTableConsumer(std::vector<IValueConsumerPtr>(1, valueConsumer))
{ }

TError TTableConsumer::AttachLocationAttributes(TError error)
{
    return error << TErrorAttribute("row_index", RowIndex_);
}

void TTableConsumer::OnControlInt64Scalar(i64 value)
{
    switch (ControlAttribute_) {
        case EControlAttribute::TableIndex:
            if (value >= ValueConsumers_.size()) {
                THROW_ERROR AttachLocationAttributes(TError("Invalid table index %v", value));
            }
            CurrentValueConsumer_ = ValueConsumers_[value].Get();
            break;

        default:
            ThrowControlAttributesNotSupported();
    }
}

void TTableConsumer::OnControlStringScalar(const TStringBuf& /*value*/)
{
    ThrowControlAttributesNotSupported();
}

void TTableConsumer::OnStringScalar(const TStringBuf& value)
{
    if (ControlState_ == EControlState::ExpectValue) {
        YASSERT(Depth_ == 1);
        OnControlStringScalar(value);
        return;
    }

    YASSERT(ControlState_ == EControlState::None);

    if (Depth_ == 0) {
        ThrowMapExpected();
    } else if (Depth_ == 1) {
        CurrentValueConsumer_->OnValue(MakeUnversionedStringValue(value, ColumnIndex_));
    } else {
        ValueWriter_.OnStringScalar(value);
    }
}

void TTableConsumer::OnInt64Scalar(i64 value)
{
    if (ControlState_ == EControlState::ExpectValue) {
        YASSERT(Depth_ == 1);
        OnControlInt64Scalar(value);
        return;
    }

    YASSERT(ControlState_ == EControlState::None);

    if (Depth_ == 0) {
        ThrowMapExpected();
    } else if (Depth_ == 1) {
        CurrentValueConsumer_->OnValue(MakeUnversionedInt64Value(value, ColumnIndex_));
    } else {
        ValueWriter_.OnInt64Scalar(value);
    }
}

void TTableConsumer::OnUint64Scalar(ui64 value)
{
    if (ControlState_ == EControlState::ExpectValue) {
        ThrowInvalidControlAttribute("be an unsigned integer");
    }

    YASSERT(ControlState_ == EControlState::None);

    if (Depth_ == 0) {
        ThrowMapExpected();
    } else if (Depth_ == 1) {
        CurrentValueConsumer_->OnValue(MakeUnversionedUint64Value(value, ColumnIndex_));
    } else {
        ValueWriter_.OnUint64Scalar(value);
    }
}

void TTableConsumer::OnDoubleScalar(double value)
{
    if (ControlState_ == EControlState::ExpectValue) {
        YASSERT(Depth_ == 1);
        ThrowInvalidControlAttribute("be a double value");
        return;
    }

    YASSERT(ControlState_ == EControlState::None);

    if (Depth_ == 0) {
        ThrowMapExpected();
    } else if (Depth_ == 1) {
        CurrentValueConsumer_->OnValue(MakeUnversionedDoubleValue(value, ColumnIndex_));
    } else {
        ValueWriter_.OnDoubleScalar(value);
    }
}

void TTableConsumer::OnBooleanScalar(bool value)
{
    if (ControlState_ == EControlState::ExpectValue) {
        YASSERT(Depth_ == 1);
        ThrowInvalidControlAttribute("be a boolean value");
        return;
    }

    YASSERT(ControlState_ == EControlState::None);

    if (Depth_ == 0) {
        ThrowMapExpected();
    } else if (Depth_ == 1) {
        CurrentValueConsumer_->OnValue(MakeBooleanValue<TUnversionedValue>(value, ColumnIndex_));
    } else {
        ValueWriter_.OnBooleanScalar(value);
    }
}

void TTableConsumer::OnEntity()
{
    switch (ControlState_) {
        case EControlState::None:
            break;

        case EControlState::ExpectEntity:
            YASSERT(Depth_ == 0);
            // Successfully processed control statement.
            ControlState_ = EControlState::None;
            return;

        case EControlState::ExpectValue:
            ThrowInvalidControlAttribute("be an entity");
            break;

        default:
            YUNREACHABLE();
    }


    if (Depth_ == 0) {
        ThrowMapExpected();
    } else {
        ValueWriter_.OnEntity();
    }
}

void TTableConsumer::OnBeginList()
{
    if (ControlState_ == EControlState::ExpectValue) {
        YASSERT(Depth_ == 1);
        ThrowInvalidControlAttribute("be a list");
        return;
    }

    YASSERT(ControlState_ == EControlState::None);

    if (Depth_ == 0) {
        ThrowMapExpected();
    } else {
        if (Depth_ == 1) {
            ValueBegin_ = ValueBuffer_.Begin() + ValueBuffer_.Size();
        }
        ValueWriter_.OnBeginList();
    }
    ++Depth_;
}

void TTableConsumer::OnBeginAttributes()
{
    if (ControlState_ == EControlState::ExpectValue) {
        YASSERT(Depth_ == 1);
        ThrowInvalidControlAttribute("have attributes");
    }

    YASSERT(ControlState_ == EControlState::None);

    if (Depth_ == 0) {
        ControlState_ = EControlState::ExpectName;
    } else {
        if (Depth_ == 1) {
            ValueBegin_ = ValueBuffer_.Begin() + ValueBuffer_.Size();
        }
        ValueWriter_.OnBeginAttributes();
    }

    ++Depth_;
}

void TTableConsumer::ThrowControlAttributesNotSupported()
{
    THROW_ERROR AttachLocationAttributes(TError("Control attributes are not supported"));
}

void TTableConsumer::ThrowMapExpected()
{
    THROW_ERROR AttachLocationAttributes(TError("Invalid row format, map expected"));
}

void TTableConsumer::ThrowCompositesNotSupported() {
    THROW_ERROR AttachLocationAttributes(TError("Composite types are not supported"));
}

void TTableConsumer::ThrowInvalidControlAttribute(const Stroka& whatsWrong)
{
    THROW_ERROR AttachLocationAttributes(TError("Control attribute %Qlv cannot %v",
        ControlAttribute_,
        whatsWrong));
}

void TTableConsumer::OnListItem()
{
    YASSERT(ControlState_ == EControlState::None);

    if (Depth_ == 0) {
        // Row separator, do nothing.
    } else {
        ValueWriter_.OnListItem();
    }
}

void TTableConsumer::OnBeginMap()
{
    if (ControlState_ == EControlState::ExpectValue) {
        YASSERT(Depth_ == 1);
        ThrowInvalidControlAttribute("be a map");
    }

    YASSERT(ControlState_ == EControlState::None);

    if (Depth_ == 0) {
        CurrentValueConsumer_->OnBeginRow();
    } else {
        if (Depth_ == 1) {
            ValueBegin_ = ValueBuffer_.Begin() + ValueBuffer_.Size();
        }
        ValueWriter_.OnBeginMap();
    }
    ++Depth_;
}

void TTableConsumer::OnKeyedItem(const TStringBuf& name)
{
    switch (ControlState_) {
        case EControlState::None:
            break;

        case EControlState::ExpectName:
            YASSERT(Depth_ == 1);
            try {
                ControlAttribute_ = ParseEnum<EControlAttribute>(ToString(name));
            } catch (const std::exception&) {
                // Ignore ex, our custom message is more meaningful.
                THROW_ERROR AttachLocationAttributes(TError("Failed to parse control attribute name %Qv", name));
            }
            ControlState_ = EControlState::ExpectValue;
            return;

        case EControlState::ExpectEndAttributes:
            YASSERT(Depth_ == 1);
            THROW_ERROR AttachLocationAttributes(TError("Too many control attributes per record: at most one attribute is allowed"));
            break;

        default:
            YUNREACHABLE();
    }

    YASSERT(Depth_ > 0);
    if (Depth_ == 1) {
        if (CurrentValueConsumer_->GetAllowUnknownColumns()) {
            ColumnIndex_ = CurrentValueConsumer_->GetNameTable()->GetIdOrRegisterName(name);
        } else {
            auto maybeIndex = CurrentValueConsumer_->GetNameTable()->FindId(name);
            if (!maybeIndex) {
                THROW_ERROR AttachLocationAttributes(TError("No such column %Qv in schema",
                    name));
            }
            ColumnIndex_ = *maybeIndex;
        }
    } else {
        ValueWriter_.OnKeyedItem(name);
    }
}

void TTableConsumer::OnEndMap()
{
    YASSERT(Depth_ > 0);
    // No control attribute allows map or composite values.
    YASSERT(ControlState_ == EControlState::None);

    --Depth_;
    if (Depth_ > 0) {
        ValueWriter_.OnEndMap();
        if (Depth_ == 1) {
            CurrentValueConsumer_->OnValue(MakeUnversionedAnyValue(
                TStringBuf(ValueBegin_, ValueBuffer_.Begin() + ValueBuffer_.Size()),
                ColumnIndex_));
        }
    } else {
        CurrentValueConsumer_->OnEndRow();
        ++RowIndex_;
    }
}

void TTableConsumer::OnEndList()
{
    // No control attribute allow list or composite values.
    YASSERT(ControlState_ == EControlState::None);

    --Depth_;
    YASSERT(Depth_ > 0);

    ValueWriter_.OnEndList();
    if (Depth_ == 1) {
        CurrentValueConsumer_->OnValue(MakeUnversionedAnyValue(
            TStringBuf(ValueBegin_, ValueBuffer_.Begin() + ValueBuffer_.Size()),
            ColumnIndex_));
    }
}

void TTableConsumer::OnEndAttributes()
{
    --Depth_;

    switch (ControlState_) {
        case EControlState::ExpectName:
            THROW_ERROR AttachLocationAttributes(TError("Too few control attributes per record: at least one attribute is required"));
            break;

        case EControlState::ExpectEndAttributes:
            YASSERT(Depth_ == 0);
            ControlState_ = EControlState::ExpectEntity;
            break;

        case EControlState::None:
            YASSERT(Depth_ > 0);
            ValueWriter_.OnEndAttributes();
            break;

        default:
            YUNREACHABLE();
    }
}


void TTableConsumer::OnRaw(const TStringBuf& yson, EYsonType type)
{
    YUNREACHABLE();
}

////////////////////////////////////////////////////////////////////////////////

TWritingValueConsumer::TWritingValueConsumer(ISchemalessWriterPtr writer)
    : Writer_(writer)
    , CurrentBufferSize_(0)
{
    YCHECK(Writer_);
}

void TWritingValueConsumer::Flush() 
{
    if (!Writer_->Write(Rows_)) {
        auto error = WaitFor(Writer_->GetReadyEvent());
        THROW_ERROR_EXCEPTION_IF_FAILED(error, "Table writer failed");
    }

    Rows_.clear();
    OwningRows_.clear();
    CurrentBufferSize_ = 0;
}

TNameTablePtr TWritingValueConsumer::GetNameTable() const 
{
    return Writer_->GetNameTable();
}

bool TWritingValueConsumer::GetAllowUnknownColumns() const 
{
    return true;
}

void TWritingValueConsumer::OnBeginRow() 
{ }

void TWritingValueConsumer::OnValue(const TUnversionedValue& value) 
{
    Builder_.AddValue(value);
}

void TWritingValueConsumer::OnEndRow() 
{
    OwningRows_.emplace_back(Builder_.FinishRow());
    const auto& row = OwningRows_.back();

    CurrentBufferSize_ += row.GetSize();
    Rows_.emplace_back(row.Get());

    if (CurrentBufferSize_ > MaxBufferSize) {
        Flush();
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NVersionedTableClient
} // namespace NYT