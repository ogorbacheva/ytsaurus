GO_LIBRARY()

LICENSE(Apache-2.0)

SRCS(
    factory.go
)

GO_TEST_SRCS(factory_test.go)

END()

RECURSE(
    gotest
)
