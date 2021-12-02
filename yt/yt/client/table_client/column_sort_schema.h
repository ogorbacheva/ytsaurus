#pragma once

#include "public.h"
#include "comparator.h"

namespace NYT::NTableClient {

////////////////////////////////////////////////////////////////////////////////

struct TColumnSortSchema
{
    TString Name;
    ESortOrder SortOrder;

    void Persist(const TStreamPersistenceContext& context);
};

void Serialize(const TColumnSortSchema& schema, NYson::IYsonConsumer* consumer);
void Deserialize(TColumnSortSchema& schema, NYTree::INodePtr node);

bool operator == (const TColumnSortSchema& lhs, const TColumnSortSchema& rhs);
bool operator != (const TColumnSortSchema& lhs, const TColumnSortSchema& rhs);

////////////////////////////////////////////////////////////////////////////////

void ValidateSortColumns(const std::vector<TColumnSortSchema>& columns);

////////////////////////////////////////////////////////////////////////////////

void ToProto(
    NProto::TSortColumnsExt* protoSortColumns,
    const TSortColumns& sortColumns);

void FromProto(
    TSortColumns* sortColumns,
    const NProto::TSortColumnsExt& protoSortColumns);

void FormatValue(TStringBuilderBase* builder, const TSortColumns& key, TStringBuf format);
TString ToString(const TSortColumns& key);

////////////////////////////////////////////////////////////////////////////////

TKeyColumns GetColumnNames(const TSortColumns& sortColumns);

TComparator GetComparator(const TSortColumns& sortColumns);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTableClient
