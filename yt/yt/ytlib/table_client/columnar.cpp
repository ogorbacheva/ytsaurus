#include "columnar.h"

#include <yt/client/table_client/row_base.h>

#include <yt/core/misc/algorithm_helpers.h>

#include <util/system/cpu_id.h>

#include <immintrin.h>

namespace NYT::NTableClient {

////////////////////////////////////////////////////////////////////////////////

namespace {

ui64 SafeReadQword(const ui64* ptr, const char* end)
{
    ui64 qword = 0;
    ::memcpy(&qword, ptr, std::min<size_t>(sizeof(ui64), end - reinterpret_cast<const char*>(ptr)));
    return qword;
}

void SafeWriteQword(ui64* ptr, char* end, ui64 qword)
{
    ::memcpy(ptr, &qword, std::min<size_t>(sizeof(ui64), end - reinterpret_cast<char*>(ptr)));
}

template <bool Negate, class T>
auto MaybeNegateValue(T value)
{
    if constexpr(Negate) {
        value = ~value;
    }
    return value;
}

template <class T, bool Negate>
std::tuple<const T*, T*> MaybeNegaveAndCopyValues(
    const void* beginInput,
    const void* endInput,
    void* output)
{
    const auto* currentTypedInput = static_cast<const T*>(beginInput);
    const auto* endTypedInput = static_cast<const T*>(endInput);
    auto* currentTypedOutput = static_cast<T*>(output);
    while (currentTypedInput < endTypedInput) {
        *currentTypedOutput++ = MaybeNegateValue<Negate>(*currentTypedInput++);
    }
    return {currentTypedInput, currentTypedOutput};
}

template <bool Negate>
void CopyBitmapRangeToBitmapImpl(
    TRef bitmap,
    i64 startIndex,
    i64 endIndex,
    TMutableRef dst)
{
    YT_VERIFY(startIndex >= 0 && startIndex <= endIndex);
    YT_VERIFY(endIndex <= bitmap.Size() * 8);
    YT_VERIFY(endIndex - startIndex <= dst.Size() * 8);

    auto bitCount = endIndex - startIndex;
    auto byteCount = GetBitmapByteSize(bitCount);

    auto startQwordIndexRem = startIndex & 63;
    auto startQwordIndexQuot = startIndex >> 6;

    auto endQwordIndexQuot = endIndex >> 6;

    const auto* beginQwordInput = reinterpret_cast<const ui64*>(bitmap.Begin()) + startQwordIndexQuot;
    const auto* endQwordInput = beginQwordInput + endQwordIndexQuot - startQwordIndexQuot;
    const auto* currentQwordInput = beginQwordInput;
    auto* currentQwordOutput = reinterpret_cast<ui64*>(dst.Begin());

    auto qwordShift = startQwordIndexRem;
    auto qwordCoshift = 64 - startQwordIndexRem;

    if (qwordShift == 0) {
        const auto* beginByteInput = reinterpret_cast<const ui8*>(beginQwordInput);
        const auto* endByteInput = beginByteInput + byteCount;
        auto* beginByteOutput = reinterpret_cast<ui8*>(dst.Begin());
        if constexpr(Negate) {
            std::tie(currentQwordInput, currentQwordOutput) = MaybeNegaveAndCopyValues<ui64, Negate>(
                currentQwordInput,
                endQwordInput - 1,
                currentQwordOutput);
            MaybeNegaveAndCopyValues<ui8, Negate>(
                currentQwordInput,
                endByteInput,
                currentQwordOutput);
        } else {
            ::memcpy(beginByteOutput, beginByteInput, byteCount);
        }
        return;
    }

    // Head
    while (currentQwordInput < endQwordInput) {
        auto qword1 = currentQwordInput[0];
        auto qword2 = currentQwordInput[1];
        ++currentQwordInput;
        qword1 >>= qwordShift;
        qword2 &= (1ULL << qwordShift) - 1;
        qword2 <<= qwordCoshift;
        *currentQwordOutput++ = MaybeNegateValue<Negate>(qword1 | qword2);
    }

    // Tail
    {
        auto qword = currentQwordInput[0];
        qword >>= qwordShift;
        qword = MaybeNegateValue<Negate>(qword);
        SafeWriteQword(currentQwordOutput, dst.End(), qword);
    }
}

bool GetBit(TRef bitmap, i64 index)
{
    return (bitmap[index >> 3] & (1U << (index & 7))) != 0;
}

void SetBit(TMutableRef bitmap, i64 index, bool value)
{
    auto& byte = bitmap[index >> 3];
    auto mask = (1U << (index & 7));
    if (value) {
        byte |= mask;
    } else {
        byte &= ~mask;
    }
}

template <class F>
void BuildBitmapFromRleImpl(
    TRange<ui64> rleIndexes,
    i64 startIndex,
    i64 endIndex,
    F valueFetcher,
    TMutableRef dst)
{
    YT_VERIFY(startIndex >= 0 && startIndex <= endIndex);
    YT_VERIFY(dst.Size() * 8 >= endIndex - startIndex);
    YT_VERIFY(rleIndexes[0] == 0);

    auto startRleIndex = TranslateRleStartIndex(rleIndexes, startIndex);
    auto currentInputIndex = startRleIndex;
    auto currentIndex = startIndex;
    auto currentRleIndex = startRleIndex;
    bool currentBoolValue;
    i64 thresholdIndex = -1;
    i64 currentOutputIndex = 0;
    while (currentIndex < endIndex) {
        if (currentIndex >= thresholdIndex) {
            ++currentRleIndex;
            thresholdIndex = currentRleIndex < rleIndexes.Size()
                ? std::min(static_cast<i64>(rleIndexes[currentRleIndex]), endIndex)
                : endIndex;
            currentBoolValue = valueFetcher(currentInputIndex++);
        }
        if ((currentOutputIndex & 63) == 0 && currentIndex + 64 <= thresholdIndex) {
            auto* currentQwordOutput = reinterpret_cast<ui64*>(dst.Begin()) + (currentOutputIndex >> 6);
            auto currentQwordValue = currentBoolValue ? ~static_cast<ui64>(0) : 0;
            while (currentIndex + 64 <= thresholdIndex) {
                *currentQwordOutput++ = currentQwordValue;
                currentOutputIndex += 64;
                currentIndex += 64;
            }
        } else {
            SetBit(dst, currentOutputIndex++, currentBoolValue);
            ++currentIndex;
        }
    }
}

template <class F>
void BuildBytemapFromRleImpl(
    TRange<ui64> rleIndexes,
    i64 startIndex,
    i64 endIndex,
    F valueFetcher,
    TMutableRange<ui8> dst)
{
    YT_VERIFY(startIndex >= 0 && startIndex <= endIndex);
    YT_VERIFY(dst.Size() == endIndex - startIndex);
    YT_VERIFY(rleIndexes[0] == 0);

    auto startRleIndex = TranslateRleStartIndex(rleIndexes, startIndex);
    auto currentInputIndex = startRleIndex;
    auto currentIndex = startIndex;
    auto currentRleIndex = startRleIndex;
    bool currentBoolValue;
    i64 thresholdIndex = -1;
    i64 currentOutputIndex = 0;
    while (currentIndex < endIndex) {
        if (currentIndex >= thresholdIndex) {
            ++currentRleIndex;
            thresholdIndex = std::min(
                endIndex,
                currentRleIndex < rleIndexes.Size() ? static_cast<i64>(rleIndexes[currentRleIndex]) : Max<i64>());
            currentBoolValue = valueFetcher(currentInputIndex++);
        }
        if ((currentOutputIndex & 7) == 0 && currentIndex + 8 <= thresholdIndex) {
            auto* currentQwordOutput = reinterpret_cast<ui64*>(dst.Begin()) + (currentOutputIndex >> 3);
            auto currentQwordValue = currentBoolValue ? 0x0101010101010101ULL : 0ULL;
            while (currentIndex + 8 <= thresholdIndex) {
                *currentQwordOutput++ = currentQwordValue;
                currentOutputIndex += 8;
                currentIndex += 8;
            }
        } else {
            dst[currentOutputIndex++] = static_cast<ui8>(currentBoolValue);
            ++currentIndex;
        }
    }
}

#ifdef __clang__

__attribute__((target("bmi2")))
void DecodeBytemapFromBitmapImplBmi2(
    TRef bitmap,
    i64 startIndex,
    i64 endIndex,
    TMutableRange<ui8> dst)
{
    auto index = startIndex;
    while (index < endIndex) {
        if ((index & 7) == 0 && index + 8 <= endIndex) {
            const auto* currentInput = bitmap.Begin() + (index >> 3);
            const auto* endInput = bitmap.Begin() + (endIndex >> 3);
            auto* currentOutput = reinterpret_cast<ui64*>(dst.Begin() + (index - startIndex));
            while (currentInput < endInput) {
                // Cf. https://stackoverflow.com/questions/52098873/how-to-efficiently-convert-an-8-bit-bitmap-to-array-of-0-1-integers-with-x86-sim
                constexpr ui64 Mask = 0x0101010101010101;
                *currentOutput++ = __builtin_ia32_pdep_di(*currentInput++, Mask);
            }
            index = (endIndex & ~7);
        } else {
            dst[index - startIndex] = GetBit(bitmap, index);
            ++index;
        }
    }
}

#endif

void DecodeBytemapFromBitmapImplNoBmi2(
    TRef bitmap,
    i64 startIndex,
    i64 endIndex,
    TMutableRange<ui8> dst)
{
    for (auto index = startIndex; index < endIndex; ++index) {
        dst[index - startIndex] = GetBit(bitmap, index);
    }
}

} // namespace

void BuildValidityBitmapFromDictionaryIndexesWithZeroNull(
    TRange<ui32> dictionaryIndexes,
    TMutableRef dst)
{
    YT_VERIFY(dst.Size() >= GetBitmapByteSize(dictionaryIndexes.Size()));

    const auto* beginInput = dictionaryIndexes.Begin();
    const auto* endInput = dictionaryIndexes.End();
    const auto* endHeadInput = endInput - dictionaryIndexes.Size() % 8;
    const auto* currentInput = beginInput;
    auto* currentOutput = reinterpret_cast<ui8*>(dst.Begin());
    
    // Head
    while (currentInput < endHeadInput) {
        ui8 result = 0;
#define XX(shift) if (currentInput[shift] != 0) result |= (1U << shift);
        XX(0)
        XX(1)
        XX(2)
        XX(3)
        XX(4)
        XX(5)
        XX(6)
        XX(7)
#undef XX
        *currentOutput++ = result;
        currentInput += 8;
    }
    
    if (currentInput == endInput) {
        return;
    }

    // Tail
    {
        ui8 mask = 1;
        ui8 result = 0;
        while (currentInput < endInput) {
            if (*currentInput++ != 0) {
                result |= mask;
            }
            mask <<= 1;
        }
        *currentOutput++ = result;
    }
}

void BuildValidityBitmapFromRleDictionaryIndexesWithZeroNull(
    TRange<ui32> dictionaryIndexes,
    TRange<ui64> rleIndexes,
    i64 startIndex,
    i64 endIndex,
    TMutableRef dst)
{
    YT_VERIFY(rleIndexes.size() == dictionaryIndexes.size());

    BuildBitmapFromRleImpl(
        rleIndexes,
        startIndex,
        endIndex,
        [&] (i64 inputIndex) { return dictionaryIndexes[inputIndex] != 0; },
        dst);
}

void BuildNullBytemapFromDictionaryIndexesWithZeroNull(
    TRange<ui32> dictionaryIndexes,
    TMutableRange<ui8> dst)
{
    YT_VERIFY(dst.Size() == dictionaryIndexes.Size());

    const auto* beginInput = dictionaryIndexes.Begin();
    const auto* endInput = dictionaryIndexes.End();
    const auto* currentInput = beginInput;
    auto* currentOutput = dst.Begin();
    while (currentInput < endInput) {
        *currentOutput++ = static_cast<ui8>(*currentInput++ == 0);
    }
}

void BuildNullBytemapFromRleDictionaryIndexesWithZeroNull(
    TRange<ui32> dictionaryIndexes,
    TRange<ui64> rleIndexes,
    i64 startIndex,
    i64 endIndex,
    TMutableRange<ui8> dst)
{
    YT_VERIFY(rleIndexes.size() == dictionaryIndexes.size());

    BuildBytemapFromRleImpl(
        rleIndexes,
        startIndex,
        endIndex,
        [&] (i64 inputIndex) { return dictionaryIndexes[inputIndex] == 0; },
        dst);
}

void BuildDictionaryIndexesFromDictionaryIndexesWithZeroNull(
    TRange<ui32> dictionaryIndexes,
    TMutableRange<ui32> dst)
{
    YT_VERIFY(dst.Size() == dictionaryIndexes.Size());

    const auto* beginInput = dictionaryIndexes.Begin();
    const auto* endInput = dictionaryIndexes.End();
    const auto* currentInput = beginInput;
    auto* currentOutput = dst.Begin();
    while (currentInput < endInput) {
        // NB: null becomes FFFFFFFF.
        *currentOutput++ = (*currentInput++) - 1;
    }
}

void BuildDictionaryIndexesFromRleDictionaryIndexesWithZeroNull(
    TRange<ui32> dictionaryIndexes,
    TRange<ui64> rleIndexes,
    i64 startIndex,
    i64 endIndex,
    TMutableRange<ui32> dst)
{
    auto* currentOutput = dst.Begin();
    DecodeRawVector<ui32>(
        startIndex,
        endIndex,
        {},
        rleIndexes,
        [&] (auto index) {
            return dictionaryIndexes[index];
        },
        [&] (auto value) {
            *currentOutput++ = value - 1;
        });
    YT_VERIFY(currentOutput == dst.End());
}

void BuildIotaDictionaryIndexesFromRleIndexes(
    TRange<ui64> rleIndexes,
    i64 startIndex,
    i64 endIndex,
    TMutableRange<ui32> dst)
{
    YT_VERIFY(startIndex >= 0 && startIndex <= endIndex);
    YT_VERIFY(endIndex - startIndex == dst.Size());
    YT_VERIFY(rleIndexes[0] == 0);

    auto startRleIndex = TranslateRleStartIndex(rleIndexes, startIndex);
    auto* currentOutput = dst.Begin();
    auto currentIndex = startIndex;
    auto currentRleIndex = startRleIndex;
    auto currentValue = static_cast<ui32>(-1);
    i64 thresholdIndex = -1;
    while (currentIndex < endIndex) {
        if (currentIndex >= thresholdIndex) {
            ++currentRleIndex;
            thresholdIndex = currentRleIndex < rleIndexes.Size() ? static_cast<i64>(rleIndexes[currentRleIndex]) : Max<i64>();
            ++currentValue;
        }
        *currentOutput++ = currentValue;
        ++currentIndex;
    }
}

i64 CountNullsInDictionaryIndexesWithZeroNull(TRange<ui32> dictionaryIndexes)
{
    const auto* beginInput = dictionaryIndexes.Begin();
    const auto* endInput = dictionaryIndexes.End();
    const auto* currentInput = beginInput;
    i64 result = 0;
    while (currentInput < endInput) {
        if (*currentInput++ == 0) {
            ++result;
        }
    }
    return result;
}

i64 CountNullsInRleDictionaryIndexesWithZeroNull(
    TRange<ui32> dictionaryIndexes,
    TRange<ui64> rleIndexes,
    i64 startIndex,
    i64 endIndex)
{
    YT_VERIFY(startIndex >= 0 && startIndex <= endIndex);
    YT_VERIFY(rleIndexes[0] == 0);

    auto startRleIndex = TranslateRleStartIndex(rleIndexes, startIndex);
    const auto* currentInput = dictionaryIndexes.Begin() + startRleIndex;
    auto currentIndex = startIndex;
    auto currentRleIndex = startRleIndex;
    i64 result = 0;
    while (currentIndex < endIndex) {
        ++currentRleIndex;
        auto thresholdIndex = currentRleIndex < rleIndexes.Size() ? static_cast<i64>(rleIndexes[currentRleIndex]) : Max<i64>();
        auto currentValue = *currentInput++;
        auto newIndex = std::min(endIndex, thresholdIndex);
        if (currentValue == 0) {
            result += (newIndex - currentIndex);
        }
        currentIndex = newIndex;
    }
    return result;
}

i64 CountOnesInBitmap(TRef bitmap, i64 startIndex, i64 endIndex)
{
    YT_VERIFY(startIndex >= 0 && startIndex <= endIndex);
    YT_VERIFY(endIndex <= bitmap.Size() * 8);

    if (startIndex == endIndex) {
        return 0;
    }

    const auto* qwords = reinterpret_cast<const ui64*>(bitmap.Begin());

    auto startIndexRem = startIndex & 63;
    auto startIndexQuot = startIndex >> 6;

    auto endIndexRem = endIndex & 63;
    auto endIndexQuot = endIndex >> 6;

    // Tiny
    if (startIndexQuot == endIndexQuot) {
        auto qword = SafeReadQword(qwords + startIndexQuot, bitmap.End());
        qword &= (1ULL << endIndexRem) - 1;
        qword >>= startIndexRem;
        return __builtin_popcountll(qword);
    }

    i64 result = 0;

    // Head
    if (startIndexRem != 0) {
        auto qword = qwords[startIndexQuot];
        qword >>= startIndexRem;
        result += __builtin_popcountll(qword);
        ++startIndexQuot;
        startIndexRem = 0;
    }

    // Middle
    {
        const auto* currentQword = qwords + startIndexQuot;
        const auto* endQword = qwords + endIndexQuot;
        while (currentQword < endQword) {
            result += __builtin_popcountll(*currentQword++);
        }
    }

    // Tail
    if (endIndexRem != 0) {
        auto qword = SafeReadQword(qwords + endIndexQuot, bitmap.End());
        qword &= (1ULL << endIndexRem) - 1;
        result += __builtin_popcountll(qword);
    }

    return result;
}

i64 CountOnesInRleBitmap(
    TRef bitmap,
    TRange<ui64> rleIndexes,
    i64 startIndex,
    i64 endIndex)
{
    YT_VERIFY(startIndex >= 0 && startIndex <= endIndex);
    YT_VERIFY(rleIndexes[0] == 0);

    auto startRleIndex = TranslateRleStartIndex(rleIndexes, startIndex);
    auto currentInputIndex = startRleIndex;
    auto currentIndex = startIndex;
    auto currentRleIndex = startRleIndex;
    i64 result = 0;
    while (currentIndex < endIndex) {
        ++currentRleIndex;
        auto thresholdIndex = currentRleIndex < rleIndexes.Size() ? static_cast<i64>(rleIndexes[currentRleIndex]) : Max<i64>();
        auto currentValue = GetBit(bitmap, currentInputIndex++);
        auto newIndex = std::min(endIndex, thresholdIndex);
        if (currentValue) {
            result += (newIndex - currentIndex);
        }
        currentIndex = newIndex;
    }
    return result;
}

void CopyBitmapRangeToBitmap(
    TRef bitmap,
    i64 startIndex,
    i64 endIndex,
    TMutableRef dst)
{
    CopyBitmapRangeToBitmapImpl<false>(
        bitmap,
        startIndex,
        endIndex,
        dst);
}

void CopyBitmapRangeToBitmapNegated(
    TRef bitmap,
    i64 startIndex,
    i64 endIndex,
    TMutableRef dst)
{
    CopyBitmapRangeToBitmapImpl<true>(
        bitmap,
        startIndex,
        endIndex,
        dst);
}

void DecodeBytemapFromBitmap(
    TRef bitmap,
    i64 startIndex,
    i64 endIndex,
    TMutableRange<ui8> dst)
{
    YT_VERIFY(startIndex >= 0 && startIndex <= endIndex);
    YT_VERIFY(endIndex - startIndex == dst.Size());

#ifdef __clang__
    if (NX86::CachedHaveBMI2()) {
        DecodeBytemapFromBitmapImplBmi2(bitmap, startIndex, endIndex, dst);
        return;
    }
#endif
    
    DecodeBytemapFromBitmapImplNoBmi2(bitmap, startIndex, endIndex, dst);
}

void BuildValidityBitmapFromRleNullBitmap(
    TRef bitmap,
    TRange<ui64> rleIndexes,
    i64 startIndex,
    i64 endIndex,
    TMutableRef dst)
{
    BuildBitmapFromRleImpl(
        rleIndexes,
        startIndex,
        endIndex,
        [&] (i64 inputIndex) { return !GetBit(bitmap, inputIndex); },
        dst);
}

void BuildNullBytemapFromRleNullBitmap(
    TRef bitmap,
    TRange<ui64> rleIndexes,
    i64 startIndex,
    i64 endIndex,
    TMutableRange<ui8> dst)
{
    BuildBytemapFromRleImpl(
        rleIndexes,
        startIndex,
        endIndex,
        [&] (i64 inputIndex) { return GetBit(bitmap, inputIndex); },
        dst);
}

void DecodeStringOffsets(
    TRange<ui32> offsets,
    ui32 avgLength,
    i64 startIndex,
    i64 endIndex,
    TMutableRange<ui32> dst)
{
    YT_VERIFY(startIndex <= endIndex);
    YT_VERIFY(dst.Size() == endIndex - startIndex + 1);

    auto* currentOutput = reinterpret_cast<ui32*>(dst.Begin());

    auto startOffset = DecodeStringOffset(offsets, avgLength, startIndex);

    if (startIndex == 0) {
        // See DecodeStringOffset for a special handing of 0.
        *currentOutput++ = 0;
        ++startIndex;
    }

    // Mind offsets[index - 1] in DecodeStringOffset.
    const auto* currentInput = offsets.Begin() + startIndex - 1;
    // No -1 here; will output endIndex - startIndex + 1 offsets.
    const auto* endInput = offsets.Begin() + endIndex;
    // See DecodeStringOffset.
    auto avgLengthTimesIndex = startIndex * avgLength;
    while (currentInput < endInput) {
        *currentOutput++ = avgLengthTimesIndex + ZigZagDecode64(*currentInput++) - startOffset;
        avgLengthTimesIndex += avgLength;
    }
}

void DecodeStringPointersAndLengths(
    TRange<ui32> offsets,
    ui32 avgLength,
    TRef stringData,
    TMutableRange<const char*> strings,
    TMutableRange<i32> lengths)
{
    YT_VERIFY(offsets.Size() == strings.Size());
    YT_VERIFY(offsets.Size() == lengths.Size());

    i64 startOffset = 0;
    i64 avgLengthTimesIndex = 0;
    for (size_t index = 0; index < offsets.size(); ++index) {
        strings[index] = stringData.Begin() + startOffset;
        avgLengthTimesIndex += avgLength;
        i64 endOffset = avgLengthTimesIndex + ZigZagDecode64(offsets[index]);
        i32 length = endOffset - startOffset;
        lengths[index] = length;
        startOffset = endOffset;
    }
}


i64 CountTotalStringLengthInRleDictionaryIndexesWithZeroNull(
    TRange<ui32> dictionaryIndexes,
    TRange<ui64> rleIndexes,
    TRange<i32> stringLengths,
    i64 startIndex,
    i64 endIndex)
{
    YT_VERIFY(startIndex >= 0 && startIndex <= endIndex);
    YT_VERIFY(rleIndexes[0] == 0);

    auto startRleIndex = TranslateRleStartIndex(rleIndexes, startIndex);
    const auto* currentInput = dictionaryIndexes.Begin() + startRleIndex;
    auto currentIndex = startIndex;
    auto currentRleIndex = startRleIndex;
    i64 result = 0;
    while (currentIndex < endIndex) {
        ++currentRleIndex;
        auto thresholdIndex = currentRleIndex < static_cast<i64>(rleIndexes.Size()) ? static_cast<i64>(rleIndexes[currentRleIndex]) : Max<i64>();
        auto currentDictionaryIndex = *currentInput++;
        auto newIndex = std::min(endIndex, thresholdIndex);
        if (currentDictionaryIndex != 0) {
            result += (newIndex - currentIndex) * stringLengths[currentDictionaryIndex - 1];
        }
        currentIndex = newIndex;
    }
    return result;
}

i64 TranslateRleIndex(
    TRange<ui64> rleIndexes,
    i64 index)
{
    YT_VERIFY(index >= 0);
    YT_VERIFY(rleIndexes[0] == 0);

    return BinarySearch(
        static_cast<i64>(0),
        static_cast<i64>(rleIndexes.size()),
        [&] (i64 k) {
            return rleIndexes[k] <= index;
        }) - 1;
}

i64 TranslateRleStartIndex(
    TRange<ui64> rleIndexes,
    i64 index)
{
    return TranslateRleIndex(rleIndexes, index);
}

i64 TranslateRleEndIndex(
    TRange<ui64> rleIndexes,
    i64 index)
{
    YT_VERIFY(index >= 0);
    if (index == 0) {
        return 0;
    }
    return TranslateRleIndex(rleIndexes, index - 1) + 1;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTableClient
