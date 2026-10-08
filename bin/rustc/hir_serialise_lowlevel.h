#pragma once

#include "floats.h"
#include "int128.h"
#include "rc_string.h"

#include <string>
#include <stddef.h>
#include <string.h>

namespace stl {
    class ObjPool;
    class StringView;
}

struct HIRSerialiseWriter {
    struct CloseOnDrop {
        HIRSerialiseWriter* r;

        explicit CloseOnDrop(HIRSerialiseWriter& r);
        CloseOnDrop(const CloseOnDrop&) = delete;
        CloseOnDrop(CloseOnDrop&& x);
        ~CloseOnDrop();
    };

    virtual void open(stl::StringView filename) = 0;
    virtual void finish() = 0;
    virtual void write(const void* data, size_t count) = 0;
    virtual void writeU16(u16 v) = 0;
    virtual void writeU32(u32 v) = 0;
    virtual void writeU64(u64 v) = 0;
    virtual void writeU128(U128 v) = 0;
    virtual void writeDouble(double v) = 0;
    virtual void writeFloatValue(FloatValue value) = 0;
    virtual void writeTag(unsigned int t) = 0;
    virtual void writeCount(size_t c) = 0;
    virtual void writeString(const RcString& v) = 0;
    virtual void writeString(size_t len, const char* s) = 0;
    virtual void writeBool(bool v) = 0;
    virtual size_t reserveCount() = 0;
    virtual void finishCount(size_t at) = 0;
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

    struct ObjectName {
        const char* name;
        u32 index;
    };

    const u8* begin = nullptr;
    const u8* cur = nullptr;
    const u8* end = nullptr;
    const RcString* strings = nullptr;
    ObjectName objectNames[32] = {};

    size_t getPos() const {
        return static_cast<size_t>(cur - begin);
    }

    void setPos(size_t at);

    void read(void* dst, size_t count) {
        need(count);
        memcpy(dst, cur, count);
        cur += count;
    }

    u8 readU8() {
        need(1);
        return *cur++;
    }

    u16 readU16() {
        return readRaw<u16>();
    }

    u32 readU32() {
        return readRaw<u32>();
    }

    u64 readU64() {
        return readRaw<u64>();
    }

    double readDouble() {
        return readRaw<double>();
    }

    size_t readCount() {
        return readU32();
    }

    RcString readIstring() {
        return strings[readCount()];
    }

    bool readBool() {
        const u8 v = readU8();
        if (v > 1) {
            badBool(v);
        }
        return v != 0;
    }

    unsigned int readTag() {
        return readU8();
    }

    void closeObject() {
        if (readU8() != TAG_CLOSE) {
            badClose();
        }
    }

    U128 readU128();
    FloatValue readFloatValue();
    std::string readString();
    CloseOnDrop openObject(const char* name);
    CloseOnDrop openAnonObject();
    i64 readI64();
    S128 readI128();

    static bool isMetadata(const std::string& path);
    static HIRSerialiseReader* create(stl::ObjPool& pool, const std::string& path);
    static RcString readFirstString(stl::StringView path);

    static constexpr u8 TAG_OPEN_NAMED = 0xFD;
    static constexpr u8 TAG_OPEN_ANON = 0xFE;
    static constexpr u8 TAG_CLOSE = 0xFF;

protected:
    template <typename T>
    T readRaw() {
        need(sizeof(T));
        T v;
        memcpy(&v, cur, sizeof(T));
        cur += sizeof(T);
        return v;
    }

    void need(size_t count) const {
        if (static_cast<size_t>(end - cur) < count) [[unlikely]] {
            overrun(count);
        }
    }

    [[noreturn]] void overrun(size_t count) const;
    [[noreturn]] void badBool(u8 v) const;
    [[noreturn]] void badClose() const;
};
