GO_LIBRARY()

SRCS(
    registryutil.go
)

GO_TEST_SRCS(
    registryutil_test.go
    registryutils_bench_test.go
)

END()

RECURSE(
    gotest
)
