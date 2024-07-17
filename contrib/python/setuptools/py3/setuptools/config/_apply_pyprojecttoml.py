"""Translation layer between pyproject config and setuptools distribution and
metadata objects.

The distribution and metadata objects are modeled after (an old version of)
core metadata, therefore configs in the format specified for ``pyproject.toml``
need to be processed before being applied.

**PRIVATE MODULE**: API reserved for setuptools internal usage only.
"""

from __future__ import annotations

import logging
import os
from email.headerregistry import Address
from functools import partial, reduce
from inspect import cleandoc
from itertools import chain
from types import MappingProxyType
from typing import (
    TYPE_CHECKING,
    Any,
    Callable,
    Dict,
    Mapping,
    Union,
)
from .._path import StrPath
from ..errors import RemovedConfigError
from ..warnings import SetuptoolsWarning

if TYPE_CHECKING:
    from distutils.dist import _OptionsList
    from setuptools._importlib import metadata
    from setuptools.dist import Distribution

EMPTY: Mapping = MappingProxyType({})  # Immutable dict-like
_ProjectReadmeValue = Union[str, Dict[str, str]]
_CorrespFn = Callable[["Distribution", Any, StrPath], None]
_Correspondence = Union[str, _CorrespFn]

_logger = logging.getLogger(__name__)


def apply(dist: Distribution, config: dict, filename: StrPath) -> Distribution:
    """Apply configuration dict read with :func:`read_configuration`"""

    if not config:
        return dist  # short-circuit unrelated pyproject.toml file

    root_dir = os.path.dirname(filename) or "."

    _apply_project_table(dist, config, root_dir)
    _apply_tool_table(dist, config, filename)

    current_directory = os.getcwd()
    os.chdir(root_dir)
    try:
        dist._finalize_requires()
        dist._finalize_license_files()
    finally:
        os.chdir(current_directory)

    return dist


def _apply_project_table(dist: Distribution, config: dict, root_dir: StrPath):
    project_table = config.get("project", {}).copy()
    if not project_table:
        return  # short-circuit

    _handle_missing_dynamic(dist, project_table)
    _unify_entry_points(project_table)

    for field, value in project_table.items():
        norm_key = json_compatible_key(field)
        corresp = PYPROJECT_CORRESPONDENCE.get(norm_key, norm_key)
        if callable(corresp):
            corresp(dist, value, root_dir)
        else:
            _set_config(dist, corresp, value)


def _apply_tool_table(dist: Distribution, config: dict, filename: StrPath):
    tool_table = config.get("tool", {}).get("setuptools", {})
    if not tool_table:
        return  # short-circuit

    for field, value in tool_table.items():
        norm_key = json_compatible_key(field)

        if norm_key in TOOL_TABLE_REMOVALS:
            suggestion = cleandoc(TOOL_TABLE_REMOVALS[norm_key])
            msg = f"""
            The parameter `tool.setuptools.{field}` was long deprecated
            and has been removed from `pyproject.toml`.
            """
            raise RemovedConfigError("\n".join([cleandoc(msg), suggestion]))

        norm_key = TOOL_TABLE_RENAMES.get(norm_key, norm_key)
        _set_config(dist, norm_key, value)

    _copy_command_options(config, dist, filename)


def _handle_missing_dynamic(dist: Distribution, project_table: dict):
    """Be temporarily forgiving with ``dynamic`` fields not listed in ``dynamic``"""
    dynamic = set(project_table.get("dynamic", []))
    for field, getter in _PREVIOUSLY_DEFINED.items():
        if not (field in project_table or field in dynamic):
            value = getter(dist)
            if value:
                _MissingDynamic.emit(field=field, value=value)
                project_table[field] = _RESET_PREVIOUSLY_DEFINED.get(field)


def json_compatible_key(key: str) -> str:
    """As defined in :pep:`566#json-compatible-metadata`"""
    return key.lower().replace("-", "_")


def _set_config(dist: Distribution, field: str, value: Any):
    setter = getattr(dist.metadata, f"set_{field}", None)
    if setter:
        setter(value)
    elif hasattr(dist.metadata, field) or field in SETUPTOOLS_PATCHES:
        setattr(dist.metadata, field, value)
    else:
        setattr(dist, field, value)


_CONTENT_TYPES = {
    ".md": "text/markdown",
    ".rst": "text/x-rst",
    ".txt": "text/plain",
}


def _guess_content_type(file: str) -> str | None:
    _, ext = os.path.splitext(file.lower())
    if not ext:
        return None

    if ext in _CONTENT_TYPES:
        return _CONTENT_TYPES[ext]

    valid = ", ".join(f"{k} ({v})" for k, v in _CONTENT_TYPES.items())
    msg = f"only the following file extensions are recognized: {valid}."
    raise ValueError(f"Undefined content type for {file}, {msg}")


def _long_description(dist: Distribution, val: _ProjectReadmeValue, root_dir: StrPath):
    from setuptools.config import expand

    file: str | tuple[()]
    if isinstance(val, str):
        file = val
        text = expand.read_files(file, root_dir)
        ctype = _guess_content_type(file)
    else:
        file = val.get("file") or ()
        text = val.get("text") or expand.read_files(file, root_dir)
        ctype = val["content-type"]

    _set_config(dist, "long_description", text)

    if ctype:
        _set_config(dist, "long_description_content_type", ctype)

    if file:
        dist._referenced_files.add(file)


def _license(dist: Distribution, val: dict, root_dir: StrPath):
    from setuptools.config import expand

    if "file" in val:
        _set_config(dist, "license", expand.read_files([val["file"]], root_dir))
        dist._referenced_files.add(val["file"])
    else:
        _set_config(dist, "license", val["text"])


def _people(dist: Distribution, val: list[dict], _root_dir: StrPath, kind: str):
    field = []
    email_field = []
    for person in val:
        if "name" not in person:
            email_field.append(person["email"])
        elif "email" not in person:
            field.append(person["name"])
        else:
            addr = Address(display_name=person["name"], addr_spec=person["email"])
            email_field.append(str(addr))

    if field:
        _set_config(dist, kind, ", ".join(field))
    if email_field:
        _set_config(dist, f"{kind}_email", ", ".join(email_field))


def _project_urls(dist: Distribution, val: dict, _root_dir):
    _set_config(dist, "project_urls", val)


def _python_requires(dist: Distribution, val: dict, _root_dir):
    from setuptools.extern.packaging.specifiers import SpecifierSet

    _set_config(dist, "python_requires", SpecifierSet(val))


def _dependencies(dist: Distribution, val: list, _root_dir):
    if getattr(dist, "install_requires", []):
        msg = "`install_requires` overwritten in `pyproject.toml` (dependencies)"
        SetuptoolsWarning.emit(msg)
    dist.install_requires = val


def _optional_dependencies(dist: Distribution, val: dict, _root_dir):
    existing = getattr(dist, "extras_require", None) or {}
    dist.extras_require = {**existing, **val}


def _unify_entry_points(project_table: dict):
    project = project_table
    entry_points = project.pop("entry-points", project.pop("entry_points", {}))
    renaming = {"scripts": "console_scripts", "gui_scripts": "gui_scripts"}
    for key, value in list(project.items()):  # eager to allow modifications
        norm_key = json_compatible_key(key)
        if norm_key in renaming:
            # Don't skip even if value is empty (reason: reset missing `dynamic`)
            entry_points[renaming[norm_key]] = project.pop(key)

    if entry_points:
        project["entry-points"] = {
            name: [f"{k} = {v}" for k, v in group.items()]
            for name, group in entry_points.items()
            if group  # now we can skip empty groups
        }
        # Sometimes this will set `project["entry-points"] = {}`, and that is
        # intentional (for resetting configurations that are missing `dynamic`).


def _copy_command_options(pyproject: dict, dist: Distribution, filename: StrPath):
    tool_table = pyproject.get("tool", {})
    cmdclass = tool_table.get("setuptools", {}).get("cmdclass", {})
    valid_options = _valid_command_options(cmdclass)

    cmd_opts = dist.command_options
    for cmd, config in pyproject.get("tool", {}).get("distutils", {}).items():
        cmd = json_compatible_key(cmd)
        valid = valid_options.get(cmd, set())
        cmd_opts.setdefault(cmd, {})
        for key, value in config.items():
            key = json_compatible_key(key)
            cmd_opts[cmd][key] = (str(filename), value)
            if key not in valid:
                # To avoid removing options that are specified dynamically we
                # just log a warn...
                _logger.warning(f"Command option {cmd}.{key} is not defined")


def _valid_command_options(cmdclass: Mapping = EMPTY) -> dict[str, set[str]]:
    from .._importlib import metadata
    from setuptools.dist import Distribution

    valid_options = {"global": _normalise_cmd_options(Distribution.global_options)}

    unloaded_entry_points = metadata.entry_points(group='distutils.commands')
    loaded_entry_points = (_load_ep(ep) for ep in unloaded_entry_points)
    entry_points = (ep for ep in loaded_entry_points if ep)
    for cmd, cmd_class in chain(entry_points, cmdclass.items()):
        opts = valid_options.get(cmd, set())
        opts = opts | _normalise_cmd_options(getattr(cmd_class, "user_options", []))
        valid_options[cmd] = opts

    return valid_options


def _load_ep(ep: metadata.EntryPoint) -> tuple[str, type] | None:
    # Ignore all the errors
    try:
        return (ep.name, ep.load())
    except Exception as ex:
        msg = f"{ex.__class__.__name__} while trying to load entry-point {ep.name}"
        _logger.warning(f"{msg}: {ex}")
        return None


def _normalise_cmd_option_key(name: str) -> str:
    return json_compatible_key(name).strip("_=")


def _normalise_cmd_options(desc: _OptionsList) -> set[str]:
    return {_normalise_cmd_option_key(fancy_option[0]) for fancy_option in desc}


def _get_previous_entrypoints(dist: Distribution) -> dict[str, list]:
    ignore = ("console_scripts", "gui_scripts")
    value = getattr(dist, "entry_points", None) or {}
    return {k: v for k, v in value.items() if k not in ignore}


def _get_previous_scripts(dist: Distribution) -> list | None:
    value = getattr(dist, "entry_points", None) or {}
    return value.get("console_scripts")


def _get_previous_gui_scripts(dist: Distribution) -> list | None:
    value = getattr(dist, "entry_points", None) or {}
    return value.get("gui_scripts")


def _attrgetter(attr):
    """
    Similar to ``operator.attrgetter`` but returns None if ``attr`` is not found
    >>> from types import SimpleNamespace
    >>> obj = SimpleNamespace(a=42, b=SimpleNamespace(c=13))
    >>> _attrgetter("a")(obj)
    42
    >>> _attrgetter("b.c")(obj)
    13
    >>> _attrgetter("d")(obj) is None
    True
    """
    return partial(reduce, lambda acc, x: getattr(acc, x, None), attr.split("."))


def _some_attrgetter(*items):
    """
    Return the first "truth-y" attribute or None
    >>> from types import SimpleNamespace
    >>> obj = SimpleNamespace(a=42, b=SimpleNamespace(c=13))
    >>> _some_attrgetter("d", "a", "b.c")(obj)
    42
    >>> _some_attrgetter("d", "e", "b.c", "a")(obj)
    13
    >>> _some_attrgetter("d", "e", "f")(obj) is None
    True
    """

    def _acessor(obj):
        values = (_attrgetter(i)(obj) for i in items)
        return next((i for i in values if i is not None), None)

    return _acessor


PYPROJECT_CORRESPONDENCE: dict[str, _Correspondence] = {
    "readme": _long_description,
    "license": _license,
    "authors": partial(_people, kind="author"),
    "maintainers": partial(_people, kind="maintainer"),
    "urls": _project_urls,
    "dependencies": _dependencies,
    "optional_dependencies": _optional_dependencies,
    "requires_python": _python_requires,
}

TOOL_TABLE_RENAMES = {"script_files": "scripts"}
TOOL_TABLE_REMOVALS = {
    "namespace_packages": """
        Please migrate to implicit native namespaces instead.
        See https://packaging.python.org/en/latest/guides/packaging-namespace-packages/.
        """,
}

SETUPTOOLS_PATCHES = {
    "long_description_content_type",
    "project_urls",
    "provides_extras",
    "license_file",
    "license_files",
}

_PREVIOUSLY_DEFINED = {
    "name": _attrgetter("metadata.name"),
    "version": _attrgetter("metadata.version"),
    "description": _attrgetter("metadata.description"),
    "readme": _attrgetter("metadata.long_description"),
    "requires-python": _some_attrgetter("python_requires", "metadata.python_requires"),
    "license": _attrgetter("metadata.license"),
    "authors": _some_attrgetter("metadata.author", "metadata.author_email"),
    "maintainers": _some_attrgetter("metadata.maintainer", "metadata.maintainer_email"),
    "keywords": _attrgetter("metadata.keywords"),
    "classifiers": _attrgetter("metadata.classifiers"),
    "urls": _attrgetter("metadata.project_urls"),
    "entry-points": _get_previous_entrypoints,
    "scripts": _get_previous_scripts,
    "gui-scripts": _get_previous_gui_scripts,
    "dependencies": _attrgetter("install_requires"),
    "optional-dependencies": _attrgetter("extras_require"),
}


_RESET_PREVIOUSLY_DEFINED: dict = {
    # Fix improper setting: given in `setup.py`, but not listed in `dynamic`
    # dict: pyproject name => value to which reset
    "license": {},
    "authors": [],
    "maintainers": [],
    "keywords": [],
    "classifiers": [],
    "urls": {},
    "entry-points": {},
    "scripts": {},
    "gui-scripts": {},
    "dependencies": [],
    "optional-dependencies": {},
}


class _MissingDynamic(SetuptoolsWarning):
    _SUMMARY = "`{field}` defined outside of `pyproject.toml` is ignored."

    _DETAILS = """
    The following seems to be defined outside of `pyproject.toml`:

    `{field} = {value!r}`

    According to the spec (see the link below), however, setuptools CANNOT
    consider this value unless `{field}` is listed as `dynamic`.

    https://packaging.python.org/en/latest/specifications/pyproject-toml/#declaring-project-metadata-the-project-table

    To prevent this problem, you can list `{field}` under `dynamic` or alternatively
    remove the `[project]` table from your file and rely entirely on other means of
    configuration.
    """
    # TODO: Consider removing this check in the future?
    #       There is a trade-off here between improving "debug-ability" and the cost
    #       of running/testing/maintaining these unnecessary checks...

    @classmethod
    def details(cls, field: str, value: Any) -> str:
        return cls._DETAILS.format(field=field, value=value)
