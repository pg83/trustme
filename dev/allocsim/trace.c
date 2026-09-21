#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
extern void* __libc_malloc(size_t);
extern void __libc_free(void*);
extern void* __libc_realloc(void*, size_t);
extern void* __libc_calloc(size_t, size_t);
extern void* __libc_memalign(size_t, size_t);
enum { OP_ALLOC = 1, OP_FREE = 2 };
typedef struct { uint64_t ptr; uint64_t size; } Rec;
#define NREC (1u << 16)
static Rec buf[NREC];
static unsigned n;
static int fd = -1;
static void flush(void) {
    if (fd < 0) { const char* path = getenv("TRACE_OUT"); fd = open(path ? path : "trace.bin", O_WRONLY | O_CREAT | O_TRUNC, 0644); }
    const char* p = (const char*)buf; size_t left = n * sizeof(Rec);
    while (left) { ssize_t w = write(fd, p, left); if (w <= 0) break; p += w; left -= (size_t)w; }
    n = 0;
}
static void rec(unsigned op, const void* ptr, size_t size) {
    if (n == NREC) flush();
    buf[n].ptr = (uint64_t)(uintptr_t)ptr | ((uint64_t)op << 60);
    buf[n].size = size;
    n++;
}
void* malloc(size_t s) { void* p = __libc_malloc(s); if (p) rec(OP_ALLOC, p, s); return p; }
void free(void* p) { if (p) rec(OP_FREE, p, 0); __libc_free(p); }
void* calloc(size_t a, size_t b) { void* p = __libc_calloc(a, b); if (p) rec(OP_ALLOC, p, a * b); return p; }
void* realloc(void* p, size_t s) { if (p) rec(OP_FREE, p, 0); void* q = __libc_realloc(p, s); if (q) rec(OP_ALLOC, q, s); return q; }
void* memalign(size_t a, size_t s) { void* p = __libc_memalign(a, s); if (p) rec(OP_ALLOC, p, s); return p; }
void* aligned_alloc(size_t a, size_t s) { return memalign(a, s); }
int posix_memalign(void** out, size_t a, size_t s) { *out = memalign(a, s); return *out ? 0 : 12; }
__attribute__((destructor)) static void done(void) { flush(); if (fd >= 0) close(fd); }
