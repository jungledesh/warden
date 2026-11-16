# Building Warden

We change the allocator that contains GWP-ASan, and turn sampling off. Only a marked function is guarded.

## Building the side file

Build once, unstripped.
Read each function name, its offset, and size with `nm` or `readelf`.
Scan each function with a disassembler. If a call or a jump names `malloc`, `calloc`, `realloc`, `free`, `new`, or `delete`, record that it allocates. (A call through a pointer stays off).
Read the build id with `readelf`, and write the map with it next to the function list.
Warden does not grow its own reader for any of that.

### Slot writing

The map is finished before this step.
CI hashes that map.
CI writes the hash into a metadata slot in the binary. The build id already lives in a slot of that kind.
Strip that same binary. The strip leaves the slot in place.
Deploy the stripped binary.
Keep the map with Warden.

When the stripped binary starts, Warden adds this run's load address to each offset.  
Warden reads the build id from the running binary and compares. Match: use the map. No match: ignore the map.

## Allocator Work

### Checking the hash

At start, the allocator reads the map and hashes it.
It compares that hash to the stamp in the binary.
Match: use the map. No match: ignore the map.

Warden profiles every memory operation that runs. It does not sample.

A memory operation comes in. The allocator takes the address of that call and looks for it in the side file.

A function row is the function start and its size. The call address sits inside that range. The row keeps the address of each memory operation already profiled.

If the call address is inside a function row and is not yet listed there, profile this operation and add its address to the row. A later operation in the same function has a different address, so it is profiled too. The same address on a later run is already listed, so it is left alone.

If the call address is in no row, add a row for that address and profile it. The same address on a later run finds that row.

An operation that has not run yet stays unprofiled until it runs. The first time it runs, it is profiled and its address is added.
