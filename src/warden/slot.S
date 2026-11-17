/*
 * Empty slot linked into the program before the map is made.
 * The hash of the map is written over the 32 zero bytes. The size stays 32.
 *
 *   clang -Wl,--build-id -Wl,-T,src/warden/slot.ld src/warden/slot.S main.c -o app
 */
.section .note.warden, "a", @note
.p2align 2
.long 7
.long 32
.long 1
.ascii "WARDEN\0\0"
.p2align 2
.zero 32
