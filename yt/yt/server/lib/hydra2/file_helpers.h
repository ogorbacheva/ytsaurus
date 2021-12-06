#pragma once

#include "public.h"

#include <util/stream/file.h>

#include <util/system/file.h>

namespace NYT::NHydra2 {

////////////////////////////////////////////////////////////////////////////////

//! Wrapper on arcadia TFile with appropriate interface
class TFileWrapper
{
public:
    TFileWrapper(const TString& fileName, EOpenMode oMode);

    i64 Seek(i64 offset, SeekDir origin);
    void Flush();
    void FlushData();
    void Write(const void* buffer, size_t length);
    size_t Pread(void* buffer, size_t length, i64 offset);
    size_t Load(void* buffer, size_t length);
    void Skip(size_t length);
    size_t GetPosition();
    size_t GetLength();
    void Resize(size_t length);
    void Close();
    void Flock(int op);

private:
    TFile File_;
};

////////////////////////////////////////////////////////////////////////////////

//! Wraps TFile-like instance and checks that all read attempts
//! fall within file boundaries.
template <class T>
class TCheckedReader
{
public:
    explicit TCheckedReader(T& underlying);

    size_t Load(void* buffer, size_t length);
    void Skip(size_t length);
    size_t Avail() const;
    bool Success() const;

private:
    T& Underlying_;
    i64 CurrentOffset_;
    i64 FileLength_;
    bool Success_;

    bool Check(size_t length);

};

////////////////////////////////////////////////////////////////////////////////

//! Wraps another IOutputStream and measures the number of bytes
//! written through it.
class TLengthMeasureOutputStream
    : public IOutputStream
{
public:
    explicit TLengthMeasureOutputStream(IOutputStream* output);

    i64 GetLength() const;

protected:
    void DoWrite(const void* buf, size_t len) override;
    void DoFlush() override;
    void DoFinish() override;

private:
    IOutputStream* Output;
    i64 Length;

};

////////////////////////////////////////////////////////////////////////////////

void RemoveChangelogFiles(const TString& path);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NHydra2

#define FILE_HELPERS_INL_H_
#include "file_helpers-inl.h"
#undef FILE_HELPERS_INL_H_
