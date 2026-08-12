#include "hir_serialise_lowlevel.h"

#include "debug.h"
#include "common.h"

#include <zlib.h>
#include <fstream>
#include <iomanip>
#include <string.h> // memcpy
#include <algorithm>


    class HIRSerialiseWriterInner {
        ::std::ofstream backing;
        z_stream zstream;
        ::std::vector<unsigned char> buffer;

        unsigned int byteOutCount = 0;
        unsigned int byteInCount = 0;

    public:
        HIRSerialiseWriterInner(const ::std::string& filename);
        ~HIRSerialiseWriterInner();
        void write(const void* buf, size_t len);
    };

    HIRSerialiseWriter::HIRSerialiseWriter()
        : inner(nullptr)
    {
    }

    HIRSerialiseWriter::~HIRSerialiseWriter() {
        delete inner, inner = nullptr;
    }

    void HIRSerialiseWriter::open(const ::std::string& filename) {
        // 1. Sort strings by frequency
        ::std::vector<::std::pair<RcString, unsigned>> sorted;
        sorted.reserve(istringCache.size());
        for (const auto& e : istringCache) {
            sorted.push_back(e);
        }
        // 2. Write out string table
        ::std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
            return a.second > b.second;
        });

        objnameCache.clear();

        inner = new HIRSerialiseWriterInner(filename);
        // 3. Reset m_istring_cache to use the same value
        this->writeCount(sorted.size());
        for (size_t i = 0; i < sorted.size(); i++) {
            const auto& s = sorted[i].first;
            this->writeString(s.size(), s.c_str());
            DEBUG(i << " = " << istringCache[s] << " '" << s << "'");
            istringCache[s] = i;
        }
        for (const auto& e : istringCache) {
            assert(e.second < sorted.size());
        }
    }

    void HIRSerialiseWriter::write(const void* buf, size_t len) {
        if (inner) {
            DEBUG("write(" << FMT_CB(ss, for (size_t i = 0; i < len; i++) ss << std::setw(2) << std::setfill('0') << std::hex << unsigned(((const uint8_t*)buf)[i])) << ")");
            inner->write(buf, len);
        } else {
            // No-op, pre caching
        }
    }

    void HIRSerialiseWriter::writeString(const RcString& v) {
        if (inner) {
            // Emit ID from the cache
            this->writeCount(istringCache.at(v));
        } else {
            // Find/add in cache
            istringCache.insert(::std::make_pair(v, 0)).first->second += 1;
        }
    }

    HIRSerialiseWriterInner::HIRSerialiseWriterInner(const ::std::string& filename)
        : backing(filename, ::std::ios_base::out | ::std::ios_base::binary)
        , zstream()
        , buffer(16 * 1024)
    //m_buffer( 4*1024 )
    {
        zstream.zalloc = Z_NULL;
        zstream.zfree = Z_NULL;
        zstream.opaque = Z_NULL;

        const int COMPRESSION_LEVEL = Z_BEST_COMPRESSION;
        int ret = deflateInit(&zstream, COMPRESSION_LEVEL);
        if (ret != Z_OK) {
            throw ::std::runtime_error("zlib init failure");
        }

        zstream.avail_out = buffer.size();
        zstream.next_out = buffer.data();
    }

    HIRSerialiseWriterInner::~HIRSerialiseWriterInner()
    {
        assert(zstream.avail_in == 0);

        // Complete the compression
        int ret;
        do {
            ret = deflate(&zstream, Z_FINISH);
            if (ret == Z_STREAM_ERROR) {
                ::std::cerr << "ERROR: zlib deflate stream error (cleanup)";
                abort();
            }
            if (zstream.avail_out != buffer.size()) {
                size_t rem = buffer.size() - zstream.avail_out;
                byteOutCount += rem;
                backing.write(reinterpret_cast<char*>(buffer.data()), rem);

                zstream.avail_out = buffer.size();
                zstream.next_out = buffer.data();
            }
        } while (ret == Z_OK);
        deflateEnd(&zstream);
    }

    void HIRSerialiseWriterInner::write(const void* buf, size_t len) {
        zstream.avail_in = len;
        zstream.next_in = reinterpret_cast<unsigned char*>(const_cast<void*>(buf));

        size_t lastAvailIn = zstream.avail_in;

        // While there's data to compress
        while (zstream.avail_in > 0) {
            assert(zstream.avail_out != 0);

            // Compress the data
            int ret = deflate(&zstream, Z_NO_FLUSH);
            if (ret == Z_STREAM_ERROR) {
                throw ::std::runtime_error("zlib deflate stream error");
            }

            size_t usedThisTime = lastAvailIn - zstream.avail_in;
            lastAvailIn = zstream.avail_in;
            byteInCount += usedThisTime;

            // If the entire input wasn't consumed, then it was likely due to a lack of output space
            // - Flush the output buffer to the file
            if (zstream.avail_in > 0) {
                size_t bytes = buffer.size() - zstream.avail_out;
                backing.write(reinterpret_cast<char*>(buffer.data()), bytes);
                byteOutCount += bytes;

                zstream.avail_out = buffer.size();
                zstream.next_out = buffer.data();
            }
        }

        // Flush stream contents if the output buffer is full.
        while (zstream.avail_out == 0) {
            size_t bytes = buffer.size() - zstream.avail_out;
            backing.write(reinterpret_cast<char*>(buffer.data()), bytes);
            byteOutCount += bytes;

            zstream.avail_out = buffer.size();
            zstream.next_out = buffer.data();

            int ret = deflate(&zstream, Z_NO_FLUSH);
            if (ret == Z_STREAM_ERROR) {
                throw ::std::runtime_error("zlib deflate stream error");
            }
        }
    }

    // --------------------------------------------------------------------
    class HIRSerialiseReaderInner {
        ::std::ifstream backing;
        z_stream zstream;
        ::std::vector<unsigned char> buffer;

        unsigned int byteOutCount = 0;
        unsigned int byteInCount = 0;

    public:
        HIRSerialiseReaderInner(const ::std::string& filename);
        ~HIRSerialiseReaderInner();
        size_t read(void* buf, size_t len);
    };

    HIRSerialiseReadBuffer::HIRSerialiseReadBuffer(size_t cap)
        : ofs(0)
    {
        backing.reserve(cap);
    }

    size_t HIRSerialiseReadBuffer::read(void* dst, size_t len) {
        size_t rem = backing.size() - ofs;
        if (rem >= len) {
            memcpy(dst, backing.data() + ofs, len);
            ofs += len;
            return len;
        } else {
            memcpy(dst, backing.data() + ofs, rem);
            ofs = backing.size();
            return rem;
        }
    }

    void HIRSerialiseReadBuffer::populate(HIRSerialiseReaderInner& is) {
        backing.resize(backing.capacity(), 0);
        auto len = is.read(backing.data(), backing.size());
        backing.resize(len);
        ofs = 0;
    }

    HIRSerialiseReader::HIRSerialiseReader(const ::std::string& filename)
        : inner(new HIRSerialiseReaderInner(filename))
        , buffer(1024)
        , pos(0)
    {
        size_t nStrings = readCount();
        strings.reserve(nStrings);
        DEBUG("n_strings = " << nStrings);
        for (size_t i = 0; i < nStrings; i++) {
            auto s = readString();
            strings.push_back(RcString::newInterned(s));
        }
    }

    HIRSerialiseReader::~HIRSerialiseReader() {
        delete inner, inner = nullptr;
    }

    void HIRSerialiseReader::read(void* buf, size_t len) {
        auto used = buffer.read(buf, len);
        if (used == len) {
            pos += len;
            return;
        }
        buf = reinterpret_cast<uint8_t*>(buf) + used;
        len -= used;

        if (len >= buffer.capacity()) {
            inner->read(buf, len);
        } else {
            buffer.populate(*inner);
            used = buffer.read(buf, len);
            if (used != len) {
                throw ::std::runtime_error(FMT("Reader::read - Requested " << len << " bytes from buffer, got " << used));
            }
        }

        pos += len;
    }

    HIRSerialiseReaderInner::HIRSerialiseReaderInner(const ::std::string& filename)
        : backing(filename, ::std::ios_base::in | ::std::ios_base::binary)
        , zstream()
        , buffer(16 * 1024)
    {
        if (!backing.is_open()) {
            throw ::std::runtime_error("Unable to open file");
        }

        zstream.zalloc = Z_NULL;
        zstream.zfree = Z_NULL;
        zstream.opaque = Z_NULL;

        int ret = inflateInit(&zstream);
        if (ret != Z_OK) {
            throw ::std::runtime_error("zlib init failure");
        }

        zstream.avail_in = 0;
    }

    HIRSerialiseReaderInner::~HIRSerialiseReaderInner() {
        inflateEnd(&zstream);
    }

    size_t HIRSerialiseReaderInner::read(void* buf, size_t len) {
        zstream.avail_out = len;
        zstream.next_out = reinterpret_cast<unsigned char*>(buf);
        do {
            // Reset input buffer if empty
            if (zstream.avail_in == 0) {
                backing.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
                zstream.avail_in = backing.gcount();
                if (zstream.avail_in == 0) {
                    byteOutCount += len - zstream.avail_out;
                    //::std::cerr << "Out of bytes, " << m_zstream.avail_out << " needed" << ::std::endl;
                    return len - zstream.avail_out;
                }
                zstream.next_in = const_cast<unsigned char*>(buffer.data());

                byteInCount += zstream.avail_in;
            }

            int ret = inflate(&zstream, Z_NO_FLUSH);
            if (ret == Z_STREAM_ERROR) {
                throw ::std::runtime_error("zlib inflate stream error");
            }
            switch (ret) {
                case Z_NEED_DICT:
                    ret = Z_DATA_ERROR;
                case Z_DATA_ERROR:
                case Z_MEM_ERROR:
                    throw ::std::runtime_error("zlib inflate error");
                default:
                    break;
            }

        } while (zstream.avail_out > 0);
        byteOutCount += len;

        return len;
    }


void HIRSerialiseWriter::writeU16(uint16_t v) {
    uint8_t buf[] = {static_cast<uint8_t>(v & 0xFF), static_cast<uint8_t>(v >> 8)};
    this->write(buf, 2);
}

void HIRSerialiseWriter::writeU32(uint32_t v) {
    uint8_t buf[] = {static_cast<uint8_t>(v & 0xFF), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 24)};
    this->write(buf, 4);
}

void HIRSerialiseWriter::writeU64(uint64_t v) {
    uint8_t buf[] = {static_cast<uint8_t>(v & 0xFF), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 24), static_cast<uint8_t>(v >> 32), static_cast<uint8_t>(v >> 40), static_cast<uint8_t>(v >> 48), static_cast<uint8_t>(v >> 56)};
    this->write(buf, 8);
}

// Variable-length encoded u64 (for array sizes)
void HIRSerialiseWriter::writeU64c(uint64_t v) {
    if (v < (1 << 7)) {
        writeU8(static_cast<uint8_t>(v));
    } else if (v < (1 << (6 + 16))) {
        uint8_t buf[] = {
            static_cast<uint8_t>(0x80 + (v >> 16)), // 0x80 -- 0xBF
            static_cast<uint8_t>(v >> 8),
            static_cast<uint8_t>(v & 0xFF)
        };
        this->write(buf, sizeof buf);
    } else if (v < (1ull << (5 + 32))) {
        uint8_t buf[] = {
            static_cast<uint8_t>(0xC0 + (v >> 32)), // 0xC0 -- 0xDF
            static_cast<uint8_t>(v >> 24),
            static_cast<uint8_t>(v >> 16),
            static_cast<uint8_t>(v >> 8),
            static_cast<uint8_t>(v)
        };
        this->write(buf, sizeof buf);
    } else {
        uint8_t buf[] = {0xFF, static_cast<uint8_t>(v & 0xFF), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 24), static_cast<uint8_t>(v >> 32), static_cast<uint8_t>(v >> 40), static_cast<uint8_t>(v >> 48), static_cast<uint8_t>(v >> 56)};
        this->write(buf, sizeof buf);
    }
}

void HIRSerialiseWriter::writeI64c(int64_t v) {
    // Convert from 2's completement
    bool sign = (v < 0);
    uint64_t va = (v < 0 ? -v : v);
    va <<= 1;
    va |= (sign ? 1 : 0);
    writeU64c(va);
}

void HIRSerialiseWriter::writeU128(U128 v) {
    writeU64(v.getLo());
    writeU64(v.getHi());
}

void HIRSerialiseWriter::writeDouble(double v) {
    // - Just raw-writes the double
    this->write(&v, sizeof v);
}

void HIRSerialiseWriter::writeFloatValue(FloatValue value) {
    auto encoded = F128(value);
    writeU64(encoded.lo);
    writeU64(encoded.hi);
}

void HIRSerialiseWriter::writeTag(unsigned int t) {
    assert(t < 256);
    writeU8(static_cast<uint8_t>(t));
}

void HIRSerialiseWriter::writeCount(size_t c) {
    DEBUG(c);
    if (c < 0xFD) {
        writeU8(static_cast<uint8_t>(c));
    } else if (c == ~0u) {
        writeU8(0xFF);
    } else if (c < (1u << 16)) {
        writeU8(0xFD);
        writeU16(static_cast<uint16_t>(c));
    } else {
        assert(c < (1u << 31));
        writeU8(0xFE);
        writeU32(static_cast<uint32_t>(c));
    }
}

void HIRSerialiseWriter::writeString(size_t len, const char* s) {
    TRACE_FUNCTION;
    if (len < 128) {
        writeU8(static_cast<uint8_t>(len));
    } else {
        assert(len < (1u << (16 + 7)));
        writeU8(static_cast<uint8_t>(128 + (len >> 16)));
        writeU16(static_cast<uint16_t>(len & 0xFFFF));
    }
    this->write(s, len);
}

void HIRSerialiseWriter::writeBool(bool v) {
    TRACE_FUNCTION_F(v);
    writeU8(v ? 0xFF : 0x00);
}

// Core protocol
void HIRSerialiseWriter::rawWriteUint(uint64_t val) {
    if (val < 0xC0) {
        writeU8(static_cast<uint8_t>(val));
    } else {
        uint8_t bytes[8];
        uint8_t len = 0;
        while (val > 0) {
            assert(len < 8);
            bytes[len] = static_cast<uint8_t>(val);
            val >>= 8;
            len += 1;
        }
        writeU8(0xC0 + len);
        this->write(bytes, len);
    }
}

void HIRSerialiseWriter::rawWriteLen(size_t len) {
    if (len < (0xFC - 0xC0)) {
        writeU8(0xC0 + len);
    } else {
        writeU8(0xFC);
        rawWriteUint(len);
    }
}

void HIRSerialiseWriter::rawWriteBytes(size_t len, const void* data) {
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

HIRSerialiseWriter::CloseOnDrop HIRSerialiseWriter::openObject(const char* name) {
    writeU8(0xFD);
    auto iv = objnameCache.insert(std::make_pair(name, static_cast<unsigned>(objnameCache.size())));
    rawWriteUint(iv.first->second);
    if (iv.second) {
        rawWriteBytes(strlen(name), name);
    }
    return CloseOnDrop(*this);
}

HIRSerialiseWriter::CloseOnDrop HIRSerialiseWriter::openAnonObject() {
    writeU8(0xFE);
    return CloseOnDrop(*this);
}

uint8_t HIRSerialiseReader::readU8() {
    uint8_t v;
    read(&v, sizeof v);
    return v;
}

uint16_t HIRSerialiseReader::readU16() {
    uint8_t buf[2];
    read(buf, sizeof buf);
    return static_cast<uint16_t>(buf[0]) | (static_cast<uint16_t>(buf[1]) << 8);
}

uint32_t HIRSerialiseReader::readU32() {
    uint8_t buf[4];
    read(buf, sizeof buf);
    return static_cast<uint32_t>(buf[0]) | (static_cast<uint32_t>(buf[1]) << 8) | (static_cast<uint32_t>(buf[2]) << 16) | (static_cast<uint32_t>(buf[3]) << 24);
}

uint64_t HIRSerialiseReader::readU64() {
    uint8_t buf[8];
    read(buf, sizeof buf);
    return static_cast<uint64_t>(buf[0]) | (static_cast<uint64_t>(buf[1]) << 8) | (static_cast<uint64_t>(buf[2]) << 16) | (static_cast<uint64_t>(buf[3]) << 24) | (static_cast<uint64_t>(buf[4]) << 32) | (static_cast<uint64_t>(buf[5]) << 40) | (static_cast<uint64_t>(buf[6]) << 48) | (static_cast<uint64_t>(buf[7]) << 56);
}

U128 HIRSerialiseReader::readU128() {
    auto lo = readU64();
    auto hi = readU64();
    return U128(lo, hi);
}

// Variable-length encoded u64 (for array sizes)
uint64_t HIRSerialiseReader::readU64c() {
    auto v = readU8();
    if (v < (1 << 7)) {
        return static_cast<uint64_t>(v);
    } else if (v < 0xC0) {
        uint64_t rv = static_cast<uint64_t>(v & 0x3F) << 16;
        rv |= static_cast<uint64_t>(readU8()) << 8;
        rv |= static_cast<uint64_t>(readU8());
        return rv;
    } else if (v < 0xFF) {
        uint64_t rv = static_cast<uint64_t>(v & 0x3F) << 32;
        rv |= static_cast<uint64_t>(readU8()) << 24;
        rv |= static_cast<uint64_t>(readU8()) << 16;
        rv |= static_cast<uint64_t>(readU8()) << 8;
        rv |= static_cast<uint64_t>(readU8());
        return rv;
    } else {
        return readU64();
    }
}

int64_t HIRSerialiseReader::readI64c() {
    uint64_t va = readU64c();
    bool sign = (va & 0x1) != 0;
    va >>= 1;

    if (va == 0 && sign) {
        return INT64_MIN;
    } else if (sign) {
        return -static_cast<int64_t>(va);
    } else {
        return static_cast<int64_t>(va);
    }
}

double HIRSerialiseReader::readDouble() {
    double v;
    read(reinterpret_cast<char*>(&v), sizeof v);
    return v;
}

FloatValue HIRSerialiseReader::readFloatValue() {
    F128 encoded;
    encoded.lo = readU64();
    encoded.hi = readU64();
    return encoded;
}

size_t HIRSerialiseReader::readCount() {
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

RcString HIRSerialiseReader::readIstring() {
    size_t idx = readCount();
    return strings.at(idx);
}

::std::string HIRSerialiseReader::readString() {
    size_t len = readU8();
    if (len < 128) {
    } else {
        len = (len & 0x7F) << 16;
        len |= readU16();
    }
    ::std::string rv(len, '\0');
    read(const_cast<char*>(rv.data()), len);
    return rv;
}

bool HIRSerialiseReader::readBool() {
    auto v = readU8();
    switch (v) {
        case 0:
            return false;
        case 255:
            return true;
        default:
            std::cerr << "Expected false(0)/true(255), got " << unsigned(v) << "u8" << ::std::endl;
            abort();
    }
}

// Core protocol
uint64_t HIRSerialiseReader::rawReadUint() {
    auto v = readU8();
    assert(v <= 0xC0 + 8);
    if (v < 0xC0) {
        return v;
    } else {
        size_t len = v - 0xC0;
        uint64_t rv = 0;
        for (size_t p = 0; p < len; p++) {
            rv |= static_cast<uint64_t>(readU8()) << (8 * p);
        }
        return rv;
    }
}

size_t HIRSerialiseReader::rawReadLen() {
    auto v = readU8();
    if (v < 0xC0) {
        std::cerr << "Expected length, got literal integer " << unsigned(v) << ::std::endl;
        abort();
    } else if (v < 0xFC) {
        return v - 0xC0;
    } else if (v == 0xFC) {
        return rawReadUint();
    } else {
        std::cerr << "Expected length, got tag " << unsigned(v) << ::std::endl;
        abort();
    }
}

std::string HIRSerialiseReader::rawReadBytesStdstring() {
    auto len = rawReadLen();
    std::string rv(len, '\0');
    read(const_cast<char*>(rv.data()), len);
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

HIRSerialiseReader::CloseOnDrop HIRSerialiseReader::openObject(const char* name) {
    auto v = readU8();
    if (v != 0xFD) {
        std::cerr << "Expected OpenNamed(" << name << "), got " << unsigned(v) << "u8" << ::std::endl;
        abort();
    }
    auto key = rawReadUint();
    //std::cout << key << " = " << "..." << std::endl;
    if (key == objnameCache.size()) {
        objnameCache.push_back(rawReadBytesStdstring());
    }
    assert(key < objnameCache.size());
    //std::cout << key << " = " << m_objname_cache[key] << std::endl;
    if (objnameCache[key] != name) {
        std::cerr << "Expecting OpenNamed(" << name << "), got OpenNamed(" << objnameCache[key] << ")" << std::endl;
        abort();
    }
    return CloseOnDrop(*this);
}

HIRSerialiseReader::CloseOnDrop HIRSerialiseReader::openAnonObject() {
    auto v = readU8();
    if (v != 0xFE) {
        std::cerr << "Expected OpenAnon, got " << unsigned(v) << ::std::endl;
        abort();
    }
    return CloseOnDrop(*this);
}

void HIRSerialiseReader::closeObject() {
    auto v = readU8();
    if (v != 0xFF) {
        std::cerr << "Expected CloseObject(0xFF), got " << unsigned(v) << ::std::endl;
        abort();
    }
}
