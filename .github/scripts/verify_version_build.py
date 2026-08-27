#!/usr/bin/env python3
"""Verify one isolated component version build before matrix publication."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any


TEXT_SUFFIXES = {".html", ".json", ".md", ".txt", ".yaml", ".yml", ".yfm"}
VERSION_LABEL_RE = re.compile(r"^[0-9]+\.[0-9]+$")
SAFE_REVISION_RE = re.compile(r"^[A-Za-z0-9._-]+$")
UNRESOLVED_ROUTE_RE = re.compile(
    r"{{\s*(?:[a-z0-9][a-z0-9-]*-docs-root|docs-revision-query|lang)\s*}}"
)
CONFLICTING_QUERY_RE = re.compile(
    r"[?&]revision=[^&\"'\s<>]*&version=|"
    r"[?&]version=[^&\"'\s<>]*&revision="
)
URL_RE = re.compile(r"https://[^\s\"'<>\\]+")


class VerificationError(RuntimeError):
    pass


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--registry", type=Path, required=True)
    parser.add_argument("--input-root", type=Path, required=True)
    parser.add_argument("--html-root", type=Path, required=True)
    parser.add_argument("--md-root", type=Path, required=True)
    parser.add_argument("--module", required=True)
    parser.add_argument("--version-label", required=True)
    parser.add_argument("--artifact-version", required=True)
    parser.add_argument("--docs-revision", required=True)
    return parser.parse_args()


def load_registry(path: Path) -> dict[str, dict[str, Any]]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise VerificationError(f"Cannot read module registry {path}: {error}") from error
    modules = document.get("modules")
    if document.get("schema_version") != 1 or not isinstance(modules, list):
        raise VerificationError("Unsupported module registry")
    result: dict[str, dict[str, Any]] = {}
    for module in modules:
        if not isinstance(module, dict) or not isinstance(module.get("name"), str):
            raise VerificationError("Invalid module registry entry")
        result[module["name"]] = module
    return result


def require_file(path: Path, description: str) -> None:
    if not path.is_file():
        raise VerificationError(f"Missing {description}: {path}")


def read_text_tree(root: Path) -> list[tuple[Path, str]]:
    if not root.is_dir():
        raise VerificationError(f"Missing build directory: {root}")
    result: list[tuple[Path, str]] = []
    for path in sorted(root.rglob("*")):
        if path.is_file() and path.suffix in TEXT_SUFFIXES:
            result.append((path, path.read_text(encoding="utf-8")))
    return result


def verify(args: argparse.Namespace) -> None:
    if not VERSION_LABEL_RE.fullmatch(args.version_label):
        raise VerificationError(f"Invalid version label: {args.version_label!r}")
    if not SAFE_REVISION_RE.fullmatch(args.docs_revision):
        raise VerificationError(f"Invalid documentation revision: {args.docs_revision!r}")

    registry = load_registry(args.registry)
    module = registry.get(args.module)
    if module is None:
        raise VerificationError(f"Unknown module: {args.module}")

    input_module = args.input_root / args.module
    html_module = args.html_root / args.module
    md_module = args.md_root / args.module
    config = md_module / ".yfm"
    require_file(config, "md2md .yfm")
    require_file(html_module / "index.html", "HTML index")
    config_text = config.read_text(encoding="utf-8")
    for expected in (
        f"project-name: {module['project_name']}",
        "unrestrict-revision-access: true",
        "no-index: true",
    ):
        if expected not in config_text:
            raise VerificationError(f"Missing {expected!r} in {config}")

    manifest_path = args.input_root / "assembly-manifest.json"
    require_file(manifest_path, "assembly manifest")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    assembled = manifest.get("modules")
    if not isinstance(assembled, list) or len(assembled) != 1:
        raise VerificationError("Version build must assemble exactly one module")
    entry = assembled[0]
    if entry.get("name") != args.module:
        raise VerificationError("Assembly manifest module mismatch")
    route_queries = entry.get("route_queries")
    expected_query = f"?version={args.version_label}"
    if not isinstance(route_queries, dict) or set(route_queries) != set(registry):
        raise VerificationError("Assembly route map does not match module registry")
    for target, query in route_queries.items():
        expected = expected_query if target == args.module else ""
        if query != expected:
            raise VerificationError(
                f"Invalid route query for {target}: expected {expected!r}, found {query!r}"
            )

    for language in module["languages"]:
        require_file(md_module / language / "llms.txt", f"{language} llms.txt")
        require_file(md_module / language / "llms-full.txt", f"{language} llms-full.txt")
        toc = md_module / language / "toc.yaml"
        require_file(toc, f"{language} toc.yaml")
        toc_text = toc.read_text(encoding="utf-8")
        if expected_query not in toc_text:
            raise VerificationError(
                f"Selected version query is missing from {language} navigation"
            )

    for asset in ("logo-dark.svg", "logo-light.svg", "github.svg"):
        require_file(html_module / "_assets" / "navigation" / asset, asset)
        require_file(md_module / "_assets" / "navigation" / asset, asset)

    texts = read_text_tree(html_module) + read_text_tree(md_module)
    html_text = "\n".join(text for path, text in texts if html_module in path.parents)
    if args.artifact_version not in html_text:
        raise VerificationError(
            f"Artifact version {args.artifact_version!r} is absent from HTML output"
        )

    selected_version_url_seen = False
    for path, text in texts:
        if UNRESOLVED_ROUTE_RE.search(text):
            raise VerificationError(f"Unresolved modular route variable in {path}")
        if "?revision=" in text:
            raise VerificationError(f"Version build contains a revision query in {path}")
        if CONFLICTING_QUERY_RE.search(text):
            raise VerificationError(f"Conflicting revision/version query in {path}")
        for match in URL_RE.finditer(text):
            url = match.group(0).rstrip(".,);]")
            if "?version=" not in url:
                continue
            target = next(
                (
                    name
                    for name, candidate in registry.items()
                    if url.startswith(candidate["viewer_url"] + "/")
                ),
                None,
            )
            if target is None:
                continue
            if target != args.module or expected_query not in url:
                raise VerificationError(
                    f"Version query leaked into a foreign project URL: {url}"
                )
            selected_version_url_seen = True

    if not selected_version_url_seen:
        raise VerificationError("No selected component URL contains its version query")
    for root in (input_module, html_module, md_module):
        if any("internal" in path.parts for path in root.rglob("*")):
            raise VerificationError(f"Internal documentation leaked into {root}")


def main() -> int:
    args = parse_args()
    try:
        verify(args)
    except (OSError, json.JSONDecodeError, VerificationError) as error:
        print(f"Version build verification failed: {error}", file=sys.stderr)
        return 1
    print(
        f"verified {args.module} {args.version_label} at {args.docs_revision}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
