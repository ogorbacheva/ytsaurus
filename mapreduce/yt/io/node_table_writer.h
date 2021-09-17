#pragma once

#include <mapreduce/yt/interface/io.h>
#include <library/cpp/yson/public.h>

namespace NYT {

class TProxyOutput;

////////////////////////////////////////////////////////////////////////////////

class TNodeTableWriter
    : public INodeWriterImpl
{
public:
    explicit TNodeTableWriter(THolder<TProxyOutput> output, EYsonFormat format = YF_BINARY);
    ~TNodeTableWriter() override;

    void AddRow(const TNode& row, size_t tableIndex) override;
    void AddRow(TNode&& row, size_t tableIndex) override;

    size_t GetTableCount() const override;
    void FinishTable(size_t) override;
    void Abort() override;

private:
    THolder<TProxyOutput> Output_;
    TVector<THolder<TYsonWriter>> Writers_;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
