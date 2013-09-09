#include "stdafx.h"

#include <core/concurrency/thread_affinity.h>
#include <core/concurrency/action_queue.h>

#include <core/actions/bind.h>
#include <core/actions/future.h>

#include <contrib/testing/framework.h>

namespace NYT {

using namespace NConcurrency;

////////////////////////////////////////////////////////////////////////////////

namespace {

class TMyObject
{
    DECLARE_THREAD_AFFINITY_SLOT(FirstThread);
    DECLARE_THREAD_AFFINITY_SLOT(SecondThread);

public:
    void A()
    {
        VERIFY_THREAD_AFFINITY(FirstThread);
    }

    void B()
    {
        VERIFY_THREAD_AFFINITY(SecondThread);
    }

    void C()
    {
        VERIFY_THREAD_AFFINITY(FirstThread);
    }
};

#define PROLOGUE() \
    auto queue1 = New<TActionQueue>(); \
    auto queue2 = New<TActionQueue>(); \
    auto invoker1 = queue1->GetInvoker(); \
    auto invoker2 = queue2->GetInvoker(); \

void SingleThreadedAccess(TMyObject* object)
{
    PROLOGUE();

    BIND(&TMyObject::A, object).AsyncVia(invoker1).Run().Get();
    BIND(&TMyObject::B, object).AsyncVia(invoker1).Run().Get();

    BIND(&TMyObject::A, object).AsyncVia(invoker1).Run().Get();
    BIND(&TMyObject::B, object).AsyncVia(invoker1).Run().Get();
}

void UntangledThreadAccess(TMyObject* object)
{
    PROLOGUE();

    BIND(&TMyObject::A, object).AsyncVia(invoker1).Run().Get();
    BIND(&TMyObject::B, object).AsyncVia(invoker2).Run().Get();

    BIND(&TMyObject::A, object).AsyncVia(invoker1).Run().Get();
    BIND(&TMyObject::B, object).AsyncVia(invoker2).Run().Get();
}

void UntangledThreadAccessToSharedSlot(TMyObject* object)
{
    PROLOGUE();

    BIND(&TMyObject::A, object).AsyncVia(invoker1).Run().Get();
    BIND(&TMyObject::B, object).AsyncVia(invoker2).Run().Get();
    BIND(&TMyObject::C, object).AsyncVia(invoker1).Run().Get();

    BIND(&TMyObject::A, object).AsyncVia(invoker1).Run().Get();
    BIND(&TMyObject::B, object).AsyncVia(invoker2).Run().Get();
    BIND(&TMyObject::C, object).AsyncVia(invoker1).Run().Get();
}

void TangledThreadAccess1(TMyObject* object)
{
    PROLOGUE();

    BIND(&TMyObject::A, object).AsyncVia(invoker1).Run().Get();
    BIND(&TMyObject::B, object).AsyncVia(invoker2).Run().Get();

    BIND(&TMyObject::A, object).AsyncVia(invoker1).Run().Get();
    BIND(&TMyObject::B, object).AsyncVia(invoker1).Run().Get();
}

void TangledThreadAccess2(TMyObject* object)
{
    PROLOGUE();

    BIND(&TMyObject::A, object).AsyncVia(invoker1).Run().Get();
    BIND(&TMyObject::B, object).AsyncVia(invoker2).Run().Get();

    BIND(&TMyObject::A, object).AsyncVia(invoker2).Run().Get();
    BIND(&TMyObject::B, object).AsyncVia(invoker2).Run().Get();
}

#undef PROLOGUE

} // namespace

////////////////////////////////////////////////////////////////////////////////

TEST(TThreadAffinityTest, SingleThreadedAccess)
{
    TMyObject object;
    SingleThreadedAccess(&object);

    SUCCEED();
}

TEST(TThreadAffinityTest, UntangledThreadAccess)
{
    TMyObject object;
    UntangledThreadAccess(&object);

    SUCCEED();
}

TEST(TThreadAffinityTest, UntangledThreadAccessToSharedSlot)
{
    TMyObject object;
    UntangledThreadAccessToSharedSlot(&object);

    SUCCEED();
}

#ifndef NDEBUG

TEST(TThreadAffinityDeathTest, TangledThreadAccess1)
{
    TMyObject object;
    ASSERT_DEATH({ TangledThreadAccess1(&object); }, ".*");
}

TEST(TThreadAffinityDeathTest, TangledThreadAccess2)
{
    TMyObject object;
    ASSERT_DEATH({ TangledThreadAccess2(&object); }, ".*");
}

#endif

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT

