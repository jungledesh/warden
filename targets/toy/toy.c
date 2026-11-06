/*
 * toy.c - tiny target with planted memory-safety bugs, for GWP-ASan baseline runs.
 *
 * Build:  gcc -O0 -g -fno-omit-frame-pointer -o toy toy.c
 * Usage:  ./toy <mode> [iterations] [overflow_offset]
 *
 * Modes:
 *   clean         no bug. Used for overhead measurement and to check for false positives.
 *   overflow      writes 1 byte past the end of a 100-byte buffer, plus overflow_offset.
 *   uaf           writes to a buffer after it has been freed (use-after-free).
 *   double-free   frees the same pointer twice.
 *   invalid-free  frees a pointer that is not the start of an allocation.
 *
 * Each mode repeats its operation `iterations` times (default 100000).
 * GWP-ASan only guards a small random sample of allocations, so the loop gives
 * the sampler enough chances to pick a buggy allocation.
 *
 * overflow_offset (default 32): bytes beyond buf[SIZE] to write at.
 *   Offset 0 writes exactly one byte past the end. With default alignment
 *   rounding, that byte can land in padding before the guard page and be missed.
 *   (Limit #8 in the design notes.) The offset lets us test that.
 */
 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 
 #define SIZE 100
 
 /* volatile keeps the compiler from optimizing the buggy accesses away */
 static void fill(volatile char *p, size_t n, char c) {
     for (size_t i = 0; i < n; i++) p[i] = c;
 }
 
 static void run_clean(long iters) {
     unsigned long sum = 0;
     for (long i = 0; i < iters; i++) {
         volatile char *p = malloc(SIZE);
         fill(p, SIZE, (char)i);
         sum += (unsigned char)p[SIZE - 1];
         free((void *)p);
     }
     printf("clean: %ld iterations, checksum %lu\n", iters, sum);
 }
 
 static void run_overflow(long iters, long offset) {
     for (long i = 0; i < iters; i++) {
         volatile char *p = malloc(SIZE);
         fill(p, SIZE, 'a');
         p[SIZE + offset] = 'X'; /* BUG: write past the end */
         free((void *)p);
     }
     printf("overflow: no crash after %ld iterations (offset %ld)\n", iters, offset);
 }
 
 static void run_uaf(long iters) {
     for (long i = 0; i < iters; i++) {
         volatile char *p = malloc(SIZE);
         fill(p, SIZE, 'a');
         free((void *)p);
         p[0] = 'X'; /* BUG: write after free */
     }
     printf("uaf: no crash after %ld iterations\n", iters);
 }
 
 static void run_double_free(long iters) {
     for (long i = 0; i < iters; i++) {
         void *volatile p = malloc(SIZE);
         free(p);
         free(p); /* BUG: second free of the same pointer */
     }
     printf("double-free: no crash after %ld iterations\n", iters);
 }
 
 static void run_invalid_free(long iters) {
     for (long i = 0; i < iters; i++) {
         char *volatile p = malloc(SIZE);
         free(p + 8); /* BUG: not the start of the allocation */
     }
     printf("invalid-free: no crash after %ld iterations\n", iters);
 }
 
 int main(int argc, char **argv) {
     if (argc < 2) {
         fprintf(stderr,
                 "usage: %s <clean|overflow|uaf|double-free|invalid-free> "
                 "[iterations] [overflow_offset]\n",
                 argv[0]);
         return 2;
     }
     const char *mode = argv[1];
     long iters = argc > 2 ? atol(argv[2]) : 100000;
     long offset = argc > 3 ? atol(argv[3]) : 32;
 
     if (iters <= 0) {
         fprintf(stderr, "iterations must be positive\n");
         return 2;
     }
 
     if (!strcmp(mode, "clean")) run_clean(iters);
     else if (!strcmp(mode, "overflow")) run_overflow(iters, offset);
     else if (!strcmp(mode, "uaf")) run_uaf(iters);
     else if (!strcmp(mode, "double-free")) run_double_free(iters);
     else if (!strcmp(mode, "invalid-free")) run_invalid_free(iters);
     else {
         fprintf(stderr, "unknown mode: %s\n", mode);
         return 2;
     }
     return 0;
 }