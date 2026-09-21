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
typedef struct { uint64_t key; uint32_t blk; uint32_t size; } Slot;
#define HBITS 26
#define HN (1u << HBITS)
static Slot* table;
static uint64_t hcount;
static inline uint32_t hidx(uint64_t k) { return (uint32_t)((k * 0x9E3779B97F4A7C15ull) >> (64 - HBITS)); }
static void hput(uint64_t k, uint32_t blk, uint32_t size) {
    uint32_t i = hidx(k);
    while (table[i].key) i = (i + 1) & (HN - 1);
    table[i].key = k; table[i].blk = blk; table[i].size = size; hcount++;
}
static int hpop(uint64_t k, uint32_t* blk, uint32_t* size) {
    uint32_t i = hidx(k);
    while (table[i].key != k) { if (!table[i].key) return 0; i = (i + 1) & (HN - 1); }
    *blk = table[i].blk; *size = table[i].size; hcount--;
    uint32_t j = i;
    for (;;) {
        j = (j + 1) & (HN - 1);
        if (!table[j].key) break;
        uint32_t h = hidx(table[j].key);
        if ((i < j) ? (h <= i || h > j) : (h <= i && h > j)) { table[i] = table[j]; i = j; }
    }
    table[i].key = 0;
    return 1;
}
static inline uint64_t r16(uint64_t n) { return (n + 15) & ~15ull; }
static inline uint64_t r4k(uint64_t n) { return (n + 4095) & ~4095ull; }
static int classIndex(uint64_t n) {
    if (n <= 256) return (int)((n + 15) / 16) - 1;
    int e = 63 - __builtin_clzll(n - 1);
    uint64_t base = 1ull << e, step = base / 4;
    return 16 + (e - 8) * 4 + (int)((n - base + step - 1) / step) - 1;
}
static uint64_t classSize(int i) {
    if (i < 16) return 16 * (uint64_t)(i + 1);
    int e = 8 + (i - 16) / 4, k = (i - 16) % 4 + 1;
    return (1ull << e) + k * ((1ull << e) / 4);
}
#define MAXBLK (1u << 24)
static uint32_t* blkLive;
static uint64_t* blkBytes;
static uint32_t* freeBlocks; static uint32_t nFree, nBlocks;
static uint32_t newBlock(uint64_t* nonEmpty) {
    uint32_t b = nFree ? freeBlocks[--nFree] : nBlocks++;
    if (b >= MAXBLK) { fprintf(stderr, "too many blocks\n"); exit(1); }
    (*nonEmpty)++;
    return b;
}
int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "sim trace {bump B H T | horiz P T | vert T | ideal T}\n"); return 2; }
    int fd = open(argv[1], O_RDONLY); struct stat st; fstat(fd, &st);
    const Rec* rec = mmap(0, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    uint64_t nrec = st.st_size / sizeof(Rec);
    table = calloc(HN, sizeof(Slot));
    blkLive = calloc(MAXBLK, 4); blkBytes = calloc(MAXBLK, 8); freeBlocks = calloc(MAXBLK, 4);
    const char* mode = argv[2];
    uint64_t B = 0, H = 0, T = 0, P = 0;
    if (!strcmp(mode, "bump")) { B = strtoull(argv[3], 0, 0); H = strtoull(argv[4], 0, 0); T = strtoull(argv[5], 0, 0); }
    else if (!strcmp(mode, "horiz")) { P = strtoull(argv[3], 0, 0); T = strtoull(argv[4], 0, 0); }
    else T = strtoull(argv[3], 0, 0);
    uint64_t large = 0, peakLarge = 0, liveSmall = 0, peakLiveSmall = 0, nonEmpty = 0, peakNonEmpty = 0, releases = 0, peakLiveAtPeak = 0, nLarge = 0;
    uint64_t curBlk = 0, curOff = 0; int haveCur = 0; uint64_t hist[10];
    enum { NCLS = 64 };
    uint64_t clsCur[NCLS], clsOff[NCLS], clsCount[NCLS], clsPeak[NCLS]; int clsHave[NCLS];
    uint32_t* clsFree[NCLS]; uint32_t clsNFree[NCLS];
    memset(clsCur, 0, sizeof clsCur); memset(clsOff, 0, sizeof clsOff); memset(clsCount, 0, sizeof clsCount); memset(clsPeak, 0, sizeof clsPeak); memset(clsHave, 0, sizeof clsHave);
    for (int c = 0; c < NCLS; c++) { clsFree[c] = calloc(MAXBLK / 16, 4); clsNFree[c] = 0; }
    uint64_t medium = 0, peakMedium = 0;
    for (uint64_t i = 0; i < nrec; i++) {
        uint64_t op = rec[i].ptr >> 60, ptr = rec[i].ptr & ((1ull << 60) - 1);
        if (op == 1) {
            uint64_t n = rec[i].size ? rec[i].size : 1;
            if (n > T) { large += r4k(n); nLarge++; if (large > peakLarge) peakLarge = large; hput(ptr, 0xFFFFFFFFu, 0); continue; }
            uint64_t need = r16(n) + H;
            liveSmall += need; if (liveSmall > peakLiveSmall) peakLiveSmall = liveSmall;
            if (B) {
                if (!haveCur || curOff + need > B) { curBlk = newBlock(&nonEmpty); curOff = 0; haveCur = 1; }
                curOff += need; blkLive[curBlk]++; blkBytes[curBlk] += need;
                hput(ptr, (uint32_t)curBlk, (uint32_t)need);
            } else if (P) {
                int c = classIndex(n); uint64_t cs = classSize(c);
                if (cs > P / 4) { medium += (cs + P - 1) / P * P; if (medium > peakMedium) peakMedium = medium; hput(ptr, 0xFFFFFFFEu, (uint32_t)cs); continue; }
                uint32_t blk;
                if (clsNFree[c]) { blk = clsFree[c][clsNFree[c] - 1]; if (--blkLive[blk] == 0) {} blkLive[blk]++; if (blkLive[blk] == 1) nonEmpty++; }
                else { if (!clsHave[c] || clsOff[c] + cs > P) { clsCur[c] = newBlock(&nonEmpty); clsOff[c] = 0; clsHave[c] = 1; blkLive[clsCur[c]] = 0; nonEmpty--; }
                       clsOff[c] += cs; blk = (uint32_t)clsCur[c]; blkLive[blk]++; if (blkLive[blk] == 1) nonEmpty++; }
                hput(ptr, blk, (uint32_t)cs);
            } else {
                int c = classIndex(n); clsCount[c]++; if (clsCount[c] > clsPeak[c]) clsPeak[c] = clsCount[c];
                hput(ptr, (uint32_t)c, (uint32_t)n);
            }
            if (nonEmpty > peakNonEmpty) { peakNonEmpty = nonEmpty; peakLiveAtPeak = liveSmall; if (B && (i & 0) == 0) { memset(hist, 0, sizeof hist); for (uint32_t b = 0; b < nBlocks; b++) if (blkLive[b]) { int k = (int)(blkBytes[b] * 10 / B); if (k > 9) k = 9; hist[k]++; } } }
        } else {
            uint32_t blk, size;
            if (!hpop(ptr, &blk, &size)) { fprintf(stderr, "free of unknown %llx at %llu\n", (unsigned long long)ptr, (unsigned long long)i); continue; }
            if (blk == 0xFFFFFFFFu) { continue; }
            if (blk == 0xFFFFFFFEu) { medium -= (size + P - 1) / P * P; continue; }
            liveSmall -= size;
            if (B) {
                blkBytes[blk] -= size;
                if (--blkLive[blk] == 0 && blk != curBlk) { nonEmpty--; releases++; freeBlocks[nFree++] = blk; }
            } else if (P) {
                int c = classIndex(size);
                if (--blkLive[blk] == 0) { nonEmpty--; releases++; freeBlocks[nFree++] = blk; }
                else { clsFree[c][clsNFree[c]++] = blk; }
            } else {
                clsCount[blk]--;
            }
        }
    }
    uint64_t vertSum = 0; for (int c = 0; c < NCLS; c++) vertSum += clsPeak[c] * classSize(c);
    if (B) printf("bump B=%llu H=%llu T=%llu: peak blocks=%llu (%.0f MB) liveSmall at that peak=%.0f MB util=%.0f%% releases=%llu peakLarge=%.0f MB nLarge=%llu peakLiveSmall=%.0f MB\n",
        (unsigned long long)B, (unsigned long long)H, (unsigned long long)T, (unsigned long long)peakNonEmpty, peakNonEmpty * B / 1048576.0, peakLiveAtPeak / 1048576.0,
        100.0 * peakLiveAtPeak / (peakNonEmpty * B), (unsigned long long)releases, peakLarge / 1048576.0, (unsigned long long)nLarge, peakLiveSmall / 1048576.0);
    else if (P) printf("horiz P=%llu T=%llu: peak pages=%llu (%.0f MB) + medium %.0f MB, live at peak=%.0f MB releases=%llu peakLarge=%.0f MB peakLiveSmall=%.0f MB\n",
        (unsigned long long)P, (unsigned long long)T, (unsigned long long)peakNonEmpty, peakNonEmpty * P / 1048576.0, peakMedium / 1048576.0, peakLiveAtPeak / 1048576.0, (unsigned long long)releases, peakLarge / 1048576.0, peakLiveSmall / 1048576.0);
    else if (!strcmp(mode, "vert")) printf("vert T=%llu: sum of per-class peaks=%.0f MB peakLarge=%.0f MB peakLiveSmall=%.0f MB\n", (unsigned long long)T, vertSum / 1048576.0, peakLarge / 1048576.0, peakLiveSmall / 1048576.0);
    else printf("ideal T=%llu: peakLiveSmall=%.0f MB peakLarge=%.0f MB nLarge=%llu\n", (unsigned long long)T, peakLiveSmall / 1048576.0, peakLarge / 1048576.0, (unsigned long long)nLarge);
    if (B) { printf("blocks at peak by live fraction:"); for (int k = 0; k < 10; k++) printf(" %d0%%:%llu", k, (unsigned long long)hist[k]); printf("\n"); }
    return 0;
}
