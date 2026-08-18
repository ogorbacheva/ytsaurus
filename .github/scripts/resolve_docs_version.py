#!/usr/bin/env python3
"""Validate and resolve the modular documentation version build plan."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any


MODE_REVISION = "revision-preview"
MODE_VERSION = "version-preview"
MODES = (MODE_REVISION, MODE_VERSION)
NAME_RE = re.compile(r"^[a-z0-9][a-z0-9-]*$")
LABEL_RE = re.compile(r"^[0-9]+\.[0-9]+$")
ARTIFACT_VERSION_RE = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+$")
VARIABLE_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]*$")


class VersionPlanError(RuntimeError):
    pass


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Resolve revision-preview or version-preview inputs."
    )
    parser.add_argument(
        "--registry",
        type=Path,
        default=Path("yt/docs/versions/component-versions.json"),
    )
    parser.add_argument(
        "--modules-registry",
        type=Path,
        default=Path(".github/docs-modules.json"),
    )
    parser.add_argument("--mode", choices=MODES, required=True)
    parser.add_argument("--component", default="")
    parser.add_argument("--version", default="")
    parser.add_argument("--revision", required=True)
    parser.add_argument(
        "--github-output",
        type=Path,
        help="Also write scalar and compact JSON values to GITHUB_OUTPUT.",
    )
    return parser.parse_args()


def load_json(path: Path, description: str) -> dict[str, Any]:
    if not path.is_file():
        raise VersionPlanError(f"Missing {description}: {path}")
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise VersionPlanError(f"Cannot read {description} {path}: {error}") from error
    if not isinstance(value, dict):
        raise VersionPlanError(f"{description} root must be an object")
    return value


def load_modules(path: Path) -> list[dict[str, Any]]:
    document = load_json(path, "module registry")
    if document.get("schema_version") != 1:
        raise VersionPlanError("Unsupported module registry schema_version")
    modules = document.get("modules")
    if not isinstance(modules, list) or not modules:
        raise VersionPlanError("Module registry must contain modules")
    for module in modules:
        if not isinstance(module, dict) or not isinstance(module.get("name"), str):
            raise VersionPlanError("Invalid module registry entry")
    return modules


def require_string(value: Any, description: str, pattern: re.Pattern[str]) -> str:
    if not isinstance(value, str) or not pattern.fullmatch(value):
        raise VersionPlanError(f"Invalid {description}: {value!r}")
    return value


def load_versions(
    path: Path, module_names: set[str]
) -> dict[str, dict[str, Any]]:
    document = load_json(path, "component version registry")
    if document.get("schema_version") != 1:
        raise VersionPlanError("Unsupported component version schema_version")
    components = document.get("components")
    if not isinstance(components, list) or not components:
        raise VersionPlanError("Component version registry must contain components")

    result: dict[str, dict[str, Any]] = {}
    used_variables: set[str] = set()
    for component in components:
        if not isinstance(component, dict):
            raise VersionPlanError("Every versioned component must be an object")
        name = require_string(component.get("name"), "component name", NAME_RE)
        if name in result:
            raise VersionPlanError(f"Duplicate versioned component: {name}")
        if name not in module_names:
            raise VersionPlanError(f"Versioned component is not a module: {name}")
        if component.get("selector") != "minor":
            raise VersionPlanError(f"Component {name} must use the minor selector")
        if component.get("docs_source") != "profiled-current-checkout":
            raise VersionPlanError(
                f"Component {name} has unsupported docs_source; "
                "expected profiled-current-checkout"
            )

        profile_variable = require_string(
            component.get("profile_variable"),
            f"{name} profile_variable",
            VARIABLE_RE,
        )
        artifact_variable = require_string(
            component.get("artifact_variable"),
            f"{name} artifact_variable",
            VARIABLE_RE,
        )
        if profile_variable == artifact_variable:
            raise VersionPlanError(
                f"Component {name} must use different profile and artifact variables"
            )
        for variable in (profile_variable, artifact_variable):
            if variable in used_variables:
                raise VersionPlanError(f"Build variable is owned twice: {variable}")
            used_variables.add(variable)

        release_source = component.get("release_source")
        if not isinstance(release_source, dict):
            raise VersionPlanError(f"Component {name} is missing release_source")
        for key in ("repository", "tag_pattern", "published_when"):
            if not isinstance(release_source.get(key), str) or not release_source[key]:
                raise VersionPlanError(
                    f"Component {name} release_source is missing {key}"
                )
        try:
            tag_pattern = re.compile(release_source["tag_pattern"])
        except re.error as error:
            raise VersionPlanError(
                f"Component {name} has invalid tag_pattern: {error}"
            ) from error

        versions = component.get("versions")
        if not isinstance(versions, list) or not versions:
            raise VersionPlanError(f"Component {name} must contain versions")
        version_map: dict[str, dict[str, Any]] = {}
        for version in versions:
            if not isinstance(version, dict):
                raise VersionPlanError(f"Component {name} has invalid version entry")
            label = require_string(version.get("label"), f"{name} label", LABEL_RE)
            artifact_version = require_string(
                version.get("artifact_version"),
                f"{name} artifact_version",
                ARTIFACT_VERSION_RE,
            )
            release_ref = version.get("release_ref")
            if not isinstance(release_ref, str) or not tag_pattern.fullmatch(release_ref):
                raise VersionPlanError(
                    f"Component {name} release_ref does not match tag_pattern: "
                    f"{release_ref!r}"
                )
            if not release_ref.endswith(f"/{artifact_version}"):
                raise VersionPlanError(
                    f"Component {name} release_ref and artifact_version disagree: "
                    f"{release_ref!r}"
                )
            if label in version_map:
                raise VersionPlanError(f"Duplicate {name} version label: {label}")
            if not artifact_version.startswith(f"{label}."):
                raise VersionPlanError(
                    f"Component {name} artifact {artifact_version} is outside line {label}"
                )
            additional = version.get("additional_build_vars", {})
            if not isinstance(additional, dict) or any(
                not isinstance(key, str)
                or not VARIABLE_RE.fullmatch(key)
                or not isinstance(value, str)
                for key, value in additional.items()
            ):
                raise VersionPlanError(
                    f"Component {name} {label} has invalid additional_build_vars"
                )
            if profile_variable in additional or artifact_variable in additional:
                raise VersionPlanError(
                    f"Component {name} {label} additional variables override owned variables"
                )
            version_map[label] = version

        default_version = require_string(
            component.get("default_version"), f"{name} default_version", LABEL_RE
        )
        if default_version not in version_map:
            raise VersionPlanError(
                f"Component {name} default_version is not present: {default_version}"
            )
        result[name] = {**component, "version_map": version_map}
    return result


def resolve_plan(
    *,
    mode: str,
    component_name: str,
    version_label: str,
    revision: str,
    modules: list[dict[str, Any]],
    components: dict[str, dict[str, Any]],
) -> dict[str, Any]:
    if not revision or any(character.isspace() for character in revision):
        raise VersionPlanError("Revision must be a non-empty scalar")
    if mode not in MODES:
        raise VersionPlanError(f"Unsupported mode: {mode}")

    module_map = {module["name"]: module for module in modules}

    def component_build_vars(
        component: dict[str, Any], label: str, version: dict[str, Any]
    ) -> dict[str, str]:
        return {
            component["profile_variable"]: label,
            component["artifact_variable"]: version["artifact_version"],
            **version.get("additional_build_vars", {}),
        }

    if mode == MODE_REVISION:
        selected_names = list(module_map)
        build_vars = {"docs-revision-query": f"?revision={revision}"}
        for component in components.values():
            label = component["default_version"]
            values = component_build_vars(
                component, label, component["version_map"][label]
            )
            overlap = set(build_vars) & set(values)
            if overlap:
                raise VersionPlanError(
                    "Default component build variables collide: "
                    + ", ".join(sorted(overlap))
                )
            build_vars.update(values)
        resolved_component = ""
        resolved_version = ""
        artifact_version = ""
        release_ref = ""
        update_only_version = "true"
    else:
        component = components.get(component_name)
        if component is None:
            available = ", ".join(sorted(components))
            raise VersionPlanError(
                f"Unknown or unversioned component {component_name!r}; choose: {available}"
            )
        version = component["version_map"].get(version_label)
        if version is None:
            available = ", ".join(component["version_map"])
            raise VersionPlanError(
                f"Unknown {component_name} version {version_label!r}; choose: {available}"
            )
        selected_names = [component_name]
        artifact_version = version["artifact_version"]
        release_ref = version["release_ref"]
        build_vars = {"docs-revision-query": ""}
        build_vars.update(component_build_vars(component, version_label, version))
        resolved_component = component_name
        resolved_version = version_label
        update_only_version = (
            "false" if version_label == component["default_version"] else "true"
        )

    upload_matrix = {
        "include": [
            {
                "module": name,
                "storage-suffix": f"/{module_map[name]['storage_prefix']}",
                "viewer_url": module_map[name]["viewer_url"],
                "version_label": resolved_version,
                "update_only_version": update_only_version,
            }
            for name in selected_names
        ]
    }
    return {
        "mode": mode,
        "modules": selected_names,
        "docs_revision_query": build_vars["docs-revision-query"],
        "build_vars": build_vars,
        "upload_matrix": upload_matrix,
        "component": resolved_component,
        "version_label": resolved_version,
        "artifact_version": artifact_version,
        "release_ref": release_ref,
    }


def write_github_output(path: Path, plan: dict[str, Any]) -> None:
    values = {
        "mode": plan["mode"],
        "modules": json.dumps(plan["modules"], separators=(",", ":")),
        "docs_revision_query": plan["docs_revision_query"],
        "build_vars": json.dumps(plan["build_vars"], separators=(",", ":")),
        "upload_matrix": json.dumps(plan["upload_matrix"], separators=(",", ":")),
        "component": plan["component"],
        "version_label": plan["version_label"],
        "artifact_version": plan["artifact_version"],
        "release_ref": plan["release_ref"],
    }
    with path.open("a", encoding="utf-8") as output:
        for name, value in values.items():
            if "\n" in value or "\r" in value:
                raise VersionPlanError(f"GitHub output {name} contains a newline")
            output.write(f"{name}={value}\n")


def main() -> int:
    args = parse_args()
    try:
        modules = load_modules(args.modules_registry)
        components = load_versions(
            args.registry, {module["name"] for module in modules}
        )
        plan = resolve_plan(
            mode=args.mode,
            component_name=args.component,
            version_label=args.version,
            revision=args.revision,
            modules=modules,
            components=components,
        )
        if args.github_output:
            write_github_output(args.github_output, plan)
        print(json.dumps(plan, ensure_ascii=False, sort_keys=True))
    except VersionPlanError as error:
        print(f"Version plan error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
