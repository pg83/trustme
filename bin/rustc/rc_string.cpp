#include "rc_string.h"
#include <cstring>
#include <string>
#include <iostream>
#include <algorithm> // std::max

RcString::RcString(const char* s, size_t len)
    : ptr(nullptr)
{
    if (len > 0) {
        size_t nwords = (len + 1 + sizeof(unsigned int) - 1) / sizeof(unsigned int);
        ptr = reinterpret_cast<Inner*>(malloc(sizeof(Inner) + (nwords - 1) * sizeof(unsigned int)));
        ptr->refcount = 1;
        ptr->size = static_cast<unsigned>(len);
        ptr->ordering = 0;
        char* dataMut = reinterpret_cast<char*>(ptr->data);
        for (unsigned int j = 0; j < len; j++) {
            dataMut[j] = s[j];
        }
        dataMut[len] = '\0';
    }
}

RcString::~RcString() {
    if (ptr) {
        ptr->refcount -= 1;
        //::std::cout << "RcString(" << m_ptr << " \"" << *this << "\") - " << *m_ptr << " refs left (drop)" << ::std::endl;
        if (ptr->refcount == 0) {
            free(ptr);
        }
        ptr = nullptr;
    }
}

Ordering RcString::ord(const char* s, size_t len) const {
    auto cmpLen = ::std::min(len, this->size());
    if (cmpLen > 0) {
        int cmp = memcmp(this->c_str(), s, cmpLen);
        if (cmp != 0) {
            return ::ord(cmp, 0);
        }
    }
    // Since the prefix is equal, then sort `this` before `s` if it's shorter
    return ::ord(this->size(), len);
}

Ordering RcString::ord(const char* s) const {
    if (ptr == nullptr) {
        return (*s == '\0' ? OrdEqual : OrdLess);
    }

    int cmp = strncmp(this->c_str(), s, this->size());
    if (cmp == 0) {
        if (s[this->size()] == '\0') {
            return OrdEqual;
        } else {
            return OrdLess;
        }
    }
    return ::ord(cmp, 0);
}

::std::ostream& operator<<(::std::ostream& os, const RcString& x) {
    for (size_t i = 0; i < x.size(); i++) {
        os << x.c_str()[i];
    }
    return os;
}

// Replace the use of `std::set` with a collection of sorted buffers
// Limit each entry to ~1024 items, and split in half when full.
// - This limits the cost of insertion to just needing to move a maximum of 1024 items plus the ~170 items in the outer list (assuming an average of 75% usage)
// - Numbers: libcargo 1.74 has 128,900 interned strings (of which 115,984 are in use at Trans), hence the above estimate of 170 blocks of 1024
namespace {
    struct StringView {
        const char* p;
        size_t l;

        operator RcString() const {
            return RcString(p, l);
        }
    };

    struct CmpRcStringRaw {
        bool operator()(const RcString& a, const RcString& b) const {
            return a.ord(b.c_str(), b.size()) == OrdLess;
        }

        bool operator()(const RcString& a, StringView& b) const {
            return a.ord(b.p, b.l) == OrdLess;
        }
    };

    // This is faster than std::set, as it doesn't have to allocate `RcString` instances, and it has lower memory overhead
    const size_t BLOCK_SIZE = 1024;

    class TieredSet {
        struct Block {
            std::vector<RcString> ents;

            Block() {
                ents.reserve(BLOCK_SIZE);
            }
        };

        std::vector<Block> blocks;

    public:
        TieredSet() {
            blocks.reserve(150'000 * 3 / BLOCK_SIZE / 2);
        }

        std::pair<const RcString*, bool> lookup_or_add(const StringView& sv) {
            // Special case: empty collection
            if (blocks.empty()) {
                blocks.push_back(Block());
                blocks.front().ents.push_back(RcString(sv));
                return ::std::make_pair(&blocks.front().ents.front(), true);
            }

            // Find the block that starts with an element after this string
            auto maybe_after = ::std::lower_bound(blocks.begin(), blocks.end(), sv, [](const Block& b, const StringView& sv) {
                return b.ents.front().ord(sv.p, sv.l) == OrdLess;
            });

            if (maybe_after != blocks.end() && maybe_after->ents.front().ord(sv.p, sv.l) == OrdEqual) {
                return std::make_pair(&maybe_after->ents.front(), false);
            }
            // Special case: The first block sorts after this string, so we need to add the new string to the start of it (or to a new block before)
            else if (maybe_after == blocks.begin()) {
                return insert_into_block(maybe_after, maybe_after->ents.begin(), RcString(sv));
            }
            // Since the string sorts before the beginning of `maybe_after`, it should be in (or be added to) the previous block
            else {
                auto maybe_block = maybe_after - 1;
                auto& ents = maybe_block->ents;
                auto maybe_pos = std::lower_bound(ents.begin(), ents.end(), sv, [](const RcString& s, const StringView& sv) {
                    return s.ord(sv.p, sv.l) == OrdLess;
                });
                if (maybe_pos != ents.end() && maybe_pos->ord(sv.p, sv.l) == OrdEqual) {
                    return std::make_pair(&*maybe_pos, false);
                } else {
                    // Not equal, so it has to be above - so insert
                    return insert_into_block(maybe_block, maybe_pos, RcString(sv));
                }
            }
        }

        struct It {
            std::vector<Block>::iterator block, blockE;
            std::vector<RcString>::iterator slot;

            RcString& operator*() {
                assert(block != blockE);
                assert(slot != block->ents.end());
                return *slot;
            }

            It& operator++() {
                assert(block != blockE);
                assert(slot != block->ents.end());
                ++slot;
                if (slot == block->ents.end()) {
                    ++block;
                    if (block != blockE) {
                        slot = block->ents.begin();
                    }
                }
                return *this;
            }

            bool operator!=(const It& x) const {
                return block != x.block || slot != x.slot;
            }
        };

        It begin() {
            return It{blocks.begin(), blocks.end(), blocks.front().ents.begin()};
        }

        It end() {
            return It{blocks.end(), blocks.end(), blocks.back().ents.end()};
        }

    private:
        std::pair<const RcString*, bool> insert_into_block(std::vector<Block>::iterator block, std::vector<RcString>::iterator slot, RcString rv) {
#define VALIDATE 0
#if VALIDATE
            size_t start_len = 0;
            for (const auto& v : *this) {
                start_len += 1;
            }
#endif
            if (block->ents.size() == block->ents.capacity()) {
                // Block is full, so create a new block and split the contents between the two
                // - The new block should go after the current one, and get half of its contents
                auto new_block = blocks.insert(block + 1, Block());
                block = new_block - 1;
                const auto split_point = block->ents.size() / 2;
                if (static_cast<size_t>(slot - block->ents.begin()) >= split_point) {
                    // The target location is in the second half of the range, so we're inserting into the new block
                    new_block->ents.insert(new_block->ents.end(), block->ents.begin() + split_point, slot);
                    new_block->ents.push_back(rv);
                    slot = new_block->ents.insert(new_block->ents.end(), slot, block->ents.end()) - 1;
                    block->ents.resize(split_point);
                } else {
                    // Target is in the lower half, so copy the entities and then insert
                    new_block->ents.insert(new_block->ents.end(), block->ents.begin() + split_point, block->ents.end());
                    block->ents.resize(split_point);
                    slot = block->ents.insert(slot, rv);
                }
            } else {
                slot = block->ents.insert(slot, rv);
            }

#if VALIDATE
            StringView prev{nullptr, 0};
            size_t endLen = 0;
            for (auto& v : *this) {
                if (prev.p) {
                    if (v.ord(prev.p, prev.l) > 0) {
                    } else {
                        std::cerr << "BUG: Ordering lost after adding `" << rv << "` - '" << prev.p << "' and '" << v << "'\n";
                        abort();
                    }
                }
                prev.p = v.c_str();
                prev.l = v.size();
                endLen += 1;
            }
            if (start_len + 1 != endLen) {
                std::cerr << "BUG: Counts failed after addition of `" << rv << " (was " << start_len << ", now " << endLen << ")`\n";
                abort();
            }
            ::std::cerr << "ADDED #" << endLen << ": `" << rv << "`\n";
#endif

            return std::make_pair(&*slot, true);
        }
    };
}

TieredSet* RcStringInternedStrings;
bool RcStringInternedOrderingValid;

RcString RcString::new_interned(const char* s, size_t len) {
    if (len == 0) {
        return RcString();
    }
    if (!RcStringInternedStrings) {
        RcStringInternedStrings = new TieredSet;
    }
    auto ret = RcStringInternedStrings->lookup_or_add(StringView{s, len});
    // Set interned and invalidate the cache if an insert happened
    if (ret.second) {
        ret.first->ptr->ordering = 1;
        RcStringInternedOrderingValid = false;
    }
    //assert( ret.first->ord(s, len) == 0 );
    return *ret.first;
}

Ordering RcString::ord_interned(const RcString& s) const {
    assert(s.is_interned() && this->is_interned());
    if (!RcStringInternedOrderingValid) {
        // Populate cache
        unsigned i = 1;
        assert(RcStringInternedStrings);
        for (auto& e : *RcStringInternedStrings) {
            e.ptr->ordering = i++;
        }
        RcStringInternedOrderingValid = true;
    }
    return ::ord(this->ptr->ordering, s.ptr->ordering);
}

size_t std::hash<RcString>::operator()(const RcString& s) const noexcept {
    // http://www.cse.yorku.ca/~oz/hash.html "djb2"
    size_t h = 5381;
    for (auto c : s) {
        h = h * 33 + (unsigned)c;
    }
    return h;
    //return hash<std::string_view>(s.c_str(), s.size());
}

RcString::RcString()
    : ptr(nullptr) {
}
RcString::RcString(const char* s)
    : RcString(s, ::std::strlen(s)) {
}
RcString::RcString(const ::std::string& s)
    : RcString(s.data(), s.size()) {
}
RcString::RcString(const RcString& x)
    : ptr(x.ptr) {
    if (ptr) {
        ptr->refcount += 1;
    }
}
RcString::RcString(RcString&& x)
    : ptr(x.ptr) {
    x.ptr = nullptr;
}
RcString& RcString::operator=(const RcString& x) {
    if (&x != this) {
        this->~RcString();
        ptr = x.ptr;
        if (ptr) {
            ptr->refcount += 1;
        }
    }
    return *this;
}
RcString& RcString::operator=(RcString&& x) {
    if (&x != this) {
        this->~RcString();
        ptr = x.ptr;
        x.ptr = nullptr;
    }
    return *this;
}
const char* RcString::c_str() const {
    if (ptr) {
        return reinterpret_cast<const char*>(ptr->data);
    } else {
        return "";
    }
}
char RcString::back() const {
    assert(size() > 0);
    return *(c_str() + size() - 1);
}
Ordering RcString::ord(const RcString& s) const {
    if (ptr == s.ptr) {
        return OrdEqual;
    }
    if (!ptr || !s.ptr) {
        return ptr ? OrdGreater : OrdLess;
    }
    // If both are interned, then use stored sorting
    if (is_interned() && s.is_interned()) {
        return ord_interned(s);
    }
    return ord(s.c_str(), s.size());
}
bool RcString::operator==(const RcString& s) const {
    if (s.size() != this->size()) {
        return false;
    }
    // If both are interned, then just compare pointers
    if (is_interned() && s.is_interned()) {
        return ptr == s.ptr;
    }
    return this->ord(s) == OrdEqual;
}
int RcString::compare(size_t o, size_t l, const char* s) const {
    assert(o <= this->size());
    if (l <= this->size() - o) {
        return memcmp(this->c_str() + o, s, l);
    } else {
        if (int rv = memcmp(this->c_str() + o, s, this->size() - o)) {
            return rv;
        }
        return -1;
    }
}
