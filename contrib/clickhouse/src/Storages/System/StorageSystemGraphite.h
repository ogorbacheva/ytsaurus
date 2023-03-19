#pragma once

#include <DataTypes/DataTypeArray.h>
#include <DataTypes/DataTypeString.h>
#include <DataTypes/DataTypesNumber.h>
#include <Storages/System/IStorageSystemOneBlock.h>
#include <Storages/MergeTree/MergeTreeData.h>
#include <common/shared_ptr_helper.h>

namespace DB
{

/// Provides information about Graphite configuration.
class StorageSystemGraphite final : public shared_ptr_helper<StorageSystemGraphite>, public IStorageSystemOneBlock<StorageSystemGraphite>
{
    friend struct shared_ptr_helper<StorageSystemGraphite>;
public:
    std::string getName() const override { return "SystemGraphite"; }

    static NamesAndTypesList getNamesAndTypes();

    struct Config
    {
        Graphite::Params graphite_params;
        Array databases;
        Array tables;
    };

    using Configs = std::map<const String, Config>;


protected:
    using IStorageSystemOneBlock::IStorageSystemOneBlock;

    void fillData(MutableColumns & res_columns, ContextPtr context, const SelectQueryInfo & query_info) const override;
};

}
