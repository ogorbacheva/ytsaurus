#!/usr/bin/env bash

set -Eeuo pipefail
trap 'printf "Version matrix build failed at line %s: %s\n" "${LINENO}" "${BASH_COMMAND}" >&2' ERR

for name in \
  VERSION_COMPONENT \
  VERSION_LABEL \
  ARTIFACT_VERSION \
  DOCS_REVISION \
  BUILD_VARS \
  ROUTE_QUERIES \
  DOCS_SOURCE_ROOT \
  DOCS_INPUT_ROOT \
  BUILD_OUTPUT_HTML \
  BUILD_OUTPUT_MD \
  BUILD_LOGS; do
  if [[ -z "${!name:-}" ]]; then
    printf 'Required environment variable %s is empty.\n' "${name}" >&2
    exit 1
  fi
done

jq -e --arg module "${VERSION_COMPONENT}" \
  '.modules[] | select(.name == $module)' \
  .github/docs-modules.json >/dev/null
jq -e --argjson vars "${BUILD_VARS}" '$vars | type == "object"' \
  <<<null >/dev/null
jq -e --argjson routes "${ROUTE_QUERIES}" '$routes | type == "object"' \
  <<<null >/dev/null

python3 .github/scripts/assemble-modular-docs.py \
  --source-root "${DOCS_SOURCE_ROOT}" \
  --registry .github/docs-modules.json \
  --output-root "${DOCS_INPUT_ROOT}" \
  --module "${VERSION_COMPONENT}" \
  --docs-revision "${DOCS_REVISION}" \
  --docs-revision-query '' \
  --route-queries "${ROUTE_QUERIES}" \
  --public-version-preview \
  --clean

mkdir -p "${BUILD_OUTPUT_HTML}" "${BUILD_OUTPUT_MD}" "${BUILD_LOGS}"
log_file="${BUILD_LOGS}/${VERSION_COMPONENT}-${VERSION_LABEL}.log"

printf 'Building %s %s as strict HTML\n' \
  "${VERSION_COMPONENT}" "${VERSION_LABEL}" | tee "${log_file}"
yfm \
  -i "${DOCS_INPUT_ROOT}/${VERSION_COMPONENT}" \
  -o "${BUILD_OUTPUT_HTML}/${VERSION_COMPONENT}" \
  --strict \
  --vars "${BUILD_VARS}" \
  2>&1 | tee -a "${log_file}"

printf '\nBuilding %s %s as strict md2md\n' \
  "${VERSION_COMPONENT}" "${VERSION_LABEL}" | tee -a "${log_file}"
yfm \
  -i "${DOCS_INPUT_ROOT}/${VERSION_COMPONENT}" \
  -o "${BUILD_OUTPUT_MD}/${VERSION_COMPONENT}" \
  --output-format md \
  --add-map-file \
  --allow-custom-resources \
  --no-search \
  --strict \
  --vars "${BUILD_VARS}" \
  2>&1 | tee -a "${log_file}"

python3 .github/scripts/verify_version_build.py \
  --registry .github/docs-modules.json \
  --input-root "${DOCS_INPUT_ROOT}" \
  --html-root "${BUILD_OUTPUT_HTML}" \
  --md-root "${BUILD_OUTPUT_MD}" \
  --module "${VERSION_COMPONENT}" \
  --version-label "${VERSION_LABEL}" \
  --artifact-version "${ARTIFACT_VERSION}" \
  --docs-revision "${DOCS_REVISION}"
