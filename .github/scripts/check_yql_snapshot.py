#!/usr/bin/env python3
"""Verify that the checked-in YQL module is the recorded as-is snapshot."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path
from typing import Any


DEFAULT_SOURCE_ROOT = Path("yt/docs")
DEFAULT_MANIFEST = Path(".github/yql-snapshot.json")


class SnapshotError(RuntimeError):
    pass


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=DEFAULT_SOURCE_ROOT)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument(
        "--write",
        action="store_true",
        help="Refresh hashes after an intentional source-adapter update.",
    )
    return parser.parse_args()


def load_manifest(path: Path) -> dict[str, Any]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise SnapshotError(f"Cannot read snapshot manifest {path}: {error}") from error
    if document.get("schema_version") != 1 or document.get("policy") != "as-is":
        raise SnapshotError("YQL snapshot must use schema_version=1 and policy=as-is")
    if not isinstance(document.get("managed_roots"), dict):
        raise SnapshotError("YQL snapshot is missing managed_roots")
    return document


def digest(path: Path) -> str:
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def collect_inventory(
    source_root: Path, managed_roots: dict[str, dict[str, int]]
) -> dict[str, str]:
    inventory: dict[str, str] = {}
    for relative_root, expectations in managed_roots.items():
        root = source_root / relative_root
        if not root.is_dir() or root.is_symlink():
            raise SnapshotError(f"Managed YQL root is missing or is a symlink: {root}")
        files = []
        for path in sorted(root.rglob("*")):
            if path.is_symlink():
                raise SnapshotError(f"YQL snapshot contains a symlink: {path}")
            if path.is_file():
                files.append(path)
                relative = path.relative_to(source_root).as_posix()
                inventory[relative] = digest(path)
        expected_files = expectations.get("files")
        if len(files) != expected_files:
            raise SnapshotError(
                f"{relative_root}: expected {expected_files} files, found {len(files)}"
            )
        expected_markdown = expectations.get("markdown")
        markdown = sum(path.suffix == ".md" for path in files)
        if expected_markdown is not None and markdown != expected_markdown:
            raise SnapshotError(
                f"{relative_root}: expected {expected_markdown} Markdown files, "
                f"found {markdown}"
            )
    return inventory


def markdown_paths(root: Path) -> set[str]:
    return {
        path.relative_to(root).as_posix()
        for path in root.rglob("*.md")
        if path.is_file() and not path.is_symlink()
    }


def validate_locale_delta(source_root: Path, document: dict[str, Any]) -> None:
    ru = markdown_paths(source_root / "public/yql/ru/yql")
    en = markdown_paths(source_root / "public/yql/en/yql")
    expected = document["locale_markdown"]
    if len(ru & en) != expected["shared"]:
        raise SnapshotError(
            f"Expected {expected['shared']} shared RU/EN Markdown paths, "
            f"found {len(ru & en)}"
        )
    if sorted(ru - en) != expected["ru_only"]:
        raise SnapshotError("The recorded RU-only YQL page set has changed")
    if sorted(en - ru) != expected["en_only"]:
        raise SnapshotError("The recorded EN-only YQL page set has changed")


def validate_required_paths(source_root: Path, document: dict[str, Any]) -> None:
    for relative in document["required_paths"]:
        path = source_root / relative
        if not path.is_file() or path.is_symlink():
            raise SnapshotError(f"Required YQL snapshot file is missing: {relative}")


def compare_inventory(expected: Any, actual: dict[str, str]) -> None:
    if not isinstance(expected, dict):
        raise SnapshotError("Snapshot manifest has no generated inventory")
    expected_paths = set(expected)
    actual_paths = set(actual)
    missing = sorted(expected_paths - actual_paths)
    unexpected = sorted(actual_paths - expected_paths)
    changed = sorted(
        path for path in expected_paths & actual_paths if expected[path] != actual[path]
    )
    if missing or unexpected or changed:
        details = []
        if missing:
            details.append(f"missing: {', '.join(missing[:5])}")
        if unexpected:
            details.append(f"unexpected: {', '.join(unexpected[:5])}")
        if changed:
            details.append(f"changed: {', '.join(changed[:5])}")
        raise SnapshotError("YQL snapshot differs from its inventory (" + "; ".join(details) + ")")


def run(args: argparse.Namespace) -> None:
    source_root = args.source_root.resolve()
    manifest_path = args.manifest.resolve()
    document = load_manifest(manifest_path)
    inventory = collect_inventory(source_root, document["managed_roots"])
    validate_locale_delta(source_root, document)
    validate_required_paths(source_root, document)
    if args.write:
        document["inventory"] = inventory
        manifest_path.write_text(
            json.dumps(document, ensure_ascii=False, indent=2, sort_keys=False) + "\n",
            encoding="utf-8",
        )
        print(f"Recorded {len(inventory)} YQL snapshot files in {manifest_path}")
    else:
        compare_inventory(document.get("inventory"), inventory)
        print(f"Verified {len(inventory)} YQL snapshot files")


def main() -> int:
    try:
        run(parse_args())
    except (SnapshotError, OSError, KeyError, TypeError) as error:
        print(f"check-yql-snapshot: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
