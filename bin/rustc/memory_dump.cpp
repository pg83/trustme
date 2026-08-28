#include "memory_dump.h"

#include <std/sys/types.h>

#include <vector>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>

#if defined(__linux__)
    #include <zlib.h>
#endif

void memoryDump(unsigned& sequence, const char* phase) {
    if (getenv("TRUSTME_DUMPMEM")) {
        auto idx = sequence++;
        char filename[256];
        sprintf(filename, "trustme-%i-%s.dmp", idx, phase);
#if defined(__linux__) && defined(__x86_64__)
    #define DEBUG_MEM_DUMP 1

        struct RangeEnt {
            u64 vStart = 0;
            u64 vEnd = 0;
            char flagsStr[5];
            u64 fileOfs = 0;
            int devMaj = 0;
            int devMin = 0;
            int inode = 0;
            std::string name;
            u32 firstChunk;
        };

        size_t chunkSize = 1 << 20;
        std::vector<RangeEnt> rangeEnts;
        size_t chunkCount = 0;
        {
            u64 lastVaddr = 0;
            FILE* fp = std::fopen("/proc/self/maps", "r");
            while (!feof(fp)) {
                RangeEnt e;
                if (fscanf(fp, "%lx-%lx %4s %lx %d:%d %d", &e.vStart, &e.vEnd, e.flagsStr, &e.fileOfs, &e.devMaj, &e.devMin, &e.inode) != 7) {
                }
                for (;;) {
                    int ch = getc(fp);
                    if (ch < 0 || ch == '\n') {
                        break;
                    }
                    if (ch == ' ' && e.name.empty()) {
                        continue;
                    }
                    e.name.push_back(ch);
                }

                if (e.name == "[vvar]") {
                    continue;
                }

                if (e.flagsStr[0] != 'r') {
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
                rangeEnts.push_back(std::move(e));
            }
            if (lastVaddr % chunkSize != 0) {
                chunkCount += 1;
            }
            fclose(fp);
        }

        FILE* outFp = fopen(filename, "wb");

        struct DumpFileHdr {
            char magic[12];
            u32 nRanges;
            u32 nChunks;
            u32 chunkSize;
        } fileHdr;

        strcpy(fileHdr.magic, "FullDump\x97\r\n");
        fileHdr.nRanges = rangeEnts.size();
        fileHdr.nChunks = chunkCount;
        fileHdr.chunkSize = chunkSize;
        fwrite(&fileHdr, sizeof(fileHdr), 1, outFp);

        struct DumpRangeHdr {
            u64 vStart;
            u64 size;
            u64 fileOfs;

            u16 nameLength;
            u16 _flags;
            u16 _pad[2];
        };

        for (const auto& r : rangeEnts) {
            DumpRangeHdr hdr;
            hdr.vStart = r.vStart;
            hdr.size = r.vEnd - r.vStart;
            hdr.fileOfs = r.fileOfs;
            hdr.nameLength = r.name.size();
            hdr._flags = 0 | (r.flagsStr[0] == 'r' ? 1 : 0);
            hdr._pad[0] = 0;
            hdr._pad[1] = 0;
            fwrite(&hdr, sizeof(hdr), 1, outFp);
            fwrite(r.name.c_str(), 1, r.name.size(), outFp);
        }
        std::vector<unsigned char> zlibBuffer(16 * 1024);
        std::vector<u8> buf(chunkSize);
        size_t chunkCountFlushed = 0;
        auto flushChunk = [&](u64 chunkAddr) {
    #if DEBUG_MEM_DUMP
            printf("FLUSH %zi @ %li (0x%lx)\n", chunkCountFlushed, ftell(outFp), chunkAddr);
    #endif
            fwrite(&chunkAddr, sizeof(chunkAddr), 1, outFp);
            chunkCountFlushed += 1;
            z_stream zstream;
            zstream.zalloc = Z_NULL;
            zstream.zfree = Z_NULL;
            zstream.opaque = Z_NULL;

            const int COMPRESSION_LEVEL = Z_BEST_COMPRESSION;
            int ret = deflateInit(&zstream, COMPRESSION_LEVEL);
            if (ret != Z_OK)
                throw std::runtime_error("zlib init failure");

            zstream.avail_out = zlibBuffer.size();
            zstream.next_out = zlibBuffer.data();

            zstream.avail_in = buf.size();
            zstream.next_in = reinterpret_cast<unsigned char*>(buf.data());

            while (zstream.avail_in > 0) {
                assert(zstream.avail_out != 0);

                int ret = deflate(&zstream, Z_NO_FLUSH);
                if (ret == Z_STREAM_ERROR)
                    throw std::runtime_error("zlib deflate stream error");

                if (zstream.avail_out < zlibBuffer.size()) {
                    size_t bytes = zlibBuffer.size() - zstream.avail_out;
                    fwrite(zlibBuffer.data(), bytes, 1, outFp);

                    zstream.avail_out = zlibBuffer.size();
                    zstream.next_out = zlibBuffer.data();
                }
            }

            do {
                ret = deflate(&zstream, Z_FINISH);
                if (ret == Z_STREAM_ERROR) {
                    std::cerr << "ERROR: zlib deflate stream error (cleanup)";
                    abort();
                }
                if (zstream.avail_out != zlibBuffer.size()) {
                    size_t bytes = zlibBuffer.size() - zstream.avail_out;
                    fwrite(zlibBuffer.data(), bytes, 1, outFp);

                    zstream.avail_out = zlibBuffer.size();
                    zstream.next_out = zlibBuffer.data();
                }
            } while (ret == Z_OK);
            deflateEnd(&zstream);
            memset(buf.data(), 0, buf.size());
        };
        u64 lastVaddr = 0;
        for (const auto& r : rangeEnts) {
            if (r.flagsStr[0] == 'r') {
                if (lastVaddr / chunkSize != r.vStart / chunkSize) {
                    if (lastVaddr % chunkSize != 0) {
                        flushChunk(lastVaddr / chunkSize * chunkSize);
                    }
                }
                assert(chunkCountFlushed == r.firstChunk);
    #if DEBUG_MEM_DUMP
                std::cout << chunkCountFlushed << "/" << chunkCount << ": " << std::hex << r.vStart << " -- " << r.vEnd << "(" << (r.vEnd - r.vStart) << ")" << std::dec << " " << r.flagsStr << " : " << r.name << "\n";
    #endif
                if (r.vStart / chunkSize == (r.vEnd - 1) / chunkSize) {
                    memcpy(buf.data() + r.vStart % chunkSize, (const void*)r.vStart, r.vEnd - r.vStart);
                    if (r.vEnd % chunkSize == 0) {
                        flushChunk(r.vStart / chunkSize * chunkSize);
                    }
                } else {
                    const auto headSize = chunkSize - r.vStart % chunkSize;
                    memcpy(buf.data() + r.vStart % chunkSize, (const void*)r.vStart, headSize);
                    flushChunk(r.vStart / chunkSize * chunkSize);
                    const auto tailSize = r.vEnd % chunkSize;
                    const auto tailPos = r.vEnd - tailSize;
                    u64 va = r.vStart + headSize;
                    while (va < tailPos) {
                        memcpy(buf.data(), (const void*)va, chunkSize);
                        flushChunk(va / chunkSize * chunkSize);
                        va += chunkSize;
                    }
                    memcpy(buf.data(), (const void*)tailPos, tailSize);
                }
                lastVaddr = r.vEnd;
            }
        }
        if (lastVaddr % chunkSize != 0) {
            flushChunk(lastVaddr / chunkSize * chunkSize);
        }
        if (chunkCountFlushed != chunkCount) {
            assert(false);
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
        fwrite(&regs, sizeof(regs), 1, outFp);
        fclose(outFp);
#else
        std::cerr << "NOTE: No memory dump supported on this platform" << std::endl;
#endif
    }
}
