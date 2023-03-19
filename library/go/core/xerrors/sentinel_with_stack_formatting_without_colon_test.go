package xerrors

import (
	"testing"

	"go.ytsaurus.tech/library/go/core/xerrors/assertxerrors"
)

func TestSentinelWithStackTraceFormattingWithoutColon(t *testing.T) {
	constructor := func(t *testing.T) error {
		err := NewSentinel("sentinel")
		return err.WithStackTrace()
	}
	expected := assertxerrors.Expectations{
		ExpectedS: "sentinel",
		ExpectedV: "sentinel",
		Frames: assertxerrors.NewStackTraceModeExpectation(`
sentinel:
    go.ytsaurus.tech/library/go/core/xerrors.TestSentinelWithStackTraceFormattingWithoutColon.func1
        library/go/core/xerrors/sentinel_with_stack_formatting_without_colon_test.go:12
`,
		),
		Stacks: assertxerrors.NewStackTraceModeExpectation(`
sentinel:
    go.ytsaurus.tech/library/go/core/xerrors.TestSentinelWithStackTraceFormattingWithoutColon.func1
        library/go/core/xerrors/sentinel_with_stack_formatting_without_colon_test.go:12
    go.ytsaurus.tech/library/go/core/xerrors/assertxerrors.RunTestsPerMode.func1
        /home/sidh/devel/go/src/go.ytsaurus.tech/library/go/core/xerrors/assertxerrors/assertxerrors.go:18
    testing.tRunner
        /home/sidh/.ya/tools/v4/774223543/src/testing/testing.go:1127
`,
			3, 4, 5, 6,
		),
		StackThenFrames: assertxerrors.NewStackTraceModeExpectation(`
sentinel:
    go.ytsaurus.tech/library/go/core/xerrors.TestSentinelWithStackTraceFormattingWithoutColon.func1
        library/go/core/xerrors/sentinel_with_stack_formatting_without_colon_test.go:12
    go.ytsaurus.tech/library/go/core/xerrors/assertxerrors.RunTestsPerMode.func1
        /home/sidh/devel/go/src/go.ytsaurus.tech/library/go/core/xerrors/assertxerrors/assertxerrors.go:18
    testing.tRunner
        /home/sidh/.ya/tools/v4/774223543/src/testing/testing.go:1127
`,
			3, 4, 5, 6,
		),
		StackThenNothing: assertxerrors.NewStackTraceModeExpectation(`
sentinel:
    go.ytsaurus.tech/library/go/core/xerrors.TestSentinelWithStackTraceFormattingWithoutColon.func1
        library/go/core/xerrors/sentinel_with_stack_formatting_without_colon_test.go:12
    go.ytsaurus.tech/library/go/core/xerrors/assertxerrors.RunTestsPerMode.func1
        /home/sidh/devel/go/src/go.ytsaurus.tech/library/go/core/xerrors/assertxerrors/assertxerrors.go:18
    testing.tRunner
        /home/sidh/.ya/tools/v4/774223543/src/testing/testing.go:1127
`,
			3, 4, 5, 6,
		),
		Nothing: assertxerrors.NewStackTraceModeExpectation("sentinel"),
	}
	assertxerrors.RunTestsPerMode(t, expected, constructor)
}
