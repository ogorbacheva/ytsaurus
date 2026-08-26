#!/usr/bin/env python3
"""Verify that the Flow migration is complete, navigable, and lossless."""

from __future__ import annotations

import json
import re
import sys
from collections import Counter
from pathlib import Path


LANGUAGES = ("ru", "en")
EXPECTED_SOURCE_COUNTS = {
    ("ru", "public"): 343,
    ("en", "public"): 318,
    ("ru", "include"): 123,
    ("en", "include"): 124,
}
EXPECTED_NAVIGATION_REPLACED = 24
EXPECTED_CONFIGURATION_FILES = 211
EXPECTED_IMAGES = {"ru": 7, "en": 5}
CATEGORY_ROOTS = ("concepts", "tutorials", "how-to-guides", "reference")
HREF_RE = re.compile(r"^\s*href:\s*[\"']?(?P<href>[^\"'#\s]+)", re.MULTILINE)


class ValidationError(RuntimeError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValidationError(message)


def local_toc_pages(toc: Path) -> set[Path]:
    pages: set[Path] = set()
    for match in HREF_RE.finditer(toc.read_text(encoding="utf-8")):
        href = match.group("href")
        if "{{" in href or "://" in href or not href.endswith(".md"):
            continue
        pages.add(Path(href))
    return pages


def redirect_targets(path: Path) -> dict[str, list[str]]:
    language = ""
    result = {language: [] for language in LANGUAGES}
    for line in path.read_text(encoding="utf-8").splitlines():
        if line in {"ru:", "en:"}:
            language = line[:-1]
        elif language and line.startswith("    to: /"):
            result[language].append(line.removeprefix("    to: /"))
    return result


def validate(repository: Path) -> None:
    manifest_path = repository / ".github/flow-migration-map.json"
    require(manifest_path.is_file(), f"Missing migration manifest: {manifest_path}")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    require(manifest.get("schema_version") == 1, "Unsupported Flow manifest schema")
    entries = manifest.get("entries")
    require(isinstance(entries, list), "Flow manifest entries must be a list")
    require(len(entries) == 908, f"Expected 908 source records, found {len(entries)}")

    source_counts = Counter((entry["language"], entry["kind"]) for entry in entries)
    require(
        source_counts == Counter(EXPECTED_SOURCE_COUNTS),
        f"Unexpected source corpus counts: {dict(source_counts)}",
    )
    require(
        len({entry["source"] for entry in entries}) == len(entries),
        "Duplicate source paths in Flow migration manifest",
    )

    replaced = [entry for entry in entries if entry["mode"] == "navigation-replaced"]
    moved = [entry for entry in entries if entry["mode"] == "moved"]
    require(
        len(replaced) == EXPECTED_NAVIGATION_REPLACED,
        f"Expected {EXPECTED_NAVIGATION_REPLACED} replaced TOC fragments, found {len(replaced)}",
    )
    require(
        len(moved) + len(replaced) == len(entries),
        "Unknown migration mode in Flow manifest",
    )
    require(
        len({entry["destination"] for entry in moved}) == len(moved),
        "Multiple source files map to one Flow destination",
    )
    for entry in moved:
        destination = repository / entry["destination"]
        require(destination.is_file(), f"Missing migrated file: {entry['destination']}")
    for entry in entries:
        require(
            not (repository / entry["source"]).exists(),
            f"Legacy Flow source still exists: {entry['source']}",
        )

    public = repository / "yt/docs/public/flow"
    common = repository / "yt/docs/common/flow"
    for required in (public / ".yfm", public / ".yfmlint", public / "redirects.yaml"):
        require(required.is_file(), f"Missing Flow project file: {required}")
    require("project-name: flow" in (public / ".yfm").read_text(encoding="utf-8"),
            "Flow .yfm must declare project-name: flow")

    for language in LANGUAGES:
        language_root = public / language
        require((language_root / "index.md").is_file(), f"Missing {language} Flow index.md")
        require((language_root / "toc.yaml").is_file(), f"Missing {language} Flow toc.yaml")
        require((language_root / "presets.yaml").is_file(), f"Missing {language} Flow presets.yaml")
        for category in CATEGORY_ROOTS:
            require(
                (language_root / category / "index.md").is_file(),
                f"Missing {language}/{category}/index.md overview",
            )
        for legacy_role in ("user-guide", "admin-guide", "explanation"):
            require(
                not (language_root / legacy_role).exists(),
                f"Role leaked into Flow URL tree: {language}/{legacy_role}",
            )

        configuration_count = sum(
            path.is_file()
            for path in (language_root / "reference/configuration").iterdir()
        )
        require(
            configuration_count == EXPECTED_CONFIGURATION_FILES,
            f"Expected {EXPECTED_CONFIGURATION_FILES} generated configuration files for "
            f"{language}, found {configuration_count}",
        )
        image_count = sum(path.is_file() for path in (language_root / "_images").rglob("*"))
        require(
            image_count == EXPECTED_IMAGES[language],
            f"Expected {EXPECTED_IMAGES[language]} images for {language}, found {image_count}",
        )

        pages = {
            path.relative_to(language_root)
            for path in language_root.rglob("*.md")
            if path.parent != language_root / "reference/configuration"
        }
        pages.add(Path("reference/configuration/all_yson_structs.md"))
        navigated = local_toc_pages(language_root / "toc.yaml")
        missing_from_toc = sorted(pages - navigated)
        require(
            not missing_from_toc,
            f"Flow {language} pages missing from toc.yaml: "
            + ", ".join(map(str, missing_from_toc)),
        )

    targets = redirect_targets(public / "redirects.yaml")
    for language, routes in targets.items():
        for route in routes:
            target = public / language / f"{route}.md"
            require(target.is_file(), f"Broken Flow redirect target for {language}: /{route}")

    wrappers = manifest.get("added_wrappers")
    require(isinstance(wrappers, list) and len(wrappers) == 5,
            "Expected five language-asymmetric Flow wrappers")
    for wrapper in wrappers:
        require((repository / wrapper).is_file(), f"Missing Flow wrapper: {wrapper}")

    for language in LANGUAGES:
        include_count = sum(
            path.is_file() for path in (common / language / "_includes").rglob("*")
        )
        require(
            include_count == EXPECTED_SOURCE_COUNTS[(language, "include")],
            f"Unexpected {language} include count: {include_count}",
        )

    print(
        "Flow structure is complete: 908 source files accounted for, "
        "884 files moved, 24 TOC fragments replaced, RU/EN navigation complete."
    )


def main() -> int:
    repository = Path(__file__).resolve().parents[2]
    try:
        validate(repository)
    except (OSError, KeyError, json.JSONDecodeError, ValidationError) as error:
        print(f"Flow structure validation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
