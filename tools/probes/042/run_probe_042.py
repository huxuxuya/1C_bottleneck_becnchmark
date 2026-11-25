#!/usr/bin/env python3
"""Run probe 042 on Linux with fio and write a versioned JSON import package."""

from __future__ import annotations

import argparse
import hashlib
import json
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import uuid
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

IMPLEMENTATION_ID = "fio-linux-psync-v1"
MIB = 1024 * 1024


class ProbeError(RuntimeError):
    pass


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


def parse_rates(value: str) -> list[int]:
    rates: list[int] = []
    for part in value.split(","):
        try:
            rate = int(part.strip())
        except ValueError as exc:
            raise argparse.ArgumentTypeError("write rates must be comma-separated positive integers") from exc
        if rate <= 0:
            raise argparse.ArgumentTypeError("write rates must be positive")
        if rate in rates:
            raise argparse.ArgumentTypeError("write rates must not contain duplicates")
        rates.append(rate)
    if not rates:
        raise argparse.ArgumentTypeError("at least one write rate is required")
    return rates


def percentile(stats: dict[str, Any], quantile: int) -> int:
    clat = stats.get("clat_ns") or stats.get("lat_ns") or {}
    percentiles = clat.get("percentile", {})
    for key, value in percentiles.items():
        try:
            if abs(float(key) - quantile) < 0.0001:
                return int(value)
        except (TypeError, ValueError):
            continue
    raise ProbeError(f"fio JSON has no {quantile}th percentile for completion latency")


def observation_seed(repeat_no: int, phase: str, target_mib_s: int) -> int:
    seed_material = f"probe-042-v1:{repeat_no}:{phase}:{target_mib_s}".encode("ascii")
    return int.from_bytes(hashlib.sha256(seed_material).digest()[:4], "big") & 0x7FFFFFFF


def find_mount(path: Path) -> dict[str, str]:
    """Return the deepest Linux mount point containing path, when mountinfo is readable."""
    mountinfo = Path("/proc/self/mountinfo")
    if not mountinfo.exists():
        return {"mount_point": str(path), "filesystem": "unknown", "device": "unknown"}

    resolved = str(path.resolve())
    best: tuple[int, dict[str, str]] | None = None
    for line in mountinfo.read_text(encoding="utf-8", errors="replace").splitlines():
        left, separator, right = line.partition(" - ")
        if not separator:
            continue
        fields = left.split()
        right_fields = right.split()
        if len(fields) < 5 or len(right_fields) < 2:
            continue
        mount_point = fields[4].replace("\\040", " ").replace("\\011", "\t").replace("\\134", "\\")
        if resolved == mount_point or resolved.startswith(mount_point.rstrip("/") + "/"):
            candidate = {
                "mount_point": mount_point,
                "filesystem": right_fields[0],
                "device": right_fields[1],
            }
            if best is None or len(mount_point) > best[0]:
                best = (len(mount_point), candidate)
    return best[1] if best else {"mount_point": str(path), "filesystem": "unknown", "device": "unknown"}


def fio_json(executable: str, job_text: str, timeout_seconds: int) -> dict[str, Any]:
    with tempfile.NamedTemporaryFile("w", encoding="utf-8", suffix=".fio", delete=False) as job_file:
        job_file.write(job_text)
        job_path = Path(job_file.name)
    try:
        completed = subprocess.run(
            [executable, str(job_path), "--output-format=json+"],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout_seconds,
            check=False,
        )
    finally:
        job_path.unlink(missing_ok=True)

    if completed.returncode != 0:
        raise ProbeError(f"fio exited with {completed.returncode}: {completed.stderr.strip() or completed.stdout.strip()}")
    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise ProbeError(f"fio did not return valid JSON: {exc}") from exc


def job_by_name(payload: dict[str, Any], name: str) -> dict[str, Any]:
    for job in payload.get("jobs", []):
        if job.get("jobname") == name:
            return job
    raise ProbeError(f"fio JSON is missing job {name!r}")


def make_observation(
    *,
    point_id: str,
    repeat_no: int,
    phase: str,
    target_mib_s: int,
    random_seed: int,
    fio_payload: dict[str, Any] | None,
    error: str = "",
) -> dict[str, Any]:
    observation: dict[str, Any] = {
        "point_id": point_id,
        "repeat_no": repeat_no,
        "phase": phase,
        "random_seed": random_seed,
        "target_write_rate_Bps": target_mib_s * MIB,
        "completed_write_bytes": 0,
        "window_elapsed_ns": 0,
        "write_window_elapsed_ns": 0,
        "actual_write_rate_Bps": 0,
        "read_p50_ns": 0,
        "read_p95_ns": 0,
        "read_p99_ns": 0,
        "read_iops": 0,
        "read_qd_actual": 0,
        "read_errors": 0,
        "write_errors": 0,
        "status": "error",
        "message": error,
    }
    if fio_payload is None:
        return observation

    reader = job_by_name(fio_payload, "reader")
    read_stats = reader.get("read", {})
    measured_ms = int(read_stats.get("runtime", 0))
    if measured_ms <= 0:
        raise ProbeError("fio JSON has no positive measured read runtime")

    observation.update(
        {
            "window_elapsed_ns": measured_ms * 1_000_000,
            "read_p50_ns": percentile(read_stats, 50),
            "read_p95_ns": percentile(read_stats, 95),
            "read_p99_ns": percentile(read_stats, 99),
            "read_iops": float(read_stats.get("iops", 0)),
            # One synchronous reader job cannot have more than one read in flight.
            "read_qd_actual": 1,
            "read_errors": int(read_stats.get("total_err", 0)),
        }
    )

    writer_error = 0
    if phase == "pressure":
        writer = job_by_name(fio_payload, "writer")
        write_stats = writer.get("write", {})
        writer_measured_ms = int(write_stats.get("runtime", 0))
        if writer_measured_ms <= 0:
            raise ProbeError("fio JSON has no positive measured write runtime")
        observation["completed_write_bytes"] = int(write_stats.get("io_bytes", 0))
        observation["write_errors"] = int(write_stats.get("total_err", 0))
        observation["write_window_elapsed_ns"] = writer_measured_ms * 1_000_000
        numerator = observation["completed_write_bytes"] * 1_000_000_000
        denominator = observation["write_window_elapsed_ns"]
        observation["actual_write_rate_Bps"] = (numerator + denominator // 2) // denominator
        writer_error = int(writer.get("error", 0))

    reader_error = int(reader.get("error", 0))
    if reader_error == 0 and writer_error == 0:
        observation["status"] = "valid"
        observation["message"] = ""
    else:
        observation["message"] = f"fio job error: reader={reader_error}, writer={writer_error}"
    return observation


def prefill_file(executable: str, path: Path, size_bytes: int, timeout_seconds: int) -> None:
    job = f"""[global]
ioengine=psync
direct=1
thread=1

[prepare]
filename={path}
rw=write
bs=1m
size={size_bytes}
iodepth=1
end_fsync=1
"""
    fio_json(executable, job, timeout_seconds)


def measurement_job(
    read_path: Path,
    write_path: Path,
    file_size_bytes: int,
    read_block_bytes: int,
    write_block_bytes: int,
    duration_seconds: int,
    ramp_seconds: int,
    target_rate_Bps: int | None,
    random_seed: int,
) -> str:
    job = f"""[global]
ioengine=psync
direct=1
thread=1
time_based=1
runtime={duration_seconds}
ramp_time={ramp_seconds}
randseed={random_seed}
invalidate=1

[reader]
filename={read_path}
rw=randread
bs={read_block_bytes}
size={file_size_bytes}
iodepth=1
numjobs=1
"""
    if target_rate_Bps is not None:
        job += f"""
[writer]
filename={write_path}
rw=randwrite
bs={write_block_bytes}
size={file_size_bytes}
iodepth=1
numjobs=1
rate={target_rate_Bps}
"""
    return job


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work-dir", required=True, type=Path, help="existing directory on the target filesystem")
    parser.add_argument("--output", required=True, type=Path, help="JSON package path; write after the fio series")
    parser.add_argument("--run-id", default=str(uuid.uuid4()), help="UUID used to link this package to the 1C run")
    parser.add_argument("--fio", default="fio", help="fio executable name or path")
    parser.add_argument("--file-size-gib", type=float, required=True,
                        help="size of each test file; choose it large enough to exceed caches under study")
    parser.add_argument("--duration-seconds", type=int, default=10)
    parser.add_argument("--ramp-seconds", type=int, default=3)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--setup-timeout-seconds", type=int, default=3600)
    parser.add_argument("--read-block-kib", type=int, default=4)
    parser.add_argument("--write-block-kib", type=int, default=64)
    parser.add_argument("--write-rates-mib-s", type=parse_rates, default=parse_rates("8,32,64"))
    parser.add_argument("--keep-files", action="store_true", help="leave only this run's generated test files")
    args = parser.parse_args()
    output = args.output.expanduser().resolve()
    if output.exists():
        print(f"refusing to overwrite existing package: {output}", file=sys.stderr)
        return 2

    package: dict[str, Any] = {
        "schema_version": 1,
        "probe_id": "042",
        "implementation_id": IMPLEMENTATION_ID,
        "run_id": args.run_id,
        "started_at_utc": utc_now(),
        "completed_at_utc": "",
        "fio_version": "unknown",
        "host": {},
        "parameters": {},
        "observations": [],
        "status": "error",
        "message": "",
    }
    run_directory: Path | None = None
    exit_code = 1
    try:
        if platform.system() != "Linux":
            raise ProbeError("implementation fio-linux-psync-v1 currently supports Linux only")
        if not re.fullmatch(r"[0-9a-fA-F-]{1,36}", args.run_id):
            raise ProbeError("run-id must contain up to 36 hexadecimal digits and hyphens")
        if args.file_size_gib <= 0 or args.duration_seconds < 2 or args.ramp_seconds < 0 or args.ramp_seconds >= args.duration_seconds:
            raise ProbeError("file size must be positive; duration >= 2s; ramp must be nonnegative and shorter than duration")
        if args.repeats < 3 or args.read_block_kib < 1 or args.write_block_kib < 1 or args.setup_timeout_seconds < 1:
            raise ProbeError("at least three repeats, positive block sizes, and a positive setup timeout are required")
        work_dir = args.work_dir.expanduser().resolve(strict=True)
        if not work_dir.is_dir() or work_dir == Path("/"):
            raise ProbeError("work-dir must be an existing, non-root directory")
        fio_path = shutil.which(args.fio)
        if not fio_path:
            raise ProbeError(f"fio executable not found: {args.fio}")
        version_result = subprocess.run([fio_path, "--version"], text=True, capture_output=True, check=False)
        if version_result.returncode != 0:
            raise ProbeError(f"cannot read fio version: {version_result.stderr.strip()}")
        fio_version = version_result.stdout.strip()
        if not fio_version.startswith("fio-"):
            raise ProbeError(f"selected executable is not fio: {fio_version or fio_path}")

        run_directory = work_dir / f".probe-042-{args.run_id}"
        try:
            output.relative_to(run_directory.resolve())
        except ValueError:
            pass
        else:
            raise ProbeError("result package must be outside the temporary workload directory")
        run_directory.mkdir(mode=0o700, exist_ok=False)
        package["fio_version"] = fio_version
        package["host"] = {
            "hostname": platform.node(),
            "kernel_release": platform.release(),
            "platform": platform.platform(),
            **find_mount(work_dir),
        }
        file_size_bytes = int(args.file_size_gib * 1024 * 1024 * 1024)
        if file_size_bytes % (1024 * 1024) != 0:
            raise ProbeError("file size must align to the 1 MiB direct-I/O preparation block")
        read_path = run_directory / "read-random.dat"
        write_path = run_directory / "write-random.dat"
        package["parameters"] = {
            "work_dir": str(work_dir),
            "filesystem_device": package["host"].get("device", "unknown"),
            "file_size_bytes": file_size_bytes,
            "read_block_bytes": args.read_block_kib * 1024,
            "write_block_bytes": args.write_block_kib * 1024,
            "read_qd_requested": 1,
            "read_qd_actual": 1,
            "ioengine": "psync",
            "direct": True,
            "cache_note": "direct I/O bypasses guest page cache; storage controller cache may still serve requests",
            "duration_seconds": args.duration_seconds,
            "ramp_seconds": args.ramp_seconds,
            "setup_timeout_seconds": args.setup_timeout_seconds,
            "repeats": args.repeats,
            "write_rate_targets_MiB_s": args.write_rates_mib_s,
            "random_seed_rule": "sha256(probe-042-v1:repeat:phase:target-MiB/s), first 31 bits",
            "generated_files_removed": not args.keep_files,
        }

        prefill_file(fio_path, read_path, file_size_bytes, args.setup_timeout_seconds)
        prefill_file(fio_path, write_path, file_size_bytes, args.setup_timeout_seconds)

        for repeat_no in range(1, args.repeats + 1):
            phases: list[tuple[str, int | None, str]] = [("baseline", None, "BASE")]
            rotation = (repeat_no - 1) % len(args.write_rates_mib_s)
            ordered_rates = args.write_rates_mib_s[rotation:] + args.write_rates_mib_s[:rotation]
            for rate in ordered_rates:
                phases.append(("pressure", rate, f"W{rate:06d}"))
                phases.append(("recovery", rate, f"REC{rate:06d}"))
            for phase, rate_mib_s, rate_token in phases:
                target_Bps = rate_mib_s * MIB if phase == "pressure" and rate_mib_s is not None else None
                point_id = f"R{repeat_no:03d}-{rate_token}"
                random_seed = observation_seed(repeat_no, phase, rate_mib_s or 0)
                job = measurement_job(
                    read_path,
                    write_path,
                    file_size_bytes,
                    args.read_block_kib * 1024,
                    args.write_block_kib * 1024,
                    args.duration_seconds,
                    args.ramp_seconds,
                    target_Bps,
                    random_seed,
                )
                try:
                    raw = fio_json(fio_path, job, args.duration_seconds + args.ramp_seconds + 120)
                    observation = make_observation(
                        point_id=point_id,
                        repeat_no=repeat_no,
                        phase=phase,
                        target_mib_s=rate_mib_s or 0,
                        random_seed=random_seed,
                        fio_payload=raw,
                    )
                    observation["fio_raw"] = raw
                    observation["fio_jobfile"] = job
                except (ProbeError, subprocess.TimeoutExpired) as exc:
                    observation = make_observation(
                        point_id=point_id,
                        repeat_no=repeat_no,
                        phase=phase,
                        target_mib_s=rate_mib_s or 0,
                        random_seed=random_seed,
                        fio_payload=None,
                        error=str(exc),
                    )
                    package["message"] = f"series stopped at {point_id}: {exc}"
                package["observations"].append(observation)
                if observation["status"] != "valid":
                    package["status"] = "partial"
                    break
            if package["status"] == "partial":
                break
        if package["status"] != "partial":
            package["status"] = "complete"
            exit_code = 0
    except (OSError, ProbeError, ValueError, subprocess.TimeoutExpired) as exc:
        package["message"] = str(exc)
    finally:
        if run_directory and run_directory.exists() and not args.keep_files:
            shutil.rmtree(run_directory, ignore_errors=True)
        package["completed_at_utc"] = utc_now()
        package["parameters"].setdefault("generated_files_removed", bool(run_directory and not run_directory.exists()))
        payload_json = json.dumps(package, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        payload_sha256 = hashlib.sha256(payload_json.encode("utf-8")).hexdigest()
        envelope = {
            "schema_version": 1,
            "payload_sha256": payload_sha256,
            "payload_json": payload_json,
        }
        output.parent.mkdir(parents=True, exist_ok=True)
        with output.open("x", encoding="utf-8") as output_file:
            output_file.write(json.dumps(envelope, ensure_ascii=False, indent=2) + "\n")
        print(f"package: {output}", file=sys.stderr)
        print(f"status: {package['status']}", file=sys.stderr)
        if package.get("message"):
            print(package["message"], file=sys.stderr)
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
