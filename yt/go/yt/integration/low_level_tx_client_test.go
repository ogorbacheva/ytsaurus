package integration

import (
	"testing"

	"github.com/stretchr/testify/require"

	"a.yandex-team.ru/yt/go/yt"
	"a.yandex-team.ru/yt/go/yterrors"
)

func TestLowLevelTxClient(t *testing.T) {
	suite := NewSuite(t)

	RunClientTests(t, []ClientTest{
		{"CommitTx", suite.TestCommitTx},
		{"CommitTabletTx", suite.TestCommitTabletTx},
		{"AbortTx", suite.TestAbortTx},
		{"AbortTabletTx", suite.TestAbortTabletTx},
	})
}

func (s *Suite) TestCommitTx(t *testing.T, yc yt.Client) {
	t.Parallel()

	tx, err := yc.StartTx(s.Ctx, nil)
	require.NoError(t, err)

	err = yc.PingTx(s.Ctx, tx, nil)
	require.NoError(t, err)

	err = yc.CommitTx(s.Ctx, tx, nil)
	require.NoError(t, err)

	err = yc.PingTx(s.Ctx, tx, nil)
	require.Error(t, err)
	require.True(t, yterrors.ContainsErrorCode(err, yterrors.CodeNoSuchTransaction))
}

func (s *Suite) TestCommitTabletTx(t *testing.T, yc yt.Client) {
	t.Parallel()

	tx, err := yc.StartTabletTx(s.Ctx, &yt.StartTabletTxOptions{
		Type:   yt.TxTypeTablet,
		Sticky: true,
	})
	require.NoError(t, err)

	err = yc.PingTx(s.Ctx, tx, nil)
	require.NoError(t, err)

	err = yc.CommitTx(s.Ctx, tx, nil)
	require.NoError(t, err)

	err = yc.PingTx(s.Ctx, tx, nil)
	require.Error(t, err)
	require.True(t, yterrors.ContainsErrorCode(err, yterrors.CodeNoSuchTransaction))
}

func (s *Suite) TestAbortTx(t *testing.T, yc yt.Client) {
	t.Parallel()

	tx, err := yc.StartTx(s.Ctx, nil)
	require.NoError(t, err)

	err = yc.AbortTx(s.Ctx, tx, nil)
	require.NoError(t, err)

	err = yc.PingTx(s.Ctx, tx, nil)
	require.Error(t, err)
	require.True(t, yterrors.ContainsErrorCode(err, yterrors.CodeNoSuchTransaction))
}

func (s *Suite) TestAbortTabletTx(t *testing.T, yc yt.Client) {
	t.Parallel()

	tx, err := yc.StartTabletTx(s.Ctx, &yt.StartTabletTxOptions{
		Type:   yt.TxTypeTablet,
		Sticky: true,
	})
	require.NoError(t, err)

	err = yc.AbortTx(s.Ctx, tx, nil)
	require.NoError(t, err)

	err = yc.PingTx(s.Ctx, tx, nil)
	require.Error(t, err)
	require.True(t, yterrors.ContainsErrorCode(err, yterrors.CodeNoSuchTransaction))
}
