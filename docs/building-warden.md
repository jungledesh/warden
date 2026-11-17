# Building Warden

We change the allocator that contains GWP-ASan, and turn sampling off. Only a marked function is guarded.

## Building the side file

Before the program runs, `scripts/sidefile` reads the unstripped binary and writes the map. The map is the build id, then the offset and size of each function that allocates. The script does not run the binary.

### Slot writing

Before the program runs, the binary is linked with an empty slot. `scripts/stamp` writes the hash of the map into that slot. It then reads the binary again and checks the build id and the rows did not change.

## Allocator Work

### Checking the hash

When the program starts, Warden reads the map named by `WARDEN_MAP`. It compares the hash of the map to the slot, and the two build ids. Both match: it keeps the rows. A miss writes one line to standard error and keeps no rows. The program keeps running. Guarding stays off.

## Open work

Time the in-memory table search on a running program. An allocation that is checked searches the rows already in memory. Valkey's table is about 10 comparisons. A 10,000-function table is about 14. Each comparison is two addresses. With the table in cache, that is tens of nanoseconds, and it stays under a microsecond if the table is not in cache. This has not been timed.
