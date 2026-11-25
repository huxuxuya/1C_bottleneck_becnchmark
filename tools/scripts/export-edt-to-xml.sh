#!/usr/bin/env bash
set -euo pipefail

# Конвертирует проекты 1C:EDT (edt/benchmark и edt/BenchmarkTests)
# в XML-формат Конфигуратора (build/xml/cf и build/xml/cfe/BenchmarkTests)
# для последующей загрузки в ИБ через ibcmd.

WORKSPACE="${WORKSPACE:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
EDT_WS="${EDT_WS:-${WORKSPACE}/build/edt-workspace}"
EDT_CONFIG_DIR="${EDT_CONFIG_DIR:-${EDT_WS}/.configuration}"
XML_OUT="${XML_OUT:-${WORKSPACE}/build/xml}"
EDT_VMARGS="${EDT_VMARGS:--Xmx4g}"
EDT_INI_FILE="${EDT_INI_FILE:-}"
EDT_TIMEOUT="${EDT_TIMEOUT:-150}"

find_edt_cli() {
  local bin
  for name in 1cedtcli ring; do
    bin="$(command -v "${name}" 2>/dev/null || true)"
    if [[ -n "${bin}" ]]; then
      echo "${bin}"
      return 0
    fi
  done
  if [[ -d /Applications/1C ]]; then
    bin="$(find /Applications/1C -type f -name "1cedtcli" 2>/dev/null | sort | tail -n 1 || true)"
    if [[ -n "${bin}" ]]; then
      echo "${bin}"
      return 0
    fi
  fi
  if [[ -n "${HOME:-}" ]]; then
    bin="$(find "${HOME}/Library/Application Support/1C/1cedtstart/installations" \
      -type f -name "1cedtcli" 2>/dev/null | sort | tail -n 1 || true)"
    if [[ -n "${bin}" ]]; then
      echo "${bin}"
      return 0
    fi
  fi
  bin="$(find /opt/1C /Applications -type f \( -name "1cedtcli" -o -name "ring" \) 2>/dev/null | head -n 1 || true)"
  if [[ -n "${bin}" ]]; then
    echo "${bin}"
    return 0
  fi
  return 1
}

EDT_CLI="${EDT_CLI:-$(find_edt_cli || true)}"
if [[ -z "${EDT_CLI}" ]]; then
  echo "Ошибка: не найден 1cedtcli или ring." >&2
  echo "Установите 1C:EDT локально, передайте путь в EDT_CLI или используйте профиль edt-export в Docker." >&2
  exit 1
fi

export_project() {
  local project_dir="$1"
  local target_xml_dir="$2"

  rm -rf "${target_xml_dir}"
  mkdir -p "${target_xml_dir}"

  if [[ "$(basename "${EDT_CLI}")" == "1cedtcli" ]]; then
    local cli_args=(-data "${EDT_WS}" -timeout "${EDT_TIMEOUT}")
    local vm_args=()
    if [[ -n "${EDT_INI_FILE}" ]]; then
      cli_args+=(-ini-file "${EDT_INI_FILE}")
    else
      local source_ini="$(dirname "${EDT_CLI}")/1cedt.ini"
      local export_ini="${EDT_WS}/1cedt-export.ini"
      if [[ -f "${source_ini}" ]]; then
        awk -v configuration_dir="${EDT_CONFIG_DIR}" '
          $0 == "-vmargs" {
            print "-configuration"
            print configuration_dir
          }
          { print }
        ' "${source_ini}" > "${export_ini}"
        cli_args+=(-ini-file "${export_ini}")
      fi
    fi
    cli_args+=(-nl ru_RU -vmargs)
    read -r -a vm_args <<< "${EDT_VMARGS}"
    cli_args+=("${vm_args[@]}" -command export
      --project "${project_dir}"
      --configuration-files "${target_xml_dir}")
    "${EDT_CLI}" "${cli_args[@]}"
  else
    "${EDT_CLI}" edt workspace export \
      --workspace-location "${EDT_WS}" \
      --project "${project_dir}" \
      --configuration-files "${target_xml_dir}"
  fi
}

mkdir -p "${EDT_WS}" "${EDT_CONFIG_DIR}"

echo "=== [EDT -> XML] Экспорт основной конфигурации (edt/benchmark) ==="
export_project "${WORKSPACE}/edt/benchmark" "${XML_OUT}/cf"

echo "=== [EDT -> XML] Экспорт расширения тестов (edt/BenchmarkTests) ==="
export_project "${WORKSPACE}/edt/BenchmarkTests" "${XML_OUT}/cfe/BenchmarkTests"

echo "Готово: XML-файлы выгружены в ${XML_OUT}"
