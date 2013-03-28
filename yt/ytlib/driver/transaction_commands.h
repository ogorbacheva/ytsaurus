#pragma once

#include "command.h"

namespace NYT {
namespace NDriver {

////////////////////////////////////////////////////////////////////////////////

struct TStartTransactionRequest
    : public TTransactedRequest
{
    TNullable<TDuration> Timeout;
    NYTree::INodePtr Attributes;

    TStartTransactionRequest()
    {
        Register("timeout", Timeout)
            .Default(Null);
        Register("attributes", Attributes)
            .Default(nullptr);
    }
};

typedef TIntrusivePtr<TStartTransactionRequest> TStartRequestPtr;

class TStartTransactionCommand
    : public TTransactedCommandBase<TStartTransactionRequest>
{
public:
    explicit TStartTransactionCommand(ICommandContext* host)
        : TTransactedCommandBase(host)
    { }

private:
    virtual void DoExecute();
};

////////////////////////////////////////////////////////////////////////////////

struct TPingTransactionRequest
    : public TTransactedRequest
{ };

typedef TIntrusivePtr<TPingTransactionRequest> TRenewRequestPtr;

class TPingTransactionCommand
    : public TTransactedCommandBase<TPingTransactionRequest>
{
public:
    explicit TPingTransactionCommand(ICommandContext* host)
        : TTransactedCommandBase(host)
    { }

private:
    virtual void DoExecute();
};

////////////////////////////////////////////////////////////////////////////////

struct TCommitTransactionRequest
    : public TTransactedRequest
{ };

typedef TIntrusivePtr<TCommitTransactionRequest> TCommitRequestPtr;

class TCommitTransactionCommand
    : public TTransactedCommandBase<TCommitTransactionRequest>
{
public:
    explicit TCommitTransactionCommand(ICommandContext* host)
        : TTransactedCommandBase(host)
    { }

private:
    virtual void DoExecute();
};

////////////////////////////////////////////////////////////////////////////////

struct TAbortTransactionRequest
    : public TTransactedRequest
{ };

typedef TIntrusivePtr<TAbortTransactionRequest> TAbortTransactionRequestPtr;

class TAbortTransactionCommand
    : public TTransactedCommandBase<TAbortTransactionRequest>
{
public:
    explicit TAbortTransactionCommand(ICommandContext* host)
        : TTransactedCommandBase(host)
    { }

private:
    virtual void DoExecute();
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NDriver
} // namespace NYT

