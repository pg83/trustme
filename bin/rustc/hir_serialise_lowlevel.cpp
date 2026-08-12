#include "hir_serialise_lowlevel.h"
#include "debug.h"
#include <zlib.h>
#include <fstream>
#include <string.h> // memcpy
#include "common.h"
#include <algorithm>
#include <iomanip>

namespace HIR {
    namespace serialise {

        class WriterInner {
            ::std::ofstream m_backing;
            z_stream m_zstream;
            ::std::vector<unsigned char> m_buffer;

            unsigned int m_byte_out_count = 0;
            unsigned int m_byte_in_count = 0;

        public:
            WriterInner(const ::std::string& filename);
            ~WriterInner();
            void write(const void* buf, size_t len);
        };

        Writer::Writer()
            : m_inner(nullptr)
        {
        }

        Writer::~Writer() {
            delete m_inner, m_inner = nullptr;
        }

        void Writer::open(const ::std::string& filename) {
            // 1. Sort strings by frequency
            ::std::vector<::std::pair<RcString, unsigned>> sorted;
            sorted.reserve(m_istring_cache.size());
            for (const auto& e : m_istring_cache) {
                sorted.push_back(e);
            }
            // 2. Write out string table
            ::std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
                return a.second > b.second;
            });

            m_objname_cache.clear();

            m_inner = new WriterInner(filename);
            // 3. Reset m_istring_cache to use the same value
            this->write_count(sorted.size());
            for (size_t i = 0; i < sorted.size(); i++) {
                const auto& s = sorted[i].first;
                this->write_string(s.size(), s.c_str());
                DEBUG(i << " = " << m_istring_cache[s] << " '" << s << "'");
                m_istring_cache[s] = i;
            }
            for (const auto& e : m_istring_cache) {
                assert(e.second < sorted.size());
            }
        }

        void Writer::write(const void* buf, size_t len) {
            if (m_inner) {
                DEBUG("write(" << FMT_CB(ss, for (size_t i = 0; i < len; i++) ss << std::setw(2) << std::setfill('0') << std::hex << unsigned(((const uint8_t*)buf)[i])) << ")");
                m_inner->write(buf, len);
            } else {
                // No-op, pre caching
            }
        }

        void Writer::write_string(const RcString& v) {
            if (m_inner) {
                // Emit ID from the cache
                this->write_count(m_istring_cache.at(v));
            } else {
                // Find/add in cache
                m_istring_cache.insert(::std::make_pair(v, 0)).first->second += 1;
            }
        }

        WriterInner::WriterInner(const ::std::string& filename)
            : m_backing(filename, ::std::ios_base::out | ::std::ios_base::binary)
            , m_zstream()
            , m_buffer(16 * 1024)
        //m_buffer( 4*1024 )
        {
            m_zstream.zalloc = Z_NULL;
            m_zstream.zfree = Z_NULL;
            m_zstream.opaque = Z_NULL;

            const int COMPRESSION_LEVEL = Z_BEST_COMPRESSION;
            int ret = deflateInit(&m_zstream, COMPRESSION_LEVEL);
            if (ret != Z_OK) {
                throw ::std::runtime_error("zlib init failure");
            }

            m_zstream.avail_out = m_buffer.size();
            m_zstream.next_out = m_buffer.data();
        }

        WriterInner::~WriterInner()
        {
            assert(m_zstream.avail_in == 0);

            // Complete the compression
            int ret;
            do {
                ret = deflate(&m_zstream, Z_FINISH);
                if (ret == Z_STREAM_ERROR) {
                    ::std::cerr << "ERROR: zlib deflate stream error (cleanup)";
                    abort();
                }
                if (m_zstream.avail_out != m_buffer.size()) {
                    size_t rem = m_buffer.size() - m_zstream.avail_out;
                    m_byte_out_count += rem;
                    m_backing.write(reinterpret_cast<char*>(m_buffer.data()), rem);

                    m_zstream.avail_out = m_buffer.size();
                    m_zstream.next_out = m_buffer.data();
                }
            } while (ret == Z_OK);
            deflateEnd(&m_zstream);
        }

        void WriterInner::write(const void* buf, size_t len) {
            m_zstream.avail_in = len;
            m_zstream.next_in = reinterpret_cast<unsigned char*>(const_cast<void*>(buf));

            size_t last_avail_in = m_zstream.avail_in;

            // While there's data to compress
            while (m_zstream.avail_in > 0) {
                assert(m_zstream.avail_out != 0);

                // Compress the data
                int ret = deflate(&m_zstream, Z_NO_FLUSH);
                if (ret == Z_STREAM_ERROR) {
                    throw ::std::runtime_error("zlib deflate stream error");
                }

                size_t used_this_time = last_avail_in - m_zstream.avail_in;
                last_avail_in = m_zstream.avail_in;
                m_byte_in_count += used_this_time;

                // If the entire input wasn't consumed, then it was likely due to a lack of output space
                // - Flush the output buffer to the file
                if (m_zstream.avail_in > 0) {
                    size_t bytes = m_buffer.size() - m_zstream.avail_out;
                    m_backing.write(reinterpret_cast<char*>(m_buffer.data()), bytes);
                    m_byte_out_count += bytes;

                    m_zstream.avail_out = m_buffer.size();
                    m_zstream.next_out = m_buffer.data();
                }
            }

            // Flush stream contents if the output buffer is full.
            while (m_zstream.avail_out == 0) {
                size_t bytes = m_buffer.size() - m_zstream.avail_out;
                m_backing.write(reinterpret_cast<char*>(m_buffer.data()), bytes);
                m_byte_out_count += bytes;

                m_zstream.avail_out = m_buffer.size();
                m_zstream.next_out = m_buffer.data();

                int ret = deflate(&m_zstream, Z_NO_FLUSH);
                if (ret == Z_STREAM_ERROR) {
                    throw ::std::runtime_error("zlib deflate stream error");
                }
            }
        }

        // --------------------------------------------------------------------
        class ReaderInner {
            ::std::ifstream m_backing;
            z_stream m_zstream;
            ::std::vector<unsigned char> m_buffer;

            unsigned int m_byte_out_count = 0;
            unsigned int m_byte_in_count = 0;

        public:
            ReaderInner(const ::std::string& filename);
            ~ReaderInner();
            size_t read(void* buf, size_t len);
        };

        ReadBuffer::ReadBuffer(size_t cap)
            : m_ofs(0)
        {
            m_backing.reserve(cap);
        }

        size_t ReadBuffer::read(void* dst, size_t len) {
            size_t rem = m_backing.size() - m_ofs;
            if (rem >= len) {
                memcpy(dst, m_backing.data() + m_ofs, len);
                m_ofs += len;
                return len;
            } else {
                memcpy(dst, m_backing.data() + m_ofs, rem);
                m_ofs = m_backing.size();
                return rem;
            }
        }

        void ReadBuffer::populate(ReaderInner& is) {
            m_backing.resize(m_backing.capacity(), 0);
            auto len = is.read(m_backing.data(), m_backing.size());
            m_backing.resize(len);
            m_ofs = 0;
        }

        Reader::Reader(const ::std::string& filename)
            : m_inner(new ReaderInner(filename))
            , m_buffer(1024)
            , m_pos(0)
        {
            size_t n_strings = read_count();
            m_strings.reserve(n_strings);
            DEBUG("n_strings = " << n_strings);
            for (size_t i = 0; i < n_strings; i++) {
                auto s = read_string();
                m_strings.push_back(RcString::new_interned(s));
            }
        }

        Reader::~Reader() {
            delete m_inner, m_inner = nullptr;
        }

        void Reader::read(void* buf, size_t len) {
            auto used = m_buffer.read(buf, len);
            if (used == len) {
                m_pos += len;
                return;
            }
            buf = reinterpret_cast<uint8_t*>(buf) + used;
            len -= used;

            if (len >= m_buffer.capacity()) {
                m_inner->read(buf, len);
            } else {
                m_buffer.populate(*m_inner);
                used = m_buffer.read(buf, len);
                if (used != len) {
                    throw ::std::runtime_error(FMT("Reader::read - Requested " << len << " bytes from buffer, got " << used));
                }
            }

            m_pos += len;
        }

        ReaderInner::ReaderInner(const ::std::string& filename)
            : m_backing(filename, ::std::ios_base::in | ::std::ios_base::binary)
            , m_zstream()
            , m_buffer(16 * 1024)
        {
            if (!m_backing.is_open()) {
                throw ::std::runtime_error("Unable to open file");
            }

            m_zstream.zalloc = Z_NULL;
            m_zstream.zfree = Z_NULL;
            m_zstream.opaque = Z_NULL;

            int ret = inflateInit(&m_zstream);
            if (ret != Z_OK) {
                throw ::std::runtime_error("zlib init failure");
            }

            m_zstream.avail_in = 0;
        }

        ReaderInner::~ReaderInner() {
            inflateEnd(&m_zstream);
        }

        size_t ReaderInner::read(void* buf, size_t len) {
            m_zstream.avail_out = len;
            m_zstream.next_out = reinterpret_cast<unsigned char*>(buf);
            do {
                // Reset input buffer if empty
                if (m_zstream.avail_in == 0) {
                    m_backing.read(reinterpret_cast<char*>(m_buffer.data()), m_buffer.size());
                    m_zstream.avail_in = m_backing.gcount();
                    if (m_zstream.avail_in == 0) {
                        m_byte_out_count += len - m_zstream.avail_out;
                        //::std::cerr << "Out of bytes, " << m_zstream.avail_out << " needed" << ::std::endl;
                        return len - m_zstream.avail_out;
                    }
                    m_zstream.next_in = const_cast<unsigned char*>(m_buffer.data());

                    m_byte_in_count += m_zstream.avail_in;
                }

                int ret = inflate(&m_zstream, Z_NO_FLUSH);
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

            } while (m_zstream.avail_out > 0);
            m_byte_out_count += len;

            return len;
        }

    } // namespace serialise
} // namespace HIR

namespace HIR { namespace serialise {

void Writer::write_u16(uint16_t v) {
    uint8_t buf[] = {static_cast<uint8_t>(v & 0xFF), static_cast<uint8_t>(v >> 8)};
    this->write(buf, 2);
}
void Writer::write_u32(uint32_t v) {
    uint8_t buf[] = {static_cast<uint8_t>(v & 0xFF), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 24)};
    this->write(buf, 4);
}
void Writer::write_u64(uint64_t v) {
    uint8_t buf[] = {static_cast<uint8_t>(v & 0xFF), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 24), static_cast<uint8_t>(v >> 32), static_cast<uint8_t>(v >> 40), static_cast<uint8_t>(v >> 48), static_cast<uint8_t>(v >> 56)};
    this->write(buf, 8);
}
// Variable-length encoded u64 (for array sizes)
void Writer::write_u64c(uint64_t v) {
    if (v < (1 << 7)) {
        write_u8(static_cast<uint8_t>(v));
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
void Writer::write_i64c(int64_t v) {
    // Convert from 2's completement
    bool sign = (v < 0);
    uint64_t va = (v < 0 ? -v : v);
    va <<= 1;
    va |= (sign ? 1 : 0);
    write_u64c(va);
}
void Writer::write_u128(U128 v) {
    write_u64(v.get_lo());
    write_u64(v.get_hi());
}
void Writer::write_double(double v) {
    // - Just raw-writes the double
    this->write(&v, sizeof v);
}
void Writer::write_float_value(FloatValue value) {
    auto encoded = F128(value);
    write_u64(encoded.lo);
    write_u64(encoded.hi);
}
void Writer::write_tag(unsigned int t) {
    assert(t < 256);
    write_u8(static_cast<uint8_t>(t));
}
void Writer::write_count(size_t c) {
    DEBUG(c);
    if (c < 0xFD) {
        write_u8(static_cast<uint8_t>(c));
    } else if (c == ~0u) {
        write_u8(0xFF);
    } else if (c < (1u << 16)) {
        write_u8(0xFD);
        write_u16(static_cast<uint16_t>(c));
    } else {
        assert(c < (1u << 31));
        write_u8(0xFE);
        write_u32(static_cast<uint32_t>(c));
    }
}
void Writer::write_string(size_t len, const char* s) {
    TRACE_FUNCTION;
    if (len < 128) {
        write_u8(static_cast<uint8_t>(len));
    } else {
        assert(len < (1u << (16 + 7)));
        write_u8(static_cast<uint8_t>(128 + (len >> 16)));
        write_u16(static_cast<uint16_t>(len & 0xFFFF));
    }
    this->write(s, len);
}
void Writer::write_bool(bool v) {
    TRACE_FUNCTION_F(v);
    write_u8(v ? 0xFF : 0x00);
}
// Core protocol
void Writer::raw_write_uint(uint64_t val) {
    if (val < 0xC0) {
        write_u8(static_cast<uint8_t>(val));
    } else {
        uint8_t bytes[8];
        uint8_t len = 0;
        while (val > 0) {
            assert(len < 8);
            bytes[len] = static_cast<uint8_t>(val);
            val >>= 8;
            len += 1;
        }
        write_u8(0xC0 + len);
        this->write(bytes, len);
    }
}
void Writer::raw_write_len(size_t len) {
    if (len < (0xFC - 0xC0)) {
        write_u8(0xC0 + len);
    } else {
        write_u8(0xFC);
        raw_write_uint(len);
    }
}
void Writer::raw_write_bytes(size_t len, const void* data) {
    raw_write_len(len);
    this->write(data, len);
}
Writer::CloseOnDrop::CloseOnDrop(Writer& r)
    : r(&r) {
}
Writer::CloseOnDrop::CloseOnDrop(CloseOnDrop&& x)
    : r(x.r) {
    x.r = nullptr;
}
Writer::CloseOnDrop::~CloseOnDrop() {
    if (r) {
        r->close_object();
    }
    r = nullptr;
}
Writer::CloseOnDrop Writer::open_object(const char* name) {
    write_u8(0xFD);
    auto iv = m_objname_cache.insert(std::make_pair(name, static_cast<unsigned>(m_objname_cache.size())));
    raw_write_uint(iv.first->second);
    if (iv.second) {
        raw_write_bytes(strlen(name), name);
    }
    return CloseOnDrop(*this);
}
Writer::CloseOnDrop Writer::open_anon_object() {
    write_u8(0xFE);
    return CloseOnDrop(*this);
}
uint8_t Reader::read_u8() {
    uint8_t v;
    read(&v, sizeof v);
    return v;
}
uint16_t Reader::read_u16() {
    uint8_t buf[2];
    read(buf, sizeof buf);
    return static_cast<uint16_t>(buf[0]) | (static_cast<uint16_t>(buf[1]) << 8);
}
uint32_t Reader::read_u32() {
    uint8_t buf[4];
    read(buf, sizeof buf);
    return static_cast<uint32_t>(buf[0]) | (static_cast<uint32_t>(buf[1]) << 8) | (static_cast<uint32_t>(buf[2]) << 16) | (static_cast<uint32_t>(buf[3]) << 24);
}
uint64_t Reader::read_u64() {
    uint8_t buf[8];
    read(buf, sizeof buf);
    return static_cast<uint64_t>(buf[0]) | (static_cast<uint64_t>(buf[1]) << 8) | (static_cast<uint64_t>(buf[2]) << 16) | (static_cast<uint64_t>(buf[3]) << 24) | (static_cast<uint64_t>(buf[4]) << 32) | (static_cast<uint64_t>(buf[5]) << 40) | (static_cast<uint64_t>(buf[6]) << 48) | (static_cast<uint64_t>(buf[7]) << 56);
}
U128 Reader::read_u128() {
    auto lo = read_u64();
    auto hi = read_u64();
    return U128(lo, hi);
}
// Variable-length encoded u64 (for array sizes)
uint64_t Reader::read_u64c() {
    auto v = read_u8();
    if (v < (1 << 7)) {
        return static_cast<uint64_t>(v);
    } else if (v < 0xC0) {
        uint64_t rv = static_cast<uint64_t>(v & 0x3F) << 16;
        rv |= static_cast<uint64_t>(read_u8()) << 8;
        rv |= static_cast<uint64_t>(read_u8());
        return rv;
    } else if (v < 0xFF) {
        uint64_t rv = static_cast<uint64_t>(v & 0x3F) << 32;
        rv |= static_cast<uint64_t>(read_u8()) << 24;
        rv |= static_cast<uint64_t>(read_u8()) << 16;
        rv |= static_cast<uint64_t>(read_u8()) << 8;
        rv |= static_cast<uint64_t>(read_u8());
        return rv;
    } else {
        return read_u64();
    }
}
int64_t Reader::read_i64c() {
    uint64_t va = read_u64c();
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
double Reader::read_double() {
    double v;
    read(reinterpret_cast<char*>(&v), sizeof v);
    return v;
}
FloatValue Reader::read_float_value() {
    F128 encoded;
    encoded.lo = read_u64();
    encoded.hi = read_u64();
    return encoded;
}
size_t Reader::read_count() {
    size_t rv;
    auto v = read_u8();
    if (v < 0xFD) {
        rv = v;
    } else if (v == 0xFD) {
        rv = read_u16();
    } else if (v == 0xFE) {
        rv = read_u32();
    } else /*if( v == 0xFF )*/ {
        rv = ~0u;
    }
    DEBUG(rv);
    return rv;
}
RcString Reader::read_istring() {
    size_t idx = read_count();
    return m_strings.at(idx);
}
::std::string Reader::read_string() {
    size_t len = read_u8();
    if (len < 128) {
    } else {
        len = (len & 0x7F) << 16;
        len |= read_u16();
    }
    ::std::string rv(len, '\0');
    read(const_cast<char*>(rv.data()), len);
    return rv;
}
bool Reader::read_bool() {
    auto v = read_u8();
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
uint64_t Reader::raw_read_uint() {
    auto v = read_u8();
    assert(v <= 0xC0 + 8);
    if (v < 0xC0) {
        return v;
    } else {
        size_t len = v - 0xC0;
        uint64_t rv = 0;
        for (size_t p = 0; p < len; p++) {
            rv |= static_cast<uint64_t>(read_u8()) << (8 * p);
        }
        return rv;
    }
}
size_t Reader::raw_read_len() {
    auto v = read_u8();
    if (v < 0xC0) {
        std::cerr << "Expected length, got literal integer " << unsigned(v) << ::std::endl;
        abort();
    } else if (v < 0xFC) {
        return v - 0xC0;
    } else if (v == 0xFC) {
        return raw_read_uint();
    } else {
        std::cerr << "Expected length, got tag " << unsigned(v) << ::std::endl;
        abort();
    }
}
std::string Reader::raw_read_bytes_stdstring() {
    auto len = raw_read_len();
    std::string rv(len, '\0');
    read(const_cast<char*>(rv.data()), len);
    return rv;
}
Reader::CloseOnDrop::CloseOnDrop(Reader& r)
    : r(&r) {
}
Reader::CloseOnDrop::CloseOnDrop(CloseOnDrop&& x)
    : r(x.r) {
    x.r = nullptr;
}
Reader::CloseOnDrop::~CloseOnDrop() {
    if (r) {
        r->close_object();
    }
    r = nullptr;
}
Reader::CloseOnDrop Reader::open_object(const char* name) {
    auto v = read_u8();
    if (v != 0xFD) {
        std::cerr << "Expected OpenNamed(" << name << "), got " << unsigned(v) << "u8" << ::std::endl;
        abort();
    }
    auto key = raw_read_uint();
    //std::cout << key << " = " << "..." << std::endl;
    if (key == m_objname_cache.size()) {
        m_objname_cache.push_back(raw_read_bytes_stdstring());
    }
    assert(key < m_objname_cache.size());
    //std::cout << key << " = " << m_objname_cache[key] << std::endl;
    if (m_objname_cache[key] != name) {
        std::cerr << "Expecting OpenNamed(" << name << "), got OpenNamed(" << m_objname_cache[key] << ")" << std::endl;
        abort();
    }
    return CloseOnDrop(*this);
}
Reader::CloseOnDrop Reader::open_anon_object() {
    auto v = read_u8();
    if (v != 0xFE) {
        std::cerr << "Expected OpenAnon, got " << unsigned(v) << ::std::endl;
        abort();
    }
    return CloseOnDrop(*this);
}
void Reader::close_object() {
    auto v = read_u8();
    if (v != 0xFF) {
        std::cerr << "Expected CloseObject(0xFF), got " << unsigned(v) << ::std::endl;
        abort();
    }
}
}}
