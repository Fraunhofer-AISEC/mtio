# MTIO Code Artifacts (gem5 + LLVM)


Memory Tagging with Intra-Object Detection

This repository contains the prototype presented in:

Konrad Hohentanner, Lukas Hertel, Minu Föger, Lukas Auer, and Julian Horsch. "Memory Tagging with Intra-Object Detection: Bridging the Gap in Fine-Grained Memory Safety." In Proceedings of the ACM Asia Conference on Computer and Communications Security, pp. 1000-1012. 2026.

Paper PDF: https://dl.acm.org/doi/pdf/10.1145/3779208.3785284

This package contains the code changes used for our **MTIO — Memory Tagging with Intra-Object Detection** prototype.

---

## Overview

MTIO combines:
- **4-bit color tags** at 16-byte granularity for object-level spatial/temporal checks, and
- **per-byte “shade” bits** (parity) for intra-object (field/member) checks,

with lightweight pointer metadata to enforce checks during memory accesses.

---

## Repository Layout

- `gem5/` — hardware simulation (RISC-V timing model modifications)
- `llvm-project/` — compiler + runtime instrumentation (LLVM/Clang + Scudo)


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
