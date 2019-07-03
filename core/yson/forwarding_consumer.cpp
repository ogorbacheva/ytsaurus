#include "forwarding_consumer.h"

#include <yt/core/misc/assert.h>

namespace NYT::NYson {

////////////////////////////////////////////////////////////////////////////////

void TForwardingYsonConsumer::Forward(
    IYsonConsumer* consumer,
    std::function<void()> onFinished,
    EYsonType type)
{
    YT_ASSERT(!ForwardingConsumer_);
    YT_ASSERT(consumer);
    YT_ASSERT(ForwardingDepth_ == 0);

    ForwardingConsumer_ = consumer;
    OnFinished_ = std::move(onFinished);
    ForwardingType_ = type;
}

bool TForwardingYsonConsumer::CheckForwarding(int depthDelta)
{
    if (ForwardingDepth_ + depthDelta < 0) {
        FinishForwarding();
    }
    return ForwardingConsumer_ != nullptr;
}

void TForwardingYsonConsumer::UpdateDepth(int depthDelta, bool checkFinish)
{
    ForwardingDepth_ += depthDelta;
    YT_ASSERT(ForwardingDepth_ >= 0);
    if (checkFinish && ForwardingType_ == EYsonType::Node && ForwardingDepth_ == 0) {
        FinishForwarding();
    }
}

void TForwardingYsonConsumer::FinishForwarding()
{
    ForwardingConsumer_ = nullptr;
    if (OnFinished_) {
        OnFinished_();
        OnFinished_ = nullptr;
    }
}

void TForwardingYsonConsumer::OnStringScalar(TStringBuf value)
{
    if (CheckForwarding()) {
        ForwardingConsumer_->OnStringScalar(value);
        UpdateDepth(0);
    } else {
        OnMyStringScalar(value);
    }
}

void TForwardingYsonConsumer::OnInt64Scalar(i64 value)
{
    if (CheckForwarding()) {
        ForwardingConsumer_->OnInt64Scalar(value);
        UpdateDepth(0);
    } else {
        OnMyInt64Scalar(value);
    }
}

void TForwardingYsonConsumer::OnUint64Scalar(ui64 value)
{
    if (CheckForwarding()) {
        ForwardingConsumer_->OnUint64Scalar(value);
        UpdateDepth(0);
    } else {
        OnMyUint64Scalar(value);
    }
}

void TForwardingYsonConsumer::OnDoubleScalar(double value)
{
    if (CheckForwarding()) {
        ForwardingConsumer_->OnDoubleScalar(value);
        UpdateDepth(0);
    } else {
        OnMyDoubleScalar(value);
    }
}

void TForwardingYsonConsumer::OnBooleanScalar(bool value)
{
    if (CheckForwarding()) {
        ForwardingConsumer_->OnBooleanScalar(value);
        UpdateDepth(0);
    } else {
        OnMyBooleanScalar(value);
    }
}

void TForwardingYsonConsumer::OnEntity()
{
    if (CheckForwarding()) {
        ForwardingConsumer_->OnEntity();
        UpdateDepth(0);
    } else {
        OnMyEntity();
    }
}

void TForwardingYsonConsumer::OnBeginList()
{
    if (CheckForwarding(+1)) {
        ForwardingConsumer_->OnBeginList();
        UpdateDepth(+1);
    } else {
        OnMyBeginList();
    }
}

void TForwardingYsonConsumer::OnListItem()
{
    if (CheckForwarding()) {
        ForwardingConsumer_->OnListItem();
    } else {
        OnMyListItem();
    }
}

void TForwardingYsonConsumer::OnEndList()
{
    if (CheckForwarding(-1)) {
        ForwardingConsumer_->OnEndList();
        UpdateDepth(-1);
    } else {
        OnMyEndList();
    }
}

void TForwardingYsonConsumer::OnBeginMap()
{
    if (CheckForwarding(+1)) {
        ForwardingConsumer_->OnBeginMap();
        UpdateDepth(+1);
    } else {
        OnMyBeginMap();
    }
}

void TForwardingYsonConsumer::OnKeyedItem(TStringBuf name)
{
    if (CheckForwarding()) {
        ForwardingConsumer_->OnKeyedItem(name);
    } else {
        OnMyKeyedItem(name);
    }
}

void TForwardingYsonConsumer::OnEndMap()
{
    if (CheckForwarding(-1)) {
        ForwardingConsumer_->OnEndMap();
        UpdateDepth(-1);
    } else {
        OnMyEndMap();
    }
}

void TForwardingYsonConsumer::OnRaw(TStringBuf yson, EYsonType type)
{
    if (CheckForwarding()) {
        ForwardingConsumer_->OnRaw(yson, type);
        UpdateDepth(0);
    } else {
        OnMyRaw(yson, type);
    }
}

void TForwardingYsonConsumer::OnBeginAttributes()
{
    if (CheckForwarding(+1)) {
        ForwardingConsumer_->OnBeginAttributes();
        UpdateDepth(+1);
    } else {
        OnMyBeginAttributes();
    }
}

void TForwardingYsonConsumer::OnEndAttributes()
{
    if (CheckForwarding(-1)) {
        ForwardingConsumer_->OnEndAttributes();
        UpdateDepth(-1, false);
    } else {
        OnMyEndAttributes();
    }
}

////////////////////////////////////////////////////////////////////////////////

void TForwardingYsonConsumer::OnMyStringScalar(TStringBuf /*value*/)
{
    YT_ABORT();
}

void TForwardingYsonConsumer::OnMyInt64Scalar(i64 /*value*/)
{
    YT_ABORT();
}

void TForwardingYsonConsumer::OnMyUint64Scalar(ui64 /*value*/)
{
    YT_ABORT();
}

void TForwardingYsonConsumer::OnMyDoubleScalar(double /*value*/)
{
    YT_ABORT();
}

void TForwardingYsonConsumer::OnMyBooleanScalar(bool /*value*/)
{
    YT_ABORT();
}

void TForwardingYsonConsumer::OnMyEntity()
{
    YT_ABORT();
}

void TForwardingYsonConsumer::OnMyBeginList()
{
    YT_ABORT();
}

void TForwardingYsonConsumer::OnMyListItem()
{
    YT_ABORT();
}

void TForwardingYsonConsumer::OnMyEndList()
{
    YT_ABORT();
}

void TForwardingYsonConsumer::OnMyBeginMap()
{
    YT_ABORT();
}

void TForwardingYsonConsumer::OnMyKeyedItem(TStringBuf /*name*/)
{
    YT_ABORT();
}

void TForwardingYsonConsumer::OnMyEndMap()
{
    YT_ABORT();
}

void TForwardingYsonConsumer::OnMyBeginAttributes()
{
    YT_ABORT();
}

void TForwardingYsonConsumer::OnMyEndAttributes()
{
    YT_ABORT();
}

void TForwardingYsonConsumer::OnMyRaw(TStringBuf yson, EYsonType type)
{
    TYsonConsumerBase::OnRaw(yson, type);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NYson
