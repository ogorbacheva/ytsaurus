#pragma once

#include <yt/yt/core/misc/common.h>

#include <yt/yt/core/ypath/public.h>

#include <yt/yt/core/yson/public.h>

namespace NYT::NQueueClient {

////////////////////////////////////////////////////////////////////////////////

struct TCrossClusterReference
{
    TString Cluster;
    NYPath::TYPath Path;

    bool operator ==(const TCrossClusterReference& other) const;
    bool operator <(const TCrossClusterReference& other) const;

    static TCrossClusterReference FromString(TStringBuf path);
};

TString ToString(const TCrossClusterReference& queueRef);

void Serialize(const TCrossClusterReference& queueRef, NYson::IYsonConsumer* consumer);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NQueueClient

template <>
struct THash<NYT::NQueueClient::TCrossClusterReference>
{
    size_t operator()(const NYT::NQueueClient::TCrossClusterReference& crossClusterRef) const;
};
