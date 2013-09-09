#include "stdafx.h"

#include <core/misc/common.h>
#include <core/actions/callback.h>
#include <core/actions/callback_internal.h>

#include <contrib/testing/framework.h>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

namespace {

// White-box testpoint.
struct TFakeInvoker
{
    typedef void(Signature)(NDetail::TBindStateBase*);
    static void Run(NDetail::TBindStateBase*)
    { }
};

} // namespace

////////////////////////////////////////////////////////////////////////////////

namespace NDetail {

template <class Runnable, class Signature, class BoundArgs>
class TBindState;

// White-box injection into a #TCallback<> object for checking
// comparators and emptiness APIs. Use a #TBindState<> that is specialized
// based on a type we declared in the anonymous namespace above to remove any
// chance of colliding with another instantiation and breaking the
// one-definition-rule.
template <>
class TBindState<void(), void(), void(TFakeInvoker)>
    : public TBindStateBase
{
public:
    typedef TFakeInvoker TInvokerType;
#ifdef ENABLE_BIND_LOCATION_TRACKING
    TBindState()
        : TBindStateBase(FROM_HERE)
    { }
#endif
};

template <>
class TBindState<void(), void(), void(TFakeInvoker, TFakeInvoker)>
    : public TBindStateBase
{
public:
    typedef TFakeInvoker TInvokerType;
#ifdef ENABLE_BIND_LOCATION_TRACKING
    TBindState()
        : TBindStateBase(FROM_HERE)
    { }
#endif
};

} // namespace NDetail

////////////////////////////////////////////////////////////////////////////////

// TODO(sandello): Implement accurate check on the number of Ref() and Unref()s.

namespace {

typedef NDetail::TBindState<void(), void(), void(TFakeInvoker)>
    TFakeBindState1;
typedef NDetail::TBindState<void(), void(), void(TFakeInvoker, TFakeInvoker)>
    TFakeBindState2;

class TCallbackTest
    : public ::testing::Test
{
public:
    TCallbackTest()
        : FirstCallback(New<TFakeBindState1>())
        , SecondCallback(New<TFakeBindState2>())
    { }

    virtual ~TCallbackTest()
    { }

protected:
    TCallback<void()> FirstCallback;
    const TCallback<void()> SecondCallback;

    TCallback<void()> NullCallback;
};

// Ensure we can create unbound callbacks. We need this to be able to store
// them in class members that can be initialized later.
TEST_F(TCallbackTest, DefaultConstruction)
{
    TCallback<void()> c0;

    TCallback<void(int)> c1;
    TCallback<void(int,int)> c2;
    TCallback<void(int,int,int)> c3;
    TCallback<void(int,int,int,int)> c4;
    TCallback<void(int,int,int,int,int)> c5;
    TCallback<void(int,int,int,int,int,int)> c6;

    EXPECT_FALSE(c0);
    EXPECT_FALSE(c1);
    EXPECT_FALSE(c2);
    EXPECT_FALSE(c3);
    EXPECT_FALSE(c4);
    EXPECT_FALSE(c5);
    EXPECT_FALSE(c6);
}

TEST_F(TCallbackTest, IsNull)
{
    EXPECT_FALSE(NullCallback);
    EXPECT_TRUE(FirstCallback);
    EXPECT_TRUE(SecondCallback);
}

TEST_F(TCallbackTest, Move)
{
    EXPECT_TRUE(FirstCallback);

    TCallback<void()> localCallback(std::move(FirstCallback));
    TCallback<void()> anotherCallback;

    EXPECT_FALSE(FirstCallback);
    EXPECT_TRUE(localCallback);
    EXPECT_FALSE(anotherCallback);

    anotherCallback = std::move(localCallback);

    EXPECT_FALSE(FirstCallback);
    EXPECT_FALSE(localCallback);
    EXPECT_TRUE(anotherCallback);
}

TEST_F(TCallbackTest, Equals)
{
    EXPECT_TRUE(FirstCallback.Equals(FirstCallback));
    EXPECT_FALSE(FirstCallback.Equals(SecondCallback));
    EXPECT_FALSE(SecondCallback.Equals(FirstCallback));

    // We should compare based on instance, not type.
    TCallback<void()> localCallback(New<TFakeBindState1>());
    TCallback<void()> anotherCallback = FirstCallback;

    EXPECT_TRUE(FirstCallback.Equals(anotherCallback));
    EXPECT_FALSE(FirstCallback.Equals(localCallback));

    // Empty, however, is always equal to empty.
    TCallback<void()> localNullCallback;
    EXPECT_TRUE(NullCallback.Equals(localNullCallback));
}

TEST_F(TCallbackTest, Reset)
{
    // Resetting should bring us back to empty.
    ASSERT_TRUE(FirstCallback);
    ASSERT_FALSE(FirstCallback.Equals(NullCallback));

    FirstCallback.Reset();

    EXPECT_FALSE(FirstCallback);
    EXPECT_TRUE(FirstCallback.Equals(NullCallback));
}

} // namespace

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
