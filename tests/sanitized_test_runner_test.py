#!/usr/bin/env python3
import contextlib
import io
from pathlib import Path
import subprocess
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tests import sanitized_test_runner


class SanitizedTestRunnerTest(unittest.TestCase):
    def completed(self, returncode=0, stdout="", stderr=""):
        return subprocess.CompletedProcess([], returncode, stdout, stderr)

    def test_runtime_environment_enables_requested_sanitizers(self):
        environment = sanitized_test_runner.runtime_environment(
            {"address", "undefined"}, detect_leaks=True
        )
        self.assertEqual("halt_on_error=1:detect_leaks=1", environment["ASAN_OPTIONS"])
        self.assertEqual("halt_on_error=1:print_stacktrace=1", environment["UBSAN_OPTIONS"])

    def test_auto_lsan_disables_only_ptrace_lsan_failure(self):
        def run(*_args, **_kwargs):
            return self.completed(
                1,
                stderr=(
                    "LeakSanitizer has encountered a fatal error. "
                    "LeakSanitizer does not work under ptrace"
                ),
            )

        environment, skipped = sanitized_test_runner.select_environment(
            "ace_tests", {"address"}, "auto", run
        )
        self.assertTrue(skipped)
        self.assertEqual("halt_on_error=1:detect_leaks=0", environment["ASAN_OPTIONS"])

    def test_enabled_lsan_rejects_unavailable_environment(self):
        def run(*_args, **_kwargs):
            return self.completed(
                1,
                stderr=(
                    "LeakSanitizer has encountered a fatal error. "
                    "LeakSanitizer does not work under ptrace"
                ),
            )

        with contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaisesRegex(RuntimeError, "LSan capability probe failed"):
                sanitized_test_runner.select_environment(
                    "ace_tests", {"address"}, "enabled", run
                )

    def test_non_address_profile_never_runs_lsan_probe(self):
        def run(*_args, **_kwargs):
            self.fail("non-ASan profiles must not execute an LSan probe")

        environment, skipped = sanitized_test_runner.select_environment(
            "ace_tests", {"thread"}, "auto", run
        )
        self.assertFalse(skipped)
        self.assertEqual("halt_on_error=1", environment["TSAN_OPTIONS"])

    def test_unexpected_lsan_probe_failure_is_never_suppressed(self):
        def run(*_args, **_kwargs):
            return self.completed(1, stderr="AddressSanitizer: heap-use-after-free")

        with contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaisesRegex(RuntimeError, "LSan capability probe failed"):
                sanitized_test_runner.select_environment(
                    "ace_tests", {"address"}, "auto", run
                )

    def test_run_command_forwards_environment_and_exit_code(self):
        observed = {}

        def run(command, **kwargs):
            observed["command"] = command
            observed["environment"] = kwargs["env"]
            observed["check"] = kwargs["check"]
            return self.completed(23)

        environment = {"ASAN_OPTIONS": "halt_on_error=1:detect_leaks=1"}
        result = sanitized_test_runner.run_command(
            ["ace_tests", "--gtest_list_tests"], environment, run
        )

        self.assertEqual(23, result)
        self.assertEqual(["ace_tests", "--gtest_list_tests"], observed["command"])
        self.assertIs(environment, observed["environment"])
        self.assertFalse(observed["check"])

    def test_probe_only_does_not_require_a_child_command(self):
        arguments = sanitized_test_runner.parse_arguments(
            ["--probe-executable", "ace_tests", "--probe-only"]
        )

        self.assertTrue(arguments.probe_only)
        self.assertEqual([], arguments.command)


if __name__ == "__main__":
    unittest.main()
