#!/usr/bin/env python3
"""Verify the roleless Core source layout used by the modular build."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


CATEGORIES = ("concepts", "tutorials", "how-to-guides", "reference")
ROLES = {"user-guide", "admin-guide"}
REDIRECT_FROM_RE = re.compile(r"^  - from: (?P<route>/\S+)$")


class CoreStructureError(RuntimeError):
    pass


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=Path("yt/docs"))
    return parser.parse_args()


def require_file(path: Path) -> None:
    if not path.is_file() or path.is_symlink():
        raise CoreStructureError(f"Required Core file is missing: {path}")


def require_directory(path: Path) -> None:
    if not path.is_dir() or path.is_symlink():
        raise CoreStructureError(f"Required Core directory is missing: {path}")


def check_role_directories(root: Path, *, allow_partials: bool) -> None:
    failures = []
    for path in root.rglob("*"):
        if not path.is_dir() or path.name not in ROLES:
            continue
        relative = path.relative_to(root)
        if allow_partials and "_partials" in relative.parts:
            continue
        failures.append(relative.as_posix())
    if failures:
        raise CoreStructureError(
            "Audience roles remain in Core source routes: " + ", ".join(failures)
        )


def check_redirects(public: Path) -> dict[str, int]:
    path = public / "redirects.yaml"
    require_file(path)
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
        if not section or index + 2 >= len(lines):
            raise CoreStructureError(f"Malformed Core redirect near line {index + 1}")
        source = match.group("route")
        to_line = lines[index + 1]
        type_line = lines[index + 2]
        if not to_line.startswith("    to: /") or type_line != "    type: redirect":
            raise CoreStructureError(f"Malformed Core redirect near line {index + 1}")
        target = to_line.removeprefix("    to: ")
        if not any(f"/{role}/" in source for role in ROLES):
            raise CoreStructureError(f"Legacy Core redirect has no audience role: {source}")
        if source in sections[section]:
            raise CoreStructureError(f"Duplicate Core redirect: {section} {source}")
        sections[section][source] = target
        index += 3

    counts = {}
    for language in ("ru", "en"):
        effective = dict(sections["common"])
        overlap = set(effective) & set(sections[language])
        if overlap:
            raise CoreStructureError(
                f"Locale-specific Core redirect duplicates common: {sorted(overlap)[0]}"
            )
        effective.update(sections[language])
        for target in effective.values():
            target_file = public / language / f"{target.lstrip('/')}.md"
            if not target_file.is_file():
                raise CoreStructureError(
                    f"Core redirect target is missing for {language}: {target}"
                )
        counts[language] = len(effective)
    return counts


def run(args: argparse.Namespace) -> None:
    source_root = args.source_root.resolve()
    public = source_root / "public/core"
    common = source_root / "common/core"
    require_directory(public)
    require_directory(common)
    redirects = check_redirects(public)

    markdown = 0
    for language in ("ru", "en"):
        locale = public / language
        require_file(locale / "index.md")
        require_file(locale / "toc.yaml")
        require_file(locale / "presets.yaml")
        for category in CATEGORIES:
            require_directory(locale / category)
            require_file(locale / category / "index.md")
        if (locale / "_images").exists():
            raise CoreStructureError(
                f"Core images must be shared, not locale-specific: {locale / '_images'}"
            )
        check_role_directories(locale, allow_partials=False)
        check_role_directories(
            common / language / "_includes", allow_partials=True
        )
        markdown += sum(1 for path in locale.rglob("*.md") if path.is_file())

    images = common / "_images"
    require_directory(images)
    nested = [path for path in images.rglob("*") if path.is_dir()]
    if nested:
        raise CoreStructureError(
            "Core shared images must use a flat directory: "
            + ", ".join(path.relative_to(images).as_posix() for path in nested)
        )
    image_count = sum(1 for path in images.iterdir() if path.is_file())
    if image_count == 0:
        raise CoreStructureError("Core shared image directory is empty")

    print(
        f"Verified roleless Core structure: {markdown} public Markdown files, "
        f"{image_count} shared images, {redirects['ru']} RU and "
        f"{redirects['en']} EN redirects"
    )


def main() -> int:
    try:
        run(parse_args())
    except (CoreStructureError, OSError) as error:
        print(f"check-core-structure: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
