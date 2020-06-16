package integration

import (
	"context"
	"testing"
	"time"

	"github.com/stretchr/testify/require"

	"a.yandex-team.ru/library/go/ptr"
	"a.yandex-team.ru/yt/go/schema"
	"a.yandex-team.ru/yt/go/ypath"
	"a.yandex-team.ru/yt/go/yt"
	"a.yandex-team.ru/yt/go/yttest"
)

type exampleRow struct {
	A string `yson:"a"`
	B int    `yson:"b"`
}

func TestTables(t *testing.T) {
	t.Parallel()

	env, cancel := yttest.NewEnv(t)
	defer cancel()

	ctx, cancel := context.WithTimeout(context.Background(), time.Second*15)
	defer cancel()

	validate := func(name ypath.Path) {
		w, err := env.YT.WriteTable(ctx, name, nil)
		require.NoError(t, err)

		require.NoError(t, w.Write(exampleRow{"foo", 1}))
		require.NoError(t, w.Write(exampleRow{"bar", 2}))
		require.NoError(t, w.Write(exampleRow{B: 3}))
		require.NoError(t, w.Commit())

		r, err := env.YT.ReadTable(ctx, name, nil)
		require.NoError(t, err)
		defer func() { _ = r.Close() }()

		var s exampleRow
		require.True(t, r.Next())
		require.NoError(t, r.Scan(&s))
		require.Equal(t, exampleRow{"foo", 1}, s)

		require.True(t, r.Next())
		require.NoError(t, r.Scan(&s))
		require.Equal(t, exampleRow{"bar", 2}, s)

		require.True(t, r.Next())
		require.NoError(t, r.Scan(&s))
		require.Equal(t, exampleRow{"", 3}, s)

		require.False(t, r.Next())
		require.NoError(t, r.Err())
	}

	t.Run("WriteReadTable", func(t *testing.T) {
		name := tmpPath()

		_, err := env.YT.CreateNode(ctx, name, yt.NodeTable, nil)
		require.NoError(t, err)

		validate(name)
	})

	t.Run("WriteWithSchema", func(t *testing.T) {
		name := tmpPath()

		_, err := env.YT.CreateNode(ctx, name, yt.NodeTable, &yt.CreateNodeOptions{
			Attributes: map[string]interface{}{
				"schema": schema.MustInfer(&exampleRow{}),
			},
		})
		require.NoError(t, err)

		validate(name)
	})

	t.Run("AppendToTable", func(t *testing.T) {
		name := tmpPath()

		_, err := env.YT.CreateNode(ctx, name, yt.NodeTable, nil)
		require.NoError(t, err)

		write := func() {
			appendAttr := true
			w, err := env.YT.WriteTable(ctx, ypath.Rich{Path: name, Append: &appendAttr}, nil)
			require.NoError(t, err)

			require.NoError(t, w.Write(exampleRow{"foo", 1}))
			require.NoError(t, w.Write(exampleRow{"bar", 2}))
			require.NoError(t, w.Commit())
		}

		write()

		write()

		var rowCount int
		require.NoError(t, env.YT.GetNode(env.Ctx, name.Attr("row_count"), &rowCount, nil))
		require.Equal(t, 4, rowCount)
	})

	t.Run("ReadRanges", func(t *testing.T) {
		name := tmpPath()

		err := env.UploadSlice(name, []exampleRow{
			{"foo", 1},
			{"bar", 2},
		})
		require.NoError(t, err)

		richPath := name.Rich().
			AddRange(ypath.Exact(ypath.RowIndex(1))).
			AddRange(ypath.Exact(ypath.RowIndex(0)))

		var result []exampleRow
		err = env.DownloadSlice(richPath, &result)
		require.NoError(t, err)

		require.Equal(t, []exampleRow{
			{"bar", 2},
			{"foo", 1},
		}, result)
	})

	t.Run("Rollback", func(t *testing.T) {
		name := tmpPath()

		_, err := env.YT.CreateNode(ctx, name, yt.NodeTable, nil)
		require.NoError(t, err)

		w, err := env.YT.WriteTable(ctx, name, nil)
		require.NoError(t, err)

		go func() {
			time.Sleep(time.Second * 5)
			_ = w.Rollback()
		}()

		for {
			if err := w.Write(exampleRow{"foo", 1}); err != nil {
				break
			}
		}

		var count int
		require.NoError(t, env.YT.GetNode(ctx, name.Attr("row_count"), &count, nil))
		require.Equal(t, 0, count)
	})

	t.Run("EmptyTableInfo", func(t *testing.T) {
		name := tmpPath()

		_, err := yt.CreateTable(env.Ctx, env.YT, name)
		require.NoError(t, err)

		r, err := env.YT.ReadTable(env.Ctx, name, nil)
		require.NoError(t, err)
		defer r.Close()

		_, ok := yt.StartRowIndex(r)
		require.False(t, ok)
	})

	t.Run("ReaderInfo", func(t *testing.T) {
		name := tmpPath()

		err := env.UploadSlice(name, []exampleRow{
			{"foo", 1},
			{"bar", 2},
		})
		require.NoError(t, err)

		r, err := env.YT.ReadTable(env.Ctx, name, nil)
		require.NoError(t, err)
		defer r.Close()

		startRowIndex, ok := yt.StartRowIndex(r)
		require.True(t, ok)
		require.Equal(t, int64(0), startRowIndex)

		r, err = env.YT.ReadTable(env.Ctx, name+"[#1:]", nil)
		require.NoError(t, err)
		defer r.Close()

		startRowIndex, ok = yt.StartRowIndex(r)
		require.True(t, ok)
		require.Equal(t, int64(1), startRowIndex)

		rowCount, ok := yt.ApproximateRowCount(r)
		require.True(t, ok)
		require.Greater(t, rowCount, int64(0))
	})
}

func TestHighLevelTableWriter(t *testing.T) {
	t.Parallel()

	env, cancel := yttest.NewEnv(t)
	defer cancel()

	checkTable := func(t *testing.T, path ypath.Path, rowCount int, expectedSchema schema.Schema) {
		t.Helper()

		var realRowCount int
		require.NoError(t, env.YT.GetNode(env.Ctx, path.Attr("row_count"), &realRowCount, nil))
		require.Equal(t, rowCount, realRowCount, "table size differs from number of written rows")

		var realSchema schema.Schema
		require.NoError(t, env.YT.GetNode(env.Ctx, path.Attr("schema"), &realSchema, nil))
		require.Truef(t, expectedSchema.Equal(realSchema), "%v != %v", expectedSchema, realSchema)
	}

	t.Run("BigWrite", func(t *testing.T) {
		tmpTableName := tmpPath()

		w, err := yt.WriteTable(env.Ctx, env.YT, tmpTableName, yt.WithBatchSize(100))
		require.NoError(t, err)
		defer func() { _ = w.Rollback() }()

		const testSize = 1024
		for i := 0; i < testSize; i++ {
			require.NoError(t, w.Write(exampleRow{"foo", 1}))
		}

		require.NoError(t, w.Commit())

		checkTable(t, tmpTableName, testSize, schema.MustInfer(&exampleRow{}))
	})

	t.Run("EagerCreate", func(t *testing.T) {
		tmpTableName := tmpPath()

		w, err := yt.WriteTable(env.Ctx, env.YT, tmpTableName,
			yt.WithCreateOptions(yt.WithInferredSchema(&exampleRow{})))
		require.NoError(t, err)
		defer func() { _ = w.Rollback() }()

		checkTable(t, tmpTableName, 0, schema.MustInfer(&exampleRow{}))

		const testSize = 1024
		for i := 0; i < testSize; i++ {
			require.NoError(t, w.Write(exampleRow{"foo", 1}))
		}

		require.NoError(t, w.Commit())

		checkTable(t, tmpTableName, testSize, schema.MustInfer(&exampleRow{}))
	})

	t.Run("ExistingTable", func(t *testing.T) {
		tmpTableName := tmpPath()

		_, err := yt.CreateTable(env.Ctx, env.YT, tmpTableName)
		require.NoError(t, err)

		w, err := yt.WriteTable(env.Ctx, env.YT, tmpTableName, yt.WithExistingTable())
		require.NoError(t, err)
		defer func() { _ = w.Rollback() }()

		checkTable(t, tmpTableName, 0, schema.Schema{Strict: ptr.Bool(false)})

		const testSize = 1024
		for i := 0; i < testSize; i++ {
			require.NoError(t, w.Write(exampleRow{"foo", 1}))
		}

		require.NoError(t, w.Commit())

		checkTable(t, tmpTableName, testSize, schema.Schema{Strict: ptr.Bool(false)})
	})
}

type testTimeTypes struct {
	D0 schema.Date
	D1 schema.Datetime
	D2 schema.Timestamp
	D3 schema.Interval
}

func TestTimeTables(t *testing.T) {
	t.Parallel()

	env, cancel := yttest.NewEnv(t)
	defer cancel()

	rows := []testTimeTypes{
		{D0: 1, D1: 2, D2: 3, D3: 4},
	}

	path := env.TmpPath()
	require.NoError(t, env.UploadSlice(path, rows))

	var outRows []testTimeTypes
	require.NoError(t, env.DownloadSlice(path, &outRows))

	require.Equal(t, rows, outRows)
}
