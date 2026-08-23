#!/usr/bin/env python3
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import discover_tests


class DiscoverTestsTest(unittest.TestCase):
    def write_source(self, directory, name, source):
        path = Path(directory) / name
        path.write_text(source, encoding="utf-8")
        return path

    def test_ignores_comments_and_literals(self):
        with tempfile.TemporaryDirectory() as directory:
            path = self.write_source(
                directory,
                "fixture.cpp",
                r'''
// TEST(comment_fixture, line_comment)
/* TEST_F(comment_fixture, block_comment) */
const char* normal = "TEST(string_fixture, normal_string)";
const char* raw = R"tag(TEST(raw_fixture, raw_string))tag";
const char* prefixed_raw = u8R"(TEST(raw_fixture, prefixed_raw_string))";
auto character = 'TEST(char_fixture, character_literal)';
TEST(real_fixture, real_test) {}
''',
            )

            declarations = discover_tests.discover_sources([path])

            self.assertEqual(
                [item.name for item in declarations], ["real_fixture.real_test"]
            )

    def test_discovers_multiline_test_and_test_f(self):
        with tempfile.TemporaryDirectory() as directory:
            path = self.write_source(
                directory,
                "fixture.cpp",
                """
TEST
(
    plain_fixture,
    plain_test
) {}
TEST_F /* between macro and arguments */
(
    class_fixture,
    fixture_test
) {}
""",
            )

            declarations = discover_tests.discover_sources([path])

            self.assertEqual(
                [item.name for item in declarations],
                ["plain_fixture.plain_test", "class_fixture.fixture_test"],
            )

    def test_rejects_duplicates_across_sources(self):
        with tempfile.TemporaryDirectory() as directory:
            first = self.write_source(
                directory, "first.cpp", "TEST_F(duplicate_fixture, same_test) {}"
            )
            second = self.write_source(
                directory, "second.cpp", "TEST(duplicate_fixture, same_test) {}"
            )

            with self.assertRaisesRegex(
                discover_tests.DiscoveryError,
                "duplicate test duplicate_fixture.same_test",
            ):
                discover_tests.discover_sources([first, second])

    def test_skips_disabled_suites_and_tests(self):
        with tempfile.TemporaryDirectory() as directory:
            path = self.write_source(
                directory,
                "fixture.cpp",
                """
TEST(DISABLED_fixture, suite_disabled) {}
TEST(fixture, DISABLED_test) {}
TEST(fixture, enabled_test) {}
""",
            )

            declarations = discover_tests.discover_sources([path])

            self.assertEqual(
                [item.name for item in declarations], ["fixture.enabled_test"]
            )

    def test_rejects_parameterized_macros(self):
        for macro in ("TEST_P", "TYPED_TEST", "INSTANTIATE_TEST_SUITE_P"):
            with self.subTest(macro=macro):
                with tempfile.TemporaryDirectory() as directory:
                    path = self.write_source(
                        directory,
                        "fixture.cpp",
                        f"{macro}(fixture, parameterized_test) {{}}",
                    )

                    with self.assertRaisesRegex(
                        discover_tests.DiscoveryError,
                        f"unsupported parameterized GTest macro {macro}",
                    ):
                        discover_tests.discover_sources([path])

    def test_verify_reports_runtime_mismatch(self):
        with tempfile.TemporaryDirectory() as directory:
            path = self.write_source(
                directory, "fixture.cpp", "TEST(fixture, source_test) {}"
            )
            completed = subprocess.CompletedProcess(
                args=["ace_tests", "--gtest_list_tests"],
                returncode=0,
                stdout="fixture.\n  executable_test\n",
                stderr="",
            )

            with mock.patch.object(
                discover_tests.subprocess, "run", return_value=completed
            ):
                with self.assertRaisesRegex(
                    discover_tests.DiscoveryError,
                    "Missing from executable: fixture.source_test",
                ):
                    discover_tests.verify("ace_tests", [path])

    def test_gtest_list_parser_skips_disabled_tests(self):
        output = """
DISABLED_suite.
  disabled_by_suite
fixture.
  DISABLED_test
  enabled_test
"""

        self.assertEqual(
            discover_tests.parse_gtest_list(output), ["fixture.enabled_test"]
        )


if __name__ == "__main__":
    unittest.main()
