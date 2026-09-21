#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
extern void* __libc_malloc(size_t);
extern void __libc_free(void*);
extern void* __libc_realloc(void*, size_t);
extern void* __libc_calloc(size_t, size_t);
extern void* __libc_memalign(size_t, size_t);
#define NB 64
static unsigned long long cnt[NB], bytes[NB];
static unsigned long long nMalloc, nFree, nRealloc, nCalloc, nAlign, live, peak, totalBytes, reallocGrow, reallocSame;
static int bucket(size_t n) {
    if (n <= 256) return (int)((n + 15) / 16);            /* 0..16: 16-byte steps */
    int b = 17; size_t lim = 512;
    while (n > lim && b < NB - 1) { b++; lim <<= 1; }     /* 512,1K,2K,... */
    return b;
}
static void account(size_t n) { int b = bucket(n); cnt[b]++; bytes[b] += n; totalBytes += n; }
static void addLive(void* p) { if (p) { live += malloc_usable_size(p); if (live > peak) peak = live; } }
static void subLive(void* p) { if (p) live -= malloc_usable_size(p); }
void* malloc(size_t n) { nMalloc++; account(n); void* p = __libc_malloc(n); addLive(p); return p; }
void free(void* p) { if (p) { nFree++; subLive(p); } __libc_free(p); }
void* calloc(size_t a, size_t b) { nCalloc++; account(a * b); void* p = __libc_calloc(a, b); addLive(p); return p; }
void* realloc(void* p, size_t n) { nRealloc++; size_t old = p ? malloc_usable_size(p) : 0; if (p && n <= old) reallocSame++; else reallocGrow++; account(n); subLive(p); void* q = __libc_realloc(p, n); addLive(q); return q; }
void* memalign(size_t a, size_t n) { nAlign++; account(n); void* p = __libc_memalign(a, n); addLive(p); return p; }
void* aligned_alloc(size_t a, size_t n) { return memalign(a, n); }
int posix_memalign(void** out, size_t a, size_t n) { *out = memalign(a, n); return *out ? 0 : 12; }
__attribute__((destructor)) static void report(void) {
    fprintf(stderr, "HIST malloc=%llu free=%llu realloc=%llu(grow %llu,same %llu) calloc=%llu memalign=%llu totalBytes=%llu peakLive=%llu\n", nMalloc, nFree, nRealloc, reallocGrow, reallocSame, nCalloc, nAlign, totalBytes, peak);
    for (int b = 0; b < NB; b++) if (cnt[b]) {
        if (b <= 16) fprintf(stderr, "HIST <=%4d  %10llu calls %12llu bytes\n", b * 16, cnt[b], bytes[b]);
        else fprintf(stderr, "HIST <=%4dK %10llu calls %12llu bytes\n", (int)((512ull << (b - 17)) / 1024) ? (int)((512ull << (b - 17)) / 1024) : 1, cnt[b], bytes[b]);
    }
}
