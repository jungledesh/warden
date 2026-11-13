# Valkey under Scudo and GWP-ASan

Valkey 9.1.3, built in the image with `make MALLOC=libc CC=clang-18` and installed on the path. `INFO memory` reported `mem_allocator:libc`. Scudo was loaded only into `valkey-server` with `LD_PRELOAD=$SCUDO_SO`. The process map showed `libclang_rt.scudo_standalone-aarch64.so`.

The client was two `valkey-benchmark` processes at the same time, one `SET` and one `GET`. Scudo was not loaded into the clients. Disk saving was off. Runs were in the container on 2026-11-11 UTC. No run printed a GWP-ASan report, and the server stayed up.

A lower `SampleRate` guards more often. `1` means every allocation is a candidate. The pool still caps how many objects are guarded at once (16 unless raised).

| Run | Memory cap | GWP-ASan | Clients | Length | Commands/sec | Evicted keys | Memory in use | Report |
|---|---|---|---|---|---|---|---|---|
| 1 | 64MB | on, 1 in 5000 | 201 | 60s | 444k–460k | 0 | 55.35M | none |
| 2 | 32MB | on, 1 in 5000 | 201 | 60s | 371k–451k | 5,917,465 | 32.00M | none |
| 3 | 32MB | on, 1 in 100 | 201 | 60s | 444k–465k | 5,884,099 | 32.00M | none |
| 4 | 32MB | on, every allocation, pool 16 | 201 | 5 min | 399k–450k | 28,724,515 | 32.00M | none |
| 5 | 8MB | on, every allocation, pool 256 | 400 | 10 min | not recorded | not recorded | not recorded | none |

Run 1 never filled the cap, so nothing was evicted. Runs 2 through 4 sat on the cap and freed old keys to store new ones. Run 5 was a harsher load (2048-byte values, a million-key range, 200 connections per client). It finished with no report. The per-minute counters from that run were not kept.

Client shape for runs 1 through 4:

```bash
valkey-benchmark -t set -c 100 -r 100000 -d 512 --duration 60
valkey-benchmark -t get -c 100 -r 100000 -d 512 --duration 60
```

Run 4 used `--duration 300`. Run 5 used `-c 200 -r 1000000 -d 2048 --duration 600` and started the server with:

```bash
SCUDO_OPTIONS="GWP_ASAN_Enabled=1:GWP_ASAN_SampleRate=1:GWP_ASAN_MaxSimultaneousAllocations=256"
```

plus `--maxmemory 8mb --maxmemory-policy allkeys-lru`.

This workload is SET, GET, and eviction. A clean run means those paths did not hit a guarded bad access.

Later, run the other parts of Valkey the same way. This load never calls streams, Pub/Sub, cluster, or the server's own scripting (`EVAL`). That scripting is a Lua interpreter built into Valkey, separate from the Lua interpreter in `reports/lua.md`.
