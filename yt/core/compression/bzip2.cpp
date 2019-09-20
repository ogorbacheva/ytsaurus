#include "bzip2.h"

#include <yt/core/misc/finally.h>

#include <contrib/libs/libbz2/bzlib.h>

#include <util/generic/utility.h>

namespace NYT::NCompression {

namespace {

////////////////////////////////////////////////////////////////////////////////

constexpr size_t MinBlobSize = 1024;

////////////////////////////////////////////////////////////////////////////////

ui64 GetTotalOut(const bz_stream& bzStream)
{
    ui64 result = bzStream.total_out_hi32;
    result <<= 32;
    result |= bzStream.total_out_lo32;
    return result;
}

void PeekInputBytes(StreamSource* source, bz_stream* bzStream)
{
    size_t bufLen;
    const char* buf = source->Peek(&bufLen);
    bufLen = std::min(source->Available(), bufLen);
    // const_cast is due to C Api, data will not be modified.
    bzStream->next_in = const_cast<char*>(buf);
    bzStream->avail_in = bufLen;
}

void DirectOutputToBlobEnd(TBlob* blob, bz_stream* bzStream)
{
    if (blob->Size() == blob->Capacity()) {
        YT_VERIFY(blob->Capacity() >= MinBlobSize);

        constexpr double growFactor = 1.5;
        blob->Reserve(growFactor * blob->Capacity());
    }
    bzStream->next_out = blob->End();
    bzStream->avail_out = blob->Capacity() - blob->Size();
}

void ActualizeOutputBlobSize(TBlob* blob, bz_stream* bzStream)
{
    ui64 totalOut = GetTotalOut(*bzStream);
    YT_VERIFY(totalOut >= blob->Size());
    blob->Resize(totalOut, false);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace

////////////////////////////////////////////////////////////////////////////////

void Bzip2Compress(int level, StreamSource* source, TBlob* output)
{
    YT_VERIFY(source);
    YT_VERIFY(output);
    YT_VERIFY(1 <= level && level <= 9);

    bz_stream bzStream;
    Zero(bzStream);
    int ret = BZ2_bzCompressInit(&bzStream, level, 0, 0);
    YT_VERIFY(ret == BZ_OK);
    auto finally = Finally([&] { BZ2_bzCompressEnd(&bzStream); });

    output->Reserve(std::max(
        MinBlobSize,
        source->Available() / 8)); // just a heuristic
    output->Resize(0);
    while (source->Available()) {
        PeekInputBytes(source, &bzStream);
        const size_t peekedSize = bzStream.avail_in;

        DirectOutputToBlobEnd(output, &bzStream);

        ret = BZ2_bzCompress(&bzStream, BZ_RUN);
        YT_VERIFY(ret == BZ_RUN_OK);

        ActualizeOutputBlobSize(output, &bzStream);

        const size_t processedInputSize = peekedSize - bzStream.avail_in;
        source->Skip(processedInputSize);
    }

    do {
        DirectOutputToBlobEnd(output, &bzStream);

        ret = BZ2_bzCompress(&bzStream, BZ_FINISH);
        YT_VERIFY(ret == BZ_FINISH_OK || ret == BZ_STREAM_END);

        ActualizeOutputBlobSize(output, &bzStream);
    } while (ret != BZ_STREAM_END);
}

void Bzip2Decompress(StreamSource* source, TBlob* output)
{
    output->Reserve(std::max(MinBlobSize, source->Available()));
    output->Resize(0);
    while (source->Available()) {
        bz_stream bzStream;
        Zero(bzStream);

        int ret = BZ2_bzDecompressInit(&bzStream, 0, 0);
        YT_VERIFY(ret == BZ_OK);
        auto finally = Finally([&] { BZ2_bzDecompressEnd(&bzStream); });

        do {
            PeekInputBytes(source, &bzStream);
            const size_t peekedSize = bzStream.avail_in;

            DirectOutputToBlobEnd(output, &bzStream);

            ret = BZ2_bzDecompress(&bzStream);
            YT_VERIFY(ret == BZ_OK || ret == BZ_STREAM_END);

            ActualizeOutputBlobSize(output, &bzStream);

            const size_t processedInputSize = peekedSize - bzStream.avail_in;
            source->Skip(processedInputSize);
        } while (ret != BZ_STREAM_END);
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NCompression
