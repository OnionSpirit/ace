#!/usr/bin/env python3
import subprocess
import sys

TEST_NAME = "futures.cutex_race"
BINARY = "./build/ace_tests"
TIMEOUT = 1.5

def main():
    for iteration in range(100):
        iteration += 1
        print(f"[{iteration}] Running {TEST_NAME}...", end=" ", flush=True)
        try:
            result = subprocess.run(
                [BINARY, f"--gtest_filter={TEST_NAME}"],
                capture_output=True,
                text=True,
                timeout=TIMEOUT
            )
        except subprocess.TimeoutExpired:
            print(f"Timeout : [ RunTime > {TIMEOUT} sec]")
            continue
        if result.returncode == 0:
            print("Ok")
        else:
            print("Failed")
            print(result.stdout)
            print(result.stderr)
            sys.exit(1)

if __name__ == "__main__":
    main()
