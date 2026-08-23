#!/usr/bin/env python3
import argparse
from dataclasses import dataclass
from pathlib import Path
import subprocess
import sys


SUPPORTED_MACROS = {"TEST", "TEST_F"}
UNSUPPORTED_MACROS = {
    "TEST_P",
    "TYPED_TEST",
    "TYPED_TEST_P",
    "TYPED_TEST_SUITE",
    "TYPED_TEST_CASE",
    "TYPED_TEST_SUITE_P",
    "TYPED_TEST_CASE_P",
    "REGISTER_TYPED_TEST_SUITE_P",
    "REGISTER_TYPED_TEST_CASE_P",
    "INSTANTIATE_TEST_SUITE_P",
    "INSTANTIATE_TEST_CASE_P",
    "INSTANTIATE_TYPED_TEST_SUITE_P",
    "INSTANTIATE_TYPED_TEST_CASE_P",
}
RAW_STRING_PREFIXES = ("u8R\"", "uR\"", "UR\"", "LR\"", "R\"")


class DiscoveryError(RuntimeError):
    pass


@dataclass(frozen=True)
class Token:
    value: str
    line: int


@dataclass(frozen=True)
class TestDeclaration:
    name: str
    path: Path
    line: int


def _is_identifier_character(character):
    return character == "_" or character.isalnum()


def _raw_string_end(source, start):
    if start > 0 and _is_identifier_character(source[start - 1]):
        return None

    for prefix in RAW_STRING_PREFIXES:
        if not source.startswith(prefix, start):
            continue

        delimiter_start = start + len(prefix)
        opening = source.find("(", delimiter_start, delimiter_start + 17)
        if opening == -1:
            raise DiscoveryError("invalid raw string delimiter")
        delimiter = source[delimiter_start:opening]
        if any(character.isspace() or character in "()\\" for character in delimiter):
            raise DiscoveryError("invalid raw string delimiter")

        marker = ")" + delimiter + "\""
        closing = source.find(marker, opening + 1)
        if closing == -1:
            raise DiscoveryError("unterminated raw string literal")
        return closing + len(marker)
    return None


def tokenize(source, path):
    tokens = []
    index = 0
    line = 1

    while index < len(source):
        try:
            raw_end = _raw_string_end(source, index)
        except DiscoveryError as error:
            raise DiscoveryError(f"{path}:{line}: {error}") from error
        if raw_end is not None:
            line += source.count("\n", index, raw_end)
            index = raw_end
            continue

        if source.startswith("//", index):
            newline = source.find("\n", index + 2)
            if newline == -1:
                break
            index = newline
            continue

        if source.startswith("/*", index):
            closing = source.find("*/", index + 2)
            if closing == -1:
                raise DiscoveryError(f"{path}:{line}: unterminated block comment")
            end = closing + 2
            line += source.count("\n", index, end)
            index = end
            continue

        character = source[index]
        if character in "\"'":
            quote = character
            literal_line = line
            index += 1
            while index < len(source):
                if source[index] == "\\":
                    if index + 1 < len(source):
                        if source[index + 1] == "\n":
                            line += 1
                        index += 2
                        continue
                if source[index] == quote:
                    index += 1
                    break
                if source[index] == "\n":
                    line += 1
                index += 1
            else:
                raise DiscoveryError(f"{path}:{literal_line}: unterminated literal")
            continue

        if character.isspace():
            if character == "\n":
                line += 1
            index += 1
            continue

        if character == "_" or character.isalpha():
            end = index + 1
            while end < len(source) and _is_identifier_character(source[end]):
                end += 1
            tokens.append(Token(source[index:end], line))
            index = end
            continue

        tokens.append(Token(character, line))
        index += 1

    return tokens


def declarations_from_source(path):
    path = Path(path)
    source = path.read_text(encoding="utf-8")
    tokens = tokenize(source, path)
    declarations = []

    for index, token in enumerate(tokens):
        if token.value not in SUPPORTED_MACROS | UNSUPPORTED_MACROS:
            continue
        if index + 1 >= len(tokens) or tokens[index + 1].value != "(":
            continue
        if token.value in UNSUPPORTED_MACROS:
            raise DiscoveryError(
                f"{path}:{token.line}: unsupported parameterized GTest macro "
                f"{token.value}"
            )

        invocation = tokens[index:index + 6]
        if (
            len(invocation) != 6
            or invocation[1].value != "("
            or not invocation[2].value.isidentifier()
            or invocation[3].value != ","
            or not invocation[4].value.isidentifier()
            or invocation[5].value != ")"
        ):
            raise DiscoveryError(
                f"{path}:{token.line}: malformed {token.value} declaration"
            )

        suite = invocation[2].value
        test = invocation[4].value
        if suite.startswith("DISABLED_") or test.startswith("DISABLED_"):
            continue
        declarations.append(TestDeclaration(f"{suite}.{test}", path, token.line))

    return declarations


def discover_sources(paths):
    declarations = []
    declarations_by_name = {}
    for path in paths:
        for declaration in declarations_from_source(path):
            previous = declarations_by_name.get(declaration.name)
            if previous is not None:
                raise DiscoveryError(
                    f"duplicate test {declaration.name}: "
                    f"{previous.path}:{previous.line} and "
                    f"{declaration.path}:{declaration.line}"
                )
            declarations_by_name[declaration.name] = declaration
            declarations.append(declaration)
    return declarations


def parse_gtest_list(output):
    tests = []
    current_suite = None
    for raw_line in output.splitlines():
        if not raw_line.strip():
            continue
        if raw_line.startswith("  "):
            if current_suite is None:
                raise DiscoveryError("test listed before its GTest suite")
            test = raw_line.strip().split(maxsplit=1)[0]
            suite_name = current_suite.removesuffix(".")
            if suite_name.startswith("DISABLED_") or test.startswith("DISABLED_"):
                continue
            tests.append(f"{current_suite}{test}")
        else:
            current_suite = raw_line.strip().split(maxsplit=1)[0]
            if not current_suite.endswith("."):
                raise DiscoveryError(
                    f"unexpected --gtest_list_tests output: {raw_line!r}"
                )
    return tests


def verify(executable, source_paths):
    source_tests = {item.name for item in discover_sources(source_paths)}
    result = subprocess.run(
        [executable, "--gtest_list_tests"],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        raise DiscoveryError(
            f"{executable} --gtest_list_tests exited {result.returncode}:\n"
            f"{result.stderr.strip()}"
        )

    executable_tests = set(parse_gtest_list(result.stdout))
    missing = sorted(source_tests - executable_tests)
    unexpected = sorted(executable_tests - source_tests)
    if missing or unexpected:
        details = []
        if missing:
            details.append("Missing from executable: " + ", ".join(missing))
        if unexpected:
            details.append("Missing from sources: " + ", ".join(unexpected))
        raise DiscoveryError("GTest discovery mismatch:\n" + "\n".join(details))


def parse_arguments(argv):
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)

    discover = commands.add_parser("discover")
    discover.add_argument("sources", nargs="+")

    verify_parser = commands.add_parser("verify")
    verify_parser.add_argument("executable")
    verify_parser.add_argument("sources", nargs="+")
    return parser.parse_args(argv)


def main(argv=None):
    arguments = parse_arguments(argv)
    try:
        if arguments.command == "discover":
            for declaration in discover_sources(arguments.sources):
                print(declaration.name)
        else:
            verify(arguments.executable, arguments.sources)
    except (DiscoveryError, OSError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
