#include "hir_serialise_lowlevel.h"

#include "common.h"

#include <std/ios/fs_utils.h>
#include <std/ios/out_fd.h>
#include <std/sys/fd.h>
#include <std/lib/buffer.h>
#include <std/lib/vector.h>
#include <std/sym/h_map.h>
#include <std/mem/obj_pool.h>
#include <std/rng/split_mix_64.h>

#include <zstd.h>
#include <fcntl.h>
#include <map>
#include <vector>
#include <string.h>
#include <algorithm>

using namespace stl;

namespace {
    const int COMPRESSION_LEVEL = 9;

    const u8 TAG_OPEN_NAMED = 0xFD;
    const u8 TAG_OPEN_ANON = 0xFE;
    const u8 TAG_CLOSE = 0xFF;

    struct InternedStringHasher {
        static u64 hash(RcString name) noexcept {
            return splitMix64(name.rawId());
        }
    };

    struct WrittenString {
        RcString value;
        unsigned uses;
        unsigned slot;
    };

    struct WriterImpl final: public HIRSerialiseWriter {
        Buffer path;
        Buffer data;
        bool recording;
        ObjPool::Ref istringPool;
        HashMap<unsigned, RcString, InternedStringHasher> istringIndex;
        Vector<WrittenString> istrings;
        std::map<const char*, unsigned> objnameCache;

        WriterImpl();
        ~WriterImpl();

        void open(const std::string& filename) override;
        void write(const void* data, size_t count) override;
        void writeU16(u16 v) override;
        void writeU32(u32 v) override;
        void writeU64(u64 v) override;
        void writeU128(U128 v) override;
        void writeDouble(double v) override;
        void writeFloatValue(FloatValue value) override;
        void writeTag(unsigned int t) override;
        void writeCount(size_t c) override;
        void writeString(const RcString& v) override;
        void writeString(size_t len, const char* s) override;
        void writeBool(bool v) override;
        CloseOnDrop openObject(const char* name) override;
        CloseOnDrop openAnonObject() override;
    };

    struct ReaderImpl final: public HIRSerialiseReader {
        Buffer data;
        size_t pos;
        Vector<RcString> strings;
        std::vector<std::string> objnameCache;

        explicit ReaderImpl(const std::string& path);

        size_t getPos() const override;
        void read(void* dst, size_t count) override;
        u8 readU8() override;
        u16 readU16() override;
        u32 readU32() override;
        u64 readU64() override;
        U128 readU128() override;
        double readDouble() override;
        FloatValue readFloatValue() override;
        size_t readCount() override;
        RcString readIstring() override;
        std::string readString() override;
        bool readBool() override;
        CloseOnDrop openObject(const char* name) override;
        CloseOnDrop openAnonObject() override;
        void closeObject() override;
    };
}

WriterImpl::WriterImpl()
    : recording(false)
    , istringPool(ObjPool::fromMemory())
    , istringIndex(istringPool.mutPtr())
{
}

WriterImpl::~WriterImpl() {
    if (!recording) {
        return;
    }

    Buffer packed(ZSTD_compressBound(data.length()));
    auto len = ZSTD_compress(packed.mutData(), packed.capacity(), data.data(), data.length(), COMPRESSION_LEVEL);

    if (ZSTD_isError(len)) {
        sysE << StringView("ERROR: zstd compression failed: ") << StringView(ZSTD_getErrorName(len)) << endL;
        abort();
    }

    ScopedFD fd(::open(path.cStr(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666));
    if (fd.get() < 0) {
        sysE << StringView("ERROR: can not open ") << path.cStr() << endL;
        abort();
    }

    FDOutput(fd).writeC(packed.data(), len);
}

void WriterImpl::open(const std::string& filename) {
    Vector<unsigned> order;
    order.grow(istrings.length());
    for (unsigned i = 0; i < istrings.length(); i++) {
        order.pushBack(i);
    }
    std::sort(order.mutBegin(), order.mutEnd(), [this](unsigned a, unsigned b) {
        const auto& left = istrings[a];
        const auto& right = istrings[b];
        if (left.uses != right.uses) {
            return left.uses > right.uses;
        }
        return left.value.ord(right.value) == OrdLess;
    });

    objnameCache.clear();

    path = Buffer(StringView(filename.c_str()));
    recording = true;

    this->writeCount(order.length());
    for (unsigned i = 0; i < order.length(); i++) {
        auto& entry = istrings.mut(order[i]);
        this->writeString(entry.value.size(), entry.value.c_str());
        DEBUG(i << StringView(" = ") << entry.uses << StringView(" '") << entry.value << StringView("'"));
        entry.slot = i;
    }
}

void WriterImpl::write(const void* data, size_t count) {
    if (recording) {
        this->data.append(data, count);
    }
}

void WriterImpl::writeU16(u16 v) {
    this->write(&v, sizeof v);
}

void WriterImpl::writeU32(u32 v) {
    this->write(&v, sizeof v);
}

void WriterImpl::writeU64(u64 v) {
    this->write(&v, sizeof v);
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
    writeU64(c);
}

void WriterImpl::writeString(const RcString& v) {
    const auto* found = istringIndex.find(v);
    if (recording) {
        BUG_ASSERT(found);
        this->writeCount(istrings[*found].slot);
        return;
    }
    if (!found) {
        found = istringIndex.insert(v, static_cast<unsigned>(istrings.length()));
        istrings.pushBack(WrittenString{v, 0, 0});
    }
    istrings.mut(*found).uses++;
}

void WriterImpl::writeString(size_t len, const char* s) {
    writeU64(len);
    this->write(s, len);
}

void WriterImpl::writeBool(bool v) {
    writeU8(v ? 1 : 0);
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
    writeU8(TAG_OPEN_NAMED);
    auto iv = objnameCache.insert(std::make_pair(name, static_cast<unsigned>(objnameCache.size())));
    writeU64(iv.first->second);
    if (iv.second) {
        writeString(strlen(name), name);
    }
    return CloseOnDrop(*this);
}

HIRSerialiseWriter::CloseOnDrop WriterImpl::openAnonObject() {
    writeU8(TAG_OPEN_ANON);
    return CloseOnDrop(*this);
}

ReaderImpl::ReaderImpl(const std::string& path)
    : data()
    , pos(0)
{
    Buffer name(StringView(path.c_str()));
    Buffer packed;
    readFileContent(name, packed);

    auto size = ZSTD_getFrameContentSize(packed.data(), packed.length());
    if (size == ZSTD_CONTENTSIZE_ERROR || size == ZSTD_CONTENTSIZE_UNKNOWN) {
        throw std::runtime_error("Not a zstd frame: " + path);
    }

    data.grow(size);
    auto len = ZSTD_decompress(data.mutData(), data.capacity(), packed.data(), packed.length());
    if (ZSTD_isError(len)) {
        throw std::runtime_error("Unable to decompress " + path + ": " + ZSTD_getErrorName(len));
    }
    data.seekAbsolute(len);

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

void ReaderImpl::read(void* dst, size_t count) {
    if (data.length() - pos < count) {
        throw std::runtime_error(FMT(StringView("Reader::read - requested ") << count << StringView(" bytes at ") << pos << StringView(" of ") << data.length()));
    }
    memcpy(dst, static_cast<const u8*>(data.data()) + pos, count);
    pos += count;
}

u8 ReaderImpl::readU8() {
    u8 v;
    read(&v, sizeof v);
    return v;
}

u16 ReaderImpl::readU16() {
    u16 v;
    read(&v, sizeof v);
    return v;
}

u32 ReaderImpl::readU32() {
    u32 v;
    read(&v, sizeof v);
    return v;
}

u64 ReaderImpl::readU64() {
    u64 v;
    read(&v, sizeof v);
    return v;
}

U128 ReaderImpl::readU128() {
    auto lo = readU64();
    auto hi = readU64();
    return U128(lo, hi);
}

double ReaderImpl::readDouble() {
    double v;
    read(&v, sizeof v);
    return v;
}

FloatValue ReaderImpl::readFloatValue() {
    F128 encoded;
    encoded.lo = readU64();
    encoded.hi = readU64();
    return encoded;
}

size_t ReaderImpl::readCount() {
    return readU64();
}

RcString ReaderImpl::readIstring() {
    return strings[readCount()];
}

std::string ReaderImpl::readString() {
    size_t len = readU64();
    std::string rv(len, '\0');
    read(rv.data(), len);
    return rv;
}

bool ReaderImpl::readBool() {
    auto v = readU8();
    if (v > 1) {
        sysE << StringView("Expected false(0)/true(1), got ") << unsigned(v) << StringView("u8") << endL;
        abort();
    }
    return v != 0;
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
    if (v != TAG_OPEN_NAMED) {
        sysE << StringView("Expected OpenNamed(") << name << StringView("), got ") << unsigned(v) << StringView("u8") << endL;
        abort();
    }
    auto key = readU64();
    if (key == objnameCache.size()) {
        objnameCache.push_back(readString());
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
    if (v != TAG_OPEN_ANON) {
        sysE << StringView("Expected OpenAnon, got ") << unsigned(v) << endL;
        abort();
    }
    return CloseOnDrop(*this);
}

void ReaderImpl::closeObject() {
    auto v = readU8();
    if (v != TAG_CLOSE) {
        sysE << StringView("Expected CloseObject(0xFF), got ") << unsigned(v) << endL;
        abort();
    }
}

void HIRSerialiseWriter::writeU8(u8 v) {
    write(&v, sizeof v);
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
    writeU8(TAG_CLOSE);
}

HIRSerialiseWriter* HIRSerialiseWriter::create(ObjPool& pool) {
    return pool.make<WriterImpl>();
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

bool HIRSerialiseReader::isMetadata(const std::string& path) {
    ScopedFD fd(::open(path.c_str(), O_RDONLY));
    if (fd.get() < 0) {
        return false;
    }

    u32 magic = 0;
    if (fd.read(&magic, sizeof magic) != sizeof magic) {
        return false;
    }

    return magic == ZSTD_MAGICNUMBER;
}

HIRSerialiseReader* HIRSerialiseReader::create(ObjPool& pool, const std::string& path) {
    return pool.make<ReaderImpl>(path);
}
