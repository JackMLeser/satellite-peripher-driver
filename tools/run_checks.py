#!/usr/bin/env python3
"""Build and validate sat-peripheral-driver-demo.

A small CI-style helper that configures the CMake build, compiles, runs the
unit-test binary, and runs the demo while sanity-checking its output. This is
the kind of host-side scripting often used to wrap embedded test binaries in a
continuous-integration / functional-validation pipeline.

Usage:
    python3 tools/run_checks.py
"""
from __future__ import annotations

import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
BUILD = ROOT / "build"


def run(cmd: list[str], cwd: pathlib.Path) -> subprocess.CompletedProcess[str]:
    print(f"$ {' '.join(cmd)}  (cwd={cwd})")
    proc = subprocess.run(cmd, cwd=cwd, text=True, capture_output=True)
    if proc.stdout:
        print(proc.stdout, end="")
    if proc.returncode != 0:
        print(proc.stderr, file=sys.stderr)
    return proc


def main() -> int:
    BUILD.mkdir(exist_ok=True)

    if run(["cmake", ".."], cwd=BUILD).returncode != 0:
        print("FAILED: cmake configure")
        return 1
    if run(["cmake", "--build", "."], cwd=BUILD).returncode != 0:
        print("FAILED: build")
        return 1

    tests = run(["./sat_tests"], cwd=BUILD)
    if tests.returncode != 0 or "0 failure(s)" not in tests.stdout:
        print("FAILED: unit tests")
        return 1

    demo = run(["./sat_demo"], cwd=BUILD)
    expected = ["25.00 C", "3.300 V", "after recovery"]
    if demo.returncode != 0 or not all(token in demo.stdout for token in expected):
        print("FAILED: demo output check")
        return 1

    print("\nAll checks passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
