﻿#include "stdafx.h"
#include "async_stream_state.h"

#include <util/system/guard.h>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

TAsyncStreamState::TAsyncStreamState()
    : IsOperationFinished(true)
    , IsActive_(true)
    , StaticError(New<TAsyncError>(TError()))
    , CurrentError(NULL)
{ }

void TAsyncStreamState::Cancel(const TError& error)
{
    TGuard<TSpinLock> guard(SpinLock);

    if (!IsActive_) {
        return;
    }

    DoFail(error);
}

void TAsyncStreamState::Fail(const TError& error)
{
    TGuard<TSpinLock> guard(SpinLock);
    if (!IsActive_) {
        YASSERT(!StaticError->Get().IsOK());
        return;
    }

    DoFail(error);
}

void TAsyncStreamState::DoFail(const TError& error)
{
    YASSERT(!error.IsOK());
    IsActive_ = false;
    if (CurrentError) {
        StaticError = CurrentError;
        CurrentError.Reset();
    } else {
        StaticError = New<TAsyncError>();
    }
    StaticError->Set(error);
}

void TAsyncStreamState::Close()
{
    TGuard<TSpinLock> guard(SpinLock);
    YASSERT(IsActive_);

    IsActive_ = false;
    if (CurrentError) {
        auto result = CurrentError;
        CurrentError.Reset();

        result->Set(TError());
    }
}

bool TAsyncStreamState::IsActive() const
{
    TGuard<TSpinLock> guard(SpinLock);
    return IsActive_;
}

bool TAsyncStreamState::IsClosed() const
{
    TGuard<TSpinLock> guard(SpinLock);
    return !IsActive_ && StaticError->Get().IsOK();
}

bool TAsyncStreamState::HasRunningOperation() const
{
    TGuard<TSpinLock> guard(SpinLock);
    return !IsOperationFinished;
}

void TAsyncStreamState::Finish(const TError& error)
{
    if (error.IsOK()) {
        Close();
    } else {
        Fail(error);
    }
}

TError TAsyncStreamState::GetCurrentError()
{
    return StaticError->Get();
}

void TAsyncStreamState::StartOperation()
{
    TGuard<TSpinLock> guard(SpinLock);
    YASSERT(IsOperationFinished);
    IsOperationFinished = false;
}

TAsyncError::TPtr TAsyncStreamState::GetOperationError()
{
    TGuard<TSpinLock> guard(SpinLock);
    if (IsOperationFinished || !IsActive_) {
        return StaticError;
    } else {
        YASSERT(!CurrentError);
        CurrentError = New<TAsyncError>();
        return CurrentError;
    }
}

void TAsyncStreamState::FinishOperation(const TError& error)
{
    TGuard<TSpinLock> guard(SpinLock);
    YASSERT(!IsOperationFinished);
    IsOperationFinished = true;
    if (error.IsOK()) {
        if (IsActive_ && CurrentError) {
            auto currentError = CurrentError;
            CurrentError.Reset();
            // Always release guard before setting future with 
            // unknown subscribers.
            guard.Release();

            currentError->Set(TError());
        }
    } else {
        DoFail(error);
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
