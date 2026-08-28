#!/usr/bin/env python3
"""Verify the complete public FLYT snapshot used by GitHub builds."""

from __future__ import annotations

import json
import re
import sys
from collections import Counter
from pathlib import Path


REPOSITORY = Path(__file__).resolve().parents[2]
DOCS = REPOSITORY / "yt/docs"
HREF_RE = re.compile(r"^\s*href:\s*[\"']?(?P<href>[^\"'#\s]+)", re.MULTILINE)


class ValidationError(RuntimeError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValidationError(message)


def local_toc_pages(toc: Path) -> set[Path]:
    result = set()
    for match in HREF_RE.finditer(toc.read_text(encoding="utf-8")):
        href = match.group("href")
        if "{{" not in href and "://" not in href and href.endswith(".md"):
            result.add(Path(href))
    return result


def validate() -> None:
    manifest = json.loads((REPOSITORY / ".github/flyt-migration-map.json").read_text(encoding="utf-8"))
    entries = manifest.get("entries", [])
    require(manifest.get("schema_version") == 1, "Unsupported FLYT manifest schema")
    require(manifest.get("source_file_count") == 29 == len(entries), f"Expected 29 FLYT source records, found {len(entries)}")
    require(len({entry["source"] for entry in entries}) == 29, "Duplicate FLYT sources")
    require(len({entry["destination"] for entry in entries}) == 29, "Duplicate FLYT destinations")
    require(Counter(entry["kind"] for entry in entries) == Counter({
        "public-wrapper": 6,
        "public-include": 6,
        "internal-page": 7,
        "public-image": 5,
        "internal-image": 5,
    }), "Unexpected FLYT source inventory")
    for entry in entries:
        require(re.fullmatch(r"[0-9a-f]{64}", entry["source_sha256"]) is not None, f"Invalid SHA-256 for {entry['source']}")
        if entry["kind"].startswith("public"):
            require((DOCS / entry["destination"]).is_file(), f"Missing public FLYT destination: {entry['destination']}")

    public = DOCS / "public/flyt"
    common = DOCS / "common/flyt"
    require("project-name: flyt" in (public / ".yfm").read_text(encoding="utf-8"), "FLYT .yfm must declare project-name: flyt")
    for required in (".yfmlint", "redirects.yaml"):
        require((public / required).is_file(), f"Missing FLYT project file: {required}")
    for language in ("ru", "en"):
        language_root = public / language
        pages = {path.relative_to(language_root) for path in language_root.rglob("*.md")}
        require(len(pages) == 4, f"Expected four public {language} pages, found {len(pages)}")
        require(pages <= local_toc_pages(language_root / "toc.yaml"), f"Public FLYT {language} page is absent from toc.yaml")
        require((language_root / "reference/index.md").is_file(), f"Missing {language} reference overview")
        for legacy_role in ("user-guide", "admin-guide", "explanation"):
            require(not (language_root / legacy_role).exists(), f"Role leaked into FLYT URL: {language}/{legacy_role}")
        texts = list(language_root.rglob("*.md")) + list((common / language).rglob("*.md"))
        require("yandex-team" not in "\n".join(path.read_text(encoding="utf-8") for path in texts), f"Internal URL leaked into public FLYT {language}")
        require(sum(path.is_file() for path in (common / language / "_includes").rglob("*.md")) == 3, f"Unexpected {language} include count")
    require(sum(path.is_file() for path in (common / "_images").iterdir()) == 5, "Expected five shared FLYT images")
    print("FLYT GitHub snapshot is complete: 12 public source pages/includes and 5 shared images are accounted for.")


def main() -> int:
    try:
        validate()
    except (OSError, KeyError, json.JSONDecodeError, ValidationError) as error:
        print(f"FLYT structure validation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
