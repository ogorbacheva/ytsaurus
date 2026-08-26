#!/usr/bin/env python3
"""Keep the workflow-dispatch module choices aligned with the module registry."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


BEGIN = "          # BEGIN docs module choices: generated from .github/docs-modules.json"
END = "          # END docs module choices"


class WorkflowChoicesError(RuntimeError):
    pass


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--registry",
        type=Path,
        default=Path(".github/docs-modules.json"),
    )
    parser.add_argument(
        "--workflow",
        type=Path,
        default=Path(".github/workflows/docs-release-testing.yaml"),
    )
    parser.add_argument("--check", action="store_true")
    return parser.parse_args()


def module_names(registry: Path) -> list[str]:
    try:
        document = json.loads(registry.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise WorkflowChoicesError(f"Cannot read module registry {registry}: {error}") from error
    modules = document.get("modules")
    if document.get("schema_version") != 1 or not isinstance(modules, list):
        raise WorkflowChoicesError(f"Invalid module registry: {registry}")
    names = [module.get("name") for module in modules if isinstance(module, dict)]
    if len(names) != len(modules) or any(not isinstance(name, str) for name in names):
        raise WorkflowChoicesError(f"Invalid module entry in {registry}")
    if len(names) != len(set(names)):
        raise WorkflowChoicesError(f"Duplicate module name in {registry}")
    return names


def expected_block(names: list[str]) -> str:
    choices = "\n".join(f"          - {name}" for name in ("all", *names))
    return f"{BEGIN}\n{choices}\n{END}"


def sync_workflow(workflow: Path, names: list[str], *, check: bool) -> None:
    try:
        text = workflow.read_text(encoding="utf-8")
    except OSError as error:
        raise WorkflowChoicesError(f"Cannot read workflow {workflow}: {error}") from error
    begin = text.find(BEGIN)
    end = text.find(END)
    if begin < 0 or end < begin or text.find(BEGIN, begin + 1) >= 0 or text.find(END, end + 1) >= 0:
        raise WorkflowChoicesError(
            f"Workflow {workflow} must contain exactly one generated choices block"
        )
    end += len(END)
    expected = expected_block(names)
    actual = text[begin:end]
    if actual == expected:
        return
    if check:
        raise WorkflowChoicesError(
            "Workflow module choices are stale; run "
            "python3 .github/scripts/sync_docs_workflow_modules.py"
        )
    workflow.write_text(text[:begin] + expected + text[end:], encoding="utf-8")


def main() -> int:
    args = parse_args()
    try:
        sync_workflow(args.workflow, module_names(args.registry), check=args.check)
    except WorkflowChoicesError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
