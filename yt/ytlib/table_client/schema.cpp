#include "stdafx.h"
#include "schema.h"

#include "../misc/assert.h"

namespace NYT {
namespace NTableClient {

////////////////////////////////////////////////////////////////////////////////

TRange::TRange(const TColumn& begin, const TColumn& end)
    : IsInfinite_(false)
    , Begin_(begin)
    , End_(end)
{
    YASSERT(begin < end);
}

TRange::TRange(const TColumn& begin)
    : IsInfinite_(true)
    , Begin_(begin)
    , End_("")
{ }

TColumn TRange::Begin() const
{
    return Begin_;
}

TColumn TRange::End() const
{
    return End_;
}

NProto::TRange TRange::ToProto() const
{
    NProto::TRange protoRange;
    protoRange.SetBegin(Begin_);
    protoRange.SetEnd(End_);
    protoRange.SetIsInfinite(IsInfinite_);
    return protoRange;
}

TRange TRange::FromProto(const NProto::TRange& protoRange)
{
    if (protoRange.GetIsInfinite()) {
        return TRange(protoRange.GetBegin());
    } else {
        return TRange(protoRange.GetBegin(), protoRange.GetEnd());
    }
}

bool TRange::Contains(const TColumn& value) const
{
    if (value < Begin_)
        return false;

    if (!IsInfinite() && value >= End_)
        return false;

    return true;
}

bool TRange::Contains(const TRange& range) const
{
    if (range.IsInfinite()) {
        return Contains(range.Begin()) && IsInfinite();
    } else if (IsInfinite()) {
        return Contains(range.Begin());
    } else {
        return Contains(range.Begin()) && range.End() <= End_;
    }
}

bool TRange::Overlaps(const TRange& range) const
{
    if ((Begin_ <= range.Begin_ && (IsInfinite() || range.Begin_ < End_)) || 
        (Begin_ < range.End_ && (IsInfinite() || range.End_ <= End_)) ||
        (range.Begin_ <= Begin_ && (range.IsInfinite() || Begin_ < range.End_)))
    {
        return true;
    } else {
        return false;
    }
}

bool TRange::IsInfinite() const
{
    return IsInfinite_;
}

////////////////////////////////////////////////////////////////////////////////

void TChannel::AddColumn(const TColumn& column)
{
    FOREACH(auto& oldColumn, Columns){
        if (oldColumn == column) {
            return;
        }
    }

    Columns.push_back(column);
}

void TChannel::AddRange(const TRange& range)
{
    Ranges.push_back(range);
}

void TChannel::AddRange(const TColumn& begin, const TColumn& end)
{
    Ranges.push_back(TRange(begin, end));
}

NProto::TChannel TChannel::ToProto() const
{
    NProto::TChannel protoChannel;
    FOREACH(auto column, Columns) {
        protoChannel.AddColumns(~column);
    }

    FOREACH(const auto& range, Ranges) {
        *protoChannel.AddRanges() = range.ToProto();
    }
    return protoChannel;
}

NYT::NTableClient::TChannel TChannel::FromProto( const NProto::TChannel& protoChannel )
{
    TChannel result;
    for (int i = 0; i < protoChannel.columns_size(); ++i) {
        result.AddColumn(protoChannel.GetColumns(i));
    }

    for (int i = 0; i < protoChannel.ranges_size(); ++i) {
        result.AddRange(TRange::FromProto(protoChannel.GetRanges(i)));
    }
    return result;
}

bool TChannel::Contains(const TColumn& column) const
{
    FOREACH(auto& oldColumn, Columns) {
        if (oldColumn == column) {
            return true;
        }
    }
    return ContainsInRanges(column);
}

bool TChannel::Contains(const TRange& range) const
{
    FOREACH(auto& currentRange, Ranges) {
        if (currentRange.Contains(range)) {
            return true;
        }
    }
    return false;
}

bool TChannel::Contains(const TChannel& channel) const
{
    FOREACH(auto& column, channel.Columns) {
        if (!Contains(column)) {
            return false;
        }
    }

    FOREACH(auto& range, channel.Ranges) {
        if (!Contains(range)) {
            return false;
        }
    }

    return true;
}

bool TChannel::ContainsInRanges(const TColumn& column) const
{
    FOREACH(auto& range, Ranges) {
        if (range.Contains(column)) {
            return true;
        }
    }
    return false;
}

bool TChannel::Overlaps(const TRange& range) const
{
    FOREACH(auto& column, Columns) {
        if (range.Contains(column)) {
            return true;
        }
    }

    FOREACH(auto& currentRange, Ranges) {
        if (currentRange.Overlaps(range)){
            return true;
        }
    }

    return false;
}

bool TChannel::Overlaps(const TChannel& channel) const
{
    FOREACH(auto& column, channel.Columns) {
        if (Contains(column)) {
            return true;
        }
    }

    FOREACH(auto& range, channel.Ranges) {
        if (Overlaps(range)) {
            return true;
        }
    }

    return false;
}

const yvector<TColumn>& TChannel::GetColumns() const
{
    return Columns;
}

bool TChannel::IsEmpty() const
{
    return Columns.empty() && Ranges.empty();
}


////////////////////////////////////////////////////////////////////////////////

void operator-= (TChannel& lhs, const TChannel& rhs)
{
    yvector<TColumn> newColumns;
    FOREACH(auto column, lhs.Columns) {
        if (!rhs.Contains(column)) {
            newColumns.push_back(column);
        }
    }
    lhs.Columns.swap(newColumns);

    yvector<TRange> rhsRanges(rhs.Ranges);
    FOREACH(auto column, rhs.Columns) {
        // Add single columns as ranges.
        rhsRanges.push_back(TRange(column, column + "\0"));
    }

    yvector<TRange> newRanges;
    FOREACH(auto& rhsRange, rhsRanges) {
        FOREACH(auto& lhsRange, lhs.Ranges) {
            if (!lhsRange.Overlaps(rhsRange)) {
                newRanges.push_back(lhsRange);
                continue;
            } 

            if (lhsRange.Begin() < rhsRange.Begin()) {
                newRanges.push_back(TRange(lhsRange.Begin(), rhsRange.Begin()));
            }

            if (rhsRange.IsInfinite()) {
                continue;
            }

            if (lhsRange.IsInfinite()) {
                newRanges.push_back(TRange(rhsRange.End()));
            } else if (lhsRange.End() > rhsRange.End()) {
                newRanges.push_back(TRange(rhsRange.End(), lhsRange.End()));
            }
        }
        lhs.Ranges.swap(newRanges);
        newRanges.clear();
    }
}

////////////////////////////////////////////////////////////////////////////////

TSchema::TSchema()
{
    TChannel trashChannel;

    // Initially the schema consists of a single trash channel,
    // i.e. [epsilon, infinity).
    // This "trash" channel is expected to be present in any chunk
    // (this is how table writer works now). 
    trashChannel.AddRange(TRange(""));
    Channels.push_back(trashChannel);
}

void TSchema::AddChannel(const TChannel& channel)
{
    // Trash channel always goes first.
    Channels.front() -= channel; 
    Channels.push_back(channel);
}

const yvector<TChannel>& TSchema::GetChannels() const
{
    return Channels;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NTableClient
} // namespace NYT
