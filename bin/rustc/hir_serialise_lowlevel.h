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


    class HIRSerialiseWriterInner;
    class HIRSerialiseReaderInner;

    class HIRSerialiseWriter {
        HIRSerialiseWriterInner* inner;
        ::std::map<RcString, unsigned> istringCache;
        ::std::map<const char*, unsigned> objnameCache;

    public:
        HIRSerialiseWriter();
        HIRSerialiseWriter(const HIRSerialiseWriter&) = delete;
        HIRSerialiseWriter(HIRSerialiseWriter&&) = delete;
        ~HIRSerialiseWriter();

        void open(const ::std::string& filename);
        void write(const void* data, size_t count);

        void writeU8(uint8_t v) {
            write(reinterpret_cast<const char*>(&v), 1);
        }

        void writeU16(uint16_t v);

        void writeU32(uint32_t v);

        void writeU64(uint64_t v);

        void writeI64(int64_t v) {
            writeU64(static_cast<uint64_t>(v));
        }

        // Variable-length encoded u64 (for array sizes)
        void writeU64c(uint64_t v);

        void writeI64c(int64_t v);

        void writeU128(U128 v);

        void writeI128(S128 v) {
            writeU128(v.getInner());
        }

        void writeDouble(double v);

        void writeFloatValue(FloatValue value);

        void writeTag(unsigned int t);

        void writeCount(size_t c);

        void writeString(const RcString& v);

        void writeString(size_t len, const char* s);

        void writeString(const ::std::string& v) {
            writeString(v.size(), v.c_str());
        }

        void writeBool(bool v);

        // Core protocol
        void rawWriteUint(uint64_t val);

        void rawWriteLen(size_t len);

        void rawWriteBytes(size_t len, const void* data);

        class CloseOnDrop {
            friend class HIRSerialiseWriter;
            HIRSerialiseWriter* r;

            CloseOnDrop(HIRSerialiseWriter& r);

        public:
            CloseOnDrop(CloseOnDrop&& x);

            ~CloseOnDrop();
        };

        CloseOnDrop openObject(const char* name);

        CloseOnDrop openAnonObject();

        void closeObject() {
            writeU8(0xFF);
        }
    };

    class HIRSerialiseReadBuffer {
        ::std::vector<uint8_t> backing;
        unsigned int ofs;

    public:
        HIRSerialiseReadBuffer(size_t size);

        size_t capacity() const {
            return backing.capacity();
        }

        size_t read(void* dst, size_t len);
        void populate(HIRSerialiseReaderInner& is);
    };

    class HIRSerialiseReader {
        HIRSerialiseReaderInner* inner;
        HIRSerialiseReadBuffer buffer;
        size_t pos;
        ::std::vector<RcString> strings;

        ::std::vector<std::string> objnameCache;

    public:
        HIRSerialiseReader(const ::std::string& path);
        HIRSerialiseReader(const HIRSerialiseWriter&) = delete;
        HIRSerialiseReader(HIRSerialiseWriter&&) = delete;
        ~HIRSerialiseReader();

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
            friend class HIRSerialiseReader;
            HIRSerialiseReader* r;

            CloseOnDrop(HIRSerialiseReader& r);

        public:
            CloseOnDrop(const CloseOnDrop&) = delete;

            CloseOnDrop(CloseOnDrop&& x);

            ~CloseOnDrop();
        };

        CloseOnDrop openObject(const char* name);

        CloseOnDrop openAnonObject();

        void closeObject();
    };

