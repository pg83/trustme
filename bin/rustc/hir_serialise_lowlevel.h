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
            WriterInner* m_inner;
            ::std::map<RcString, unsigned> m_istring_cache;
            ::std::map<const char*, unsigned> m_objname_cache;

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
                write_u128(v.get_inner());
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
            void raw_write_uint(uint64_t val);

            void raw_write_len(size_t len);

            void raw_write_bytes(size_t len, const void* data);

            class CloseOnDrop {
                friend class Writer;
                Writer* r;

                CloseOnDrop(Writer& r);

            public:
                CloseOnDrop(CloseOnDrop&& x);

                ~CloseOnDrop();
            };

            CloseOnDrop open_object(const char* name);

            CloseOnDrop open_anon_object();

            void close_object() {
                write_u8(0xFF);
            }
        };

        class ReadBuffer {
            ::std::vector<uint8_t> m_backing;
            unsigned int m_ofs;

        public:
            ReadBuffer(size_t size);

            size_t capacity() const {
                return m_backing.capacity();
            }

            size_t read(void* dst, size_t len);
            void populate(ReaderInner& is);
        };

        class Reader {
            ReaderInner* m_inner;
            ReadBuffer m_buffer;
            size_t m_pos;
            ::std::vector<RcString> m_strings;

            ::std::vector<std::string> m_objname_cache;

        public:
            Reader(const ::std::string& path);
            Reader(const Writer&) = delete;
            Reader(Writer&&) = delete;
            ~Reader();

            size_t get_pos() const {
                return m_pos;
            }

            void read(void* dst, size_t count);

            uint8_t read_u8();

            uint16_t read_u16();

            uint32_t read_u32();

            uint64_t read_u64();

            int64_t read_i64() {
                return static_cast<int64_t>(read_u64());
            }

            U128 read_u128();

            S128 read_i128() {
                return S128(read_u128());
            }

            // Variable-length encoded u64 (for array sizes)
            uint64_t read_u64c();

            int64_t read_i64c();

            double read_double();

            FloatValue read_float_value();

            unsigned int read_tag() {
                return static_cast<unsigned int>(read_u8());
            }

            size_t read_count();

            RcString read_istring();

            ::std::string read_string();

            bool read_bool();

            // Core protocol
            uint64_t raw_read_uint();

            size_t raw_read_len();

            std::string raw_read_bytes_stdstring();

            class CloseOnDrop {
                friend class Reader;
                Reader* r;

                CloseOnDrop(Reader& r);

            public:
                CloseOnDrop(const CloseOnDrop&) = delete;

                CloseOnDrop(CloseOnDrop&& x);

                ~CloseOnDrop();
            };

            CloseOnDrop open_object(const char* name);

            CloseOnDrop open_anon_object();

            void close_object();
        };

    } // namespace serialise
} // namespace HIR
