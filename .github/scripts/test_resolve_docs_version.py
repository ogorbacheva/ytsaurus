from __future__ import annotations

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).with_name("resolve_docs_version.py")
SPEC = importlib.util.spec_from_file_location("resolve_docs_version", SCRIPT)
assert SPEC and SPEC.loader
resolver = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(resolver)


class ResolveDocsVersionTest(unittest.TestCase):
    def setUp(self) -> None:
        self.modules = [
            {
                "name": name,
                "storage_prefix": name,
                "viewer_url": f"https://docs---{name}.viewer.ydocs.io",
            }
            for name in ("landing", "core", "spyt")
        ]
        self.registry = {
            "schema_version": 1,
            "components": [
                {
                    "name": "core",
                    "selector": "minor",
                    "docs_source": "profiled-current-checkout",
                    "default_version": "25.4",
                    "profile_variable": "yt-version",
                    "artifact_variable": "yt-server-version",
                    "release_source": {
                        "repository": "example/core",
                        "tag_pattern": "^core/[0-9]+\\.[0-9]+\\.[0-9]+$",
                        "published_when": "published",
                    },
                    "versions": [
                        {
                            "label": "25.4",
                            "artifact_version": "25.4.0",
                            "release_ref": "core/25.4.0",
                            "additional_build_vars": {"yt-stable-branch": "25.4"},
                        }
                    ],
                },
                {
                    "name": "spyt",
                    "selector": "minor",
                    "docs_source": "profiled-current-checkout",
                    "default_version": "2.11",
                    "profile_variable": "spyt-version",
                    "artifact_variable": "spyt-release-version",
                    "release_source": {
                        "repository": "example/spyt",
                        "tag_pattern": "^spyt/[0-9]+\\.[0-9]+\\.[0-9]+$",
                        "published_when": "published",
                    },
                    "versions": [
                        {
                            "label": "2.10",
                            "artifact_version": "2.10.0",
                            "release_ref": "spyt/2.10.0",
                        },
                        {
                            "label": "2.11",
                            "artifact_version": "2.11.0",
                            "release_ref": "spyt/2.11.0",
                        }
                    ],
                },
            ],
        }

    def write_registry(self, value: dict) -> tuple[tempfile.TemporaryDirectory, Path]:
        directory = tempfile.TemporaryDirectory()
        path = Path(directory.name) / "versions.json"
        path.write_text(json.dumps(value), encoding="utf-8")
        return directory, path

    def load(self, value: dict | None = None):
        directory, path = self.write_registry(value or self.registry)
        self.addCleanup(directory.cleanup)
        return resolver.load_versions(path, {module["name"] for module in self.modules})

    def test_revision_preview_builds_every_module_without_release_label(self) -> None:
        plan = resolver.resolve_plan(
            mode=resolver.MODE_REVISION,
            component_name="all",
            version_label="",
            revision="abc123",
            modules=self.modules,
            components=self.load(),
        )
        self.assertEqual(plan["modules"], ["landing", "core", "spyt"])
        self.assertEqual(plan["source_revision"], "abc123")
        self.assertEqual(plan["docs_revision"], "abc123")
        self.assertEqual(
            plan["build_vars"],
            {
                "docs-revision-query": "?revision=abc123",
                "yt-version": "25.4",
                "yt-server-version": "25.4.0",
                "yt-stable-branch": "25.4",
                "spyt-version": "2.11",
                "spyt-release-version": "2.11.0",
            },
        )
        self.assertEqual(plan["version_label"], "")
        self.assertEqual(
            plan["route_queries"],
            {
                "landing": "?revision=abc123",
                "core": "?revision=abc123",
                "spyt": "?revision=abc123",
            },
        )

    def test_revision_preview_builds_one_unversioned_module(self) -> None:
        plan = resolver.resolve_plan(
            mode=resolver.MODE_REVISION,
            component_name="landing",
            version_label="",
            revision="abc123",
            modules=self.modules,
            components=self.load(),
        )
        self.assertEqual(plan["modules"], ["landing"])
        self.assertEqual(
            plan["upload_matrix"]["include"][0]["module"], "landing"
        )
        self.assertRegex(plan["docs_revision"], r"^[0-9a-f]{40}$")
        self.assertNotEqual(plan["docs_revision"], "abc123")
        selected_query = f"?revision={plan['docs_revision']}"
        self.assertEqual(plan["docs_revision_query"], selected_query)
        self.assertEqual(
            plan["route_queries"],
            {
                "landing": selected_query,
                "core": "",
                "spyt": "",
            },
        )

    def test_revision_preview_rejects_unknown_module(self) -> None:
        with self.assertRaisesRegex(resolver.VersionPlanError, "Unknown module"):
            resolver.resolve_plan(
                mode=resolver.MODE_REVISION,
                component_name="missing",
                version_label="",
                revision="abc123",
                modules=self.modules,
                components=self.load(),
            )

    def test_version_preview_resolves_profile_and_artifact_variables(self) -> None:
        plan = resolver.resolve_plan(
            mode=resolver.MODE_VERSION,
            component_name="core",
            version_label="25.4",
            revision="abc123",
            modules=self.modules,
            components=self.load(),
        )
        self.assertEqual(plan["modules"], ["core"])
        self.assertEqual(
            plan["build_vars"],
            {
                "docs-revision-query": "",
                "yt-version": "25.4",
                "yt-server-version": "25.4.0",
                "yt-stable-branch": "25.4",
            },
        )
        self.assertEqual(plan["artifact_version"], "25.4.0")
        self.assertEqual(plan["release_ref"], "core/25.4.0")
        self.assertEqual(
            plan["route_queries"],
            {"landing": "", "core": "?version=25.4", "spyt": ""},
        )
        self.assertEqual(plan["source_revision"], "abc123")
        self.assertRegex(plan["docs_revision"], r"^[0-9a-f]{40}$")
        self.assertNotEqual(plan["docs_revision"], plan["source_revision"])
        self.assertEqual(plan["upload_matrix"]["include"][0]["version_label"], "25.4")
        self.assertEqual(
            plan["upload_matrix"]["include"][0]["update_only_version"], "false"
        )

    def test_non_default_version_does_not_move_head(self) -> None:
        plan = resolver.resolve_plan(
            mode=resolver.MODE_VERSION,
            component_name="spyt",
            version_label="2.10",
            revision="abc123",
            modules=self.modules,
            components=self.load(),
        )
        self.assertEqual(
            plan["upload_matrix"]["include"][0]["update_only_version"], "true"
        )
        default_plan = resolver.resolve_plan(
            mode=resolver.MODE_VERSION,
            component_name="spyt",
            version_label="2.11",
            revision="abc123",
            modules=self.modules,
            components=self.load(),
        )
        self.assertNotEqual(plan["docs_revision"], default_plan["docs_revision"])

    def test_versioned_revision_is_deterministic_and_component_scoped(self) -> None:
        first = resolver.versioned_docs_revision("abc123", "spyt", "2.11")
        self.assertEqual(
            first, resolver.versioned_docs_revision("abc123", "spyt", "2.11")
        )
        self.assertNotEqual(
            first, resolver.versioned_docs_revision("abc123", "spyt", "2.10")
        )
        self.assertNotEqual(
            first, resolver.versioned_docs_revision("abc123", "core", "2.11")
        )

    def test_partial_revision_namespace_is_deterministic_and_set_scoped(self) -> None:
        modules = ["landing", "core", "spyt"]
        first = resolver.revision_preview_docs_revision(
            "abc123", ["spyt", "core"], modules
        )
        self.assertEqual(
            first,
            resolver.revision_preview_docs_revision(
                "abc123", ["core", "spyt"], modules
            ),
        )
        self.assertNotEqual(
            first,
            resolver.revision_preview_docs_revision("abc123", ["spyt"], modules),
        )
        self.assertEqual(
            resolver.revision_preview_docs_revision("abc123", modules, modules),
            "abc123",
        )

    def test_route_queries_support_arbitrary_selected_module_sets(self) -> None:
        queries = resolver.resolve_route_queries(
            ["landing", "core", "spyt", "chyt"],
            ["core", "spyt", "chyt"],
            "?revision=abc123",
        )
        self.assertEqual(
            queries,
            {
                "landing": "",
                "core": "?revision=abc123",
                "spyt": "?revision=abc123",
                "chyt": "?revision=abc123",
            },
        )

    def test_sequential_partial_rebuilds_do_not_propagate_foreign_revision(self) -> None:
        landing_build = resolver.resolve_route_queries(
            ["landing", "spyt"], ["landing"], "?revision=landing-revision"
        )
        spyt_build = resolver.resolve_route_queries(
            ["landing", "spyt"], ["spyt"], "?revision=spyt-revision"
        )
        self.assertEqual(
            landing_build,
            {"landing": "?revision=landing-revision", "spyt": ""},
        )
        self.assertEqual(
            spyt_build,
            {"landing": "", "spyt": "?revision=spyt-revision"},
        )

    def test_same_source_partial_plans_use_distinct_upload_namespaces(self) -> None:
        components = self.load()
        landing_plan = resolver.resolve_plan(
            mode=resolver.MODE_REVISION,
            component_name="landing",
            version_label="",
            revision="abc123",
            modules=self.modules,
            components=components,
        )
        spyt_plan = resolver.resolve_plan(
            mode=resolver.MODE_REVISION,
            component_name="spyt",
            version_label="",
            revision="abc123",
            modules=self.modules,
            components=components,
        )

        self.assertNotEqual(
            landing_plan["docs_revision"], spyt_plan["docs_revision"]
        )
        self.assertEqual(landing_plan["route_queries"]["spyt"], "")
        self.assertEqual(spyt_plan["route_queries"]["landing"], "")

    def test_route_queries_reject_revision_and_version_combination(self) -> None:
        with self.assertRaisesRegex(
            resolver.VersionPlanError, "Invalid selected route query"
        ):
            resolver.resolve_route_queries(
                ["spyt"], ["spyt"], "?revision=abc123&version=2.11"
            )

    def test_route_queries_reject_empty_selection(self) -> None:
        with self.assertRaisesRegex(
            resolver.VersionPlanError, "At least one route query target"
        ):
            resolver.resolve_route_queries(["spyt"], [], "?revision=abc123")

    def test_route_queries_reject_duplicate_selection(self) -> None:
        with self.assertRaisesRegex(
            resolver.VersionPlanError, "Route query targets must be unique"
        ):
            resolver.resolve_route_queries(
                ["spyt"], ["spyt", "spyt"], "?revision=abc123"
            )

    def test_route_queries_reject_unknown_target(self) -> None:
        with self.assertRaisesRegex(
            resolver.VersionPlanError, "unknown modules: missing"
        ):
            resolver.resolve_route_queries(
                ["spyt"], ["missing"], "?revision=abc123"
            )

    def test_version_preview_rejects_landing(self) -> None:
        with self.assertRaisesRegex(resolver.VersionPlanError, "Unknown or unversioned"):
            resolver.resolve_plan(
                mode=resolver.MODE_VERSION,
                component_name="landing",
                version_label="25.4",
                revision="abc123",
                modules=self.modules,
                components=self.load(),
            )

    def test_version_preview_rejects_unknown_label(self) -> None:
        with self.assertRaisesRegex(resolver.VersionPlanError, "Unknown core version"):
            resolver.resolve_plan(
                mode=resolver.MODE_VERSION,
                component_name="core",
                version_label="25.5",
                revision="abc123",
                modules=self.modules,
                components=self.load(),
            )

    def test_default_version_must_exist(self) -> None:
        self.registry["components"][0]["default_version"] = "25.5"
        with self.assertRaisesRegex(resolver.VersionPlanError, "default_version"):
            self.load(self.registry)

    def test_release_ref_must_match_artifact_version(self) -> None:
        self.registry["components"][0]["versions"][0]["release_ref"] = "core/25.4.1"
        with self.assertRaisesRegex(resolver.VersionPlanError, "disagree"):
            self.load(self.registry)

    def test_profile_and_artifact_variables_must_differ(self) -> None:
        self.registry["components"][0]["artifact_variable"] = "yt-version"
        with self.assertRaisesRegex(resolver.VersionPlanError, "different"):
            self.load(self.registry)

    def test_unknown_docs_source_strategy_is_rejected(self) -> None:
        self.registry["components"][0]["docs_source"] = "release-branch"
        with self.assertRaisesRegex(resolver.VersionPlanError, "unsupported docs_source"):
            self.load(self.registry)

    def test_github_output_uses_single_line_compact_json(self) -> None:
        plan = resolver.resolve_plan(
            mode=resolver.MODE_VERSION,
            component_name="spyt",
            version_label="2.11",
            revision="abc123",
            modules=self.modules,
            components=self.load(),
        )
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "github-output"
            resolver.write_github_output(output, plan)
            values = dict(
                line.split("=", 1)
                for line in output.read_text(encoding="utf-8").splitlines()
            )
        self.assertEqual(json.loads(values["modules"]), ["spyt"])
        self.assertEqual(
            json.loads(values["route_queries"]),
            {"landing": "", "core": "", "spyt": "?version=2.11"},
        )
        self.assertEqual(json.loads(values["build_vars"])["spyt-version"], "2.11")
        self.assertRegex(values["docs_revision"], r"^[0-9a-f]{40}$")

    def test_version_matrix_expands_every_configured_version(self) -> None:
        components = self.load()
        plan = resolver.resolve_version_matrix(
            revision="abc123",
            modules=self.modules,
            components=components,
        )
        rows = plan["version_matrix"]["include"]

        self.assertEqual(
            {(row["module"], row["version_label"]) for row in rows},
            {("core", "25.4"), ("spyt", "2.10"), ("spyt", "2.11")},
        )
        self.assertEqual(plan["matrix_size"], 3)
        self.assertEqual(
            {
                (row["module"], row["version_label"])
                for row in plan["default_matrix"]["include"]
            },
            {("core", "25.4"), ("spyt", "2.11")},
        )

    def test_version_matrix_rows_are_isolated_and_collision_free(self) -> None:
        plan = resolver.resolve_version_matrix(
            revision="abc123",
            modules=self.modules,
            components=self.load(),
        )
        rows = plan["version_matrix"]["include"]

        self.assertEqual(len({row["docs_revision"] for row in rows}), len(rows))
        for row in rows:
            build_vars = json.loads(row["build_vars"])
            route_queries = json.loads(row["route_queries"])
            self.assertEqual(
                route_queries[row["module"]], f"?version={row['version_label']}"
            )
            self.assertTrue(
                all(
                    query == ""
                    for module, query in route_queries.items()
                    if module != row["module"]
                )
            )
            self.assertEqual(build_vars["docs-revision-query"], "")
            self.assertEqual(
                row["artifact_name"],
                f"version-{row['module']}-{row['version_label']}",
            )

    def test_repository_version_matrix_matches_registry_size(self) -> None:
        repository_root = SCRIPT.parents[2]
        modules = resolver.load_modules(
            repository_root / ".github" / "docs-modules.json"
        )
        components = resolver.load_versions(
            repository_root / "yt" / "docs" / "versions" / "component-versions.json",
            {module["name"] for module in modules},
        )
        plan = resolver.resolve_version_matrix(
            revision="abc123",
            modules=modules,
            components=components,
        )

        expected_size = sum(
            len(component["version_map"]) for component in components.values()
        )
        self.assertEqual(plan["matrix_size"], expected_size)
        self.assertEqual(
            len(plan["default_matrix"]["include"]), len(components)
        )

    def test_matrix_github_output_uses_single_line_compact_json(self) -> None:
        plan = resolver.resolve_version_matrix(
            revision="abc123",
            modules=self.modules,
            components=self.load(),
        )
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "github-output"
            resolver.write_matrix_github_output(output, plan)
            lines = output.read_text(encoding="utf-8").splitlines()
            values = dict(line.split("=", 1) for line in lines)

        self.assertEqual(len(lines), 5)
        self.assertEqual(values["matrix_size"], "3")
        self.assertEqual(
            len(json.loads(values["version_matrix"])["include"]), 3
        )
        self.assertEqual(
            len(json.loads(values["default_matrix"])["include"]), 2
        )


if __name__ == "__main__":
    unittest.main()
