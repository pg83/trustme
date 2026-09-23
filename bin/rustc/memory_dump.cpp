#include "memory_dump.h"

#include "output.h"
#include "output_file.h"
#include "compile_error.h"

#include <std/ios/sys.h>
#include <std/sys/crt.h>
#include <std/str/view.h>
#include <std/sys/types.h>
#include <std/ios/out_zc.h>
#include <std/lib/buffer.h>
#include <std/lib/vector.h>
#include <std/str/builder.h>
#include <std/ios/fs_utils.h>
#include <std/mem/obj_pool.h>

#include <zstd.h>
#include <stdlib.h>

using namespace stl;

namespace {
    /* One readable row of /proc/self/maps. The two views borrow the map text,
       which outlives every range parsed out of it. */
    struct MapRange {
        u64 vStart;
        u64 vEnd;
        u64 fileOfs;
        StringView flags;
        StringView name;
        u32 firstChunk;
    };

    StringView takeField(StringView& rest) {
        const u8* p = rest.begin();
        const u8* e = rest.end();

        while (p != e && *p == ' ') {
            ++p;
        }

        const u8* field = p;

        while (p != e && *p != ' ') {
            ++p;
        }

        rest = StringView(p, e);

        return StringView(field, p);
    }

    StringView dropSpace(StringView rest) {
        const u8* p = rest.begin();
        const u8* e = rest.end();

        while (p != e && *p == ' ') {
            ++p;
        }

        return StringView(p, e);
    }

    bool parseMapLine(StringView line, MapRange& range) {
        StringView first;
        StringView last;

        if (!takeField(line).split('-', first, last)) {
            return false;
        }

        range.vStart = first.stoh();
        range.vEnd = last.stoh();
        range.flags = takeField(line);
        range.fileOfs = takeField(line).stoh();

        takeField(line);
        takeField(line);

        range.name = dropSpace(line);
        range.firstChunk = 0;

        return !range.flags.empty();
    }
}

void memoryDump(unsigned& sequence, const char* phase) {
    if (getenv("TRUSTME_DUMPMEM")) {
        auto idx = sequence++;
        StringBuilder filename;

        filename << StringView("trustme-") << idx << StringView("-") << phase << StringView(".dmp");
#if defined(__linux__) && defined(__x86_64__)
    #define DEBUG_MEM_DUMP 1

        size_t chunkSize = 1 << 20;
        Buffer maps;
        Vector<MapRange> ranges;
        size_t chunkCount = 0;
        {
            Buffer path(StringView("/proc/self/maps"));

            /* The map is a snapshot: reserve the whole text up front so that
               reading it does not itself move the brk the map describes. */
            maps.grow(1 << 20);
            readFileContent(path, maps);
        }
        {
            u64 lastVaddr = 0;
            StringView rest(maps);
            StringView line;

            while (rest.split('\n', line, rest)) {
                MapRange e;

                if (!parseMapLine(line, e)) {
                    continue;
                }

                if (e.name == StringView("[vvar]")) {
                    continue;
                }

                if (e.flags[0] != 'r') {
                    continue;
                }

                if (lastVaddr / chunkSize != e.vStart / chunkSize) {
                    if (lastVaddr % chunkSize != 0) {
                        chunkCount += 1;
                    }
                }
                e.firstChunk = chunkCount;
                if (e.vStart / chunkSize == (e.vEnd - 1) / chunkSize) {
                    if (e.vEnd % chunkSize == 0) {
                        chunkCount += 1;
                    }
                } else {
                    auto headSize = (chunkSize - e.vStart % chunkSize) % chunkSize;
                    if (headSize > 0) {
                        chunkCount += 1;
                    }
                    chunkCount += (e.vEnd - (e.vStart + headSize)) / chunkSize;
                }
                lastVaddr = e.vEnd;
                ranges.pushBack(e);
            }
            if (lastVaddr % chunkSize != 0) {
                chunkCount += 1;
            }
        }

        auto pool = ObjPool::fromMemory();
        auto* outFile = outputFile(*pool, filename);
        size_t written = 0;
        auto put = [&](const void* data, size_t len) {
            outFile->write(data, len);
            written += len;
        };

        struct DumpFileHdr {
            char magic[12];
            u32 nRanges;
            u32 nChunks;
            u32 chunkSize;
        } fileHdr;

        /* Chunks are zstd frames, each preceded by its compressed length;
           the magic tells a reader of the older zlib-chunked dumps apart. */
        memCpy(fileHdr.magic, "ZstdDump\x97\r\n", sizeof(fileHdr.magic));
        fileHdr.nRanges = ranges.length();
        fileHdr.nChunks = chunkCount;
        fileHdr.chunkSize = chunkSize;
        put(&fileHdr, sizeof(fileHdr));

        struct DumpRangeHdr {
            u64 vStart;
            u64 size;
            u64 fileOfs;

            u16 nameLength;
            u16 _flags;
            u16 _pad[2];
        };

        for (const auto& r : ranges) {
            DumpRangeHdr hdr;
            hdr.vStart = r.vStart;
            hdr.size = r.vEnd - r.vStart;
            hdr.fileOfs = r.fileOfs;
            hdr.nameLength = r.name.length();
            hdr._flags = 0 | (r.flags[0] == 'r' ? 1 : 0);
            hdr._pad[0] = 0;
            hdr._pad[1] = 0;
            put(&hdr, sizeof(hdr));
            put(r.name.data(), r.name.length());
        }
        Buffer packed(ZSTD_compressBound(chunkSize));
        Vector<u8> buf;
        buf.zero(chunkSize);
        size_t chunkCountFlushed = 0;
        auto flushChunk = [&](u64 chunkAddr) {
    #if DEBUG_MEM_DUMP
            sysO << StringView("FLUSH ") << chunkCountFlushed << StringView(" @ ") << written << StringView(" (0x") << formatHex(chunkAddr) << StringView(")") << endL;
    #endif
            put(&chunkAddr, sizeof(chunkAddr));
            chunkCountFlushed += 1;

            /* A dump is many chunks of a large process: a fast level keeps the
               pause short, and the frames still shrink zeroed pages to nothing. */
            const int COMPRESSION_LEVEL = 3;
            const auto len = ZSTD_compress(packed.mutData(), packed.capacity(), buf.data(), buf.length(), COMPRESSION_LEVEL);
            if (ZSTD_isError(len)) {
                compileErrorGeneric("zstd compression of a memory dump chunk failed");
            }
            const u32 packedLength = len;
            put(&packedLength, sizeof(packedLength));
            put(packed.data(), len);
            memZero(buf.mutBegin(), buf.mutEnd());
        };
        u64 lastVaddr = 0;
        for (const auto& r : ranges) {
            if (r.flags[0] == 'r') {
                if (lastVaddr / chunkSize != r.vStart / chunkSize) {
                    if (lastVaddr % chunkSize != 0) {
                        flushChunk(lastVaddr / chunkSize * chunkSize);
                    }
                }
                BUG_ASSERT(chunkCountFlushed == r.firstChunk);
    #if DEBUG_MEM_DUMP
                sysO << chunkCountFlushed << StringView("/") << chunkCount << StringView(": ") << formatHex(r.vStart) << StringView(" -- ") << formatHex(r.vEnd) << StringView("(") << formatHex(r.vEnd - r.vStart) << StringView(") ") << r.flags << StringView(" : ") << r.name << endL;
    #endif
                if (r.vStart / chunkSize == (r.vEnd - 1) / chunkSize) {
                    memCpy(buf.mutData() + r.vStart % chunkSize, (const void*)r.vStart, r.vEnd - r.vStart);
                    if (r.vEnd % chunkSize == 0) {
                        flushChunk(r.vStart / chunkSize * chunkSize);
                    }
                } else {
                    const auto headSize = chunkSize - r.vStart % chunkSize;
                    memCpy(buf.mutData() + r.vStart % chunkSize, (const void*)r.vStart, headSize);
                    flushChunk(r.vStart / chunkSize * chunkSize);
                    const auto tailSize = r.vEnd % chunkSize;
                    const auto tailPos = r.vEnd - tailSize;
                    u64 va = r.vStart + headSize;
                    while (va < tailPos) {
                        memCpy(buf.mutData(), (const void*)va, chunkSize);
                        flushChunk(va / chunkSize * chunkSize);
                        va += chunkSize;
                    }
                    memCpy(buf.mutData(), (const void*)tailPos, tailSize);
                }
                lastVaddr = r.vEnd;
            }
        }
        if (lastVaddr % chunkSize != 0) {
            flushChunk(lastVaddr / chunkSize * chunkSize);
        }
        if (chunkCountFlushed != chunkCount) {
            BUG_ASSERT(false);
        }

        struct RegState {
            u64 pc;
            u64 gprs[16];
        } regs;

        asm volatile("\
            mov %%rax, 0x08(%0);\
            mov %%rdx, 0x10(%0);\
            mov %%rcx, 0x18(%0);\
            mov %%rbx, 0x20(%0);\
            mov %%rsi, 0x28(%0);\
            mov %%rdi, 0x30(%0);\
            mov %%rbp, 0x38(%0);\
            mov %%rsp, 0x40(%0);\
            mov %%r8 , 0x58(%0);\
            mov %%r9 , 0x60(%0);\
            mov %%r10, 0x68(%0);\
            mov %%r11, 0x70(%0);\
            mov %%r12, 0x78(%0);\
            mov %%r13, 0x80(%0);\
            mov %%r14, 0x88(%0);\
            mov %%r15, 0x90(%0);\
            call 1f ;\
            mov %%rax, (%0);\
            jmp 2f ;\
            1: mov (%%rsp), %%rax; ret ; \
            2: \
            "
                     :
                     : "r"(&regs)
                     : "rax");
        put(&regs, sizeof(regs));
        outFile->finish();
#else
        sysE << StringView("NOTE: No memory dump supported on this platform") << endL;
#endif
    }
}
