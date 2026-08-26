#!/usr/bin/env python3

from __future__ import annotations

import json
import subprocess
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).with_name("assemble-modular-docs.py")


class AssembleModularDocsTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tempdir = tempfile.TemporaryDirectory()
        self.root = Path(self.tempdir.name)
        self.source = self.root / "yt" / "docs"
        self.registry = self.root / "modules.json"
        self.output = self.root / "output"
        navigation_assets = self.source / "navigation" / "_assets" / "navigation"
        navigation_assets.mkdir(parents=True)
        for asset in ("logo-dark.svg", "logo-light.svg", "github.svg"):
            (navigation_assets / asset).write_text(
                f"<svg id='{asset}'></svg>\n", encoding="utf-8"
            )
        (self.source / "public" / "demo" / "ru").mkdir(parents=True)
        (self.source / "common" / "demo" / "ru" / "_includes").mkdir(
            parents=True
        )
        (self.source / "common" / "demo" / "_images").mkdir()
        (self.source / "common" / "demo" / "_images" / "shared.png").write_bytes(
            b"shared image"
        )
        (self.source / "public" / "presets.yaml").write_text(
            "default: {}\n"
            "public:\n"
            "  product-name: Demo\n"
            "  docs-root: https://example.test/docs\n"
            "  landing-docs-root: https://example.test/docs\n"
            "  core-docs-root: https://example.test/docs/core\n"
            "  spyt-docs-root: https://example.test/docs/spyt\n"
            "  chyt-docs-root: https://example.test/docs/chyt\n"
            "  yql-docs-root: https://example.test/docs\n"
            "  flow-docs-root: https://example.test/docs/flow\n"
            "  demo-docs-root: https://example.test/docs/demo\n"
            "  docs-revision-query: \"\"\n",
            encoding="utf-8",
        )
        (self.source / "public" / "demo" / ".yfm").write_text(
            "langs: [ru]\n"
            "docs-viewer:\n"
            "  project-name: demo-docs\n"
            "  langs: [ru]\n",
            encoding="utf-8",
        )
        (self.source / "public" / "demo" / "ru" / "toc.yaml").write_text(
            "title: Demo\nhref: index.yaml\n", encoding="utf-8"
        )
        (self.source / "public" / "demo" / "ru" / "presets.yaml").write_text(
            "public:\n"
            "  lang: ru\n"
            "  generated-link: \"{{ docs-root }}/{{ lang }}/preset"
            "{{ docs-revision-query }}\"\n",
            encoding="utf-8",
        )
        (self.source / "public" / "demo" / "ru" / "index.yaml").write_text(
            "blocks:\n"
            "  - type: basic-card\n"
            "    url: '{{ docs-root }}/{{ lang }}/start{{ docs-revision-query }}'\n",
            encoding="utf-8",
        )
        (self.source / "common" / "demo" / "ru" / "_includes" / "note.md").write_text(
            "See [guide]({{ docs_root }}/demo/guide.md#section) and "
            "[hardcoded](https://ytsaurus.tech/docs/demo/guide.md#section).\n",
            encoding="utf-8",
        )
        (self.source / "public" / "demo" / "ru" / "guide.md").write_text(
            "# Guide {#section}\n", encoding="utf-8"
        )
        self.registry.write_text(
            json.dumps(
                {
                    "schema_version": 1,
                    "modules": [
                        {
                            "name": "demo",
                            "project_name": "demo-docs",
                            "viewer_url": (
                                "https://demo-bucket---demo-docs.viewer.ydocs.io"
                            ),
                            "languages": ["ru"],
                            "common": True,
                            "storage_prefix": "demo-docs",
                        }
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
                "python3",
                str(SCRIPT),
                "--registry",
                str(self.registry),
                "--source-root",
                str(self.source),
                "--output-root",
                str(self.output),
                *extra,
            ],
            check=False,
            text=True,
            capture_output=True,
        )

    def make_bilingual_core_fixture(self) -> tuple[Path, Path]:
        public_demo = self.source / "public" / "demo"
        config = public_demo / ".yfm"
        config.write_text(
            config.read_text(encoding="utf-8").replace("[ru]", "[ru, en]"),
            encoding="utf-8",
        )

        english = public_demo / "en"
        english.mkdir()
        (english / "toc.yaml").write_text(
            "title: Demo\nhref: index.yaml\n", encoding="utf-8"
        )
        (english / "presets.yaml").write_text(
            "public:\n  lang: en\n", encoding="utf-8"
        )
        (english / "index.yaml").write_text("title: Demo\n", encoding="utf-8")

        common_demo = self.source / "common" / "demo"
        (common_demo / "en").mkdir()

        public_core = self.source / "public" / "core"
        common_core = self.source / "common" / "core"
        public_demo.rename(public_core)
        common_demo.rename(common_core)

        document = json.loads(self.registry.read_text(encoding="utf-8"))
        document["modules"][0]["name"] = "core"
        document["modules"][0]["languages"] = ["ru", "en"]
        self.registry.write_text(json.dumps(document), encoding="utf-8")
        return public_core, common_core

    def test_assembles_public_and_common_without_internal(self) -> None:
        result = self.run_script()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((self.output / "demo" / "presets.yaml").is_file())
        self.assertEqual(
            (
                self.output
                / "demo"
                / "_assets"
                / "navigation"
                / "github.svg"
            ).read_text(encoding="utf-8"),
            "<svg id='github.svg'></svg>\n",
        )
        self.assertTrue(
            (self.output / "demo" / "ru" / "common" / "_includes" / "note.md").is_file()
        )
        self.assertEqual(
            (self.output / "demo" / "ru" / "_images" / "shared.png").read_bytes(),
            b"shared image",
        )
        self.assertIn(
            "{{ demo-docs-root }}/{{ lang }}/guide"
            "{{ docs-revision-query }}#section",
            (
                self.output
                / "demo"
                / "ru"
                / "common"
                / "_includes"
                / "note.md"
            ).read_text(encoding="utf-8"),
        )
        self.assertNotIn(
            "https://ytsaurus.tech/docs/demo/guide.md",
            (
                self.output
                / "demo"
                / "ru"
                / "common"
                / "_includes"
                / "note.md"
            ).read_text(encoding="utf-8"),
        )
        self.assertIn(
            "{{ docs_root }}/demo/guide.md#section",
            (
                self.source
                / "common"
                / "demo"
                / "ru"
                / "_includes"
                / "note.md"
            ).read_text(encoding="utf-8"),
        )
        self.assertIn(
            "https://ytsaurus.tech/docs/demo/guide.md#section",
            (
                self.source
                / "common"
                / "demo"
                / "ru"
                / "_includes"
                / "note.md"
            ).read_text(encoding="utf-8"),
        )

    def test_materializes_arcadia_code_directive_from_repository_root(self) -> None:
        example = self.root / "yt" / "yt" / "flow" / "examples" / "demo.py"
        example.parent.mkdir(parents=True)
        example.write_text(
            "# BEGIN public-example\n"
            "print('Flow')\n"
            "# END public-example\n",
            encoding="utf-8",
        )
        page = self.source / "public" / "demo" / "ru" / "guide.md"
        page.write_text(
            "# Guide\n\n"
            "{% code '/yt/yt/flow/examples/demo.py' lang='python' "
            "lines='[BEGIN public-example]-[END public-example]' %}\n",
            encoding="utf-8",
        )

        result = self.run_script()
        self.assertEqual(result.returncode, 0, result.stderr)
        generated = (self.output / "demo" / "ru" / "guide.md").read_text(
            encoding="utf-8"
        )
        self.assertIn("```python\nprint('Flow')\n```", generated)
        self.assertNotIn("{% code", generated)
        manifest = json.loads(
            (self.output / "assembly-manifest.json").read_text(encoding="utf-8")
        )
        self.assertEqual(manifest["modules"][0]["code_directives_materialized"], 1)

    def test_preserves_code_directive_when_source_is_not_public(self) -> None:
        page = self.source / "public" / "demo" / "ru" / "guide.md"
        directive = "{% code '/internal/flow/demo.py' lang='python' %}"
        page.write_text(f"# Guide\n\n{directive}\n", encoding="utf-8")

        result = self.run_script()
        self.assertEqual(result.returncode, 0, result.stderr)
        generated = (self.output / "demo" / "ru" / "guide.md").read_text(
            encoding="utf-8"
        )
        self.assertIn(directive, generated)
        manifest = json.loads(
            (self.output / "assembly-manifest.json").read_text(encoding="utf-8")
        )
        self.assertEqual(manifest["modules"][0]["code_directives_materialized"], 0)

    def test_canonical_metadata_url_is_not_rewritten(self) -> None:
        page = self.source / "public" / "demo" / "ru" / "guide.md"
        page.write_text(
            "metadata:\n"
            "    - property: 'og:url'\n"
            "      content: 'https://ytsaurus.tech/docs/demo/ru/guide'\n",
            encoding="utf-8",
        )
        result = self.run_script()
        self.assertEqual(result.returncode, 0, result.stderr)
        generated = (self.output / "demo" / "ru" / "guide.md").read_text(
            encoding="utf-8"
        )
        self.assertIn("https://ytsaurus.tech/docs/demo/ru/guide", generated)

    def test_language_prefixed_content_url_is_rewritten(self) -> None:
        note = self.source / "common" / "demo" / "ru" / "_includes" / "note.md"
        note.write_text(
            "[guide](https://ytsaurus.tech/docs/demo/ru/guide.md#section)\n",
            encoding="utf-8",
        )
        result = self.run_script()
        self.assertEqual(result.returncode, 0, result.stderr)
        generated = (self.output / "demo" / "ru" / "common" / "_includes" / "note.md")
        self.assertIn(
            "{{ demo-docs-root }}/{{ lang }}/guide"
            "{{ docs-revision-query }}#section",
            generated.read_text(encoding="utf-8"),
        )
        self.assertIn(
            "https://example.test/docs/ru/start'",
            (self.output / "demo" / "ru" / "index.yaml").read_text(encoding="utf-8"),
        )
        self.assertFalse((self.output / "internal").exists())

    def test_renders_revision_query_for_page_constructor(self) -> None:
        result = self.run_script("--docs-revision-query", "?revision=abc123")
        self.assertEqual(result.returncode, 0, result.stderr)
        content = (self.output / "demo" / "ru" / "index.yaml").read_text(
            encoding="utf-8"
        )
        self.assertIn("https://example.test/docs/ru/start?revision=abc123", content)
        self.assertNotIn("{{", content)
        presets = (self.output / "demo" / "ru" / "presets.yaml").read_text(
            encoding="utf-8"
        )
        self.assertIn(
            "https://example.test/docs/ru/preset?revision=abc123", presets
        )

    def test_missing_common_blocks_assembly(self) -> None:
        missing = self.source / "common" / "demo" / "ru"
        missing.rename(self.source / "common" / "demo" / "missing")
        result = self.run_script()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("missing common/demo/ru", result.stderr)

    def test_missing_navigation_asset_blocks_assembly(self) -> None:
        (
            self.source
            / "navigation"
            / "_assets"
            / "navigation"
            / "github.svg"
        ).unlink()
        result = self.run_script()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("shared navigation asset navigation/github.svg", result.stderr)

    def test_missing_language_preset_blocks_assembly(self) -> None:
        (self.source / "public" / "demo" / "ru" / "presets.yaml").unlink()
        result = self.run_script()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("demo/ru presets.yaml", result.stderr)

    def test_public_common_collision_blocks_assembly(self) -> None:
        (self.source / "public" / "demo" / "ru" / "common").mkdir()
        result = self.run_script()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("collision", result.stderr)

    def test_public_shared_image_collision_blocks_assembly(self) -> None:
        (self.source / "public" / "demo" / "ru" / "_images").mkdir()
        result = self.run_script()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Public/shared image collision", result.stderr)

    def test_internal_source_blocks_assembly(self) -> None:
        (self.source / "internal").mkdir()
        result = self.run_script()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("must not contain internal", result.stderr)

    def test_nested_internal_path_blocks_assembly(self) -> None:
        (self.source / "public" / "demo" / "ru" / "internal").mkdir()
        result = self.run_script()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Internal documentation paths are not allowed", result.stderr)

    def test_unsafe_revision_query_blocks_assembly(self) -> None:
        result = self.run_script(
            "--docs-revision-query", "?revision=ok&redirect=https://example.test"
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("docs-revision-query must be empty", result.stderr)

    def test_missing_yql_docs_root_blocks_assembly(self) -> None:
        presets = self.source / "public" / "presets.yaml"
        presets.write_text(
            presets.read_text(encoding="utf-8").replace(
                "  yql-docs-root: https://example.test/docs\n", ""
            ),
            encoding="utf-8",
        )
        result = self.run_script()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("missing yql-docs-root", result.stderr)

    def test_public_revision_preview_is_generated_and_noindex(self) -> None:
        source_config = self.source / "public" / "demo" / ".yfm"
        result = self.run_script(
            "--docs-revision-query",
            "?revision=abc123",
            "--public-revision-preview",
        )
        self.assertEqual(result.returncode, 0, result.stderr)

        generated = (self.output / "demo" / ".yfm").read_text(encoding="utf-8")
        self.assertIn("unrestrict-revision-access: true", generated)
        self.assertIn("no-index: true", generated)
        generated_presets = (self.output / "demo" / "presets.yaml").read_text(
            encoding="utf-8"
        )
        self.assertIn(
            'demo-docs-root: "https://demo-bucket---demo-docs.viewer.ydocs.io"',
            generated_presets,
        )
        manifest = json.loads(
            (self.output / "assembly-manifest.json").read_text(encoding="utf-8")
        )
        self.assertEqual(
            manifest["modules"][0]["viewer_url"],
            "https://demo-bucket---demo-docs.viewer.ydocs.io",
        )

        source = source_config.read_text(encoding="utf-8")
        self.assertNotIn("unrestrict-revision-access", source)
        self.assertNotIn("no-index", source)

    def test_public_preview_overrides_language_specific_docs_root(self) -> None:
        language_presets = (
            self.source / "public" / "demo" / "ru" / "presets.yaml"
        )
        language_presets.write_text(
            language_presets.read_text(encoding="utf-8").replace(
                "  lang: ru\n",
                "  lang: ru\n"
                "  demo-docs-root: https://language.example.test/docs/demo\n"
                "  preview-link: '{{ demo-docs-root }}/{{ lang }}/preview"
                "{{ docs-revision-query }}'\n",
            ),
            encoding="utf-8",
        )

        result = self.run_script(
            "--docs-revision-query",
            "?revision=abc123",
            "--public-revision-preview",
        )
        self.assertEqual(result.returncode, 0, result.stderr)

        generated = (self.output / "demo" / "ru" / "presets.yaml").read_text(
            encoding="utf-8"
        )
        viewer_root = "https://demo-bucket---demo-docs.viewer.ydocs.io"
        self.assertIn(f'demo-docs-root: "{viewer_root}"', generated)
        self.assertIn(
            f"preview-link: '{viewer_root}/ru/preview?revision=abc123'",
            generated,
        )

    def test_public_revision_preview_requires_revision(self) -> None:
        result = self.run_script("--public-revision-preview")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("requires a non-empty", result.stderr)

    def test_public_version_preview_is_generated_without_revision_query(self) -> None:
        result = self.run_script("--public-version-preview")
        self.assertEqual(result.returncode, 0, result.stderr)

        generated = (self.output / "demo" / ".yfm").read_text(encoding="utf-8")
        self.assertIn("unrestrict-revision-access: true", generated)
        self.assertIn("no-index: true", generated)
        generated_presets = (self.output / "demo" / "presets.yaml").read_text(
            encoding="utf-8"
        )
        self.assertIn(
            'demo-docs-root: "https://demo-bucket---demo-docs.viewer.ydocs.io"',
            generated_presets,
        )
        manifest = json.loads(
            (self.output / "assembly-manifest.json").read_text(encoding="utf-8")
        )
        self.assertFalse(manifest["modules"][0]["public_revision_preview"])
        self.assertTrue(manifest["modules"][0]["public_version_preview"])

    def test_public_version_preview_rejects_revision_query(self) -> None:
        result = self.run_script(
            "--docs-revision-query",
            "?revision=abc123",
            "--public-version-preview",
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("requires an empty", result.stderr)

    def test_committed_preview_setting_blocks_assembly(self) -> None:
        config = self.source / "public" / "demo" / ".yfm"
        config.write_text(
            config.read_text(encoding="utf-8") + "no-index: true\n",
            encoding="utf-8",
        )
        result = self.run_script(
            "--docs-revision-query",
            "?revision=abc123",
            "--public-revision-preview",
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("must not be committed", result.stderr)

    def test_unsafe_modular_route_blocks_assembly(self) -> None:
        (self.source / "public" / "demo" / "ru" / "index.yaml").write_text(
            "blocks:\n"
            "  - type: basic-card\n"
            "    url: '{{ core-docs-root }}/{{ lang }}/../outside"
            "{{ docs-revision-query }}'\n",
            encoding="utf-8",
        )
        result = self.run_script()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Modular route contains an unsafe path", result.stderr)

    def test_missing_legacy_self_route_target_blocks_assembly(self) -> None:
        note = self.source / "common" / "demo" / "ru" / "_includes" / "note.md"
        note.write_text(
            "[missing]({{ docs_root }}/demo/not-found.md)\n", encoding="utf-8"
        )
        result = self.run_script()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("must resolve exactly once", result.stderr)

    def test_language_specific_modular_route_only_requires_matching_language(self) -> None:
        public_core, common_core = self.make_bilingual_core_fixture()
        ru_only = public_core / "ru" / "ru-only.md"
        ru_only.write_text("# Только RU\n", encoding="utf-8")
        note = common_core / "ru" / "_includes" / "note.md"
        note.write_text(
            "[RU]({{ core-docs-root }}/{{ lang }}/ru-only"
            "{{ docs-revision-query }})\n",
            encoding="utf-8",
        )

        result = self.run_script()
        self.assertEqual(result.returncode, 0, result.stderr)
        generated_note = (
            self.output / "core" / "ru" / "common" / "_includes" / "note.md"
        )
        self.assertTrue(generated_note.is_file())

    def test_modular_root_route_resolves_language_index(self) -> None:
        _, common_core = self.make_bilingual_core_fixture()
        note = common_core / "ru" / "_includes" / "note.md"
        note.write_text(
            "[Core]({{ core-docs-root }}/{{ lang }}/"
            "{{ docs-revision-query }})\n",
            encoding="utf-8",
        )
        result = self.run_script()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_language_specific_modular_route_rejects_missing_matching_language(
        self,
    ) -> None:
        public_core, common_core = self.make_bilingual_core_fixture()
        (public_core / "ru" / "ru-only.md").write_text(
            "# Только RU\n", encoding="utf-8"
        )
        english_includes = common_core / "en" / "_includes"
        english_includes.mkdir()
        (english_includes / "note.md").write_text(
            "[missing]({{ core-docs-root }}/{{ lang }}/ru-only"
            "{{ docs-revision-query }})\n",
            encoding="utf-8",
        )

        result = self.run_script()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Modular route target is missing: core/en/ru-only", result.stderr)

    def test_malformed_legacy_self_route_fragment_blocks_assembly(self) -> None:
        note = self.source / "common" / "demo" / "ru" / "_includes" / "note.md"
        note.write_text(
            "[guide]({{ docs_root }}/demo/guide.md##section)\n", encoding="utf-8"
        )
        result = self.run_script()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("multiple fragments", result.stderr)

    def test_empty_legacy_self_route_fragment_blocks_assembly(self) -> None:
        note = self.source / "common" / "demo" / "ru" / "_includes" / "note.md"
        note.write_text(
            "[guide]({{ docs_root }}/demo/guide.md#)\n", encoding="utf-8"
        )
        result = self.run_script()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("empty fragment", result.stderr)

    def test_legacy_self_route_query_blocks_assembly(self) -> None:
        note = self.source / "common" / "demo" / "ru" / "_includes" / "note.md"
        note.write_text(
            "[guide]({{ docs_root }}/demo/guide.md?x=1)\n", encoding="utf-8"
        )
        result = self.run_script()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("already contains a query", result.stderr)

    def test_unsafe_legacy_self_route_path_blocks_assembly(self) -> None:
        note = self.source / "common" / "demo" / "ru" / "_includes" / "note.md"
        note.write_text(
            "[guide]({{ docs_root }}/demo/../guide.md)\n", encoding="utf-8"
        )
        result = self.run_script()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unsafe path", result.stderr)

    def test_legacy_self_route_cannot_target_common(self) -> None:
        note = self.source / "common" / "demo" / "ru" / "_includes" / "note.md"
        note.write_text(
            "[common]({{ docs_root }}/demo/common/_includes/note.md)\n",
            encoding="utf-8",
        )
        result = self.run_script()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("must resolve exactly once", result.stderr)

    def test_ambiguous_legacy_self_route_target_blocks_assembly(self) -> None:
        (self.source / "public" / "demo" / "ru" / "guide.yaml").write_text(
            "title: Guide\n", encoding="utf-8"
        )
        note = self.source / "common" / "demo" / "ru" / "_includes" / "note.md"
        note.write_text(
            "[guide]({{ docs_root }}/demo/guide)\n", encoding="utf-8"
        )
        result = self.run_script()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("must resolve exactly once", result.stderr)

    def test_bare_legacy_self_route_blocks_assembly(self) -> None:
        note = self.source / "common" / "demo" / "ru" / "_includes" / "note.md"
        note.write_text("[root]({{ docs_root }}/demo)\n", encoding="utf-8")
        result = self.run_script()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("remains after rewrite", result.stderr)

    def test_unsafe_legacy_self_route_fragment_blocks_assembly(self) -> None:
        note = self.source / "common" / "demo" / "ru" / "_includes" / "note.md"
        note.write_text(
            "[guide]({{ docs_root }}/demo/guide.md#{section})\n",
            encoding="utf-8",
        )
        result = self.run_script()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unsafe fragment", result.stderr)

    def test_empty_storage_prefix_blocks_assembly(self) -> None:
        document = json.loads(self.registry.read_text(encoding="utf-8"))
        document["modules"][0]["storage_prefix"] = ""
        self.registry.write_text(json.dumps(document), encoding="utf-8")
        result = self.run_script()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("invalid storage_prefix", result.stderr)

    def test_duplicate_storage_prefix_blocks_assembly(self) -> None:
        document = json.loads(self.registry.read_text(encoding="utf-8"))
        document["modules"].append(
            {
                "name": "second",
                "project_name": "second-docs",
                "viewer_url": "https://second-docs.viewer.ydocs.io",
                "languages": ["ru"],
                "common": False,
                "storage_prefix": document["modules"][0]["storage_prefix"],
            }
        )
        self.registry.write_text(json.dumps(document), encoding="utf-8")
        result = self.run_script()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Duplicate storage_prefix", result.stderr)

    def test_missing_viewer_url_blocks_assembly(self) -> None:
        document = json.loads(self.registry.read_text(encoding="utf-8"))
        del document["modules"][0]["viewer_url"]
        self.registry.write_text(json.dumps(document), encoding="utf-8")
        result = self.run_script()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("invalid viewer_url", result.stderr)

    def test_invalid_viewer_urls_block_assembly(self) -> None:
        invalid_urls = (
            "http://demo-docs.viewer.ydocs.io",
            "https://demo-docs.viewer.ydocs.io/",
            "https://demo-docs.viewer.ydocs.io/path",
            "https://demo-docs.viewer.ydocs.io?preview=1",
            "https://demo-docs.viewer.ydocs.io#preview",
            "https://user@demo-docs.viewer.ydocs.io",
            "https://demo-docs.viewer.ydocs.io:443",
            "https://demo-docs.example.test",
        )
        for viewer_url in invalid_urls:
            with self.subTest(viewer_url=viewer_url):
                document = json.loads(self.registry.read_text(encoding="utf-8"))
                document["modules"][0]["viewer_url"] = viewer_url
                self.registry.write_text(json.dumps(document), encoding="utf-8")
                result = self.run_script()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("invalid viewer_url", result.stderr)

    def test_duplicate_viewer_url_blocks_assembly(self) -> None:
        document = json.loads(self.registry.read_text(encoding="utf-8"))
        document["modules"].append(
            {
                "name": "second",
                "project_name": "second-docs",
                "viewer_url": document["modules"][0]["viewer_url"],
                "languages": ["ru"],
                "common": False,
                "storage_prefix": "second-docs",
            }
        )
        self.registry.write_text(json.dumps(document), encoding="utf-8")
        result = self.run_script()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Duplicate viewer_url", result.stderr)

    def test_preview_requires_every_registered_docs_root(self) -> None:
        presets = self.source / "public" / "presets.yaml"
        presets.write_text(
            presets.read_text(encoding="utf-8").replace(
                "  demo-docs-root: https://example.test/docs/demo\n", ""
            ),
            encoding="utf-8",
        )
        result = self.run_script(
            "--docs-revision-query",
            "?revision=abc123",
            "--public-revision-preview",
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("missing registered demo-docs-root", result.stderr)

    def test_clean_replaces_only_marked_output(self) -> None:
        first = self.run_script()
        self.assertEqual(first.returncode, 0, first.stderr)
        second = self.run_script("--clean")
        self.assertEqual(second.returncode, 0, second.stderr)

    def test_clean_refuses_unmarked_output(self) -> None:
        self.output.mkdir()
        sentinel = self.output / "keep.txt"
        sentinel.write_text("keep\n", encoding="utf-8")
        result = self.run_script("--clean")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Refusing to clean unmarked output root", result.stderr)
        self.assertTrue(sentinel.is_file())


if __name__ == "__main__":
    unittest.main()
