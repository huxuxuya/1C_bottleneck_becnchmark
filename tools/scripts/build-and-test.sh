#!/usr/bin/env bash
set -euo pipefail

# Единый скрипт сборки ИБ из XML-исходников и запуска тестов YAxUnit.
# Поддерживает два режима:
#   --mode ibcmd     (файловая ИБ через ibcmd, режим по умолчанию)
#   --mode postgres  (клиент-серверная ИБ в PostgreSQL + кластер 1С)

MODE="${MODE:-ibcmd}"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --mode)
      MODE="${2:?Укажите режим: ibcmd или postgres}"
      shift 2
      ;;
    *)
      echo "Неизвестный аргумент: $1" >&2
      exit 2
      ;;
  esac
done

WORKSPACE="${WORKSPACE:-/workspace}"
XML_DIR="${XML_DIR:-${WORKSPACE}/build/xml}"
IB_DIR="${IB_DIR:-${WORKSPACE}/build/ib}"
REPORTS_DIR="${REPORTS_DIR:-${WORKSPACE}/build/reports}"
YAXUNIT_CFE="${YAXUNIT_CFE:-${WORKSPACE}/build/deps/YAxUnit.cfe}"
YAXUNIT_CFG="${YAXUNIT_CFG:-${WORKSPACE}/tools/yaxunit.json}"

PG_HOST="${PG_HOST:-postgres}"
PG_PORT="${PG_PORT:-5432}"
PG_DB="${PG_DB:-benchmark_ib}"
PG_USER="${PG_USER:-postgres}"
PG_PASSWORD="${PG_PASSWORD:-postgres}"
ONEC_SERVER_HOST="${ONEC_SERVER_HOST:-1c-server}"
ONEC_IB_NAME="${ONEC_IB_NAME:-benchmark_ib}"

find_1c_bin() {
  local name="$1"
  local found
  found="$(command -v "${name}" 2>/dev/null || true)"
  if [[ -n "${found}" ]]; then
    echo "${found}"
    return 0
  fi
  found="$(find /opt/1cv8 -type f -name "${name}" 2>/dev/null | head -n 1 || true)"
  if [[ -n "${found}" ]]; then
    echo "${found}"
    return 0
  fi
  return 1
}

IBCMD_BIN="$(find_1c_bin ibcmd || true)"
if [[ -z "${IBCMD_BIN}" ]]; then
  echo "Ошибка: утилита ibcmd не найдена в PATH и /opt/1cv8" >&2
  exit 1
fi

if [[ ! -f "${YAXUNIT_CFE}" ]]; then
  echo "Ошибка: не найден файл ${YAXUNIT_CFE}. Выполните make download-yaxunit." >&2
  exit 1
fi

if [[ ! -f "${XML_DIR}/cf/Configuration.xml" ]]; then
  echo "Ошибка: не найден экспортированный XML (${XML_DIR}/cf/Configuration.xml)." >&2
  echo "Выполните make export-xml (локальный EDT CLI) или make export-xml-docker." >&2
  exit 1
fi

mkdir -p "${REPORTS_DIR}"
rm -f "${REPORTS_DIR}/junit.xml" "${REPORTS_DIR}/exitcode.txt" "${REPORTS_DIR}/yaxunit.log"

case "${MODE}" in
  ibcmd)
    rm -rf "${IB_DIR}"
    mkdir -p "${IB_DIR}/server-data"
    DB_ARGS=(--db-path="${IB_DIR}" --data="${IB_DIR}/server-data")
    CREATE_EXTRA=()
    CLIENT_CONN="/F${IB_DIR}"
    ;;
  postgres)
    SERVER_DATA_DIR="${WORKSPACE}/build/ibcmd-server-data/postgres"
    mkdir -p "${SERVER_DATA_DIR}"
    DB_ARGS=(
      --data="${SERVER_DATA_DIR}"
      --dbms=PostgreSQL
      --db-server="${PG_HOST} port=${PG_PORT}"
      --db-name="${PG_DB}"
      --db-user="${PG_USER}"
      --db-pwd="${PG_PASSWORD}"
    )
    CREATE_EXTRA=(--create-database)
    CLIENT_CONN="/S${ONEC_SERVER_HOST}\\${ONEC_IB_NAME}"
    ;;
  *)
    echo "Неподдерживаемый режим: ${MODE} (ожидается ibcmd или postgres)" >&2
    exit 2
    ;;
esac

install_extension_from_xml() {
  local ext_name="$1"
  local ext_prefix="$2"
  local ext_dir="$3"
  "${IBCMD_BIN}" infobase config extension create "${DB_ARGS[@]}" \
    --name="${ext_name}" --name-prefix="${ext_prefix}" --purpose="customization"
  "${IBCMD_BIN}" infobase config import "${DB_ARGS[@]}" \
    --extension="${ext_name}" "${ext_dir}"
  "${IBCMD_BIN}" infobase config extension update "${DB_ARGS[@]}" \
    --name="${ext_name}" --safe-mode=no --unsafe-action-protection=no
  "${IBCMD_BIN}" infobase config apply "${DB_ARGS[@]}" --extension="${ext_name}" --force
}

install_extension_from_cfe() {
  local ext_name="$1"
  local ext_prefix="$2"
  local cfe_path="$3"
  "${IBCMD_BIN}" infobase config extension create "${DB_ARGS[@]}" \
    --name="${ext_name}" --name-prefix="${ext_prefix}" --purpose="customization"
  "${IBCMD_BIN}" infobase config load "${DB_ARGS[@]}" \
    --extension="${ext_name}" "${cfe_path}"
  "${IBCMD_BIN}" infobase config extension update "${DB_ARGS[@]}" \
    --name="${ext_name}" --safe-mode=no --unsafe-action-protection=no
  "${IBCMD_BIN}" infobase config apply "${DB_ARGS[@]}" --extension="${ext_name}" --force
}

echo "=== [1/5] Создание ИБ (режим: ${MODE}) ==="
"${IBCMD_BIN}" infobase create "${DB_ARGS[@]}" ${CREATE_EXTRA[@]+"${CREATE_EXTRA[@]}"}

echo "=== [2/5] Импорт основной конфигурации из ${XML_DIR}/cf ==="
"${IBCMD_BIN}" infobase config import "${DB_ARGS[@]}" "${XML_DIR}/cf"
"${IBCMD_BIN}" infobase config apply "${DB_ARGS[@]}" --force

echo "=== [3/5] Подключение расширения тестов BenchmarkTests ==="
install_extension_from_xml "BenchmarkTests" "Тест_" "${XML_DIR}/cfe/BenchmarkTests"

echo "=== [4/5] Подключение расширения YAxUnit ==="
install_extension_from_cfe "YAXUnit" "ЮТ" "${YAXUNIT_CFE}"

if [[ "${MODE}" == "postgres" ]]; then
  RAC_BIN="$(find_1c_bin rac || true)"
  if [[ -n "${RAC_BIN}" ]]; then
    echo "=== Регистрация ИБ ${ONEC_IB_NAME} в кластере ${ONEC_SERVER_HOST} через rac ==="
    CLUSTER_ID="$("${RAC_BIN}" cluster list "${ONEC_SERVER_HOST}:1545" | awk -F': ' '/^cluster/{print $2; exit}' | tr -d '[:space:]')"
    if [[ -n "${CLUSTER_ID}" ]]; then
      EXISTING_IB="$("${RAC_BIN}" infobase summary list --cluster="${CLUSTER_ID}" "${ONEC_SERVER_HOST}:1545" | grep -E "name[[:space:]]*:[[:space:]]*${ONEC_IB_NAME}$" || true)"
      if [[ -z "${EXISTING_IB}" ]]; then
        "${RAC_BIN}" infobase create "${ONEC_SERVER_HOST}:1545" \
          --cluster="${CLUSTER_ID}" \
          --name="${ONEC_IB_NAME}" \
          --dbms=PostgreSQL \
          --db-server="${PG_HOST}" \
          --db-name="${PG_DB}" \
          --db-user="${PG_USER}" \
          --db-pwd="${PG_PASSWORD}" \
          --locale=ru
      fi
    fi
  fi
fi

echo "=== [5/5] Запуск тестов YAxUnit ==="
CLIENT_BIN="${CLIENT_BIN:-$(find_1c_bin 1cv8 || find_1c_bin 1cv8c || true)}"
if [[ -z "${CLIENT_BIN}" ]]; then
  echo "Ошибка: не найден 1cv8c или 1cv8 для выполнения RunUnitTests" >&2
  exit 1
fi

YAXUNIT_RUN_CFG="${REPORTS_DIR}/yaxunit-run.json"
sed \
  -e "s|/workspace/build/reports|${REPORTS_DIR}|g" \
  -e "s|/workspace|${WORKSPACE}|g" \
  "${YAXUNIT_CFG}" > "${YAXUNIT_RUN_CFG}"

CLIENT_ARGS=(
  ENTERPRISE
  "${CLIENT_CONN}"
  /DisableStartupDialogs
  /DisableStartupMessages
  "/CRunUnitTests=${YAXUNIT_RUN_CFG}"
)

if command -v xvfb-run >/dev/null 2>&1; then
  xvfb-run -a -s "-screen 0 1024x768x24" "${CLIENT_BIN}" "${CLIENT_ARGS[@]}"
else
  "${CLIENT_BIN}" "${CLIENT_ARGS[@]}"
fi

echo "=== Результаты тестирования ==="
if [[ -f "${REPORTS_DIR}/junit.xml" ]]; then
  echo "JUnit отчёт: ${REPORTS_DIR}/junit.xml"
fi

if [[ -f "${REPORTS_DIR}/exitcode.txt" ]]; then
  TEST_CODE="$(tr -dc '0-9' < "${REPORTS_DIR}/exitcode.txt")"
  echo "YAxUnit exitcode: ${TEST_CODE}"
  if [[ -z "${TEST_CODE}" || "${TEST_CODE}" != "0" ]]; then
    exit "${TEST_CODE:-1}"
  fi

  python3 - "${REPORTS_DIR}/junit.xml" "${REPORTS_DIR}/yaxunit.log" "${MIN_YAXUNIT_TESTS:-102}" <<'PY'
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

report_path, log_path, minimum_text = sys.argv[1:]
minimum = int(minimum_text)
report = Path(report_path)
log = Path(log_path)
if not report.is_file():
    raise SystemExit(f"Ошибка: YAxUnit не создал JUnit-отчёт: {report}")

root = ET.parse(report).getroot()
suites = list(root.iter("testsuite"))
tests = sum(int(suite.attrib.get("tests", 0)) for suite in suites)
failures = sum(int(suite.attrib.get("failures", 0)) for suite in suites)
errors = sum(int(suite.attrib.get("errors", 0)) for suite in suites)
log_text = log.read_text(encoding="utf-8", errors="replace") if log.is_file() else ""
compile_markers = ("Ошибка инициализации модуля", "[ОшибкаКомпиляцииВстроенногоЯзыка]")
if any(marker in log_text for marker in compile_markers):
    raise SystemExit("Ошибка: YAxUnit зарегистрировал ошибку инициализации/компиляции модуля; см. yaxunit.log")
if tests < minimum:
    raise SystemExit(f"Ошибка: выполнено {tests} тестов; требуется не менее {minimum}")
if failures or errors:
    raise SystemExit(f"Ошибка: JUnit содержит failures={failures}, errors={errors}")

print(f"Проверено тестов по JUnit: {tests}; failures: {failures}; errors: {errors}")
PY
  exit 0
fi

echo "Предупреждение: файл ${REPORTS_DIR}/exitcode.txt не создан (проверьте ${REPORTS_DIR}/yaxunit.log)" >&2
exit 1
