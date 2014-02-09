#pragma once

#include "public.h"

#include <core/rpc/public.h>

#include <core/misc/common.h>
#include <core/misc/error.h>

namespace NYT {
namespace NQueryClient {

////////////////////////////////////////////////////////////////////////////////

struct IPrepareCallbacks
{
    virtual ~IPrepareCallbacks()
    { }

    virtual TFuture<TErrorOr<TDataSplit>> GetInitialSplit(const NYPath::TYPath& path) = 0;
};

////////////////////////////////////////////////////////////////////////////////

struct IEvaluateCallbacks
{
    virtual ~IEvaluateCallbacks()
    { }

    virtual ISchemedReaderPtr GetReader(const TDataSplit& dataSplit) = 0;

};

////////////////////////////////////////////////////////////////////////////////

struct ICoordinateCallbacks
    : public IEvaluateCallbacks
{
    virtual ~ICoordinateCallbacks()
    { }

    virtual bool CanSplit(
        const TDataSplit& dataSplit) = 0;

    virtual TFuture<TErrorOr<std::vector<TDataSplit>>> SplitFurther(
        const TDataSplit& dataSplit) = 0;

    virtual ISchemedReaderPtr Delegate(
        const TPlanFragment& fragment,
        const TDataSplit& colocatedDataSplit) = 0;

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NQueryClient
} // namespace NYT

