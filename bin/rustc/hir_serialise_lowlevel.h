#pragma once

#include "floats.h"
#include "int128.h"
#include "rc_string.h"

#include <string>
#include <stddef.h>

namespace stl {
    class ObjPool;
}

struct HIRSerialiseWriter {
    struct CloseOnDrop {
        HIRSerialiseWriter* r;

        explicit CloseOnDrop(HIRSerialiseWriter& r);
        CloseOnDrop(const CloseOnDrop&) = delete;
        CloseOnDrop(CloseOnDrop&& x);
        ~CloseOnDrop();
    };

    virtual void open(const std::string& filename) = 0;
    virtual void write(const void* data, size_t count) = 0;
    virtual void writeU16(u16 v) = 0;
    virtual void writeU32(u32 v) = 0;
    virtual void writeU64(u64 v) = 0;
    virtual void writeU64c(u64 v) = 0;
    virtual void writeI64c(i64 v) = 0;
    virtual void writeU128(U128 v) = 0;
    virtual void writeDouble(double v) = 0;
    virtual void writeFloatValue(FloatValue value) = 0;
    virtual void writeTag(unsigned int t) = 0;
    virtual void writeCount(size_t c) = 0;
    virtual void writeString(const RcString& v) = 0;
    virtual void writeString(size_t len, const char* s) = 0;
    virtual void writeBool(bool v) = 0;
    virtual void rawWriteUint(u64 val) = 0;
    virtual void rawWriteLen(size_t len) = 0;
    virtual void rawWriteBytes(size_t len, const void* data) = 0;
    virtual CloseOnDrop openObject(const char* name) = 0;
    virtual CloseOnDrop openAnonObject() = 0;

    void writeU8(u8 v);
    void writeI64(i64 v);
    void writeI128(S128 v);
    void writeString(const std::string& v);
    void closeObject();

    static HIRSerialiseWriter* create(stl::ObjPool& pool);
};

struct HIRSerialiseReader {
    struct CloseOnDrop {
        HIRSerialiseReader* r;

        explicit CloseOnDrop(HIRSerialiseReader& r);
        CloseOnDrop(const CloseOnDrop&) = delete;
        CloseOnDrop(CloseOnDrop&& x);
        ~CloseOnDrop();
    };

    virtual size_t getPos() const = 0;
    virtual void read(void* dst, size_t count) = 0;
    virtual u8 readU8() = 0;
    virtual u16 readU16() = 0;
    virtual u32 readU32() = 0;
    virtual u64 readU64() = 0;
    virtual U128 readU128() = 0;
    virtual u64 readU64c() = 0;
    virtual i64 readI64c() = 0;
    virtual double readDouble() = 0;
    virtual FloatValue readFloatValue() = 0;
    virtual size_t readCount() = 0;
    virtual RcString readIstring() = 0;
    virtual std::string readString() = 0;
    virtual bool readBool() = 0;
    virtual u64 rawReadUint() = 0;
    virtual size_t rawReadLen() = 0;
    virtual std::string rawReadBytesStdstring() = 0;
    virtual CloseOnDrop openObject(const char* name) = 0;
    virtual CloseOnDrop openAnonObject() = 0;
    virtual void closeObject() = 0;

    i64 readI64();
    S128 readI128();
    unsigned int readTag();

    static HIRSerialiseReader* create(stl::ObjPool& pool, const std::string& path);
};
