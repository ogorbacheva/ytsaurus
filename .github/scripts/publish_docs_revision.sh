#!/usr/bin/env bash

set -Eeuo pipefail
trap 'printf "Documentation publish failed at line %s: %s\n" "${LINENO}" "${BASH_COMMAND}" >&2' ERR

for name in \
  VERSION_COMPONENT \
  DOCS_REVISION \
  BUILD_OUTPUT_MD \
  STORAGE_ENDPOINT \
  STORAGE_REGION \
  STORAGE_BUCKET \
  STORAGE_SUFFIX \
  STORAGE_ACCESS_KEY_ID \
  STORAGE_SECRET_ACCESS_KEY; do
  if [[ -z "${!name:-}" ]]; then
    printf 'Required environment variable %s is empty.\n' "${name}" >&2
    exit 1
  fi
done

storage_target="${STORAGE_BUCKET}${STORAGE_SUFFIX}"
if [[ ! "${storage_target}" =~ ^[A-Za-z0-9._-]+(/[A-Za-z0-9._-]+)+$ ]] || \
  [[ "${storage_target}" =~ (^|/)\.\.?(/|$) ]]; then
  printf 'Unsafe complex storage target: %s\n' "${storage_target}" >&2
  exit 1
fi
if [[ ! "${DOCS_REVISION}" =~ ^[A-Za-z0-9._-]+$ ]]; then
  printf 'Unsafe documentation revision: %s\n' "${DOCS_REVISION}" >&2
  exit 1
fi

bucket_base="${storage_target%%/*}"
storage_prefix="${storage_target#*/}"
input_path="${BUILD_OUTPUT_MD}/${VERSION_COMPONENT}"
test -f "${input_path}/.yfm"

yfm publish \
  -i "${input_path}" \
  --endpoint "${STORAGE_ENDPOINT}" \
  --region "${STORAGE_REGION}" \
  --bucket "${bucket_base}-stable" \
  --prefix "${storage_prefix}/rev/${DOCS_REVISION}" \
  --access-key-id "${STORAGE_ACCESS_KEY_ID}" \
  --secret-access-key "${STORAGE_SECRET_ACCESS_KEY}"
