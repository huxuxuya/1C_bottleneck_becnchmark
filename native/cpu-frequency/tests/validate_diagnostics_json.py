#!/usr/bin/env python3
"""Run the native diagnostic suite and validate its JSON contract."""

from __future__ import annotations

import json
import math
import subprocess
import sys


EXPECTED_CODES = [
    "CPU_002_INDEPENDENT_INSTRUCTIONS",
    "CPU_003_WAKE_LATENCY",
    "CPU_004_READY_QUEUE",
    "CPU_005_CPU_QUOTA",
    "CPU_006_SMT_COMPETITION",
    "CPU_007_CORE_MIGRATION",
    "CPU_008_IDLE_RECOVERY",
    "CPU_009_SUSTAINED_SPEED",
]


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: validate_diagnostics_json.py <cpu-frequency-cli>")
    output = subprocess.run(
        [sys.argv[1], "--all-diagnostics"], check=True, capture_output=True, text=True
    ).stdout
    results = json.loads(output)
    if [result.get("probeCode") for result in results] != EXPECTED_CODES:
        raise SystemExit("diagnostic suite returned missing, duplicate, or reordered probe codes")
    for result in results:
        for field in ("schemaVersion", "componentVersion", "probeName", "available", "status",
                      "primaryMetric", "primaryValue", "primaryUnit", "details", "measurements"):
            if field not in result:
                raise SystemExit(f"{result['probeCode']} is missing {field}")
        if result["schemaVersion"] != 1 or not isinstance(result["measurements"], dict):
            raise SystemExit(f"{result['probeCode']} has an incompatible JSON shape")
        if not math.isfinite(result["primaryValue"]):
            raise SystemExit(f"{result['probeCode']} returned a non-finite primary value")
        if result["available"]:
            if result["status"] != "Косвенный сигнал" or result["primaryUnit"] == "н/д":
                raise SystemExit(f"{result['probeCode']} has inconsistent available status")
            if result["primaryMetric"] not in result["measurements"]:
                raise SystemExit(f"{result['probeCode']} omitted its primary metric from measurements")
        elif result["status"] != "Недоступно" or result["primaryValue"] != 0:
            raise SystemExit(f"{result['probeCode']} has inconsistent unavailable status")
        print(f"{result['probeCode']}: {result['status']} — {result['primaryMetric']}={result['primaryValue']} {result['primaryUnit']}")
    print(f"PASS: validated {len(results)} native diagnostic results")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
