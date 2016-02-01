#include "utf8_decoder.h"

#include <yt/core/misc/error.h>

namespace NYT {
namespace NFormats {

////////////////////////////////////////////////////////////////////////////////

TUtf8Transcoder::TUtf8Transcoder(bool enableEscaping)
    : EnableEncoding_(enableEscaping)
{ }
    
TStringBuf TUtf8Transcoder::Encode(const TStringBuf& str)
{
    if (!EnableEncoding_) {
        return str;
    }

    Buffer_.clear();

    bool isAscii = true;
    for (int i = 0; i < str.size(); ++i) {
        if (ui8(str[i]) < 128) {
            if (!isAscii) {
                Buffer_.push_back(str[i]);
            }
        } else {
            if (isAscii) {
                Buffer_.resize(i);
                std::copy(str.data(), str.data() + i, Buffer_.data());
                isAscii = false;
            }
            Buffer_.push_back('\xC0' | (ui8(str[i]) >> 6));
            Buffer_.push_back('\x80' | (ui8(str[i]) & ~'\xC0'));
        }
    }

    if (isAscii) {
        return str;
    } else {
        return TStringBuf(Buffer_.data(), Buffer_.size());
    }
}

TStringBuf TUtf8Transcoder::Decode(const TStringBuf& str)
{
    if (!EnableEncoding_) {
        return str;
    }

    Buffer_.clear();

    bool isAscii = true;
    for (int i = 0; i < str.size(); ++i) {
        if (ui8(str[i]) < 128) {
            if (!isAscii) {
                Buffer_.push_back(str[i]);
            }
        } else if ((str[i] & '\xFC') == '\xC0') {
            if (isAscii) {
                Buffer_.resize(i);
                std::copy(str.data(), str.data() + i, Buffer_.data());
                isAscii = false;
            }
            Buffer_.push_back(((str[i] & '\x03') << 6) | (str[i + 1] & '\x3F'));
            i += 1;
        } else {
            THROW_ERROR_EXCEPTION("Unicode symbols with codes greater than 255 are not supported. "
                 "Please refer to https://wiki.yandex-team.ru/yt/userdoc/formats/#json and "
                 "consider using encode_utf8=false in format options");
        }
    }

    if (isAscii) {
        return str;
    } else {
        return TStringBuf(Buffer_.data(), Buffer_.size());
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NFormats
} // namespace NYT
