#pragma once

#include "block_store.h"
#include "chunk_store.h"
#include "chunk_holder_service_rpc.h"

#include "../chunk_client/file_writer.h"
#include "../misc/lease_manager.h"

namespace NYT {
namespace NChunkHolder {

////////////////////////////////////////////////////////////////////////////////

class TSessionManager;

//! Represents a chunk upload in progress.
class TSession
    : public TRefCountedBase
{
public:
    typedef TIntrusivePtr<TSession> TPtr;

    TSession(
        TSessionManager* sessionManager,
        const NChunkClient::TChunkId& chunkId,
        TLocation* location,
        int windowSize);

    ~TSession();

    //! Returns TChunkId being uploaded.
    NChunkClient::TChunkId GetChunkId() const;

    //! Returns target chunk location.
    TLocation::TPtr GetLocation() const;

    //! Returns the size of blocks received so far.
    i64 GetSize() const;

    //! Returns a cached block that is still in the session window.
    TCachedBlock::TPtr GetBlock(i32 blockIndex);

    //! Puts a block into the window.
    void PutBlock(i32 blockIndex, const TSharedRef& data);

    //! Flushes a block and moves the window
    /*!
     * The operation is asynchronous. It returns a result that gets set
     * when the actual flush happens. Once a block is flushed, the next block becomes
     * the first one in the window.
     */
    TFuture<TVoid>::TPtr FlushBlock(i32 blockIndex);

    void RenewLease();

private:
    friend class TSessionManager;

    typedef TChunkHolderServiceProxy TProxy;
    typedef TProxy::EErrorCode EErrorCode;
    typedef NChunkServer::NProto::TChunkAttributes TChunkAttributes;

    DECLARE_ENUM(ESlotState,
        (Empty)
        (Received)
        (Written)
    );

    struct TSlot
    {
        TSlot()
            : State(ESlotState::Empty)
            , Block(NULL)
            , IsWritten(New< TFuture<TVoid> >())
        { }

        ESlotState State;
        TCachedBlock::TPtr Block;
        TFuture<TVoid>::TPtr IsWritten;
    };

    typedef yvector<TSlot> TWindow;

    TIntrusivePtr<TSessionManager> SessionManager;
    NChunkClient::TChunkId ChunkId;
    TLocation::TPtr Location;
    
    TWindow Window;
    i32 WindowStart;
    i32 FirstUnwritten;
    i64 Size;

    Stroka FileName;
    NChunkClient::TFileWriter::TPtr Writer;

    TLeaseManager::TLease Lease;

    TFuture<TVoid>::TPtr Finish(const TChunkAttributes& attributes);
    void Cancel(const Stroka& errorMessage);

    void SetLease(TLeaseManager::TLease lease);
    void CloseLease();

    bool IsInWindow(i32 blockIndex);
    void VerifyInWindow(i32 blockIndex);
    TSlot& GetSlot(i32 blockIndex);
    void RotateWindow(i32 flushedBlockIndex);

    IInvoker::TPtr GetInvoker();

    void OpenFile();
    void DoOpenFile();

    void DeleteFile(const Stroka& errorMessage);
    void DoDeleteFile(const Stroka& errorMessage);

    TFuture<TVoid>::TPtr CloseFile(const TChunkAttributes& attributes);
    TVoid DoCloseFile(const TChunkAttributes& attributes);

    void EnqueueWrites();
    TVoid DoWrite(TCachedBlock::TPtr block, i32 blockIndex);
    void OnBlockWritten(TVoid, i32 blockIndex);

    TVoid OnBlockFlushed(TVoid, i32 blockIndex);
};

////////////////////////////////////////////////////////////////////////////////

//! Manages chunk uploads.
class TSessionManager
    : public TRefCountedBase
{
public:
    typedef TIntrusivePtr<TSessionManager> TPtr;

    //! Constructs a manager.
    TSessionManager(
        const TChunkHolderConfig& config,
        TBlockStore::TPtr blockStore,
        TChunkStore::TPtr chunkStore,
        IInvoker::TPtr serviceInvoker);

    //! Starts a new chunk upload session.
    TSession::TPtr StartSession(
        const NChunkClient::TChunkId& chunkId,
        int windowSize);

    //! Completes an earlier opened upload session.
    /*!
     * The call returns a result that gets set when the session is finished.
     */
    TFuture<TVoid>::TPtr FinishSession(
        TSession::TPtr session,
        const NChunkServer::NProto::TChunkAttributes& attributes);

    //! Cancels an earlier opened upload session.
    /*!
     * Chunk file is closed asynchronously, however the call returns immediately.
     */
    void CancelSession(TSession* session, const Stroka& errorMessage);

    //! Finds a session by TChunkId. Returns NULL when no session is found.
    TSession::TPtr FindSession(const NChunkClient::TChunkId& chunkId);

    //! Returns the number of currently active session.
    int GetSessionCount();

private:
    friend class TSession;

    TChunkHolderConfig Config; // TODO: avoid copying
    TBlockStore::TPtr BlockStore;
    TChunkStore::TPtr ChunkStore;
    IInvoker::TPtr ServiceInvoker;

    typedef yhash_map<NChunkClient::TChunkId, TSession::TPtr> TSessionMap;
    TSessionMap SessionMap;

    void OnLeaseExpired(TSession::TPtr session);
    TVoid OnSessionFinished(TVoid, TSession::TPtr session);

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkHolder
} // namespace NYT

