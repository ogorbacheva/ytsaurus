# Generated by devtools/yamaker (pypi).

PY3_LIBRARY()

PROVIDES(python_traitlets)

VERSION(5.14.1)

LICENSE(BSD-3-Clause)

NO_LINT()

PY_SRCS(
    TOP_LEVEL
    traitlets/__init__.py
    traitlets/_version.py
    traitlets/config/__init__.py
    traitlets/config/application.py
    traitlets/config/argcomplete_config.py
    traitlets/config/configurable.py
    traitlets/config/loader.py
    traitlets/config/manager.py
    traitlets/config/sphinxdoc.py
    traitlets/log.py
    traitlets/tests/__init__.py
    traitlets/tests/utils.py
    traitlets/traitlets.py
    traitlets/utils/__init__.py
    traitlets/utils/bunch.py
    traitlets/utils/decorators.py
    traitlets/utils/descriptions.py
    traitlets/utils/getargspec.py
    traitlets/utils/importstring.py
    traitlets/utils/nested_update.py
    traitlets/utils/sentinel.py
    traitlets/utils/text.py
    traitlets/utils/warnings.py
)

RESOURCE_FILES(
    PREFIX contrib/python/traitlets/py3/
    .dist-info/METADATA
    .dist-info/top_level.txt
    traitlets/py.typed
)

END()

RECURSE_FOR_TESTS(
    tests
)
