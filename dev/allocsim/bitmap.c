#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
typedef struct { uint64_t ptr; uint64_t size; } Rec;
typedef uint64_t u64; typedef uint32_t u32; typedef uint8_t u8;
/* ---- pointer table: real ptr -> (simulated addr, size, tag) ---- */
typedef struct { u64 key; u64 addr; u64 size; } Slot;
#define HBITS 26
#define HN (1u << HBITS)
static Slot* table;
static inline u32 hidx(u64 k) { return (u32)((k * 0x9E3779B97F4A7C15ull) >> (64 - HBITS)); }
static void hput(u64 k, u64 addr, u64 size) { u32 i = hidx(k); while (table[i].key) i = (i + 1) & (HN - 1); table[i].key = k; table[i].addr = addr; table[i].size = size; }
static int hpop(u64 k, u64* addr, u64* size) {
    u32 i = hidx(k);
    while (table[i].key != k) { if (!table[i].key) return 0; i = (i + 1) & (HN - 1); }
    *addr = table[i].addr; *size = table[i].size;
    u32 j = i;
    for (;;) { j = (j + 1) & (HN - 1); if (!table[j].key) break; u32 h = hidx(table[j].key); if ((i < j) ? (h <= i || h > j) : (h <= i && h > j)) { table[i] = table[j]; i = j; } }
    table[i].key = 0; return 1;
}
/* ---- page counters keyed by page id ---- */
typedef struct { u64 key; u32 count; } PSlot;
typedef struct { PSlot* s; u32 bits; u64 live, peak; } PMap;
static void pmInit(PMap* m, u32 bits) { m->bits = bits; m->s = calloc((size_t)1 << bits, sizeof(PSlot)); m->live = m->peak = 0; }
static inline u32 pidx(PMap* m, u64 k) { return (u32)((k * 0x9E3779B97F4A7C15ull) >> (64 - m->bits)); }
static void pmAdd(PMap* m, u64 page, int delta) {
    u32 mask = (1u << m->bits) - 1, i = pidx(m, page); u64 key = page + 1;
    while (m->s[i].key && m->s[i].key != key) i = (i + 1) & mask;
    if (!m->s[i].key) { m->s[i].key = key; m->s[i].count = 0; }
    if (delta > 0) { if (m->s[i].count == 0) { m->live++; if (m->live > m->peak) m->peak = m->live; } m->s[i].count++; }
    else { if (--m->s[i].count == 0) m->live--; }
}
static PMap p4k, p2m;
static void touch(u64 addr, u64 size, int delta) {
    for (u64 p = addr >> 12; p <= (addr + size - 1) >> 12; p++) pmAdd(&p4k, p, delta);
    for (u64 p = addr >> 21; p <= (addr + size - 1) >> 21; p++) pmAdd(&p2m, p, delta);
}
/* ---- allocation window locality ---- */
#define W 256
static u64 win[W]; static u32 wn; static double sum4k, sum2m, sum64; static u64 windows;
static int cmpu64(const void* a, const void* b) { u64 x = *(const u64*)a, y = *(const u64*)b; return x < y ? -1 : x > y; }
static u32 distinct(u64* v, u32 n, int shift) { for (u32 i = 0; i < n; i++) v[i] >>= shift; qsort(v, n, 8, cmpu64); u32 d = n ? 1 : 0; for (u32 i = 1; i < n; i++) if (v[i] != v[i - 1]) d++; return d; }
static void windowAdd(u64 addr) {
    win[wn++] = addr;
    if (wn == W) { u64 t[W]; memcpy(t, win, sizeof t); sum64 += distinct(t, W, 6); memcpy(t, win, sizeof t); sum4k += distinct(t, W, 12); memcpy(t, win, sizeof t); sum2m += distinct(t, W, 21); windows++; wn = 0; }
}
static u64 liveBytes, peakLiveBytes, liveBytesAtPeak4k;
static void onAlloc(u64 addr, u64 size) { touch(addr, size, 1); windowAdd(addr); liveBytes += size; if (liveBytes > peakLiveBytes) peakLiveBytes = liveBytes; }
static void onFree(u64 addr, u64 size) { touch(addr, size, -1); liveBytes -= size; }
/* ---- size classes ---- */
static int classIndex(u64 n) { if (n <= 256) return (int)((n + 15) / 16) - 1; int e = 63 - __builtin_clzll(n - 1); u64 base = 1ull << e, step = base / 4; return 16 + (e - 8) * 4 + (int)((n - base + step - 1) / step) - 1; }
static u64 classSize(int i) { if (i < 16) return 16 * (u64)(i + 1); int e = 8 + (i - 16) / 4, k = (i - 16) % 4 + 1; return (1ull << e) + k * ((1ull << e) / 4); }
enum { NCLS = 64 };
/* ---- mmap model for huge objects ---- */
static u64 mmapBase = 0x7000ull << 32, mmapCursor;
static u64 mmapAlloc(u64 n) { u64 a = mmapBase + mmapCursor; mmapCursor += (n + 4095) & ~4095ull; mmapCursor += 4096; return a; }
/* ---- vertical (current malloc.cpp) ---- */
static u64 vCursor[NCLS], vMapped[NCLS]; static u64* vFree[NCLS]; static u32 vNFree[NCLS], vCap[NCLS];
static u64 vertAlloc(u64 n, u64 T) {
    if (n > T) return mmapAlloc(n);
    int c = classIndex(n); u64 cs = classSize(c);
    if (vNFree[c]) return vFree[c][--vNFree[c]];
    u64 base = (0x2000ull << 32) + ((u64)c << 34);
    if (vCursor[c] + cs > vMapped[c]) vMapped[c] += 2097152;
    u64 a = base + vCursor[c]; vCursor[c] += cs; return a;
}
static void vertFree(u64 a, u64 n, u64 T) { if (n > T) return; int c = classIndex(n); if (vNFree[c] == vCap[c]) { vCap[c] = vCap[c] ? vCap[c] * 2 : 1024; vFree[c] = realloc(vFree[c], vCap[c] * 8); } vFree[c][vNFree[c]++] = a; }
/* ---- pure bump ---- */
static u64 bCursor;
static u64 bumpAlloc(u64 n, u64 T) { if (n > T) return mmapAlloc(n); u64 a = (0x2000ull << 32) + bCursor + 16; bCursor += ((n + 15) & ~15ull) + 16; return a; }
/* ---- candidate: segments of 2 MB, pages of P per class, per-page LIFO free lists ---- */
#define SEG (2097152ull)
#define MAXSEG 65536
#define MAXPAGE (1u << 22)
typedef struct { u32 cls; u32 live; u32 nfree, fcap; u32* freeSlots; u32 off; u32 gen; u32 inStack; u32 runLen; } Page;
static Page* pages; static u32 pagesPerSeg; static u64 P;
typedef struct { u32 freePages; u8* freeMap; u32 lastTouched; } Seg;
static Seg segs[MAXSEG]; static u32 nSegs, segCur = 0xFFFFFFFFu;
static u32* segStack; static u32 segSN;
static int segPolicy, pagePolicy;
static u32 clsCur[NCLS]; static int clsHave[NCLS]; static u32* clsStack[NCLS]; static u32 clsSN[NCLS], clsCap[NCLS];
static u64 segReleases, pageReleases, nonEmptySegs, peakNonEmptySegs;
static void clsPush(int c, u32 pg) { if (pages[pg].inStack) return; if (clsSN[c] == clsCap[c]) { clsCap[c] = clsCap[c] ? clsCap[c] * 2 : 1024; clsStack[c] = realloc(clsStack[c], clsCap[c] * 4); } clsStack[c][clsSN[c]++] = pg; pages[pg].inStack = 1; }
static u32 newSeg(void) { u32 s = nSegs++; if (s >= MAXSEG) { fprintf(stderr, "too many segs\n"); exit(1); } segs[s].freePages = pagesPerSeg; segs[s].freeMap = calloc(pagesPerSeg, 1); nonEmptySegs++; if (nonEmptySegs > peakNonEmptySegs) peakNonEmptySegs = nonEmptySegs; return s; }
static int segFindRun(u32 s, u32 k, u32* start) { u32 run = 0; for (u32 i = 0; i < pagesPerSeg; i++) { if (!segs[s].freeMap[i]) { if (++run == k) { *start = i + 1 - k; return 1; } } else run = 0; } return 0; }
static u32 takePages(u32 k) {
    u32 s = 0xFFFFFFFFu, start = 0;
    if (segCur != 0xFFFFFFFFu && segs[segCur].freePages >= k && segFindRun(segCur, k, &start)) s = segCur;
    if (s == 0xFFFFFFFFu) {
        if (segPolicy == 0) { while (segSN) { u32 c = segStack[segSN - 1]; if (segs[c].freePages >= k && segFindRun(c, k, &start)) { s = c; break; } segSN--; } }
        else { for (u32 c = 0; c < nSegs; c++) if (segs[c].freePages >= k && segFindRun(c, k, &start)) { s = c; break; } }
    }
    if (s == 0xFFFFFFFFu) { s = newSeg(); start = 0; }
    if (segs[s].freePages == pagesPerSeg && s != nSegs - 1) { nonEmptySegs++; if (nonEmptySegs > peakNonEmptySegs) peakNonEmptySegs = nonEmptySegs; }
    for (u32 i = 0; i < k; i++) segs[s].freeMap[start + i] = 1;
    segs[s].freePages -= k; segCur = s;
    u32 pg = s * pagesPerSeg + start;
    pages[pg].runLen = k; pages[pg].gen++; pages[pg].live = 0; pages[pg].nfree = 0; pages[pg].off = 0; pages[pg].inStack = 0;
    return pg;
}
static void releasePages(u32 pg) {
    u32 s = pg / pagesPerSeg, start = pg % pagesPerSeg, k = pages[pg].runLen;
    for (u32 i = 0; i < k; i++) segs[s].freeMap[start + i] = 0;
    segs[s].freePages += k; pageReleases++;
    if (segs[s].freePages == pagesPerSeg) { nonEmptySegs--; segReleases++; if (s == segCur) segCur = 0xFFFFFFFFu; }
    else if (segPolicy == 0) { if (segSN < MAXSEG * 4) segStack[segSN++] = s; }
}
static inline u64 pageAddr(u32 pg) { return (0x2000ull << 32) + (u64)pg * P; }

/* ---- hualloc-style: per-page free bitmaps, lowest free slot first, lowest page first ---- */
static int pow2Classes;
static int bClassIndex(u64 n) { if (!pow2Classes) return classIndex(n); if (n <= 16) return 0; return 64 - __builtin_clzll(n - 1) - 4; }
static u64 bClassSize(int c) { if (!pow2Classes) return classSize(c); return 16ull << c; }
typedef struct { u64* bits; u32 nchunks, capacity, freeCount, curChunk, inList, cls; } BPage;
static BPage* bp;
static u32 bCur[NCLS]; static int bHave[NCLS];
static u32* bPartial[NCLS]; static u32 bPN[NCLS], bPCap[NCLS];
static u64 bRefills, bPageSwitches, bPageReleases;
static void bAdd(int c, u32 pg) { if (bp[pg].inList) return; if (bPN[c] == bPCap[c]) { bPCap[c] = bPCap[c] ? bPCap[c] * 2 : 1024; bPartial[c] = realloc(bPartial[c], bPCap[c] * 4); } bPartial[c][bPN[c]++] = pg; bp[pg].inList = 1; }
static u32 bLowestPartial(int c) {
    u32 best = 0xFFFFFFFFu, j = 0;
    for (u32 i = 0; i < bPN[c]; i++) { u32 pg = bPartial[c][i]; if (bp[pg].cls == (u32)c && bp[pg].freeCount) { bPartial[c][j++] = pg; if (pg < best) best = pg; } else bp[pg].inList = 0; }
    bPN[c] = j;
    return best;
}
static void bOpenPage(int c, u32 pg, u64 cs) {
    BPage* b = &bp[pg]; b->cls = (u32)c; b->capacity = (u32)(P / cs); b->nchunks = (b->capacity + 63) / 64;
    if (!b->bits) b->bits = calloc(P / 16 / 64 + 1, 8);
    for (u32 k = 0; k < b->nchunks; k++) b->bits[k] = ~0ull;
    if (b->capacity % 64) b->bits[b->nchunks - 1] = (1ull << (b->capacity % 64)) - 1;
    b->freeCount = b->capacity; b->curChunk = 0; b->inList = 0;
}
static u64 bitmapAlloc(u64 n, u64 T) {
    if (n > T) return mmapAlloc(n);
    int c = bClassIndex(n); u64 cs = bClassSize(c);
    if (cs > P / 4) { u32 k = (u32)((cs + P - 1) / P); u32 pg = takePages(k); pages[pg].cls = 0xFFFF; return pageAddr(pg); }
    for (;;) {
        if (bHave[c]) {
            BPage* b = &bp[bCur[c]];
            u64 w = b->bits[b->curChunk];
            if (!w) { bRefills++; for (u32 k = 0; k < b->nchunks; k++) if (b->bits[k]) { b->curChunk = k; w = b->bits[k]; break; } }
            if (w) { unsigned i = (unsigned)__builtin_ctzll(w); b->bits[b->curChunk] &= ~(1ull << i); b->freeCount--; return pageAddr(bCur[c]) + (u64)(b->curChunk * 64 + i) * cs; }
        }
        bPageSwitches++;
        u32 pg = bLowestPartial(c);
        if (pg != 0xFFFFFFFFu) { bp[pg].inList = 0; bp[pg].curChunk = 0; bCur[c] = pg; bHave[c] = 1; continue; }
        pg = takePages(1); pages[pg].cls = 0xFFFE; bOpenPage(c, pg, cs); bCur[c] = pg; bHave[c] = 1;
    }
}
static void bitmapFree(u64 a, u64 n, u64 T) {
    if (n > T) return;
    int c = bClassIndex(n); u64 cs = bClassSize(c);
    u32 pg = (u32)((a - (0x2000ull << 32)) / P);
    if (cs > P / 4) { releasePages(pg); return; }
    BPage* b = &bp[pg]; u32 slot = (u32)((a - pageAddr(pg)) / cs);
    b->bits[slot / 64] |= 1ull << (slot % 64); b->freeCount++;
    if (bHave[c] && pg == bCur[c]) return;
    if (b->freeCount == b->capacity) { b->cls = 0xFFFFFFFFu; b->inList = 0; pages[pg].cls = 0xFFFD; releasePages(pg); bPageReleases++; return; }
    bAdd(c, pg);
}
int main(int argc, char** argv) {
    if (argc < 5) { fprintf(stderr, "bitmap trace P T pow2\n"); return 2; }
    int fd = open(argv[1], O_RDONLY); struct stat st; fstat(fd, &st);
    const Rec* rec = mmap(0, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    u64 nrec = st.st_size / sizeof(Rec);
    table = calloc(HN, sizeof(Slot)); pmInit(&p4k, 24); pmInit(&p2m, 20);
    P = strtoull(argv[2], 0, 0); u64 T = strtoull(argv[3], 0, 0); pow2Classes = atoi(argv[4]);
    pagesPerSeg = (u32)(SEG / P); pages = calloc(MAXPAGE, sizeof(Page)); segStack = calloc(MAXSEG * 4, 4); segPolicy = 0;
    bp = calloc(MAXPAGE, sizeof(BPage));
    u64 Tm = 262144;
    for (u64 i = 0; i < nrec; i++) {
        u64 op = rec[i].ptr >> 60, ptr = rec[i].ptr & ((1ull << 60) - 1);
        if (op == 1) { u64 n = rec[i].size ? rec[i].size : 1; u64 a = bitmapAlloc(n, T); hput(ptr, a, n); if (n <= Tm) onAlloc(a, n); }
        else { u64 a, n; if (!hpop(ptr, &a, &n)) continue; if (n <= Tm) onFree(a, n); bitmapFree(a, n, T); }
    }
    printf("bitmap P=%llu pow2=%d: peak4K=%6llu MB peak2M=%6llu MB  win256: lines=%5.1f pages4K=%5.1f regions2M=%4.1f  refills=%llu pageSwitches=%llu pageRel=%llu segs=%u peakSegs=%llu\n",
        (unsigned long long)P, pow2Classes, (unsigned long long)(p4k.peak * 4096 / 1048576), (unsigned long long)(p2m.peak * 2), sum64 / windows, sum4k / windows, sum2m / windows,
        (unsigned long long)bRefills, (unsigned long long)bPageSwitches, (unsigned long long)bPageReleases, nSegs, (unsigned long long)peakNonEmptySegs);
    return 0;
}
