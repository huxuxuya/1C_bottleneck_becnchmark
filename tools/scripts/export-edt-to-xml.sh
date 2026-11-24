#!/usr/bin/env bash
set -euo pipefail

# Конвертирует проекты 1C:EDT (edt/benchmark и edt/BenchmarkTests)
# в XML-формат Конфигуратора (build/xml/cf и build/xml/cfe/BenchmarkTests)
# для последующей загрузки в ИБ через ibcmd.

WORKSPACE="${WORKSPACE:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
EDT_WS="${EDT_WS:-${WORKSPACE}/build/edt-workspace}"
XML_OUT="${XML_OUT:-${WORKSPACE}/build/xml}"
EDT_VMARGS="${EDT_VMARGS:--Xmx4g}"

find_edt_cli() {
  local bin
  for name in 1cedtcli ring; do
    bin="$(command -v "${name}" 2>/dev/null || true)"
    if [[ -n "${bin}" ]]; then
      echo "${bin}"
      return 0
    fi
  done
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
    "${EDT_CLI}" -data "${EDT_WS}" -vmargs ${EDT_VMARGS} \
      -command export \
      --project "${project_dir}" \
      --configuration-files "${target_xml_dir}" \
      -nl ru_RU
  else
    "${EDT_CLI}" edt workspace export \
      --workspace-location "${EDT_WS}" \
      --project "${project_dir}" \
      --configuration-files "${target_xml_dir}"
  fi
}

mkdir -p "${EDT_WS}"

echo "=== [EDT -> XML] Экспорт основной конфигурации (edt/benchmark) ==="
export_project "${WORKSPACE}/edt/benchmark" "${XML_OUT}/cf"

echo "=== [EDT -> XML] Экспорт расширения тестов (edt/BenchmarkTests) ==="
export_project "${WORKSPACE}/edt/BenchmarkTests" "${XML_OUT}/cfe/BenchmarkTests"

echo "Готово: XML-файлы выгружены в ${XML_OUT}"
