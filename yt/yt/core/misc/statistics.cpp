#include "statistics.h"

#include <yt/yt/core/ypath/token.h>
#include <yt/yt/core/ypath/tokenizer.h>

#include <yt/yt/core/ytree/fluent.h>
#include <yt/yt/core/ytree/helpers.h>
#include <yt/yt/core/ytree/serialize.h>

namespace NYT {

using namespace NYTree;
using namespace NYson;
using namespace NYPath;

////////////////////////////////////////////////////////////////////////////////

TSummary::TSummary()
{
    Reset();
}

TSummary::TSummary(i64 sum, i64 count, i64 min, i64 max)
    : Sum_(sum)
    , Count_(count)
    , Min_(min)
    , Max_(max)
{ }

void TSummary::AddSample(i64 sample)
{
    Sum_ += sample;
    Count_ += 1;
    Min_ = std::min(Min_, sample);
    Max_ = std::max(Max_, sample);
}

void TSummary::Update(const TSummary& summary)
{
    Sum_ += summary.GetSum();
    Count_ += summary.GetCount();
    Min_ = std::min(Min_, summary.GetMin());
    Max_ = std::max(Max_, summary.GetMax());
}

void TSummary::Reset()
{
    Sum_ = 0;
    Count_ = 0;
    Min_ = std::numeric_limits<i64>::max();
    Max_ = std::numeric_limits<i64>::min();
}

void TSummary::Persist(const TStreamPersistenceContext& context)
{
    using NYT::Persist;

    Persist(context, Sum_);
    Persist(context, Count_);
    Persist(context, Min_);
    Persist(context, Max_);
}

void Serialize(const TSummary& summary, IYsonConsumer* consumer)
{
    BuildYsonFluently(consumer)
        .BeginMap()
            .Item("sum").Value(summary.GetSum())
            .Item("count").Value(summary.GetCount())
            .Item("min").Value(summary.GetMin())
            .Item("max").Value(summary.GetMax())
        .EndMap();
}

bool TSummary::operator ==(const TSummary& other) const
{
    return
        Sum_ == other.Sum_ &&
        Count_ == other.Count_ &&
        Min_ == other.Min_ &&
        Max_ == other.Max_;
}

////////////////////////////////////////////////////////////////////////////////

TSummary& TStatistics::GetSummary(const NYPath::TYPath& path)
{
    auto result = Data_.emplace(path, TSummary());
    auto it = result.first;
    if (result.second) {
        // This is a new statistic, check validity.
        if (it != Data_.begin()) {
            auto prev = std::prev(it);
            if (HasPrefix(it->first, prev->first)) {
                Data_.erase(it);
                THROW_ERROR_EXCEPTION(
                    "Incompatible statistic paths: old %v, new %v",
                    prev->first,
                    path);
            }
        }
        auto next = std::next(it);
        if (next != Data_.end()) {
            if (HasPrefix(next->first, it->first)) {
                Data_.erase(it);
                THROW_ERROR_EXCEPTION(
                    "Incompatible statistic paths: old %v, new %v",
                    next->first,
                    path);
            }
        }
    }

    return it->second;
}

void TStatistics::AddSample(const NYPath::TYPath& path, i64 sample)
{
    GetSummary(path).AddSample(sample);
}

void TStatistics::AddSample(const NYPath::TYPath& path, const INodePtr& sample)
{
    switch (sample->GetType()) {
        case ENodeType::Int64:
            AddSample(path, sample->AsInt64()->GetValue());
            break;

        case ENodeType::Uint64:
            AddSample(path, static_cast<i64>(sample->AsUint64()->GetValue()));
            break;

        case ENodeType::Map:
            for (const auto& [key, child] : sample->AsMap()->GetChildren()) {
                if (key.empty()) {
                    THROW_ERROR_EXCEPTION("Statistic cannot have an empty name")
                        << TErrorAttribute("path_prefix", path);
                }
                AddSample(path + "/" + ToYPathLiteral(key), child);
            }
            break;

        default:
            THROW_ERROR_EXCEPTION(
                "Invalid statistics type: expected map or integral type but found %v of type %v",
                ConvertToYsonString(sample, EYsonFormat::Text).AsStringBuf(),
                sample->GetType());
    }
}

void TStatistics::Update(const TStatistics& statistics)
{
    for (const auto& [path, summary] : statistics.Data()) {
        GetSummary(path).Update(summary);
    }
}

void TStatistics::AddSuffixToNames(const TString& suffix)
{
    TSummaryMap newData;
    for (const auto& [path, summary] : Data_) {
        newData[path + suffix] = summary;
    }

    std::swap(Data_, newData);
}

TStatistics::TSummaryRange TStatistics::GetRangeByPrefix(const TString& prefix) const
{
    auto begin = Data().lower_bound(prefix + '/');
    // This will efficiently return an iterator to the first path not starting with "`prefix`/".
    auto end = Data().lower_bound(prefix + ('/' + 1));
    return TSummaryRange(begin, end);
}

void TStatistics::Persist(const TStreamPersistenceContext& context)
{
    using NYT::Persist;

    Persist(context, Data_);
}

void Serialize(const TStatistics& statistics, IYsonConsumer* consumer)
{
    using NYT::Serialize;

    if (statistics.GetTimestamp()) {
        consumer->OnBeginAttributes();
        consumer->OnKeyedItem("timestamp");
        NYTree::Serialize(*statistics.GetTimestamp(), consumer);
        consumer->OnEndAttributes();
    }
    consumer->OnBeginMap();

    // Depth of the previous key defined as a number of nested maps before the summary itself.
    int previousDepth = 0;
    TYPath previousKey;
    for (const auto& [currentKey, summary] : statistics.Data()) {
        NYPath::TTokenizer previousTokenizer(previousKey);
        NYPath::TTokenizer currentTokenizer(currentKey);

        // The depth of the common part of two keys, needed to determine the number of maps to close.
        int commonDepth = 0;

        if (previousKey) {
            // First we find the position in which current key is different from the
            // previous one in order to close necessary number of maps.
            commonDepth = 0;
            while (true)
            {
                currentTokenizer.Advance();
                previousTokenizer.Advance();
                // Note that both tokenizers can't reach EndOfStream token, because it would mean that
                // currentKey is prefix of prefixKey or vice versa that is prohibited in TStatistics.
                currentTokenizer.Expect(NYPath::ETokenType::Slash);
                previousTokenizer.Expect(NYPath::ETokenType::Slash);

                currentTokenizer.Advance();
                previousTokenizer.Advance();
                currentTokenizer.Expect(NYPath::ETokenType::Literal);
                previousTokenizer.Expect(NYPath::ETokenType::Literal);
                if (currentTokenizer.GetLiteralValue() == previousTokenizer.GetLiteralValue()) {
                    ++commonDepth;
                } else {
                    break;
                }
            }
            // Close all redundant maps.
            while (previousDepth > commonDepth) {
                consumer->OnEndMap();
                --previousDepth;
            }
        } else {
            currentTokenizer.Advance();
            currentTokenizer.Expect(NYPath::ETokenType::Slash);
            currentTokenizer.Advance();
            currentTokenizer.Expect(NYPath::ETokenType::Literal);
        }

        int currentDepth = commonDepth;
        // Open all newly appeared maps.
        while (true) {
            consumer->OnKeyedItem(currentTokenizer.GetLiteralValue());
            currentTokenizer.Advance();
            if (currentTokenizer.GetType() == NYPath::ETokenType::Slash) {
                consumer->OnBeginMap();
                ++currentDepth;
                currentTokenizer.Advance();
                currentTokenizer.Expect(NYPath::ETokenType::Literal);
            } else if (currentTokenizer.GetType() == NYPath::ETokenType::EndOfStream) {
                break;
            } else {
                YT_VERIFY(false && "Wrong token type in statistics key");
            }
        }
        // Serialize summary.
        Serialize(summary, consumer);

        previousDepth = currentDepth;
        previousKey = currentKey;
    }
    while (previousDepth > 0) {
        consumer->OnEndMap();
        --previousDepth;
    }

    // This OnEndMap is complementary to the OnBeginMap before the main loop.
    consumer->OnEndMap();
}

// Helper function for GetNumericValue.
i64 GetSum(const TSummary& summary)
{
    return summary.GetSum();
}

i64 GetNumericValue(const TStatistics& statistics, const TString& path)
{
    auto value = FindNumericValue(statistics, path);
    if (!value) {
        THROW_ERROR_EXCEPTION("Statistics %v is not present",
            path);
    } else {
        return *value;
    }
}

std::optional<i64> FindNumericValue(const TStatistics& statistics, const TString& path)
{
    auto summary = FindSummary(statistics, path);
    return summary ? std::make_optional(summary->GetSum()) : std::nullopt;
}

std::optional<TSummary> FindSummary(const TStatistics& statistics, const TString& path)
{
    const auto& data = statistics.Data();
    auto iterator = data.lower_bound(path);
    if (iterator != data.end() && iterator->first != path && HasPrefix(iterator->first, path)) {
        THROW_ERROR_EXCEPTION("Invalid statistics type: cannot get summary of %v since it is a map",
            path);
    } else if (iterator == data.end() || iterator->first != path) {
        return std::nullopt;
    } else {
        return iterator->second;
    }
}


////////////////////////////////////////////////////////////////////////////////

class TStatisticsBuildingConsumer
    : public TYsonConsumerBase
    , public IBuildingYsonConsumer<TStatistics>
{
public:
    virtual void OnStringScalar(TStringBuf value) override
    {
        if (!AtAttributes_) {
            THROW_ERROR_EXCEPTION("String scalars are not allowed for statistics");
        }
        Statistics_.SetTimestamp(ConvertTo<TInstant>(value));
    }

    virtual void OnInt64Scalar(i64 value) override
    {
        if (AtAttributes_) {
            THROW_ERROR_EXCEPTION("Timestamp should have string type");
        }
        bool isFieldKnown = true;
        AtSummaryMap_ = true;
        if (LastKey_ == "sum") {
            CurrentSummary_.Sum_ = value;
        } else if (LastKey_ == "count") {
            CurrentSummary_.Count_ = value;
        } else if (LastKey_ == "min") {
            CurrentSummary_.Min_ = value;
        } else if (LastKey_ == "max") {
            CurrentSummary_.Max_ = value;
        } else {
            isFieldKnown = false;
        }

        if (isFieldKnown) {
            ++FilledSummaryFields_;
        }
    }

    virtual void OnUint64Scalar(ui64 /*value*/) override
    {
        THROW_ERROR_EXCEPTION("Uint64 scalars are not allowed for statistics");
    }

    virtual void OnDoubleScalar(double /*value*/) override
    {
        THROW_ERROR_EXCEPTION("Double scalars are not allowed for statistics");
    }

    virtual void OnBooleanScalar(bool /*value*/) override
    {
        THROW_ERROR_EXCEPTION("Boolean scalars are not allowed for statistics");
    }

    virtual void OnEntity() override
    {
        THROW_ERROR_EXCEPTION("Entities are not allowed for statistics");
    }

    virtual void OnBeginList() override
    {
        THROW_ERROR_EXCEPTION("Lists are not allowed for statistics");
    }

    virtual void OnListItem() override
    {
        THROW_ERROR_EXCEPTION("Lists are not allowed for statistics");
    }

    virtual void OnEndList() override
    {
        THROW_ERROR_EXCEPTION("Lists are not allowed for statistics");
    }

    virtual void OnBeginMap() override
    {
        // If we are here, we are either:
        // * at the root (then do nothing)
        // * at some directory (then the last key was the directory name)
        if (!LastKey_.empty()) {
            DirectoryNameLengths_.push_back(LastKey_.size());
            CurrentPath_.append('/');
            CurrentPath_.append(LastKey_);
            LastKey_.clear();
        } else {
            if (!CurrentPath_.empty()) {
                THROW_ERROR_EXCEPTION("Empty keys are not allowed for statistics");
            }
        }
    }

    virtual void OnKeyedItem(TStringBuf key) override
    {
        if (AtAttributes_) {
            if (key != "timestamp") {
                THROW_ERROR_EXCEPTION("Attributes other than \"timestamp\" are not allowed");
            }
        } else {
            LastKey_ = ToYPathLiteral(key);
        }
    }

    virtual void OnEndMap() override
    {
        if (AtSummaryMap_) {
            if (FilledSummaryFields_ != 4) {
                THROW_ERROR_EXCEPTION("All four summary fields should be filled for statistics");
            }
            Statistics_.Data_[CurrentPath_] = CurrentSummary_;
            FilledSummaryFields_ = 0;
            AtSummaryMap_ = false;
        }

        if (!CurrentPath_.empty()) {
            // We need to go to the parent.
            CurrentPath_.resize(CurrentPath_.size() - DirectoryNameLengths_.back() - 1);
            DirectoryNameLengths_.pop_back();
        }
    }

    virtual void OnBeginAttributes() override
    {
        if (!CurrentPath_.empty()) {
            THROW_ERROR_EXCEPTION("Attributes are not allowed for statistics");
        }
        AtAttributes_ = true;
    }

    virtual void OnEndAttributes() override
    {
        AtAttributes_ = false;
    }

    virtual TStatistics Finish() override
    {
        return Statistics_;
    }

private:
    TStatistics Statistics_;

    TString CurrentPath_;
    std::vector<int> DirectoryNameLengths_;

    TSummary CurrentSummary_;
    i64 FilledSummaryFields_ = 0;

    TString LastKey_;

    bool AtSummaryMap_ = false;
    bool AtAttributes_ = false;
};

void CreateBuildingYsonConsumer(std::unique_ptr<IBuildingYsonConsumer<TStatistics>>* buildingConsumer, EYsonType ysonType)
{
    YT_VERIFY(ysonType == EYsonType::Node);
    *buildingConsumer = std::make_unique<TStatisticsBuildingConsumer>();
}

////////////////////////////////////////////////////////////////////////////////

TStatisticsConsumer::TStatisticsConsumer(TSampleHandler sampleHandler)
    : TreeBuilder_(CreateBuilderFromFactory(GetEphemeralNodeFactory()))
    , SampleHandler_(sampleHandler)
{ }

void TStatisticsConsumer::OnMyListItem()
{
    TreeBuilder_->BeginTree();
    Forward(
        TreeBuilder_.get(),
        [this] {
            auto node = TreeBuilder_->EndTree();
            SampleHandler_.Run(node);
        },
        EYsonType::Node);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
