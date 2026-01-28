# MTIO Demo and Code Artifacts (gem5 + LLVM)

This package contains the code changes used for our **MTIO — Memory Tagging with Intra-Object Detection** prototype.
Additionally, a small demo setup is provided to showcase the bug detection capabilities.

---

## Overview

MTIO combines:
- **4-bit color tags** at 16-byte granularity for object-level spatial/temporal checks, and
- **per-byte “shade” bits** (parity) for intra-object (field/member) checks,

with lightweight pointer metadata to enforce checks during memory accesses.

---

## Repository Layout

- `iodemo/` — Small docker-based demo 
- `gem5/` — hardware simulation (RISC-V timing model modifications)
- `llvm-project/` — compiler + runtime instrumentation (LLVM/Clang + Scudo)

---


## iodemo (Docker-based demo of MTIO)

**Overview:** Demo contains prebuilt gem5 simulation binary and minimal test cases (out-of-bounds, intra-object out-of-bounds, and use-after-free) to showcase the prototype. Running the entire evaluation is not possible, since the simulation takes more multiple weeks.

**Prerequisites:** Installed Docker, at least 16 GB RAM. 

To run, execute the following:
```
cd iodemo
docker build -t mtio-demo .
docker run --rm -it mtio-demo /bin/bash
./gem5.opt shading-riscv.py shading_examples/bin/oob
./gem5.opt shading-riscv.py shading_examples/bin/iooob
./gem5.opt shading-riscv.py shading_examples/bin/uaf
```

In all cases the simulation will crash and report the tag check fault at the target address. Testcase sources can be found at iodemo/shading_examples.

---

## gem5 (Hardware Simulation)

**Main change:** Extend the load/store pipeline to fetch and verify tag metadata, introduce new tagging instructions.

- **Pipeline changes:** `gem5/src/cpu/simple/timing.cc` 
  - Integrates MTIO checks into the timing CPU’s memory access path:
    - Retrieves **color** (16:1) and **shade** (1:1) metadata.
    - Compares pointer-embedded metadata against tag memory.
    - Enforces object-level color match and intra-object shade policy.
---

## LLVM (Compiler Instrumentation + Runtime)

**1) Stack instrumentation (MT / MTIO):** 
- **New pass:** `llvm-project/llvm/lib/Target/RISCV/RISCVStackTagging.cpp` 
  - Inserts instrumentation for stack tagging
  - Inserts instrumentation for shade metadata management.

**2) Heap instrumentation (Scudo allocator):** 
- **Hardware tagging support:** `llvm-project/compiler-rt/lib/scudo/standalone/memtag.h` 
  - Assigns color tags on allocation.
