GO_LIBRARY()

LICENSE(
    Apache-2.0 AND
    BSD-3-Clause AND
    MIT
)

SRCS(
    compress.go
    compress_go120.go
    transport.go
)

GO_TEST_SRCS(
    asserts_test.go
    compress_test.go
    transport_test.go
)

GO_XTEST_SRCS(examples_test.go)

END()

RECURSE(
    gotest
    writer
)
