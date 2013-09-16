#include "public.h"

#include <core/concurrency/async_stream.h>

#include <ytlib/driver/driver.h>

#include <contrib/libs/pycxx/Extensions.hxx>

namespace NYT {
namespace NPython {

///////////////////////////////////////////////////////////////////////////////

class TBufferedStream
    : public NConcurrency::IAsyncOutputStream
{
public:
    explicit TBufferedStream(i64 bufferSize);

    TSharedRef Read(i64 size = 0);

    bool Empty() const;

    void Finish(NDriver::TDriverResponse);

    virtual bool Write(const void* buf, size_t len) override;

    virtual TAsyncError GetReadyEvent() override;

private:
    i64 Size_;
    i64 AllowedSize_;

    TSharedRef Data_;
    char* Begin_;
    char* End_;

    DECLARE_ENUM(EState,
        (Normal)
        (Full)
        (WaitingData)
        (Finished)
    );

    EState State_;

    TAsyncErrorPromise AllowWrite_;
    TPromise<void> AllowRead_;

    TMutex Mutex_;

    void Reallocate(size_t len);
    void Move(char* dest);
    TSharedRef ExtractChunk(i64 size);
};

///////////////////////////////////////////////////////////////////////////////

class TBufferedStreamWrap
    : public Py::PythonClass<TBufferedStreamWrap>
{
public:
    TBufferedStreamWrap(Py::PythonClassInstance* self, Py::Tuple& args, Py::Dict& kwds);

    Py::Object Read(Py::Tuple& args, Py::Dict &kwds);
    PYCXX_KEYWORDS_METHOD_DECL(TBufferedStreamWrap, Read);

    Py::Object Empty(Py::Tuple& args, Py::Dict &kwds);
    PYCXX_KEYWORDS_METHOD_DECL(TBufferedStreamWrap, Empty);

    TBufferedStreamPtr GetStream();

    virtual ~TBufferedStreamWrap();

    static void InitType();

private:
    TBufferedStreamPtr Stream_;
};

///////////////////////////////////////////////////////////////////////////////

} // namespace NPython
} // namespace NYT
