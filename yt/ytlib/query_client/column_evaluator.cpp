#include "stdafx.h"

#include "column_evaluator.h"
#include "config.h"

#ifdef YT_USE_LLVM
#include "cg_fragment_compiler.h"
#include "query_statistics.h"
#include "folding_profiler.h"
#endif

#include <core/misc/sync_cache.h>

namespace NYT {
namespace NQueryClient {

using namespace NVersionedTableClient;

////////////////////////////////////////////////////////////////////////////////

TColumnEvaluator::TColumnEvaluator(const TTableSchema& schema, int keySize)
    : Schema_(schema)
    , KeySize_(keySize)
#ifdef YT_USE_LLVM
    , Evaluators_(keySize)
    , Variables_(keySize)
    , References_(keySize)
#endif
{ }

void TColumnEvaluator::PrepareEvaluator(int index)
{
#ifdef YT_USE_LLVM
    YCHECK(index < KeySize_);
    YCHECK(Schema_.Columns()[index].Expression);

    if (!Evaluators_[index]) {
        auto expr = PrepareExpression(Schema_.Columns()[index].Expression.Get(), Schema_);
        TCGBinding binding;
        TFoldingProfiler()
            .Set(binding)
            .Set(Variables_[index])
            .Set(References_[index])
            .Profile(expr);
        Evaluators_[index] = CodegenExpression(expr, Schema_, binding);
    }
#else
    THROW_ERROR_EXCEPTION("Computed colums require LLVM enabled in build");
#endif
}

void TColumnEvaluator::EvaluateKey(TRow fullRow, TRowBuffer& buffer, int index)
{
#ifdef YT_USE_LLVM
    PrepareEvaluator(index);

    TQueryStatistics statistics;
    TExecutionContext executionContext;
    executionContext.Schema = Schema_;
    executionContext.LiteralRows = &Variables_[index].LiteralRows;
    executionContext.PermanentBuffer = &buffer;
    executionContext.OutputBuffer = &buffer;
    executionContext.IntermediateBuffer = &buffer;
    executionContext.Statistics = &statistics;

    Evaluators_[index](
        &fullRow[index],
        fullRow,
        Variables_[index].ConstantsRowBuilder.GetRow(),
        &executionContext);
#else
    THROW_ERROR_EXCEPTION("Computed colums require LLVM enabled in build");
#endif
}

void TColumnEvaluator::EvaluateKeys(TRow fullRow, TRowBuffer& buffer)
{
    for (int index = 0; index < KeySize_; ++index) {
        if (Schema_.Columns()[index].Expression) {
            EvaluateKey(fullRow, buffer, index);
        }
    }
}

void TColumnEvaluator::EvaluateKeys(
    TRow fullRow,
    TRowBuffer& buffer,
    const TRow partialRow,
    const TNameTableToSchemaIdMapping& idMapping)
{
    int columnCount = fullRow.GetCount();
    bool keyColumnSeen[MaxKeyColumnCount] {};

    for (int index = 0; index < columnCount; ++index) {
        fullRow[index].Type = EValueType::Null;
    }

    for (int index = 0; index < partialRow.GetCount(); ++index) {
        int id = partialRow[index].Id;

        if (id >= idMapping.size()) {
            THROW_ERROR_EXCEPTION("Invalid column id %v, expected in range [0,%v]",
                id,
                idMapping.size() - 1);
        }

        int schemaId = idMapping[id];
        YCHECK(schemaId < Schema_.Columns().size());
        const auto& column = Schema_.Columns()[schemaId];

        if (schemaId >= columnCount) {
            THROW_ERROR_EXCEPTION("Unexpected column %Qv", column.Name);
        }

        if (column.Expression) {
            THROW_ERROR_EXCEPTION(
                "Column %Qv is computed automatically and should not be provided by user",
                column.Name);
        }

        if (schemaId < KeySize_) {
            if (keyColumnSeen[schemaId]) {
                THROW_ERROR_EXCEPTION("Duplicate key component %Qv",
                    column.Name);
            }

            keyColumnSeen[schemaId] = true;
        }

        fullRow[schemaId] = partialRow[index];
    }

    EvaluateKeys(fullRow, buffer);

    for (int index = 0; index < columnCount; ++index) {
        fullRow[index].Id = index;
    }
}

const yhash_set<Stroka>& TColumnEvaluator::GetReferences(int index)
{
#ifdef YT_USE_LLVM
    PrepareEvaluator(index);
    return References_[index];
#else
    THROW_ERROR_EXCEPTION("Computed colums require LLVM enabled in build");
#endif
}

////////////////////////////////////////////////////////////////////////////////

#ifdef YT_USE_LLVM

class TCachedColumnEvaluator
    : public TSyncCacheValueBase<llvm::FoldingSetNodeID, TCachedColumnEvaluator>
{
public:
    TCachedColumnEvaluator(
        const llvm::FoldingSetNodeID& id,
        TColumnEvaluatorPtr evaluator)
        : TSyncCacheValueBase(id)
        , Evaluator_(std::move(evaluator))
    { }

    TColumnEvaluatorPtr GetColumnEvaluator()
    {
        return Evaluator_;
    }

private:
    const TColumnEvaluatorPtr Evaluator_;
};

class TColumnEvaluatorCache::TImpl
    : public TSyncSlruCacheBase<llvm::FoldingSetNodeID, TCachedColumnEvaluator>
{
public:
    explicit TImpl(TColumnEvaluatorCacheConfigPtr config)
        : TSyncSlruCacheBase(config->CGCache)
    { }

    TColumnEvaluatorPtr Get(const TTableSchema& schema, int keySize)
    {
        llvm::FoldingSetNodeID id;
        TFoldingProfiler()
            .Set(id)
            .Profile(schema, keySize);

        auto cachedEvaluator = Find(id);
        if (!cachedEvaluator) {
            auto evaluator = New<TColumnEvaluator>(schema, keySize);
            cachedEvaluator = New<TCachedColumnEvaluator>(id, evaluator);
            TryInsert(cachedEvaluator, &cachedEvaluator);
        }

        return cachedEvaluator->GetColumnEvaluator();
    }
};

#endif

////////////////////////////////////////////////////////////////////////////////

TColumnEvaluatorCache::TColumnEvaluatorCache(TColumnEvaluatorCacheConfigPtr config)
#ifdef YT_USE_LLVM
    : Impl_(New<TImpl>(std::move(config)))
#endif
{ }

TColumnEvaluatorCache::~TColumnEvaluatorCache() = default;

TColumnEvaluatorPtr TColumnEvaluatorCache::Find(
    const TTableSchema& schema,
    int keySize)
{
#ifdef YT_USE_LLVM
    return Impl_->Get(schema, keySize);
#else
    THROW_ERROR_EXCEPTION("Computed columns are not supported in this build");
#endif
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NQueryClient
} // namespace NYT
