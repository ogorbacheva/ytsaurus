# Generated by devtools/yamaker (pypi).

PY3_LIBRARY()

VERSION(6.108.2)

LICENSE(MPL-2.0)

PEERDIR(
    contrib/python/attrs
    contrib/python/sortedcontainers
)

NO_LINT()

NO_CHECK_IMPORTS(
    hypothesis.extra.*
)

PY_SRCS(
    TOP_LEVEL
    _hypothesis_ftz_detector.py
    _hypothesis_globals.py
    _hypothesis_pytestplugin.py
    hypothesis/__init__.py
    hypothesis/_settings.py
    hypothesis/configuration.py
    hypothesis/control.py
    hypothesis/core.py
    hypothesis/database.py
    hypothesis/entry_points.py
    hypothesis/errors.py
    hypothesis/extra/__init__.py
    hypothesis/extra/_array_helpers.py
    hypothesis/extra/_patching.py
    hypothesis/extra/array_api.py
    hypothesis/extra/cli.py
    hypothesis/extra/codemods.py
    hypothesis/extra/dateutil.py
    hypothesis/extra/django/__init__.py
    hypothesis/extra/django/_fields.py
    hypothesis/extra/django/_impl.py
    hypothesis/extra/dpcontracts.py
    hypothesis/extra/ghostwriter.py
    hypothesis/extra/lark.py
    hypothesis/extra/numpy.py
    hypothesis/extra/pandas/__init__.py
    hypothesis/extra/pandas/impl.py
    hypothesis/extra/pytestplugin.py
    hypothesis/extra/pytz.py
    hypothesis/extra/redis.py
    hypothesis/internal/__init__.py
    hypothesis/internal/cache.py
    hypothesis/internal/cathetus.py
    hypothesis/internal/charmap.py
    hypothesis/internal/compat.py
    hypothesis/internal/conjecture/__init__.py
    hypothesis/internal/conjecture/choicetree.py
    hypothesis/internal/conjecture/data.py
    hypothesis/internal/conjecture/datatree.py
    hypothesis/internal/conjecture/dfa/__init__.py
    hypothesis/internal/conjecture/dfa/lstar.py
    hypothesis/internal/conjecture/engine.py
    hypothesis/internal/conjecture/floats.py
    hypothesis/internal/conjecture/junkdrawer.py
    hypothesis/internal/conjecture/optimiser.py
    hypothesis/internal/conjecture/pareto.py
    hypothesis/internal/conjecture/shrinker.py
    hypothesis/internal/conjecture/shrinking/__init__.py
    hypothesis/internal/conjecture/shrinking/bytes.py
    hypothesis/internal/conjecture/shrinking/collection.py
    hypothesis/internal/conjecture/shrinking/common.py
    hypothesis/internal/conjecture/shrinking/floats.py
    hypothesis/internal/conjecture/shrinking/integer.py
    hypothesis/internal/conjecture/shrinking/ordering.py
    hypothesis/internal/conjecture/shrinking/string.py
    hypothesis/internal/conjecture/utils.py
    hypothesis/internal/coverage.py
    hypothesis/internal/detection.py
    hypothesis/internal/entropy.py
    hypothesis/internal/escalation.py
    hypothesis/internal/filtering.py
    hypothesis/internal/floats.py
    hypothesis/internal/healthcheck.py
    hypothesis/internal/intervalsets.py
    hypothesis/internal/observability.py
    hypothesis/internal/reflection.py
    hypothesis/internal/scrutineer.py
    hypothesis/internal/validation.py
    hypothesis/provisional.py
    hypothesis/reporting.py
    hypothesis/stateful.py
    hypothesis/statistics.py
    hypothesis/strategies/__init__.py
    hypothesis/strategies/_internal/__init__.py
    hypothesis/strategies/_internal/attrs.py
    hypothesis/strategies/_internal/collections.py
    hypothesis/strategies/_internal/core.py
    hypothesis/strategies/_internal/datetime.py
    hypothesis/strategies/_internal/deferred.py
    hypothesis/strategies/_internal/featureflags.py
    hypothesis/strategies/_internal/flatmapped.py
    hypothesis/strategies/_internal/functions.py
    hypothesis/strategies/_internal/ipaddress.py
    hypothesis/strategies/_internal/lazy.py
    hypothesis/strategies/_internal/misc.py
    hypothesis/strategies/_internal/numbers.py
    hypothesis/strategies/_internal/random.py
    hypothesis/strategies/_internal/recursive.py
    hypothesis/strategies/_internal/regex.py
    hypothesis/strategies/_internal/shared.py
    hypothesis/strategies/_internal/strategies.py
    hypothesis/strategies/_internal/strings.py
    hypothesis/strategies/_internal/types.py
    hypothesis/strategies/_internal/utils.py
    hypothesis/utils/__init__.py
    hypothesis/utils/conventions.py
    hypothesis/utils/dynamicvariables.py
    hypothesis/utils/terminal.py
    hypothesis/vendor/__init__.py
    hypothesis/vendor/pretty.py
    hypothesis/version.py
)

RESOURCE_FILES(
    PREFIX contrib/python/hypothesis/py3/
    .dist-info/METADATA
    .dist-info/entry_points.txt
    .dist-info/top_level.txt
    hypothesis/py.typed
    hypothesis/vendor/tlds-alpha-by-domain.txt
)

END()
