from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).with_name("publish_docs_revision.sh")


class PublishDocsRevisionTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tempdir = tempfile.TemporaryDirectory()
        self.addCleanup(self.tempdir.cleanup)
        self.root = Path(self.tempdir.name)
        self.output = self.root / "build-output-md"
        (self.output / "spyt").mkdir(parents=True)
        (self.output / "spyt" / ".yfm").write_text("project: spyt\n", encoding="utf-8")
        self.bin = self.root / "bin"
        self.bin.mkdir()
        self.arguments = self.root / "arguments"
        fake_yfm = self.bin / "yfm"
        fake_yfm.write_text(
            "#!/usr/bin/env bash\n"
            "set -euo pipefail\n"
            "printf '%s\\n' \"$@\" > \"${FAKE_YFM_ARGUMENTS}\"\n",
            encoding="utf-8",
        )
        fake_yfm.chmod(0o755)

    def run_script(self, **overrides: str) -> subprocess.CompletedProcess[str]:
        environment = {
            **os.environ,
            "PATH": f"{self.bin}:{os.environ['PATH']}",
            "FAKE_YFM_ARGUMENTS": str(self.arguments),
            "VERSION_COMPONENT": "spyt",
            "DOCS_REVISION": "abc123",
            "BUILD_OUTPUT_MD": str(self.output),
            "STORAGE_ENDPOINT": "https://s3.example.test",
            "STORAGE_REGION": "eu-test-1",
            "STORAGE_BUCKET": "common-ytsaurus",
            "STORAGE_SUFFIX": "/spyt",
            "STORAGE_ACCESS_KEY_ID": "access",
            "STORAGE_SECRET_ACCESS_KEY": "secret",
            **overrides,
        }
        return subprocess.run(
            ["bash", str(SCRIPT)],
            check=False,
            text=True,
            capture_output=True,
            env=environment,
        )

    def test_simple_bucket_publishes_to_component_revision_prefix(self) -> None:
        result = self.run_script()
        self.assertEqual(result.returncode, 0, result.stderr)
        arguments = self.arguments.read_text(encoding="utf-8").splitlines()
        self.assertIn("common-ytsaurus-stable", arguments)
        self.assertIn("spyt/rev/abc123", arguments)
        self.assertIn(str(self.output / "spyt"), arguments)

    def test_existing_bucket_prefix_is_preserved(self) -> None:
        result = self.run_script(STORAGE_BUCKET="common-ytsaurus/docs")
        self.assertEqual(result.returncode, 0, result.stderr)
        arguments = self.arguments.read_text(encoding="utf-8").splitlines()
        self.assertIn("common-ytsaurus-stable", arguments)
        self.assertIn("docs/spyt/rev/abc123", arguments)

    def test_unsafe_storage_suffix_is_rejected(self) -> None:
        result = self.run_script(STORAGE_SUFFIX="/../spyt")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Unsafe complex storage target", result.stderr)
        self.assertFalse(self.arguments.exists())


if __name__ == "__main__":
    unittest.main()
