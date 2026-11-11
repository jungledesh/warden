# Memory bug report

The loop was `for m in clean uaf overflow double-free invalid-free; do LD_PRELOAD=$SCUDO_SO /tmp/toy $m 100000; done`.

`SCUDO_OPTIONS` was not set, so GWP-ASan used its default sampling rate: 1 allocation in 5000.

The traces have offsets, not file:line. Source lines come from the mode.

## 1. clean — no bug

- Command: `LD_PRELOAD=$SCUDO_SO /tmp/toy clean 100000`
- Sampling: default, 1 in 5000
- Exit: 0 — finished normally
- Where: `targets/toy/toy.c` `run_clean`
- What happened: 100000 iterations, checksum 12742320.
- Bad write: none
- Free: none
- Malloc: none

## 2. uaf — use after free

- Command: `LD_PRELOAD=$SCUDO_SO /tmp/toy uaf 100000`
- Sampling: default, 1 in 5000
- Exit: 139 — SIGSEGV, the detection
- Where: `targets/toy/toy.c:60` `p[0] = 'X'` after `free` on line 59
- What happened: wrote the first byte of a freed 100-byte block at `0xffff9a81f000`. Thread 20.
- Bad write: Scudo handler (`+0x365c`, `+0x3980`) → signal return → `/tmp/toy+0xcd0` (this write) → `/tmp/toy+0xa8c` → libc startup → `/tmp/toy+0x830`
- Free: Scudo free (`+0x34f0`, `+0x212c`, `+0x30b8`, `+0x71b0`) → `/tmp/toy+0xcc8` (this free) → `/tmp/toy+0xa8c` → libc startup → `/tmp/toy+0x830`
- Malloc: Scudo (`+0x34f0`, `+0x212c`, `+0x2ef8`, `+0x6ba8`) → `malloc` (this block) → `/tmp/toy+0xcac` → `/tmp/toy+0xa8c` → libc startup → `/tmp/toy+0x830`

## 3. overflow — buffer overflow

- Command: `LD_PRELOAD=$SCUDO_SO /tmp/toy overflow 100000`
- Sampling: default, 1 in 5000
- Exit: 139 — SIGSEGV, the detection
- Where: `targets/toy/toy.c:49` `p[SIZE + offset] = 'X'`. Default offset is 32, so the write is `p[132]`.
- What happened: wrote 132 bytes from the start of a 100-byte block, 32 bytes past the end. Thread 21. No free stack.
- Bad write: Scudo handler (`+0x365c`, `+0x3980`) → signal return → `/tmp/toy+0xc34` (this write) → `/tmp/toy+0xa68` → libc startup → `/tmp/toy+0x830`
- Free: none
- Malloc: Scudo (`+0x34f0`, `+0x212c`, `+0x2ef8`, `+0x6ba8`) → `malloc` (this block) → `/tmp/toy+0xc0c` → `/tmp/toy+0xa68` → libc startup → `/tmp/toy+0x830`

## 4. double-free — double free

- Command: `LD_PRELOAD=$SCUDO_SO /tmp/toy double-free 100000`
- Sampling: default, 1 in 5000
- Exit: 134 — SIGABRT. `Aborted` means Scudo called abort.
- Where: `targets/toy/toy.c:69` the second `free(p)`
- What happened: `Scudo ERROR: invalid chunk state when deallocating address 0x200ffdd85008b90`. The pointer was already freed. Scudo's own check, not a guard page.
- Bad write: none
- Free: none
- Malloc: none

## 5. invalid-free — invalid free

- Command: `LD_PRELOAD=$SCUDO_SO /tmp/toy invalid-free 100000`
- Sampling: default, 1 in 5000
- Exit: 134 — SIGABRT. `Aborted` means Scudo called abort.
- Where: `targets/toy/toy.c:77` `free(p + 8)`
- What happened: `Scudo ERROR: misaligned pointer when deallocating address 0xffeeade06718`. The address is not the start of an allocation. Scudo's own check, not a guard page.
- Bad write: none
- Free: none
- Malloc: none
