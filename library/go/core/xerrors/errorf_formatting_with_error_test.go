package xerrors

import (
	"testing"

	"go.ytsaurus.tech/library/go/core/xerrors/assertxerrors"
)

func TestErrorfFormattingWithError(t *testing.T) {
	constructor := func(t *testing.T) error {
		err := New("new")
		return Errorf("errorf: %w", err)
	}
	expected := assertxerrors.Expectations{
		ExpectedS: "errorf: new",
		ExpectedV: "errorf: new",
		Frames: assertxerrors.NewStackTraceModeExpectation(`
errorf:
    go.ytsaurus.tech/library/go/core/xerrors.TestErrorfFormattingWithError.func1
        library/go/core/xerrors/errorf_formatting_with_error_test.go:12
new
    go.ytsaurus.tech/library/go/core/xerrors.TestErrorfFormattingWithError.func1
        library/go/core/xerrors/errorf_formatting_with_error_test.go:11
`,
		),
		Stacks: assertxerrors.NewStackTraceModeExpectation(`
errorf:
    go.ytsaurus.tech/library/go/core/xerrors.TestErrorfFormattingWithError.func1
        library/go/core/xerrors/errorf_formatting_with_error_test.go:12
    go.ytsaurus.tech/library/go/core/xerrors/assertxerrors.RunTestsPerMode.func1
        /home/sidh/devel/go/src/go.ytsaurus.tech/library/go/core/xerrors/assertxerrors/assertxerrors.go:18
    testing.tRunner
        /home/sidh/.ya/tools/v4/774223543/src/testing/testing.go:1127
new
    go.ytsaurus.tech/library/go/core/xerrors.TestErrorfFormattingWithError.func1
        library/go/core/xerrors/errorf_formatting_with_error_test.go:11
    go.ytsaurus.tech/library/go/core/xerrors/assertxerrors.RunTestsPerMode.func1
        /home/sidh/devel/go/src/go.ytsaurus.tech/library/go/core/xerrors/assertxerrors/assertxerrors.go:18
    testing.tRunner
        /home/sidh/.ya/tools/v4/774223543/src/testing/testing.go:1127
`,
			3, 4, 5, 6, 10, 11, 12, 13,
		),
		StackThenFrames: assertxerrors.NewStackTraceModeExpectation(`
errorf:
    go.ytsaurus.tech/library/go/core/xerrors.TestErrorfFormattingWithError.func1
        library/go/core/xerrors/errorf_formatting_with_error_test.go:12
new
    go.ytsaurus.tech/library/go/core/xerrors.TestErrorfFormattingWithError.func1
        library/go/core/xerrors/errorf_formatting_with_error_test.go:11
    go.ytsaurus.tech/library/go/core/xerrors/assertxerrors.RunTestsPerMode.func1
        /home/sidh/devel/go/src/go.ytsaurus.tech/library/go/core/xerrors/assertxerrors/assertxerrors.go:18
    testing.tRunner
        /home/sidh/.ya/tools/v4/774223543/src/testing/testing.go:1127
`,
			6, 7, 8, 9,
		),
		StackThenNothing: assertxerrors.NewStackTraceModeExpectation(`
errorf: new
    go.ytsaurus.tech/library/go/core/xerrors.TestErrorfFormattingWithError.func1
        library/go/core/xerrors/errorf_formatting_with_error_test.go:11
    go.ytsaurus.tech/library/go/core/xerrors/assertxerrors.RunTestsPerMode.func1
        /home/sidh/devel/go/src/go.ytsaurus.tech/library/go/core/xerrors/assertxerrors/assertxerrors.go:18
    testing.tRunner
        /home/sidh/.ya/tools/v4/774223543/src/testing/testing.go:1127
`,
			3, 4, 5, 6,
		),
		Nothing: assertxerrors.NewStackTraceModeExpectation("errorf: new"),
	}
	assertxerrors.RunTestsPerMode(t, expected, constructor)
}
