PY3TEST()

OWNER(g:yatest)

TEST_SRCS(
    test_params.py
    test_reporter.py
)

PEERDIR(
    library/python/testing/custom_linter_util
)

END()
