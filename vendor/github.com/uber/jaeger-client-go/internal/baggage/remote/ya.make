GO_LIBRARY()

LICENSE(Apache-2.0)

SRCS(
    options.go
    restriction_manager.go
)

GO_TEST_SRCS(restriction_manager_test.go)

END()

RECURSE(
    gotest
)
