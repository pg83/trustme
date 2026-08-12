#include "memory_dump.h"
#include <iostream>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <vector>

#if defined(__linux__)
    #include <zlib.h>
#endif

void memory_dump(const char* phase) {
    if (getenv("MRUSTC_DUMPMEM")) {
        static unsigned s_count;
        auto idx = s_count++;
        char filename[256];
        sprintf(filename, "mrustc-%i-%s.dmp", idx, phase);
#if defined(__linux__) && defined(__x86_64__)
        // On linux, dump out a custom format that covers the entire address space
        // Could save as an ELF core dump, but lazy
        //
        // For the format, see down near `struct DumpFileHdr`
    #define DEBUG_MEM_DUMP 1

        // 1. Enumerate all memory ranges
        struct RangeEnt {
            uint64_t v_start = 0;
            uint64_t v_end = 0;
            char flagsStr[5];
            uint64_t fileOfs = 0;
            int devMaj = 0;
            int devMin = 0;
            int inode = 0;
            ::std::string name;
            uint32_t firstChunk;
        };

        size_t chunkSize = 1 << 20;
        ::std::vector<RangeEnt> range_ents;
        size_t chunkCount = 0;
        // - Open `/proc/self/maps`, parse `<start>-<end> <flags> <ofs> <maj>:<minor> <inode> <file_name>`
        {
            uint64_t last_vaddr = 0;
            FILE* fp = ::std::fopen("/proc/self/maps", "r");
            while (!feof(fp)) {
                RangeEnt e;
                if (fscanf(fp, "%lx-%lx %4s %lx %d:%d %d", &e.v_start, &e.v_end, e.flagsStr, &e.fileOfs, &e.devMaj, &e.devMin, &e.inode) != 7) {
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

                //printf("%i : %s\n", chunk_count, e.name.c_str());
                // Chunk count
                if (e.flagsStr[0] != 'r') {
                    continue;
                }

                if (last_vaddr / chunkSize != e.v_start / chunkSize) {
                    //::std::cout << "e.name =" << e.name << "\n";
                    if (last_vaddr % chunkSize != 0) {
                        chunkCount += 1;
                    }
                    // Otherwise, the chunk would have already been flushed
                }
                e.firstChunk = chunkCount;
                if (e.v_start / chunkSize == (e.v_end - 1) / chunkSize) {
                    // No chunk used
                    if (e.v_end % chunkSize == 0) {
                        chunkCount += 1;
                    }
                } else {
                    // uses at least one chunk
                    auto headSize = (chunkSize - e.v_start % chunkSize) % chunkSize;
                    if (headSize > 0) {
                        chunkCount += 1;
                    }
                    chunkCount += (e.v_end - (e.v_start + headSize)) / chunkSize;
                }
                last_vaddr = e.v_end;
                // Add entry
                range_ents.push_back(std::move(e));
            }
            // Account for last chunk's count
            if (last_vaddr % chunkSize != 0) {
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
        FILE* out_fp = fopen(filename, "wb");

        // - Header
        struct DumpFileHdr {
            char magic[12];
            uint32_t n_ranges;
            uint32_t n_chunks;
            uint32_t chunkSize;
        } fileHdr;

        strcpy(fileHdr.magic, "FullDump\x97\r\n");
        fileHdr.n_ranges = range_ents.size();
        fileHdr.n_chunks = chunkCount;
        fileHdr.chunkSize = chunkSize;
        fwrite(&fileHdr, sizeof(fileHdr), 1, out_fp);

        // - Write out the parsed maps
        struct DumpRangeHdr {
            uint64_t v_start;
            uint64_t size;
            uint64_t fileOfs;

            uint16_t name_length;
            uint16_t _flags;
            uint16_t _pad[2];
        };

        for (const auto& r : range_ents) {
            DumpRangeHdr hdr;
            hdr.v_start = r.v_start;
            hdr.size = r.v_end - r.v_start;
            hdr.fileOfs = r.fileOfs;
            hdr.name_length = r.name.size();
            hdr._flags = 0 | (r.flagsStr[0] == 'r' ? 1 : 0);
            hdr._pad[0] = 0;
            hdr._pad[1] = 0;
            fwrite(&hdr, sizeof(hdr), 1, out_fp);
            fwrite(r.name.c_str(), 1, r.name.size(), out_fp);
        }
        // - Write out the content of the maps
        ::std::vector<unsigned char> zlib_buffer(16 * 1024);
        ::std::vector<uint8_t> buf(chunkSize);
        size_t chunkCountFlushed = 0;
        auto flushChunk = [&](uint64_t chunkAddr) {
    #if DEBUG_MEM_DUMP
            printf("FLUSH %zi @ %li (0x%lx)\n", chunkCountFlushed, ftell(out_fp), chunkAddr);
    #endif
            fwrite(&chunkAddr, sizeof(chunkAddr), 1, out_fp);
            chunkCountFlushed += 1;
            z_stream zstream;
            zstream.zalloc = Z_NULL;
            zstream.zfree = Z_NULL;
            zstream.opaque = Z_NULL;

            const int COMPRESSION_LEVEL = Z_BEST_COMPRESSION;
            int ret = deflateInit(&zstream, COMPRESSION_LEVEL);
            if (ret != Z_OK)
                throw ::std::runtime_error("zlib init failure");

            zstream.avail_out = zlib_buffer.size();
            zstream.next_out = zlib_buffer.data();

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
                if (zstream.avail_out < zlib_buffer.size()) {
                    size_t bytes = zlib_buffer.size() - zstream.avail_out;
                    fwrite(zlib_buffer.data(), bytes, 1, out_fp);

                    zstream.avail_out = zlib_buffer.size();
                    zstream.next_out = zlib_buffer.data();
                }
            }

            // Complete the compression
            do {
                ret = deflate(&zstream, Z_FINISH);
                if (ret == Z_STREAM_ERROR) {
                    ::std::cerr << "ERROR: zlib deflate stream error (cleanup)";
                    abort();
                }
                if (zstream.avail_out != zlib_buffer.size()) {
                    size_t bytes = zlib_buffer.size() - zstream.avail_out;
                    fwrite(zlib_buffer.data(), bytes, 1, out_fp);

                    zstream.avail_out = zlib_buffer.size();
                    zstream.next_out = zlib_buffer.data();
                }
            } while (ret == Z_OK);
            deflateEnd(&zstream);
            // Zero the buffer, just to make compression better on partial blocks
            memset(buf.data(), 0, buf.size());
        };
        uint64_t last_vaddr = 0;
        for (const auto& r : range_ents) {
            if (r.flagsStr[0] == 'r') {
                if (last_vaddr / chunkSize != r.v_start / chunkSize) {
                    // Flush chunk, if the last end was not aligned
                    if (last_vaddr % chunkSize != 0) {
                        flushChunk(last_vaddr / chunkSize * chunkSize);
                    }
                }
                assert(chunkCountFlushed == r.firstChunk);
    #if DEBUG_MEM_DUMP
                ::std::cout << chunkCountFlushed << "/" << chunkCount << ": " << std::hex << r.v_start << " -- " << r.v_end << "(" << (r.v_end - r.v_start) << ")" << std::dec << " " << r.flagsStr << " : " << r.name << "\n";
    #endif
                if (r.v_start / chunkSize == (r.v_end - 1) / chunkSize) {
                    // Small
                    memcpy(buf.data() + r.v_start % chunkSize, (const void*)r.v_start, r.v_end - r.v_start);
                    // Flush if this has just finished a chunk
                    if (r.v_end % chunkSize == 0) {
                        flushChunk(r.v_start / chunkSize * chunkSize);
                    }
                } else {
                    // Leading partial
                    const auto headSize = chunkSize - r.v_start % chunkSize;
                    memcpy(buf.data() + r.v_start % chunkSize, (const void*)r.v_start, headSize);
                    flushChunk(r.v_start / chunkSize * chunkSize);
                    // Fill whole chunks
                    const auto tail_size = r.v_end % chunkSize;
                    const auto tail_pos = r.v_end - tail_size;
                    uint64_t va = r.v_start + headSize;
                    while (va < tail_pos) {
                        //printf("%lx+%lx (mid)\n", va, chunk_size);
                        memcpy(buf.data(), (const void*)va, chunkSize);
                        flushChunk(va / chunkSize * chunkSize);
                        va += chunkSize;
                    }
                    // Fill tail chunk (no flush)
                    //printf("%lx+%lx (tail)\n", tail_pos, tail_size);
                    memcpy(buf.data(), (const void*)tail_pos, tail_size);
                    // - No flush, next push will do that
                }
                last_vaddr = r.v_end;
                //printf("> last_vaddr=%li\n", last_vaddr);
            }
        }
        if (last_vaddr % chunkSize != 0) {
            flushChunk(last_vaddr / chunkSize * chunkSize);
        }
        if (chunkCountFlushed != chunkCount) {
            //printf("BUG: flushed %i chunks, but expected %i\n", chunk_count_flushed, chunk_count);
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
        fwrite(&regs, sizeof(regs), 1, out_fp);
        fclose(out_fp);
#else
        std::cerr << "NOTE: No memory dump supported on this platform" << std::endl;
#endif
    }
}
