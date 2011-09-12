#pragma once

#include "chunk_manager.h"

namespace NYT {
namespace NChunkManager {

////////////////////////////////////////////////////////////////////////////////

class TChunkPlacement
    : public TRefCountedBase
{
public:
    typedef TIntrusivePtr<TChunkPlacement> TPtr;

    void AddHolder(const THolder& holder);
    void RemoveHolder(const THolder& holder);
    void UpdateHolder(const THolder& holder);

    yvector<THolderId> GetTargetHolders(int replicaCount);

private:
    typedef ymultimap<double, THolderId> TPreferenceMap;
    typedef yhash_map<THolderId, TPreferenceMap::iterator> TIteratorMap;

    TPreferenceMap PreferenceMap;
    TIteratorMap IteratorMap;

    static double GetLoadFactor(const THolder& holder);
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkManager
} // namespace NYT
