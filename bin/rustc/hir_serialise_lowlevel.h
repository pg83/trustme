#pragma once

// Encoding protocol ideas:
// > Semi-typed data format (encode length in the format)
// Purpose: Allows internal consistency checking and recovery (recovery not needed here)
//
// 0x00-0xBF are literal integer values.
// 0xC0-0xFB <data>: Short encoded length prefixed data (lengths 0 to 59 bytes)
// 0xFC <len+> <data>: Length prefixed literal data
// 0xFD indicates start of a named object (string index follows)
// 0xFE indicates start of an unnamed object
// 0xFF indicates end of an object

#include "int128.h"
#include "floats.h"
#include <vector>
#include <string>
#include <map>
#include <stddef.h>
#include <assert.h>
#include "rc_string.h"

namespace HIR {
    namespace serialise {

        class WriterInner;
        class ReaderInner;

        class Writer {
            WriterInner* inner;
            ::std::map<RcString, unsigned> istringCache;
            ::std::map<const char*, unsigned> objnameCache;

        public:
            Writer();
            Writer(const Writer&) = delete;
            Writer(Writer&&) = delete;
            ~Writer();

            void open(const ::std::string& filename);
            void write(const void* data, size_t count);

            void write_u8(uint8_t v) {
                write(reinterpret_cast<const char*>(&v), 1);
            }

            void write_u16(uint16_t v);

            void write_u32(uint32_t v);

            void write_u64(uint64_t v);

            void write_i64(int64_t v) {
                write_u64(static_cast<uint64_t>(v));
            }

            // Variable-length encoded u64 (for array sizes)
            void write_u64c(uint64_t v);

            void write_i64c(int64_t v);

            void write_u128(U128 v);

            void write_i128(S128 v) {
                write_u128(v.getInner());
            }

            void write_double(double v);

            void write_float_value(FloatValue value);

            void write_tag(unsigned int t);

            void write_count(size_t c);

            void write_string(const RcString& v);

            void write_string(size_t len, const char* s);

            void write_string(const ::std::string& v) {
                write_string(v.size(), v.c_str());
            }

            void write_bool(bool v);

            // Core protocol
            void rawWriteUint(uint64_t val);

            void rawWriteLen(size_t len);

            void rawWriteBytes(size_t len, const void* data);

            class CloseOnDrop {
                friend class Writer;
                Writer* r;

                CloseOnDrop(Writer& r);

            public:
                CloseOnDrop(CloseOnDrop&& x);

                ~CloseOnDrop();
            };

            CloseOnDrop openObject(const char* name);

            CloseOnDrop openAnonObject();

            void closeObject() {
                write_u8(0xFF);
            }
        };

        class ReadBuffer {
            ::std::vector<uint8_t> backing;
            unsigned int ofs;

        public:
            ReadBuffer(size_t size);

            size_t capacity() const {
                return backing.capacity();
            }

            size_t read(void* dst, size_t len);
            void populate(ReaderInner& is);
        };

        class Reader {
            ReaderInner* inner;
            ReadBuffer buffer;
            size_t pos;
            ::std::vector<RcString> strings;

            ::std::vector<std::string> objnameCache;

        public:
            Reader(const ::std::string& path);
            Reader(const Writer&) = delete;
            Reader(Writer&&) = delete;
            ~Reader();

            size_t getPos() const {
                return pos;
            }

            void read(void* dst, size_t count);

            uint8_t readU8();

            uint16_t readU16();

            uint32_t readU32();

            uint64_t readU64();

            int64_t readI64() {
                return static_cast<int64_t>(readU64());
            }

            U128 readU128();

            S128 readI128() {
                return S128(readU128());
            }

            // Variable-length encoded u64 (for array sizes)
            uint64_t readU64c();

            int64_t readI64c();

            double readDouble();

            FloatValue readFloatValue();

            unsigned int readTag() {
                return static_cast<unsigned int>(readU8());
            }

            size_t readCount();

            RcString readIstring();

            ::std::string readString();

            bool readBool();

            // Core protocol
            uint64_t rawReadUint();

            size_t rawReadLen();

            std::string rawReadBytesStdstring();

            class CloseOnDrop {
                friend class Reader;
                Reader* r;

                CloseOnDrop(Reader& r);

            public:
                CloseOnDrop(const CloseOnDrop&) = delete;

                CloseOnDrop(CloseOnDrop&& x);

                ~CloseOnDrop();
            };

            CloseOnDrop openObject(const char* name);

            CloseOnDrop openAnonObject();

            void closeObject();
        };

    } // namespace serialise
} // namespace HIR
