#!/bin/python3
"""
This gem5 configuration script runs a "hello world" binary on a
Tagged Timing CPU
"""

import os
import subprocess
import shutil


def print_summary(good, bad, gtfaults, btfaults):
    print("===== Good Results =====")
    for k, v in good.items():
        print(f"{k}:\t{len(v)}")
    print(f"{gtfaults} tag mismatches encountered")
    print()
    print("===== Bad Results =====")
    for k, v in bad.items():
        print(f"{k}:\t{len(v)}")
    print(f"{btfaults} tag mismatches encountered")


# Source: https://stackoverflow.com/a/34325723
# Print iterations progress
def printProgressBar(
    iteration,
    total,
    prefix="",
    suffix="",
    decimals=1,
    length=80,
    fill="█",
    printEnd="\r",
):
    """
    Call in a loop to create terminal progress bar
    @params:
        iteration   - Required  : current iteration (Int)
        total       - Required  : total iterations (Int)
        prefix      - Optional  : prefix string (Str)
        suffix      - Optional  : suffix string (Str)
        decimals    - Optional  : positive number of decimals in percent complete (Int)
        length      - Optional  : character length of bar (Int)
        fill        - Optional  : bar fill character (Str)
        printEnd    - Optional  : end character (e.g. "\r", "\r\n") (Str)
    """
    percent = ("{0:." + str(decimals) + "f}").format(
        100 * (iteration / float(total))
    )
    filledLength = int(length * iteration // total)
    bar = fill * filledLength + "-" * (length - filledLength)
    print(" " * shutil.get_terminal_size().columns, end="\r")
    print(f"\r{prefix} |{bar}| {percent}% {suffix}", end=printEnd)
    # Print New Line on Complete
    if iteration == total:
        print()


juliet_location = (
    "tests/test-progs/tagging/bin/riscv/linux/juliet/bin_scudo/CWE122"
)

thispath = os.path.dirname(os.path.realpath(__file__))
juliet_runner = os.path.join(thispath, "juliet-runner.py")

good_folder = os.path.join(thispath, "../../", juliet_location, "good")

bad_folder = os.path.join(thispath, "../../", juliet_location, "bad")

good_results = {}
good_tag_faults = 0
bad_results = {}
bad_tag_faults = 0

total_iter = len(os.listdir(good_folder)) * 2
cur_iter = 0


for binary in os.listdir(good_folder):
    printProgressBar(cur_iter, total_iter, suffix=f"[{binary}]")
    binary = os.path.join(good_folder, binary)

    if not os.path.isfile(binary):
        print("%s is not a file" % binary)
        continue

    res = subprocess.run(
        ["/gem5/build/RISCV/gem5.opt", juliet_runner, binary],
        capture_output=True,
    )

    if res.returncode not in good_results:
        good_results[res.returncode] = []
    good_results[res.returncode].append(binary)

    if "Tag check fault when accessing" in res.stderr.decode():
        good_tag_faults += 1

    cur_iter += 1


for binary in os.listdir(bad_folder):
    printProgressBar(cur_iter, total_iter, suffix=f"[{binary}]")
    binary = os.path.join(bad_folder, binary)

    if not os.path.isfile(binary):
        print(f"{binary} is not a file")
        continue

    res = subprocess.run(
        ["/gem5/build/RISCV/gem5.opt", juliet_runner, binary],
        capture_output=True,
    )

    if res.returncode not in bad_results:
        bad_results[res.returncode] = []
    bad_results[res.returncode].append(binary)

    if "Tag check fault when accessing" in res.stderr.decode():
        bad_tag_faults += 1

    cur_iter += 1

print_summary(good_results, bad_results, good_tag_faults, bad_tag_faults)
