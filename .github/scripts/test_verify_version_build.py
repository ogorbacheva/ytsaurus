from __future__ import annotations

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace


SCRIPT = Path(__file__).with_name("verify_version_build.py")
SPEC = importlib.util.spec_from_file_location("verify_version_build", SCRIPT)
assert SPEC and SPEC.loader
verifier = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(verifier)


class VerifyVersionBuildTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tempdir = tempfile.TemporaryDirectory()
        self.addCleanup(self.tempdir.cleanup)
        self.root = Path(self.tempdir.name)
        self.input_root = self.root / "input"
        self.html_root = self.root / "html"
        self.md_root = self.root / "md"
        self.registry = self.root / "modules.json"
        self.registry.write_text(
            json.dumps(
                {
                    "schema_version": 1,
                    "modules": [
                        {
                            "name": "spyt",
                            "project_name": "spyt",
                            "viewer_url": "https://docs---spyt.viewer.ydocs.io",
                            "languages": ["ru", "en"],
                        },
                        {
                            "name": "core",
                            "project_name": "core",
                            "viewer_url": "https://docs---core.viewer.ydocs.io",
                            "languages": ["ru", "en"],
                        },
                    ],
                }
            ),
            encoding="utf-8",
        )
        (self.input_root / "spyt").mkdir(parents=True)
        (self.html_root / "spyt").mkdir(parents=True)
        (self.md_root / "spyt").mkdir(parents=True)
        (self.input_root / "assembly-manifest.json").write_text(
            json.dumps(
                {
                    "modules": [
                        {
                            "name": "spyt",
                            "route_queries": {
                                "spyt": "?version=2.11",
                                "core": "",
                            },
                        }
                    ]
                }
            ),
            encoding="utf-8",
        )
        (self.html_root / "spyt" / "index.html").write_text(
            '<a href="https://docs---spyt.viewer.ydocs.io/ru/start?version=2.11">'
            "SPYT 2.11.0</a>\n",
            encoding="utf-8",
        )
        (self.md_root / "spyt" / ".yfm").write_text(
            "docs-viewer:\n"
            "  project-name: spyt\n"
            "  unrestrict-revision-access: true\n"
            "  no-index: true\n",
            encoding="utf-8",
        )
        for build_root in (self.html_root, self.md_root):
            assets = build_root / "spyt" / "_assets" / "navigation"
            assets.mkdir(parents=True)
            for asset in ("logo-dark.svg", "logo-light.svg", "github.svg"):
                (assets / asset).write_text("<svg/>\n", encoding="utf-8")
        for language in ("ru", "en"):
            language_root = self.md_root / "spyt" / language
            language_root.mkdir()
            (language_root / "llms.txt").write_text("SPYT\n", encoding="utf-8")
            (language_root / "llms-full.txt").write_text(
                "SPYT 2.11.0\n", encoding="utf-8"
            )
            (language_root / "toc.yaml").write_text(
                "navigation:\n"
                "  url: https://docs---spyt.viewer.ydocs.io/"
                f"{language}/start?version=2.11\n"
                "  core: https://docs---core.viewer.ydocs.io/"
                f"{language}/overview\n",
                encoding="utf-8",
            )

    def args(self) -> SimpleNamespace:
        return SimpleNamespace(
            registry=self.registry,
            input_root=self.input_root,
            html_root=self.html_root,
            md_root=self.md_root,
            module="spyt",
            version_label="2.11",
            artifact_version="2.11.0",
            docs_revision="abc123",
        )

    def test_valid_isolated_version_build_passes(self) -> None:
        verifier.verify(self.args())

    def test_foreign_project_version_query_is_rejected(self) -> None:
        toc = self.md_root / "spyt" / "ru" / "toc.yaml"
        toc.write_text(
            toc.read_text(encoding="utf-8").replace(
                "/overview", "/overview?version=2.11"
            ),
            encoding="utf-8",
        )

        with self.assertRaisesRegex(
            verifier.VerificationError, "foreign project URL"
        ):
            verifier.verify(self.args())

    def test_revision_query_is_rejected(self) -> None:
        index = self.html_root / "spyt" / "index.html"
        index.write_text(
            index.read_text(encoding="utf-8")
            + '<a href="https://docs---spyt.viewer.ydocs.io/ru/?revision=abc123">bad</a>',
            encoding="utf-8",
        )

        with self.assertRaisesRegex(verifier.VerificationError, "revision query"):
            verifier.verify(self.args())

    def test_selected_route_map_mismatch_is_rejected(self) -> None:
        manifest = self.input_root / "assembly-manifest.json"
        document = json.loads(manifest.read_text(encoding="utf-8"))
        document["modules"][0]["route_queries"]["spyt"] = ""
        manifest.write_text(json.dumps(document), encoding="utf-8")

        with self.assertRaisesRegex(verifier.VerificationError, "Invalid route query"):
            verifier.verify(self.args())


if __name__ == "__main__":
    unittest.main()
