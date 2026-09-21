#!/usr/bin/env python3
"""Run an ACE test with one consistent sanitizer and LSan policy."""

import argparse
import os
import subprocess
import sys


LSAN_UNAVAILABLE = 77
LSAN_PTRACE_MARKERS = (
    "LeakSanitizer has encountered a fatal error",
    "does not work under ptrace",
)


def sanitizer_set(value):
    return frozenset(item for item in value.split(",") if item)


def runtime_environment(sanitizers, detect_leaks):
    environment = os.environ.copy()
    if "address" in sanitizers:
        environment["ASAN_OPTIONS"] = (
            "halt_on_error=1:detect_leaks=" + ("1" if detect_leaks else "0")
        )
    if "undefined" in sanitizers:
        environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
    if "thread" in sanitizers:
        environment["TSAN_OPTIONS"] = "halt_on_error=1"
    return environment


def is_lsan_ptrace_failure(completed):
    output = (completed.stdout or "") + (completed.stderr or "")
    return all(marker in output for marker in LSAN_PTRACE_MARKERS)


def probe_lsan(executable, sanitizers, run=subprocess.run):
    return run(
        [executable, "--gtest_list_tests"],
        capture_output=True,
        text=True,
        check=False,
        env=runtime_environment(sanitizers, detect_leaks=True),
    )


def select_environment(executable, sanitizers, leak_mode, run=subprocess.run):
    if "address" not in sanitizers or leak_mode == "disabled":
        return runtime_environment(sanitizers, detect_leaks=False), False

    probe = probe_lsan(executable, sanitizers, run)
    if probe.returncode == 0:
        return runtime_environment(sanitizers, detect_leaks=True), False
    if leak_mode == "auto" and is_lsan_ptrace_failure(probe):
        return runtime_environment(sanitizers, detect_leaks=False), True

    if probe.stdout:
        print(probe.stdout, end="", file=sys.stderr)
    if probe.stderr:
        print(probe.stderr, end="", file=sys.stderr)
    raise RuntimeError("LSan capability probe failed")


def run_command(command, environment, run=subprocess.run):
    return run(command, env=environment, check=False).returncode


def parse_arguments(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--sanitizers", default="")
    parser.add_argument(
        "--leak-mode", choices=("auto", "enabled", "disabled"), default="auto"
    )
    parser.add_argument("--probe-executable", required=True)
    parser.add_argument("--probe-only", action="store_true")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    arguments = parser.parse_args(argv)
    if arguments.command and arguments.command[0] == "--":
        arguments.command.pop(0)
    if not arguments.probe_only and not arguments.command:
        parser.error("a command is required unless --probe-only is used")
    return arguments


def main(argv=None):
    arguments = parse_arguments(argv)
    sanitizers = sanitizer_set(arguments.sanitizers)
    try:
        environment, lsan_unavailable = select_environment(
            arguments.probe_executable, sanitizers, arguments.leak_mode
        )
    except RuntimeError as error:
        print(error, file=sys.stderr)
        return 1

    if arguments.probe_only:
        if lsan_unavailable:
            print("LSan unavailable under ptrace; capability check skipped", file=sys.stderr)
            return LSAN_UNAVAILABLE
        return 0

    if lsan_unavailable:
        print("LSan unavailable under ptrace; running correctness checks without leak detection", file=sys.stderr)
    return run_command(arguments.command, environment)


if __name__ == "__main__":
    sys.exit(main())
