# warden

Continuous memory-safety checks on critical paths.

## What
Warden checks memory rules (use-after-free, buffer overflow and underflow, invalid
free) on marked critical paths while software runs in production. It
catches violations under real traffic and real timing, and works
alongside existing protections (ASLR, sandboxes, confidential
computing) rather than replacing them.

## Why
Memory-safety bugs still cause serious failures in production C and
C++ systems. Testing misses rare races and production-only inputs.
Google's GWP-ASan catches some of these by sampling a tiny fraction
of allocations, so many bugs are never hit. Continuous checks on the
paths that matter close that gap.

## Approach
Start from GWP-ASan and extend it to continuous checking on
explicitly marked paths, so every allocation and free on those paths
is checked.

Constraints: no recompiling the target, no late detection, regular
hardware.

## Status
Early stage. First step: run GWP-ASan as-is on LLVM (Scudo), then on
TCMalloc, to set a baseline.

## Scope and responsible use
Targets are limited to open-source code, bug-bounty programs, or
systems with written permission. Findings stay private until the
owner has a chance to fix them.

## References
- Paper: https://arxiv.org/abs/2311.09394
- LLVM docs: https://llvm.org/docs/GwpAsan.html
- LLVM source: https://github.com/llvm/llvm-project/tree/main/compiler-rt/lib/gwp_asan
- TCMalloc: https://google.github.io/tcmalloc/gwp-asan.html

## License
TBD