#!/usr/bin/env python3
"""Rewrite links to legacy role-based Core routes.

The script supports both the Arcadia prototype root and a GitHub checkout.
Without ``--write`` it is a read-only guard and fails when a known legacy Core
route is still present.
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path


ROUTES = (
    ("user-guide/explanation", "concepts"),
    ("admin-guide/explanation", "concepts"),
    ("user-guide/how-to-guides", "how-to-guides"),
    ("admin-guide/how-to-guides", "how-to-guides"),
    ("user-guide/reference", "reference"),
    ("admin-guide/reference", "reference"),
    ("user-guide/tutorials", "tutorials"),
    ("concepts/concepts/architecture", "concepts/architecture"),
    ("concepts/concepts/general", "concepts/basic-concepts"),
)

TEXT_SUFFIXES = {
    ".html",
    ".json",
    ".md",
    ".py",
    ".txt",
    ".yaml",
    ".yfm",
    ".yfmlint",
    ".yml",
}
CROSS_MODULE_DOCS_ROOT_RE = re.compile(
    r"\{\{ docs_root \}\}/(?P<module>chyt|spyt|yql)/"
    r"(?P<path>[A-Za-z0-9_./-]+)(?P<fragment>#[^)\s'\"]+)?"
)


def iter_text_files(root: Path):
    candidates = []
    if (root / "public").is_dir():
        candidates.extend(
            root / name
            for name in ("public", "common", "internal", "navigation", "github")
            if (root / name).exists()
        )
    elif (root / "yt/docs/public").is_dir():
        candidates.extend(
            path
            for path in (
                root / "yt/docs/public",
                root / "yt/docs/common",
                root / "yt/docs/navigation",
                root / ".github",
            )
            if path.exists()
        )
    else:
        raise RuntimeError(f"Unsupported documentation root: {root}")

    seen = set()
    for candidate in candidates:
        for path in candidate.rglob("*"):
            if not path.is_file() or path.suffix.lower() not in TEXT_SUFFIXES:
                continue
            resolved = path.resolve()
            if resolved in seen:
                continue
            seen.add(resolved)
            yield path


def replacements_for(path: Path, root: Path):
    relative = path.relative_to(root).as_posix()
    replacements = []
    for source, target in ROUTES:
        replacements.extend(
            (
                (
                    f"{{{{ core-docs-root }}}}/{source}",
                    f"{{{{ core-docs-root }}}}/{target}",
                ),
                (
                    f"{{{{ core-docs-root }}}}/{{{{ lang }}}}/{source}",
                    f"{{{{ core-docs-root }}}}/{{{{ lang }}}}/{target}",
                ),
                (
                    f"{{{{ spyt-docs-root }}}}/{{{{ lang }}}}/{source}",
                    f"{{{{ spyt-docs-root }}}}/{{{{ lang }}}}/{target}",
                ),
                (
                    f"{{{{ docs_root }}}}/core/{source}",
                    f"{{{{ docs_root }}}}/core/{target}",
                ),
                (
                    f"{{{{ navigation-core-root }}}}{{{{ navigation-language-path }}}}/{source}",
                    f"{{{{ navigation-core-root }}}}{{{{ navigation-language-path }}}}/{target}",
                ),
                (
                    f"${{core_url}}/${{language}}/{source}",
                    f"${{core_url}}/${{language}}/{target}",
                ),
                (
                    f"${{BUILD_OUTPUT_HTML}}/core/${{language}}/{source}",
                    f"${{BUILD_OUTPUT_HTML}}/core/${{language}}/{target}",
                ),
                (
                    f"https://ytsaurus.tech/docs/core/{source}",
                    f"https://ytsaurus.tech/docs/core/{target}",
                ),
                (
                    f"https://docs.yandex-team.ru/yt/core/{source}",
                    f"https://docs.yandex-team.ru/yt/core/{target}",
                ),
            )
        )

        if relative.endswith(("public/core/ru/toc.yaml", "public/core/en/toc.yaml")):
            replacements.append((f'"{source}', f'"{target}'))
        if relative.endswith(("public/core/ru/index.md", "public/core/en/index.md")):
            replacements.append((f"({source}", f"({target}"))
        if relative.endswith("internal/ru/toc.yaml"):
            replacements.append((f'"core/{source}', f'"core/{target}'))
    return replacements


def expected_text(path: Path, root: Path, source: str) -> tuple[str, int]:
    replacements = 0
    for old, new in replacements_for(path, root):
        count = source.count(old)
        if count:
            source = source.replace(old, new)
            replacements += count
    relative = path.relative_to(root).as_posix()
    if relative.startswith(("yt/docs/public/core/ru/", "yt/docs/public/core/en/")):
        source, count = re.subn(
            r"\{\{ core-docs-root \}\}/(?!\{\{ lang \}\}/)",
            "{{ core-docs-root }}/{{ lang }}/",
            source,
        )
        replacements += count
    if relative.startswith(("yt/docs/public/core/", "yt/docs/common/core/")):
        old_image = "_images/user-guide/explanation/storage/attrs-icon.png"
        new_image = "_images/attrs-icon.png"
        count = source.count(old_image)
        if count:
            source = source.replace(old_image, new_image)
            replacements += count
        source, count = CROSS_MODULE_DOCS_ROOT_RE.subn(
            lambda match: (
                f"{{{{ {match.group('module')}-docs-root }}}}/{{{{ lang }}}}/"
                f"{match.group('path')}{{{{ docs-revision-query }}}}"
                f"{match.group('fragment') or ''}"
            ),
            source,
        )
        replacements += count
        old_spyt = "{{ docs_root }}/user-guide/data-processing/spyt/overview"
        new_spyt = (
            "{{ spyt-docs-root }}/{{ lang }}/concepts/index"
            "{{ docs-revision-query }}"
        )
        count = source.count(old_spyt)
        if count:
            source = source.replace(old_spyt, new_spyt)
            replacements += count
        if path.suffix == ".md":
            old_yql_root = (
                "{{ navigation-yql-root }}{{ navigation-language-path }}/"
            )
            new_yql_root = "{{ yql-docs-root }}/{{ lang }}/"
            count = source.count(old_yql_root)
            if count:
                source = source.replace(old_yql_root, new_yql_root)
                replacements += count
    return source, replacements


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()
    root = args.root.resolve()

    stale = []
    total_replacements = 0
    for path in iter_text_files(root):
        try:
            source = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        expected, replacements = expected_text(path, root, source)
        if expected == source:
            continue
        stale.append(path.relative_to(root).as_posix())
        total_replacements += replacements
        if args.write:
            path.write_text(expected, encoding="utf-8")

    if stale and not args.write:
        print("Legacy Core links remain:")
        print("\n".join(f"  {path}" for path in stale))
        return 1
    action = "Rewrote" if args.write else "Checked"
    print(f"{action} Core route links: {total_replacements} replacements in {len(stale)} files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
