# Global navigation

The Russian and English navigation templates in this directory are the
canonical source for the shared Diplodoc header. The header is generated into
every public documentation project and language registered in
`.github/docs-modules.json`.

Do not edit content between these markers in a project `toc.yaml` directly:

```text
# BEGIN GENERATED GLOBAL NAVIGATION
# END GENERATED GLOBAL NAVIGATION
```

Run the generator from the repository root after changing a template or the
module registry:

```bash
python3 .github/scripts/build_global_navigation.py
```

Check that committed `toc.yaml` files are current without modifying them:

```bash
python3 .github/scripts/build_global_navigation.py --check
```

The generator reads its targets from the module registry, validates every
registered cross-project link against the corresponding local language tree,
and fails if a language has no navigation template.

## Routing variables

The templates reuse the modular routing variables from `public/presets.yaml`:

- `landing-docs-root`, `core-docs-root`, `spyt-docs-root`, and
  `chyt-docs-root` identify independently published projects;
- `lang` selects the matching language;
- `docs-revision-query` keeps links between those four projects on the same
  testing revision.

During a revision preview, the assembler replaces the four project roots with
their Viewer URLs from `.github/docs-modules.json`. In production, the same
variables resolve to `ytsaurus.tech` routes.

YQL is not one of the independently published projects yet. Its links use the
production-only `yql-docs-root` and intentionally omit `docs-revision-query`:
the modular revision is not available in the legacy YQL project.
