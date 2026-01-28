#!/bin/python3
"""
This gem5 configuration script runs a "hello world" binary on a
Tagged Timing CPU
"""

import os
import json
import subprocess
import multiprocessing.dummy as mp
from multiprocessing import cpu_count

CWE_MAP = {
    "122": "Heap-based Buffer Overflow",
    # "124": "Buffer Underwrite",
    # "126": "Buffer Over-read",
    # "127": "Buffer Under-read",
    "415": "Double Free",
    "416": "Use After Free",
    "761": "Free of Pointer not at Start of Buffer",
}

JULIET_LOCATION = (
    "tests/test-progs/tagging/bin/riscv/linux/juliet/bin_scudo/CWE"
)

OUT_FILE = "m5out/tag.json"


def print_summary(results):
    for cwe, val in results.items():
        tag_faults = 0
        ldg_faults = 0
        print("========================")
        print(f"CWE {cwe}: {CWE_MAP[cwe]}")
        print("========================")
        print("===== Good Results =====")
        for code, test_cases in val["good"].items():
            print(f"{code}:\t{len(test_cases)}")
            for (_, reason) in test_cases:
                if reason == "tag":
                    tag_faults += 1
                elif reason == "ldg":
                    ldg_faults += 1
        print(f"{tag_faults} Tag faults and {ldg_faults} load faults")
        tag_faults = 0
        ldg_faults = 0
        print("===== Bad Results =====")
        for code, test_cases in val["bad"].items():
            print(f"{code}:\t{len(test_cases)}")
            for (_, reason) in test_cases:
                if reason == "tag":
                    tag_faults += 1
                elif reason == "ldg":
                    ldg_faults += 1
        print(f"{tag_faults} Tag faults and {ldg_faults} load faults")


thispath = os.path.dirname(os.path.realpath(__file__))
juliet_runner = os.path.join(thispath, "juliet-runner.py")

OUT_FILE = os.path.join(thispath, "../../", OUT_FILE)

results = {}
good_tag_faults = 0
bad_tag_faults = 0


def write_locked(l, cwe, is_good, key, value, reason=""):
    l.acquire()
    try:
        print(f"Done with {value}")
        if cwe not in results:
            results[cwe] = {}

        good_string = "good" if is_good else "bad"

        if cwe not in results:
            results[cwe] = {}
        if good_string not in results[cwe]:
            results[cwe][good_string] = {}
        if key not in results[cwe][good_string]:
            results[cwe][good_string][key] = []

        results[cwe][good_string][key].append((value, reason))

    finally:
        l.release()


def run(folder, file, cwe, is_good, l):

    binary = os.path.join(folder, file)
    if not os.path.isfile(binary):
        print("%s is not a file" % binary)
        return

    res = subprocess.run(
        ["/gem5/build/RISCV/gem5.opt", juliet_runner, binary],
        capture_output=True,
    )

    reason = (
        "tag"
        if "Tag check fault when" in res.stderr.decode()
        else ("ldg" if "tag loading not" in res.stderr.decode() else "")
    )
    write_locked(l, cwe, is_good, res.returncode, file, reason)


if __name__ == "__main__":
    p = mp.Pool(cpu_count())
    l = mp.Lock()
    args = []
    for cwe, _ in CWE_MAP.items():
        good_folder = os.path.join(
            thispath, "../../", JULIET_LOCATION + cwe, "good"
        )
        bad_folder = os.path.join(
            thispath, "../../", JULIET_LOCATION + cwe, "bad"
        )
        print(
            f"Evaluating {len(os.listdir(good_folder)) * 2} programs with CWE{cwe}"
        )
        args += [
            (good_folder, i, cwe, True, l) for i in os.listdir(good_folder)
        ]
        args += [
            (bad_folder, i, cwe, False, l) for i in os.listdir(bad_folder)
        ]

    p.starmap(run, args)

    p.close()
    p.join()
    print_summary(results)

    with open(OUT_FILE, "w") as f:
        json.dump(results, f, indent=4)
