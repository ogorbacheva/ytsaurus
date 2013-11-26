#pragma once

#include "public.h"
#include "fiber.h"

#include <core/misc/nullable.h>

namespace NYT {
namespace NConcurrency {

////////////////////////////////////////////////////////////////////////////////

template <class Signature>
class TCoroutine;

class TCoroutineBase
{
protected:
    TCoroutineBase();
    TCoroutineBase(TCoroutineBase&&);

    virtual ~TCoroutineBase();

    virtual void Trampoline() = 0;

public:
    EFiberState GetState() const;

protected:
    TFiberPtr Fiber;

private:
    TCoroutineBase(const TCoroutineBase&);
    TCoroutineBase& operator=(const TCoroutineBase&);

};

namespace NDetail {

////////////////////////////////////////////////////////////////////////////////

template<unsigned...>
struct TSequence { };

template<unsigned N, unsigned... Indexes>
struct TGenerateSequence : TGenerateSequence<N - 1, N - 1, Indexes...> { };

template<unsigned... Indexes>
struct TGenerateSequence<0, Indexes...>
{
    typedef TSequence<Indexes...> TType;
};

template<class TCallee, class TCaller, class TArguments, unsigned... Indexes>
void Invoke(
    TCallee&& Callee,
    TCaller&& Caller,
    TArguments&& Arguments,
    TSequence<Indexes...>)
{
    Callee.Run(
        std::forward<TCaller>(Caller),
        std::get<Indexes>(std::forward<TArguments>(Arguments))...);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NDetail

template <class R, class... TArgs>
class TCoroutine<R(TArgs...)>
    : public TCoroutineBase
{
public:
    typedef R (FunctionalSignature)(TArgs...);
    typedef void (CoroutineSignature)(TCoroutine&, TArgs...);

    typedef TCallback<CoroutineSignature> TCallee;
    typedef std::tuple<TArgs...> TArguments;

    TCoroutine()
        : TCoroutineBase()
    { }

    TCoroutine(TCoroutine&& other)
        : TCoroutineBase(std::move(other))
        , Callee(std::move(other.Callee))
        , Arguments(std::move(other.Arguments))
        , Result(std::move(other.Result))
    { }

    TCoroutine(TCallee&& callee)
        : TCoroutineBase()
        , Callee(std::move(callee))
    { }

    void Reset(TCallee callee)
    {
        Fiber->Reset();
        Callee = std::move(callee);
    }

    template <class... TParams>
    const TNullable<R>& Run(TParams&&... params)
    {
        static_assert(
            sizeof...(TParams) == sizeof...(TArgs),
            "Parameters and arguments counts do not match.");
        Arguments = std::forward_as_tuple(std::forward<TParams>(params)...);
        Fiber->Run();
        return Result;
    }

    template <class Q>
    TArguments&& Yield(Q&& result)
    {
        Result = std::forward<Q>(result);
        Fiber->Yield();
        return std::move(Arguments);
    }

private:
    virtual void Trampoline() override
    {
        try {
            NDetail::Invoke(
                Callee,
                *this,
                std::move(Arguments),
                typename NDetail::TGenerateSequence<sizeof...(TArgs)>::TType());
            Result.Reset();
        } catch (...) {
            Result.Reset();
            throw;
        }
    }

private:
    TCallee Callee;
    TArguments Arguments;
    TNullable<R> Result;

};

template <class... TArgs>
class TCoroutine<void(TArgs...)>
    : public TCoroutineBase
{
public:
    typedef void (FunctionalSignature)(TArgs...);
    typedef void (CoroutineSignature)(TCoroutine&, TArgs...);

    typedef TCallback<CoroutineSignature> TCallee;
    typedef std::tuple<TArgs...> TArguments;

    TCoroutine()
        : TCoroutineBase()
    { }

    TCoroutine(TCoroutine&& other)
        : TCoroutineBase(std::move(other))
        , Arguments(std::move(other.Arguments))
        , Result(other.Result)
    {
        other.Result = false;
    }

    TCoroutine(TCallee&& callee)
        : TCoroutineBase()
        , Callee(std::move(callee))
    { }

    void Reset(TCallee callee)
    {
        Fiber->Reset();
        Callee = std::move(callee);
    }

    template <class... TParams>
    bool Run(TParams&&... params)
    {
        static_assert(
            sizeof...(TParams) == sizeof...(TArgs),
            "Parameters and arguments counts do not match.");
        Arguments = std::forward_as_tuple(std::forward<TParams>(params)...);
        Fiber->Run();
        return Result;
    }

    TArguments&& Yield()
    {
        Result = true;
        Fiber->Yield();
        return std::move(Arguments);
    }

private:
    virtual void Trampoline() override
    {
        try {
            NDetail::Invoke(
                Callee,
                *this,
                std::move(Arguments),
                typename NDetail::TGenerateSequence<sizeof...(TArgs)>::TType());
            Result = false;
        } catch (const std::exception& ex) {
            Result = false;
            throw;
        }
    }

private:
    TCallee Callee;
    TArguments Arguments;
    bool Result;

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NConcurrency
} // namespace NYT
