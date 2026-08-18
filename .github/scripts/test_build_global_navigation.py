#!/usr/bin/env python3

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).with_name("build_global_navigation.py")
MODULES = ("landing", "core", "spyt", "chyt")
LANGUAGES = ("ru", "en")


class BuildGlobalNavigationTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tempdir = tempfile.TemporaryDirectory()
        self.root = Path(self.tempdir.name)
        self.source = self.root / "yt" / "docs"
        self.registry = self.root / "docs-modules.json"
        (self.source / "navigation").mkdir(parents=True)

        for module in MODULES:
            for language in LANGUAGES:
                language_root = self.source / "public" / module / language
                language_root.mkdir(parents=True)
                index_name = "index.yaml" if module == "landing" else "index.md"
                (language_root / index_name).write_text(
                    f"# {module} {language}\n", encoding="utf-8"
                )
                (language_root / "guide.md").write_text(
                    "# Guide\n", encoding="utf-8"
                )
                (language_root / "toc.yaml").write_text(
                    f"title: {module}\nhref: {index_name}\nitems:\n  - name: Guide\n"
                    "    href: guide.md\n",
                    encoding="utf-8",
                )

        template = (
            "navigation:\n"
            "  logo:\n"
            '    url: "{{ landing-docs-root }}/{{ lang }}/'
            '{{ docs-revision-query }}"\n'
            "  header:\n"
            "    leftItems:\n"
            "      - type: link\n"
            '        url: "{{ core-docs-root }}/{{ lang }}/guide'
            '{{ docs-revision-query }}"\n'
            "      - type: link\n"
            '        url: "{{ spyt-docs-root }}/{{ lang }}/'
            '{{ docs-revision-query }}"\n'
            "      - type: link\n"
            '        url: "{{ chyt-docs-root }}/{{ lang }}/guide'
            '{{ docs-revision-query }}"\n'
            "      - type: link\n"
            '        url: "{{ yql-docs-root }}/{{ lang }}/yql/"\n'
        )
        for language in LANGUAGES:
            (self.source / "navigation" / f"{language}.yaml").write_text(
                template, encoding="utf-8"
            )

        self.registry.write_text(
            json.dumps(
                {
                    "schema_version": 1,
                    "modules": [
                        {"name": module, "languages": list(LANGUAGES)}
                        for module in MODULES
                    ],
                }
            ),
            encoding="utf-8",
        )

    def tearDown(self) -> None:
        self.tempdir.cleanup()

    def run_script(self, *extra: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [
                sys.executable,
                str(SCRIPT),
                "--source-root",
                str(self.source),
                "--registry",
                str(self.registry),
                *extra,
            ],
            check=False,
            text=True,
            capture_output=True,
        )

    def test_build_updates_every_registered_module_and_language(self) -> None:
        result = self.run_script()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.count("public/"), 8)
        for module in MODULES:
            for language in LANGUAGES:
                toc = self.source / "public" / module / language / "toc.yaml"
                content = toc.read_text(encoding="utf-8")
                self.assertEqual(
                    content.count("# BEGIN GENERATED GLOBAL NAVIGATION"), 1
                )
                self.assertEqual(
                    content.count("# END GENERATED GLOBAL NAVIGATION"), 1
                )
                self.assertIn("navigation:", content)
                self.assertIn("items:", content)

    def test_check_reports_all_stale_targets_without_writing(self) -> None:
        toc = self.source / "public" / "core" / "ru" / "toc.yaml"
        before = toc.read_text(encoding="utf-8")
        result = self.run_script("--check")
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertEqual(result.stdout.count("Stale global navigation:"), 8)
        self.assertEqual(toc.read_text(encoding="utf-8"), before)

    def test_check_passes_after_build(self) -> None:
        build = self.run_script()
        self.assertEqual(build.returncode, 0, build.stderr)
        check = self.run_script("--check")
        self.assertEqual(check.returncode, 0, check.stderr)
        self.assertEqual(check.stdout, "")

    def test_build_replaces_only_the_generated_region(self) -> None:
        first = self.run_script()
        self.assertEqual(first.returncode, 0, first.stderr)
        template = self.source / "navigation" / "ru.yaml"
        template.write_text(
            template.read_text(encoding="utf-8").replace(
                "navigation:", "navigation:\n  meta: refreshed", 1
            ),
            encoding="utf-8",
        )
        second = self.run_script()
        self.assertEqual(second.returncode, 0, second.stderr)
        toc = self.source / "public" / "core" / "ru" / "toc.yaml"
        content = toc.read_text(encoding="utf-8")
        self.assertEqual(content.count("# BEGIN GENERATED GLOBAL NAVIGATION"), 1)
        self.assertIn("meta: refreshed", content)
        self.assertIn("href: guide.md", content)

    def test_missing_local_navigation_target_fails(self) -> None:
        (self.source / "public" / "core" / "ru" / "guide.md").unlink()
        result = self.run_script()
        self.assertEqual(result.returncode, 2)
        self.assertIn("missing local page", result.stderr)
        self.assertIn("public/core/ru/guide", result.stderr)

    def test_unregistered_project_cannot_receive_revision_query(self) -> None:
        template = self.source / "navigation" / "ru.yaml"
        template.write_text(
            template.read_text(encoding="utf-8").replace(
                '{{ yql-docs-root }}/{{ lang }}/yql/"',
                '{{ yql-docs-root }}/{{ lang }}/yql/'
                '{{ docs-revision-query }}"',
            ),
            encoding="utf-8",
        )
        result = self.run_script()
        self.assertEqual(result.returncode, 2)
        self.assertIn("unregistered project yql", result.stderr)
        self.assertIn("must not receive docs-revision-query", result.stderr)

    def test_registry_language_requires_a_template(self) -> None:
        document = json.loads(self.registry.read_text(encoding="utf-8"))
        document["modules"][0]["languages"].append("de")
        self.registry.write_text(json.dumps(document), encoding="utf-8")
        language_root = self.source / "public" / "landing" / "de"
        language_root.mkdir()
        (language_root / "index.yaml").write_text("# Landing\n", encoding="utf-8")
        (language_root / "toc.yaml").write_text(
            "title: Landing\nhref: index.yaml\n", encoding="utf-8"
        )
        result = self.run_script()
        self.assertEqual(result.returncode, 2)
        self.assertIn("Global navigation template is missing", result.stderr)
        self.assertIn("de.yaml", result.stderr)

    def test_malformed_generated_markers_fail(self) -> None:
        toc = self.source / "public" / "core" / "ru" / "toc.yaml"
        toc.write_text(
            toc.read_text(encoding="utf-8")
            + "# BEGIN GENERATED GLOBAL NAVIGATION\n",
            encoding="utf-8",
        )
        result = self.run_script()
        self.assertEqual(result.returncode, 2)
        self.assertIn("malformed generated-navigation markers", result.stderr)

    def test_reversed_generated_markers_fail_cleanly(self) -> None:
        toc = self.source / "public" / "core" / "ru" / "toc.yaml"
        toc.write_text(
            toc.read_text(encoding="utf-8")
            + "# END GENERATED GLOBAL NAVIGATION\n"
            + "# BEGIN GENERATED GLOBAL NAVIGATION\n",
            encoding="utf-8",
        )
        result = self.run_script()
        self.assertEqual(result.returncode, 2)
        self.assertIn("malformed generated-navigation markers", result.stderr)


if __name__ == "__main__":
    unittest.main()
