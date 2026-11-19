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
Closed. See [Why this stopped](#why-this-stopped).

## Why this stopped
The goal was to guard chosen allocations with sampling turned off, so a bug
would not be missed because of a random pick. The pool is still 16 live
blocks. GWP-ASan already uses that pool, and its sample rate can be turned
up until it tries to guard every allocation. We did not find a way to do
more with the same 16 slots.

Work done:

- Side file. A script reads the unstripped binary and records the start and
  size of each function that calls an allocator by name. A call through a
  pointer is not recorded. Checked on curl and on a small fmt program.
- Hash slot. The binary is linked with an empty slot. The hash of the side
  file is written into it. Startup checks that hash and the build id. A
  match keeps the rows in memory. A miss keeps none, and the program keeps
  running. Checked on curl.
- Sampling is off in this tree. GWP-ASan's sample check returns no, and
  Scudo no longer calls it.

Explored, not built:

- Guard every block from a function in the side file, and let no other
  caller take a slot. The rows are loaded. Nothing reads the caller on
  malloc. A call through a pointer is missing, or a small wrapper takes the
  slots for every caller.
- Remember a caller address and skip it after the first guarded block. A
  loop stays on the same address, so a later pass is not guarded. That later
  pass can be the one that overflows or uses the block after free.
- Remember the address and the size. A new size is guarded once. The same
  size with a different use is not. The use happens after malloc returns.
- A compiler tag on the malloc site. One call is one tag. A loop that
  sometimes overflows and sometimes does not is still one tag.

Same caller and same size can be a good block one time and a bad block the
next. Nothing known at malloc tells them apart. Spending the 16 slots on a
chosen function is a smaller sampler, not a better one.

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