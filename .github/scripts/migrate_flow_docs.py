#!/usr/bin/env python3
"""Move the monolithic Flow corpus into the modular Diataxis layout.

This is a one-shot, auditable migration helper.  It intentionally changes only
file locations, include targets, and documentation routes; article prose is
left untouched.  The generated manifest records every source file, including
the legacy TOC fragments that are replaced by the module navigation.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import posixpath
import re
import shutil
import subprocess
import sys
from pathlib import Path, PurePosixPath
from typing import Any


LANGUAGES = ("ru", "en")
TEXT_SUFFIXES = {".md", ".yaml", ".yml"}
LINK_RE = re.compile(r"(?P<prefix>\]\()(?P<destination>[^)\n]+)(?P<suffix>\))")
SELF_TEMPLATE_RE = re.compile(
    r"\{\{\s*docs_root\s*\}\}/flow/"
    r"(?P<target>[A-Za-z0-9_./-]+\.md)(?P<fragment>#[^\s)\]<>\"']+)?"
)
HARDCODED_SELF_RE = re.compile(
    r"https://ytsaurus\.tech/docs/(?:ru/|en/)?flow/"
    r"(?P<target>[A-Za-z0-9_./-]+\.md)(?P<fragment>#[^\s)\]<>\"']+)?"
)
REDIRECT_FROM_RE = re.compile(r"^  - from: (?P<route>/\S+)$")


class MigrationError(RuntimeError):
    pass


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository-root", type=Path, default=Path("."))
    parser.add_argument(
        "--source-ref",
        default="origin/main",
        help="Git ref recorded as the source of the restored monolith corpus.",
    )
    parser.add_argument("--apply", action="store_true")
    return parser.parse_args()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def route_without_suffix(path: PurePosixPath) -> str:
    value = path.as_posix()
    for suffix in (".md", ".yaml", ".yml"):
        if value.endswith(suffix):
            return value[: -len(suffix)]
    return value


def content_destination(relative: PurePosixPath) -> PurePosixPath | None:
    """Map one old Flow content/include path to its new topical route."""
    if relative.suffix in {".yaml", ".yml"} and relative.name.startswith("toc"):
        return None
    if relative == PurePosixPath("presets.yaml"):
        return relative
    if relative.parts[0] == "images":
        return PurePosixPath("_images", *relative.parts[1:])
    if relative.parts[0] == "generated_docs":
        return PurePosixPath("reference", "configuration", *relative.parts[1:])

    exact = {
        "about.md": "concepts/overview.md",
        "start.md": "concepts/getting-started.md",
        "quickstart.md": "tutorials/quick-start.md",
        "tasks.md": "tutorials/task-examples.md",
        "faq.md": "reference/faq.md",
        "language-choice.md": "concepts/language-choice.md",
        "testing-integration-body.md": "_partials/testing-integration-body.md",
        "testing-test-param-body.md": "_partials/testing-test-param-body.md",
        "tools/cli.md": "reference/cli.md",
        "yql/getting-started.md": "tutorials/yql/quick-start.md",
        "yql/features.md": "reference/yql/features.md",
        "devops/auth.md": "how-to-guides/deployment/configure-authentication.md",
        "devops/flow-core-target.md": "concepts/deployment/flow-core-target.md",
        "devops/yt-sync-rules.md": "how-to-guides/deployment/yt-sync-rules.md",
        "devops/vanilla/initial-deploy.md": "how-to-guides/deployment/launch-vanilla.md",
        "devops/vanilla/pipeline-operations.md": "how-to-guides/operations/manage-pipeline.md",
        "devops/vanilla/diagnostics/logs.md": "how-to-guides/operations/view-logs.md",
        "devops/vanilla/releases.md": "how-to-guides/deployment/update-pipeline.md",
        "devops/vanilla/security.md": "how-to-guides/deployment/configure-security.md",
        "release/auth.md": "how-to-guides/deployment/configure-authentication.md",
        "release/basic-rules.md": "how-to-guides/deployment/basic-rules.md",
        "release/cli.md": "reference/cli.md",
        "release/flow-core-target.md": "concepts/deployment/flow-core-target.md",
        "release/launch-vanilla.md": "how-to-guides/deployment/launch-vanilla.md",
        "release/logs.md": "how-to-guides/operations/view-logs.md",
        "release/pipeline-operations.md": "how-to-guides/operations/manage-pipeline.md",
        "release/releases.md": "how-to-guides/deployment/update-pipeline.md",
        "release/security.md": "how-to-guides/deployment/configure-security.md",
        "release/yt-sync-rules.md": "how-to-guides/deployment/yt-sync-rules.md",
    }
    value = relative.as_posix()
    if value in exact:
        return PurePosixPath(exact[value])

    if relative.parts[0] == "concepts":
        return relative
    if relative.parts[0] == "connectors":
        if relative.name == "about.md":
            return PurePosixPath("concepts/connectors/overview.md")
        return PurePosixPath("reference/connectors", *relative.parts[1:])
    if relative.parts[0] == "contributor":
        return PurePosixPath("how-to-guides/contributing", *relative.parts[1:])
    if relative.parts[0] in {"cpp", "java", "python", "go"}:
        sdk = relative.parts[0]
        remainder = PurePosixPath(*relative.parts[1:])
        if remainder == PurePosixPath("getting-started.md"):
            return PurePosixPath("tutorials", sdk, "quick-start.md")
        if remainder.parts and remainder.parts[0] == "examples":
            return PurePosixPath("tutorials", sdk, *remainder.parts)
        return PurePosixPath("how-to-guides", sdk, *remainder.parts)

    raise MigrationError(f"No Flow destination rule for {relative}")


def load_core_redirects(path: Path) -> dict[str, dict[str, str]]:
    sections: dict[str, dict[str, str]] = {"common": {}, "ru": {}, "en": {}}
    section = ""
    lines = path.read_text(encoding="utf-8").splitlines()
    index = 0
    while index < len(lines):
        line = lines[index]
        if line in {"common:", "ru:", "en:"}:
            section = line[:-1]
            index += 1
            continue
        match = REDIRECT_FROM_RE.fullmatch(line)
        if not match:
            index += 1
            continue
        if not section or index + 1 >= len(lines):
            raise MigrationError(f"Malformed Core redirect at line {index + 1}")
        target_line = lines[index + 1]
        if not target_line.startswith("    to: /"):
            raise MigrationError(f"Malformed Core redirect at line {index + 1}")
        sections[section][match.group("route")] = target_line.removeprefix("    to: ")
        index += 3
    return sections


def assembled_location(kind: str, relative: PurePosixPath) -> PurePosixPath:
    if kind == "public":
        return relative
    return PurePosixPath("common/_includes", relative)


def relative_asset(
    *, kind: str, current: PurePosixPath, target: PurePosixPath
) -> str:
    current_location = assembled_location(kind, current)
    return posixpath.relpath(target.as_posix(), current_location.parent.as_posix())


def modular_url(module: str, route: str, fragment: str) -> str:
    suffix = fragment or ""
    return (
        f"{{{{ {module}-docs-root }}}}/{{{{ lang }}}}/{route}"
        f"{{{{ docs-revision-query }}}}{suffix}"
    )


def rewrite_destination(
    destination: str,
    *,
    language: str,
    old_file: PurePosixPath,
    kind: str,
    new_file: PurePosixPath,
    public_map: dict[tuple[str, str], PurePosixPath | None],
    include_map: dict[tuple[str, str], PurePosixPath | None],
    core_redirects: dict[str, dict[str, str]],
) -> str:
    if any(character.isspace() for character in destination):
        return destination
    path_part, marker, fragment_value = destination.partition("#")
    fragment = f"#{fragment_value}" if marker else ""
    if not path_part or path_part.startswith(("http://", "https://", "mailto:")):
        return destination
    if "{{" in path_part or path_part.startswith("/"):
        return destination

    resolved = PurePosixPath(
        posixpath.normpath(posixpath.join(old_file.parent.as_posix(), path_part))
    )
    parts = resolved.parts
    if len(parts) >= 3 and parts[:2] == (language, "flow"):
        old_relative = PurePosixPath(*parts[2:])
        mapped = public_map.get((language, old_relative.as_posix()))
        if mapped is None:
            return destination
        if mapped.parts[0] == "_images":
            return relative_asset(
                kind=kind, current=new_file, target=mapped
            ) + fragment
        return modular_url("flow", route_without_suffix(mapped), fragment)

    if len(parts) >= 4 and parts[:3] == (language, "_includes", "flow"):
        old_relative = PurePosixPath(*parts[3:])
        mapped = include_map.get((language, old_relative.as_posix()))
        if mapped is None:
            return destination
        target = PurePosixPath("common/_includes", mapped)
        return relative_asset(kind=kind, current=new_file, target=target) + fragment

    if len(parts) >= 2 and parts[0] == language and parts[1] == "yql":
        route = route_without_suffix(PurePosixPath(*parts[1:]))
        if route == "yql/index":
            route = "yql/"
        return modular_url("yql", route, fragment)

    if len(parts) >= 2 and parts[0] == language and parts[1] in {
        "user-guide",
        "admin-guide",
    }:
        old_route = "/" + route_without_suffix(PurePosixPath(*parts[1:]))
        effective = dict(core_redirects["common"])
        effective.update(core_redirects[language])
        target = effective.get(old_route)
        if target:
            return modular_url("core", target.lstrip("/"), fragment)

    return destination


def rewrite_text(
    text: str,
    *,
    language: str,
    old_file: PurePosixPath,
    kind: str,
    new_file: PurePosixPath,
    public_map: dict[tuple[str, str], PurePosixPath | None],
    include_map: dict[tuple[str, str], PurePosixPath | None],
    core_redirects: dict[str, dict[str, str]],
) -> str:
    def replace_link(match: re.Match[str]) -> str:
        rewritten = rewrite_destination(
            match.group("destination"),
            language=language,
            old_file=old_file,
            kind=kind,
            new_file=new_file,
            public_map=public_map,
            include_map=include_map,
            core_redirects=core_redirects,
        )
        return match.group("prefix") + rewritten + match.group("suffix")

    rendered = LINK_RE.sub(replace_link, text)

    def replace_self(match: re.Match[str]) -> str:
        old_target = PurePosixPath(match.group("target"))
        mapped = public_map.get((language, old_target.as_posix()))
        if mapped is None:
            raise MigrationError(f"Cannot map Flow route {old_target} in {old_file}")
        return modular_url(
            "flow", route_without_suffix(mapped), match.group("fragment") or ""
        )

    rendered = SELF_TEMPLATE_RE.sub(replace_self, rendered)
    rendered = HARDCODED_SELF_RE.sub(replace_self, rendered)
    return rendered


def collect_files(root: Path) -> list[Path]:
    return sorted(path for path in root.rglob("*") if path.is_file())


def build_maps(
    docs: Path,
) -> tuple[
    dict[tuple[str, str], PurePosixPath | None],
    dict[tuple[str, str], PurePosixPath | None],
]:
    public_map: dict[tuple[str, str], PurePosixPath | None] = {}
    include_map: dict[tuple[str, str], PurePosixPath | None] = {}
    for language in LANGUAGES:
        public_root = docs / language / "flow"
        include_root = docs / language / "_includes/flow"
        if not public_root.is_dir() or not include_root.is_dir():
            raise MigrationError(f"Restored Flow source is missing for {language}")
        for path in collect_files(public_root):
            relative = PurePosixPath(path.relative_to(public_root).as_posix())
            public_map[(language, relative.as_posix())] = content_destination(relative)
        for path in collect_files(include_root):
            relative = PurePosixPath(path.relative_to(include_root).as_posix())
            include_map[(language, relative.as_posix())] = content_destination(relative)
    return public_map, include_map


def write_redirects(
    path: Path, public_map: dict[tuple[str, str], PurePosixPath | None]
) -> None:
    lines = ["common: []\n"]
    for language in LANGUAGES:
        lines.append(f"{language}:\n")
        entries: list[tuple[str, str]] = []
        for (entry_language, old), new in public_map.items():
            if entry_language != language or new is None:
                continue
            old_path = PurePosixPath(old)
            if old_path == PurePosixPath("presets.yaml") or old_path.parts[0] == "images":
                continue
            old_route = route_without_suffix(old_path)
            new_route = route_without_suffix(new)
            if old_route == new_route:
                continue
            entries.append((old_route, new_route))
        for old_route, new_route in sorted(entries):
            lines.extend(
                [
                    f"  - from: /{old_route}\n",
                    f"    to: /{new_route}\n",
                    "    type: redirect\n",
                ]
            )
    path.write_text("".join(lines), encoding="utf-8")


def create_orphan_wrapper(
    path: Path, include_target: str, title: str
) -> None:
    if path.exists():
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        f"{{% include [{title}]({include_target}) %}}\n", encoding="utf-8"
    )


def run(args: argparse.Namespace) -> dict[str, Any]:
    repository = args.repository_root.resolve()
    docs = repository / "yt/docs"
    public_map, include_map = build_maps(docs)
    source_commit = subprocess.check_output(
        ["git", "rev-parse", args.source_ref], cwd=repository, text=True
    ).strip()
    core_redirects = load_core_redirects(docs / "public/core/redirects.yaml")

    manifest: dict[str, Any] = {
        "schema_version": 1,
        "source_ref": args.source_ref,
        "source_commit": source_commit,
        "entries": [],
        "added_wrappers": [],
    }
    planned_destinations: set[str] = set()

    for language in LANGUAGES:
        for kind, source_root, mapping, destination_root in (
            (
                "public",
                docs / language / "flow",
                public_map,
                docs / "public/flow" / language,
            ),
            (
                "include",
                docs / language / "_includes/flow",
                include_map,
                docs / "common/flow" / language / "_includes",
            ),
        ):
            for source in collect_files(source_root):
                old_relative = PurePosixPath(source.relative_to(source_root).as_posix())
                new_relative = mapping[(language, old_relative.as_posix())]
                source_record = source.relative_to(repository).as_posix()
                entry: dict[str, Any] = {
                    "language": language,
                    "kind": kind,
                    "source": source_record,
                    "source_sha256": sha256(source),
                }
                if new_relative is None:
                    entry["mode"] = "navigation-replaced"
                    manifest["entries"].append(entry)
                    continue

                destination = destination_root / Path(new_relative.as_posix())
                destination_record = destination.relative_to(repository).as_posix()
                if destination_record in planned_destinations:
                    raise MigrationError(f"Flow destination collision: {destination_record}")
                planned_destinations.add(destination_record)
                entry.update({"mode": "moved", "destination": destination_record})
                manifest["entries"].append(entry)

                if not args.apply:
                    continue
                destination.parent.mkdir(parents=True, exist_ok=True)
                if source.suffix in TEXT_SUFFIXES:
                    old_file = PurePosixPath(
                        language,
                        *("flow",) if kind == "public" else ("_includes", "flow"),
                        *old_relative.parts,
                    )
                    rendered = rewrite_text(
                        source.read_text(encoding="utf-8"),
                        language=language,
                        old_file=old_file,
                        kind=kind,
                        new_file=new_relative,
                        public_map=public_map,
                        include_map=include_map,
                        core_redirects=core_redirects,
                    )
                    destination.write_text(rendered, encoding="utf-8")
                    source.unlink()
                else:
                    shutil.move(str(source), str(destination))

    if not args.apply:
        return manifest

    for language in LANGUAGES:
        shutil.rmtree(docs / language / "flow")
        shutil.rmtree(docs / language / "_includes/flow")

    orphan_wrappers = {
        "ru": {
            "concepts/language-choice.md": "Выбор языка",
            "how-to-guides/deployment/configure-authentication.md": "Аутентификация Flow",
            "concepts/deployment/flow-core-target.md": "Целевая версия Flow Core",
            "how-to-guides/deployment/yt-sync-rules.md": "Правила YtSync",
        },
        "en": {
            "concepts/language-choice.md": "Choose a language",
        },
    }
    for language, wrappers in orphan_wrappers.items():
        for relative_value, title in wrappers.items():
            relative = PurePosixPath(relative_value)
            include = PurePosixPath("common/_includes", relative)
            include_target = posixpath.relpath(
                include.as_posix(), relative.parent.as_posix()
            )
            wrapper = docs / "public/flow" / language / Path(relative.as_posix())
            create_orphan_wrapper(wrapper, include_target, title)
            record = wrapper.relative_to(repository).as_posix()
            manifest["added_wrappers"].append(record)

    write_redirects(docs / "public/flow/redirects.yaml", public_map)
    manifest_path = repository / ".github/flow-migration-map.json"
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    return manifest


def main() -> int:
    args = parse_args()
    try:
        manifest = run(args)
    except (MigrationError, OSError, UnicodeError, subprocess.CalledProcessError) as error:
        print(f"migrate-flow-docs: {error}", file=sys.stderr)
        return 1
    counts: dict[str, int] = {}
    for entry in manifest["entries"]:
        key = f"{entry['language']}-{entry['kind']}"
        counts[key] = counts.get(key, 0) + 1
    action = "Migrated" if args.apply else "Planned"
    summary = ", ".join(f"{key}={value}" for key, value in sorted(counts.items()))
    print(f"{action} Flow corpus: {summary}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
