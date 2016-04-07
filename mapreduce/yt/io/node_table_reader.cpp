#include "node_table_reader.h"

#include "proxy_input.h"

#include <library/yson/parser.h>
#include <mapreduce/yt/common/node_builder.h>
#include <mapreduce/yt/common/log.h>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

class TStopException
    : public yexception
{ };

////////////////////////////////////////////////////////////////////////////////

TRowQueue::TRowQueue()
    : Size_(0)
    , SizeLimit_(4 << 20)
    , Stopped_(false)
{ }

void TRowQueue::Enqueue(TRowElementPtr row)
{
    while (!Stopped_ && Size_ && Size_ + row->Size > SizeLimit_) {
        DequeueEvent_.Wait();
    }
    if (Stopped_) {
        throw TStopException();
    }
    AtomicAdd(Size_, row->Size);
    {
        TGuard<TSpinLock> guard(SpinLock_);
        Queue_.push(row);
    }
    EnqueueEvent_.Signal();
}

TRowElementPtr TRowQueue::Dequeue()
{
    while (true) {
        TGuard<TSpinLock> guard(SpinLock_);
        if (Queue_.empty()) {
            guard.Release();
            EnqueueEvent_.Wait();
        } else {
            TRowElementPtr element = Queue_.front();
            Queue_.pop();
            AtomicSub(Size_, element->Size);
            DequeueEvent_.Signal();
            return element;
        }
    }
}

void TRowQueue::Clear()
{
    TGuard<TSpinLock> guard(SpinLock_);
    Queue_.clear();
}

void TRowQueue::Stop()
{
    Stopped_ = true;
    DequeueEvent_.Signal();
}

////////////////////////////////////////////////////////////////////////////////

class TRowBuilder
    : public TYsonConsumerBase
{
public:
    explicit TRowBuilder(TRowQueue* queue);

    void OnStringScalar(const TStringBuf& value) override;
    void OnInt64Scalar(i64 value) override;
    void OnUint64Scalar(ui64 value) override;
    void OnDoubleScalar(double value) override;
    void OnBooleanScalar(bool value) override;
    void OnBeginList() override;
    void OnEntity() override;
    void OnListItem() override;
    void OnEndList() override;
    void OnBeginMap() override;
    void OnKeyedItem(const TStringBuf& key) override;
    void OnEndMap() override;
    void OnBeginAttributes() override;
    void OnEndAttributes() override;

    void Stop();
    void OnStreamError();
    void Finalize();

private:
    THolder<TNodeBuilder> Builder_;
    TRowElementPtr Row_;
    int Depth_;
    bool Started_;
    volatile bool Stopped_;
    TRowQueue* RowQueue_;

    void EnqueueRow();
};

TRowBuilder::TRowBuilder(TRowQueue* queue)
    : Depth_(0)
    , Started_(false)
    , Stopped_(false)
    , RowQueue_(queue)
{ }

void TRowBuilder::OnStringScalar(const TStringBuf& value)
{
    Row_->Size += sizeof(TNode) + sizeof(Stroka) + value.Size();
    Builder_->OnStringScalar(value);
}

void TRowBuilder::OnInt64Scalar(i64 value)
{
    Row_->Size += sizeof(TNode);
    Builder_->OnInt64Scalar(value);
}

void TRowBuilder::OnUint64Scalar(ui64 value)
{
    Row_->Size += sizeof(TNode);
    Builder_->OnUint64Scalar(value);
}

void TRowBuilder::OnDoubleScalar(double value)
{
    Row_->Size += sizeof(TNode);
    Builder_->OnDoubleScalar(value);
}

void TRowBuilder::OnBooleanScalar(bool value)
{
    Row_->Size += sizeof(TNode);
    Builder_->OnBooleanScalar(value);
}

void TRowBuilder::OnBeginList()
{
    ++Depth_;
    Builder_->OnBeginList();
}

void TRowBuilder::OnEntity()
{
    Row_->Size += sizeof(TNode);
    Builder_->OnEntity();
}

void TRowBuilder::OnListItem()
{
    if (Depth_ == 0) {
        EnqueueRow();
    } else {
        Builder_->OnListItem();
    }
}

void TRowBuilder::OnEndList()
{
    --Depth_;
    Builder_->OnEndList();
}

void TRowBuilder::OnBeginMap()
{
    ++Depth_;
    Builder_->OnBeginMap();
}

void TRowBuilder::OnKeyedItem(const TStringBuf& key)
{
    Row_->Size += sizeof(Stroka) + key.Size();
    Builder_->OnKeyedItem(key);
}

void TRowBuilder::OnEndMap()
{
    --Depth_;
    Builder_->OnEndMap();
}

void TRowBuilder::OnBeginAttributes()
{
    ++Depth_;
    Builder_->OnBeginAttributes();
}

void TRowBuilder::OnEndAttributes()
{
    --Depth_;
    Builder_->OnEndAttributes();
}

void TRowBuilder::EnqueueRow()
{
    if (!Started_) {
        Started_ = true;
    } else {
        RowQueue_->Enqueue(Row_);
    }
    Row_ = new TRowElement;
    Builder_.Reset(new TNodeBuilder(&Row_->Node));
}

void TRowBuilder::Stop()
{
    Stopped_ = true;
    RowQueue_->Stop();
}

void TRowBuilder::OnStreamError()
{
    Row_ = new TRowElement;
    Row_->Type = TRowElement::ERROR;
    RowQueue_->Enqueue(Row_);
}

void TRowBuilder::Finalize()
{
    if (Started_) {
        RowQueue_->Enqueue(Row_);
    }
    Row_ = new TRowElement;
    Row_->Type = TRowElement::FINISH;
    RowQueue_->Enqueue(Row_);
}

////////////////////////////////////////////////////////////////////////////////

TNodeTableReader::TNodeTableReader(THolder<TProxyInput> input)
    : Input_(std::move(input))
{
    PrepareParsing();

    Running_ = true;
    Thread_.Reset(new TThread(TThread::TParams(FetchThread, this).SetName("node_reader")));
    Thread_->Start();

    Next();
}

TNodeTableReader::~TNodeTableReader()
{
    if (Running_) {
        Running_ = false;
        Builder_->Stop();
        RetryPrepared_.Signal();
        Thread_->Join();
    }
}

const TNode& TNodeTableReader::GetRow() const
{
    CheckValidity();
    return Row_->Node;
}

bool TNodeTableReader::IsValid() const
{
    return Valid_;
}

void TNodeTableReader::Next()
{
    CheckValidity();

    if (RowIndex_) {
        ++*RowIndex_;
    }

    TMaybe<ui64> rowIndex;
    TMaybe<ui32> rangeIndex;

    while (true) {
        Row_ = RowQueue_.Dequeue();
        if (Row_->Type == TRowElement::ROW) {
            if (!Row_->Node.IsEntity()) {
                AtStart_ = false;
                break;
            }

            for (auto& entry : Row_->Node.GetAttributes().AsMap()) {
                if (entry.first == "key_switch") {
                    if (!AtStart_) {
                        Valid_ = false;
                    }
                } else if (entry.first == "table_index") {
                    TableIndex_ = static_cast<ui32>(entry.second.AsInt64());
                } else if (entry.first == "row_index") {
                    rowIndex = static_cast<ui64>(entry.second.AsInt64());
                } else if (entry.first == "range_index") {
                    rangeIndex = static_cast<ui32>(entry.second.AsInt64());
                }
            }

            if (rowIndex) {
                if (Input_->HasRangeIndices()) {
                    if (rangeIndex) {
                        RowIndex_ = rowIndex;
                        RangeIndex_ = rangeIndex;
                    }
                } else {
                    RowIndex_ = rowIndex;
                }
            }

            if (!Valid_) {
                break;
            }

        } else if (Row_->Type == TRowElement::FINISH) {
            Finished_ = true;
            Valid_ = false;
            Running_ = false;
            Thread_->Join();
            break;

        } else if (Row_->Type == TRowElement::ERROR) {
            OnStreamError();
        }
    }
}

ui32 TNodeTableReader::GetTableIndex() const
{
    CheckValidity();
    return TableIndex_;
}

ui64 TNodeTableReader::GetRowIndex() const
{
    CheckValidity();
    return RowIndex_.GetOrElse(0UL);
}

void TNodeTableReader::NextKey()
{
    while (Valid_) {
        Next();
    }

    if (Finished_) {
        return;
    }

    Valid_ = true;
}

void TNodeTableReader::PrepareParsing()
{
    RowQueue_.Clear();
    Builder_.Reset(new TRowBuilder(&RowQueue_));
    Parser_.Reset(new TYsonParser(Builder_.Get(), Input_.Get(), YT_LIST_FRAGMENT));
}

void TNodeTableReader::OnStreamError()
{
    if (Input_->OnStreamError(Exception_, !RowIndex_.Defined(),
        RangeIndex_.GetOrElse(0ul), RowIndex_.GetOrElse(0ull)))
    {
        RowIndex_.Clear();
        RangeIndex_.Clear();
        PrepareParsing();
        RetryPrepared_.Signal();
    } else {
        Running_ = false;
        RetryPrepared_.Signal();
        Thread_->Join();
        throw Exception_;
    }
}

void TNodeTableReader::CheckValidity() const
{
    if (!Valid_) {
        ythrow yexception() << "Iterator is not valid";
    }
}

void TNodeTableReader::FetchThread()
{
    while (Running_) {
        try {
            Parser_->Parse();
            Builder_->Finalize();
            break;
        } catch (TStopException&) {
            break;
        } catch (yexception& e) {
            Exception_ = e;
            Builder_->OnStreamError();
            RetryPrepared_.Wait();
        }
    }
}

void* TNodeTableReader::FetchThread(void* opaque)
{
    static_cast<TNodeTableReader*>(opaque)->FetchThread();
    return nullptr;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
