# warden: design notes
 
**Status:** Early. Design only. Nothing is built or measured yet.
 
**Convention:** **[S]** = from a source (links at the end). **[O]** = our idea, untested.
 
---
 
## 1. Goal
 
Catch use-after-free, buffer overflow, and invalid free on **marked critical paths** of live C/C++ software, under real traffic and real timing.
 
- Complements ASLR, sandboxes, and confidential computing. It does not replace them.
- Humans and agents can both enable it.
- Targets: open-source code, bug-bounty programs, or systems with written permission.
- Findings stay private until the owner can fix them.
**Constraints we set**
- No recompiling the target.
- No late detection (checks must fire at the moment of the bad access).
- Regular hardware. ARM MTE is out for now.
- Make pages cheaper rather than replace them.
---
 
## 2. Baseline: GWP-ASan
 
GWP-ASan is a sampled allocator framework. A tiny random fraction of allocations is guarded, and the hardware traps any bad access.
 
**How it catches bugs [S]**
- Each sampled allocation gets its own slot with an inaccessible guard page on each side.
- Overflow or underflow touches a guard page, the CPU faults, the OS sends SIGSEGV, and GWP-ASan's handler prints a report with stack traces.
- On free, the slot is made inaccessible, so a later touch faults (use-after-free). Detection is transient: freed slots are randomly reused for later allocations.
- Double free and invalid free are caught by bookkeeping on the pool's own records, not by guard pages.
- Each allocation is randomly left- or right-aligned in its slot, to cover underflow and overflow equally.
**Defaults in LLVM's version [S]**
- `SampleRate` = 5000 (a 1-in-5000 chance per allocation).
- `MaxSimultaneousAllocations` = 16 slots.
- About 40 KiB fixed memory overhead per process.
- `PerfectlyRightAlign` = false.
**The gap:** Sampling means rare bugs are rarely hit.
 
**Name:** GWP = Google-Wide Profiling (sample a little across a lot of machines), ASan = AddressSanitizer. Official expansion: "GWP-ASan Will Provide Allocation SANity".
 
---
 
## 3. Alternatives and why not
 
| Option | Reason |
|---|---|
| ASan | Needs recompiling, about 2x slower and larger |
| Valgrind | 10 to 50x slower |
| ARM MTE | Needs specific ARM chips (out for now) |
| Canaries and poisoning | Catch bugs too late |
| Rust | Needs a full rewrite |
 
Electric Fence, Guard Malloc (macOS), and PageHeap (Windows) put guard pages on every allocation, which is close to our idea, but they are testing tools and too slow for production.
 
---
 
## 4. Core idea
 
GWP-ASan with **sampling off on marked critical paths**. Every allocation and free on those paths is guarded. The rest of the program stays sampled or unchecked.
 
"Continuous" means no allocation on the marked path is left unwatched. Nothing sweeps memory. The hardware traps at the moment of the bad access.
 
---
 
## 5. Limits with sampling off, and the plan
 
| # | Limit | Plan | Status |
|---|---|---|---|
| 1 | Wasted RAM, use-after-free | Virtual aliasing | Workaround. Adds mappings. |
| 2 | Wasted RAM, overflow | Two tiers (section 6). Fillers share the guarded object's page. | Partly solved. Depends on filler supply. Untested. |
| 3 | Kernel mapping cap (about 65k by default on Linux) | Treat as a fixed budget | Managed by rules and the agent. Aliasing uses more of it. |
| 4 | Permission calls (every alloc and free changes page permissions) | Helper thread unlocks slots ahead of time | Alloc side removes the wait in `malloc`. Free side is an accepted limit. |
| 5 | Pool size | Agent plans the budget, allocator rules enforce it | Managed. It is the dial between 1 to 4. |
| 6 | One-sided guard | Agent picks the side per allocation site | Partly solved. One object cannot cover both sides. |
| 7 | Cache and TLB pressure | Keep live guarded objects few | Low priority |
| 8 | Alignment gap | Agent picks neat or perfect-fit alignment per site | Handled with a trade-off |
 
**Why pages are expensive.** A guarded 100-byte array takes a whole 4 KB page, so RAM use is about 40x for small objects. Large buffers barely notice, since one extra page is a rounding error. Small objects are the problem.
 
```
[ guard ][ ...... unused ...... | array (100 B) ][ guard ]
```
 
**Limit 4, in detail.**
- Each slot is open (usable) or locked (no access). Every switch is an OS call.
- Free side: the lock must happen immediately, or use-after-free is missed.
- Alloc side: a reused slot is still locked and must be opened before the program gets it. A helper thread can open slots ahead of time. The trade-off is a shorter window for catching stale pointers.
- Each permission change forces other CPU cores to drop stale address-translation entries (TLB shootdown). Other cores still get interrupted. **[S: paper names this as the main cost of user-space permission changes. KFENCE avoids it on x86 by invalidating only the local TLB, at the price of a few missed bugs.]**
- A single call is cheap (microseconds). The cost is volume: about 2 calls per guarded allocation.
**Limit 6, in detail.** Place `buf[100]` flush against the right guard page. `buf[100]` (overflow) hits the guard and is caught. `buf[-1]` (underflow) lands in the unused space, which is valid memory, so it is missed. **[S: Android data in the paper shows overflows are far more common than underflows, 27% vs 4% of crashes.]**
 
**Limit 7, in detail.** The TLB holds one entry per page and has room for tens to a few thousand. 40 small objects normally share 1 page (1 entry). Guarded, they take 40 pages (40 entries). This only matters when many guarded objects are touched in quick succession.
 
**Limit 8, in detail.** The CPU wants aligned addresses (multiples of 8 or 16). A 100-byte array rounds to 112 bytes, leaving a 12-byte gap before the guard page. An overflow of 1 to 12 bytes lands in the gap and is missed.
- `PerfectlyRightAlign` closes the gap and catches single-byte overflows. **[S: the docs say it costs performance and may be incompatible with some architectures.]**
- **[O: the risk is false crashes.]** Misaligned addresses can slow some CPUs, make some SIMD instructions fault, and break code that assumes `malloc` alignment. A false crash on a critical path is the worst outcome. The agent should enable perfect fit only for plain byte buffers (strings, network data) and keep neat alignment for anything used with SIMD, atomics, or aligned structs.
---
 
## 6. Two-tier design **[O]**
 
```
[ filler objects ...... | guarded object ][ guard page ]
        one real page                         no access
```
 
| Tier | Gets | Protection |
|---|---|---|
| **Guarded** (end of page) | Riskiest objects | Overflow (or underflow, one side) plus use-after-free |
| **Filler** (front of page) | Everything else on the marked path | Use-after-free only |
 
**Why aliasing alone is not enough.** Aliasing hands each object out through its own virtual address of a shared real page. Freeing one object locks only its alias, so its neighbors keep working and RAM is not wasted. That fixes use-after-free waste. But an alias keeps the object at the same spot in the page. Overflow detection needs the object at the very end of a page, next to a guard, and only one object can be last. So aliasing covers use-after-free, and the end-of-page position covers overflow. The two tiers use both.
 
**Tier choice.** The allocator picks the tier at allocation time, following the agent's policy. An object cannot move afterward, so it is dynamic across allocations, not for a live object.
 
**What the filler design depends on**
- **Filler supply:** The page's RAM is recovered only if enough small filler objects are allocated. On a path with few allocations the page stays mostly empty and the waste returns.
- **Page lifetime:** A host page cannot go back to the OS until every object on it is freed. One long-lived filler keeps it alive.
- **Placement control:** The allocator must control where objects sit inside pages.
- **Mappings:** Each guarded object adds a guard page and an alias, so it uses the kernel mapping budget even though RAM is saved.
Virtual aliasing for use-after-free is known from research (Oscar, 2017). That reference is from memory and not verified.
 
---
 
## 7. Roles **[O]**
 
- **Allocator rules (inside `malloc`, fast):**
  - KFENCE-style rules. **[S: once the pool is 75% full, KFENCE skips a new allocation if one with the same allocation stack trace is already guarded.]**
  - Rate and lifetime limits.
  - Applying the agent's policy through a quick lookup. The agent is never consulted per allocation.
- **Agent (outside `malloc`, slower):**
  - Picks critical paths that fit the budget.
  - Plans the pool budget: allocation rate x object lifetime.
  - Chooses the tier, guard side, and alignment per allocation site.
  - Tunes the rules, and drops the least critical paths when the pool is full.
  - Prefers paths that are critical and light on allocations. Critical is not the same as hot.
- **Helper thread:** Unlocks slots ahead of time.
- **Aliasing:** Makes the filler tier cheap.
**Pool budget.** What fills the pool is allocations per second x how long each object lives. 1,000 allocations/sec at 0.01 s lifetime is about 10 slots. The same rate at 10 s lifetime is about 10,000 slots. Freed slots held back for use-after-free detection count too.
 
**Agent ideas to explore later.** The paper's future-work list includes raising sampling for allocations in earlier bug reports and steering the budget toward one process to hunt hard bugs. **[S]** The agent could also use past bug reports per allocation site, and how a buffer is used (for example, whether an index grows upward from outside input) to pick the guard side. **[O]**
 
---
 
## 8. Accepted limits
 
- The free-side lock stays immediate.
- Other cores still get interrupted by permission changes.
- Fillers get no overflow protection.
- One guard direction per object.
- Only one guarded object fits at the end of each page, so the number of guarded objects is capped by the number of host pages.
- Aliasing and guard pages add mappings, so the budget must cover both tiers.
- The goal is to curb the limits, not to remove them all.
---
 
## 9. Allocator findings
 
**What an allocator is.** The library that answers `malloc`/`free` (and `new`/`delete`). It takes big chunks from the OS, cuts them into pieces, hands them out, and reuses freed pieces itself, with no OS call. It returns memory to the OS only occasionally, usually when whole pages are free. The default on most Linux systems is glibc `malloc`, which has no GWP-ASan.
 
**GWP-ASan is a library, not a program.** It has no `main`. **[S]** It works by stubs inside a supporting allocator's `malloc`, `free`, and `realloc`. On each allocation the allocator asks GWP-ASan whether to guard it. The allocator also routes frees to it and its handler catches the fault. LLVM recommends static linking for performance.
 
### Scudo (LLVM)
 
- Hardened allocator in LLVM's compiler-rt. Default on Fuchsia and on Android since version 11. **[S]**
- Splits work by size. **Primary:** fast, for smaller allocations, carved from reserved regions into equal-size blocks. **Secondary:** slower, for larger allocations through OS memory mapping, **surrounded by guard pages**. **[S]**
- Its own safety work (present in both GWP-ASan-on and GWP-ASan-off runs, so it cancels out of the comparison): **[S]**
  - Every chunk has an 8-byte checksummed header, checked when accessed. No option to disable it was listed.
  - Randomized block handout in the Primary. This is an anti-exploit measure, not bug detection.
  - Quarantine (holds freed chunks back) is off by default in current docs and costly. Type and size mismatch checks and zero/pattern fill are optional.
- GWP-ASan ships in Scudo. Options go through `SCUDO_OPTIONS=GWP_ASAN_<Option>=<value>`, for example `GWP_ASAN_SampleRate=100`. `Enabled` is a listed GWP-ASan option.
- To verify at setup:
  - The GWP-ASan docs recommend `-fsanitize=scudo` as the quickest way to try GWP-ASan. The Scudo docs say that flag links the "old" Scudo and a move to standalone is planned. Check what it actually links.
  - The Scudo docs show building standalone Scudo as a shared library and running an existing binary with `LD_PRELOAD`. That would fit our no-recompile constraint. The docs describe GWP-ASan as support Scudo must be compiled with, so confirm the preload build includes it.
  - Confirm that `Enabled` can be set through `SCUDO_OPTIONS`, and that a `SampleRate` of 1 guards every allocation (the source special-cases it).
- Some of the Scudo docs I found were older snapshots. Re-read the current page at setup.
### TCMalloc (Google)
 
- Enabled by calling `tcmalloc::MallocExtension::ActivateGuardedSampling()` in code. **[S]**
- Sampling is **byte-based**: it guards roughly once per `GuardedSamplingRate` bytes allocated (default 100M). LLVM's is allocation-based (1 in N). Our reading is that bigger allocations are picked more often under byte-based sampling. **[S + O]**
- Only finds bugs in allocations of **8 KB or less**. **[S]**
- RAM overhead up to 512 KB on x86_64. **[S]**
- Overflows inside alignment padding do not touch a guard page, so diagnostics are limited. **[S]** This is Google's own note on limit 8.
### Allocator-dependent policy **[O]**
 
| Underlying allocator | Large allocations | Implication for the agent |
|---|---|---|
| Scudo | Guard pages from the Secondary allocator (overflow and underflow; use-after-free on large allocations not confirmed) | Spend the pool on small allocations |
| TCMalloc | Not covered by GWP-ASan (over 8 KB) | Large allocations stay unguarded |
| glibc | No guard pages | Agent should include large allocations |
 
The agent should prioritize small buffers, which are often overlooked. They are also the expensive ones to guard, so the pool budget and the two-tier design matter most there.
 
**Decision.** Real targets use whatever allocator they use, so we cannot pick for them. We try both, one at a time: LLVM (Scudo with GWP-ASan) first, then TCMalloc. The tool should work on both, and we add other allocators later if needed. GWP-ASan is a framework that plugs into other allocators, so the tool should not depend on Scudo long term. A target that moves to Scudo to use our tool also takes on Scudo's overhead.
 
---
 
## 10. Baseline plan (agreed order)
 
1. Set up LLVM (Scudo with GWP-ASan) first, then TCMalloc, one at a time.
2. Run GWP-ASan as-is on a sample program or repo.
3. On Scudo, compare runs with GWP-ASan off and on, to isolate GWP-ASan's cost from Scudo's own.
Behavior checks (does it catch the bug?) are fine in a sandbox. Timing numbers are only trustworthy on real hardware.
 
---
 
## 11. Open questions
 
- **Main question:** Is continuous checking affordable on allocation-heavy, latency-sensitive paths? The original proposal does not answer it.
- **Filler design:** Does it recover enough RAM in practice? It is a design goal, not a proven result.
- **Deferred ("how" phase):**
  - How paths get marked (the proposal says "explicitly marked" but not how).
  - How the allocator applies the policy quickly inside `malloc`.
  - How it controls placement inside pages.
  - How the pool is configured.
  - How the agent measures allocation rate and lifetime per path.
- **Not yet checked:** The Scudo size cutoff between Primary and Secondary, and whether Secondary guard pages catch use-after-free.
---
 
## Glossary
 
- **Allocator:** Library answering `malloc`/`free`.
- **Page:** Fixed block of memory, usually 4 KB. The OS manages memory one page at a time.
- **Guard page:** A page set to "no access". It uses address space, not real RAM. Touching it faults.
- **Mapping:** A kernel record for a range of pages. Linux caps their number.
- **Virtual alias:** A second virtual address for the same real page.
- **TLB:** Translation Lookaside Buffer. A small CPU cache of recent virtual-to-real page translations.
- **TLB shootdown:** Interrupting other cores to drop stale TLB entries after a permission change.
- **Quarantine:** Holding freed memory back so use-after-free can be caught.
- **Redzone (ASan):** A few bytes of poisoned padding around an allocation, checked in software.
- **MTE:** ARM Memory Tagging Extension. The CPU compares a pointer tag with a memory tag on every access.
---
 
## Sources
 
- GWP-ASan paper: https://arxiv.org/abs/2311.09394
- LLVM GWP-ASan docs: https://llvm.org/docs/GwpAsan.html
- LLVM GWP-ASan source: https://github.com/llvm/llvm-project/tree/main/compiler-rt/lib/gwp_asan
- TCMalloc GWP-ASan: https://google.github.io/tcmalloc/gwp-asan.html
- Scudo docs (source file, main branch): https://llvm.googlesource.com/llvm-project/llvm/+/refs/heads/main/docs/ScudoHardenedAllocator.rst
 