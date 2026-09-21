#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <dlfcn.h>
enum { OP_ALLOC = 1, OP_FREE = 2 };
typedef struct { uint64_t ptr; uint64_t size; } Rec;
#define NREC (1u << 16)
static Rec buf[NREC];
static unsigned n;
static int fd = -1;
static void* (*nextMalloc)(size_t);
static void (*nextFree)(void*);
static void* (*nextCalloc)(size_t, size_t);
static void* (*nextRealloc)(void*, size_t);
static void* (*nextMemalign)(size_t, size_t);
static char boot[1 << 16]; static size_t bootUsed; static int resolving;
static void resolve(void) {
    resolving = 1;
    nextMalloc = dlsym(RTLD_NEXT, "malloc"); nextFree = dlsym(RTLD_NEXT, "free"); nextCalloc = dlsym(RTLD_NEXT, "calloc");
    nextRealloc = dlsym(RTLD_NEXT, "realloc"); nextMemalign = dlsym(RTLD_NEXT, "memalign");
    resolving = 0;
}
static void flush(void) {
    if (fd < 0) { const char* path = getenv("TRACE_OUT"); fd = open(path ? path : "trace.bin", O_WRONLY | O_CREAT | O_TRUNC, 0644); }
    const char* p = (const char*)buf; size_t left = n * sizeof(Rec);
    while (left) { ssize_t w = write(fd, p, left); if (w <= 0) break; p += w; left -= (size_t)w; }
    n = 0;
}
static void rec(unsigned op, const void* ptr, size_t size) {
    if (n == NREC) flush();
    buf[n].ptr = (uint64_t)(uintptr_t)ptr | ((uint64_t)op << 60); buf[n].size = size; n++;
}
static int isBoot(const void* p) { return (const char*)p >= boot && (const char*)p < boot + sizeof boot; }
void* malloc(size_t s) {
    if (resolving) { size_t a = (bootUsed + 15) & ~(size_t)15; bootUsed = a + s; return boot + a; }
    if (!nextMalloc) resolve();
    void* p = nextMalloc(s); if (p) rec(OP_ALLOC, p, s); return p;
}
void free(void* p) { if (!p || isBoot(p)) return; rec(OP_FREE, p, 0); nextFree(p); }
void* calloc(size_t a, size_t b) {
    if (resolving) { void* p = malloc(a * b); memset(p, 0, a * b); return p; }
    if (!nextCalloc) resolve();
    void* p = nextCalloc(a, b); if (p) rec(OP_ALLOC, p, a * b); return p;
}
void* realloc(void* p, size_t s) { if (!nextRealloc) resolve(); if (p) rec(OP_FREE, p, 0); void* q = nextRealloc(p, s); if (q) rec(OP_ALLOC, q, s); return q; }
void* memalign(size_t a, size_t s) { if (!nextMemalign) resolve(); void* p = nextMemalign(a, s); if (p) rec(OP_ALLOC, p, s); return p; }
void* aligned_alloc(size_t a, size_t s) { return memalign(a, s); }
int posix_memalign(void** out, size_t a, size_t s) { *out = memalign(a, s); return *out ? 0 : 12; }
__attribute__((destructor)) static void done(void) { flush(); if (fd >= 0) close(fd); }
void* _Znwm(size_t s) { return malloc(s); }
void* _ZnwmRKSt9nothrow_t(size_t s, const void* t) { (void)t; return malloc(s); }
void _ZdlPv(void* p) { free(p); }
void _ZdlPvm(void* p, size_t s) { (void)s; free(p); }
