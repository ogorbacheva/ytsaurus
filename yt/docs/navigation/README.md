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

- `landing-docs-root`, `core-docs-root`, `spyt-docs-root`, `chyt-docs-root`,
  and `yql-docs-root` identify independently published projects;
- `lang` selects the matching language;
- `docs-revision-query` keeps links between those five projects on the same
  testing revision.

During a revision preview, the assembler replaces the five project roots with
their Viewer URLs from `.github/docs-modules.json`. In production, the same
variables resolve to `ytsaurus.tech` routes.

YQL keeps its existing content path, `/<lang>/yql/...`, inside its independent
project. It is part of `revision-preview`, but intentionally has no entry in
the component version matrix until its release and source-repository policy is
defined.
