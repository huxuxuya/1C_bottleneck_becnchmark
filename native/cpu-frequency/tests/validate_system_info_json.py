#!/usr/bin/env python3
"""Validate the GetSystemInfo JSON schema emitted by the native CLI."""

from __future__ import annotations

import json
import math
import subprocess
import sys
from typing import Any


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def require_nullable_string(value: Any, field: str) -> None:
    require(value is None or isinstance(value, str), f"{field} must be a string or null")


def require_nullable_positive_integer(value: Any, field: str) -> None:
    require(
        value is None or (isinstance(value, int) and not isinstance(value, bool) and value > 0),
        f"{field} must be a positive integer or null",
    )


def require_nullable_positive_number(value: Any, field: str) -> None:
    require(
        value is None
        or (
            isinstance(value, (int, float))
            and not isinstance(value, bool)
            and math.isfinite(value)
            and value > 0
        ),
        f"{field} must be a positive finite number or null",
    )


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: validate_system_info_json.py <cpu-frequency-cli>")

    output = subprocess.run(
        [sys.argv[1], "--system-info"], check=True, capture_output=True, text=True
    ).stdout
    try:
        result = json.loads(output)
    except json.JSONDecodeError as error:
        raise SystemExit(f"GetSystemInfo returned invalid JSON: {error}") from error

    require(isinstance(result, dict), "system info must be a JSON object")
    for field in ("schemaVersion", "componentVersion", "system", "cpu", "memory", "scope"):
        require(field in result, f"system info is missing {field}")
    require(result["schemaVersion"] == 1, "unsupported system info schemaVersion")
    require(
        isinstance(result["componentVersion"], str) and bool(result["componentVersion"]),
        "componentVersion must be a non-empty string",
    )

    system = result["system"]
    cpu = result["cpu"]
    memory = result["memory"]
    require(isinstance(system, dict), "system must be an object")
    require(isinstance(cpu, dict), "cpu must be an object")
    require(isinstance(memory, dict), "memory must be an object")

    for field in ("name", "version", "kernelVersion", "architecture", "machineModel"):
        require(field in system, f"system is missing {field}")
        require_nullable_string(system[field], f"system.{field}")
    require(bool(system["name"]), "system.name must be available")
    require(bool(system["architecture"]), "system.architecture must be available")

    for field in ("vendor", "model"):
        require(field in cpu, f"cpu is missing {field}")
        require_nullable_string(cpu[field], f"cpu.{field}")
    for field in ("logicalProcessorsReportedByOS", "physicalCoresReportedByOS"):
        require(field in cpu, f"cpu is missing {field}")
        require_nullable_positive_integer(cpu[field], f"cpu.{field}")
    for field in ("cgroupQuotaCores", "jobCpuRatePercent"):
        require(field in cpu, f"cpu is missing {field}")
        require_nullable_positive_number(cpu[field], f"cpu.{field}")

    for field in ("physicalTotalBytes", "physicalAvailableBytes", "processLimitBytes"):
        require(field in memory, f"memory is missing {field}")
        require_nullable_positive_integer(memory[field], f"memory.{field}")
    require("limitSource" in memory, "memory is missing limitSource")
    require_nullable_string(memory["limitSource"], "memory.limitSource")
    require(isinstance(result["scope"], str) and bool(result["scope"]), "scope must be a non-empty string")

    print(
        "PASS: GetSystemInfo schema v{} — {} {}, {} logical processors".format(
            result["schemaVersion"],
            system["name"],
            system["architecture"],
            cpu["logicalProcessorsReportedByOS"],
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
