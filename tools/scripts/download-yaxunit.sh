#!/usr/bin/env bash
set -euo pipefail

# Скачивает скомпилированное расширение YAxUnit.cfe из официальных релизов GitHub.
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DEPS_DIR="${ROOT_DIR}/build/deps"
TARGET_FILE="${DEPS_DIR}/YAxUnit.cfe"
YAXUNIT_VERSION="${YAXUNIT_VERSION:-25.04}"

mkdir -p "${DEPS_DIR}"

if [[ -f "${TARGET_FILE}" ]]; then
  echo "YAxUnit.cfe уже присутствует: ${TARGET_FILE}"
  exit 0
fi

echo "Поиск релиза YAxUnit (${YAXUNIT_VERSION}) на GitHub..."
API_URL="https://api.github.com/repos/bia-technologies/yaxunit/releases"

DOWNLOAD_URL="$(
  curl -fsSL "${API_URL}" \
    | grep -Eo '"browser_download_url":[[:space:]]*"[^"]*\.cfe"' \
    | sed -E 's/.*"([^"]+)".*/\1/' \
    | grep -E "${YAXUNIT_VERSION}|YAxUnit" \
    | head -n 1 || true
)"

if [[ -z "${DOWNLOAD_URL}" ]]; then
  DOWNLOAD_URL="$(
    curl -fsSL "https://api.github.com/repos/bia-technologies/yaxunit/releases/latest" \
      | grep -Eo '"browser_download_url":[[:space:]]*"[^"]*\.cfe"' \
      | sed -E 's/.*"([^"]+)".*/\1/' \
      | head -n 1 || true
  )"
fi

if [[ -z "${DOWNLOAD_URL}" ]]; then
  echo "Ошибка: не удалось найти .cfe файл в релизах bia-technologies/yaxunit." >&2
  echo "Скачайте YAxUnit.cfe вручную в ${TARGET_FILE}" >&2
  exit 1
fi

echo "Скачивание ${DOWNLOAD_URL} -> ${TARGET_FILE}"
curl -fsSL -L "${DOWNLOAD_URL}" -o "${TARGET_FILE}"
echo "Готово: ${TARGET_FILE}"
