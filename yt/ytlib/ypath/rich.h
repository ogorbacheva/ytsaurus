#pragma once

#include "public.h"

#include <yt/core/yson/public.h>

#include <yt/core/ytree/attributes.h>

#include <yt/ytlib/table_client/schema.h>

#include <yt/ytlib/chunk_client/read_limit.h>

namespace NYT {
namespace NYPath {

////////////////////////////////////////////////////////////////////////////////

//! YPath string plus attributes.
class TRichYPath
{
public:
    TRichYPath();
    TRichYPath(const TRichYPath& other);
    TRichYPath(TRichYPath&& other);
    TRichYPath(const char* path);
    TRichYPath(const TYPath& path);
    TRichYPath(const TYPath& path, const NYTree::IAttributeDictionary& attributes);
    TRichYPath& operator = (const TRichYPath& other);

    static TRichYPath Parse(const Stroka& str);
    TRichYPath Normalize() const;

    const TYPath& GetPath() const;
    void SetPath(const TYPath& path);

    const NYTree::IAttributeDictionary& Attributes() const;
    NYTree::IAttributeDictionary& Attributes();

    void Save(TStreamSaveContext& context) const;
    void Load(TStreamLoadContext& context);

    // Attribute accessors.
    bool GetAppend() const;
    NChunkClient::TChannel GetChannel() const;
    std::vector<NChunkClient::TReadRange> GetRanges() const;
    TNullable<Stroka> FindFileName() const;
    TNullable<bool> FindExecutable() const;
    TNullable<NYson::TYsonString> FindFormat() const;
    TNullable<NTableClient::TTableSchema> FindTableSchema() const;

private:
    TYPath Path_;
    std::unique_ptr<NYTree::IAttributeDictionary> Attributes_;
};

bool operator== (const TRichYPath& lhs, const TRichYPath& rhs);

////////////////////////////////////////////////////////////////////////////////

Stroka ToString(const TRichYPath& path);

std::vector<TRichYPath> Normalize(const std::vector<TRichYPath>& paths);

void InitializeFetchRequest(
    NChunkClient::NProto::TReqFetch* request,
    const TRichYPath& richPath);

void Serialize(const TRichYPath& richPath, NYson::IYsonConsumer* consumer);
void Deserialize(TRichYPath& richPath, NYTree::INodePtr node);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYPath
} // namespace NYT
