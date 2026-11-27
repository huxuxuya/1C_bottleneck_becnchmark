#!/usr/bin/env bash
set -euo pipefail

WORKSPACE="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CLIENT_BIN="${CLIENT_BIN:-/opt/1cv8/8.5.1.1529/1cv8}"
INFOBASE_DIR="${WORKSPACE}/local/onec/benchmark"

if [[ ! -x "${CLIENT_BIN}" ]]; then
  echo "Ошибка: не найден клиент 1С: ${CLIENT_BIN}" >&2
  exit 1
fi

if [[ ! -f "${INFOBASE_DIR}/1Cv8.1CD" ]]; then
  echo "Ошибка: рабочая ИБ не создана: ${INFOBASE_DIR}" >&2
  echo "Сначала выполните шаги подготовки из docs/runbook.md." >&2
  exit 1
fi

exec "${CLIENT_BIN}" ENTERPRISE "/F${INFOBASE_DIR}" "$@"
