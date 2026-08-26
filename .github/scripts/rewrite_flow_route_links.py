#!/usr/bin/env python3
"""Rewrite Flow links that still point at the pre-modular documentation tree."""

from __future__ import annotations

import argparse
import posixpath
import re
from pathlib import Path, PurePosixPath


LANGUAGES = ("ru", "en")
TEXT_SUFFIXES = {".md", ".yaml", ".yml"}
LINK_RE = re.compile(r"(?P<prefix>\]\()(?P<destination>[^)\n]+)(?P<suffix>\))")
FLOW_INCLUDE_RE = re.compile(
    r"(?P<prefix>\{% include(?: notitle)? \[[^]\n]*\]\()"
    r"{{\s*flow-docs-root\s*}}/{{\s*lang\s*}}/"
    r"(?P<route>[A-Za-z0-9_./-]+)"
    r"{{\s*docs-revision-query\s*}}"
    r"(?P<fragment>#[^)\s]+)?(?P<suffix>\)\s*%})"
)

CORE_ROUTES = {
    "user-guide/problems/jobshell-and-slowjobs": "how-to-guides/problems/jobshell-and-slowjobs",
    "user-guide/data-processing/operations/vanilla": "reference/data-processing/operations/vanilla",
    "user-guide/dynamic-tables/queues": "reference/dynamic-tables/queues",
    "user-guide/dynamic-tables/sorted-dynamic-tables": "concepts/dynamic-tables/sorted-dynamic-tables",
    "user-guide/storage/auth": "reference/storage/auth",
    "user-guide/storage/cypress-example": "how-to-guides/storage/cypress-example",
    "user-guide/storage/cypress": "concepts/storage/cypress",
    "user-guide/storage/data-types": "reference/storage/data-types",
    "user-guide/storage/static-tables": "concepts/storage/static-tables",
    "user-guide/proxy/about": "concepts/proxy/about",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository-root", type=Path, default=Path("."))
    parser.add_argument("--apply", action="store_true")
    return parser.parse_args()


def modular_url(module: str, route: str, fragment: str = "") -> str:
    return (
        f"{{{{ {module}-docs-root }}}}/{{{{ lang }}}}/{route}"
        f"{{{{ docs-revision-query }}}}{fragment}"
    )


def split_destination(destination: str) -> tuple[str, str]:
    path, marker, fragment = destination.partition("#")
    return path, f"#{fragment}" if marker else ""


def normalize_markdown_route(path: str) -> str:
    route = path.removesuffix(".md")
    parts = route.split("/")
    for index, part in enumerate(parts):
        if part in {"user-guide", "admin-guide"}:
            return "/".join(parts[index:])
    return ""


def rewrite_destination(destination: str) -> str:
    if destination.startswith(("http://", "https://", "mailto:")):
        return destination

    path, fragment = split_destination(destination)
    core_route = normalize_markdown_route(path)
    if core_route in CORE_ROUTES:
        return modular_url("core", CORE_ROUTES[core_route], fragment)

    if path.endswith("/flow/concepts/watermarks"):
        return modular_url("flow", "concepts/watermarks", fragment)

    if path == "../../flow/images/flow_noop_pipeline.png =600x230":
        return "../../../_images/flow_noop_pipeline.png =600x230"

    return destination


def assembled_relative(path: Path, docs: Path) -> PurePosixPath:
    public = docs / "public/flow"
    common = docs / "common/flow"
    if path.is_relative_to(public):
        relative = path.relative_to(public)
        return PurePosixPath(*relative.parts[1:])
    relative = path.relative_to(common)
    return PurePosixPath("common", "_includes", *relative.parts[2:])


def rewrite_text(text: str, path: Path, docs: Path) -> tuple[str, int]:
    rewrites = 0

    def replace_include(match: re.Match[str]) -> str:
        nonlocal rewrites
        route = match.group("route")
        language = path.relative_to(
            docs / ("public/flow" if path.is_relative_to(docs / "public/flow") else "common/flow")
        ).parts[0]
        target = docs / "public/flow" / language / f"{route}.md"
        if not target.is_file():
            return match.group(0)
        current = assembled_relative(path, docs)
        relative = posixpath.relpath(f"{route}.md", current.parent.as_posix())
        rewrites += 1
        return (
            match.group("prefix")
            + relative
            + (match.group("fragment") or "")
            + match.group("suffix")
        )

    text = FLOW_INCLUDE_RE.sub(replace_include, text)

    def replace(match: re.Match[str]) -> str:
        nonlocal rewrites
        destination = match.group("destination")
        rewritten = rewrite_destination(destination)
        if rewritten != destination:
            rewrites += 1
        return match.group("prefix") + rewritten + match.group("suffix")

    return LINK_RE.sub(replace, text), rewrites


def source_files(docs: Path) -> list[Path]:
    roots = [docs / "public/flow", docs / "common/flow"]
    return sorted(
        path
        for root in roots
        for path in root.rglob("*")
        if path.is_file() and path.suffix in TEXT_SUFFIXES
    )


def remaining_known_legacy_routes(files: list[Path]) -> list[str]:
    problems: list[str] = []
    for path in files:
        for line_number, line in enumerate(
            path.read_text(encoding="utf-8").splitlines(), start=1
        ):
            for route in CORE_ROUTES:
                if route in line:
                    problems.append(f"{path}:{line_number}: {route}")
    return problems


def main() -> int:
    args = parse_args()
    repository = args.repository_root.resolve()
    files = source_files(repository / "yt/docs")
    changed_files = 0
    rewrite_count = 0
    for path in files:
        source = path.read_text(encoding="utf-8")
        rendered, rewrites = rewrite_text(source, path, repository / "yt/docs")
        if not rewrites:
            continue
        changed_files += 1
        rewrite_count += rewrites
        if args.apply:
            path.write_text(rendered, encoding="utf-8")

    mode = "rewritten" if args.apply else "would rewrite"
    print(f"{mode} {rewrite_count} route(s) in {changed_files} file(s)")
    if args.apply:
        problems = remaining_known_legacy_routes(files)
        if problems:
            print("Known legacy Core routes remain after rewriting:")
            print("\n".join(problems))
            return 1
        modular_includes = [
            str(path)
            for path in files
            if FLOW_INCLUDE_RE.search(path.read_text(encoding="utf-8"))
        ]
        if modular_includes:
            print("Flow modular URLs remain inside include directives:")
            print("\n".join(modular_includes))
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
