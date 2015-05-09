#include "stdafx.h"
#include "packet.h"

namespace NYT {
namespace NBus {

////////////////////////////////////////////////////////////////////////////////

static const auto& Logger = BusLogger;

static const i64 PacketDecoderChunkSize = 16 * 1024;
struct TPacketDecoderTag { };

////////////////////////////////////////////////////////////////////////////////

TPacketDecoder::TPacketDecoder()
    : Allocator_(
        PacketDecoderChunkSize,
        TChunkedMemoryAllocator::DefaultMaxSmallBlockSizeRatio,
        GetRefCountedTypeCookie<TPacketDecoderTag>())
{
    Restart();
}

void TPacketDecoder::Restart()
{
    Phase_ = EPacketPhase::Header;
    PacketSize_ = 0;
    PartSizes_.clear();
    Parts_.clear();
    PartCount_ = 0;
    PartIndex_ = -1;
    Message_.Reset();

    BeginPhase(EPacketPhase::Header, &Header_, sizeof (TPacketHeader));
}

bool TPacketDecoder::Advance(size_t size)
{
    YASSERT(FragmentRemaining_ != 0);
    YASSERT(size <= FragmentRemaining_);

    FragmentRemaining_ -= size;
    FragmentPtr_ += size;
    if (FragmentRemaining_ == 0) {
        return EndPhase();
    } else {
        return true;
    }
}

EPacketType TPacketDecoder::GetPacketType() const
{
    return Header_.Type;
}

EPacketFlags TPacketDecoder::GetPacketFlags() const
{
    return EPacketFlags(Header_.Flags);
}

const TPacketId& TPacketDecoder::GetPacketId() const
{
    return Header_.PacketId;
}

TSharedRefArray TPacketDecoder::GetMessage() const
{
    return Message_;
}

size_t TPacketDecoder::GetPacketSize() const
{
    return PacketSize_;
}

bool TPacketDecoder::EndHeaderPhase()
{
    if (Header_.Signature != PacketSignature) {
        LOG_ERROR("Packet header signature mismatch: expected %X, actual %X",
            PacketSignature,
            Header_.Signature);
        return false;
    }

    switch (Header_.Type) {
        case EPacketType::Message:
            BeginPhase(EPacketPhase::PartCount, &PartCount_, sizeof (i32));
            return true;

        case EPacketType::Ack:
            SetFinished();
            return true;

        default:
            LOG_ERROR("Invalid packet type %v", Header_.Type);
            return false;
    }
}

bool TPacketDecoder::EndPartCountPhase()
{
    if (PartCount_ < 0 || PartCount_ > MaxPacketPartCount) {
        LOG_ERROR("Invalid part count %v", PartCount_);
        return false;
    }

    PartSizes_.resize(PartCount_);
    BeginPhase(EPacketPhase::PartSizes, PartSizes_.data(), PartCount_ * sizeof (i32));
    return true;
}

bool TPacketDecoder::EndPartSizesPhase()
{
    PacketSize_ =
        sizeof (TPacketHeader) + // header
        sizeof (i32) + // PartCount
        PartCount_ * sizeof (i32); // PartSizes

    for (int index = 0; index < PartCount_; ++index) {
        i32 partSize = PartSizes_[index];
        if (partSize < 0 || partSize > MaxPacketPartSize) {
            LOG_ERROR("Invalid size %v of part %v",
                partSize,
                index);
            return false;
        }
        PacketSize_ += partSize;
    }

    NextMessagePartPhase();
    return true;
}

bool TPacketDecoder::EndMessagePartPhase()
{
    NextMessagePartPhase();
    return true;
}

void TPacketDecoder::NextMessagePartPhase()
{
    YASSERT(PartIndex_ < PartCount_);

    while (true) {
        ++PartIndex_;
        if (PartIndex_ == PartCount_) {
            break;
        }

        size_t partSize = PartSizes_[PartIndex_];
        if (partSize != 0) {
            auto part = Allocator_.AllocateAligned(partSize);
            BeginPhase(EPacketPhase::MessagePart, part.Begin(), part.Size());            
            Parts_.push_back(std::move(part));
            return;
        }

        Parts_.push_back(TSharedRef());
    }

    Message_ = TSharedRefArray(std::move(Parts_));
    SetFinished();
}

////////////////////////////////////////////////////////////////////////////////

TPacketEncoder::TPacketEncoder()
{
    Phase_ = EPacketPhase::Unstarted;
    Header_.Signature = PacketSignature;
}

i64 TPacketEncoder::GetPacketSize(
    EPacketType type,
    const TSharedRefArray& message)
{
    i64 size = sizeof (TPacketHeader);
    switch (type) {
        case EPacketType::Ack:
            break;

        case EPacketType::Message: {
            size += sizeof (i32); // PartCount
            for (const auto& part : message) {
                size += sizeof (i32); // PartSize
                size += part.Size();
            }
            break;
        }

        default:
            YUNREACHABLE();
    }
    return size;
}

bool TPacketEncoder::Start(
    EPacketType type,
    EPacketFlags flags,
    const TPacketId& packetId,
    TSharedRefArray message)
{
    Header_.Type = type;
    Header_.Flags = flags;
    Header_.PacketId = packetId;

    PartSizes_.clear();
    PartCount_ = 0;
    PartIndex_ = -1;
    Message_ = message;

    if (type == EPacketType::Message) {
        PartCount_ = message.Size();

        if (PartCount_ > MaxPacketPartCount) {
            LOG_ERROR("Invalid part count %v", PartCount_);
            return false;
        }

        for (int index = 0; index < PartCount_; ++index) {
            const auto& part = message[index];
            int partSize = static_cast<int>(part.Size());
            if (partSize > MaxPacketPartSize) {
                LOG_ERROR("Invalid size %v of part %v",
                    partSize,
                    index);
                return false;
            }

            PartSizes_.push_back(partSize);
        }
    }

    BeginPhase(EPacketPhase::Header, &Header_, sizeof (TPacketHeader));
    return true;
}

bool TPacketEncoder::IsFragmentOwned() const
{
    return Phase_ == EPacketPhase::MessagePart;
}

void TPacketEncoder::NextFragment()
{
    EndPhase();
}

bool TPacketEncoder::EndHeaderPhase()
{
    switch (Header_.Type) {
        case EPacketType::Message:
            BeginPhase(EPacketPhase::PartCount, &PartCount_, sizeof (i32));
            return true;

        case EPacketType::Ack:
            SetFinished();
            return true;

        default:
            YUNREACHABLE();
    }
}

bool TPacketEncoder::EndPartCountPhase()
{
    BeginPhase(EPacketPhase::PartSizes, PartSizes_.data(), PartCount_ * sizeof (i32));
    return true;
}

bool TPacketEncoder::EndPartSizesPhase()
{
    NextMessagePartPhase();
    return true;
}

bool TPacketEncoder::EndMessagePartPhase()
{
    NextMessagePartPhase();
    return true;
}

void TPacketEncoder::NextMessagePartPhase()
{
    YASSERT(PartIndex_ < PartCount_);

    while (true) {
        ++PartIndex_;
        if (PartIndex_ == PartCount_) {
            break;
        }

        const auto& part = Message_[PartIndex_];
        if (part.Size() != 0) {
            BeginPhase(EPacketPhase::MessagePart, const_cast<char*>(part.Begin()), part.Size());
            return;
        }
    }

    SetFinished();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NBus
} // namespace NYT
