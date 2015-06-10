#pragma once

#include "public.h"

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

template <typename T, ui64 (*Hash)(T), int Precision>
class THyperLogLog
{
public:
    THyperLogLog();

    void Add(T value);

    void Merge(const THyperLogLog& that);

    ui64 EstimateCardinality();

private:
    static constexpr ui64 RegisterCount = (ui64)1 << Precision;
    static constexpr ui64 PrecisionMask = RegisterCount - 1;
    std::array<ui64, RegisterCount> ZeroCounts_;

    void AddHash(ui64 hash);
};

template <typename T, ui64 (*Hash)(T), int Precision>
ui64 EstimateCardinality(
    const std::vector<ui64>& values)
{
    auto state = THyperLogLog<T, Hash, Precision>();
    for (auto v : values) {
        state.Add(v);
    }
    return state.EstimateCardinality();
}

template <typename T, ui64 (*Hash)(T), int Precision>
THyperLogLog<T, Hash, Precision>::THyperLogLog()
{
    std::fill(ZeroCounts_.begin(), ZeroCounts_.end(), 0);
}

template <typename T, ui64 (*Hash)(T), int Precision>
void THyperLogLog<T, Hash, Precision>::Add(T value)
{
    auto hash = Hash(value);
    auto zeroes = 1;
    hash |= ((ui64)1 << 63);
    auto bit = RegisterCount;

    while ((bit & hash) == 0) {
        zeroes++;
        bit <<= 1;
    }

    auto index = hash & PrecisionMask;
    if (ZeroCounts_[index] < zeroes) {
        ZeroCounts_[index] = zeroes;
    }
}

template <typename T, ui64 (*Hash)(T), int Precision>
void THyperLogLog<T, Hash, Precision>::Merge(const THyperLogLog<T, Hash, Precision>& that)
{
    for (int i = 0; i < RegisterCount; i++) {
        auto thatCount = that.ZeroCounts_[i];
        if (ZeroCounts_[i] < thatCount) {
            ZeroCounts_[i] = thatCount;
        }
    }
}

template <typename T, ui64 (*Hash)(T), int Precision>
ui64 THyperLogLog<T, Hash, Precision>::EstimateCardinality()
{
    auto zeroRegisters = 0;
    double sum = 0;
    for (auto count : ZeroCounts_) {
        if (count == 0) {
            zeroRegisters++;
        } else {
            sum += 1.0 / ((ui64)1 << count);
        }
    }
    sum += zeroRegisters;

    double alpha = 0.7213 / (1 + 1.079 / RegisterCount);
    double m = RegisterCount;
    double raw = (1.0 / sum) * m * m * alpha;

    if (raw < 2.5 * m && zeroRegisters != 0) {
        return m * log(m / zeroRegisters);
    } else {
        return raw;
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
