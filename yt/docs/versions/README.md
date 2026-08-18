# Component documentation versions

`component-versions.json` is the machine-readable lock file for versioned
YTsaurus documentation. Its product rules come from the
[component release-cycle table](https://wiki.yandex-team.ru/infrastructure/ype/editorial/projects/yt-doc/proekty/redizajjn-dokumentacii/#component-release-cycles).

The registry keeps two different values for every documentation version:

- `label` is the value shown in the Diplodoc version selector and in
  `?version=<label>`. It is also assigned to the component's
  `profile_variable` and can be used in inline YFM conditions;
- `artifact_version` is an exact published product version. It is assigned to
  `artifact_variable` and is used in image tags, package versions, commands,
  and links.

For example, CHYT documentation label `2.17` builds with
`chyt-version=2.17` and `chyt-release-version=2.17.4`.

`default_version` identifies the currently recommended documentation line. It
is metadata for automation, not a special Diplodoc label named `latest`. When
that label is published by `version-preview`, the workflow updates both the
named version and the project's default/head, so the documentation opens
without a `?version=` query. Publishing any other registered label updates
only that label and cannot move the default/head backwards.

`docs_source: profiled-current-checkout` means that the product `release_ref`
is audit metadata, not a Git ref used to check out documentation. The workflow
builds the selected profile from the current documentation checkout. A future
branch-based component must introduce and validate a separate docs-source
strategy instead of overloading `release_ref`.

## Updating the registry

1. Confirm that the product release satisfies `release_source.published_when`.
2. Add or update the explicit `label`, `artifact_version`, and `release_ref`.
3. Move `default_version` only after the new line is ready to be the suggested
   documentation version.
4. Run the resolver tests and a `version-preview` workflow for the changed
   component.

The lock file is intentionally checked in. Release discovery automation may
propose updates, but a documentation build never silently replaces a locked
artifact version with the newest tag.

Both workflow modes resolve their variables from this file. A
`revision-preview` uses every component's `default_version`; a
`version-preview` uses the explicitly selected component and label.
