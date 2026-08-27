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
validates project-local header icons, and fails if a language has no navigation
template.

Header SVG files live in `_assets/navigation/`. The modular assembler copies
them into every project and adds a hidden `_navigation-assets.md` page for each
language. Its image references make Diplodoc include the SVG files in
`yfm-build-content.json`; the GitHub workflow verifies both the files and their
manifest entries before uploading the md2md artifact to Viewer. For a public
preview, the assembler also expands the header references to the project's
revision-specific `docs-assets/<project>/rev/<revision>/` URLs. Do not replace
these paths with third-party URLs: the Viewer may restrict external images.

## Routing variables

The templates reuse the modular routing variables from `public/presets.yaml`:

- `<module>-docs-root` variables identify independently published projects;
- `lang` selects the matching language;
- `docs-revision-query` marks the place where the assembler writes the query
  for the target project.

During a revision preview, the assembler replaces the registered project roots
with their Viewer URLs from `.github/docs-modules.json` and materializes a
query for every cross-project route. A full preview points all registered
projects at one shared revision. A partial preview adds its revision only to
links targeting projects selected for that build; links to untouched projects
open their published default instead of requesting an artifact that was never
uploaded there.

Partial preview revisions are derived from the source revision and the sorted
set of selected projects. Thus two runs for the same commit but different
project sets cannot overwrite one another under the same `rev/` prefix. A
version preview writes `?version=<label>` only for the selected component and
never combines `version` with `revision`. In production, the project-root
variables resolve to `ytsaurus.tech` routes.

This routing prevents a foreign-revision 404, but a partial preview is not a
sticky multi-project snapshot: after following a link to an untouched
project, subsequent navigation uses that project's published default. Use a
full preview when all transitions must remain inside one coherent preview.

YQL keeps its existing content path, `/<lang>/yql/...`, inside its independent
project. It is part of `revision-preview`, but intentionally has no entry in
the component version matrix until its release and source-repository policy is
defined.
