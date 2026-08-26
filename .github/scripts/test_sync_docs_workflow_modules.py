from __future__ import annotations

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).with_name("sync_docs_workflow_modules.py")
SPEC = importlib.util.spec_from_file_location("sync_docs_workflow_modules", SCRIPT)
assert SPEC and SPEC.loader
sync = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(sync)


class SyncDocsWorkflowModulesTest(unittest.TestCase):
    def test_sync_and_check_use_registry_order(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            registry = root / "modules.json"
            workflow = root / "workflow.yaml"
            registry.write_text(
                json.dumps(
                    {
                        "schema_version": 1,
                        "modules": [{"name": "landing"}, {"name": "flow"}],
                    }
                ),
                encoding="utf-8",
            )
            workflow.write_text(f"before\n{sync.BEGIN}\n{sync.END}\nafter\n", encoding="utf-8")
            names = sync.module_names(registry)
            sync.sync_workflow(workflow, names, check=False)
            sync.sync_workflow(workflow, names, check=True)
            self.assertIn("          - all\n          - landing\n          - flow", workflow.read_text())

    def test_check_rejects_stale_choices(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            workflow = Path(directory) / "workflow.yaml"
            workflow.write_text(
                f"{sync.BEGIN}\n          - all\n{sync.END}\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(sync.WorkflowChoicesError, "stale"):
                sync.sync_workflow(workflow, ["flow"], check=True)


if __name__ == "__main__":
    unittest.main()
