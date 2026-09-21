#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
extern void* __libc_malloc(size_t);
extern void __libc_free(void*);
extern void* __libc_realloc(void*, size_t);
extern void* __libc_calloc(size_t, size_t);
extern void* __libc_memalign(size_t, size_t);
#define DEPTH 10
enum { OP_ALLOC = 1, OP_FREE = 2 };
typedef struct { uint64_t ptr; uint64_t size; uint32_t site; uint32_t pad; } Rec;
typedef struct { uint64_t hash; uint64_t frames[DEPTH]; uint64_t count; } Site;
#define NREC (1u << 16)
#define SBITS 20
static Rec buf[NREC];
static unsigned n;
static int fd = -1;
static Site* sites;
static uint32_t nsites;
static uint64_t stackTop;
static void flush(void) {
    if (fd < 0) { const char* path = getenv("TRACE_OUT"); fd = open(path ? path : "trace2.bin", O_WRONLY | O_CREAT | O_TRUNC, 0644); }
    const char* p = (const char*)buf; size_t left = n * sizeof(Rec);
    while (left) { ssize_t w = write(fd, p, left); if (w <= 0) break; p += w; left -= (size_t)w; }
    n = 0;
}
static uint32_t siteOf(void) {
    uint64_t frames[DEPTH]; int d = 0;
    uint64_t* fp = (uint64_t*)__builtin_frame_address(0);
    if (!stackTop) { stackTop = (uint64_t)fp + (64u << 20); }
    fp = (uint64_t*)fp[0];
    while (d < DEPTH && fp && (uint64_t)fp > (uint64_t)&frames && (uint64_t)fp < stackTop && ((uint64_t)fp & 7) == 0) {
        frames[d++] = fp[1];
        uint64_t* next = (uint64_t*)fp[0];
        if (next <= fp) break;
        fp = next;
    }
    for (int i = d; i < DEPTH; i++) frames[i] = 0;
    uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < DEPTH; i++) { h ^= frames[i]; h *= 1099511628211ull; }
    if (!sites) sites = __libc_calloc((size_t)1 << SBITS, sizeof(Site));
    uint32_t mask = (1u << SBITS) - 1, i = (uint32_t)(h >> (64 - SBITS));
    for (;;) {
        Site* s = &sites[i];
        if (s->hash == 0 && s->count == 0) { s->hash = h; memcpy(s->frames, frames, sizeof frames); s->count = 1; nsites++; return i; }
        if (s->hash == h && !memcmp(s->frames, frames, sizeof frames)) { s->count++; return i; }
        i = (i + 1) & mask;
    }
}
static void rec(unsigned op, const void* ptr, size_t size, uint32_t site) {
    if (n == NREC) flush();
    buf[n].ptr = (uint64_t)(uintptr_t)ptr | ((uint64_t)op << 60); buf[n].size = size; buf[n].site = site; buf[n].pad = 0; n++;
}
void* malloc(size_t s) { void* p = __libc_malloc(s); if (p) rec(OP_ALLOC, p, s, siteOf()); return p; }
void free(void* p) { if (p) rec(OP_FREE, p, 0, 0); __libc_free(p); }
void* calloc(size_t a, size_t b) { void* p = __libc_calloc(a, b); if (p) rec(OP_ALLOC, p, a * b, siteOf()); return p; }
void* realloc(void* p, size_t s) { if (p) rec(OP_FREE, p, 0, 0); void* q = __libc_realloc(p, s); if (q) rec(OP_ALLOC, q, s, siteOf()); return q; }
void* memalign(size_t a, size_t s) { void* p = __libc_memalign(a, s); if (p) rec(OP_ALLOC, p, s, siteOf()); return p; }
void* aligned_alloc(size_t a, size_t s) { return memalign(a, s); }
int posix_memalign(void** out, size_t a, size_t s) { *out = memalign(a, s); return *out ? 0 : 12; }
__attribute__((destructor)) static void done(void) {
    flush(); if (fd >= 0) close(fd);
    const char* path = getenv("TRACE_SITES"); FILE* f = fopen(path ? path : "sites.txt", "w");
    FILE* m = fopen("/proc/self/maps", "r"); char line[512];
    while (m && fgets(line, sizeof line, m)) if (strstr(line, "r-xp") || strstr(line, "r--p")) fputs(line, f);
    if (m) fclose(m);
    fprintf(f, "SITES %u\n", nsites);
    for (uint32_t i = 0; i < (1u << SBITS); i++) if (sites[i].count) { fprintf(f, "%u %llu", i, (unsigned long long)sites[i].count); for (int k = 0; k < DEPTH; k++) fprintf(f, " %llx", (unsigned long long)sites[i].frames[k]); fputc('\n', f); }
    fclose(f);
}
