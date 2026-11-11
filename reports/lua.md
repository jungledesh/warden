# Lua suite, GWP-ASan off and on

Same binary, `build/lua`. Same script, `tests/all.lua` (Lua 5.5.1 official suite), started with `-e '_U=true'`. Scudo was loaded both times via `LD_PRELOAD=$SCUDO_SO`. These two runs are what separate GWP-ASan's cost from Scudo's own.

Run in the container on 2026-11-11 04:42–04:43 UTC. Timing here is sandbox noise. Wall time printed `0s` both times because the suite's clock only resolves to 1 second.

| Run | Scudo | GWP-ASan | Result | Suite time | Max memory |
|---|---|---|---|---|---|
| Off | loaded | `Enabled=false` | final OK, exit 0 | 0.51s | 15.8M |
| On | loaded | default, 1 in 5000 | final OK, exit 0 | 0.46s | 15.8M |

Off command:

```bash
env LD_PRELOAD=$SCUDO_SO SCUDO_OPTIONS=GWP_ASAN_Enabled=false ../build/lua -e '_U=true' all.lua
```

On command:

```bash
env LD_PRELOAD=$SCUDO_SO ../build/lua -e '_U=true' all.lua
```

Neither run printed a GWP-ASan report. `all.lua` checks that Lua behaves correctly, so its allocations are not bad accesses. The suite time was 0.51s off and 0.46s on. That gap is sandbox noise, not a measured cost. Max memory was 15.8M both times.
