# Generated by devtools/yamaker (pypi).

PY3_LIBRARY()

VERSION(3.19.2)

LICENSE(MIT)

NO_COMPILER_WARNINGS()

NO_LINT()

NO_CHECK_IMPORTS(
    simplejson.ordered_dict
)

SRCS(
    simplejson/_speedups.c
)

PY_REGISTER(
    simplejson._speedups
)

PY_SRCS(
    TOP_LEVEL
    simplejson/__init__.py
    simplejson/compat.py
    simplejson/decoder.py
    simplejson/encoder.py
    simplejson/errors.py
    simplejson/ordered_dict.py
    simplejson/raw_json.py
    simplejson/scanner.py
    simplejson/tool.py
)

RESOURCE_FILES(
    PREFIX contrib/python/simplejson/py3/
    .dist-info/METADATA
    .dist-info/top_level.txt
)

END()

RECURSE_FOR_TESTS(
    tests
)
