# Building Warden

We make sampling worse by increasing denominator to make chance of an allocation being picked up next to zero. 
On a long running server, an alloc can still be picked up since the dice is still rolling

## Building the side file

Build once, unstripped.
Read each function name, its offset, and size with `nm` or `readelf`.
Scan each function with a disassembler. If a call or a jump names `malloc`, `calloc`, `realloc`, `free`, `new`, or `delete`, record that it allocates. (A call through a pointer stays off).
Read the build id with `readelf`, and write the map with it next to the function list.
Warden does not grow its own reader for any of that.
Strip that same binary.
Deploy the stripped binary.
Keep the map with Warden.

When the stripped binary starts, Warden adds this run's load address to each offset.
Warden reads the build id from the running binary and compares. Match: use the map. No match: ignore the map.
