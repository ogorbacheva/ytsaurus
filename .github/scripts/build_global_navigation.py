#!/usr/bin/env python3
"""Generate the shared Diplodoc header in every registered docs project."""

from __future__ import annotations

import argparse
import difflib
import json
import re
import sys
from pathlib import Path
from typing import Any


DEFAULT_SOURCE_ROOT = Path("yt/docs")
DEFAULT_REGISTRY = Path(".github/docs-modules.json")
START_MARKER = "# BEGIN GENERATED GLOBAL NAVIGATION"
END_MARKER = "# END GENERATED GLOBAL NAVIGATION"
MODULE_NAME_RE = re.compile(r"^[a-z0-9][a-z0-9-]*$")
LANGUAGE_RE = re.compile(r"^[a-z][a-z0-9-]*$")
URL_RE = re.compile(
    r"^[ \t]*url:[ \t]*(?P<quote>['\"])(?P<url>.*)(?P=quote)[ \t]*$",
    re.MULTILINE,
)
ICON_RE = re.compile(
    r"^[ \t]*icon:[ \t]*(?P<quote>['\"])(?P<icon>.*)(?P=quote)[ \t]*$",
    re.MULTILINE,
)
DOCS_ROOT_RE = re.compile(
    r"\{\{\s*(?P<module>[a-z0-9][a-z0-9-]*)-docs-root\s*\}\}"
)
LOCAL_ROUTE_RE = re.compile(
    r"^\{\{\s*(?P<module>[a-z0-9][a-z0-9-]*)-docs-root\s*\}\}/"
    r"\{\{\s*lang\s*\}\}/"
    r"(?P<path>[A-Za-z0-9_./-]*)"
    r"\{\{\s*docs-revision-query\s*\}\}$"
)
REVISION_QUERY_PLACEHOLDER_RE = re.compile(
    r"\{\{\s*docs-revision-query\s*\}\}"
)


class NavigationError(RuntimeError):
    pass


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source-root",
        type=Path,
        default=DEFAULT_SOURCE_ROOT,
        help="Documentation source root containing public/ and navigation/.",
    )
    parser.add_argument(
        "--registry",
        type=Path,
        default=DEFAULT_REGISTRY,
        help="Modular documentation registry JSON.",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Fail when a toc.yaml differs from its language template.",
    )
    return parser.parse_args()


def load_registry(path: Path) -> list[dict[str, Any]]:
    if not path.is_file():
        raise NavigationError(f"Module registry is missing: {path}")
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise NavigationError(f"Cannot read module registry {path}: {error}") from error

    if document.get("schema_version") != 1:
        raise NavigationError("Unsupported module registry schema_version")
    modules = document.get("modules")
    if not isinstance(modules, list) or not modules:
        raise NavigationError("Module registry must contain a non-empty modules list")

    normalized: list[dict[str, Any]] = []
    names: set[str] = set()
    for index, module in enumerate(modules, start=1):
        if not isinstance(module, dict):
            raise NavigationError(f"Module #{index} must be an object")
        name = module.get("name")
        languages = module.get("languages")
        if not isinstance(name, str) or not MODULE_NAME_RE.fullmatch(name):
            raise NavigationError(f"Module #{index} has invalid name: {name!r}")
        if name in names:
            raise NavigationError(f"Duplicate module name: {name}")
        if (
            not isinstance(languages, list)
            or not languages
            or any(
                not isinstance(language, str)
                or not LANGUAGE_RE.fullmatch(language)
                for language in languages
            )
            or len(set(languages)) != len(languages)
        ):
            raise NavigationError(f"Module {name} has invalid languages")
        names.add(name)
        normalized.append({"name": name, "languages": languages})
    return normalized


def generated_region(template: str) -> str:
    return f"{START_MARKER}\n{template.rstrip()}\n{END_MARKER}\n"


def update_toc(source: str, template: str, path: Path) -> str:
    region = generated_region(template)
    if START_MARKER in source or END_MARKER in source:
        if (
            source.count(START_MARKER) != 1
            or source.count(END_MARKER) != 1
            or source.index(START_MARKER) > source.index(END_MARKER)
        ):
            raise NavigationError(f"{path}: malformed generated-navigation markers")
        prefix, marked = source.split(START_MARKER, 1)
        _, suffix = marked.split(END_MARKER, 1)
        return prefix + region + suffix.lstrip("\r\n")

    lines = source.splitlines(keepends=True)
    for index, line in enumerate(lines):
        if line.startswith("href:"):
            lines.insert(index + 1, region)
            return "".join(lines)
    raise NavigationError(f"{path}: no top-level href found")


def resolve_local_target(
    source_root: Path,
    module: str,
    language: str,
    route_path: str,
) -> Path:
    normalized_path = route_path[:-1] if route_path.endswith("/") else route_path
    if route_path:
        segments = normalized_path.split("/")
        if any(segment in {"", ".", ".."} for segment in segments):
            raise NavigationError(
                f"Navigation contains an unsafe route: {module}/{language}/{route_path}"
            )
        if Path(normalized_path).suffix in {".md", ".yaml", ".yml"}:
            raise NavigationError(
                f"Navigation route must omit its source extension: "
                f"{module}/{language}/{route_path}"
            )

    content_root = source_root / "public" / module / language
    if normalized_path:
        candidates = (
            content_root / f"{normalized_path}.md",
            content_root / f"{normalized_path}.yaml",
            content_root / f"{normalized_path}.yml",
            content_root / normalized_path / "index.md",
            content_root / normalized_path / "index.yaml",
            content_root / normalized_path / "index.yml",
        )
    else:
        candidates = (
            content_root / "index.md",
            content_root / "index.yaml",
            content_root / "index.yml",
        )
    resolved = [candidate for candidate in candidates if candidate.is_file()]
    if not resolved:
        relative = route_path or "index"
        raise NavigationError(
            f"Navigation links to a missing local page: "
            f"public/{module}/{language}/{relative}"
        )
    if len(resolved) > 1:
        paths = ", ".join(str(path) for path in resolved)
        raise NavigationError(
            f"Navigation route resolves to multiple source pages: {paths}"
        )
    return resolved[0]


def validate_template_routes(
    source_root: Path,
    template_path: Path,
    language: str,
    module_names: set[str],
) -> None:
    template = template_path.read_text(encoding="utf-8")
    for icon_match in ICON_RE.finditer(template):
        icon = icon_match.group("icon")
        icon_path = Path(icon)
        if (
            "://" in icon
            or icon_path.is_absolute()
            or any(part in {"", ".", ".."} for part in icon_path.parts)
        ):
            raise NavigationError(
                f"{template_path}: navigation icon must be a safe project-local path: "
                f"{icon}"
            )
        source = source_root / "navigation" / icon_path
        if not source.is_file():
            raise NavigationError(
                f"{template_path}: navigation icon is missing: {source}"
            )
    for url_match in URL_RE.finditer(template):
        url = url_match.group("url")
        roots = list(DOCS_ROOT_RE.finditer(url))
        if not roots:
            continue
        if len(roots) != 1:
            raise NavigationError(
                f"{template_path}: navigation URL must contain exactly one docs root: {url}"
            )

        referenced_module = roots[0].group("module")
        local_route = LOCAL_ROUTE_RE.fullmatch(url)
        if referenced_module not in module_names:
            if REVISION_QUERY_PLACEHOLDER_RE.search(url):
                raise NavigationError(
                    f"{template_path}: unregistered project {referenced_module} "
                    "must not receive docs-revision-query"
                )
            continue
        if local_route is None or local_route.group("module") != referenced_module:
            raise NavigationError(
                f"{template_path}: malformed registered-project route: {url}"
            )
        resolve_local_target(
            source_root,
            referenced_module,
            language,
            local_route.group("path"),
        )


def collect_updates(
    source_root: Path,
    registry: Path,
) -> list[tuple[Path, str, str]]:
    modules = load_registry(registry)
    module_names = {module["name"] for module in modules}
    templates: dict[str, str] = {}
    updates: list[tuple[Path, str, str]] = []

    for module in modules:
        for language in module["languages"]:
            template_path = source_root / "navigation" / f"{language}.yaml"
            if language not in templates:
                if not template_path.is_file():
                    raise NavigationError(
                        f"Global navigation template is missing: {template_path}"
                    )
                validate_template_routes(
                    source_root,
                    template_path,
                    language,
                    module_names,
                )
                templates[language] = template_path.read_text(encoding="utf-8")

            target = source_root / "public" / module["name"] / language / "toc.yaml"
            if not target.is_file():
                raise NavigationError(f"Navigation target is missing: {target}")
            current = target.read_text(encoding="utf-8")
            expected = update_toc(current, templates[language], target)
            updates.append((target, current, expected))
    return updates


def check(updates: list[tuple[Path, str, str]], source_root: Path) -> int:
    stale = False
    for path, current, expected in updates:
        if current == expected:
            continue
        stale = True
        relative = path.relative_to(source_root)
        print(f"Stale global navigation: {relative}")
        print(
            "".join(
                difflib.unified_diff(
                    current.splitlines(keepends=True),
                    expected.splitlines(keepends=True),
                    fromfile=str(relative),
                    tofile=f"{relative} (generated)",
                    n=2,
                )
            ),
            end="",
        )
    return int(stale)


def build(updates: list[tuple[Path, str, str]], source_root: Path) -> int:
    changed: list[Path] = []
    for path, current, expected in updates:
        if current == expected:
            continue
        path.write_text(expected, encoding="utf-8")
        changed.append(path.relative_to(source_root))
    if changed:
        print("Updated global navigation:")
        print("\n".join(f"  {path}" for path in changed))
    else:
        print("Global navigation is already up to date.")
    return 0


def main() -> int:
    args = parse_args()
    source_root = args.source_root.resolve()
    registry = args.registry.resolve()
    try:
        updates = collect_updates(source_root, registry)
        return check(updates, source_root) if args.check else build(updates, source_root)
    except (NavigationError, OSError) as error:
        print(f"Global navigation error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
