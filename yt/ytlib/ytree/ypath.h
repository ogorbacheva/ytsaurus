#pragma once

#include "common.h"
#include "ytree.h"
#include "yson_events.h"

namespace NYT {
namespace NYTree {

////////////////////////////////////////////////////////////////////////////////

struct IYPathService
    : virtual TRefCountedBase
{
    typedef TIntrusivePtr<IYPathService> TPtr;

    DECLARE_ENUM(ECode,
        (Done)
        (Recurse)
        (Error)
    );

    template <class T>
    struct TResult
    {
        ECode Code;
        
        // Done
        T Value;

        // Recurse
        IYPathService::TPtr RecurseService;
        TYPath RecursePath;
        
        // Error
        Stroka ErrorMessage;

        static TResult CreateDone(const T& value)
        {
            TResult result;
            result.Code = ECode::Done;
            result.Value = value;
            return result;
        }

        static TResult CreateRecurse(
            IYPathService::TPtr recurseService,
            TYPath recursePath)
        {
            TResult result;
            result.Code = ECode::Recurse;
            result.RecurseService = recurseService;
            result.RecursePath = recursePath;
            return result;
        }

        static TResult CreateError(Stroka errorMessage)
        {
            TResult result;
            result.Code = ECode::Error;
            result.ErrorMessage = errorMessage;
            return result;
        }
    };

    typedef TResult< TIntrusiveConstPtr<INode> > TNavigateResult;
    virtual TNavigateResult Navigate(TYPath path) = 0;

    typedef TResult<TVoid> TGetResult;
    virtual TGetResult Get(TYPath path, IYsonConsumer* events) = 0;

    typedef TResult<TVoid> TSetResult;
    virtual TSetResult Set(TYPath path, TYsonProducer::TPtr producer) = 0;

    typedef TResult<TVoid> TRemoveResult;
    virtual TRemoveResult Remove(TYPath path) = 0;
};

////////////////////////////////////////////////////////////////////////////////

IYPathService::TPtr AsYPath(INode::TPtr node);
IYPathService::TPtr AsYPath(INode::TConstPtr node);

void ChopYPathPrefix(
    TYPath path,
    Stroka* prefix,
    TYPath* tailPath);

INode::TConstPtr NavigateYPath(
    IYPathService::TPtr rootService,
    TYPath path);

void GetYPath(
    IYPathService::TPtr rootService,
    TYPath path,
    IYsonConsumer* consumer);

void SetYPath(
    IYPathService::TPtr rootService,
    TYPath path,
    TYsonProducer::TPtr producer);

void RemoveYPath(
    IYPathService::TPtr rootService,
    TYPath path);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYTree
} // namespace NYT
