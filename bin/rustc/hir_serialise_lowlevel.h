#pragma once

#include "floats.h"
#include "int128.h"
#include "rc_string.h"

#include <std/lib/vector.h>

#include <map>
#include <string>
#include <vector>
#include <stddef.h>

namespace stl {
    class ObjPool;
}

class HIRSerialiseWriter {
    class Inner;

    Inner* inner;
    std::map<RcString, unsigned> istringCache;
    std::map<const char*, unsigned> objnameCache;

public:
    HIRSerialiseWriter();
    HIRSerialiseWriter(const HIRSerialiseWriter&) = delete;
    HIRSerialiseWriter(HIRSerialiseWriter&&) = delete;
    ~HIRSerialiseWriter();

    void open(stl::ObjPool& pool, const std::string& filename);
    void write(const void* data, size_t count);

    void writeU8(u8 v) {
        write(reinterpret_cast<const char*>(&v), 1);
    }

    void writeU16(u16 v);

    void writeU32(u32 v);

    void writeU64(u64 v);

    void writeI64(i64 v) {
        writeU64(static_cast<u64>(v));
    }

    void writeU64c(u64 v);

    void writeI64c(i64 v);

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

    void writeString(const std::string& v) {
        writeString(v.size(), v.c_str());
    }

    void writeBool(bool v);

    void rawWriteUint(u64 val);

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

class HIRSerialiseReader {
    class Inner;

    class Buffer {
        stl::Vector<u8> backing;
        unsigned int ofs;

    public:
        Buffer(size_t size);

        size_t capacity() const {
            return backing.capacity();
        }

        size_t read(void* dst, size_t len);
        void populate(Inner& is);
    };

    Inner* inner;
    Buffer buffer;
    size_t pos;
    stl::Vector<RcString> strings;

    std::vector<std::string> objnameCache;

public:
    HIRSerialiseReader(const std::string& path);
    HIRSerialiseReader(const HIRSerialiseWriter&) = delete;
    HIRSerialiseReader(HIRSerialiseWriter&&) = delete;
    ~HIRSerialiseReader();

    size_t getPos() const {
        return pos;
    }

    void read(void* dst, size_t count);

    u8 readU8();

    u16 readU16();

    u32 readU32();

    u64 readU64();

    i64 readI64() {
        return static_cast<i64>(readU64());
    }

    U128 readU128();

    S128 readI128() {
        return S128(readU128());
    }

    u64 readU64c();

    i64 readI64c();

    double readDouble();

    FloatValue readFloatValue();

    unsigned int readTag() {
        return static_cast<unsigned int>(readU8());
    }

    size_t readCount();

    RcString readIstring();

    std::string readString();

    bool readBool();

    u64 rawReadUint();

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
