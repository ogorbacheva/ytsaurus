# TODO(asaitgalin): Remove try/except when DEVTOOLS-3781 is done.
try:
    from driver_rpc_lib import Driver, BufferedStream, Response, configure_logging, configure_address_resolver, reopen_logs  # noqa
except ImportError:
    from .driver_rpc_lib import Driver, BufferedStream, Response, configure_logging, configure_address_resolver, reopen_logs  # noqa

from .driver import Request  # noqa
