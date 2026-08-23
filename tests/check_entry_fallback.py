#!/usr/bin/env python3
import subprocess
import sys


def main():
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} EXECUTABLE", file=sys.stderr)
        return 2

    result = subprocess.run(
        [sys.argv[1]],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 126:
        print(
            f"expected exit 126, got {result.returncode}\n"
            f"stdout:\n{result.stdout}\n"
            f"stderr:\n{result.stderr}",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
