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
static inline uint32_t hidx(uint64_t k) { return (uint32_t)((k * 0x9E3779B97F4A7C15ull) >> (64 - HBITS)); }
static void hput(uint64_t k, uint32_t blk, uint32_t size) { uint32_t i = hidx(k); while (table[i].key) i = (i + 1) & (HN - 1); table[i].key = k; table[i].blk = blk; table[i].size = size; }
static int hpop(uint64_t k, uint32_t* blk, uint32_t* size) {
    uint32_t i = hidx(k);
    while (table[i].key != k) { if (!table[i].key) return 0; i = (i + 1) & (HN - 1); }
    *blk = table[i].blk; *size = table[i].size;
    uint32_t j = i;
    for (;;) { j = (j + 1) & (HN - 1); if (!table[j].key) break; uint32_t h = hidx(table[j].key); if ((i < j) ? (h <= i || h > j) : (h <= i && h > j)) { table[i] = table[j]; i = j; } }
    table[i].key = 0; return 1;
}
static inline uint64_t r4k(uint64_t n) { return (n + 4095) & ~4095ull; }
static int classIndex(uint64_t n) { if (n <= 256) return (int)((n + 15) / 16) - 1; int e = 63 - __builtin_clzll(n - 1); uint64_t base = 1ull << e, step = base / 4; return 16 + (e - 8) * 4 + (int)((n - base + step - 1) / step) - 1; }
static uint64_t classSize(int i) { if (i < 16) return 16 * (uint64_t)(i + 1); int e = 8 + (i - 16) / 4, k = (i - 16) % 4 + 1; return (1ull << e) + k * ((1ull << e) / 4); }
#define MAXPG (1u << 22)
enum { NCLS = 64 };
typedef struct { uint32_t live, freeSlots, gen, cls; } Page;
static Page pg[MAXPG];
static uint32_t pool[MAXPG], nPool, nPages;
typedef struct { uint32_t page, gen; } Ref;
static Ref* stack[NCLS]; static uint32_t sn[NCLS], scap[NCLS];
static void push(int c, uint32_t p) { if (sn[c] == scap[c]) { scap[c] = scap[c] ? scap[c] * 2 : 1024; stack[c] = realloc(stack[c], scap[c] * sizeof(Ref)); } stack[c][sn[c]].page = p; stack[c][sn[c]].gen = pg[p].gen; sn[c]++; }
int main(int argc, char** argv) {
    int fd = open(argv[1], O_RDONLY); struct stat st; fstat(fd, &st);
    const Rec* rec = mmap(0, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    uint64_t nrec = st.st_size / sizeof(Rec);
    table = calloc(HN, sizeof(Slot));
    uint64_t P = strtoull(argv[2], 0, 0), T = strtoull(argv[3], 0, 0), MAXCLS = P / 4;
    uint64_t large = 0, peakLarge = 0, medium = 0, peakMedium = 0, live = 0, nonEmpty = 0, peak = 0, liveAtPeak = 0, mediumAtPeak = 0, releases = 0, reuses = 0;
    uint32_t cur[NCLS]; uint64_t off[NCLS]; int have[NCLS]; memset(have, 0, sizeof have);
    for (uint64_t i = 0; i < nrec; i++) {
        uint64_t op = rec[i].ptr >> 60, ptr = rec[i].ptr & ((1ull << 60) - 1);
        if (op == 1) {
            uint64_t n = rec[i].size ? rec[i].size : 1;
            if (n > T) { large += r4k(n); if (large > peakLarge) peakLarge = large; hput(ptr, 0xFFFFFFFFu, 0); continue; }
            int c = classIndex(n); uint64_t cs = classSize(c);
            if (cs > MAXCLS) { uint64_t m = (cs + P - 1) / P * P; medium += m; if (medium > peakMedium) peakMedium = medium; hput(ptr, 0xFFFFFFFEu, (uint32_t)m); continue; }
            live += cs;
            uint32_t p = 0xFFFFFFFFu;
            while (sn[c]) { Ref r = stack[c][sn[c] - 1]; if (pg[r.page].gen == r.gen && pg[r.page].freeSlots) { p = r.page; break; } sn[c]--; }
            if (p != 0xFFFFFFFFu) { pg[p].freeSlots--; pg[p].live++; reuses++; if (!pg[p].freeSlots) sn[c]--; }
            else {
                if (!have[c] || off[c] + cs > P) {
                    p = nPool ? pool[--nPool] : nPages++;
                    if (p >= MAXPG) { fprintf(stderr, "too many pages\n"); return 1; }
                    pg[p].gen++; pg[p].live = 0; pg[p].freeSlots = 0; pg[p].cls = (uint32_t)c; cur[c] = p; off[c] = 0; have[c] = 1; nonEmpty++;
                }
                p = cur[c]; off[c] += cs; pg[p].live++;
            }
            hput(ptr, p, (uint32_t)cs);
            if (nonEmpty > peak) { peak = nonEmpty; liveAtPeak = live; mediumAtPeak = medium; }
        } else {
            uint32_t p, size;
            if (!hpop(ptr, &p, &size)) continue;
            if (p == 0xFFFFFFFFu) continue;
            if (p == 0xFFFFFFFEu) { medium -= size; continue; }
            live -= size;
            int c = (int)pg[p].cls;
            pg[p].live--;
            if (pg[p].live == 0 && p != cur[c]) { nonEmpty--; releases++; pool[nPool++] = p; pg[p].gen++; }
            else { if (++pg[p].freeSlots == 1) push(c, p); }
        }
    }
    printf("horiz P=%llu T=%llu: peak pages=%llu (%.0f MB) + medium at peak %.0f MB (peak medium %.0f MB); live in pages at peak=%.0f MB util=%.0f%%; page releases=%llu slot reuses=%llu; peakLarge=%.0f MB\n",
        (unsigned long long)P, (unsigned long long)T, (unsigned long long)peak, peak * P / 1048576.0, mediumAtPeak / 1048576.0, peakMedium / 1048576.0, liveAtPeak / 1048576.0, 100.0 * liveAtPeak / (peak * P), (unsigned long long)releases, (unsigned long long)reuses, peakLarge / 1048576.0);
    return 0;
}
