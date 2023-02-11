package main

import (
	"bufio"
	"bytes"
	"flag"
	"fmt"
	"go/format"
	"io"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"
)

var (
	flagYTRoot = flag.String("yt-root", "", "root of yt source code directory")
	flagOutput = flag.String("out", "", "")
)

func findHeaders() (headers []string) {
	err := filepath.Walk(*flagYTRoot,
		func(path string, info os.FileInfo, err error) error {
			if err != nil {
				return err
			}

			if !info.IsDir() && strings.HasSuffix(path, ".h") {
				headers = append(headers, path)
			}

			return nil
		})

	if err != nil {
		_, _ = fmt.Fprintf(os.Stderr, "walk: %+v\n", err)
		os.Exit(1)
	}

	return
}

type errorCode struct {
	name  string
	value int
}

var (
	reStart = regexp.MustCompile(`DEFINE_ENUM\(EErrorCode,`)
	reValue = regexp.MustCompile(`\(\((\w+)\)\s*\((\d+)\)\)`)
	reEnd   = regexp.MustCompile(`\);`)
)

func scanErrorCodes(lines []string) (ec []errorCode) {
	inside := false

	for _, l := range lines {
		if inside {
			if match := reValue.FindStringSubmatch(l); match != nil {
				code, _ := strconv.Atoi(match[2])

				ec = append(ec, errorCode{
					name:  match[1],
					value: code,
				})
			} else if reEnd.MatchString(l) {
				inside = false
			}
		} else {
			if reStart.MatchString(l) {
				inside = true
			}
		}
	}

	return
}

func fatalf(msg string, args ...interface{}) {
	line := fmt.Sprintf(msg, args...)
	if line[len(line)-1] != '\n' {
		line += "\n"
	}

	_, _ = os.Stderr.WriteString(line)
	os.Exit(2)
}

func readLines(path string) (lines []string) {
	file, err := os.Open(path)
	if err != nil {
		fatalf("%+v", err)
	}
	defer func() { _ = file.Close() }()

	reader := bufio.NewReader(file)

	var line string
	for {
		line, err = reader.ReadString('\n')
		if err != nil && err != io.EOF {
			_, _ = fmt.Fprintf(os.Stderr, "readline: %+v\n", err)
			os.Exit(1)
		} else if err == io.EOF {
			break
		}

		lines = append(lines, line)
	}

	return
}

func main() {
	flag.Parse()

	var ec []errorCode
	for _, h := range findHeaders() {
		lines := readLines(h)
		ec = append(ec, scanErrorCodes(lines)...)
	}

	rename := func(e errorCode) errorCode {
		e.name = strings.Replace(e.name, "RPC", "Rpc", -1)

		switch e.value {
		case 108:
			return errorCode{"RPCRequestQueueSizeLimitExceeded", e.value}
		case 109:
			return errorCode{"RPCAuthenticationError", e.value}
		case 802:
			return errorCode{"InvalidElectionEpoch", e.value}
		case 800:
			return errorCode{"InvalidElectionState", e.value}
		case 1915:
			return errorCode{"APINoSuchOperation", e.value}
		case 1916:
			return errorCode{"APINoSuchJob", e.value}
		default:
			return e
		}
	}

	for i, e := range ec {
		ec[i] = rename(e)
	}

	sort.Slice(ec, func(i, j int) bool {
		return ec[i].value < ec[j].value
	})

	var buf bytes.Buffer

	fmt.Fprintln(&buf, "// Code generated by yt-gen-error-code. DO NOT EDIT.")
	fmt.Fprintln(&buf, "package yterrors")
	fmt.Fprintln(&buf, "import \"fmt\"")

	fmt.Fprintln(&buf, "const (")

	emitted := map[int]struct{}{}
	for _, e := range ec {
		if _, ok := emitted[e.value]; ok {
			continue
		}

		emitted[e.value] = struct{}{}
		fmt.Fprintf(&buf, "Code%s ErrorCode = %d\n", e.name, e.value)
	}
	fmt.Fprintln(&buf, ")")

	fmt.Fprintln(&buf, "func (e ErrorCode) String() string {")
	fmt.Fprintln(&buf, "switch e {")

	emitted = map[int]struct{}{}
	for _, e := range ec {
		if _, ok := emitted[e.value]; ok {
			continue
		}

		emitted[e.value] = struct{}{}
		fmt.Fprintf(&buf, "case Code%s: return %q\n", e.name, e.name)
	}

	fmt.Fprintln(&buf, "default:")

	unknown := "return fmt.Sprintf(\"UnknownCode%d\", int(e))"
	fmt.Fprintln(&buf, unknown)

	fmt.Fprintln(&buf, "}")
	fmt.Fprintln(&buf, "}")

	fmtbuf, err := format.Source(buf.Bytes())
	if err != nil {
		fatalf("%v", err)
	}

	if err = os.WriteFile(*flagOutput, fmtbuf, 0644); err != nil {
		fatalf("%v", err)
	}
}
