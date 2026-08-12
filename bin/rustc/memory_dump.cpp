#include "memory_dump.h"
#include <iostream>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <vector>

#if defined(__linux__)
    #include <zlib.h>
#endif

void memoryDump(const char* phase) {
    if (getenv("MRUSTC_DUMPMEM")) {
        static unsigned sCount;
        auto idx = sCount++;
        char filename[256];
        sprintf(filename, "mrustc-%i-%s.dmp", idx, phase);
#if defined(__linux__) && defined(__x86_64__)
        // On linux, dump out a custom format that covers the entire address space
        // Could save as an ELF core dump, but lazy
        // For the format, see down near `struct DumpFileHdr`
    #define DEBUG_MEM_DUMP 1

        // 1. Enumerate all memory ranges
        struct RangeEnt {
            uint64_t vStart = 0;
            uint64_t vEnd = 0;
            char flagsStr[5];
            uint64_t fileOfs = 0;
            int devMaj = 0;
            int devMin = 0;
            int inode = 0;
            ::std::string name;
            uint32_t firstChunk;
        };

        size_t chunkSize = 1 << 20;
        ::std::vector<RangeEnt> rangeEnts;
        size_t chunkCount = 0;
        // - Open `/proc/self/maps`, parse `<start>-<end> <flags> <ofs> <maj>:<minor> <inode> <file_name>`
        {
            uint64_t lastVaddr = 0;
            FILE* fp = ::std::fopen("/proc/self/maps", "r");
            while (!feof(fp)) {
                RangeEnt e;
                if (fscanf(fp, "%lx-%lx %4s %lx %d:%d %d", &e.vStart, &e.vEnd, e.flagsStr, &e.fileOfs, &e.devMaj, &e.devMin, &e.inode) != 7) {
                    // Uh-oh
                }
                //::std::cout << "e.inode=" << e.inode << "\n";
                for (;;) {
                    int ch = getc(fp);
                    //::std::cout << " " << ch;
                    if (ch < 0 || ch == '\n') {
                        break;
                    }
                    // Skip leading spaces
                    if (ch == ' ' && e.name.empty()) {
                        continue;
                    }
                    e.name.push_back(ch);
                }

                if (e.name == "[vvar]") {
                    continue;
                }

                // Chunk count
                if (e.flagsStr[0] != 'r') {
                    continue;
                }

                if (lastVaddr / chunkSize != e.vStart / chunkSize) {
                    //::std::cout << "e.name =" << e.name << "\n";
                    if (lastVaddr % chunkSize != 0) {
                        chunkCount += 1;
                    }
                    // Otherwise, the chunk would have already been flushed
                }
                e.firstChunk = chunkCount;
                if (e.vStart / chunkSize == (e.vEnd - 1) / chunkSize) {
                    // No chunk used
                    if (e.vEnd % chunkSize == 0) {
                        chunkCount += 1;
                    }
                } else {
                    // uses at least one chunk
                    auto headSize = (chunkSize - e.vStart % chunkSize) % chunkSize;
                    if (headSize > 0) {
                        chunkCount += 1;
                    }
                    chunkCount += (e.vEnd - (e.vStart + headSize)) / chunkSize;
                }
                lastVaddr = e.vEnd;
                // Add entry
                rangeEnts.push_back(std::move(e));
            }
            // Account for last chunk's count
            if (lastVaddr % chunkSize != 0) {
                chunkCount += 1;
            }
            fclose(fp);
        }

        // FORMAT:
        // - A fixed header
        // - Memory map information (see `DumpRangeHdr`)
        // - zlib-compressed memory contents, chunked by `chunk_size` and omitting completely empty regions
        //   - Each chunk starts with the virtual address (64-bits)
        // - Finally, register dump (PC, then x86 dwarf ordering)
        FILE* outFp = fopen(filename, "wb");

        // - Header
        struct DumpFileHdr {
            char magic[12];
            uint32_t nRanges;
            uint32_t nChunks;
            uint32_t chunkSize;
        } fileHdr;

        strcpy(fileHdr.magic, "FullDump\x97\r\n");
        fileHdr.nRanges = rangeEnts.size();
        fileHdr.nChunks = chunkCount;
        fileHdr.chunkSize = chunkSize;
        fwrite(&fileHdr, sizeof(fileHdr), 1, outFp);

        // - Write out the parsed maps
        struct DumpRangeHdr {
            uint64_t vStart;
            uint64_t size;
            uint64_t fileOfs;

            uint16_t nameLength;
            uint16_t _flags;
            uint16_t _pad[2];
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
        // - Write out the content of the maps
        ::std::vector<unsigned char> zlibBuffer(16 * 1024);
        ::std::vector<uint8_t> buf(chunkSize);
        size_t chunkCountFlushed = 0;
        auto flushChunk = [&](uint64_t chunkAddr) {
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
                throw ::std::runtime_error("zlib init failure");

            zstream.avail_out = zlibBuffer.size();
            zstream.next_out = zlibBuffer.data();

            zstream.avail_in = buf.size();
            zstream.next_in = buf.data();

            // While there's data to compress
            while (zstream.avail_in > 0) {
                assert(zstream.avail_out != 0);

                // Compress the data
                int ret = deflate(&zstream, Z_NO_FLUSH);
                if (ret == Z_STREAM_ERROR)
                    throw ::std::runtime_error("zlib deflate stream error");

                // If the entire input wasn't consumed, then it was likely due to a lack of output space
                // - Flush the output buffer to the file
                if (zstream.avail_out < zlibBuffer.size()) {
                    size_t bytes = zlibBuffer.size() - zstream.avail_out;
                    fwrite(zlibBuffer.data(), bytes, 1, outFp);

                    zstream.avail_out = zlibBuffer.size();
                    zstream.next_out = zlibBuffer.data();
                }
            }

            // Complete the compression
            do {
                ret = deflate(&zstream, Z_FINISH);
                if (ret == Z_STREAM_ERROR) {
                    ::std::cerr << "ERROR: zlib deflate stream error (cleanup)";
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
            // Zero the buffer, just to make compression better on partial blocks
            memset(buf.data(), 0, buf.size());
        };
        uint64_t lastVaddr = 0;
        for (const auto& r : rangeEnts) {
            if (r.flagsStr[0] == 'r') {
                if (lastVaddr / chunkSize != r.vStart / chunkSize) {
                    // Flush chunk, if the last end was not aligned
                    if (lastVaddr % chunkSize != 0) {
                        flushChunk(lastVaddr / chunkSize * chunkSize);
                    }
                }
                assert(chunkCountFlushed == r.firstChunk);
    #if DEBUG_MEM_DUMP
                ::std::cout << chunkCountFlushed << "/" << chunkCount << ": " << std::hex << r.vStart << " -- " << r.vEnd << "(" << (r.vEnd - r.vStart) << ")" << std::dec << " " << r.flagsStr << " : " << r.name << "\n";
    #endif
                if (r.vStart / chunkSize == (r.vEnd - 1) / chunkSize) {
                    // Small
                    memcpy(buf.data() + r.vStart % chunkSize, (const void*)r.vStart, r.vEnd - r.vStart);
                    // Flush if this has just finished a chunk
                    if (r.vEnd % chunkSize == 0) {
                        flushChunk(r.vStart / chunkSize * chunkSize);
                    }
                } else {
                    // Leading partial
                    const auto headSize = chunkSize - r.vStart % chunkSize;
                    memcpy(buf.data() + r.vStart % chunkSize, (const void*)r.vStart, headSize);
                    flushChunk(r.vStart / chunkSize * chunkSize);
                    // Fill whole chunks
                    const auto tailSize = r.vEnd % chunkSize;
                    const auto tailPos = r.vEnd - tailSize;
                    uint64_t va = r.vStart + headSize;
                    while (va < tailPos) {
                        memcpy(buf.data(), (const void*)va, chunkSize);
                        flushChunk(va / chunkSize * chunkSize);
                        va += chunkSize;
                    }
                    // Fill tail chunk (no flush)
                    memcpy(buf.data(), (const void*)tailPos, tailSize);
                    // - No flush, next push will do that
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

        // - Save/dump register state
        // > PC, and then all 16 amd64 GPRs
        struct RegState {
            uint64_t pc;
            uint64_t gprs[16];
        } regs;

        // Dwarf ordering: ADCB,SI,DI,BP,SP,r8-15
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
