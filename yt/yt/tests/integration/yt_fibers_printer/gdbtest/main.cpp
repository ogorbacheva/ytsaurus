#include "foobar.h"

#include <yt/yt/core/tracing/trace_context.h>

void StopHere() {
    volatile int dummy;
    dummy = 0;
}

void AsyncStop(NYT::TIntrusivePtr<NYT::NConcurrency::TThreadPool>& threadPool) {
    auto future = BIND([&]() {
        auto traceContext = NYT::NTracing::GetCurrentTraceContext();
        traceContext->AddTag("tag0", "value0");
        StopHere();
    }).AsyncVia(threadPool->GetInvoker()).Run();
    Y_UNUSED(NYT::NConcurrency::WaitFor(future));
}

int main() {
    auto traceContext = NYT::NTracing::CreateTraceContextFromCurrent("Test");
    traceContext->SetRecorded();
    traceContext->SetSampled();
    traceContext->AddTag("tag", "value");
    traceContext->SetLoggingTag("LoggingTag");
    NYT::NTracing::TTraceContextGuard guard(traceContext);
    auto threadPool = NYT::New<NYT::NConcurrency::TThreadPool>(1, "test");
    auto future = BIND([&]() {
        Foo(threadPool, 10);
    }).AsyncVia(threadPool->GetInvoker()).Run();
    Y_UNUSED(NYT::NConcurrency::WaitFor(future));
    return 0;
}
