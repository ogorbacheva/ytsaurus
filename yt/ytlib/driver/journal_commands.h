#pragma once

#include "command.h"

#include <yt/ytlib/formats/format.h>

#include <yt/ytlib/table_client/unversioned_row.h>

#include <yt/ytlib/ypath/rich.h>

namespace NYT {
namespace NDriver {

////////////////////////////////////////////////////////////////////////////////

class TReadJournalCommand
    : public TTypedCommand<NApi::TJournalReaderOptions>
{
public:
    TReadJournalCommand();

private:
    NYPath::TRichYPath Path;
    NYTree::INodePtr JournalReader;

    virtual void DoExecute(ICommandContextPtr context) override;
};

////////////////////////////////////////////////////////////////////////////////

class TWriteJournalCommand
    : public TTypedCommand<NApi::TJournalWriterOptions>
{
public:
    TWriteJournalCommand();

private:
    NYPath::TRichYPath Path;
    NYTree::INodePtr JournalWriter;

    virtual void DoExecute(ICommandContextPtr context) override;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NDriver
} // namespace NYT
