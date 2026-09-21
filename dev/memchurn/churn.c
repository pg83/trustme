#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
typedef uint64_t u64; typedef uint32_t u32;
typedef struct { u64 ptr; u64 size; u32 site; u32 pad; } Rec;
typedef struct { u64 key; u64 seq; u32 site; u32 size; } Slot;
#define HBITS 26
#define HN (1u << HBITS)
static Slot* table;
static inline u32 hidx(u64 k) { return (u32)((k * 0x9E3779B97F4A7C15ull) >> (64 - HBITS)); }
static void hput(u64 k, u64 seq, u32 site, u32 size) { u32 i = hidx(k); while (table[i].key) i = (i + 1) & (HN - 1); table[i].key = k; table[i].seq = seq; table[i].site = site; table[i].size = size; }
static int hpop(u64 k, u64* seq, u32* site, u32* size) {
    u32 i = hidx(k);
    while (table[i].key != k) { if (!table[i].key) return 0; i = (i + 1) & (HN - 1); }
    *seq = table[i].seq; *site = table[i].site; *size = table[i].size;
    u32 j = i;
    for (;;) { j = (j + 1) & (HN - 1); if (!table[j].key) break; u32 h = hidx(table[j].key); if ((i < j) ? (h <= i || h > j) : (h <= i && h > j)) { table[i] = table[j]; i = j; } }
    table[i].key = 0; return 1;
}
#define NSITES (1u << 20)
typedef struct { u64 count, bytes, freed, sumDist, b100, b10k, b1m, bLong, liveBytes; } Agg;
static Agg agg[NSITES];
int main(int argc, char** argv) {
    int fd = open(argv[1], O_RDONLY); struct stat st; fstat(fd, &st);
    const Rec* rec = mmap(0, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    u64 nrec = st.st_size / sizeof(Rec);
    table = calloc(HN, sizeof(Slot));
    u64 seq = 0;
    for (u64 i = 0; i < nrec; i++) {
        u64 op = rec[i].ptr >> 60, ptr = rec[i].ptr & ((1ull << 60) - 1);
        if (op == 1) { seq++; Agg* a = &agg[rec[i].site]; a->count++; a->bytes += rec[i].size; hput(ptr, seq, rec[i].site, (u32)(rec[i].size > 0xFFFFFFFFu ? 0xFFFFFFFFu : rec[i].size)); }
        else { u64 s; u32 site, size; if (!hpop(ptr, &s, &site, &size)) continue; Agg* a = &agg[site]; u64 d = seq - s; a->freed++; a->sumDist += d; if (d <= 100) a->b100++; else if (d <= 10000) a->b10k++; else if (d <= 1000000) a->b1m++; else a->bLong++; }
    }
    for (u32 i = 0; i < HN; i++) if (table[i].key) agg[table[i].site].liveBytes += table[i].size;
    FILE* out = fopen(argv[2], "w");
    fprintf(out, "site count bytes freed sumDist le100 le10k le1m gt1m liveBytes\n");
    for (u32 s = 0; s < NSITES; s++) if (agg[s].count) fprintf(out, "%u %llu %llu %llu %llu %llu %llu %llu %llu %llu\n", s, (unsigned long long)agg[s].count, (unsigned long long)agg[s].bytes, (unsigned long long)agg[s].freed, (unsigned long long)agg[s].sumDist, (unsigned long long)agg[s].b100, (unsigned long long)agg[s].b10k, (unsigned long long)agg[s].b1m, (unsigned long long)agg[s].bLong, (unsigned long long)agg[s].liveBytes);
    fclose(out);
    fprintf(stderr, "allocs=%llu\n", (unsigned long long)seq);
    return 0;
}
