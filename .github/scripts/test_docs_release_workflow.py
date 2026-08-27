from __future__ import annotations

import unittest
from pathlib import Path


WORKFLOW = Path(__file__).parents[1] / "workflows" / "docs-release-testing.yaml"


class DocsReleaseWorkflowTest(unittest.TestCase):
    def test_version_publications_share_one_non_cancelling_concurrency_group(self) -> None:
        workflow = WORKFLOW.read_text(encoding="utf-8")

        self.assertIn("inputs.mode == 'revision-preview'", workflow)
        self.assertIn(
            "format('docs-revision-preview-{0}', github.run_id)", workflow
        )
        self.assertIn("|| 'docs-version-publication'", workflow)
        self.assertIn("cancel-in-progress: false", workflow)


if __name__ == "__main__":
    unittest.main()
