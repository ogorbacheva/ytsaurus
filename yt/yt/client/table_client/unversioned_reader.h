#pragma once

#include "public.h"
#include "config.h"

#include <yt/client/chunk_client/reader_base.h>

#include <yt/core/actions/future.h>

namespace NYT::NTableClient {

////////////////////////////////////////////////////////////////////////////////

struct IUnversionedReaderBase
    : public virtual NChunkClient::IReaderBase
{
    virtual IUnversionedRowBatchPtr Read(const TRowBatchReadOptions& options = {}) = 0;
};

////////////////////////////////////////////////////////////////////////////////

struct ISchemafulUnversionedReader
    : public IUnversionedReaderBase
{ };

DEFINE_REFCOUNTED_TYPE(ISchemafulUnversionedReader)

////////////////////////////////////////////////////////////////////////////////

struct ISchemalessUnversionedReader
    : public IUnversionedReaderBase
{
    virtual const TNameTablePtr& GetNameTable() const = 0;
};

DEFINE_REFCOUNTED_TYPE(ISchemalessUnversionedReader)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTableClient
