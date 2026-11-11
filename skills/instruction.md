# Memory bug report

When you are given a GWP-ASan or Scudo stack trace, write one report per run. Use the command if it is in the output. If the command is missing, use the process name from the trace (`/tmp/toy`, `lua`, and so on).

Write in short, plain sentences. A person and another agent should both be able to see what ran, what broke, and which line did it. Do not guess a source line from a bare offset.

## What to read off the trace

- The command is the line that was run, such as `LD_PRELOAD=$SCUDO_SO /tmp/toy uaf 100000`.
- A GWP-ASan report starts with `*** GWP-ASan detected a memory error ***`. The next line names the bug, the fault address, how far that address is from the allocation, the allocation size, and the thread.
- Frames look like `#3 /tmp/toy(+0xcd0) [0x...]`. The part before `(` is the binary. `+0xcd0` is a byte offset, not a source line. A name in parentheses, such as `malloc`, is the function.
- `linux-vdso.so.1(__kernel_rt_sigreturn)` is the kernel returning from the fault signal. It is not the bug.
- `/lib/.../libc.so.6` frames such as `__libc_start_main` and `_start` are process startup. They are not the bug.
- Frames in `libscudo.so` are Scudo: the handler that prints the report, or `malloc` / the free path. The handler frame is not the bug. `malloc` is the allocation site only when the group says "was allocated".
- `was deallocated by` is the stack of the `free`. `was allocated by` is the stack of the `malloc`. The first group, "by thread N here", is the bad access.
- `Segmentation fault` plus `exit: 139` means the process was killed by signal 11 (SIGSEGV). 139 = 128 + 11. GWP-ASan prints the report, then the fault ends the process. The exit is the detection, not a second bug.
- `Scudo ERROR:` is Scudo's own check, not a guard-page fault. `invalid chunk state` means the pointer was already freed. `misaligned pointer` means the address is not the start of an allocation.
- `Aborted` plus `exit: 134` means Scudo called abort. 134 = 128 + 6 (SIGABRT).
- `exit: 0` and a normal last line, such as a checksum, means no bug was reported.

## Where the source line comes from

Use a source line only when you can point at it.

- The command names the program and, for the toy, the mode (`clean`, `uaf`, `overflow`, `double-free`, `invalid-free`).
- Open that program's source. For `targets/toy/toy.c` the bug lines are: `uaf` line 60 `p[0] = 'X'` after `free`; `overflow` line 49 `p[SIZE + offset] = 'X'`; `double-free` line 69 the second `free(p)`; `invalid-free` line 77 `free(p + 8)`.
- If the trace has `file:line`, use that. If it has only `+offset`, say "offset in `<binary>`, no source line in the trace" and give the source line only from the match above.
- "0 bytes into a 100-byte allocation" means the access is at the first byte of that block.
- "132 bytes to the right of a 100-byte allocation" means 132 bytes from the start of the block, which is 32 bytes past the end.

## Report shape

Number the runs `## 1.`, `## 2.`, and so on. The heading names the mode and the bug. Everything for that run stays under that number.

If the trace has offsets and no `file:line`, put that fact on its own line under the title: `The traces have offsets, not file:line. Source lines come from the mode.`

Keep every kind of frame, including the crash handler, the kernel signal return, and libc startup. Do not give each frame its own bullet. Write each stack as one line, top of the stack first, joined by `→`. Group the Scudo-internal offsets into one step, and keep the offsets in parentheses. Mark the step that is the bug with a short label in parentheses.

```
# Memory bug report

The traces have offsets, not file:line. Source lines come from the mode.

## 1. <mode> — <bug, or "no bug">

- Command: <command, or "not in the output">
- Sampling: <rate>. If `SCUDO_OPTIONS` sets `GWP_ASAN_SampleRate`, use that number. `1` means every allocation. If it is not set, the rate is the default, 1 in 5000. If GWP-ASan is off (`GWP_ASAN_Enabled=false` or `0`), say "off".
- Exit: <code> — <SIGSEGV, SIGABRT, or normal exit, in a few words>
- Where: <file:line and the code, or "no source line in the trace">
- What happened: <one sentence>
- Bad write: <one line, top of stack first, or "none">
- Free: <one line, or "none">
- Malloc: <one line, or "none">
```

A `Scudo ERROR` run has no stack lines. Put the error text in `What happened`, and set Bad write, Free, and Malloc to `none`.
