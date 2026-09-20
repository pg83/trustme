#include "hir_serialise_lowlevel.h"

#include "common.h"
#include "output_file.h"

#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>

#define ZLIB_CONST
#include <zlib.h>
#include <fstream>
#include <map>
#include <vector>
#include <string.h>
#include <algorithm>

using namespace stl;

struct WriterInner;
struct ReaderInner;

namespace {
    struct WriterImpl final: public HIRSerialiseWriter {
        WriterInner* inner;
        std::map<RcString, unsigned> istringCache;
        std::map<const char*, unsigned> objnameCache;
        ObjPool& pool;

        explicit WriterImpl(ObjPool& pool);

        void open(const std::string& filename) override;
        void write(const void* data, size_t count) override;
        void writeU16(u16 v) override;
        void writeU32(u32 v) override;
        void writeU64(u64 v) override;
        void writeU64c(u64 v) override;
        void writeI64c(i64 v) override;
        void writeU128(U128 v) override;
        void writeDouble(double v) override;
        void writeFloatValue(FloatValue value) override;
        void writeTag(unsigned int t) override;
        void writeCount(size_t c) override;
        void writeString(const RcString& v) override;
        void writeString(size_t len, const char* s) override;
        void writeBool(bool v) override;
        void rawWriteUint(u64 val) override;
        void rawWriteLen(size_t len) override;
        void rawWriteBytes(size_t len, const void* data) override;
        CloseOnDrop openObject(const char* name) override;
        CloseOnDrop openAnonObject() override;
    };

    struct ReaderBuffer {
        Vector<u8> backing;
        unsigned int ofs;

        explicit ReaderBuffer(size_t size);

        size_t capacity() const {
            return backing.capacity();
        }

        size_t read(void* dst, size_t len);
        void populate(ReaderInner& is);
    };

    struct ReaderImpl final: public HIRSerialiseReader {
        ReaderInner* inner;
        ReaderBuffer buffer;
        size_t pos;
        Vector<RcString> strings;
        std::vector<std::string> objnameCache;

        ReaderImpl(ObjPool& pool, const std::string& path);

        size_t getPos() const override;
        void read(void* dst, size_t count) override;
        u8 readU8() override;
        u16 readU16() override;
        u32 readU32() override;
        u64 readU64() override;
        U128 readU128() override;
        u64 readU64c() override;
        i64 readI64c() override;
        double readDouble() override;
        FloatValue readFloatValue() override;
        size_t readCount() override;
        RcString readIstring() override;
        std::string readString() override;
        bool readBool() override;
        u64 rawReadUint() override;
        size_t rawReadLen() override;
        std::string rawReadBytesStdstring() override;
        CloseOnDrop openObject(const char* name) override;
        CloseOnDrop openAnonObject() override;
        void closeObject() override;
    };
}

struct WriterInner {
    ZeroCopyOutput* backing;
    z_stream zstream;
    Vector<unsigned char> buffer;

    unsigned int byteOutCount = 0;
    unsigned int byteInCount = 0;

    WriterInner(ObjPool& pool, const std::string& filename);
    ~WriterInner();
    void write(const void* buf, size_t len);
};

struct ReaderInner {
    std::ifstream backing;
    z_stream zstream;
    Vector<unsigned char> buffer;

    unsigned int byteOutCount = 0;
    unsigned int byteInCount = 0;

    ReaderInner(const std::string& filename);
    ~ReaderInner();
    size_t read(void* buf, size_t len);
};

WriterImpl::WriterImpl(ObjPool& pool)
    : inner(nullptr)
    , pool(pool)
{
}

void WriterImpl::open(const std::string& filename) {
    std::vector<std::pair<RcString, unsigned>> sorted;
    sorted.reserve(istringCache.size());
    for (const auto& e : istringCache) {
        sorted.push_back(e);
    }
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });

    objnameCache.clear();

    inner = pool.make<WriterInner>(pool, filename);
    this->writeCount(sorted.size());
    for (size_t i = 0; i < sorted.size(); i++) {
        const auto& s = sorted[i].first;
        this->writeString(s.size(), s.c_str());
        DEBUG(i << StringView(" = ") << istringCache[s] << StringView(" '") << s << StringView("'"));
        istringCache[s] = i;
    }
    for (const auto& e : istringCache) {
        BUG_ASSERT(e.second < sorted.size());
    }
}

void WriterImpl::write(const void* buf, size_t len) {
    if (inner) {
        DEBUG(StringView("write(") << FMT_CB(ss, for (size_t i = 0; i < len; i++) ss << formatHex(unsigned(((const u8*)buf)[i]), 2)) << StringView(")"));
        inner->write(buf, len);
    } else {
    }
}

void WriterImpl::writeString(const RcString& v) {
    if (inner) {
        this->writeCount(istringCache.at(v));
    } else {
        istringCache.insert(std::make_pair(v, 0)).first->second += 1;
    }
}

WriterInner::WriterInner(ObjPool& pool, const std::string& filename)
    : backing(outputFile(pool, filename.c_str()))
    , zstream()
    , buffer()
{
    buffer.zero(16 * 1024);
    zstream.zalloc = Z_NULL;
    zstream.zfree = Z_NULL;
    zstream.opaque = Z_NULL;

    const int COMPRESSION_LEVEL = Z_BEST_COMPRESSION;
    int ret = deflateInit(&zstream, COMPRESSION_LEVEL);
    if (ret != Z_OK) {
        throw std::runtime_error("zlib init failure");
    }

    zstream.avail_out = buffer.length();
    zstream.next_out = buffer.mutData();
}

WriterInner::~WriterInner() {
    BUG_ASSERT(zstream.avail_in == 0);

    int ret;
    do {
        ret = deflate(&zstream, Z_FINISH);
        if (ret == Z_STREAM_ERROR) {
            sysE << StringView("ERROR: zlib deflate stream error (cleanup)");
            abort();
        }
        if (zstream.avail_out != buffer.length()) {
            size_t rem = buffer.length() - zstream.avail_out;
            byteOutCount += rem;
            backing->write(buffer.data(), rem);

            zstream.avail_out = buffer.length();
            zstream.next_out = buffer.mutData();
        }
    } while (ret == Z_OK);
    deflateEnd(&zstream);
    backing->finish();
}

void WriterInner::write(const void* buf, size_t len) {
    zstream.avail_in = len;
    zstream.next_in = static_cast<const unsigned char*>(buf);

    size_t lastAvailIn = zstream.avail_in;

    while (zstream.avail_in > 0) {
        BUG_ASSERT(zstream.avail_out != 0);

        int ret = deflate(&zstream, Z_NO_FLUSH);
        if (ret == Z_STREAM_ERROR) {
            throw std::runtime_error("zlib deflate stream error");
        }

        size_t usedThisTime = lastAvailIn - zstream.avail_in;
        lastAvailIn = zstream.avail_in;
        byteInCount += usedThisTime;

        if (zstream.avail_in > 0) {
            size_t bytes = buffer.length() - zstream.avail_out;
            backing->write(buffer.data(), bytes);
            byteOutCount += bytes;

            zstream.avail_out = buffer.length();
            zstream.next_out = buffer.mutData();
        }
    }

    while (zstream.avail_out == 0) {
        size_t bytes = buffer.length() - zstream.avail_out;
        backing->write(buffer.data(), bytes);
        byteOutCount += bytes;

        zstream.avail_out = buffer.length();
        zstream.next_out = buffer.mutData();

        int ret = deflate(&zstream, Z_NO_FLUSH);
        if (ret == Z_STREAM_ERROR) {
            throw std::runtime_error("zlib deflate stream error");
        }
    }
}

ReaderBuffer::ReaderBuffer(size_t cap)
    : backing(cap)
    , ofs(0)
{
}

size_t ReaderBuffer::read(void* dst, size_t len) {
    size_t rem = backing.length() - ofs;
    if (rem >= len) {
        memcpy(dst, backing.data() + ofs, len);
        ofs += len;
        return len;
    } else {
        memcpy(dst, backing.data() + ofs, rem);
        ofs = backing.length();
        return rem;
    }
}

void ReaderBuffer::populate(ReaderInner& is) {
    backing.zero(backing.capacity());
    auto len = is.read(backing.mutData(), backing.length());
    while (backing.length() > len) {
        backing.popBack();
    }
    ofs = 0;
}

ReaderImpl::ReaderImpl(ObjPool& pool, const std::string& filename)
    : inner(pool.make<ReaderInner>(filename))
    , buffer(1024)
    , pos(0)
{
    size_t nStrings = readCount();
    strings.grow(nStrings);
    DEBUG(StringView("n_strings = ") << nStrings);
    for (size_t i = 0; i < nStrings; i++) {
        auto s = readString();
        strings.pushBack(RcString::newInterned(s));
    }
}

size_t ReaderImpl::getPos() const {
    return pos;
}

void ReaderImpl::read(void* buf, size_t len) {
    auto used = buffer.read(buf, len);
    if (used == len) {
        pos += len;
        return;
    }
    buf = reinterpret_cast<u8*>(buf) + used;
    len -= used;

    if (len >= buffer.capacity()) {
        inner->read(buf, len);
    } else {
        buffer.populate(*inner);
        used = buffer.read(buf, len);
        if (used != len) {
            throw std::runtime_error(FMT(StringView("Reader::read - Requested ") << len << StringView(" bytes from buffer, got ") << used));
        }
    }

    pos += len;
}

ReaderInner::ReaderInner(const std::string& filename)
    : backing(filename, std::ios_base::in | std::ios_base::binary)
    , zstream()
    , buffer()
{
    buffer.zero(16 * 1024);
    if (!backing.is_open()) {
        throw std::runtime_error("Unable to open " + filename);
    }

    zstream.zalloc = Z_NULL;
    zstream.zfree = Z_NULL;
    zstream.opaque = Z_NULL;

    int ret = inflateInit(&zstream);
    if (ret != Z_OK) {
        throw std::runtime_error("zlib init failure");
    }

    zstream.avail_in = 0;
}

ReaderInner::~ReaderInner() {
    inflateEnd(&zstream);
}

size_t ReaderInner::read(void* buf, size_t len) {
    zstream.avail_out = len;
    zstream.next_out = reinterpret_cast<unsigned char*>(buf);
    do {
        if (zstream.avail_in == 0) {
            backing.read(reinterpret_cast<char*>(buffer.mutData()), buffer.length());
            zstream.avail_in = backing.gcount();
            if (zstream.avail_in == 0) {
                byteOutCount += len - zstream.avail_out;
                return len - zstream.avail_out;
            }
            zstream.next_in = buffer.mutData();

            byteInCount += zstream.avail_in;
        }

        int ret = inflate(&zstream, Z_NO_FLUSH);
        if (ret == Z_STREAM_ERROR) {
            throw std::runtime_error("zlib inflate stream error");
        }
        switch (ret) {
            case Z_NEED_DICT:
                ret = Z_DATA_ERROR;
            case Z_DATA_ERROR:
            case Z_MEM_ERROR:
                throw std::runtime_error("zlib inflate error");
            default:
                break;
        }
    } while (zstream.avail_out > 0);
    byteOutCount += len;

    return len;
}

void WriterImpl::writeU16(u16 v) {
    u8 buf[] = {static_cast<u8>(v & 0xFF), static_cast<u8>(v >> 8)};
    this->write(buf, 2);
}

void WriterImpl::writeU32(u32 v) {
    u8 buf[] = {static_cast<u8>(v & 0xFF), static_cast<u8>(v >> 8), static_cast<u8>(v >> 16), static_cast<u8>(v >> 24)};
    this->write(buf, 4);
}

void WriterImpl::writeU64(u64 v) {
    u8 buf[] = {static_cast<u8>(v & 0xFF), static_cast<u8>(v >> 8), static_cast<u8>(v >> 16), static_cast<u8>(v >> 24), static_cast<u8>(v >> 32), static_cast<u8>(v >> 40), static_cast<u8>(v >> 48), static_cast<u8>(v >> 56)};
    this->write(buf, 8);
}

void WriterImpl::writeU64c(u64 v) {
    if (v < (1 << 7)) {
        writeU8(static_cast<u8>(v));
    } else if (v < (1 << (6 + 16))) {
        u8 buf[] = {static_cast<u8>(0x80 + (v >> 16)), static_cast<u8>(v >> 8), static_cast<u8>(v & 0xFF)};
        this->write(buf, sizeof buf);
    } else if (v < (1ull << (5 + 32))) {
        u8 buf[] = {static_cast<u8>(0xC0 + (v >> 32)), static_cast<u8>(v >> 24), static_cast<u8>(v >> 16), static_cast<u8>(v >> 8), static_cast<u8>(v)};
        this->write(buf, sizeof buf);
    } else {
        u8 buf[] = {0xFF, static_cast<u8>(v & 0xFF), static_cast<u8>(v >> 8), static_cast<u8>(v >> 16), static_cast<u8>(v >> 24), static_cast<u8>(v >> 32), static_cast<u8>(v >> 40), static_cast<u8>(v >> 48), static_cast<u8>(v >> 56)};
        this->write(buf, sizeof buf);
    }
}

void WriterImpl::writeI64c(i64 v) {
    bool sign = (v < 0);
    u64 va = (v < 0 ? -v : v);
    va <<= 1;
    va |= (sign ? 1 : 0);
    writeU64c(va);
}

void WriterImpl::writeU128(U128 v) {
    writeU64(v.getLo());
    writeU64(v.getHi());
}

void WriterImpl::writeDouble(double v) {
    this->write(&v, sizeof v);
}

void WriterImpl::writeFloatValue(FloatValue value) {
    auto encoded = F128(value);
    writeU64(encoded.lo);
    writeU64(encoded.hi);
}

void WriterImpl::writeTag(unsigned int t) {
    BUG_ASSERT(t < 256);
    writeU8(static_cast<u8>(t));
}

void WriterImpl::writeCount(size_t c) {
    DEBUG(c);
    if (c < 0xFD) {
        writeU8(static_cast<u8>(c));
    } else if (c == ~0u) {
        writeU8(0xFF);
    } else if (c < (1u << 16)) {
        writeU8(0xFD);
        writeU16(static_cast<u16>(c));
    } else {
        BUG_ASSERT(c < (1u << 31));
        writeU8(0xFE);
        writeU32(static_cast<u32>(c));
    }
}

void WriterImpl::writeString(size_t len, const char* s) {
    TRACE_FUNCTION;
    if (len < 128) {
        writeU8(static_cast<u8>(len));
    } else {
        BUG_ASSERT(len < (1u << (16 + 7)));
        writeU8(static_cast<u8>(128 + (len >> 16)));
        writeU16(static_cast<u16>(len & 0xFFFF));
    }
    this->write(s, len);
}

void WriterImpl::writeBool(bool v) {
    TRACE_FUNCTION_F(v);
    writeU8(v ? 0xFF : 0x00);
}

void WriterImpl::rawWriteUint(u64 val) {
    if (val < 0xC0) {
        writeU8(static_cast<u8>(val));
    } else {
        u8 bytes[8];
        u8 len = 0;
        while (val > 0) {
            BUG_ASSERT(len < 8);
            bytes[len] = static_cast<u8>(val);
            val >>= 8;
            len += 1;
        }
        writeU8(0xC0 + len);
        this->write(bytes, len);
    }
}

void WriterImpl::rawWriteLen(size_t len) {
    if (len < (0xFC - 0xC0)) {
        writeU8(0xC0 + len);
    } else {
        writeU8(0xFC);
        rawWriteUint(len);
    }
}

void WriterImpl::rawWriteBytes(size_t len, const void* data) {
    rawWriteLen(len);
    this->write(data, len);
}

HIRSerialiseWriter::CloseOnDrop::CloseOnDrop(HIRSerialiseWriter& r)
    : r(&r)
{
}

HIRSerialiseWriter::CloseOnDrop::CloseOnDrop(CloseOnDrop&& x)
    : r(x.r)
{
    x.r = nullptr;
}

HIRSerialiseWriter::CloseOnDrop::~CloseOnDrop() {
    if (r) {
        r->closeObject();
    }
    r = nullptr;
}

HIRSerialiseWriter::CloseOnDrop WriterImpl::openObject(const char* name) {
    writeU8(0xFD);
    auto iv = objnameCache.insert(std::make_pair(name, static_cast<unsigned>(objnameCache.size())));
    rawWriteUint(iv.first->second);
    if (iv.second) {
        rawWriteBytes(strlen(name), name);
    }
    return CloseOnDrop(*this);
}

HIRSerialiseWriter::CloseOnDrop WriterImpl::openAnonObject() {
    writeU8(0xFE);
    return CloseOnDrop(*this);
}

u8 ReaderImpl::readU8() {
    u8 v;
    read(&v, sizeof v);
    return v;
}

u16 ReaderImpl::readU16() {
    u8 buf[2];
    read(buf, sizeof buf);
    return static_cast<u16>(buf[0]) | (static_cast<u16>(buf[1]) << 8);
}

u32 ReaderImpl::readU32() {
    u8 buf[4];
    read(buf, sizeof buf);
    return static_cast<u32>(buf[0]) | (static_cast<u32>(buf[1]) << 8) | (static_cast<u32>(buf[2]) << 16) | (static_cast<u32>(buf[3]) << 24);
}

u64 ReaderImpl::readU64() {
    u8 buf[8];
    read(buf, sizeof buf);
    return static_cast<u64>(buf[0]) | (static_cast<u64>(buf[1]) << 8) | (static_cast<u64>(buf[2]) << 16) | (static_cast<u64>(buf[3]) << 24) | (static_cast<u64>(buf[4]) << 32) | (static_cast<u64>(buf[5]) << 40) | (static_cast<u64>(buf[6]) << 48) | (static_cast<u64>(buf[7]) << 56);
}

U128 ReaderImpl::readU128() {
    auto lo = readU64();
    auto hi = readU64();
    return U128(lo, hi);
}

u64 ReaderImpl::readU64c() {
    auto v = readU8();
    if (v < (1 << 7)) {
        return static_cast<u64>(v);
    } else if (v < 0xC0) {
        u64 rv = static_cast<u64>(v & 0x3F) << 16;
        rv |= static_cast<u64>(readU8()) << 8;
        rv |= static_cast<u64>(readU8());
        return rv;
    } else if (v < 0xFF) {
        u64 rv = static_cast<u64>(v & 0x3F) << 32;
        rv |= static_cast<u64>(readU8()) << 24;
        rv |= static_cast<u64>(readU8()) << 16;
        rv |= static_cast<u64>(readU8()) << 8;
        rv |= static_cast<u64>(readU8());
        return rv;
    } else {
        return readU64();
    }
}

i64 ReaderImpl::readI64c() {
    u64 va = readU64c();
    bool sign = (va & 0x1) != 0;
    va >>= 1;

    if (va == 0 && sign) {
        return INT64_MIN;
    } else if (sign) {
        return -static_cast<i64>(va);
    } else {
        return static_cast<i64>(va);
    }
}

double ReaderImpl::readDouble() {
    double v;
    read(reinterpret_cast<char*>(&v), sizeof v);
    return v;
}

FloatValue ReaderImpl::readFloatValue() {
    F128 encoded;
    encoded.lo = readU64();
    encoded.hi = readU64();
    return encoded;
}

size_t ReaderImpl::readCount() {
    size_t rv;
    auto v = readU8();
    if (v < 0xFD) {
        rv = v;
    } else if (v == 0xFD) {
        rv = readU16();
    } else if (v == 0xFE) {
        rv = readU32();
    } else /*if( v == 0xFF )*/ {
        rv = ~0u;
    }
    DEBUG(rv);
    return rv;
}

RcString ReaderImpl::readIstring() {
    size_t idx = readCount();
    return strings[idx];
}

std::string ReaderImpl::readString() {
    size_t len = readU8();
    if (len < 128) {
    } else {
        len = (len & 0x7F) << 16;
        len |= readU16();
    }
    std::string rv(len, '\0');
    read(rv.data(), len);
    return rv;
}

bool ReaderImpl::readBool() {
    auto v = readU8();
    switch (v) {
        case 0:
            return false;
        case 255:
            return true;
        default:
            sysE << StringView("Expected false(0)/true(255), got ") << unsigned(v) << StringView("u8") << endL;
            abort();
    }
}

u64 ReaderImpl::rawReadUint() {
    auto v = readU8();
    BUG_ASSERT(v <= 0xC0 + 8);
    if (v < 0xC0) {
        return v;
    } else {
        size_t len = v - 0xC0;
        u64 rv = 0;
        for (size_t p = 0; p < len; p++) {
            rv |= static_cast<u64>(readU8()) << (8 * p);
        }
        return rv;
    }
}

size_t ReaderImpl::rawReadLen() {
    auto v = readU8();
    if (v < 0xC0) {
        sysE << StringView("Expected length, got literal integer ") << unsigned(v) << endL;
        abort();
    } else if (v < 0xFC) {
        return v - 0xC0;
    } else if (v == 0xFC) {
        return rawReadUint();
    } else {
        sysE << StringView("Expected length, got tag ") << unsigned(v) << endL;
        abort();
    }
}

std::string ReaderImpl::rawReadBytesStdstring() {
    auto len = rawReadLen();
    std::string rv(len, '\0');
    read(rv.data(), len);
    return rv;
}

HIRSerialiseReader::CloseOnDrop::CloseOnDrop(HIRSerialiseReader& r)
    : r(&r)
{
}

HIRSerialiseReader::CloseOnDrop::CloseOnDrop(CloseOnDrop&& x)
    : r(x.r)
{
    x.r = nullptr;
}

HIRSerialiseReader::CloseOnDrop::~CloseOnDrop() {
    if (r) {
        r->closeObject();
    }
    r = nullptr;
}

HIRSerialiseReader::CloseOnDrop ReaderImpl::openObject(const char* name) {
    auto v = readU8();
    if (v != 0xFD) {
        sysE << StringView("Expected OpenNamed(") << name << StringView("), got ") << unsigned(v) << StringView("u8") << endL;
        abort();
    }
    auto key = rawReadUint();
    if (key == objnameCache.size()) {
        objnameCache.push_back(rawReadBytesStdstring());
    }
    BUG_ASSERT(key < objnameCache.size());
    if (objnameCache[key] != name) {
        sysE << StringView("Expecting OpenNamed(") << name << StringView("), got OpenNamed(") << objnameCache[key] << StringView(")") << endL;
        abort();
    }
    return CloseOnDrop(*this);
}

HIRSerialiseReader::CloseOnDrop ReaderImpl::openAnonObject() {
    auto v = readU8();
    if (v != 0xFE) {
        sysE << StringView("Expected OpenAnon, got ") << unsigned(v) << endL;
        abort();
    }
    return CloseOnDrop(*this);
}

void ReaderImpl::closeObject() {
    auto v = readU8();
    if (v != 0xFF) {
        sysE << StringView("Expected CloseObject(0xFF), got ") << unsigned(v) << endL;
        abort();
    }
}

void HIRSerialiseWriter::writeU8(u8 v) {
    write(reinterpret_cast<const char*>(&v), 1);
}

void HIRSerialiseWriter::writeI64(i64 v) {
    writeU64(static_cast<u64>(v));
}

void HIRSerialiseWriter::writeI128(S128 v) {
    writeU128(v.getInner());
}

void HIRSerialiseWriter::writeString(const std::string& v) {
    writeString(v.size(), v.c_str());
}

void HIRSerialiseWriter::closeObject() {
    writeU8(0xFF);
}

HIRSerialiseWriter* HIRSerialiseWriter::create(ObjPool& pool) {
    return pool.make<WriterImpl>(pool);
}

i64 HIRSerialiseReader::readI64() {
    return static_cast<i64>(readU64());
}

S128 HIRSerialiseReader::readI128() {
    return S128(readU128());
}

unsigned int HIRSerialiseReader::readTag() {
    return static_cast<unsigned int>(readU8());
}

HIRSerialiseReader* HIRSerialiseReader::create(ObjPool& pool, const std::string& path) {
    return pool.make<ReaderImpl>(pool, path);
}
