#!/usr/bin/env python3
"""Check QR table dependencies using timestamps visible to GNU Make 3.81.

Run without another build in this source tree. The touched header is restored,
but the test intentionally rebuilds dependent objects and executables.
"""
import hashlib
import json
import os
import pathlib
import subprocess
import time

SECOND_NS = 1_000_000_000


def wait_until(timestamp_ns, deadline):
    """Avoid future file dates while bounding waits if the wall clock moves."""
    while True:
        remaining = (timestamp_ns - time.time_ns()) / SECOND_NS
        if remaining <= 0:
            return
        if time.monotonic() >= deadline:
            raise RuntimeError("cannot safely separate build timestamps within 10 seconds; "
                               "check future file dates or a changing system clock "
                               f"(target_ns={timestamp_ns}, wall_ns={time.time_ns()})")
        time.sleep(min(remaining, 0.05))


def touch_after_targets(header, original_stat, before):
    # Some Make/filesystem combinations compare only whole seconds, including
    # the observed native Make 3.81 setup. Allow two-second filesystem ticks
    # as well, then verify the timestamp actually stored.
    started = time.monotonic()
    deadline = started + 10
    latest_second = max(before.values()) // SECOND_NS
    requested_ns = (max(time.time_ns() // SECOND_NS, latest_second) + 2) * SECOND_NS
    wait_until(requested_ns, deadline)
    os.utime(header, ns=(original_stat.st_atime_ns, requested_ns))
    actual_ns = header.stat().st_mtime_ns
    if actual_ns // SECOND_NS <= latest_second:
        raise RuntimeError("filesystem did not preserve a whole-second header timestamp "
                           "newer than the build targets "
                           f"(header_ns={actual_ns}, baseline_second={latest_second})")
    # A filesystem may round upward. Start make only after that stored time.
    wait_until(actual_ns, deadline)
    return {"requested_header_mtime_ns": requested_ns,
            "actual_header_mtime_ns": actual_ns,
            "latest_baseline_target_second": latest_second,
            "wait_seconds": time.monotonic() - started,
            "make_start_wall_ns": time.time_ns()}


def require(condition, message):
    # Keep this regression control effective even under python -O.
    if not condition:
        raise RuntimeError(message)

root = pathlib.Path(__file__).resolve().parents[1]
subprocess.run(["make", "-j4", "all"], cwd=root, check=True)
header = root / "src/qr_tables.hpp"
original_stat = header.stat()
original_sha = hashlib.sha256(header.read_bytes()).hexdigest()
targets = ["grid.o", "rs.o", "libspecqr_reader.a", "specqr-reader", "consumer"]
before = {name: (root / "build" / name).stat().st_mtime_ns for name in targets}
try:
    separation = touch_after_targets(header, original_stat, before)
    completed = subprocess.run(["make", "-j4", "all"], cwd=root, check=True,
                               text=True, capture_output=True)
    after = {name: (root / "build" / name).stat().st_mtime_ns for name in targets}
    require(after["grid.o"] > before["grid.o"], "table header did not rebuild grid.o")
    require(after["rs.o"] == before["rs.o"], "unrelated RS object was rebuilt")
    for name in ("libspecqr_reader.a", "specqr-reader", "consumer"):
        require(after[name] > before[name], "table header did not relink " + name)
    require(hashlib.sha256(header.read_bytes()).hexdigest() == original_sha,
            "QR table header contents changed during dependency control")
    print(json.dumps({"status": "passed", "header_sha256": original_sha,
                      "before_mtime_ns": before, "after_mtime_ns": after,
                      "timestamp_separation": separation,
                      "incremental_build_stdout": completed.stdout}, indent=2))
finally:
    os.utime(header, ns=(original_stat.st_atime_ns, original_stat.st_mtime_ns))
