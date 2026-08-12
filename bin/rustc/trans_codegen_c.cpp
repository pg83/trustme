#include "trans_codegen_c.h"
#include "trans_codegen.h"
#include "trans_mangling.h"
#include <fstream>
#include <algorithm>
#include <cmath>
#include "hir_hir.h"
#include <limits>
#include "mir_mir.h"
#include "hir_typeck_static.h"
#include "mir_helpers.h"
#include "trans_target.h"
#include "trans_allocator.h"
#include <iomanip>
#include <string_view>
#include "target_version.h"

namespace {
    struct FmtShell {
        const ::std::string& s;

        FmtShell(const ::std::string& s)
            : s(s)
        {
        }
    };

    struct FmtGccAsm {
        const ::std::string& s;
        bool escapePercent;

        FmtGccAsm(const ::std::string& s, bool escapePercent)
            : s(s)
            , escapePercent(escapePercent)
        {
        }
    };

    class StringList {
        ::std::vector<::std::string> cached;
        ::std::vector<const char*> strings;

    public:
        StringList() {
        }

        StringList(const StringList&) = delete;
        StringList(StringList&&) = default;

        const ::std::vector<const char*>& getVec() const {
            return strings;
        }

        std::vector<const char*>::const_iterator begin() const {
            return strings.begin();
        }

        std::vector<const char*>::const_iterator end() const {
            return strings.end();
        }

        void push_back(::std::string s) {
            // If the cache list is about to move, update the pointers
            if (cached.capacity() == cached.size()) {
                // Make a bitmap of entries in `m_strings` that are pointers into `m_cached`
                ::std::vector<bool> b;
                b.reserve(strings.size());
                size_t j = 0;
                for (const auto* s : strings) {
                    if (j == cached.size()) {
                        break;
                    }
                    if (s == cached[j].c_str()) {
                        j++;
                        b.push_back(true);
                    } else {
                        b.push_back(false);
                    }
                }

                // Add the new one
                cached.push_back(::std::move(s));
                // Update pointers
                j = 0;
                for (size_t i = 0; i < b.size(); i++) {
                    if (b[i]) {
                        strings[i] = cached.at(j++).c_str();
                    }
                }
            } else {
                cached.push_back(::std::move(s));
            }
            strings.push_back(cached.back().c_str());
        }

        void push_back(const char* s) {
            strings.push_back(s);
        }
    };
}

::std::ostream& operator<<(::std::ostream& os, const FmtShell& x) {
    for (char c : x.s) {
        // Backslash and double quote need escaping
        switch (c) {
            case '\\':
            case '\"':
            case ' ':
                os << "\\";
            default:
                os << c;
        }
    }
    return os;
}

::std::ostream& operator<<(::std::ostream& os, const FmtGccAsm& x) {
    bool inComment = false;
    for (const char& ch : x.s) {
        if (ch == '/' && (&ch)[1] == '/') {
            if (!inComment) {
                os << "\" ";
            }
            inComment = true;
        } else {
            inComment = false;
        }
        switch (ch) {
            case '\n':
                os << "\\n\"\n\"";
                break;
            //case '\r':  os << "\\r";   break;
            case '\"':
                os << "\\\"";
                break;
            case '%':
                if (x.escapePercent) {
                    os << "%%";
                } else {
                    os << "%";
                }
                break;
            case '{':
                os << "%{";
                break;
            case '}':
                os << "%}";
                break;
            case '|':
                os << "%|";
                break;
            default:
                os << ch;
                break;
        }
    }
    return os;
}

namespace {
    enum class AtomicOp {
        Add,
        Sub,
        And,
        Or,
        Xor
    };

    class CodeGeneratorC: public CodeGenerator {
        static Span sp;

        const ::HIR::Crate& crate;
        ::StaticTraitResolve mResolve;

        ::std::string outfilePath;
        ::std::string outfilePathC;

        ::std::ofstream of;
        const ::MIR::TypeResolve* mirRes = nullptr;

        struct {
            bool emulatedI128 = false;
            bool disallowEmptyStructs = false;
        } options;

        ::std::set<::HIR::TypeRef> emittedFnTypes;
        ::std::set<const TypeRepr*> embeddedTags;

    public:
        CodeGeneratorC(const ::HIR::Crate& crate, const ::std::string& outfile)
            : crate(crate)
            , mResolve(crate)
            , outfilePath(outfile)
            , outfilePathC(outfile + ".cpp")
            , of(outfilePathC)
        {
            ASSERT_BUG(Span(), of.is_open(), "Failed to open `" << outfilePathC << "` for writing");
            options.emulatedI128 = TargetGetCurSpec().backendC.emulatedI128;
            if (TargetGetCurSpec().arch.pointerBits < 64 && !options.emulatedI128) {
                WARNING(Span(), W0000, "Potentially misconfigured target, 32-bit targets require i128 emulation");
            }
            options.disallowEmptyStructs = true;

            of << "/*\n"
                 << " * AUTOGENERATED by mrustc\n"
                 << " */\n"
                 << "#include <stddef.h>\n"
                 << "#include <stdint.h>\n"
                 << "#include <stdbool.h>\n"
                 << "#include <stdarg.h>\n"
                 << "#include <assert.h>\n"
                 << "#include <stdlib.h>\n"    // abort
                 << "#include <string.h>\n";   // mem*
            of << "typedef uint32_t RUST_CHAR;\n"
                 << "typedef uint8_t RUST_BOOL;\n"
                 << "typedef struct { void* PTR; size_t META; } SLICE_PTR;\n"
                 << "typedef struct { void* PTR; void* META; } TRAITOBJ_PTR;\n"
                 << "typedef struct { void (*drop)(void*); size_t size; size_t align; } VTABLE_HDR;\n";
            of << "struct mrustc_panic final { void* rust_exception; };\n";
            if (options.disallowEmptyStructs) {
                of << "typedef struct { char _d; } tUNIT;\n"
                     << "typedef char tBANG;\n"
                     << "typedef struct { char _d; } tTYPEID;\n";
            } else {
                of << "typedef struct { } tUNIT;\n"
                     << "typedef struct { } tBANG;\n"
                     << "typedef struct { } tTYPEID;\n";
            }
            of << "static inline size_t ALIGN_TO(size_t s, size_t a) { return (s + a-1) / a * a; }\n"
                 << "\n"
                 << "#define ALIGNOF(t) __alignof__(t)\n";
            // 64-bit bit ops (gcc intrinsics)
            of << "static inline uint64_t __builtin_clz64(uint64_t v) {\n"
                         << "\treturn ( (v >> 32) != 0 ? __builtin_clz(v>>32) : 32 + __builtin_clz(v));\n"
                         << "}\n"
                         << "static inline uint64_t __builtin_ctz64(uint64_t v) {\n"
                         << "\treturn ((v&0xFFFFFFFF) == 0 ? __builtin_ctz(v>>32) + 32 : __builtin_ctz(v));\n"
                         << "}\n";
                    // CAS-loop helpers for atomic operations without direct backend intrinsics.
                    for (int sz = 8; sz <= 64; sz *= 2) {
                        of << "static inline uint" << sz << "_t __mrustc_atomicloop" << sz << "(volatile uint" << sz << "_t* slot, uint" << sz << "_t param, int ordering, uint" << sz << "_t (*cb)(uint" << sz << "_t, uint" << sz << "_t)) {"
                             << " int ordering_load = (ordering == __ATOMIC_RELEASE || ordering == __ATOMIC_ACQ_REL ? __ATOMIC_RELAXED : ordering);"
                             << " for(;;) {"
                             << " uint" << sz << "_t v = __atomic_load_n(slot, ordering_load);"
                             << " uint" << sz << "_t next = cb(v, param);"
                             << " if( __atomic_compare_exchange_n(slot, &v, next, false, ordering, ordering_load) ) return v;"
                             << " }"
                             << "}\n";
            }
            of << "extern \"C\" {\n";

            if (options.emulatedI128) {
                of << "typedef struct { uint64_t lo, hi; } uint128_t;\n"
                     << "typedef struct { uint64_t lo, hi; } int128_t;\n"
                     << "static inline uint128_t intrinsic_ctlz_u128(uint128_t v);\n"
                     << "static inline uint128_t shl128(uint128_t a, uint32_t b);\n"
                     << "static inline uint128_t shr128(uint128_t a, uint32_t b);\n"
                     << "static inline float make_float(int is_neg, int exp, uint32_t mantissa_bits) { float rv; uint32_t vi=(mantissa_bits&((1<<23)-1))|((exp+127)<<23);if(is_neg)vi|=1<<31; memcpy(&rv, &vi, 4); return rv; }\n"
                     << "static inline double make_double(int is_neg, int exp, uint32_t mantissa_bits) { double rv; uint64_t vi=(mantissa_bits&((1ull<<52)-1))|((uint64_t)(exp+1023)<<52);if(is_neg)vi|=1ull<<63; memcpy(&rv, &vi, 4); return rv; }\n"
                     << "static inline uint128_t make128_raw(uint64_t hi, uint64_t lo) { uint128_t rv = { lo, hi }; return rv; }\n"
                     << "static inline uint128_t make128(uint64_t v) { uint128_t rv = { v, 0 }; return rv; }\n"
                     // https://blog.m-ou.se/floats/
                     << "static inline float cast128_float(uint128_t v) {"
                     << " int n = intrinsic_ctlz_u128(v).lo;"
                     << " uint128_t y = shl128(v, n);"
                     << " uint64_t a = (y.hi >> ((128-(23+1))-64));"                       // Base mantissa (top 24 bits)
                     << " uint64_t b = shr128(y, (64-(23+1))).lo | (y.lo & 0xFFFFFFFFFF);" // Were any discarded bits set? (shift right by 40 to get 64 bits with the top just before the last mantissa bit, then extract 40 bits from low)
                     << " uint64_t m = a + ((b - ((b >> 63) & ~a)) >> 63);"                // Account for rounding
                     << " uint64_t e = (v.lo == 0 && v.hi == 0) ? 0 : (127 - n)+127-1;"    // Exponent, with the -1 to fudge the 24th bit in mantissa
                     << " uint32_t vi = (e << 23) + m;"                                    // Add is like or, but the final bit of the mantissa can propagate through the exponent (intended)
                     //<< " printf(\"%016llx:%016llx n=%i,a=%#llx,b=%#llx  e=%lli,m=%#llx vi=%#lx\\n\", v.hi,v.lo, n, a, b, e, m, vi);"
                     << " float rv;"
                     << " memcpy(&rv, &vi, sizeof(rv));"
                     << " return rv;"
                     << " }\n"
                     << "static inline double cast128_double(uint128_t v) {"
                     << " int n = intrinsic_ctlz_u128(v).lo;"
                     << " uint128_t y = shl128(v, n);"
                     << " uint64_t a = (y.hi >> ((128-(52+1))-64));"
                     << " uint64_t b = shr128(y, (64-(52+1))).lo | (y.lo & 0x7FF);"
                     << " uint64_t m = a + ((b - (b >> 63 & ~a)) >> 63);"
                     << " uint64_t e = (v.lo == 0 && v.hi == 0) ? 0 : (127 - n)+1023-1;"
                     << " uint64_t vi = (e << 52) + m;"
                     << " double rv;"
                     << " memcpy(&rv, &vi, sizeof(rv));"
                     << " return rv;"
                     << " }\n"
                     << "static inline int cmp128(uint128_t a, uint128_t b) { if(a.hi != b.hi) return a.hi < b.hi ? -1 : 1; if(a.lo != b.lo) return a.lo < b.lo ? -1 : 1; return 0; }\n"
                     // Returns true if overflow happens (res < a)
                     << "static inline bool add128_o(uint128_t a, uint128_t b, uint128_t* o) { o->lo = a.lo + b.lo; o->hi = a.hi + b.hi + (o->lo < a.lo ? 1 : 0); return (o->hi < a.hi); }\n"
                     // Returns true if overflow happens (res > a)
                     << "static inline bool sub128_o(uint128_t a, uint128_t b, uint128_t* o) { o->lo = a.lo - b.lo; o->hi = a.hi - b.hi - (a.lo < b.lo ? 1 : 0); return (o->hi > a.hi); }\n"
                     // Serial shift+add
                     << "static inline bool mul128_o(uint128_t a, uint128_t b, uint128_t* o) {"
                     << " bool of = false;"
                     << " o->hi = 0; o->lo = 0;"
                     << " for(int i=0;i<128;i++){"
                     << " uint64_t m = (1ull << (i % 64));"
                     << " if(a.hi==0&&a.lo<m)   break;"
                     << " if(i>=64&&a.hi<m) break;"
                     << " if( m & (i >= 64 ? a.hi : a.lo) ) of |= add128_o(*o, b, o);"
                     << " b.hi = (b.hi << 1) | (b.lo >> 63);"
                     << " b.lo = (b.lo << 1);"
                     << " }"
                     << " return of;"
                     << "}\n"
                     // Long division
                     << "static inline bool div128_o(uint128_t a, uint128_t b, uint128_t* q, uint128_t* r) {"
                     << " if(a.hi == 0 && b.hi == 0) { if(q) { q->hi=0; q->lo = a.lo / b.lo; } if(r) { r->hi=0; r->lo = a.lo % b.lo; } return false; }"
                     << " if(cmp128(a, b) < 0) { if(q) { q->hi=0; q->lo=0; } if(r) *r = a; return false; }"
                     << " uint128_t a_div_2 = {(a.lo>>1)|(a.hi << 63), a.hi>>1};"
                     << " int shift = 0;"
                     << " while( cmp128(a_div_2, b) >= 0 && shift < 128 ) {"
                     << " shift += 1;"
                     << " b.hi = (b.hi<<1)|(b.lo>>63); b.lo <<= 1;"
                     << " }"
                     << " if(shift == 128) return true;" // true = overflowed
                     << " uint128_t mask = { /*lo=*/(shift >= 64 ? 0 : (1ull << shift)), /*hi=*/(shift < 64 ? 0 : 1ull << (shift-64)) };"
                     << " shift ++;"
                     << " if(q) { q->hi = 0; q->lo = 0; }"
                     << " while(shift--) {"
                     << " if( cmp128(a, b) >= 0 ) { if(q) add128_o(*q, mask, q); sub128_o(a, b, &a); }"
                     << " mask.lo = (mask.lo >> 1) | (mask.hi << 63); mask.hi >>= 1;"
                     << " b.lo = (b.lo >> 1) | (b.hi << 63); b.hi >>= 1;"
                     << " }"
                     << " if(r) *r = a;"
                     << " return false;"
                     << "}\n"
                     << "static inline uint128_t add128(uint128_t a, uint128_t b) { uint128_t v; add128_o(a, b, &v); return v; }\n"
                     << "static inline uint128_t sub128(uint128_t a, uint128_t b) { uint128_t v; sub128_o(a, b, &v); return v; }\n"
                     << "static inline uint128_t mul128(uint128_t a, uint128_t b) { uint128_t v; mul128_o(a, b, &v); return v; }\n"
                     << "static inline uint128_t div128(uint128_t a, uint128_t b) { uint128_t v; div128_o(a, b, &v, NULL); return v; }\n"
                     << "static inline uint128_t mod128(uint128_t a, uint128_t b) { uint128_t v; div128_o(a, b, NULL, &v); return v;}\n"
                     << "static inline uint128_t and128(uint128_t a, uint128_t b) { uint128_t v = { a.lo & b.lo, a.hi & b.hi }; return v; }\n"
                     << "static inline uint128_t or128 (uint128_t a, uint128_t b) { uint128_t v = { a.lo | b.lo, a.hi | b.hi }; return v; }\n"
                     << "static inline uint128_t xor128(uint128_t a, uint128_t b) { uint128_t v = { a.lo ^ b.lo, a.hi ^ b.hi }; return v; }\n"
                     << "static inline uint128_t shl128(uint128_t a, uint32_t b) { uint128_t v; if(b == 0) { return a; } else if(b < 64) { v.lo = a.lo << b; v.hi = (a.hi << b) | (a.lo >> (64 - b)); } else { v.hi = a.lo << (b - 64); v.lo = 0; } return v; }\n"
                     << "static inline uint128_t shr128(uint128_t a, uint32_t b) { uint128_t v; if(b == 0) { return a; } else if(b < 64) { v.lo = (a.lo >> b)|(a.hi << (64 - b)); v.hi = a.hi >> b; } else { v.lo = a.hi >> (b - 64); v.hi = 0; } return v; }\n"
                     << "static inline uint128_t popcount128(uint128_t a) { uint128_t v = { (uint64_t)(__builtin_popcountll(a.lo) + __builtin_popcountll(a.hi)), 0 }; return v; }\n"
                     << "static inline uint128_t __builtin_bswap128(uint128_t v) { uint128_t rv = { __builtin_bswap64(v.hi), __builtin_bswap64(v.lo) }; return rv; }\n"
                     << "static inline uint128_t intrinsic_ctlz_u128(uint128_t v) {\n"
                     << "\tuint128_t rv = { (uint64_t)(v.hi != 0 ? __builtin_clz64(v.hi) : (v.lo != 0 ? 64 + __builtin_clz64(v.lo) : 128)), 0 };\n"
                     << "\treturn rv;\n"
                     << "}\n"
                     << "static inline uint128_t intrinsic_cttz_u128(uint128_t v) {\n"
                     << "\tuint128_t rv = { (uint64_t)(v.lo == 0 ? (v.hi == 0 ? 128 : __builtin_ctz64(v.hi) + 64) : __builtin_ctz64(v.lo)), 0 };\n"
                     << "\treturn rv;\n"
                     << "}\n"
                     << "static inline int128_t make128s_raw(uint64_t hi, uint64_t lo) { int128_t rv = { lo, hi }; return rv; }\n"
                     << "static inline int128_t make128s(int64_t v) { int128_t rv = { (uint64_t)v, (v < 0 ? UINT64_MAX : 0) }; return rv; }\n"
                     << "static inline int128_t neg128s(int128_t v) { int128_t rv = { ~v.lo+1, ~v.hi + (v.lo == 0) }; return rv; }\n"
                     << "static inline float cast128s_float(int128_t v) {"
                     << " int sgn = (v.hi >> 63);"
                     << " int128_t abs = sgn ? neg128s(v) : v;"
                     << " return (sgn ? -1.0 : 1.0) * cast128_float(make128_raw(v.hi,v.lo));"
                     << " }\n"
                     << "static inline double cast128s_double(int128_t v) {"
                     << " int sgn = (v.hi >> 63);"
                     << " int128_t abs = sgn ? neg128s(v) : v;"
                     << " return (sgn ? -1.0 : 1.0) * cast128_double(make128_raw(v.hi,v.lo));"
                     << " }\n"
                     << "static inline int cmp128s(int128_t a, int128_t b) { if(a.hi != b.hi) return (int64_t)a.hi < (int64_t)b.hi ? -1 : 1; if(a.lo != b.lo) return a.lo < b.lo ? -1 : 1; return 0; }\n"
                     // Returns true if overflow happens (if negative with pos,pos or positive with neg,neg)
                     << "static inline bool add128s_o(int128_t a, int128_t b, int128_t* o) { bool sgna=a.hi>>63; bool sgnb=b.hi>>63; add128_o(*(uint128_t*)&a, *(uint128_t*)&b, (uint128_t*)o); bool sgno = o->hi>>63; return (sgna==sgnb && sgno != sgna); }\n"
                     // Returns true if overflow happens (if neg with pos,neg or pos with neg,pos)
                     << "static inline bool sub128s_o(int128_t a, int128_t b, int128_t* o) { bool sgna=a.hi>>63; bool sgnb=b.hi>>63; sub128_o(*(uint128_t*)&a, *(uint128_t*)&b, (uint128_t*)o); bool sgno = o->hi>>63; return (sgna!=sgnb && sgno != sgna); }\n"
                     << "static inline bool mul128s_o(int128_t a, int128_t b, int128_t* o) {"
                     << " bool sgna = (a.hi >> 63);"
                     << " bool sgnb = (b.hi >> 63);"
                     << " if(sgna) a = neg128s(a);"
                     << " if(sgnb) b = neg128s(b);"
                     << " bool rv = mul128_o(*(uint128_t*)&a, *(uint128_t*)&b, (uint128_t*)o);"
                     << " if(sgna != sgnb) *o = neg128s(*o);"
                     << " return rv;"
                     << " }\n"
                     << "static inline bool div128s_o(int128_t a, int128_t b, int128_t* q, int128_t* r) {"
                     << " bool sgna = (a.hi >> 63) != 0;"
                     << " bool sgnb = (b.hi >> 63) != 0;"
                     << " if(sgna) a = neg128s(a);"
                     << " if(sgnb) b = neg128s(b);"
                     << " bool rv = div128_o(*(uint128_t*)&a, *(uint128_t*)&b, (uint128_t*)q, (uint128_t*)r);"
                     << " if(sgna != sgnb && q) *q = neg128s(*q);"
                     << " if(sgna && r) *r = neg128s(*r);" // Remainder has the same sign as the dividend (a)
                     << " return rv;"
                     << " }\n"
                     << "static inline int128_t add128s(int128_t a, int128_t b) { int128_t v; add128s_o(a, b, &v); return v; }\n"
                     << "static inline int128_t sub128s(int128_t a, int128_t b) { int128_t v; sub128s_o(a, b, &v); return v; }\n"
                     << "static inline int128_t mul128s(int128_t a, int128_t b) { int128_t v; mul128s_o(a, b, &v); return v; }\n"
                     << "static inline int128_t div128s(int128_t a, int128_t b) { int128_t v; div128s_o(a, b, &v, NULL); return v; }\n"
                     << "static inline int128_t mod128s(int128_t a, int128_t b) { int128_t v; div128s_o(a, b, NULL, &v); return v; }\n"
                     << "static inline int128_t and128s(int128_t a, int128_t b) { int128_t v = { a.lo & b.lo, a.hi & b.hi }; return v; }\n"
                     << "static inline int128_t or128s (int128_t a, int128_t b) { int128_t v = { a.lo | b.lo, a.hi | b.hi }; return v; }\n"
                     << "static inline int128_t xor128s(int128_t a, int128_t b) { int128_t v = { a.lo ^ b.lo, a.hi ^ b.hi }; return v; }\n"
                     << "static inline int128_t shl128s(int128_t a, uint32_t b) { int128_t v; if(b == 0) { return a; } else if(b < 64) { v.lo = a.lo << b; v.hi = (a.hi << b) | (a.lo >> (64 - b)); } else { v.hi = a.lo << (b - 64); v.lo = 0; } return v; }\n"
                     << "static inline int128_t shr128s(int128_t a, uint32_t b) { int128_t v; if(b == 0) { return a; } else if(b < 64) { v.lo = (a.lo >> b)|(a.hi << (64 - b)); v.hi = (int64_t)a.hi >> b; } else { v.lo = (int64_t)a.hi >> (b - 64); v.hi = (int64_t)a.hi < 0 ? -1 : 0; } return v; }\n"
                     << "static inline uint128_t int128_to_uint128(int128_t a) { return make128_raw(a.lo, a.hi); }\n"
                     << "static inline int128_t uint128_to_int128(uint128_t a) { return make128s_raw(a.lo, a.hi); }\n";
            } else {
                // GCC-only
                of << "typedef unsigned __int128 uint128_t;\n"
                     << "typedef signed __int128 int128_t;\n"
                     << "static inline uint128_t __builtin_bswap128(uint128_t v) {\n"
                     << "\tuint64_t lo = __builtin_bswap64((uint64_t)v);\n"
                     << "\tuint64_t hi = __builtin_bswap64((uint64_t)(v>>64));\n"
                     << "\treturn ((uint128_t)lo << 64) | (uint128_t)hi;\n"
                     << "}\n"
                     << "static inline uint128_t intrinsic_ctlz_u128(uint128_t v) {\n"
                     << "\treturn (v == 0 ? 128 : (v >> 64 != 0 ? __builtin_clz64(v>>64) : 64 + __builtin_clz64(v)));\n"
                     << "}\n"
                     << "static inline uint128_t intrinsic_cttz_u128(uint128_t v) {\n"
                     << "\treturn (v == 0 ? 128 : ((v&0xFFFFFFFFFFFFFFFF) == 0 ? __builtin_ctz64(v>>64) + 64 : __builtin_ctz64(v)));\n"
                     << "}\n";
            }

            // Common helpers
            of << "\n"
                 << "static inline int slice_cmp(SLICE_PTR l, SLICE_PTR r) {\n"
                 << "\tint rv = memcmp(l.PTR, r.PTR, l.META < r.META ? l.META : r.META);\n"
                 << "\tif(rv != 0) return rv;\n"
                 << "\tif(l.META < r.META) return -1;\n"
                 << "\tif(l.META > r.META) return 1;\n"
                 << "\treturn 0;\n"
                 << "}\n"
                 << "static inline SLICE_PTR make_sliceptr(const void* ptr, size_t s) { SLICE_PTR rv = { (void*)ptr, s }; return rv; }\n"
                 << "static inline TRAITOBJ_PTR make_traitobjptr(const void* ptr, const void* vt) { TRAITOBJ_PTR rv = { (void*)ptr, (void*)vt }; return rv; }\n"
                 << "\n"
                 << "static inline size_t mrustc_max(size_t a, size_t b) { return a < b ? b : a; }\n"
                 << "static inline size_t mrustc_min(size_t a, size_t b) { return a < b ? a : b; }\n"
                 << "static inline void noop_drop(tUNIT *p) { }\n"
                 << "\n"
                 // A linear (fast-fail) search of a list of strings
                 << "static inline size_t mrustc_string_search_linear(SLICE_PTR val, size_t count, SLICE_PTR* options) {\n"
                 << "\tfor(size_t i = 0; i < count; i ++) {\n"
                 << "\t\tint cmp = slice_cmp(val, options[i]);\n"
                 << "\t\tif(cmp < 0) break;\n"
                 << "\t\tif(cmp == 0) return i;\n"
                 << "\t}\n"
                 << "\treturn SIZE_MAX;\n"
                 << "}\n"
                 // Map of reversed nibbles                       0  1  2  3  4  5  6  7   8  9 10 11 12 13 14 15
                 << "static const uint8_t __mrustc_revmap[16] = { 0, 8, 4,12, 2,10, 6,14,  1, 9, 5,13, 3,11, 7,15};\n"
                 << "static inline uint8_t __mrustc_bitrev8(uint8_t v) { if(v==0||v==0xFF) return v; return __mrustc_revmap[v>>4]|(__mrustc_revmap[v&15]<<4); }\n"
                 << "static inline uint16_t __mrustc_bitrev16(uint16_t v) { if(v==0) return 0; return ((uint16_t)__mrustc_bitrev8(v>>8))|((uint16_t)__mrustc_bitrev8(v)<<8); }\n"
                 << "static inline uint32_t __mrustc_bitrev32(uint32_t v) { if(v==0) return 0; return ((uint32_t)__mrustc_bitrev16(v>>16))|((uint32_t)__mrustc_bitrev16(v)<<16); }\n"
                 << "static inline uint64_t __mrustc_bitrev64(uint64_t v) { if(v==0) return 0; return ((uint64_t)__mrustc_bitrev32(v>>32))|((uint64_t)__mrustc_bitrev32(v)<<32); }\n"
                // TODO: 128
                ;
            if (options.emulatedI128) {
                of << "static inline uint128_t __mrustc_bitrev128(uint128_t v) { uint128_t rv = { __mrustc_bitrev64(v.hi), __mrustc_bitrev64(v.lo) }; return rv; }\n";
            } else {
                of << "static inline uint128_t __mrustc_bitrev128(uint128_t v) {"
                     << " if(v==0) return 0;"
                     << " uint128_t rv = ((uint128_t)__mrustc_bitrev64(v>>64))|((uint128_t)__mrustc_bitrev64(v)<<64);"
                     << " return rv;"
                     << " }\n";
            }
            for (int sz = 8; sz <= 64; sz *= 2) {
                of << "static inline uint" << sz << "_t __mrustc_op_umax" << sz << "(uint" << sz << "_t a, uint" << sz << "_t b) { return (a > b ? a : b); }\n"
                     << "static inline uint" << sz << "_t __mrustc_op_umin" << sz << "(uint" << sz << "_t a, uint" << sz << "_t b) { return (a < b ? a : b); }\n"
                     << "static inline uint" << sz << "_t __mrustc_op_imax" << sz << "(uint" << sz << "_t a, uint" << sz << "_t b) { return ((int" << sz << "_t)a > (int" << sz << "_t)b ? a : b); }\n"
                     << "static inline uint" << sz << "_t __mrustc_op_imin" << sz << "(uint" << sz << "_t a, uint" << sz << "_t b) { return ((int" << sz << "_t)a < (int" << sz << "_t)b ? a : b); }\n"
                     << "static inline uint" << sz << "_t __mrustc_op_and_not" << sz << "(uint" << sz << "_t a, uint" << sz << "_t b) { return ~(a & b); }\n";
            }

            // Float16 and Float128
            of << "typedef struct f16 { uint16_t v; } f16;\n"
                 << "static f16 f16_disabled(){ abort(); }\n"
                 << "static int f16_cmp(f16 a, f16 b){ abort(); }\n"
                 << "typedef struct f128 { uint128_t v; } f128;\n";
            if (options.emulatedI128) {
                of << "static inline f128 make_f128_bits(uint64_t hi, uint64_t lo) { f128 rv = { make128_raw(hi, lo) }; return rv; }\n";
            } else {
                of << "static inline f128 make_f128_bits(uint64_t hi, uint64_t lo) { f128 rv = { ((uint128_t)hi << 64) | lo }; return rv; }\n";
            }
            of << "static f128 f128_disabled(){ abort(); }\n"
                 << "static int f128_cmp(f128 a, f128 b){ abort(); }\n";
        }

        ~CodeGeneratorC() {
        }

        void finalise(const TransOptions& opt, CodegenOutput outTy, const ::std::string& hirFile) override {
            const bool createShims = (outTy == CodegenOutput::Executable);

            // TODO: Support dynamic libraries too
            // - No main, but has the rest.
            // - Well... for cdylibs that's the case, for rdylibs it's not
            if (outTy == CodegenOutput::Executable && !crate.noMain) {
                // TODO: Define this function in MIR?
                of << "}\n";
                of << "int main(int argc, const char* argv[]) {\n";
                auto cStartPath = mResolve.crate.getLangItemPathOpt("mrustc-start");
                if (cStartPath == ::HIR::SimplePath()) {
                    auto mainPath = crate.getLangItemPath(Span(), "mrustc-main");
                    const auto& mainFcn = crate.getFunctionByPath(sp, mainPath);

                    const auto& startPath = mResolve.crate.getLangItemPathOpt("start");
                    if (crate.isNoCore && startPath == ::HIR::SimplePath()) {
                        // A no_core binary has no standard entrypoint protocol.
                        // Call its ordinary main directly instead of inventing a
                        // `start` language item.
                        of << "\t" << TransMangle(::HIR::GenericPath(mainPath)) << "();\n";
                        of << "\treturn 0;\n";
                    } else {
                        auto startGpath = ::HIR::GenericPath(mResolve.crate.getLangItemPath(Span(), "start"));
                        startGpath.mParams.types.push_back(mainFcn.returnType);
                        of << "\treturn " << TransMangle(startGpath) << "(" << TransMangle(::HIR::GenericPath(mainPath)) << ", argc, (uint8_t**)argv";
                        of << ", 0"; // `sigpipe` setting
                        // 0: Default, 1: Inherit, 2: SIG_IGN, 3: SIG_DFL
                        of << ");\n";
                    }
                } else {
                    of << "\treturn " << TransMangle(::HIR::GenericPath(cStartPath)) << "(argc, (uint8_t**)argv);\n";
                }
                of << "}\n";
                of << "extern \"C\" {\n";
            }

            // Auto-generated code/items for the "root" rust binary (cdylib or executable)
            if (createShims) {
                // Allocator/panic shims
                {
                    const auto allocatorIt = crate.mLangItems.find(GLOBAL_ALLOCATOR_LANG_ITEM);
                    const bool hasGlobalAllocator = allocatorIt != crate.mLangItems.end();
                    const HIR::Static* globalAllocator = hasGlobalAllocator
                        ? &crate.getStaticByPath(Span(), allocatorIt->second)
                        : nullptr;
                    for (size_t i = 0; i < NUM_ALLOCATOR_METHODS; i++) {
                        struct H {
                            static void tyArgs(::std::vector<const char*>& out, AllocatorDataTy t) {
                                switch (t) {
                                    case AllocatorDataTy::Unit:
                                    case AllocatorDataTy::ResultPtr: // (..., *mut i8) + *mut u8
                                        throw "";
                                    // - Args
                                    case AllocatorDataTy::Layout: // usize, usize
                                        out.push_back("uintptr_t");
                                        out.push_back("uintptr_t");
                                        break;
                                    case AllocatorDataTy::Ptr: // *mut u8
                                        out.push_back("int8_t*");
                                        break;
                                    case AllocatorDataTy::Usize:
                                        out.push_back("uintptr_t");
                                        break;
                                }
                            }

                            static const char* tyRet(AllocatorDataTy t) {
                                switch (t) {
                                    case AllocatorDataTy::Unit:
                                        return "void";
                                    case AllocatorDataTy::ResultPtr: // (..., *mut i8) + *mut u8
                                        return "int8_t*";
                                    // - Args
                                    case AllocatorDataTy::Layout: // usize, usize
                                    case AllocatorDataTy::Ptr:    // *mut u8
                                    case AllocatorDataTy::Usize:
                                        throw "";
                                }
                                throw "";
                            }

                            static void emitProto(::std::ostream& os, const AllocatorMethod& method, const char* namePrefix, const ::std::vector<const char*>& args) {
                                os << H::tyRet(method.ret) << " " << namePrefix << method.name << "(";
                                for (size_t j = 0; j < args.size(); j++) {
                                    if (j != 0) {
                                        os << ", ";
                                    }
                                    os << args[j] << " a" << j;
                                }
                                os << ")";
                            }
                        };

                        const auto& method = ALLOCATOR_METHODS[i];
                        ::std::vector<const char*> args;
                        for (size_t j = 0; j < method.nArgs; j++) {
                            H::tyArgs(args, method.args[j]);
                        }
                        H::emitProto(of, method, "__rust_", args);
                        of << " {\n";
                        if (!hasGlobalAllocator) {
                            const char* allocPrefix = "__rdl_";
                            of << "\textern ";
                            H::emitProto(of, method, allocPrefix, args);
                            of << ";\n";
                            of << "\t";
                            if (method.ret != AllocatorDataTy::Unit) {
                                of << "return ";
                            }
                            of << allocPrefix << method.name << "(";
                            for (size_t j = 0; j < args.size(); j++) {
                                if (j != 0) {
                                    of << ", ";
                                }
                                of << "a" << j;
                            }
                            of << ");\n";
                        } else {
                            size_t flatArg = 0;
                            size_t layoutArg = 0;
                            for (size_t j = 0; j < method.nArgs; j++) {
                                switch (method.args[j]) {
                                    case AllocatorDataTy::Layout:
                                        of << "\tauto layout" << layoutArg << " = "
                                             << TransMangle(TransAllocatorLayoutCtorPath(crate))
                                             << "(a" << flatArg << ", a" << flatArg + 1 << ");\n";
                                        flatArg += 2;
                                        layoutArg += 1;
                                        break;
                                    case AllocatorDataTy::Ptr:
                                    case AllocatorDataTy::Usize:
                                        flatArg += 1;
                                        break;
                                    case AllocatorDataTy::Unit:
                                    case AllocatorDataTy::ResultPtr:
                                        throw "";
                                }
                            }

                            const auto methodPath = TransAllocatorMethodPath(crate, globalAllocator->mType, method);
                            const HIR::Path staticPath = HIR::GenericPath(allocatorIt->second);
                            of << "\t";
                            if (method.ret != AllocatorDataTy::Unit) {
                                of << "return reinterpret_cast<int8_t*>(";
                            }
                            of << TransMangle(methodPath) << "(&" << TransMangle(staticPath) << ".val";
                            flatArg = 0;
                            layoutArg = 0;
                            for (size_t j = 0; j < method.nArgs; j++) {
                                of << ", ";
                                switch (method.args[j]) {
                                    case AllocatorDataTy::Layout:
                                        of << "layout" << layoutArg;
                                        flatArg += 2;
                                        layoutArg += 1;
                                        break;
                                    case AllocatorDataTy::Ptr:
                                        of << "reinterpret_cast<uint8_t*>(a" << flatArg << ")";
                                        flatArg += 1;
                                        break;
                                    case AllocatorDataTy::Usize:
                                        of << "a" << flatArg;
                                        flatArg += 1;
                                        break;
                                    case AllocatorDataTy::Unit:
                                    case AllocatorDataTy::ResultPtr:
                                        throw "";
                                }
                            }
                            of << ")";
                            if (method.ret != AllocatorDataTy::Unit) {
                                of << ")";
                            }
                            of << ";\n";
                        }
                        of << "}\n";
                    }

                    of << "void __rust_no_alloc_shim_is_unstable_v2() {}\n";

                    {
                        auto oomMethod = crate.getLangItemPathOpt("mrustc-alloc_error_handler");
                        of << "uint8_t __rust_alloc_error_handler_should_panic = 0;\n";
                        of << "uint8_t __rust_no_alloc_shim_is_unstable = 0;\n";

                        auto layoutPath = ::HIR::SimplePath("core", {"alloc", "Layout"});
                        if (oomMethod != HIR::SimplePath()) {
                            of << "struct s_" << TransMangle(layoutPath) << "_A { uintptr_t a, b; };\n";
                            of << "void oom_impl(struct s_" << TransMangle(layoutPath) << "_A l) {"
                                 << " extern void " << TransMangle(oomMethod) << "(struct s_" << TransMangle(layoutPath) << "_A l);"
                                 << " " << TransMangle(oomMethod) << "(l);"
                                 << " }\n";
                        }

                        // Force abort on alloc error, rustc uses `-Zoom={panic,abort}` to select this
                        of << "uint8_t __rust_alloc_error_handler_should_panic_v2() { return 0; }";
                        of << "void __rust_alloc_error_handler(uintptr_t s, uintptr_t a) {\n";
                        if (oomMethod == HIR::SimplePath()) {
                            of << "\tvoid __rdl_oom(uintptr_t, uintptr_t);\n";
                            of << "\t__rdl_oom(s,a);\n";
                        } else {
                            of << "\tstruct s_" << TransMangle(layoutPath) << "_A v = { s, a };\n";
                            of << "\toom_impl(v);\n";
                        }
                        of << "}\n";
                    }
                }

                {
                    // Bind `panic_impl` only when this crate actually provides
                    // a panic implementation. A no_core binary without one can
                    // still be valid when no generated code uses it.
                    const auto& panicImplPath = crate.getLangItemPathOpt("mrustc-panic_implementation");
                    if (panicImplPath != ::HIR::SimplePath()) {
                        of << "uint32_t panic_impl(uintptr_t payload) {";
                        of << "extern uint32_t " << TransMangle(panicImplPath) << "(uintptr_t payload);";
                        of << "return " << TransMangle(panicImplPath) << "(payload);";
                        of << "}\n";
                    } else if (!crate.isNoCore) {
                        crate.getLangItemPath(Span(), "mrustc-panic_implementation");
                    }
                }
            }

            of << "}\n";
            of.flush();
            of.close();
            ASSERT_BUG(Span(), !of.bad(), "Error set on output stream for: " << outfilePathC);

            class LinkList: private StringList {
            public:
                enum class Ty {
                    //Border,   // --{push,pop}-state
                    Directory, // -L <value>
                    Explicit,  // <value>
                    Implicit,  // -l <value>
                };

            private:
                std::vector<Ty> mTy;

            public:
                void pushDir(const char* s) {
#if 1
                    // Don't de-dup since there's the push/pop rules
                    auto it = ::std::find_if(StringList::begin(), StringList::end(), [&](const char* es) {
                        return ::std::strcmp(es, s) == 0;
                    });
                    if (it != StringList::end()) {
                        return;
                    }
#endif
                    mTy.push_back(Ty::Directory);
                    this->push_back(s);
                }

                void pushExplicit(std::string s) {
                    mTy.push_back(Ty::Explicit);
                    this->push_back(std::move(s));
                }

                void pushLib(const char* s) {
                    if (mTy.size() > 0 && mTy.back() == Ty::Implicit && std::strcmp(this->getVec().back(), s) == 0) {
                        return;
                    }
                    mTy.push_back(Ty::Implicit);
                    this->push_back(s);
                }

                void pushLib(std::string s) {
                    if (mTy.size() > 0 && mTy.back() == Ty::Implicit && s == this->getVec().back()) {
                        return;
                    }
                    mTy.push_back(Ty::Implicit);
                    this->push_back(std::move(s));
                }

                void pushBorder() {
// If the previous is also a marker, don't push
                }

                class iterator {
                    const LinkList& parent;
                    size_t idx;

                public:
                    iterator(const LinkList& parent, size_t idx)
                        : parent(parent)
                        , idx(idx)
                    {
                    }

                    void operator++() {
                        this->idx++;
                    }

                    bool operator!=(const iterator& x) {
                        return this->idx != x.idx;
                    }

                    std::pair<Ty, const char*> operator*() const {
                        return std::make_pair(parent.mTy[idx], parent.getVec()[idx]);
                    }
                };

                iterator begin() const {
                    return iterator(*this, 0);
                }

                iterator end() const {
                    return iterator(*this, this->getVec().size());
                }
            };

            // Combined list to ensure a sane resolution order?
            LinkList librariesAndDirs;

            StringList extCrates;
            StringList extCratesDylib;
            switch (outTy) {
                case CodegenOutput::Executable:
                case CodegenOutput::DynamicLibrary:
                    for (const auto& crateName : crate.extCratesOrdered) {
                        const auto& extCrate = crate.extCrates.at(crateName);
                        auto isDylib = [](const ::HIR::ExternCrate& c) {
                            bool rv = false;
                            // TODO: Better rule than this
                            rv |= (c.mPath.compare(c.mPath.size() - 3, 3, ".so") == 0);
                            return rv;
                        };
                        // If this crate is included in a dylib crate, ignore it
                        bool isInDylib = false;
                        for (const auto& crate2 : crate.extCrates) {
                            if (isDylib(crate2.second)) {
                                for (const auto& subcrate : crate2.second.mData->extCrates) {
                                    if (subcrate.second.mPath == extCrate.mPath) {
                                        DEBUG(crateName << " referenced by dylib " << crate2.first);
                                        isInDylib = true;
                                    }
                                }
                            }
                            if (isInDylib) {
                                break;
                            }
                        }
                        // NOTE: Only exclude non-dylibs referenced by other dylibs
                        if (isInDylib && !isDylib(extCrate)) {
                            continue;
                        }

                        // Ignore panic crates unless they're the selected crate (and add in the selected panic crate)
                        if (extCrate.mData->mLangItems.count("mrustc-panic_runtime")) {
                            // Check if this is the requested panic crate
                            if (strncmp(crateName.c_str(), opt.panicCrate.c_str(), opt.panicCrate.size()) != 0) {
                                DEBUG("Ignore not-selected panic crate: " << crateName);
                                continue;
                            } else {
                                DEBUG("Keep panic crate: " << crateName);
                            }
                        }

                        if (extCrate.mPath.compare(extCrate.mPath.size() - 5, 5, ".rlib") == 0) {
                            extCrates.push_back(extCrate.mPath.c_str());
                        } else if (isDylib(extCrate)) {
                            extCratesDylib.push_back(extCrate.mPath.c_str());
                        } else {
                            // Probably a procedural macro, ignore it
                        }
                    }

                    struct H {
                        static bool file_exists(const std::string& path) {
                            return std::ifstream(path).is_open();
                        }

                        static std::string findLibraryOne(const std::string& path, const std::string& name) {
                            std::string libPath;
                            libPath = FMT(path << "/lib" << name << ".so");
                            if (file_exists(libPath)) {
                                return libPath;
                            }
                            libPath = FMT(path << "/lib" << name << ".a");
                            if (file_exists(libPath)) {
                                return libPath;
                            }
                            return "";
                        }

                        static std::string findLibrary(const std::vector<std::string>& paths1, const std::vector<std::string>& paths2, const std::string& name) {
                            std::string rv;
                            for (const auto& p : paths1) {
                                if ((rv = findLibraryOne(p, name)) != "") {
                                    return rv;
                                }
                            }
                            for (const auto& p : paths2) {
                                if ((rv = findLibraryOne(p, name)) != "") {
                                    return rv;
                                }
                            }
                            return "";
                        }
                    };

                    for (const auto& path : opt.librarySearchDirs) {
                        librariesAndDirs.pushDir(path.c_str());
                    }
                    for (const auto& path : opt.libraries) {
                        librariesAndDirs.pushLib(path.c_str());
                    }
                    librariesAndDirs.pushBorder();

                    for (const auto& path : crate.linkPaths) {
                        librariesAndDirs.pushDir(path.c_str());
                    }
                    for (const auto& lib : crate.extLibs) {
                        ASSERT_BUG(Span(), lib.name != "", "");
                        librariesAndDirs.pushLib(lib.name.c_str());
                    }

                    for (const auto& crateName : crate.extCratesOrdered) {
                        const auto& extCrate = crate.extCrates.at(crateName);
                        if (!extCrate.mData->extLibs.empty() || !extCrate.mData->linkPaths.empty()) {
                            librariesAndDirs.pushBorder();
                        }
                        for (const auto& path : extCrate.mData->linkPaths) {
                            librariesAndDirs.pushDir(path.c_str());
                        }
                        // NOTE: Does explicit lookup, to provide scoped search directories
                        // - Needed for 1.39 cargo on linux when libgit2 and libz exist on the system, butsystem libgit2 isn't new enough
                        for (const auto& lib : extCrate.mData->extLibs) {
                            ASSERT_BUG(Span(), lib.name != "", "Empty lib from " << crateName);
                            auto path = H::findLibrary(extCrate.mData->linkPaths, opt.librarySearchDirs, lib.name);
                            if (path != "") {
                                librariesAndDirs.pushExplicit(std::move(path));
                            } else {
                                librariesAndDirs.pushLib(lib.name.c_str());
                            }
                        }
                    }
                    break;
                case CodegenOutput::Object:
                case CodegenOutput::StaticLibrary:
                    break;
            }

            // Execute $CC with the required libraries
            StringList args;
            size_t argFileStart = 0;
            // Pick the C++ compiler.
            {
                std::string varname = "CXX_" + TargetGetCurSpec().backendC.cCompiler;
                std::replace(varname.begin(), varname.end(), '-', '_');

                if (getenv(varname.c_str())) {
                    args.push_back(getenv(varname.c_str()));
                } else if (getenv("CXX")) {
                    args.push_back(getenv("CXX"));
                } else if (system(("command -v " + TargetGetCurSpec().backendC.cCompiler + "-g++" + " >/dev/null 2>&1").c_str()) == 0) {
                    args.push_back(TargetGetCurSpec().backendC.cCompiler + "-g++");
                } else {
                    args.push_back("g++");
                }
            }
            argFileStart = args.getVec().size();
            args.push_back("-std=gnu++20");
            args.push_back("-fexceptions");
            for (const auto& a : TargetGetCurSpec().backendC.compilerOpts) {
                args.push_back(a.c_str());
            }
            switch (opt.optLevel) {
                case OptimizationLevel::None:
                    // Do not inherit an optimisation level from the C compiler's
                    // environment (e.g. Nix's cc-wrapper adds -O2). rustc's
                    // default is opt-level=0, so the C backend must request that
                    // level explicitly too.
                    args.push_back("-O0");
                    break;
                case OptimizationLevel::Less:
                    args.push_back("-O1");
                    break;
                case OptimizationLevel::More:
                case OptimizationLevel::Aggressive:
                    //args.push_back("-O2");
                    args.push_back("-O1"); // HACK: Work around mrustc #347 by reducing the optimisation level
                    break;
                case OptimizationLevel::Size:
                    args.push_back("-Os");
                    break;
                case OptimizationLevel::SizeMin:
                    args.push_back("-Oz");
                    break;
            }
#if defined(__GNUC__) && !defined(__clang__)
    #if __GNUC__ < 16 && !(__GNUC__ == 15 && __GNUC_MINOR__ > 1)
            // HACK: Work around [https://gcc.gnu.org/bugzilla/show_bug.cgi?id=117423] by disabling an optimisation stage
            if (opt.optLevel != OptimizationLevel::None) {
                args.push_back("-fno-tree-sra");
            }
    #endif
#endif
            switch (opt.debugInfo) {
                case DebugInfoLevel::None:
                    break;
                case DebugInfoLevel::LineDirectivesOnly:
                case DebugInfoLevel::LineTablesOnly:
                case DebugInfoLevel::Limited:
                    args.push_back("-g1");
                    break;
                case DebugInfoLevel::Full:
                    args.push_back("-g");
                    break;
            }
            // TODO: Why?
            args.push_back("-fPIC");
            args.push_back("-o");
            switch (outTy) {
                case CodegenOutput::DynamicLibrary:
                case CodegenOutput::Executable:
                case CodegenOutput::Object:
                    args.push_back(outfilePath.c_str());
                    break;
                case CodegenOutput::StaticLibrary:
                    args.push_back(outfilePath + ".o");
                    break;
            }
            args.push_back(outfilePathC.c_str());
            switch (outTy) {
                case CodegenOutput::DynamicLibrary:
                    args.push_back("-shared");
                case CodegenOutput::Executable:
                    for (const auto& a : TargetGetCurSpec().backendC.linkerOptsPre) {
                        args.push_back(a.c_str());
                    }
                    for (const auto& c : extCrates) {
                        args.push_back(std::string(c) + ".o");
                    }
                    for (const auto& c : extCratesDylib) {
                        args.push_back(c);
                    }
                    for (auto lD : librariesAndDirs) {
                        switch (lD.first) {
                            case LinkList::Ty::Directory:
                                args.push_back("-L");
                                args.push_back(lD.second);
                                break;
                            case LinkList::Ty::Implicit:
                                if (!strncmp(lD.second, "framework=", strlen("framework="))) {
                                    args.push_back("-framework");
                                    args.push_back(lD.second + strlen("framework="));
                                } else {
                                    args.push_back("-l");
                                    args.push_back(lD.second);
                                }
                                break;
                            case LinkList::Ty::Explicit:
                                args.push_back(lD.second);
                                break;
                        }
                    }
                    for (const auto& a : TargetGetCurSpec().backendC.linkerOptsPost) {
                        args.push_back(a.c_str());
                    }
                    for (const auto& a : opt.linkerArgs) {
                        args.push_back(a.c_str());
                    }
                    // TODO: Include the HIR file as a magic object?
                    break;
                case CodegenOutput::StaticLibrary:
                case CodegenOutput::Object:
                    args.push_back("-c");
                    break;
            }

            ::std::stringstream cmdSs;
            std::string commandFile = outfilePath + "_cmd.txt";
            std::ofstream commandFileStream;
            if (getenv("MRUSTC_CCACHE")) {
                cmdSs << "ccache ";
            }
            bool useArgFile = argFileStart > 0;
            if (useArgFile) {
                commandFileStream.open(commandFile);
                ASSERT_BUG(Span(), commandFileStream.is_open(), "Failed to open command file `" << commandFile << "` for writing");
            }
            size_t i = -1;
            for (const auto& arg : args.getVec()) {
                i++;
                auto& outSs = (useArgFile && i >= argFileStart ? static_cast<::std::ostream&>(commandFileStream) : cmdSs);
                outSs << "\"" << FmtShell(arg) << "\" ";
            }
            if (useArgFile) {
                cmdSs << "@\"" << FmtShell(commandFile) << "\"";
                commandFileStream.close();
                ASSERT_BUG(Span(), !commandFileStream.bad(), "Error set on output stream for: " << outfilePathC);
            }
            //DEBUG("- " << cmd_ss.str());
            ::std::cout << "Running command - " << cmdSs.str() << ::std::endl;
            if (opt.buildCommandFile != "") {
                ::std::cerr << "INVOKE CC: " << cmdSs.str() << ::std::endl;
                ::std::ofstream(opt.buildCommandFile) << cmdSs.str() << ::std::endl;
            } else {
                int ec = system(cmdSs.str().c_str());
                if (ec == -1) {
                    ::std::cerr << "C Compiler failed to execute (system returned -1)" << ::std::endl;
                    perror("system");
                    exit(1);
                } else if (ec != 0) {
                    ::std::cerr << "C Compiler failed to execute - error code " << ec << ::std::endl;
                    exit(1);
                }
            }

            // Custom Cargo treats `.rlib` as the metadata/completion marker and links the sibling object file.
            if (outTy == CodegenOutput::StaticLibrary) {
                ::std::ofstream of(outfilePath);
                if (!of.good()) {
                    // TODO: Error?
                }
            }
        }

        void emitBoxDrop(unsigned indentLevel, const ::HIR::TypeData* innerType, const ::HIR::TypeData* boxType, const ::MIR::LValue& slot, bool runDestructor) {
            auto indent = RepeatLitStr{"\t", static_cast<int>(indentLevel)};
            if (runDestructor) {
                auto innerPtr = ::MIR::LValue::newField(::MIR::LValue::newField(::MIR::LValue::newField(slot.clone(), 0), 0), 0);
                emitDestructorCall(::MIR::LValue::newDeref(mv$(innerPtr)), innerType, /*unsized_valid=*/true, indentLevel);
            }

            auto p = ::HIR::Path(boxType, crate.getLangItemPath(Span(), "drop"), "drop");
            of << indent << TransMangle(p) << "(&";
            emitLvalue(slot);
            of << ");\n";

            // The pointee is a synthetic Box move-path, not a physical field. A shallow
            // drop skips that path, but still drops the real fields after Box::drop.
            const auto* repr = TargetGetTypeRepr(sp, mResolve, boxType);
            MIR_ASSERT(*mirRes, repr, "No repr for Box " << boxType);
            auto field = ::MIR::LValue::newField(slot.clone(), 0);
            for (const auto& fieldRepr : repr->fields) {
                if (mResolve.typeNeedsDropGlue(sp, fieldRepr.ty)) {
                    emitDestructorCall(field, fieldRepr.ty, /*unsized_valid=*/false, indentLevel);
                }
                field.incField();
            }
        }

        void emitGlobalAsm(const ::HIR::GlobalAssembly& se) override {
            of << "__asm__ (\"";
            if ((TargetGetCurSpec().arch.mName == "x86" || TargetGetCurSpec().arch.mName == "x86_64") && !se.options.attSyntax) {
                of << ".intel_syntax noprefix; ";
            }
            for (const auto& l : se.lines) {
                for (const auto& f : l.frags) {
                    of << FmtGccAsm(f.before, false);
                    ASSERT_BUG(Span(), f.index < se.symbols.size(), "Invalid argument reference in global assembly");
                    TODO(Span(), "Handle interpolation in global_asm! - " << se.symbols[f.index]);
                }
                of << FmtGccAsm(l.trailing, false);
                of << ";\\n ";
            }
            if ((TargetGetCurSpec().arch.mName == "x86" || TargetGetCurSpec().arch.mName == "x86_64") && !se.options.attSyntax) {
                of << ".att_syntax; ";
            }
            of << "\");\n";
        }

        void emitTypeId(const ::HIR::TypeData* ty) override {
            of << "tTYPEID __typeid_" << TransMangle(ty) << " __attribute__((weak));\n";

        }

        void emitTypeProto(const ::HIR::TypeData* ty) override {
            TRACE_FUNCTION_F(ty);
            TU_MATCH_HDRA( (*ty), {)
            default:
                // No prototype required
            TU_ARMA(Tuple, te) {
                    if (te.size() > 0) {
                        of << "typedef struct ";
                        emitCtype(ty);
                        of << " ";
                        emitCtype(ty);
                        of << ";\n";
                    }
                }
                TU_ARMA(Function, te) {
                    emitTypeFn(ty);
                    of << "\n";
                }
                TU_ARMA(NamedFunction, te) {
                    of << "typedef struct ";
                    emitCtype(ty);
                    of << " ";
                    emitCtype(ty);
                    of << ";\n";
                }
                TU_ARMA(Array, te) {
                    of << "typedef struct ";
                    emitCtype(ty);
                    of << " ";
                    emitCtype(ty);
                    of << ";\n";
                }
                TU_ARMA(Path, te) {
                TU_MATCH_HDRA( (te.binding), {)
                TU_ARMA(Unbound, tpb) throw "";
                        TU_ARMA(Opaque, tpb) throw "";
                        TU_ARMA(Struct, tpb) {
                            of << "struct s_" << TransMangle(te.path) << ";\n";
                        }
                        TU_ARMA(ExternType, tpb) {
                            of << "struct x_" << TransMangle(te.path) << ";\n";
                        }
                        TU_ARMA(Union, tpb) {
                            of << "union u_" << TransMangle(te.path) << ";\n";
                        }
                        TU_ARMA(Enum, tpb) {
                            of << "struct e_" << TransMangle(te.path) << ";\n";
                        }
                }
                }
                TU_ARMA(ErasedType, te) {
                    // TODO: Is this actually a bug?
                    return;
                }
            }
        }

        void emitTypeFn(const ::HIR::TypeData* ty) {
            if (emittedFnTypes.count(ty)) {
                return;
            }
            emittedFnTypes.insert(ty);

            const auto& te = ty->as_Function();
            of << "typedef ";
            // TODO: ABI marker, need an ABI enum?
            if (te.mRettype == crate.types.unit()) {
                of << "void";
            } else {
                // TODO: Better emit_ctype call for return type?
                emitCtype(te.mRettype);
            }
            of << " (";
            of << "*";
            emitCtype(ty);
            of << ")(";
            if (te.argTypes.size() == 0) {
                of << "void)";
            } else {
                for (unsigned int i = 0; i < te.argTypes.size(); i++) {
                    if (i != 0) {
                        of << ",";
                    }
                    of << " ";
                    this->emitCtype(te.argTypes[i]);
                }
                if (te.isVariadic) {
                    of << ", ...";
                }
                of << " )";
            }
            of << ";";
        }

        // Shared logic between `emit_struct` and `emit_type` (w/ Tuple)
        void emitStructInner(const ::HIR::TypeData* ty, const TypeRepr* repr, unsigned packingMaxAlign) {
            // Fill `fields` with ascending indexes (for sorting)
            // AND: Determine if the type has a a zero-sized item that has an alignment equal to the structure's alignment
            ::std::vector<unsigned> fields;
            fields.reserve(repr->fields.size());
            ::std::vector<bool> zsts;
            zsts.reserve(repr->fields.size());
            size_t maxAlign = 0;
            // `max_align` is the largest natural field alignment; `c_max_align` is what the C compiler will derive for the emitted struct.
            size_t cMaxAlign = 0;
            bool hasManualAlign = false;
            for (const auto& ent : repr->fields) {
                const auto& ty = ent.ty;

                size_t sz = -1, al = 0;
                TargetGetSizeAndAlignOf(sp, mResolve, ty, sz, al);
                if (sz == 0 && al == repr->align && al > 0) {
                    hasManualAlign = true;
                }
                maxAlign = std::max(maxAlign, al);
                // Track what C will derive separately - under a capping ABI an interior over-aligned member doesn't raise it
                {
                    size_t alC = al;
                    if (TargetCapsMemberAlignment() && sz > 0 && ent.offset != 0 && alC > 4 && !TargetTypeHasUserAlignment(sp, mResolve, ty)) {
                        alC = 4;
                    }
                    cMaxAlign = std::max(cMaxAlign, alC);
                }

                fields.push_back(fields.size());
                zsts.push_back(sz == 0);
            }
            if (packingMaxAlign == 0 && cMaxAlign != repr->align /*&& repr->size > 0*/) {
                hasManualAlign = true;
            }
            // An align-1 type must be emitted packed - gcc takes a container's alignment from the member's natural alignment
            if (packingMaxAlign == 0 && !hasManualAlign && repr->align == 1 && repr->size > 1) {
                packingMaxAlign = 1;
            }
            // - Sort the fields by offset
            ::std::sort(fields.begin(), fields.end(), [&](auto a, auto b) {
                if (repr->fields[a].offset == repr->fields[b].offset) {
                    return !zsts[a] < !zsts[b]; // Sort zero sized fields first (!zst means size is 1+)
                }
                return repr->fields[a].offset < repr->fields[b].offset;
            });

            // For repr(packed), mark as packed
            if (packingMaxAlign) {
                of << "#pragma pack(push, " << packingMaxAlign << ")\n";
            }
            if (ty->is_Tuple()) {
                of << "typedef ";
                of << "struct ";
            }
            emitCtype(ty);
            of << " {\n";

            bool hasUnsized = false;
            size_t sizedFields = 0;
            size_t curOfs = 0;
            bool isFirstField = true;
            for (unsigned fld : fields) {
                const auto& ty = repr->fields[fld].ty;
                const auto offset = repr->fields[fld].offset;
                size_t s = 0, a;
                TargetGetSizeAndAlignOf(sp, mResolve, ty, s, a);
                DEBUG("@" << offset << ": " << ty << " " << s << "," << a);

                // Check offset/alignment
                if (s == SIZE_MAX) {
                } else if (s == 0) {
                } else {
                    MIR_ASSERT(*mirRes, curOfs <= offset, "Current offset is already past expected (#" << fld << "): " << curOfs << " > " << offset);
                    auto fieldAlign = a;
                    // PowerPC 32-bit ABI alignment
                    if (TargetGetCurSpec().arch.mName == "powerpc") {
                        if (s > 0) {
                            if (!isFirstField && fieldAlign >= 4 && fieldAlign <= 8) {
                                fieldAlign = 4;
                            }
                            isFirstField = false;
                        }
                    }
                    a = packingMaxAlign > 0 ? std::min<size_t>(packingMaxAlign, fieldAlign) : fieldAlign;
                    DEBUG("a = " << a);
                    while (curOfs % a != 0) {
                        curOfs++;
                    }
                }

                // Inject padding
                if (curOfs < offset) {
                    auto n = offset - curOfs;
                    of << "\tuint8_t _padding" << fld << "[" << n << "];\n";
                    curOfs += n;
                }
                MIR_ASSERT(*mirRes, curOfs == offset, "Current offset doesn't match expected (#" << fld << "): " << curOfs << " != " << offset);

                of << "\t";
                of << "/*@" << offset << "*/";
                if (const auto* te = ty->opt_Slice()) {
                    emitCtype(te->inner, FMT_CB(ss, ss << "_" << fld << "[0]";));
                    hasUnsized = true;
                } else if (ty->is_TraitObject()) {
                    of << "unsigned char _" << fld << "[0]";
                    hasUnsized = true;
                } else if (ty == ::HIR::CoreType::Str) {
                    of << "uint8_t _" << fld << "[0]";
                    hasUnsized = true;
                } else if (TU_TEST1(*ty, Path, .binding.is_ExternType())) {
                    of << "// External";
                    hasUnsized = true;
                } else {
                    if (s == 0 && options.disallowEmptyStructs) {
                        of << "// ZST";
                    } else {
                        // TODO: Nested unsized?
                        emitCtype(ty, FMT_CB(ss, ss << "_" << fld));
                        sizedFields++;

                        hasUnsized |= (s == SIZE_MAX);
                    }
                }
                of << "; // " << ty << "\n";

                curOfs += s;
            }
            if (sizedFields == 0 && !hasUnsized && options.disallowEmptyStructs) {
                of << "\tchar _d;\n";
            }
            of << "}";
            if (hasManualAlign) {
                of << " __attribute__((__aligned__(" << repr->align << ")))";
                of << " ";
                if (ty->is_Tuple()) {
                    emitCtype(ty);
                }
                of << ";\n";

            } else {
                of << " ";
                if (ty->is_Tuple()) {
                    emitCtype(ty);
                }
                of << ";\n";
            }
            if (packingMaxAlign != 0) {
                of << "#pragma pack(pop)\n";
            }
        }

        void emitType(const ::HIR::TypeData* ty) override {
            ::MIR::Function emptyFcn;
            ::MIR::TypeResolve topMirRes {
                sp, mResolve, FMT_CB(ss, ss << "type " << ty;), ::HIR::TypeRef(), {}, emptyFcn
            };
            mirRes = &topMirRes;

            TRACE_FUNCTION_F(ty);
            TU_MATCH_HDRA( (*ty), { )
            default:
                // Nothing to emit
                break;
                TU_ARMA(Tuple, te) {
                    if (te.size() > 0) {
                        of << " // " << ty << "\n";
                        const auto* repr = TargetGetTypeRepr(sp, mResolve, ty);

                        emitStructInner(ty, repr, /*packing_max_align=*/0);

                        if (repr->size > 0 && repr->size != SIZE_MAX) {
                            of << "typedef char sizeof_assert_";
                            emitCtype(ty);
                            of << "[ (sizeof(";
                            emitCtype(ty);
                            of << ") == " << repr->size << ") ? 1 : -1 ];\n";
                        }
                    }
                }
                TU_ARMA(Function, te) {
                    emitTypeFn(ty);
                    of << " // " << ty << "\n";
                }
                TU_ARMA(NamedFunction, te) {
                    of << "typedef struct ";
                    emitCtype(ty);
                    of << " {";
                    if (options.disallowEmptyStructs) {
                        of << " char _unused; ";
                    }
                    of << "} ";
                    emitCtype(ty);
                    of << ";\n";
                }
                TU_ARMA(Array, te) {
                    size_t rustSize;
                    ASSERT_BUG(sp, TargetGetSizeOf(sp, mResolve, ty, rustSize), "Unable to determine array size for " << ty);
                    const bool isZeroSized = rustSize == 0;

                    of << "typedef ";
                    size_t align;
                    if (isZeroSized) {
                        TargetGetAlignOf(sp, mResolve, ty, align);

                    }
                    of << "struct ";
                    emitCtype(ty);
                    of << " { ";
                    if (isZeroSized && options.disallowEmptyStructs) {
                        of << "char _d;";
                    } else if (isZeroSized) {
                        if (te.size.as_Known() > 0) {
                            emitCtype(te.inner);
                            of << " DATA[1];";
                        }
                    }
                    else {
                        emitCtype(te.inner);
                        of << " DATA[" << te.size.as_Known() << "];";
                    }
                    of << " } ";
                    if (isZeroSized) {
                        of << " __attribute__((";
                        of << "__aligned__(" << align << "),";
                        of << "))";

                    }
                    emitCtype(ty);
                    of << ";";
                    of << " // " << ty << "\n";
                }
                TU_ARMA(ErasedType, te) {
                    // TODO: Is this actually a bug?
                    return;
                }
            }

            mirRes = nullptr;
        }

        void emitStruct(const Span& sp, const ::HIR::GenericPath& p, const ::HIR::Struct& item) override {
            ::MIR::Function emptyFcn;
            ::MIR::TypeResolve topMirRes {
                sp, mResolve, FMT_CB(ss, ss << "struct " << p;), ::HIR::TypeRef(), {}, emptyFcn
            };
            mirRes = &topMirRes;
            // TODO: repr(transparent) and repr(align(foo))

            TRACE_FUNCTION_F(p);
            auto itemTy = crate.types.path(p.clone(), ::HIR::TypePathBinding::make_Struct(&item));
            const auto* repr = TargetGetTypeRepr(sp, mResolve, itemTy);
            MIR_ASSERT(*mirRes, repr, "No repr for struct " << p);

            of << "// struct " << p << "\n";

            emitStructInner(itemTy, repr, item.maxFieldAlignment);

            if (repr->size > 0 && repr->size != SIZE_MAX) {
                // TODO: Handle unsized (should check the size of the fixed-size region)
                of << "typedef char sizeof_assert_" << TransMangle(p) << "[ (sizeof(struct s_" << TransMangle(p) << ") == " << repr->size << ") ? 1 : -1 ];\n";
            }
            of << "typedef char alignof_assert_" << TransMangle(p) << "[ (ALIGNOF(struct s_" << TransMangle(p) << ") == " << repr->align << ") ? 1 : -1 ];\n";

            mirRes = nullptr;
        }

        void emitUnion(const Span& sp, const ::HIR::GenericPath& p, const ::HIR::Union& item) override {
            ::MIR::Function emptyFcn;
            ::MIR::TypeResolve topMirRes {
                sp, mResolve, FMT_CB(ss, ss << "union " << p;), ::HIR::TypeRef(), {}, emptyFcn
            };
            mirRes = &topMirRes;

            TRACE_FUNCTION_F(p);
            auto itemTy = crate.types.path(p.clone(), ::HIR::TypePathBinding::make_Union(&item));
            const auto* repr = TargetGetTypeRepr(sp, mResolve, itemTy);
            MIR_ASSERT(*mirRes, repr != nullptr, "No repr for union " << itemTy);

            of << "union u_" << TransMangle(p) << " {\n";
            for (unsigned int i = 0; i < repr->fields.size(); i++) {
                assert(repr->fields[i].offset == 0);
                of << "\t";
                emitCtype(repr->fields[i].ty, FMT_CB(ss, ss << "var_" << i;));
                of << ";\n";
            }
            of << "}";
            // Pin union alignment - under the power ABI gcc takes a union's alignment from its *first* member
            if (repr->align > 0) {
                of << " __attribute__((__aligned__(" << repr->align << ")))";
            }
            of << ";\n";
            if (true && repr->size > 0) {
                of << "typedef char sizeof_assert_" << TransMangle(p) << "[ (sizeof(union u_" << TransMangle(p) << ") == " << repr->size << ") ? 1 : -1 ];\n";
            }

            mirRes = nullptr;
        }

        bool isEnumTag(const TypeRepr* repr, size_t idx) {
            if (const auto* ve = repr->variants.opt_Values()) {
                return ve->isTag(idx);
            }
            if (const auto* ve = repr->variants.opt_Linear()) {
                return ve->isTag(idx);
            }
            return false;
        }

        const HIR::TypeData* emitEnumPath(const TypeRepr* repr, const TypeRepr::FieldPath& path) {
            if (isEnumTag(repr, path.index)) {
                // Some enums have the tag outside, some inside
                if (embeddedTags.count(repr)) {
                    of << ".DATA";
                }
                of << ".TAG";
                assert(path.subFields.empty());
            } else {
                of << ".DATA.var_" << path.index;
            }
            const auto* ty = &repr->fields[path.index].ty;
            for (const auto& fld : path.subFields) {
                if (fld == TypeRepr::FieldPath::ARRAY_ELEMENT) {
                    const auto* array = (*ty)->opt_Array();
                    assert(array && array->size.is_Known() && array->size.as_Known() > 0);
                    of << ".DATA[0]";
                    ty = &array->inner;
                    continue;
                }
                repr = TargetGetTypeRepr(sp, mResolve, *ty);
                if (isEnumTag(repr, fld)) {
                    if (embeddedTags.count(repr)) {
                        of << ".DATA";
                    }
                    of << ".TAG";
                    assert(&fld == &path.subFields.back());
                } else if (/*!repr->variants.is_None() ||*/ TU_TEST1(**ty, Path, .binding.is_Enum())) {
                    of << ".DATA.var_" << fld;
                } else {
                    of << "._" << fld;
                }

                ty = &repr->fields[fld].ty;
            }
            if (const auto* te = (*ty)->opt_Borrow()) {
                if (isDst(te->inner)) {
                    of << ".PTR";
                }
            } else if (const auto* te = (*ty)->opt_Pointer()) {
                if (isDst(te->inner)) {
                    of << ".PTR";
                }
            }
            return *ty;
        }

        void emitEnum(const Span& sp, const ::HIR::GenericPath& p, const ::HIR::Enum& item) override {
            ::MIR::Function emptyFcn;
            ::MIR::TypeResolve topMirRes {
                sp, mResolve, FMT_CB(ss, ss << "enum " << p;), ::HIR::TypeRef(), {}, emptyFcn
            };
            mirRes = &topMirRes;

            TRACE_FUNCTION_F(p);
            auto itemTy = crate.types.path(p.clone(), ::HIR::TypePathBinding::make_Enum(&item));
            const auto* repr = TargetGetTypeRepr(sp, mResolve, itemTy);

            // 1. Enumerate fields with the same offset as the first (these go into a union)
            // TODO: What if all data variants are zero-sized?
            ::std::vector<unsigned> unionFields;
            for (size_t i = 1; i < repr->fields.size(); i++) {
                if (repr->fields[i].offset == repr->fields[0].offset) {
                    unionFields.push_back(i);
                }
            }
            if (unionFields.size() > 0) {
                unionFields.insert(unionFields.begin(), 0);
            }

            of << "// enum " << p << "\n";
            of << "struct e_" << TransMangle(p) << " {\n";

            // HACK: For NonZero optimised enums, emit a struct with a single field
            // - This avoids a bug in GCC5 where it would generate incorrect code if there's a union here.
            if (const auto* ve = repr->variants.opt_NonZero()) {
                of << "\tstruct {\n";
                of << "\t\t";
                unsigned idx = 1 - ve->zeroVariant;
                emitCtype(repr->fields.at(idx).ty, FMT_CB(os, os << "var_" << idx));
                of << ";\n";
                of << "\t} DATA;\n";
            }
            // If there's only one field - it's either a single variant, or a value enum
            else if (repr->fields.size() == 1) {
                if (repr->variants.is_Values()) {
                    // Tag only.
                    // - A value-only enum.
                    of << "\t";
                    emitCtype(repr->fields.back().ty, FMT_CB(os, os << "TAG"));
                    of << ";\n";
                } else {
                    of << "\tunion {\n";
                    of << "\t\t";
                    emitCtype(repr->fields.back().ty, FMT_CB(os, os << "var_0"));
                    of << ";\n";
                    of << "\t} DATA;\n";
                    // No tag
                }
            }
            // If there multiple fields with the same offset, they're the data variants
            else if (unionFields.size() > 0) {
                if (unionFields.size() == repr->fields.size()) {
                    // Embedded tag
                    DEBUG("Untagged, nonzero or other");
                } else {
                    // Leading & external tag: repr(C)
                    assert(unionFields.size() + 1 == repr->fields.size());
                    assert(isEnumTag(repr, repr->fields.size() - 1));

                    assert(repr->fields.back().offset == 0);
                    DEBUG("Tag present at offset " << repr->fields.back().offset << " - " << repr->fields.back().ty);

                    of << "\t";
                    emitCtype(repr->fields.back().ty, FMT_CB(os, os << "TAG"));
                    of << ";\n";
                }

                // Options:
                // - Leading tag (union fields have a non-zero offset, tag has zero)
                // - Embedded (tag field shares offset with union fields, or there's no tag field)

                // Make the union!
                // NOTE: The way the structure generation works is that enum variants are always first, so the field index = the variant index
                // NOTE: Only emit if there are non-empty fields
                if (::std::any_of(unionFields.begin(), unionFields.end(), [this, repr](auto x) {
                    return !this->typeIsBadZst(repr->fields[x].ty);
                })) {
                    of << "\tunion {\n";
                    for (auto idx : unionFields) {
                        of << "\t\t";

                        const auto& ty = repr->fields[idx].ty;
                        if (this->typeIsBadZst(ty)) {
                            of << "// ZST: " << ty << "\n";
                        } else {
                            if (isEnumTag(repr, idx)) {
                                emitCtype(ty, FMT_CB(ss, ss << "TAG"));
                                embeddedTags.insert(repr);
                            } else {
                                emitCtype(ty, FMT_CB(ss, ss << "var_" << idx));
                            }
                            of << ";\n";
                            //sized_fields ++;
                        }
                    }
                    of << "\t} DATA;\n";
                }
            } else if (repr->fields.size() == 0) {
                // Empty/un-constructable
                // - Shouldn't be emitted really?
                if (options.disallowEmptyStructs) {
                    of << "\tchar _d;\n";
                }
            } else {
                // One data field and a tag (or all different offsets)
                TODO(sp, "No common offsets and more than one field, is this possible? - " << itemTy);
            }

            of << "};\n";

            size_t expSize = (repr->size > 0 ? repr->size : (options.disallowEmptyStructs ? 1 : 0));
            of << "typedef char sizeof_assert_" << TransMangle(p) << "[ (sizeof(struct e_" << TransMangle(p) << ") == " << expSize << ") ? 1 : -1 ];\n";

            mirRes = nullptr;
        }

        void emitConstructorEnum(const Span& sp, const ::HIR::GenericPath& path, const ::HIR::Enum& item, size_t varIdx) override {
            TRACE_FUNCTION_F(path << " var_idx=" << varIdx);

            auto p = path.clone();
            p.mPath.popComponent();
            auto ty = crate.types.path(p.clone(), ::HIR::TypePathBinding::make_Enum(&item));

            MonomorphStatePtr ms(crate.types, nullptr, &path.mParams, nullptr);
            ::HIR::TypeRef tmp;
            auto monomorph = [&](const auto& x) {
                return mResolve.monomorphExpandOpt(sp, tmp, x, ms);
            };

            ASSERT_BUG(sp, item.mData.is_Data(), "");
            const auto& var = item.mData.as_Data().at(varIdx);
            ASSERT_BUG(sp, var.type->is_Path(), "");
            const auto& str = *var.type->as_Path().binding.as_Struct();
            ASSERT_BUG(sp, str.mData.is_Tuple(), "");
            const auto& e = str.mData.as_Tuple();

            HIR::Function::argsT args;
            for (unsigned int i = 0; i < e.size(); i++) {
                args.push_back(::std::make_pair(HIR::Pattern(), monomorph(e[i].ent)));
            }

            ::MIR::Function emptyFcn;
            ::MIR::TypeResolve topMirRes {
                sp, mResolve, FMT_CB(ss, ss << "enum cons " << path;), ty, args, emptyFcn
            };
            mirRes = &topMirRes;

            of << "static struct e_" << TransMangle(p) << " " << TransMangle(path) << "(";
            for (unsigned int i = 0; i < e.size(); i++) {
                if (i != 0) {
                    of << ", ";
                }
                const auto& ty = args[i].second; // already monomorphised
                emitCtype(ty, FMT_CB(ss, ss << "arg" << i;));
            }
            of << ") {\n";

            of << "\tstruct e_" << TransMangle(p) << " rv;\n";

            std::vector<MIR::Param> vals;
            for (unsigned int i = 0; i < e.size(); i++) {
                vals.push_back(MIR::LValue::newArgument(i));
            }

            // Create the variant
            // - Use `emit_statement` to avoid re-writing the enum tag handling
            emitStatement(*mirRes, ::MIR::Statement::make_Assign({::MIR::LValue::newReturn(), ::MIR::RValue::make_EnumVariant({p.clone(), static_cast<unsigned>(varIdx), mv$(vals)})}));
            of << "\treturn rv;\n";
            of << "}\n";
            mirRes = nullptr;
        }

        void emitConstructorStruct(const Span& sp, const ::HIR::GenericPath& p, const ::HIR::Struct& item) override {
            TRACE_FUNCTION_F(p);
            ::HIR::TypeRef tmp;
            MonomorphStatePtr ms(crate.types, nullptr, &p.mParams, nullptr);
            auto monomorph = [&](const auto& x) {
                return mResolve.monomorphExpandOpt(sp, tmp, x, ms);
            };

            // Crate constructor function
            const auto& e = item.mData.as_Tuple();
            of << "static struct s_" << TransMangle(p) << " " << TransMangle(p) << "(";
            for (unsigned int i = 0; i < e.size(); i++) {
                if (i != 0) {
                    of << ", ";
                }
                const auto& ty = monomorph(e[i].ent);
                emitCtype(ty, FMT_CB(ss, ss << "_" << i;));
            }
            of << ") {\n";
            of << "\tstruct s_" << TransMangle(p) << " rv = {";
            bool emitted = false;
            for (unsigned int i = 0; i < e.size(); i++) {
                const auto& ty = monomorph(e[i].ent);
                if (this->typeIsBadZst(ty)) {
                    continue;
                }
                if (emitted) {
                    of << ",";
                }
                emitted = true;
                of << "\n\t\t_" << i;
            }
            if (!emitted) {
                of << "\n\t\t0";
            }
            of << "\n";
            of << "\t\t};\n";
            of << "\treturn rv;\n";
            of << "}\n";
        }

        // Returns `true` if the type is pointer-aligned (i.e. it could contain a pointer)
        bool emitStaticTy(const HIR::TypeData* type, const ::HIR::Path& p, bool isProto) {
            size_t size = 0, align = 0;
            TargetGetSizeAndAlignOf(sp, mResolve, type, size, align);
            bool rv = (align * 8 >= TargetGetCurSpec().arch.pointerBits);
            of << "union u_static_" << TransMangle(p);
            if (isProto) {
                of << "{ ";
                emitCtype(type, FMT_CB(ss, ss << "val";));
                of << "; ";
                if (rv) {
                    of << "uintptr_t raw[" << (size / (TargetGetCurSpec().arch.pointerBits / 8)) << "];";
                } else {
                    of << "uint8_t raw[" << size << "];";
                }
                of << " }";
            }
            of << " " << TransMangle(p);
            return rv;
        }

        void emitStaticExt(const ::HIR::Path& p, const ::HIR::Static& item, const TransParams& params) override {
            ::MIR::Function emptyFcn;
            ::MIR::TypeResolve topMirRes {
                sp, mResolve, FMT_CB(ss, ss << "extern static " << p;), ::HIR::TypeRef(), {}, emptyFcn
            };
            mirRes = &topMirRes;
            TRACE_FUNCTION_F(p);
            auto type = params.monomorph(mResolve, item.mType);

            // LLVM supports prepending a symbol name with \1 to prevent further mangling.
            // Since we're targeting C, not LLVM, strip off this prefix.
            std::string linkageName = item.linkage.name;
            if (!linkageName.empty() && linkageName[0] == '\1') {
                linkageName = linkageName.substr(1);
            }

            if (item.linkage.type == HIR::Linkage::Type::ExternWeak) {
                ASSERT_BUG(sp, linkageName != "", "");
                of << "extern char ";
                of << "__attribute__((weak)) ";

                of << linkageName << "[0];\n";

                emitStaticTy(type, p, /*is_proto=*/true);
                of << " = { .raw = { (uintptr_t)" << linkageName << " } };";
                of << "\t// static " << p << " : " << type;
                of << "\n";
                return;
            }

            if (linkageName != "") {
                // Handled with asm() later

            }

            of << "extern ";
            emitStaticTy(type, p, /*is_proto=*/true);
            if (linkageName != "") {
                if (TargetGetCurSpec().osName == "macos") { // Not macOS only, but all Apple platforms.
                    of << " asm(\"_" << linkageName << "\")";
                } else {
                    of << " asm(\"" << linkageName << "\")";
                }
            }
            of << ";";
            of << "\t// static " << p << " : " << type;
            of << "\n";

            mirRes = nullptr;
        }

        void emitStaticProto(const ::HIR::Path& p, const ::HIR::Static& item, const TransParams& params) override {
            ::MIR::Function emptyFcn;
            ::MIR::TypeResolve topMirRes {
                sp, mResolve, FMT_CB(ss, ss << "static " << p;), ::HIR::TypeRef(), {}, emptyFcn
            };
            mirRes = &topMirRes;

            TRACE_FUNCTION_F(p);
            auto type = params.monomorph(mResolve, item.mType);
            switch (item.linkage.type) {
                case HIR::Linkage::Type::External:
                    break;
                case HIR::Linkage::Type::Auto:
                    break;
                case HIR::Linkage::Type::Weak:
                    of << "__attribute__((weak)) ";

                    break;
                case HIR::Linkage::Type::ExternWeak:
                    of << "__attribute__((weak_import)) ";

                    break;
            }
            if (item.linkage.section != "") {
                of << "__attribute__((section(\"" << item.linkage.section << "\"))) ";

            }
            if (item.mParams.isGeneric()) {
                of << "__attribute__((weak)) ";

            }
            of << "extern ";
            emitStaticTy(type, p, /*is_proto=*/true);
            of << ";";
            of << "\t// static " << p << " : " << type;
            of << "\n";

            mirRes = nullptr;
        }

        void emitStaticLocal(const ::HIR::Path& p, const ::HIR::Static& item, const TransParams& params, const EncodedLiteral& encoded) override {
            ::MIR::Function emptyFcn;
            ::MIR::TypeResolve topMirRes {
                sp, mResolve, FMT_CB(ss, ss << "static " << p;), ::HIR::TypeRef(), {}, emptyFcn
            };
            mirRes = &topMirRes;

            TRACE_FUNCTION_F(p);

            auto type = params.monomorph(mResolve, item.mType);
            const bool isZero = isZeroLiteral(type, encoded, params);
            if (item.mParams.isGeneric()) {
                of << "__attribute__((weak)) ";

            }
            bool isPacked = emitStaticTy(type, p, /*is_proto=*/false);
            of << " = ";

            if (isZero) {
                of << "{}";
            } else {
                of << "{ .raw = {";
                if (isPacked) {
                    DEBUG("encoded.bytes = `" << FMT_CB(ss, for (auto& b : encoded.bytes) ss << std::setw(2) << std::setfill('0') << std::hex << unsigned(b) << (int(&b - encoded.bytes.data()) % 8 == 7 ? " " : "");) << "`");
                    DEBUG("encoded.relocations = " << encoded.relocations);
                    auto relocIt = encoded.relocations.begin();
                    auto ptrSize = TargetGetCurSpec().arch.pointerBits / 8;
                    for (size_t i = 0; i < encoded.bytes.size(); i += ptrSize) {
                        uint64_t v = 0;
                        if (TargetGetCurSpec().arch.bigEndian) {
                            for (size_t o = 0, j = ptrSize; j--; o++) {
                                v |= static_cast<uint64_t>(encoded.bytes[i + o]) << (j * 8);
                            }
                        } else {
                            for (size_t o = 0, j = 0; j < ptrSize; j++, o++) {
                                v |= static_cast<uint64_t>(encoded.bytes[i + o]) << (j * 8);
                            }
                        }

                        if (i > 0) {
                            of << ",";
                        }

                        if (relocIt != encoded.relocations.end() && relocIt->ofs <= i) {
                            MIR_ASSERT(*mirRes, relocIt->ofs == i, "Relocation not aligned to a pointer - " << relocIt->ofs << " != " << i);
                            MIR_ASSERT(*mirRes, relocIt->len == ptrSize, "Relocation size not pointer size - " << relocIt->len << " != " << ptrSize);
                            v -= EncodedLiteral::PTR_BASE;
                            //MIR_ASSERT(*m_mir_res, v == 0, "TODO: Relocation with non-zero offset " << i << ": v=0x" << std::hex << v << std::dec << " Reloc=" << *reloc_it << " Literal=" << encoded);

                            of << "(uintptr_t)";
                            if (relocIt->p) {
                                if (relocIt->p->mData.is_UfcsInherent() && relocIt->p->mData.as_UfcsInherent().item == "#type_id") {
                                    const auto& ty = relocIt->p->mData.as_UfcsInherent().type;
                                    of << "&__typeid_" << TransMangle(ty);
                                } else {
                                    of << "&" << TransMangle(*relocIt->p);
                                }
                            } else {
                                this->printEscapedString(relocIt->bytes);
                            }
                            if (v > 0) {
                                of << "+" << v;
                            }

                            ++relocIt;
                        } else {
                            of << "0x" << std::hex << v << "ull" << std::dec;
                        }
                    }
                } else {
                    MIR_ASSERT(*mirRes, encoded.relocations.empty(), "Non-pointer-aligned data with relocations");
                    bool e = false;
                    of << std::dec;
                    for (auto b : encoded.bytes) {
                        if (e) {
                            of << ",";
                        }
                        of << int(b); // Just leave it as decimal
                        e = true;
                    }
                }
                of << "} }";
            }
            of << ";";
            of << "\t// static " << p << " : " << type << " = " << encoded;
            of << "\n";
            mirRes = nullptr;
        }

        void emitFloat(FloatValue v, HIR::CoreType ty) {
            if (ty == HIR::CoreType::F16) {
                of << "f16_disabled()";
            } else if (ty == HIR::CoreType::F128) {
                const F128 bits(v);
                of << "make_f128_bits(0x" << ::std::hex << bits.hi << "ull, 0x" << bits.lo << "ull)" << ::std::dec;
            } else if (floatValueIsNan(v)) {
                of << (ty == HIR::CoreType::F32 ? "__builtin_nanf(\"\")" : "__builtin_nan(\"\")");
            } else if (floatValueIsInfinite(v)) {
                of << (v < 0 ? "-" : "");
                of << (ty == HIR::CoreType::F32 ? "__builtin_inff()" : "__builtin_inf()");
            } else {
                if (ty == HIR::CoreType::F32) {
                    of.precision(::std::numeric_limits<float>::max_digits10 + 1);
                    of << ::std::scientific << v << "f";
                } else {
                    of.precision(::std::numeric_limits<double>::max_digits10 + 1);
                    of << ::std::scientific << v;
                }
            }
        }

        void printEscapedString(const std::string& s) {
            printEscapedStringInner(s.c_str(), s.c_str() + s.size());
        }

        void printEscapedString(const std::vector<uint8_t>& s) {
            const char* start = reinterpret_cast<const char*>(s.data());
            printEscapedStringInner(start, start + s.size());
        }

        void printEscapedStringInner(const char* start, const char* end) {
            const unsigned MAX_STRING_LEN = 16380 / 3 - 10;
            of << "\"" << ::std::hex;
            unsigned nCh = 0;
            while (start != end) {
                const char v = *start++;
                switch (v) {
                    case '"':
                        of << "\\\"";
                        break;
                    case '\\':
                        of << "\\\\";
                        break;
                    case '\n':
                        of << "\\n";
                        break;
                    case '?':
                        if (end - start >= 2 && start[0] == '?') {
                            if (start[1] == '!') {
                                // Trigraph! Needs an escape in it.
                                of << v;
                                of << "\"\"";
                                nCh = 0;
                                break;
                            }
                        }
                        // Fall through
                    default:
                        if (' ' <= v && static_cast<uint8_t>(v) < 0x7F) {
                            of << v;
                        } else {
                            if (static_cast<uint8_t>(v) < 16) {
                                of << "\\x0" << (unsigned int)static_cast<uint8_t>(v);
                            } else {
                                of << "\\x" << (unsigned int)static_cast<uint8_t>(v);
                            }
                            // If the next character is a hex digit, close/reopen the string.
                            if (start != end && isxdigit(static_cast<unsigned char>(*start))) {
                                of << "\"\"";
                                nCh = 0;
                            }
                        }
                }
                nCh++;
                if (nCh == MAX_STRING_LEN) {
                    of << "\"\"";
                    nCh = 0;
                }
            }
            of << "\"" << ::std::dec;
        }

        void emitFunctionExt(const ::HIR::Path& p, const ::HIR::Function& item, const TransParams& params) override {
            ::MIR::Function emptyFcn;
            ::MIR::TypeResolve topMirRes {
                sp, mResolve, FMT_CB(ss, ss << "extern fn " << p;), ::HIR::TypeRef(), {}, emptyFcn
            };
            mirRes = &topMirRes;
            TRACE_FUNCTION_F(p);

            of << "// EXTERN extern \"" << item.mAbi << "\" " << p << "\n";
            if (item.linkage.name.rfind("llvm.", 0) == 0) {
                of << "static ";
                emitFunctionHeader(p, item, params);
                of << "{\n";
                of << "\t";
                emitCtype(item.returnType);
                of << " rv;\n";

                if (item.linkage.name == "llvm.prefetch") {
                    of << "\tif(arg1) {\n"
                         << "\t\tswitch(arg2) {\n"
                         << "\t\tcase 0: __builtin_prefetch(arg0, 1, 0); break;\n"
                         << "\t\tcase 1: __builtin_prefetch(arg0, 1, 1); break;\n"
                         << "\t\tcase 2: __builtin_prefetch(arg0, 1, 2); break;\n"
                         << "\t\tdefault: __builtin_prefetch(arg0, 1, 3); break;\n"
                         << "\t\t}\n"
                         << "\t} else {\n"
                         << "\t\tswitch(arg2) {\n"
                         << "\t\tcase 0: __builtin_prefetch(arg0, 0, 0); break;\n"
                         << "\t\tcase 1: __builtin_prefetch(arg0, 0, 1); break;\n"
                         << "\t\tcase 2: __builtin_prefetch(arg0, 0, 2); break;\n"
                         << "\t\tdefault: __builtin_prefetch(arg0, 0, 3); break;\n"
                         << "\t\t}\n"
                         << "\t}\n"
                         << "\treturn;\n";
                }
                // pshufb instruction w/ 128 bit operands
                else if (item.linkage.name == "llvm.x86.ssse3.pshuf.b.128") {
                    of << "\tconst uint8_t* src = (const uint8_t*)&arg0;\n"
                         << "\tconst uint8_t* mask = (const uint8_t*)&arg1;\n"
                         << "\tuint8_t* dst = (uint8_t*)&rv;\n"
                         << "\tfor(int i = 0; i < " << 128 / 8 << "; i ++) dst[i] = (mask[i] < 0x80 ? src[mask[i] & 0xF] : 0);\n"
                         << "\treturn rv;\n";
                } else if (item.linkage.name == "llvm.x86.avx2.pshuf.b") {
                    of << "\tconst uint8_t* src = (const uint8_t*)&arg0;\n"
                         << "\tconst uint8_t* mask = (const uint8_t*)&arg1;\n"
                         << "\tuint8_t* dst = (uint8_t*)&rv;\n"
                         << "\tfor(int i = 0; i < " << 256 / 8 << "; i ++) dst[i] = (mask[i] < 0x80 ? src[(i & 16) | (mask[i] & 0xF)] : 0);\n"
                         << "\treturn rv;\n";
                }
                // Multiply-add intrinsics used by simd-adler32 (via png's flate2)
                else if (item.linkage.name == "llvm.x86.ssse3.pmadd.ub.sw.128" || item.linkage.name == "llvm.x86.avx2.pmadd.ub.sw") {
                    int n = (item.linkage.name == "llvm.x86.avx2.pmadd.ub.sw" ? 32 : 16);
                    of << "\tconst uint8_t* a = (const uint8_t*)&arg0;\n"
                         << "\tconst int8_t* b = (const int8_t*)&arg1;\n"
                         << "\tint16_t* dst = (int16_t*)&rv;\n"
                         << "\tfor(int i = 0; i < " << n / 2 << "; i ++) {\n"
                         << "\t\tint32_t v = (int32_t)a[2*i]*b[2*i] + (int32_t)a[2*i+1]*b[2*i+1];\n"
                         << "\t\tdst[i] = (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));\n"
                         << "\t}\n"
                         << "\treturn rv;\n";
                } else if (item.linkage.name == "llvm.x86.sse2.pmadd.wd" || item.linkage.name == "llvm.x86.avx2.pmadd.wd") {
                    int n = (item.linkage.name == "llvm.x86.avx2.pmadd.wd" ? 16 : 8);
                    of << "\tconst int16_t* a = (const int16_t*)&arg0;\n"
                         << "\tconst int16_t* b = (const int16_t*)&arg1;\n"
                         << "\tint32_t* dst = (int32_t*)&rv;\n"
                         << "\tfor(int i = 0; i < " << n / 2 << "; i ++) dst[i] = (int32_t)a[2*i]*b[2*i] + (int32_t)a[2*i+1]*b[2*i+1];\n"
                         << "\treturn rv;\n";
                } else if (item.linkage.name == "llvm.x86.sse2.psad.bw" || item.linkage.name == "llvm.x86.avx2.psad.bw") {
                    int n = (item.linkage.name == "llvm.x86.avx2.psad.bw" ? 32 : 16);
                    of << "\tconst uint8_t* a = (const uint8_t*)&arg0;\n"
                         << "\tconst uint8_t* b = (const uint8_t*)&arg1;\n"
                         << "\tuint64_t* dst = (uint64_t*)&rv;\n"
                         << "\tfor(int k = 0; k < " << n / 8 << "; k ++) {\n"
                         << "\t\tuint64_t sum = 0;\n"
                         << "\t\tfor(int j = 0; j < 8; j ++) { int d = (int)a[k*8+j] - (int)b[k*8+j]; sum += (d < 0 ? -d : d); }\n"
                         << "\t\tdst[k] = sum;\n"
                         << "\t}\n"
                         << "\treturn rv;\n";
                } else if (item.linkage.name == "llvm.x86.sse2.psrli.d") {
                    of << "\tconst uint32_t* src = (const uint32_t*)&arg0;\n"
                         << "\tuint32_t* dst = (uint32_t*)&rv;\n"
                         << "\tfor(int i = 0; i < " << 128 / 32 << "; i ++) dst[i] = src[i] >> arg1;\n"
                         << "\treturn rv;\n";
                } else if (item.linkage.name == "llvm.x86.sse2.pslli.d") {
                    of << "\tconst uint32_t* src = (const uint32_t*)&arg0;\n"
                         << "\tuint32_t* dst = (uint32_t*)&rv;\n"
                         << "\tfor(int i = 0; i < " << 128 / 32 << "; i ++) dst[i] = src[i] << arg1;\n"
                         << "\treturn rv;\n";
                } else if (item.linkage.name == "llvm.x86.sse2.pmovmskb.128") {
                    of << "\tconst uint8_t* src = (const uint8_t*)&arg0;\n"
                         << "\tuint8_t* dst = (uint8_t*)&rv; *dst = 0;\n"
                         << "\tfor(int i = 0; i < " << 128 / 8 << "; i ++) *dst |= (src[i] >> 7) << i;\n"
                         << "\treturn rv;\n";
                } else if (item.linkage.name == "llvm.x86.sse2.storeu.dq") {
                    of << "\tmemcpy(arg0, &arg1, sizeof(arg1));\n";
                }
                // SHA-NI: the sha2 crate takes this path when runtime detection
                // reports hardware support; portable C keeps it correct.
                else if (item.linkage.name == "llvm.x86.sha256rnds2") {
                    of << "\tconst uint32_t* st_cdgh = (const uint32_t*)&arg0;\n"
                         << "\tconst uint32_t* st_abef = (const uint32_t*)&arg1;\n"
                         << "\tconst uint32_t* wk = (const uint32_t*)&arg2;\n"
                         << "\tuint32_t* dst = (uint32_t*)&rv;\n"
                         << "\tuint32_t a = st_abef[3], b = st_abef[2], e = st_abef[1], f = st_abef[0];\n"
                         << "\tuint32_t c = st_cdgh[3], d = st_cdgh[2], g = st_cdgh[1], h = st_cdgh[0];\n"
                         << "\tfor(int i = 0; i < 2; i ++) {\n"
                         << "\t\tuint32_t ch = (e & f) ^ (~e & g);\n"
                         << "\t\tuint32_t maj = (a & b) ^ (a & c) ^ (b & c);\n"
                         << "\t\tuint32_t s0 = (a >> 2 | a << 30) ^ (a >> 13 | a << 19) ^ (a >> 22 | a << 10);\n"
                         << "\t\tuint32_t s1 = (e >> 6 | e << 26) ^ (e >> 11 | e << 21) ^ (e >> 25 | e << 7);\n"
                         << "\t\tuint32_t t = ch + s1 + wk[i] + h;\n"
                         << "\t\th = g; g = f; f = e; e = t + d; d = c; c = b; b = a; a = t + maj + s0;\n"
                         << "\t}\n"
                         << "\tdst[3] = a; dst[2] = b; dst[1] = e; dst[0] = f;\n"
                         << "\treturn rv;\n";
                } else if (item.linkage.name == "llvm.x86.sha256msg1") {
                    of << "\tconst uint32_t* w = (const uint32_t*)&arg0;\n"
                         << "\tconst uint32_t* w2 = (const uint32_t*)&arg1;\n"
                         << "\tuint32_t* dst = (uint32_t*)&rv;\n"
                         << "\tfor(int i = 0; i < 4; i ++) {\n"
                         << "\t\tuint32_t x = (i < 3 ? w[i+1] : w2[0]);\n"
                         << "\t\tdst[i] = w[i] + ((x >> 7 | x << 25) ^ (x >> 18 | x << 14) ^ (x >> 3));\n"
                         << "\t}\n"
                         << "\treturn rv;\n";
                } else if (item.linkage.name == "llvm.x86.sha256msg2") {
                    of << "\tconst uint32_t* w = (const uint32_t*)&arg0;\n"
                         << "\tconst uint32_t* prev = (const uint32_t*)&arg1;\n"
                         << "\tuint32_t* dst = (uint32_t*)&rv;\n"
                         << "\tuint32_t w14 = prev[2], w15 = prev[3];\n"
                         << "\tuint32_t w16 = w[0] + ((w14 >> 17 | w14 << 15) ^ (w14 >> 19 | w14 << 13) ^ (w14 >> 10));\n"
                         << "\tuint32_t w17 = w[1] + ((w15 >> 17 | w15 << 15) ^ (w15 >> 19 | w15 << 13) ^ (w15 >> 10));\n"
                         << "\tuint32_t w18 = w[2] + ((w16 >> 17 | w16 << 15) ^ (w16 >> 19 | w16 << 13) ^ (w16 >> 10));\n"
                         << "\tuint32_t w19 = w[3] + ((w17 >> 17 | w17 << 15) ^ (w17 >> 19 | w17 << 13) ^ (w17 >> 10));\n"
                         << "\tdst[0] = w16; dst[1] = w17; dst[2] = w18; dst[3] = w19;\n"
                         << "\treturn rv;\n";
                }
                // Add with carry
                // `fn llvm_addcarry_u32(a: u8, b: u32, c: u32) -> (u8, u32)`
                else if (item.linkage.name == "llvm.x86.addcarry.32") {
                    of << "\trv._0 = __builtin_add_overflow(arg1, arg2, &rv._1);\n";
                    of << "\tif(arg0) rv._0 |= __builtin_add_overflow(rv._1, 1, &rv._1);\n";
                    of << "\treturn rv;\n";
                }
                // `fn llvm_addcarryx_u32(a: u8, b: u32, c: u32, d: *mut u8) -> u8`
                else if (item.linkage.name == "llvm.x86.addcarryx.u32") {
                    of << "\trv = __builtin_add_overflow(arg1, arg2, (uint32_t*)arg3);\n";
                    of << "\tif(arg0) rv |= __builtin_add_overflow(*arg3, 1, (uint32_t*)arg3);\n";
                    of << "\treturn rv;\n";
                }
                // `fn llvm_subborrow(a: u8, b: u32, c: u32) -> (u8, u32);`
                else if (item.linkage.name == "llvm.x86.subborrow.32") {
                    of << "\trv._0 = __builtin_sub_overflow(arg1, arg2, &rv._1);\n";
                    of << "\tif(arg0) rv._0 |= __builtin_sub_overflow(rv._1, 1, &rv._1);\n";
                    of << "\treturn rv;\n";
                } else if (item.linkage.name == "llvm.x86.xgetbv") {
                    of << "\tuint32_t lo, hi;\n";
                    of << "\t__asm__ __volatile__ (\"xgetbv\" : \"=a\" (lo), \"=d\" (hi) : \"c\" (arg0) );\n";
                    of << "\treturn lo | ((uint64_t)hi << 32);\n";

                } else if (item.linkage.name == "llvm.x86.sse2.pause") {
                    // Just a `PAUSE` instruciton, which is effectively a nop
                    of << "\t__asm__ __volatile__ (\"pause\");\n";

                    of << "\treturn ;\n";
                }
                // AES functions
                else if (item.linkage.name.rfind("llvm.x86.aesni.", 0) == 0) {
                    of << "\tassert(!\"Unsupprorted LLVM x86 intrinsic: " << item.linkage.name << "\"); abort();\n";
                } else {
                    // TODO: Hand off to compiler-specific intrinsics
                    //MIR_TODO(*m_mir_res, "LLVM extern linkage: " << item.m_linkage.name);
                    of << "\tassert(!\"Extern LLVM: " << item.linkage.name << "\"); abort();\n";
                }
                of << "}\n";
                mirRes = nullptr;
                return;
            } else if (item.linkage.name == "_Unwind_RaiseException") {
                of << "// - Magic compiler impl\n";
                of << "static ";
                emitFunctionHeader(p, item, params);
                of << " {\n";
                of << "\tthrow mrustc_panic{arg0};\n";
                of << "}\n";
                return;
            } else {
                of << "extern ";
            }
            emitFunctionHeader(p, item, params);
            if (item.linkage.name != "") {
                if (TargetGetCurSpec().osName == "macos") { // Not macOS only, but all Apple platforms.
                    of << " asm(\"_" << item.linkage.name << "\")";
                } else {
                    of << " asm(\"" << item.linkage.name << "\")";
                }

            }
            of << ";\n";

            mirRes = nullptr;
        }

        void emitFunctionProto(const ::HIR::Path& p, const ::HIR::Function& item, const TransParams& params, bool isExternDef) override {
            ::MIR::Function emptyFcn;
            ::MIR::TypeResolve topMirRes {
                sp, mResolve, FMT_CB(ss, ss << "/*proto*/ fn " << p;), ::HIR::TypeRef(), {}, emptyFcn
            };
            mirRes = &topMirRes;

            TRACE_FUNCTION_F(p);
            of << "// PROTO extern \"" << item.mAbi << "\" " << p << "\n";
            if (item.linkage.name != "") {
                // If this function is implementing an external ABI, just rename it.
                of << "#define " << TransMangle(p) << " " << item.linkage.name << "\n";
            }
            if (isExternDef) {
                of << "static ";
            }
            switch (item.linkage.type) {
                case HIR::Linkage::Type::External:
                    break;
                case HIR::Linkage::Type::Auto:
                    break;
                case HIR::Linkage::Type::Weak:
                    of << "__attribute__((weak)) ";

                    break;
                case HIR::Linkage::Type::ExternWeak:
                    BUG(Span(), "unexpected ExternWeak on function");
            }
            emitFunctionHeader(p, item, params);
            of << ";\n";

            mirRes = nullptr;
        }

        void emitFunctionCode(const ::HIR::Path& p, const ::HIR::Function& item, const TransParams& params, bool isExternDef, const ::MIR::FunctionPointer& code) override {
            TRACE_FUNCTION_F(p);

            ::MIR::TypeResolve::argsT argTypes;
            for (const auto& ent : item.mArgs) {
                argTypes.push_back(::std::make_pair(::HIR::Pattern{}, params.monomorph(mResolve, ent.second)));
            }

            ::HIR::TypeRef retTypeTmp;
            const auto& retType = monomorphiseFcnReturn(retTypeTmp, item, params);

            ::MIR::TypeResolve localMirRes {
                sp, mResolve, FMT_CB(ss, ss << p;), retType, argTypes, *code
            };
            mirRes = &localMirRes;

            of << "// " << p << "\n";
            if (isExternDef) {
                of << "static ";
            }
            emitFunctionHeader(p, item, params);
            of << "\n";
            of << "{\n";


            // Variables
            of << "\t";
            emitCtype(retType, FMT_CB(ss, ss << "rv";));
            of << ";\n";
            for (unsigned int i = 0; i < code->locals.size(); i++) {
                // If the type is a ZST, initialise it (to avoid warnings)
                if (this->typeIsBadZst(code->locals[i])) {
                    continue;
                }
                DEBUG("var" << i << " : " << code->locals[i]);
                of << "\t";
                emitCtype(code->locals[i], FMT_CB(ss, ss << "var" << i;));
                of << ";";
                of << "\t// " << code->locals[i];
                of << "\n";
            }
            for (unsigned int i = 0; i < code->dropFlags.size(); i++) {
                of << "\tbool df" << i << " = " << code->dropFlags[i] << ";\n";
            }

            ::std::set<unsigned> cleanupBlocks;
            ::std::vector<unsigned> pendingCleanupBlocks;
            for (const auto& block : code->blocks) {
                TU_MATCH_HDRA((block.terminator), {)
                    TU_ARMA(Drop, e) {
                        if (const auto* target = e.unwind.opt_Cleanup()) {
                            pendingCleanupBlocks.push_back(*target);
                        }
                    }
                    TU_ARMA(Call, e) {
                        if (const auto* target = e.unwind.opt_Cleanup()) {
                            pendingCleanupBlocks.push_back(*target);
                        }
                    }
                    default: break;
                }
            }
            while (!pendingCleanupBlocks.empty()) {
                const auto blockIndex = pendingCleanupBlocks.back();
                pendingCleanupBlocks.pop_back();
                MIR_ASSERT(localMirRes, blockIndex < code->blocks.size(), "Cleanup target BB" << blockIndex << " is out of range");
                if (!cleanupBlocks.insert(blockIndex).second) {
                    continue;
                }
                ::MIR::visit::visitTerminatorTarget(code->blocks[blockIndex].terminator, [&](const auto& target) {
                    pendingCleanupBlocks.push_back(target);
                });
            }
            if (!cleanupBlocks.empty()) {
                emitCleanupRunner(localMirRes, cleanupBlocks);
            }

            for (unsigned i = 0; i < code->blocks.size(); i++) {
                const auto& block = code->blocks[i];
                if (cleanupBlocks.count(i) != 0) {
                    continue;
                }
                of << "bb" << i << ": {\n";
                for (const auto& stmt : block.statements) {
                    localMirRes.setCurStmt(i, &stmt - block.statements.data());
                    emitStatement(localMirRes, stmt, 1);
                }
                localMirRes.setCurStmtTerm(i);
                emitBlockTerminator(localMirRes, block.terminator, i, false, 1);
                of << "}\n";
            }
            of << "}\n";
            of.flush();
            mirRes = nullptr;
            mirRes = nullptr;
        }

        void emitOperationWithUnwind(const ::MIR::UnwindAction& action, unsigned indentLevel, ::std::function<void(unsigned)> emitOperation) {
            auto indent = RepeatLitStr{"\t", static_cast<int>(indentLevel)};
            TU_MATCH_HDRA((action), {)
                TU_ARMA(Continue, _) {
                    emitOperation(indentLevel);
                }
                TU_ARMA(Cleanup, target) {
                    of << indent << "try {\n";
                    emitOperation(indentLevel + 1);
                    of << indent << "} catch (...) {\n";
                    of << indent << "\ttry { mrustc_run_cleanup(" << target << "); } catch (...) { abort(); }\n";
                    of << indent << "\tthrow;\n";
                    of << indent << "}\n";
                }
                TU_ARMA(Terminate, _) {
                    of << indent << "try {\n";
                    emitOperation(indentLevel + 1);
                    of << indent << "} catch (...) { abort(); }\n";
                }
                TU_ARMA(Unreachable, _) {
                    of << indent << "try {\n";
                    emitOperation(indentLevel + 1);
                    of << indent << "} catch (...) { abort(); }\n";
                }
            }
        }

        void emitBlockTerminator(::MIR::TypeResolve& localMirRes, const ::MIR::Terminator& term, unsigned blockIndex, bool cleanup, unsigned indentLevel) {
            auto indent = RepeatLitStr{"\t", static_cast<int>(indentLevel)};
            auto emitTarget = [&](unsigned target) {
                of << indent << "goto " << (cleanup ? "cleanup_bb" : "bb") << target << ";\n";
            };
            TU_MATCH_HDRA((term), {)
                TU_ARMA(Incomplete, _) {
                    of << indent << "abort();\n";
                }
                TU_ARMA(Return, _) {
                    if (cleanup) {
                        of << indent << "abort();\n";
                    } else if (localMirRes.retType == crate.types.unit()) {
                        of << indent << "return;\n";
                    } else {
                        of << indent << "return rv;\n";
                    }
                }
                TU_ARMA(UnwindResume, _) {
                    if (cleanup) {
                        of << indent << "return;\n";
                    } else {
                        of << indent << "abort();\n";
                    }
                }
                TU_ARMA(UnwindTerminate, _) {
                    of << indent << "abort();\n";
                }
                TU_ARMA(Unreachable, _) {
                    of << indent << "abort();\n";
                }
                TU_ARMA(Goto, target) {
                    emitTarget(target);
                }
                TU_ARMA(If, e) {
                    of << indent << "if(";
                    emitLvalue(e.cond);
                    of << ") goto " << (cleanup ? "cleanup_bb" : "bb") << e.bbTrue;
                    of << "; else goto " << (cleanup ? "cleanup_bb" : "bb") << e.bbFalse << ";\n";
                }
                TU_ARMA(Switch, e) {
                    if (e.validFlag != ~0u) {
                        of << indent << "if(!df" << e.validFlag << ") goto " << (cleanup ? "cleanup_bb" : "bb") << e.invalidTarget << ";\n";
                    }
                    emitTermSwitch(localMirRes, e.val, e.targets.size(), indentLevel, [&](size_t idx) {
                        of << "goto " << (cleanup ? "cleanup_bb" : "bb") << e.targets[idx] << ";";
                    });
                }
                TU_ARMA(SwitchValue, e) {
                    emitTermSwitchvalue(localMirRes, e.val, e.values, indentLevel, [&](size_t idx) {
                        const auto target = idx == SIZE_MAX ? e.defTarget : e.targets[idx];
                        of << "goto " << (cleanup ? "cleanup_bb" : "bb") << target << ";";
                    });
                }
                TU_ARMA(Drop, e) {
                    emitOperationWithUnwind(e.unwind, indentLevel, [&](unsigned operationIndent) {
                        emitDropOperation(localMirRes, e, operationIndent);
                    });
                    emitTarget(e.target);
                }
                TU_ARMA(Call, e) {
                    emitOperationWithUnwind(e.unwind, indentLevel, [&](unsigned operationIndent) {
                        emitTermCall(localMirRes, e, operationIndent);
                    });
                    emitTarget(e.retBlock);
                }
            }
            of << indent << "// ^ " << term << "\n";
            (void)blockIndex;
        }

        void emitCleanupRunner(::MIR::TypeResolve& localMirRes, const ::std::set<unsigned>& cleanupBlocks) {
            of << "\tauto mrustc_run_cleanup = [&](unsigned mrustc_cleanup_entry) {\n";
            of << "\t\tswitch(mrustc_cleanup_entry) {\n";
            for (auto block : cleanupBlocks) {
                of << "\t\tcase " << block << ": goto cleanup_bb" << block << ";\n";
            }
            of << "\t\tdefault: abort();\n";
            of << "\t\t}\n";
            for (auto blockIndex : cleanupBlocks) {
                const auto& block = localMirRes.fcn.blocks.at(blockIndex);
                of << "\tcleanup_bb" << blockIndex << ": {\n";
                for (const auto& stmt : block.statements) {
                    localMirRes.setCurStmt(blockIndex, &stmt - block.statements.data());
                    emitStatement(localMirRes, stmt, 2);
                }
                localMirRes.setCurStmtTerm(blockIndex);
                emitBlockTerminator(localMirRes, block.terminator, blockIndex, true, 2);
                of << "\t}\n";
            }
            of << "\t};\n";
        }
        bool typeIsEmulatedI128(const ::HIR::TypeData* ty) const {
            return options.emulatedI128 && (ty == ::HIR::CoreType::I128 || ty == ::HIR::CoreType::U128);
        }

        // Returns true if the input type is a ZST and ZSTs are not being emitted
        bool typeIsBadZst(const ::HIR::TypeData* ty) const {
            if (options.disallowEmptyStructs) {
                // TODO: Extern types are also ZSTs?
                size_t size, align;
                // NOTE: Uses the Size+Align version because that doesn't panic on unsized
                MIR_ASSERT(*mirRes, TargetGetSizeAndAlignOf(sp, mResolve, ty, size, align), "Unexpected generic? " << ty);
                return size == 0;
            } else {
                return false;
            }
        }

        bool lvalueIsBadZst(const ::MIR::LValue& lv) const {
            if (options.disallowEmptyStructs) {
                HIR::TypeRef tmp;
                return typeIsBadZst(mirRes->getLvalueType(tmp, lv));
            } else {
                return false;
            }
        }

        // Locals whose complete Rust type is a ZST aren't emitted in C.  A
        // projection from such a local has no C lvalue to take the address of.
        bool lvalueRootIsBadZst(const ::MIR::LValue& lv) const {
            if (options.disallowEmptyStructs) {
                HIR::TypeRef tmp;
                return typeIsBadZst(mirRes->getLvalueType(tmp, lv, lv.wrappers.size()));
            } else {
                return false;
            }
        }

        // An index into a zero-sized array is represented by the array's
        // address, never by a C `DATA` field (such fields are omitted).  Peel
        // nested zero-sized array projections to their materialized backing
        // lvalue before taking that address.
        ::MIR::LValue lvalueZstIndexBacking(const ::MIR::LValue& lv) const {
            auto rv = lv.clone();
            while (::MIR::LValue::CRef(rv).is_Index()) {
                auto inner = ::MIR::LValue::CRef(rv).innerRef();
                HIR::TypeRef tmp;
                if (!this->typeIsBadZst(mirRes->getLvalueType(tmp, inner))) {
                    break;
                }
                rv.wrappers.pop_back();
            }
            return rv;
        }

        void emitBorrow(const ::MIR::TypeResolve& localMirRes, HIR::BorrowType bt, const MIR::LValue& val) {
            ::HIR::TypeRef tmp;
            const auto& ty = localMirRes.getLvalueType(tmp, val);

            if (this->typeIsBadZst(ty) && !this->lvalueRootIsBadZst(val)) {
                auto backing = this->lvalueZstIndexBacking(val);
                if (backing.wrappers.size() != val.wrappers.size()) {
                    emitBorrow(localMirRes, bt, backing);
                    return;
                }
            }

            bool special = false;
            // If the inner value was a deref, just copy the pointer verbatim
            if (val.is_Deref()) {
                emitLvalue(::MIR::LValue::CRef(val).innerRef());
                special = true;
            }
            // Magic for taking a &-ptr to unsized field of a struct.
            // - Needs to get metadata from bottom-level pointer.
            else if (val.is_Field()) {
                auto metaTy = metadataType(ty);
                if (metaTy != MetadataType::None) {
                    auto baseVal = ::MIR::LValue::CRef(val).innerRef();
                    while (baseVal.is_Field()) {
                        baseVal.tryUnwrap();
                    }
                    MIR_ASSERT(localMirRes, baseVal.is_Deref(), "DST access must be via a deref");
                    const auto basePtr = baseVal.innerRef();

                    // Construct the new DST
                    switch (metaTy) {
                        case MetadataType::None:
                            throw "";
                        case MetadataType::Unknown:
                            MIR_BUG(localMirRes, "");
                        case MetadataType::Zero:
                            MIR_BUG(localMirRes, "");
                        case MetadataType::Slice:
                            of << "make_sliceptr(";
                            break;
                        case MetadataType::TraitObject:
                            of << "make_traitobjptr(";
                            break;
                    }
                    if (metaTy == MetadataType::TraitObject) {
                        ::HIR::TypeRef baseTmp;
                        const auto& baseTy = localMirRes.getLvalueType(baseTmp, baseVal.clone());
                        const auto baseParam = ::MIR::Param::make_LValue(basePtr.clone());
                        if (getInnerUnsizedType(baseTy)->is_TraitObject()) {
                            const auto* curTy = &baseTy;
                            of << "(uint8_t*)";
                            emitLvalue(basePtr);
                            of << ".PTR + ";
                            for (size_t i = baseVal.wrapperCount(); i < val.wrappers.size(); i++) {
                                const auto& wrapper = val.wrappers[i];
                                MIR_ASSERT(localMirRes, wrapper.is_Field(), "Unexpected DST lvalue wrapper - " << val);
                                if (i != baseVal.wrapperCount()) {
                                    of << " + ";
                                }
                                emitTraitObjectDstFieldOffset(*curTy, wrapper.as_Field(), baseParam);
                                const auto* repr = TargetGetTypeRepr(sp, mResolve, *curTy);
                                MIR_ASSERT(localMirRes, repr && wrapper.as_Field() < repr->fields.size(), "Invalid DST field - " << val);
                                curTy = &repr->fields[wrapper.as_Field()].ty;
                            }
                        } else {
                            of << "&";
                            emitLvalue(val);
                        }
                    } else {
                        of << "&";
                        emitLvalue(val);
                    }
                    of << ", ";
                    emitLvalue(basePtr);
                    of << ".META)";
                    special = true;
                }
            } else {
            }

            // NOTE: If disallow_empty_structs is set, structs don't include ZST fields
            // In this case, we need to avoid mentioning the removed fields
            auto valRef = ::MIR::LValue::CRef(val);
            if (!special && options.disallowEmptyStructs && valRef.is_Index() && this->typeIsBadZst(ty)) {
                auto inner = valRef.innerRef();
                ::HIR::TypeRef tmp;
                const auto& parentTy = localMirRes.getLvalueType(tmp, inner);
                const ::HIR::TypeData* elementTy = nullptr;
                if (const auto* array = parentTy->opt_Array()) {
                    elementTy = array->inner;
                } else if (const auto* slice = parentTy->opt_Slice()) {
                    elementTy = slice->inner;
                }
                MIR_ASSERT(localMirRes, elementTy, "Index of non-array type in ZST borrow path: " << parentTy);
                size_t elementSize = 0;
                MIR_ASSERT(localMirRes, TargetGetSizeOf(sp, mResolve, elementTy, elementSize), "Unknown array element size for " << parentTy);
                MIR_ASSERT(localMirRes, elementSize == 0, "Non-ZST element in ZST borrow path: " << elementTy);
                if (parentTy->is_Slice()) {
                    MIR_ASSERT(localMirRes, inner.is_Deref(), "Raw slice lvalue in ZST borrow path");
                    of << "(void*)";
                    emitLvalue(inner.innerRef());
                    of << ".PTR";
                } else {
                    of << "(void*)& ";
                    emitLvalue(inner);
                }
                special = true;
            }

            if (!special && options.disallowEmptyStructs && val.is_Field() && this->typeIsBadZst(ty)) {
                // Work backwards to the first non-ZST field
                auto valFp = ::MIR::LValue::CRef(val);
                assert(valFp.is_Field());
                while (valFp.innerRef().is_Field()) {
                    ::HIR::TypeRef tmp;
                    const auto& ty = localMirRes.getLvalueType(tmp, valFp.innerRef());
                    if (!this->typeIsBadZst(ty)) {
                        break;
                    }
                    valFp.tryUnwrap();
                }
                assert(valFp.is_Field());
                // Here, we have `val_fp` be a LValue::Field that refers to a ZST, but the inner of the field points to a non-ZST or a local

                // If the index is zero, then the best option is to borrow the source
                auto fieldInner = valFp.innerRef();
                if (fieldInner.is_Downcast()) {
                    of << "(void*)& ";
                    emitLvalue(fieldInner.innerRef());
                } else if (valFp.as_Field() == 0) {
                    ::HIR::TypeRef tmp;
                    const auto& parentTy = localMirRes.getLvalueType(tmp, fieldInner);
                    if (parentTy->is_Slice()) {
                        MIR_ASSERT(localMirRes, fieldInner.is_Deref(), "Raw slice lvalue in ZST borrow path");
                        of << "(void*)";
                        emitLvalue(fieldInner.innerRef());
                        of << ".PTR";
                    } else {
                        of << "(void*)& ";
                        emitLvalue(fieldInner);
                    }
                } else {
                    ::HIR::TypeRef tmp;
                    const auto& parentTy = localMirRes.getLvalueType(tmp, fieldInner);
                    const ::HIR::TypeData* elementTy = nullptr;
                    if (const auto* array = parentTy->opt_Array()) {
                        elementTy = array->inner;
                    } else if (const auto* slice = parentTy->opt_Slice()) {
                        elementTy = slice->inner;
                    }

                    if (elementTy) {
                        size_t elementSize = 0;
                        MIR_ASSERT(localMirRes, TargetGetSizeOf(sp, mResolve, elementTy, elementSize), "Unknown array element size for " << parentTy);
                        MIR_ASSERT(localMirRes, elementSize == 0, "Non-ZST element in ZST borrow path: " << elementTy);
                        of << "(void*)( (uint8_t*)";
                        if (parentTy->is_Slice()) {
                            MIR_ASSERT(localMirRes, fieldInner.is_Deref(), "Raw slice lvalue in ZST borrow path");
                            emitLvalue(fieldInner.innerRef());
                            of << ".PTR";
                        } else {
                            of << "& ";
                            emitLvalue(fieldInner);
                        }
                        of << " + " << elementSize * valFp.as_Field() << ") /*ZST*/";
                    } else {
                        // Get the number of fields in parent
                        auto* repr = TargetGetTypeRepr(sp, mResolve, parentTy);
                        assert(repr);
                        size_t nParentFields = repr->fields.size();
                        // Find next non-zero field
                        auto tmpLv = ::MIR::LValue::newField(fieldInner.clone(), valFp.as_Field() + 1);
                        bool found = false;
                        while (tmpLv.as_Field() < nParentFields) {
                            auto idx = tmpLv.as_Field();
                            const auto& ty = repr->fields[idx].ty;
                            if (ty->is_Path() && ty->as_Path().binding.is_ExternType()) {
                                // Extern types aren't emitted
                            } else if (this->typeIsBadZst(ty)) {
                                // ZSTs are't either
                            } else {
                                found = true;
                                break;
                            }
                            tmpLv.wrappers.back() = ::MIR::LValue::Wrapper::newField(idx + 1);
                        }

                        // If no non-zero fields were found before the end, then do pointer manipulation using the repr
                        if (!found) {
                            of << "(void*)( (uint8_t*)& ";
                            emitLvalue(fieldInner);
                            of << " + " << repr->fields[valFp.as_Field()].offset << ") /*ZST*/";
                        }
                        // Otherwise, use the next non-zero field
                        else {
                            of << "(void*)( &";
                            emitLvalue(tmpLv);
                            of << ") /*ZST*/";
                        }
                    }
                }
                special = true;
            }

            if (!special) {
                of << "& ";
                emitLvalue(val);
            }
        }

        void emitCompositeAssign(const ::MIR::TypeResolve& localMirRes, ::std::function<void()> emitSlot, const ::std::vector<::MIR::Param>& vals, unsigned indentLevel, bool prependNewline = true) {
            auto indent = RepeatLitStr{"\t", static_cast<int>(indentLevel)};
            bool hasEmitted = prependNewline;
            for (unsigned int j = 0; j < vals.size(); j++) {
                if (options.disallowEmptyStructs) {
                    ::HIR::TypeRef tmp;
                    const auto& ty = localMirRes.getParamType(tmp, vals[j]);

                    // Don't emit assignment of PhantomData
                    if (vals[j].is_LValue() && mResolve.isTypePhantomData(ty)) {
                        continue;
                    }

                    // Or ZSTs
                    if (this->typeIsBadZst(ty)) {
                        continue;
                    }
                }

                if (hasEmitted) {
                    of << ";\n" << indent;
                }
                hasEmitted = true;

                emitSlot();
                of << "._" << j << " = ";
                emitParam(vals[j]);
            }
        }

        void emitDropOperation(const ::MIR::TypeResolve& localMirRes, const ::MIR::Terminator::Data_Drop& e, unsigned indentLevel) {
            auto indent = RepeatLitStr{"\t", static_cast<int>(indentLevel)};
            ::HIR::TypeRef tmp;
            const auto& ty = localMirRes.getLvalueType(tmp, e.slot);
            if (e.flagIdx != ~0u) {
                of << indent << "if( df" << e.flagIdx << " ) {\n";
            }
            switch (e.kind) {
                case ::MIR::eDropKind::SHALLOW:
                    if (const auto* ity = mResolve.isTypeOwnedBox(ty)) {
                        emitBoxDrop(indentLevel + (e.flagIdx != ~0u ? 1 : 0), ity, ty, e.slot, false);
                    } else {
                        MIR_BUG(localMirRes, "Shallow drop on non-Box - " << ty);
                    }
                    break;
                case ::MIR::eDropKind::DEEP:
                    emitDestructorCall(e.slot, ty, true, indentLevel + (e.flagIdx != ~0u ? 1 : 0));
                    break;
            }
            if (e.flagIdx != ~0u) {
                of << indent << "}\n";
            }
        }

        void emitStatement(const ::MIR::TypeResolve& localMirRes, const ::MIR::Statement& stmt, unsigned indentLevel = 1) {
            DEBUG(stmt);
            auto indent = RepeatLitStr{"\t", static_cast<int>(indentLevel)};
            switch (stmt.tag()) {
                case ::MIR::Statement::TAGDEAD:
                    throw "";
                case ::MIR::Statement::TAG_ScopeEnd:
                    of << indent << "// " << stmt << "\n";
                    break;
                case ::MIR::Statement::TAG_SetDropFlag: {
                    const auto& e = stmt.as_SetDropFlag();
                    of << indent << "df" << e.idx << " = ";
                    if (e.other == ~0u) {
                        of << e.newVal;
                    } else {
                        of << (e.newVal ? "!" : "") << "df" << e.other;
                    }
                    of << ";\n";
                    break;
                }
                    TU_ARM(stmt, SaveDropFlag, e) {
                        of << indent << "if(df" << e.idx << ") { ";
                        emitLvalue(e.slot);
                        of << ".DATA[" << (e.bitIndex / 8) << "] |= (1 << " << (e.bitIndex % 8) << ");";
                        of << " } else { ";
                        emitLvalue(e.slot);
                        of << ".DATA[" << (e.bitIndex / 8) << "] &= ~(1 << " << (e.bitIndex % 8) << ");";
                        of << " }\n";
                    }
                    break;
                    TU_ARM(stmt, LoadDropFlag, e) {
                        of << indent << "df" << e.idx << " = ((";
                        emitLvalue(e.slot);
                        of << ".DATA[" << (e.bitIndex / 8) << "] & (1 << " << (e.bitIndex % 8) << ")) != 0)";
                        of << ";\n";
                    }
                    break;
                case ::MIR::Statement::TAG_Asm:
                    this->emitAsmGcc(localMirRes, stmt.as_Asm(), indentLevel);

                    of << indent << "// ^ " << stmt << "\n";
                    break;
                case ::MIR::Statement::TAG_Asm2:
                    this->emitAsm2Gcc(localMirRes, stmt, indentLevel);

                    of << indent << "// ^ " << stmt << "\n";
                    break;
                case ::MIR::Statement::TAG_Assign: {
                    const auto& e = stmt.as_Assign();
                    DEBUG("- " << e.dst << " = " << e.src);
                    of << indent;

                    ::HIR::TypeRef tmp;
                    const auto& ty = localMirRes.getLvalueType(tmp, e.dst);
                    if (/*(e.dst.is_Deref() || e.dst.is_Field()) &&*/ this->typeIsBadZst(ty)) {
                        of << "/* ZST assign */\n";
                        break;
                    }

                TU_MATCH_HDRA( (e.src), {)
                TU_ARMA(Use, ve) {
                            ::HIR::TypeRef tmp;
                            const auto& ty = localMirRes.getLvalueType(tmp, ve);
                            if (ty == crate.types.diverge()) {
                                of << "abort()";
                                break;
                            }

                            if (ve.is_Field() && this->typeIsBadZst(ty)) {
                                of << "/* ZST field */";
                                break;
                            }

                            emitLvalue(e.dst);
                            of << " = ";
                            emitLvalue(ve);
                        }
                        TU_ARMA(Constant, ve) {
                            emitLvalue(e.dst);
                            of << " = static_cast<";
                            emitCtype(ty);
                            of << ">(";
                            emitConstant(ve, &e.dst);
                            of << ")";
                        }
                        TU_ARMA(SizedArray, ve) {
                            if (ve.count == 0) {
                            } else if (ve.count == 1) {
                                emitLvalue(e.dst);
                                of << ".DATA[0] = ";
                                emitParam(ve.val);
                            } else if (ve.count == 2) {
                                emitLvalue(e.dst);
                                of << ".DATA[0] = ";
                                emitParam(ve.val);
                                of << ";\n" << indent;
                                emitLvalue(e.dst);
                                of << ".DATA[1] = ";
                                emitParam(ve.val);
                            } else if (ve.count == 3) {
                                emitLvalue(e.dst);
                                of << ".DATA[0] = ";
                                emitParam(ve.val);
                                of << ";\n" << indent;
                                emitLvalue(e.dst);
                                of << ".DATA[1] = ";
                                emitParam(ve.val);
                                of << ";\n" << indent;
                                emitLvalue(e.dst);
                                of << ".DATA[2] = ";
                                emitParam(ve.val);
                            } else {
                                of << "for(unsigned int i = 0; i < " << ve.count << "; i ++)\n";
                                of << indent << "\t";
                                emitLvalue(e.dst);
                                of << ".DATA[i] = ";
                                emitParam(ve.val);
                            }
                        }
                        TU_ARMA(Borrow, ve) {
                            emitLvalue(e.dst);
                            const ::HIR::TypeData* pointeeTy;
                            if (const auto* borrow = ty->opt_Borrow()) {
                                pointeeTy = borrow->inner;
                            } else if (const auto* pointer = ty->opt_Pointer()) {
                                pointeeTy = pointer->inner;
                            } else {
                                MIR_BUG(localMirRes, "Borrow rvalue has non-pointer result type " << ty);
                            }
                            const auto pointerMetadata = metadataType(pointeeTy);
                            of << (pointerMetadata == MetadataType::None || pointerMetadata == MetadataType::Zero
                                         ? " = reinterpret_cast<"
                                         : " = static_cast<");
                            emitCtype(ty);
                            of << ">(";
                            if (this->typeIsBadZst(mirRes->getLvalueType(tmp, ve.val, ve.val.wrappers.size()))) {
                                of << "(void*)&rv";
                            } else {
                                emitBorrow(localMirRes, ve.type, ve.val);
                            }
                            of << ")";
                        }
                        TU_ARMA(Cast, ve) {
                            emitRvalueCast(localMirRes, e.dst, ve);
                        }
                        TU_ARMA(BinOp, ve) {
                            emitLvalue(e.dst);
                            of << " = ";
                            ::HIR::TypeRef tmp, tmpR;
                            const auto& ty = localMirRes.getParamType(tmp, ve.valL);
                            const auto& tyR = localMirRes.getParamType(tmpR, ve.valR);
                            if (ty->is_Borrow()) {
                                of << "(slice_cmp(";
                                emitParam(ve.valL);
                                of << ", ";
                                emitParam(ve.valR);
                                of << ")";
                                switch (ve.op) {
                                    case ::MIR::eBinOp::EQ:
                                        of << " == 0";
                                        break;
                                    case ::MIR::eBinOp::NE:
                                        of << " != 0";
                                        break;
                                    case ::MIR::eBinOp::GT:
                                        of << " >  0";
                                        break;
                                    case ::MIR::eBinOp::GE:
                                        of << " >= 0";
                                        break;
                                    case ::MIR::eBinOp::LT:
                                        of << " <  0";
                                        break;
                                    case ::MIR::eBinOp::LE:
                                        of << " <= 0";
                                        break;
                                    default:
                                        MIR_BUG(localMirRes, "Unknown comparison of a &-ptr - " << e.src << " with " << ty);
                                }
                                of << ")";
                                break;
                            } else if (const auto* te = ty->opt_Pointer()) {
                                if (isDst(te->inner)) {
                                    switch (ve.op) {
                                        case ::MIR::eBinOp::EQ:
                                            emitParam(ve.valL);
                                            of << ".PTR == ";
                                            emitParam(ve.valR);
                                            of << ".PTR && ";
                                            emitParam(ve.valL);
                                            of << ".META == ";
                                            emitParam(ve.valR);
                                            of << ".META";
                                            break;
                                        case ::MIR::eBinOp::NE:
                                            emitParam(ve.valL);
                                            of << ".PTR != ";
                                            emitParam(ve.valR);
                                            of << ".PTR || ";
                                            emitParam(ve.valL);
                                            of << ".META != ";
                                            emitParam(ve.valR);
                                            of << ".META";
                                            break;
                                        default:
                                            MIR_BUG(localMirRes, "Unknown comparison of a *-ptr - " << e.src << " with " << ty);
                                    }
                                } else {
                                    emitParam(ve.valL);
                                    switch (ve.op) {
                                        case ::MIR::eBinOp::EQ:
                                            of << " == ";
                                            break;
                                        case ::MIR::eBinOp::NE:
                                            of << " != ";
                                            break;
                                        case ::MIR::eBinOp::GT:
                                            of << " > ";
                                            break;
                                        case ::MIR::eBinOp::GE:
                                            of << " >= ";
                                            break;
                                        case ::MIR::eBinOp::LT:
                                            of << " < ";
                                            break;
                                        case ::MIR::eBinOp::LE:
                                            of << " <= ";
                                            break;
                                        default:
                                            MIR_BUG(localMirRes, "Unknown comparison of a *-ptr - " << e.src << " with " << ty);
                                    }
                                    emitParam(ve.valR);
                                }
                                break;
                            } else if (ve.op == ::MIR::eBinOp::MOD && (ty == ::HIR::CoreType::F32 || ty == ::HIR::CoreType::F64)) {
                                of << "__builtin_";
                                if (ty == ::HIR::CoreType::F32) {
                                    of << "remainderf";
                                } else {
                                    of << "remainder";
                                }
                                of << "(";
                                emitParam(ve.valL);
                                of << ", ";
                                emitParam(ve.valR);
                                of << ")";
                                break;
                            } else if (ty == ::HIR::CoreType::F16 || ty == ::HIR::CoreType::F128) {
                                auto tyS = ty == ::HIR::CoreType::F16 ? "f16" : "f128";
                                switch (ve.op) {
                                    case ::MIR::eBinOp::EQ:
                                        of << "0 == ";
                                        if (0) {
                                            case ::MIR::eBinOp::NE:
                                                of << "0 != ";
                                        }
                                        if (0) {
                                            case ::MIR::eBinOp::GT:
                                                of << "0 > ";
                                        }
                                        if (0) {
                                            case ::MIR::eBinOp::GE:
                                                of << "0 >= ";
                                        }
                                        if (0) {
                                            case ::MIR::eBinOp::LT:
                                                of << "0 < ";
                                        }
                                        if (0) {
                                            case ::MIR::eBinOp::LE:
                                                of << "0 <= ";
                                        }
                                        // NOTE: Reversed order due to reversed logic above
                                        of << tyS << "_cmp(";
                                        emitParam(ve.valR);
                                        of << ", ";
                                        emitParam(ve.valL);
                                        of << ")";
                                        break;
                                    default:
                                        of << tyS << "_disabled()";
                                        break;
                                }
                                break;
                            } else if (typeIsEmulatedI128(ty)) {
                                switch (ve.op) {
                                    case ::MIR::eBinOp::ADD:
                                        of << "add128";
                                        if (0) {
                                            case ::MIR::eBinOp::SUB:
                                                of << "sub128";
                                        }
                                        if (0) {
                                            case ::MIR::eBinOp::MUL:
                                                of << "mul128";
                                        }
                                        if (0) {
                                            case ::MIR::eBinOp::DIV:
                                                of << "div128";
                                        }
                                        if (0) {
                                            case ::MIR::eBinOp::MOD:
                                                of << "mod128";
                                        }
                                        if (0) {
                                            case ::MIR::eBinOp::BIT_OR:
                                                of << "or128";
                                        }
                                        if (0) {
                                            case ::MIR::eBinOp::BIT_AND:
                                                of << "and128";
                                        }
                                        if (0) {
                                            case ::MIR::eBinOp::BIT_XOR:
                                                of << "xor128";
                                        }
                                        if (ty == ::HIR::CoreType::I128) {
                                            of << "s";
                                        }
                                        of << "(";
                                        emitParam(ve.valL);
                                        of << ", ";
                                        emitParam(ve.valR);
                                        of << ")";
                                        break;
                                    case ::MIR::eBinOp::BIT_SHR:
                                        of << "shr128";
                                        if (0) {
                                            case ::MIR::eBinOp::BIT_SHL:
                                                of << "shl128";
                                        }
                                        if (ty == ::HIR::CoreType::I128) {
                                            of << "s";
                                        }
                                        of << "(";
                                        emitParam(ve.valL);
                                        of << ", ";
                                        emitParam(ve.valR);
                                        if ((tyR == ::HIR::CoreType::I128 || tyR == ::HIR::CoreType::U128)) {
                                            of << ".lo";
                                        }
                                        of << ")";
                                        break;

                                    case ::MIR::eBinOp::EQ:
                                        of << "0 == ";
                                        if (0) {
                                            case ::MIR::eBinOp::NE:
                                                of << "0 != ";
                                        }
                                        if (0) {
                                            case ::MIR::eBinOp::GT:
                                                of << "0 > ";
                                        }
                                        if (0) {
                                            case ::MIR::eBinOp::GE:
                                                of << "0 >= ";
                                        }
                                        if (0) {
                                            case ::MIR::eBinOp::LT:
                                                of << "0 < ";
                                        }
                                        if (0) {
                                            case ::MIR::eBinOp::LE:
                                                of << "0 <= ";
                                        }
                                        // NOTE: Reversed order due to reversed logic above
                                        of << "cmp128";
                                        if (ty == ::HIR::CoreType::I128) {
                                            of << "s";
                                        }
                                        of << "(";
                                        emitParam(ve.valR);
                                        of << ", ";
                                        emitParam(ve.valL);
                                        of << ")";
                                        break;

                                    case ::MIR::eBinOp::ADD_OV:
                                    case ::MIR::eBinOp::SUB_OV:
                                    case ::MIR::eBinOp::MUL_OV:
                                    case ::MIR::eBinOp::DIV_OV:
                                        MIR_TODO(localMirRes, "Overflowing binops for emulated i128");
                                        break;
                                }
                                break;
                            } else {
                            }

                            emitParam(ve.valL);
                            switch (ve.op) {
                                case ::MIR::eBinOp::ADD:
                                    of << " + ";
                                    break;
                                case ::MIR::eBinOp::SUB:
                                    of << " - ";
                                    break;
                                case ::MIR::eBinOp::MUL:
                                    of << " * ";
                                    break;
                                case ::MIR::eBinOp::DIV:
                                    of << " / ";
                                    break;
                                case ::MIR::eBinOp::MOD:
                                    of << " % ";
                                    break;

                                case ::MIR::eBinOp::BIT_OR:
                                    of << " | ";
                                    break;
                                case ::MIR::eBinOp::BIT_AND:
                                    of << " & ";
                                    break;
                                case ::MIR::eBinOp::BIT_XOR:
                                    of << " ^ ";
                                    break;
                                case ::MIR::eBinOp::BIT_SHR:
                                    of << " >> ";
                                    break;
                                case ::MIR::eBinOp::BIT_SHL:
                                    of << " << ";
                                    break;
                                case ::MIR::eBinOp::EQ:
                                    of << " == ";
                                    break;
                                case ::MIR::eBinOp::NE:
                                    of << " != ";
                                    break;
                                case ::MIR::eBinOp::GT:
                                    of << " > ";
                                    break;
                                case ::MIR::eBinOp::GE:
                                    of << " >= ";
                                    break;
                                case ::MIR::eBinOp::LT:
                                    of << " < ";
                                    break;
                                case ::MIR::eBinOp::LE:
                                    of << " <= ";
                                    break;

                                case ::MIR::eBinOp::ADD_OV:
                                case ::MIR::eBinOp::SUB_OV:
                                case ::MIR::eBinOp::MUL_OV:
                                case ::MIR::eBinOp::DIV_OV:
                                    MIR_TODO(localMirRes, "Overflow");
                                    break;
                            }
                            emitParam(ve.valR);
                            if (typeIsEmulatedI128(tyR)) {
                                of << ".lo";
                            }
                        }
                        TU_ARMA(UniOp, ve) {
                            ::HIR::TypeRef tmp;
                            const auto& ty = localMirRes.getLvalueType(tmp, e.dst);

                            if (typeIsEmulatedI128(ty)) {
                                switch (ve.op) {
                                    case ::MIR::eUniOp::NEG:
                                        emitLvalue(e.dst);
                                        of << " = neg128s(";
                                        emitLvalue(ve.val);
                                        of << ")";
                                        break;
                                    case ::MIR::eUniOp::INV:
                                        emitLvalue(e.dst);
                                        of << ".lo = ~";
                                        emitLvalue(ve.val);
                                        of << ".lo; ";
                                        emitLvalue(e.dst);
                                        of << ".hi = ~";
                                        emitLvalue(ve.val);
                                        of << ".hi";
                                        break;
                                }
                                break;
                            } else if (ty == ::HIR::CoreType::F16) {
                                switch (ve.op) {
                                    case ::MIR::eUniOp::NEG:
                                        emitLvalue(e.dst);
                                        of << " = f16_disabled(/*";
                                        emitLvalue(ve.val);
                                        of << "*/)";
                                        break;
                                    case ::MIR::eUniOp::INV:
                                        MIR_TODO(*mirRes, "f16 INV");
                                        break;
                                }
                                break;
                            } else if (ty == ::HIR::CoreType::F128) {
                                switch (ve.op) {
                                    case ::MIR::eUniOp::NEG:
                                        emitLvalue(e.dst);
                                        of << " = f128_disabled(/*";
                                        emitLvalue(ve.val);
                                        of << "*/)";
                                        break;
                                    case ::MIR::eUniOp::INV:
                                        MIR_TODO(*mirRes, "f128 INV");
                                        break;
                                }
                                break;
                            }

                            emitLvalue(e.dst);
                            of << " = ";
                            switch (ve.op) {
                                case ::MIR::eUniOp::NEG:
                                    of << "-";
                                    break;
                                case ::MIR::eUniOp::INV:
                                    if (ty == ::HIR::CoreType::Bool) {
                                        of << "!";
                                    } else {
                                        of << "~";
                                    }
                                    break;
                            }
                            emitLvalue(ve.val);
                        }
                        TU_ARMA(DstMeta, ve) {
                            emitLvalue(e.dst);
                            // TODO: Why? Probably for getting `VTable`
                            if (ty->is_Primitive() || ty->is_Pointer() || ty->is_Borrow()) {
                            } else {
                                of << "._0._0";
                            }
                            of << " = static_cast<decltype(";
                            emitLvalue(e.dst);
                            if (ty->is_Primitive() || ty->is_Pointer() || ty->is_Borrow()) {
                            } else {
                                of << "._0._0";
                            }
                            of << ")>(";
                            emitLvalue(ve.val);
                            of << ".META)";
                        }
                        TU_ARMA(DstPtr, ve) {
                            emitLvalue(e.dst);
                            of << " = static_cast<";
                            emitCtype(ty);
                            of << ">(";
                            emitLvalue(ve.val);
                            of << ".PTR)";
                        }
                        TU_ARMA(MakeDst, ve) {
                            emitLvalue(e.dst);
                            of << " = static_cast<";
                            emitCtype(ty);
                            of << ">(";
                            auto meta = metadataType(ty->is_Pointer() ? ty->as_Pointer().inner : ty->as_Borrow().inner);
                            switch (meta) {
                                case MetadataType::Slice:
                                    of << "make_sliceptr";
                                    of << "(";
                                    emitParam(ve.ptrVal, false);
                                    of << ", ";
                                    emitParam(ve.metaVal);
                                    of << ")";
                                    break;
                                case MetadataType::TraitObject:
                                    of << "make_traitobjptr";
                                    of << "(";
                                    emitParam(ve.ptrVal);
                                    of << ", ";
                                    emitTraitMetadataParam(localMirRes, ve.metaVal);
                                    of << ")";
                                    break;
                                case MetadataType::Zero:
                                case MetadataType::Unknown:
                                case MetadataType::None:
                                    of << "(void*)";
                                    emitParam(ve.ptrVal);
                                    break;
                            }
                            of << ")";
                        }
                        TU_ARMA(Tuple, ve) {
                            emitCompositeAssign(localMirRes, [&]() {
                                emitLvalue(e.dst);
                            }, ve.vals, indentLevel);
                        }
                        TU_ARMA(Array, ve) {
                            for (unsigned int j = 0; j < ve.vals.size(); j++) {
                                if (j != 0) {
                                    of << ";\n" << indent;
                                }
                                emitLvalue(e.dst);
                                of << ".DATA[" << j << "] = ";
                                emitParam(ve.vals[j]);
                            }
                        }
                        TU_ARMA(UnionVariant, ve) {
                            MIR_ASSERT(localMirRes, crate.getTypeitemByPath(sp, ve.path.mPath).is_Union(), "");
                            if (!this->typeIsBadZst(mirRes->getParamType(tmp, ve.val))) {
                                emitLvalue(e.dst);
                                of << ".var_" << ve.index << " = ";
                                emitParam(ve.val);
                            }
                        }
                        TU_ARMA(EnumVariant, ve) {
                            const auto& tyi = crate.getTypeitemByPath(sp, ve.path.mPath);
                            MIR_ASSERT(localMirRes, tyi.is_Enum(), "");
                            const auto* enmP = &tyi.as_Enum();

                            ::HIR::TypeRef tmp;
                            const auto& ty = localMirRes.getLvalueType(tmp, e.dst);
                            auto* repr = TargetGetTypeRepr(sp, mResolve, ty);

                    TU_MATCH_HDRA( (repr->variants), {)
                    TU_ARMA(None, re) {
                                    emitCompositeAssign(localMirRes, [&]() {
                                        emitLvalue(e.dst);
                                        of << ".DATA.var_0";
                                    }, /*repr->fields[0].ty,*/ ve.vals, indentLevel);
                                }
                                TU_ARMA(NonZero, re) {
                                    MIR_ASSERT(*mirRes, ve.index < 2, "");
                                    if (ve.index == re.zeroVariant) {
                                        // TODO: Use nonzero_path
                                        of << "memset(&";
                                        emitLvalue(e.dst);
                                        of << ", 0, sizeof(";
                                        emitCtype(ty);
                                        of << "))";
                                    } else {
                                        emitCompositeAssign(localMirRes, [&]() {
                                            emitLvalue(e.dst);
                                            of << ".DATA.var_" << ve.index;
                                        }, /*repr->fields[0].ty,*/ ve.vals, indentLevel, /*prepend_newline=*/false);
                                    }
                                }
                                TU_ARMA(Linear, re) {
                                    bool emitNewline = false;
                                    if (!re.isNiche(ve.index)) {
                                        // Each variant has its own tag field, it will be the last numbered field in that variant slot
                                        // - Only use that if there isn't an explicit tag field in the enum
                                        if (re.field.subFields.empty() || typeIsBadZst(repr->fields[ve.index].ty)) {
                                            emitLvalue(e.dst);
                                            const auto& slotTy = emitEnumPath(repr, re.field);
                                            of << " = ";
                                            if (slotTy->is_Pointer() || slotTy->is_Borrow() || slotTy->is_Function()) {
                                                of << "(";
                                                emitCtype(slotTy);
                                                of << ")(uintptr_t)";
                                            }
                                            of << (re.offset + ve.index);
                                        } else {
                                            auto vr = TargetGetTypeRepr(sp, mResolve, repr->fields[ve.index].ty);
                                            //m_of << "assert(&";
                                            //emit_lvalue(e.dst); m_of << ".DATA.var_" << ve.index << "._" << (vr->fields.size() - 1);
                                            //m_of << " == &";
                                            //emit_lvalue(e.dst); emit_enum_path(repr, re.field);
                                            //m_of << "); ";
                                            emitLvalue(e.dst);
                                            of << ".DATA.var_" << ve.index << "._" << (vr->fields.size() - 1) << " = ";
                                            const auto& slotTy = vr->fields.back().ty;
                                            if (slotTy->is_Pointer() || slotTy->is_Borrow() || slotTy->is_Function()) {
                                                of << "(";
                                                emitCtype(slotTy);
                                                of << ")(uintptr_t)";
                                            }
                                            of << (re.offset + ve.index);
                                        }
                                        emitNewline = true;
                                    } else {
                                        of << "/* Niche tag */";
                                    }
                                    if (enmP->isValue()) {
                                        // Value enums have no data fields
                                    } else {
                                        emitCompositeAssign(localMirRes, [&]() {
                                            emitLvalue(e.dst);
                                            of << ".DATA.var_" << ve.index;
                                        }, ve.vals, indentLevel, emitNewline);
                                    }
                                }
                                TU_ARMA(Values, re) {
                                    if (re.field.index == 0) {
                                        emitLvalue(e.dst);
                                        of << ".TAG = ";
                                        emitEnumVariantVal(repr, ve.index);
                                    } else {
                                        emitLvalue(e.dst);
                                        of << ".DATA.TAG = ";
                                        emitEnumVariantVal(repr, ve.index);
                                    }
                                    if (!enmP->isValue()) {
                                        emitCompositeAssign(localMirRes, [&]() {
                                            emitLvalue(e.dst);
                                            of << ".DATA.var_" << ve.index;
                                        }, ve.vals, indentLevel, true);
                                    }
                                }
                    }
                        }
                        TU_ARMA(Struct, ve) {
                            if (ve.vals.empty()) {
                                if (options.disallowEmptyStructs) {
                                    emitLvalue(e.dst);
                                    of << "._d = 0";
                                }
                            } else {
                                emitCompositeAssign(localMirRes, [&]() {
                                    emitLvalue(e.dst);
                                }, ve.vals, indentLevel, /*emit_newline=*/false);
                            }
                        }
                }
                of << ";";
                of << "\t// " << e.dst << " = " << e.src;
                of << "\n";
                break;
                }
            }
        }

        void emitRvalueCast(const ::MIR::TypeResolve& localMirRes, const ::MIR::LValue& dst, const ::MIR::RValue::Data_Cast& ve) {
            if (mResolve.isTypePhantomData(ve.type)) {
                of << "/* PhantomData cast */\n";
                return;
            }

            ::HIR::TypeRef tmp;
            const auto& ty = localMirRes.getLvalueType(tmp, ve.val);

            // A cast to a fat pointer doesn't actually change the C type.
            if ((ve.type->is_Pointer() && isDst(ve.type->as_Pointer().inner)) ||
                (ve.type->is_Borrow() && isDst(ve.type->as_Borrow().inner))
                // OR: If it's a no-op cast
                || ve.type == ty) {
                emitLvalue(dst);
                of << " = ";
                emitLvalue(ve.val);
                return;
            }

            // Cast of a named function to a function pointer - originate the pointer
            if (ve.type->is_Function() && ty->is_NamedFunction()) {
                emitLvalue(dst);
                of << " = " << TransMangle(ty->as_NamedFunction().path);
                return;
            }

            // Emulated i128/u128 support
            if (options.emulatedI128 && (ve.type == ::HIR::CoreType::U128 || ve.type == ::HIR::CoreType::I128 || ty == ::HIR::CoreType::U128 || ty == ::HIR::CoreType::I128)) {
                // Destination
                MIR_ASSERT(localMirRes, ve.type->is_Primitive(), "i128/u128 cast to non-primitive - " << ve.type);
                MIR_ASSERT(localMirRes, ty->is_Primitive() || (ty->is_Path() && ty->as_Path().binding.is_Enum()), "i128/u128 cast from non-primitive - " << ty);
                switch (ve.type->as_Primitive()) {
                    case ::HIR::CoreType::I128:
                    case ::HIR::CoreType::U128:
                        if (ty == ::HIR::CoreType::I128 || ty == ::HIR::CoreType::U128) {
                            // Cast between i128 and u128
                            emitLvalue(dst);
                            of << ".lo = ";
                            emitLvalue(ve.val);
                            of << ".lo; ";
                            emitLvalue(dst);
                            of << ".hi = ";
                            emitLvalue(ve.val);
                            of << ".hi";
                        } else if (ty->is_Path() && ty->as_Path().binding.is_Enum()) {
                            emitLvalue(dst);
                            of << ".lo = ";
                            emitLvalue(ve.val);
                            of << ".TAG; ";
                            emitLvalue(dst);
                            of << ".hi = ";
                            emitLvalue(ve.val);
                            of << ".TAG < 0 ? -1 : 0";
                        } else {
                            // Cast from small to i128/u128
                            emitLvalue(dst);
                            of << ".lo = ";
                            emitLvalue(ve.val);
                            of << "; ";
                            emitLvalue(dst);
                            of << ".hi = ";
                            emitLvalue(ve.val);
                            of << " < 0 ? -1 : 0";
                        }
                        break;
                    case ::HIR::CoreType::I8:
                    case ::HIR::CoreType::I16:
                    case ::HIR::CoreType::I32:
                    case ::HIR::CoreType::I64:
                    case ::HIR::CoreType::Isize:
                    case ::HIR::CoreType::U8:
                    case ::HIR::CoreType::U16:
                    case ::HIR::CoreType::U32:
                    case ::HIR::CoreType::U64:
                    case ::HIR::CoreType::Usize:
                        emitLvalue(dst);
                        of << " = ";
                        switch (ty->as_Primitive()) {
                            case ::HIR::CoreType::U128:
                            case ::HIR::CoreType::I128:
                                emitLvalue(ve.val);
                                of << ".lo";
                                break;
                            default:
                                MIR_BUG(localMirRes, "Unreachable");
                        }
                        break;
                    case ::HIR::CoreType::F16:
                        MIR_TODO(localMirRes, "f16 from i128/u128");
                    case ::HIR::CoreType::F32:
                        emitLvalue(dst);
                        of << " = ";
                        switch (ty->as_Primitive()) {
                            case ::HIR::CoreType::U128:
                                of << "cast128_float(";
                                emitLvalue(ve.val);
                                of << ")";
                                break;
                            case ::HIR::CoreType::I128:
                                of << "cast128s_float(";
                                emitLvalue(ve.val);
                                of << ")";
                                break;
                            default:
                                MIR_BUG(localMirRes, "Unreachable");
                        }
                        break;
                    case ::HIR::CoreType::F64:
                        emitLvalue(dst);
                        of << " = ";
                        switch (ty->as_Primitive()) {
                            case ::HIR::CoreType::U128:
                                of << "cast128_double(";
                                emitLvalue(ve.val);
                                of << ")";
                                break;
                            case ::HIR::CoreType::I128:
                                of << "cast128s_double(";
                                emitLvalue(ve.val);
                                of << ")";
                                break;
                            default:
                                MIR_BUG(localMirRes, "Unreachable");
                        }
                        break;
                    case ::HIR::CoreType::F128:
                        MIR_TODO(localMirRes, "f128 from i128/u128");
                    default:
                        MIR_BUG(localMirRes, "Bad i128/u128 cast - " << ty << " to " << ve.type);
                }
                return;
            }
            if (ve.type == ::HIR::CoreType::F16 || ve.type == ::HIR::CoreType::F128 || ty == ::HIR::CoreType::F16 || ty == ::HIR::CoreType::F128) {
                of << "abort()";
                return;
            }

            // Standard cast
            ::HIR::TypeRef dstTmp;
            const auto& dstTy = localMirRes.getLvalueType(dstTmp, dst);
            emitLvalue(dst);
            of << " = ";
            of << "(";
            emitCtype(dstTy);
            of << ")";
            // TODO: If the source is an unsized borrow, then extract the pointer
            bool special = false;
            // If the destination is a thin pointer
            if (ve.type->is_Pointer() && !isDst(ve.type->as_Pointer().inner)) {
                // NOTE: Checks the result of the deref
                if ((ty->is_Borrow() && isDst(ty->as_Borrow().inner)) || (ty->is_Pointer() && isDst(ty->as_Pointer().inner))) {
                    emitLvalue(ve.val);
                    of << ".PTR";
                    special = true;
                }
            }
            if (ty->is_NamedFunction()) {
                of << TransMangle(ty->as_NamedFunction().path);
                special = true;
            }
            if (ve.type->is_Primitive() && ty->is_Path() && ty->as_Path().binding.is_Enum()) {
                emitLvalue(ve.val);
                // NOTE: Embedded tag enums can't be cast
                of << ".TAG";
                special = true;
            }
            if (!special) {
                emitLvalue(ve.val);
            }
        }

        void emitTermSwitch(const ::MIR::TypeResolve& localMirRes, const ::MIR::LValue& val, size_t nArms, unsigned indentLevel, ::std::function<void(size_t)> cb, size_t oddArm = -1) {
            auto indent = RepeatLitStr{"\t", static_cast<int>(indentLevel)};

            ::HIR::TypeRef tmp;
            const auto& ty = localMirRes.getLvalueType(tmp, val);
            MIR_ASSERT(localMirRes, ty->is_Path(), "Switch over non-Path type");
            MIR_ASSERT(localMirRes, ty->as_Path().binding.is_Enum(), "Switch over non-enum");
            const auto* repr = TargetGetTypeRepr(localMirRes.sp, mResolve, ty);
            MIR_ASSERT(localMirRes, repr, "No repr for " << ty);

            struct MaybeSigned64 {
                bool is_signed;
                uint64_t v;

                MaybeSigned64(bool is_signed, uint64_t v)
                    : is_signed(is_signed)
                    , v(v)
                {
                }

                void fmt(std::ostream& os) const {
                    if (is_signed) {
                        os << static_cast<int64_t>(v);
                    } else {
                        os << v;
                    }
                }

                //friend std::ostream& operator<<(std::ostream& os, const MaybeSigned64& x) {
                //    x.fmt(os);
                //    return os;
                //}
            };

            TU_MATCH_HDRA( (repr->variants), {)
            TU_ARMA(NonZero, e) {
                    MIR_ASSERT(localMirRes, nArms == 2, "NonZero optimised switch without two arms");
                    // If this is an emulated i128, check both fields
                    of << indent << "if( ";
                    emitLvalue(val);
                    const auto& slotTy = emitEnumPath(repr, e.field);
                    MIR_ASSERT(localMirRes, slotTy->is_Pointer() || slotTy->is_Function() || slotTy->is_Borrow() || slotTy->is_Primitive(), "Invalid niche type: " << slotTy << " in " << ty);
                    if (typeIsEmulatedI128(slotTy)) {
                        of << ".lo == 0 && ";
                        emitLvalue(val);
                        emitEnumPath(repr, e.field);
                        of << ".hi";
                    }
                    of << " != 0 )\n";
                    of << indent << "\t";
                    cb(1 - e.zeroVariant);
                    of << "\n";
                    of << indent << "else\n";
                    of << indent << "\t";
                    cb(e.zeroVariant);
                    of << "\n";
                }
                TU_ARMA(Linear, e) {
                    const auto& tagTy = TargetGetInnerType(sp, mResolve, *repr, e.field.index, e.field.subFields);
                    const bool pointerTag = tagTy->is_Pointer() || tagTy->is_Borrow() || tagTy->is_Function();
                    if (!pointerTag) {
                        switch (tagTy->as_Primitive()) {
                            case ::HIR::CoreType::Bool:
                            case ::HIR::CoreType::U8:
                            case ::HIR::CoreType::I8:
                            case ::HIR::CoreType::U16:
                            case ::HIR::CoreType::I16:
                            case ::HIR::CoreType::U32:
                            case ::HIR::CoreType::I32:
                            case ::HIR::CoreType::U64:
                            case ::HIR::CoreType::I64:
                            case ::HIR::CoreType::Usize:
                            case ::HIR::CoreType::Isize:
                            case ::HIR::CoreType::Char:
                                break;
                            default:
                                MIR_BUG(localMirRes, "Invalid tag type?! " << tagTy);
                        }
                    }

                    auto emitVariant = [&]() {
#if 1
                        if (pointerTag) {
                            of << "(uintptr_t)";
                        }
                        emitLvalue(val);
                        emitEnumPath(repr, e.field);
#else
                        // Emit using a pointer manipulation, to avoid `union` "active member" rule
                        // - Technically not type punning, as the type is the same in all cases
                        // Get the offset
                        size_t offset = repr->getOffset(sp, mResolve, e.field);
                        ;
                        // Emit
                        of << " *(";
                        emitCtype(tagTy);
                        of << "*)(";
                        of << "(const char*)&";
                        emitLvalue(val);
                        of << " + " << offset;
                        _of << ")";
#endif
                    };

                    // Optimisation: If there's only one arm with a different value, then emit an `if` isntead of a `switch`
                    if (oddArm != static_cast<size_t>(-1)) {
                        of << indent << "if( ";
                        emitVariant();
                        if (e.isNiche(oddArm)) {
                            of << " < " << e.offset;
                        } else {
                            of << " == " << (e.offset + oddArm);
                        }
                        of << ") {";
                        cb(oddArm);
                        of << "} else {";
                        cb(oddArm == 0 ? 1 : 0);
                        of << "}\n";
                    } else {
                        of << indent << "switch(";
                        emitVariant();
                        of << ") {\n";
                        for (size_t j = 0; j < nArms; j++) {
                            if (e.isNiche(j)) {
                                continue;
                            }
                            // Handle signed values
                            of << indent << "case " << (e.offset + j) << ": ";
                            cb(j);
                            of << "break;\n";
                        }
                        of << indent << "default: ";
                        if (e.usesNiche()) {
                            cb(e.field.index);
                            of << "break;";
                        } else {
                            of << "abort();";
                        }
                        of << "\n";
                        of << indent << "}\n";
                    }
                }
                TU_ARMA(Values, e) {
                    const auto& tagTy = TargetGetInnerType(sp, mResolve, *repr, e.field.index, e.field.subFields);
                    bool is_signed = false;
                    switch (tagTy->as_Primitive()) {
                        case ::HIR::CoreType::I8:
                        case ::HIR::CoreType::I16:
                        case ::HIR::CoreType::I32:
                        case ::HIR::CoreType::I64:
                        case ::HIR::CoreType::Isize:
                        case ::HIR::CoreType::I128:
                            is_signed = true;
                            break;
                        case ::HIR::CoreType::Bool:
                        case ::HIR::CoreType::U8:
                        case ::HIR::CoreType::U16:
                        case ::HIR::CoreType::U32:
                        case ::HIR::CoreType::U64:
                        case ::HIR::CoreType::Usize:
                        case ::HIR::CoreType::Char:
                        case ::HIR::CoreType::U128:
                            is_signed = false;
                            break;
                        case ::HIR::CoreType::F16:
                        case ::HIR::CoreType::F32:
                        case ::HIR::CoreType::F64:
                        case ::HIR::CoreType::F128:
                            MIR_TODO(localMirRes, "Floating point enum tag.");
                            break;
                        case ::HIR::CoreType::Str:
                            MIR_BUG(localMirRes, "Unsized tag?!");
                    }

                    const bool is128 = tagTy == ::HIR::CoreType::I128 || tagTy == ::HIR::CoreType::U128;
                    const bool emulated128 = typeIsEmulatedI128(tagTy);
                    auto emitTag = [&]() {
                        emitLvalue(val);
                        emitEnumPath(repr, e.field);
                    };
                    auto emitEqual = [&](size_t variant) {
                        if (emulated128) {
                            of << (is_signed ? "cmp128s(" : "cmp128(");
                            emitTag();
                            of << ", ";
                            emitEnumVariantVal(repr, variant);
                            of << ") == 0";
                        } else {
                            emitTag();
                            of << " == ";
                            emitEnumVariantVal(repr, variant);
                        }
                    };

                    // Optimisation: If there's only one arm with a different value, then emit an `if` isntead of a `switch`
                    if (oddArm != static_cast<size_t>(-1)) {
                        of << indent << "if(";
                        emitEqual(oddArm);
                        of << ") {";
                        cb(oddArm);
                        of << "} else {";
                        cb(oddArm == 0 ? 1 : 0);
                        of << "}\n";
                        return;
                    }

                    if (is128) {
                        for (size_t j = 0; j < nArms; j++) {
                            of << indent << (j == 0 ? "if(" : "else if(");
                            emitEqual(j);
                            of << ") {";
                            cb(j);
                            of << "}\n";
                        }
                        of << indent << "else { abort(); }\n";
                        return;
                    }

                    of << indent << "switch(";
                    emitTag();
                    of << ") {\n";
                    for (size_t j = 0; j < nArms; j++) {
                        // Handle signed values
                        if (is_signed) {
                            of << indent << "case " << S128(e.values[j]).truncateI64() << "ll: ";
                        } else {
                            of << indent << "case " << e.values[j].truncateU64() << "ull: ";
                        }
                        cb(j);
                        of << "break;\n";
                    }
                    of << indent << "default: abort();\n";
                    of << indent << "}\n";
                }
                TU_ARMA(None, e) {
                    of << indent;
                    cb(0);
                    of << "\n";
                }
            }
        }

        void emitTermSwitchvalue(const ::MIR::TypeResolve& localMirRes, const ::MIR::LValue& val, const ::MIR::SwitchValues& values, unsigned indentLevel, ::std::function<void(size_t)> cb) {
            auto indent = RepeatLitStr{"\t", static_cast<int>(indentLevel)};

            ::HIR::TypeRef tmp;
            const auto& ty = localMirRes.getLvalueType(tmp, val);
            if (const auto* ve = values.opt_String()) {
                of << indent << "{ static SLICE_PTR switch_strings[] = {";
                for (const auto& v : *ve) {
                    of << " {(void*)";
                    this->printEscapedString(v);
                    of << "," << v.size() << "},";
                }
                of << " {0,0} };\n";
                of << indent << "switch( mrustc_string_search_linear(";
                emitLvalue(val);
                of << ", " << ve->size() << ", switch_strings) ) {\n";
                for (size_t i = 0; i < ve->size(); i++) {
                    of << indent << "case " << i << ": ";
                    cb(i);
                    of << " break;\n";
                }
                of << indent << "default: ";
                cb(SIZE_MAX);
                of << "\n";
                of << indent << "} }\n";
            } else if (const auto* ve = values.opt_ByteString()) {
                of << indent << "{ static SLICE_PTR switch_strings[] = {";
                for (const auto& v : *ve) {
                    of << " {(void*)";
                    this->printEscapedString(v);
                    of << "," << v.size() << "},";
                }
                of << " {0,0} };\n";
                HIR::TypeRef tmp;
                const auto& ty = localMirRes.getLvalueType(tmp, val);
                of << indent << "switch( mrustc_string_search_linear(";
                if (const auto* a = ty->as_Borrow().inner->opt_Array()) {
                    auto len = a->size.as_Known();
                    of << "make_sliceptr(";
                    emitLvalue(val);
                    of << "->DATA, " << len << ")";
                } else {
                    emitLvalue(val);
                }
                of << ", " << ve->size() << ", switch_strings) ) {\n";
                for (size_t i = 0; i < ve->size(); i++) {
                    of << indent << "case " << i << ": ";
                    cb(i);
                    of << " break;\n";
                }
                of << indent << "default: ";
                cb(SIZE_MAX);
                of << "\n";
                of << indent << "} }\n";
            } else if (const auto* ve = values.opt_Unsigned()) {
                const bool emulatedU128 = options.emulatedI128 && ty == ::HIR::CoreType::U128;
                if (emulatedU128) {
                    of << indent << "if(";
                    emitLvalue(val);
                    of << ".hi != 0) { ";
                    cb(SIZE_MAX);
                    of << " }\n";
                }
                of << indent << (emulatedU128 ? "else " : "") << "switch(";
                emitLvalue(val);
                if (emulatedU128) {
                    of << ".lo";
                }
                of << ") {\n";
                for (size_t i = 0; i < ve->size(); i++) {
                    of << indent << "\tcase " << (*ve)[i] << "ull: ";
                    cb(i);
                    of << " break;\n";
                }
                of << indent << "\tdefault: ";
                cb(SIZE_MAX);
                of << "\n";
                of << indent << "}\n";
            } else if (const auto* ve = values.opt_Signed()) {
                //assert(ve->size() == e.targets.size());
                const bool emulatedI128 = options.emulatedI128 && ty == ::HIR::CoreType::I128;
                if (emulatedI128) {
                    of << indent << "if(";
                    emitLvalue(val);
                    of << ".hi != ((int64_t)";
                    emitLvalue(val);
                    of << ".lo < 0 ? UINT64_MAX : 0)) { ";
                    cb(SIZE_MAX);
                    of << " }\n";
                }
                of << indent << (emulatedI128 ? "else " : "") << "switch(";
                if (emulatedI128) {
                    of << "(int64_t)";
                }
                emitLvalue(val);
                if (emulatedI128) {
                    of << ".lo";
                }
                of << ") {\n";
                for (size_t i = 0; i < ve->size(); i++) {
                    of << indent << "\tcase ";
                    if ((*ve)[i] == INT64_MIN) {
                        of << "INT64_MIN";
                    } else {
                        of << (*ve)[i] << "ll";
                    }
                    of << ": ";
                    cb(i);
                    of << " break;\n";
                }
                of << indent << "\tdefault: ";
                cb(SIZE_MAX);
                of << "\n";
                of << indent << "}\n";
            } else {
                MIR_BUG(localMirRes, "SwitchValue with unknown value type - " << values.tagStr());
            }
        }

        void emitTermCall(const ::MIR::TypeResolve& localMirRes, const ::MIR::Terminator::Data_Call& e, unsigned indentLevel) {
            auto indent = RepeatLitStr{"\t", static_cast<int>(indentLevel)};
            of << indent;

            bool hasZst = false;
            for (unsigned int j = 0; j < e.args.size(); j++) {
                ::HIR::TypeRef tmp;
                const auto& ty = mirRes->getParamType(tmp, e.args[j]);
                if (options.disallowEmptyStructs /*&& TU_TEST1(e.args[j], LValue, .is_Field())*/) {
                    if (this->typeIsBadZst(ty)) {
                        if (!hasZst) {
                            of << "{\n";
                            indent.n++;
                            of << indent;
                            hasZst = true;
                        }
                        emitCtype(ty, FMT_CB(ss, ss << "zarg" << j;));
                        of << " = {0};\n";
                        of << indent;
                        continue;
                    }
                }
            }

            bool omitAssign = false;

            // If the return type is `()`, omit the assignment (all `()` returning functions are marked as returning
            // void)
            {
                ::HIR::TypeRef tmp;
                if (mirRes->getLvalueType(tmp, e.retVal) == crate.types.unit()) {
                    omitAssign = true;
                }

                if (this->typeIsBadZst(mirRes->getLvalueType(tmp, e.retVal))) {
                    omitAssign = true;
                }
            }

            TU_MATCH_HDRA( (e.fcn), {)
            TU_ARMA(Value, e2) {
                    {
                        ::HIR::TypeRef tmp;
                        const auto& ty = localMirRes.getLvalueType(tmp, e2);
                        MIR_ASSERT(localMirRes, ty->is_Function(), "Call::Value on non-function - " << ty);

                        const auto& retTy = ty->as_Function().mRettype;
                        omitAssign |= retTy->is_Diverge();
                        if (!omitAssign) {
                            emitLvalue(e.retVal);
                            of << " = ";
                        }
                    }
                    of << "(";
                    emitLvalue(e2);
                    of << ")";
                }
                TU_ARMA(Path, e2) {
                    {
                    TU_MATCH_HDRA( (e2.mData), {)
                    TU_ARMA(Generic, pe) {
                                const auto& fcn = crate.getFunctionByPath(sp, pe.mPath);
                                omitAssign |= fcn.returnType->is_Diverge();
                                // TODO: Monomorph.
                            }
                            TU_ARMA(UfcsUnknown, pe) {
                            }
                            TU_ARMA(UfcsInherent, pe) {
                                // Check if the return type is !
                                omitAssign |= mResolve.crate.findTypeImpls(pe.type, HIR::ResolvePlaceholdersNop(), [&](const auto& impl) {
                                    // Associated functions
                                    {
                                        auto it = impl.methods.find(pe.item);
                                        if (it != impl.methods.end()) {
                                            return it->second.data.returnType->is_Diverge();
                                        }
                                    }
                                    // Associated static (undef)
                                    return false;
                                });
                            }
                            TU_ARMA(UfcsKnown, pe) {
                                // Check if the return type is !
                                const auto& tr = mResolve.crate.getTraitByPath(sp, pe.trait.mPath);
                                const auto& fcn = tr.values.find(pe.item)->second.as_Function();
                                const auto& rvTpl = fcn.returnType;
                                if (rvTpl->is_Diverge() || rvTpl == crate.types.unit()) {
                                    omitAssign |= true;
                                } else if (const auto* te = rvTpl->opt_Generic()) {
                                    (void)te;
                                    // TODO: Generic lookup
                                } else if (const auto* te = rvTpl->opt_Path()) {
                                    if (te->binding.is_Opaque()) {
                                        // TODO: Associated type lookup
                                    }
                                } else {
                                    // Not a ! type
                                }
                            }
                    }
                    if(!omitAssign)
                    {
                            emitLvalue(e.retVal);
                            of << " = ";
                    }
                    }
                    of << TransMangle(e2);
                }
                TU_ARMA(Intrinsic, e2) {
                    const auto& name = e2.name;
                    const auto& params = e2.params;
                    emitIntrinsicCall(name, params, e);
                    if (hasZst) {
                        indent.n--;
                        of << indent << "}\n";
                    }
                    return;
                }
            }
            of << "(";
            for(unsigned int j = 0; j < e.args.size(); j ++) {
                if (j != 0) {
                    of << ",";
                }
                of << " ";
                ::HIR::TypeRef tmp;
                const auto& ty = mirRes->getParamType(tmp, e.args[j]);

                if (this->typeIsBadZst(ty)) {
                    of << "zarg" << j;
                    continue;
                }
                emitParam(e.args[j]);
            }
            of << " );\n";

            if( hasZst )
            {
                indent.n--;
                of << indent << "}\n";
            }
        }

        bool asmMatchesTemplate(const ::MIR::Statement::Data_Asm& e, const char* tpl, ::std::initializer_list<const char*> inputs, ::std::initializer_list<const char*> outputs) {
            struct H {
                static bool checkList(const std::vector<std::pair<std::string, MIR::LValue>>& have, const ::std::initializer_list<const char*>& exp) {
                    if (have.size() != exp.size()) {
                        return false;
                    }
                    auto hIt = have.begin();
                    auto eIt = exp.begin();
                    for (; hIt != have.end(); ++hIt, ++eIt) {
                        if (hIt->first != *eIt) {
                            return false;
                        }
                    }
                    return true;
                }
            };

            if (e.tpl == tpl) {
                if (!H::checkList(e.inputs, inputs) || !H::checkList(e.outputs, outputs)) {
                    MIR_BUG(*mirRes, "Hard-coded asm translation doesn't apply - `" << e.tpl << "` inputs=" << e.inputs << " outputs=" << e.outputs);
                }
                return true;
            }
            return false;
        }

        void emitAsmGcc(const ::MIR::TypeResolve& localMirRes, const ::MIR::Statement::Data_Asm& e, unsigned indentLevel) {
            auto indent = RepeatLitStr{"\t", static_cast<int>(indentLevel)};

            struct H {
                static bool hasFlag(const ::std::vector<::std::string>& flags, const char* des) {
                    return ::std::find_if(flags.begin(), flags.end(), [des](const auto& x) {
                        return x == des;
                    }) != flags.end();
                }

                static const char* convertReg(const char* r) {
                    if (::std::strcmp(r, "{eax}") == 0 || ::std::strcmp(r, "{rax}") == 0) {
                        return "a";
                    } else if (::std::strcmp(r, "{ebx}") == 0 || ::std::strcmp(r, "{rbx}") == 0) {
                        return "b";
                    } else if (::std::strcmp(r, "{ecx}") == 0 || ::std::strcmp(r, "{rcx}") == 0) {
                        return "c";
                    } else if (::std::strcmp(r, "{edx}") == 0 || ::std::strcmp(r, "{rdx}") == 0) {
                        return "d";
                    } else {
                        return r;
                    }
                }
            };

            bool is_volatile = H::hasFlag(e.flags, "volatile");
            bool isIntel = H::hasFlag(e.flags, "intel");

            // The following clobber overlaps with an output
            // __asm__ ("cpuid": "=a" (var0), "=b" (var1), "=c" (var2), "=d" (var3): "a" (arg0), "c" (var4): "rbx");
            if (asmMatchesTemplate(e, "cpuid", {"{eax}", "{ecx}"}, {"={eax}", "={ebx}", "={ecx}", "={edx}"})) {
                if (e.clobbers.size() == 1 && e.clobbers[0] == "rbx") {
                    of << indent << "__asm__(\"cpuid\"";
                    of << " : ";
                    of << "\"=a\" (";
                    emitLvalue(e.outputs[0].second);
                    of << "), ";
                    of << "\"=b\" (";
                    emitLvalue(e.outputs[1].second);
                    of << "), ";
                    of << "\"=c\" (";
                    emitLvalue(e.outputs[2].second);
                    of << "), ";
                    of << "\"=d\" (";
                    emitLvalue(e.outputs[3].second);
                    of << ")";
                    of << " : ";
                    of << "\"a\" (";
                    emitLvalue(e.inputs[0].second);
                    of << "), ";
                    of << "\"c\" (";
                    emitLvalue(e.inputs[1].second);
                    of << ")";
                    of << " );\n";
                    return;
                }
            }
            if (asmMatchesTemplate(e, "pushfd; popl $0", {}, {"=r"})) {
                of << indent << "__asm__ __volatile__ (\"pushfl; popl %0\" : \"=r\" (";
                emitLvalue(e.outputs[0].second);
                of << ") : : );\n";
                return;
            }
            if (asmMatchesTemplate(e, "pushl $0; popfd", {"r"}, {})) {
                of << indent << "__asm__ __volatile__ (\"pushl %0; popfl\" : : \"r\" (";
                emitLvalue(e.inputs[0].second);
                of << ") : );\n";
                return;
            }

            of << indent << "__asm__ ";
            if (is_volatile) {
                of << "__volatile__";
            }
            of << "(\"" << (isIntel ? ".intel_syntax noprefix; " : "");
            // TODO: Use a more powerful parser that can properly handle the differences between rustc/llvm and GCC
            for (auto it = e.tpl.begin(); it != e.tpl.end(); ++it) {
                if (*it == '\n') {
                    of << ";\\n";
                } else if (*it == '"') {
                    of << "\\\"";
                } else if (*it == '\\') {
                    of << "\\\\";
                } else if (*it == '/' && *(it + 1) == '/') {
                    while (it != e.tpl.end() || *it == '\n') {
                        ++it;
                    }
                    --it;
                } else if (*it == '%' && *(it + 1) == '%') {
                    of << "%";
                } else if (*it == '%' && !isdigit(*(it + 1))) {
                    of << "%%";
                } else if (*it == '$' && isdigit(*(it + 1)) && *(it + 2) != 'x') {
                    of << "%";
                }
                // Hack for `${0:b}` seen with `setc`, just emit as `%0`
                else if (*it == '$' && *(it + 1) == '{') {
                    of << "%" << *(it + 2);
                    while (it != e.tpl.end() && *it != '}') {
                        it++;
                    }
                } else {
                    of << *it;
                }
            }
            of << (isIntel ? ".att_syntax; " : "") << "\"";
            of << ": ";
            for (unsigned int i = 0; i < e.outputs.size(); i++) {
                const auto& v = e.outputs[i];
                if (i != 0) {
                    of << ", ";
                }
                of << "\"";
                switch (v.first[0]) {
                    case '=':
                        of << "=";
                        break;
                    case '+':
                        of << "+";
                        break;
                    default:
                        MIR_TODO(localMirRes, "Handle asm! output leader '" << v.first[0] << "'");
                }
                of << H::convertReg(v.first.c_str() + 1);
                of << "\" (";
                emitLvalue(v.second);
                of << ")";
            }
            of << ": ";
            for (unsigned int i = 0; i < e.inputs.size(); i++) {
                const auto& v = e.inputs[i];
                if (i != 0) {
                    of << ", ";
                }
                // TODO: If this is the same reg as an output, use the output index
                of << "\"" << H::convertReg(v.first.c_str()) << "\" (";
                emitLvalue(v.second);
                of << ")";
            }
            of << ": ";
            for (unsigned int i = 0; i < e.clobbers.size(); i++) {
                if (i != 0) {
                    of << ", ";
                }
                if (e.tpl == "cpuid\n" && e.clobbers[i] == "rbx") {
                    continue;
                }
                of << "\"" << e.clobbers[i] << "\"";
            }
            of << ");\n";
        }

        struct Asm2TplMatch {
            const MIR::TypeResolve& mirRes;
            const ::MIR::Statement& stmt;
            const ::MIR::Statement::Data_Asm2& e;
            std::vector<std::string> fmtLines;
            std::vector<std::string> fmtParams;

            Asm2TplMatch(const MIR::TypeResolve& localMirRes, const ::MIR::Statement& stmt)
                : mirRes(localMirRes)
                , stmt(stmt)
                , e(stmt.as_Asm2())
            {
                for (const auto& v : e.lines) {
                    fmtLines.push_back(FMT(FMT_CB(os, v.fmt(os))));
                    fmtLines.back().erase(fmtLines.back().begin());
                    fmtLines.back().pop_back();
                    DEBUG(fmtLines.back());
                }

                for (const auto& p : e.params) {
                    fmtParams.push_back(getParamText(p));
                }
            }

            bool matchesTemplate(::std::initializer_list<const char*> lines, ::std::initializer_list<const char*> params) const {
                if (!checkList(fmtLines, lines)) {
                    return false;
                }

                if (!checkList(fmtParams, params)) {
                    MIR_BUG(
                        mirRes,
                        "Hard-coded asm translation doesn't apply - " << stmt << "\n"
                                                                      << "[" << fmtParams << "] != \n[" << FMT_CB(os, for (auto it = params.begin(); it != params.end(); ++it) os << *it << ", ") << "]"
                    );
                }

                return true;
            }

            const MIR::AsmParam& p(size_t i) const {
                return e.params.at(i);
            }

            const MIR::Param& input(size_t i) const {
                MIR_ASSERT(mirRes, e.params.at(i).as_Reg().input, "Parameter " << i << " isn't a register input");
                return *e.params.at(i).as_Reg().input;
            }

            const MIR::LValue& output(size_t i) const {
                MIR_ASSERT(mirRes, e.params.at(i).as_Reg().output, "Parameter " << i << " isn't a register output");
                return *e.params.at(i).as_Reg().output;
            }

        private:
            /// Get a description of the parameter's important attributes
            static std::string getParamText(const MIR::AsmParam& p) {
                TU_MATCH_HDRA( (p), {)
                TU_ARMA(Reg, e) {
                    TU_MATCH_HDRA( (e.spec), { )
                    TU_ARMA(Explicit, n) {
                                return FMT(getDirText(e.dir) << "=" << n);
                            }
                            TU_ARMA(Class, c) {
                                return FMT(getDirText(e.dir) << ":" << AsmCommon::to_string(c));
                            }
                    }
                    }
                    TU_ARMA(Const, e)
                    return "const";
                    TU_ARMA(Sym, e)
                    return "sym";
                }
                throw "";
            }

            static const char* getDirText(const AsmCommon::Direction& d) {
                switch (d) {
                    case AsmCommon::Direction::In:
                        return "in";
                    case AsmCommon::Direction::Out:
                        return "out";
                    case AsmCommon::Direction::InOut:
                        return "inout";
                    case AsmCommon::Direction::LateOut:
                        return "lateout";
                    case AsmCommon::Direction::InLateOut:
                        return "inlateout";
                }
                throw "";
            }

            static bool checkList(const std::vector<std::string>& have, const ::std::initializer_list<const char*>& exp) {
                if (have.size() != exp.size()) {
                    return false;
                }
                auto hIt = have.begin();
                auto eIt = exp.begin();
                for (; hIt != have.end(); ++hIt, ++eIt) {
                    if (*hIt != *eIt) {
                        return false;
                    }
                }
                return true;
            }
        };

        void emitAsm2Gcc(const ::MIR::TypeResolve& localMirRes, const ::MIR::Statement& stmt, unsigned indentLevel) {
            auto indent = RepeatLitStr{"\t", static_cast<int>(indentLevel)};
            Asm2TplMatch m{localMirRes, stmt};
            const auto& se = stmt.as_Asm2();

            // The following clobber overlaps with an output
            // __asm__ ("cpuid": "=a" (var0), "=b" (var1), "=c" (var2), "=d" (var3): "a" (arg0), "c" (var4): "rbx");
            if (m.matchesTemplate({"movq %rbx, {0:r}", "cpuid", "xchgq %rbx, {0:r}"}, {"lateout:reg", "inlateout=eax", "inlateout=ecx", "lateout=edx"})) {
                //if( e.clobbers.size() == 1 && e.clobbers[0] == "rbx" ) {
                of << indent << "__asm__(\"cpuid\"";
                of << " : ";
                of << "\"=a\" (";
                emitLvalue(m.output(1));
                of << "), ";
                of << "\"=b\" (";
                emitLvalue(m.output(0));
                of << "), ";
                of << "\"=c\" (";
                emitLvalue(m.output(2));
                of << "), ";
                of << "\"=d\" (";
                emitLvalue(m.output(3));
                of << ")";
                of << " : ";
                of << "\"a\" (";
                emitParam(m.input(1));
                of << "), ";
                of << "\"c\" (";
                emitParam(m.input(2));
                of << ")";
                of << " );\n";
                return;
                //}
            } else if (m.matchesTemplate({"mov {0:r}, rbx", "cpuid", "xchg {0:r}, rbx"}, {"out:reg", "inout=eax", "inout=ecx", "out=edx"})) // 1.74 libstd_detect
            {
                of << indent << "__asm__(\"cpuid\"";
                of << " : ";
                of << "\"=a\" (";
                emitLvalue(m.output(1));
                of << "), ";
                of << "\"=b\" (";
                emitLvalue(m.output(0));
                of << "), ";
                of << "\"=c\" (";
                emitLvalue(m.output(2));
                of << "), ";
                of << "\"=d\" (";
                emitLvalue(m.output(3));
                of << ")";
                of << " : ";
                of << "\"a\" (";
                emitParam(m.input(1));
                of << "), ";
                of << "\"c\" (";
                emitParam(m.input(2));
                of << ")";
                of << " );\n";
                return;
            } else if (m.matchesTemplate({"btl {1:e}, ({0})", "setc {2}"}, {"in:reg", "in:reg", "out:reg_byte"})) {
                of << indent << "__asm__(\"bt %1, (%2); setc %0\"";
                of << " : \"=r\"(";
                emitLvalue(m.output(2));
                of << ")";
                of << " : \"r\"(";
                emitParam(m.input(0));
                of << "), \"r\"(";
                emitParam(m.input(1));
                of << ")";
                of << ");\n";
                return;
            } else if (m.matchesTemplate({"btcl {1:e}, ({0})", "setc {2}"}, {"in:reg", "in:reg", "out:reg_byte"})) {
                of << indent << "__asm__(\"btc %1, (%2); setc %0\"";
                of << " : \"=r\"(";
                emitLvalue(m.output(2));
                of << ")";
                of << " : \"r\"(";
                emitParam(m.input(0));
                of << "), \"r\"(";
                emitParam(m.input(1));
                of << ")";
                of << ");\n";
                return;
            } else if (m.matchesTemplate({"btrl {1:e}, ({0})", "setc {2}"}, {"in:reg", "in:reg", "out:reg_byte"})) {
                of << indent << "__asm__(\"btr %1, (%2); setc %0\"";
                of << " : \"=r\"(";
                emitLvalue(m.output(2));
                of << ")";
                of << " : \"r\"(";
                emitParam(m.input(0));
                of << "), \"r\"(";
                emitParam(m.input(1));
                of << ")";
                of << ");\n";
                return;
            } else if (m.matchesTemplate({"btsl {1:e}, ({0})", "setc {2}"}, {"in:reg", "in:reg", "out:reg_byte"})) {
                of << indent << "__asm__(\"bts %1, (%2); setc %0\"";
                of << " : \"=r\"(";
                emitLvalue(m.output(2));
                of << ")";
                of << " : \"r\"(";
                emitParam(m.input(0));
                of << "), \"r\"(";
                emitParam(m.input(1));
                of << ")";
                of << ");\n";
                return;
            }
            // HACK: Abort on various `v*` operations, as they have overly complex register specs that gcc doesn't like
            else if (se.lines[0].frags.size() > 0 && (false || se.lines[0].frags[0].before.find("vmov") == 0 || se.lines[0].frags[0].before.find("vexpand") == 0 || se.lines[0].frags[0].before.find("vpexpand") == 0)) {
                of << "abort();\n";
                return;
            } else {
                std::vector<unsigned> argMappings(se.params.size(), UINT_MAX);
                // If there is an explicit register, create a block and add `register uintptr_t asm_REGNAME asm("REGNAME");`
                // - Requires updating the arg mappings, as doing so would remove the argument from the list.
                bool blockOpen = false;
                for (size_t i = 0; i < se.params.size(); i++) {
                    if (const auto* pe = se.params[i].opt_Reg()) {
                        if (!pe->input && !pe->output) {
                        } else if (const auto* regnameP = pe->spec.opt_Explicit()) {
                            argMappings[i] = UINT_MAX - 1;
                            if (!blockOpen) {
                                blockOpen = true;
                                of << indent << "{\n";
                            }
                            of << indent << "register uintptr_t asm_" << *regnameP << " asm(\"" << *regnameP << "\")";
                            if (pe->input) {
                                of << " = (uintptr_t)";
                                emitParam(*pe->input);
                            }
                            of << ";\n";
                        }
                    }
                }
                std::vector<const MIR::AsmParam::Data_Reg*> outputs;
                // Outputs
                for (size_t i = 0; i < se.params.size(); i++) {
                    if (const auto* pe = se.params[i].opt_Reg()) {
                        if (pe->spec.is_Explicit()) {
                            // Ignore, handled explicitly above
                            if (pe->output) {
                                outputs.push_back(pe);
                            }
                        } else if (!pe->output && !pe->input) {
                            if (!blockOpen) {
                                blockOpen = true;
                                of << indent << "{\n";
                            }
                            of << indent << "uintptr_t asm_anon_" << outputs.size() << " = 0;\n";

                            argMappings[i] = outputs.size();
                            outputs.push_back(pe);
                        } else if (pe->output) {
                            argMappings[i] = outputs.size();
                            outputs.push_back(pe);
                        }
                    }
                }
                // Inputs
                std::vector<const MIR::AsmParam*> inputs;
                for (size_t i = 0; i < se.params.size(); i++) {
                    if (const auto* pe = se.params[i].opt_Reg()) {
                        if (pe->spec.opt_Explicit()) {
                            // Ignore, handled explicitly above
                            // An in+out explicit register is fully covered by its
                            // "+" output constraint; emitting a matching input for
                            // it too is rejected by clang.
                            if (pe->input && !pe->output) {
                                inputs.push_back(&se.params[i]);
                            }
                        } else if (pe->input && !pe->output) {
                            argMappings[i] = outputs.size() + inputs.size();
                            inputs.push_back(&se.params[i]);
                        }
                    }
                }
                // Clobbers
                std::vector<const char*> clobbers;
                for (size_t i = 0; i < se.params.size(); i++) {
                    // An explicit register, not "In" and output parameter
                    if (const auto* pe = se.params[i].opt_Reg()) {
                        if (!pe->input && !pe->output && pe->spec.is_Explicit()) {
                            const auto& regname = pe->spec.as_Explicit();
                            clobbers.push_back(regname.c_str());
                        }
                    }
                }

                of << indent << "__asm__ ";
                of << "__volatile__"; // Default everything to volatile
                of << "(\"";
                if ((TargetGetCurSpec().arch.mName == "x86" || TargetGetCurSpec().arch.mName == "x86_64") && !se.options.attSyntax) {
                    of << ".intel_syntax noprefix; ";
                }
                bool escapePercent = true || !inputs.empty() || !outputs.empty();
                for (const auto& l : se.lines) {
                    for (const auto& f : l.frags) {
                        of << FmtGccAsm(f.before, escapePercent);
                        MIR_ASSERT(localMirRes, argMappings.at(f.index) != UINT_MAX, stmt);
                        of << "%";
                        if (argMappings.at(f.index) == UINT8_MAX - 1) {
                            of << se.params[f.index].as_Reg().spec.as_Explicit();
                            continue;
                        }
                        switch (f.modifier) {
                            case '\0':
                                break;
                            case 'r':
                                of << 'q'; // x86: `q` selects rax explicitly
                                break;
                            case 'e':
                                of << 'k'; // x86: `k` selects eax instead of rax
                                break;
                            default:
                                MIR_TODO(localMirRes, "Asm2 GCC: modifier " << f.modifier << " - " << stmt);
                        }
                        of << argMappings.at(f.index);
                    }
                    of << FmtGccAsm(l.trailing, escapePercent);
                    of << ";\\n ";
                }
                if ((TargetGetCurSpec().arch.mName == "x86" || TargetGetCurSpec().arch.mName == "x86_64") && !se.options.attSyntax) {
                    of << ".att_syntax; ";
                }
                of << "\" :";
                for (size_t i = 0; i < outputs.size(); i++) {
                    const auto& p = *outputs[i];
                    if (i != 0) {
                        of << ",";
                    }
                    of << " ";
                    of << "\"";
                    if (!p.output && !p.input) {
                        of << "+";
                    } else {
                        of << (p.input ? "+" : "=");
                    }
                    TU_MATCH_HDRA((p.spec), {)
                    TU_ARMA(Class, c)
                        // https://gcc.gnu.org/onlinedocs/gcc/Machine-Constraints.html
                        switch(c)
                        {
                            // x86
                            case AsmCommon::RegisterClass::x86Reg:
                                of << "r";
                                break;
                            case AsmCommon::RegisterClass::x86RegAbcd:
                                of << "Q";
                                break;
                            case AsmCommon::RegisterClass::x86RegByte:
                                of << "q";
                                break;
                            case AsmCommon::RegisterClass::x86Xmm:
                                of << "x";
                                break;
                            case AsmCommon::RegisterClass::x86Ymm:
                                of << "x";
                                break;
                            case AsmCommon::RegisterClass::x86Zmm:
                                of << "v";
                                break;
                            case AsmCommon::RegisterClass::x86Kreg:
                                of << "Yk";
                                break;
                            // riscv
                            case AsmCommon::RegisterClass::riscvReg:
                                of << "r";
                                break;
                            case AsmCommon::RegisterClass::riscvFreg:
                                of << "f";
                                break;
                        }
                        TU_ARMA(Explicit, name) {
                            of << "r";
                        }
                    }
                    of << "\" (";
                    if( !p.output ) {
                        of << "asm_anon_" << i;
                    }
                    else if( const auto* regnameP = p.spec.opt_Explicit() ) {
                        of << "asm_" << *regnameP;
                    }
                    else {
                        emitLvalue(*p.output);
                    }
                    of << ")";
                }
                of << " :";
                for (size_t i = 0; i < inputs.size(); i++) {
                    const auto& p = *inputs[i];
                    if (i != 0) {
                        of << ",";
                    }
                    of << " ";
                    TU_MATCH_HDRA((p), {)
                    TU_ARMA(Reg, r) {
                            of << "\"";
                        TU_MATCH_HDRA((r.spec), {)
                        TU_ARMA(Class, c)
                            switch(c)
                            {
                                    // x86
                                    case AsmCommon::RegisterClass::x86Reg:
                                        of << "r";
                                        break;
                                    case AsmCommon::RegisterClass::x86RegAbcd:
                                        of << "Q";
                                        break;
                                    case AsmCommon::RegisterClass::x86RegByte:
                                        of << "q";
                                        break;
                                    case AsmCommon::RegisterClass::x86Xmm:
                                        of << "x";
                                        break;
                                    case AsmCommon::RegisterClass::x86Ymm:
                                        of << "x";
                                        break;
                                    case AsmCommon::RegisterClass::x86Zmm:
                                        of << "v";
                                        break;
                                    case AsmCommon::RegisterClass::x86Kreg:
                                        of << "Yk";
                                        break;
                                    // riscv
                                    case AsmCommon::RegisterClass::riscvReg:
                                        of << "r";
                                        break;
                                    case AsmCommon::RegisterClass::riscvFreg:
                                        of << "f";
                                        break;
                                }
                                TU_ARMA(Explicit, name) {
                                    auto it = ::std::find(outputs.begin(), outputs.end(), &r);
                                    if (it != outputs.end()) {
                                        of << (it - outputs.begin());
                                    } else {
                                        of << "r";
                                    }
                                }
                        }
                        assert(r.input);
                        of << "\" (";
                        if( const auto* regnameP = p.as_Reg().spec.opt_Explicit() ) {
                                of << "asm_" << *regnameP;
                        }
                        else {
                                emitParam(*r.input);
                        }
                        of << ")";
                        }
                        TU_ARMA(Const, c) MIR_TODO(localMirRes, "Asm2 GCC - Const: " << stmt);
                        TU_ARMA(Sym, c) MIR_TODO(localMirRes, "Asm2 GCC - Sym: " << stmt);
                    }
                }
                of << ":";
                for (size_t i = 0; i < clobbers.size(); i++) {
                    if (i > 0) {
                        of << ",";
                    }
                    of << " \"" << clobbers[i] << "\"";
                }
                of << ");\n";
                for (size_t i = 0; i < se.params.size(); i++) {
                    if (const auto* pe = se.params[i].opt_Reg()) {
                        if (const auto* regnameP = pe->spec.opt_Explicit()) {
                            if (pe->output) {
                                of << indent;
                                emitLvalue(*pe->output);
                                of << " = ";
                                HIR::TypeRef tmp;
                                of << "(";
                                emitCtype(mirRes->getLvalueType(tmp, *pe->output));
                                of << ")";
                                of << "asm_" << *regnameP << ";\n";
                            }
                        }
                    }
                }
                if (blockOpen) {
                    of << indent << "}\n";
                }
            }
        }

    private:
        const ::HIR::TypeData* monomorphiseFcnReturn(::HIR::TypeRef& tmp, const ::HIR::Function& item, const TransParams& params) {
            bool hasErased = visitTyWith(item.returnType, [&](const auto& x) {
                return x->is_ErasedType();
            });

            if (hasErased || monomorphiseTypeNeeded(item.returnType)) {
                // If there's an erased type, make a copy with the erased type expanded
                if (hasErased) {
                    tmp = cloneTyWith(crate.types, sp, item.returnType, [&](const auto& x, auto& out) {
                        if (const auto* te = x->opt_ErasedType()) {
                            if (const auto* e = te->inner.opt_Fcn()) {
                                out = item.mCode.erasedTypes.at(e->index);
                                return true;
                            }
                        }
                        return false;
                    });
                    tmp = params.monomorphType(Span(), tmp);
                } else {
                    tmp = params.monomorphType(Span(), item.returnType);
                }
                mResolve.expandAssociatedTypes(Span(), tmp);
                return tmp;
            } else {
                return item.returnType;
            }
        }

        void emitFunctionHeader(const ::HIR::Path& p, const ::HIR::Function& item, const TransParams& params) {
            ::HIR::TypeRef tmp;
            const auto& retTy = monomorphiseFcnReturn(tmp, item, params);
            if (item.markings.isNaked) {
                of << "__attribute__((naked)) ";

            }
            auto cb = FMT_CB(
                ss,
                // TODO: Cleaner ABI handling
                ss << " " << TransMangle(p) << "(";
                if (item.mArgs.size() == 0) { ss << "void)"; } else {
                    for (unsigned int i = 0; i < item.mArgs.size(); i++) {
                        ss << "\n\t\t";
                        auto ty = params.monomorph(mResolve, item.mArgs[i].second);
                        this->emitCtype(ty, FMT_CB(os, os << "arg" << i;));
                        if (item.variadic || i + 1 < item.mArgs.size()) {
                            of << ",";
                        }
                        of << " // " << ty;
                    }

                    if (item.variadic) {
                        of << "\n\t\t...";
                    }

                    ss << "\n\t\t)";
                }
            );
            if (retTy != crate.types.unit()) {
                emitCtype(retTy, cb);
            } else {
                of << "void " << cb;
            }
            of << " // -> " << retTy << "\n";
        }

        void emitIntrinsicCall(const RcString& name, const ::HIR::PathParams& params, const ::MIR::Terminator::Data_Call& e) {
            const auto& localMirRes = *mirRes;
            enum class Ordering {
                SeqCst,
                Acquire,
                Release,
                Relaxed,
                AcqRel,
            };
            auto getAtomicTyGcc = [&](Ordering o) -> const char* {
                switch (o) {
                    case Ordering::SeqCst:
                        return "__ATOMIC_SEQ_CST";
                    case Ordering::Acquire:
                        return "__ATOMIC_ACQUIRE";
                    case Ordering::Release:
                        return "__ATOMIC_RELEASE";
                    case Ordering::Relaxed:
                        return "__ATOMIC_RELAXED";
                    case Ordering::AcqRel:
                        return "__ATOMIC_ACQ_REL";
                }
                throw "";
            };
            auto getAtomicOrdering = [&](const RcString& name, size_t prefixLen) -> Ordering {
                if (name.size() < prefixLen) {
                    return Ordering::SeqCst;
                }
                const char* suffix = name.c_str() + prefixLen;
                if (::std::strcmp(suffix, "acq") == 0 || ::std::strcmp(suffix, "acquire") == 0 || ::std::strcmp(suffix, "relaxed_acquire") == 0 || ::std::strcmp(suffix, "acquire_acquire") == 0 || ::std::strcmp(suffix, "acquire_relaxed") == 0) {
                    return Ordering::Acquire;
                } else if (::std::strcmp(suffix, "rel") == 0 || ::std::strcmp(suffix, "release") == 0 || ::std::strcmp(suffix, "release_relaxed") == 0) {
                    return Ordering::Release;
                } else if (::std::strcmp(suffix, "relaxed") == 0 || ::std::strcmp(suffix, "relaxed_relaxed") == 0) {
                    return Ordering::Relaxed;
                } else if (::std::strcmp(suffix, "acqrel") == 0 || ::std::strcmp(suffix, "acqrel_relaxed") == 0) {
                    return Ordering::AcqRel;
                }
                // TODO: Is this correct?
                else if (::std::strcmp(suffix, "unordered") == 0) {
                    return Ordering::Relaxed;
                } else if (::std::strcmp(suffix, "seqcst") == 0 || ::std::strcmp(suffix, "relaxed_seqcst") == 0 || ::std::strcmp(suffix, "release_seqcst") == 0 || ::std::strcmp(suffix, "acquire_seqcst") == 0 || ::std::strcmp(suffix, "acqrel_seqcst") == 0 || ::std::strcmp(suffix, "seqcst_seqcst") == 0 || ::std::strcmp(suffix, "release_acquire") == 0 || ::std::strcmp(suffix, "acqrel_acquire") == 0 || ::std::strcmp(suffix, "seqcst_acquire") == 0 || ::std::strcmp(suffix, "seqcst_relaxed") == 0) {
                    return Ordering::SeqCst;
                } else {
                    MIR_BUG(localMirRes, "Unknown atomic ordering suffix - '" << suffix << "'");
                }
                throw "";
            };
            auto getPrimSize = [&localMirRes](const ::HIR::TypeData* ty) -> unsigned {
                if (ty->is_Pointer()) {
                    return TargetGetCurSpec().arch.pointerBits;
                }
                if (!ty->is_Primitive()) {
                    MIR_BUG(localMirRes, "Unknown type for getting primitive size - " << ty);
                }
                switch (ty->as_Primitive()) {
                    case ::HIR::CoreType::U8:
                    case ::HIR::CoreType::I8:
                        return 8;
                    case ::HIR::CoreType::U16:
                    case ::HIR::CoreType::I16:
                        return 16;
                    case ::HIR::CoreType::U32:
                    case ::HIR::CoreType::I32:
                        return 32;
                    case ::HIR::CoreType::U64:
                    case ::HIR::CoreType::I64:
                        return 64;
                    case ::HIR::CoreType::U128:
                    case ::HIR::CoreType::I128:
                        return 128;
                    case ::HIR::CoreType::Usize:
                    case ::HIR::CoreType::Isize:
                        // TODO: Is this a good idea?
                        return TargetGetCurSpec().arch.pointerBits;
                    default:
                        MIR_BUG(localMirRes, "Unknown primitive for getting size- " << ty);
                }
            };
            auto getRealPrimTy = [](HIR::CoreType ct) -> HIR::CoreType {
                switch (ct) {
                    case HIR::CoreType::Usize:
                        if (TargetGetCurSpec().arch.pointerBits == 64) {
                            return ::HIR::CoreType::U64;
                        }
                        if (TargetGetCurSpec().arch.pointerBits == 32) {
                            return ::HIR::CoreType::U32;
                        }
                        BUG(Span(), "");
                    case HIR::CoreType::Isize:
                        if (TargetGetCurSpec().arch.pointerBits == 64) {
                            return ::HIR::CoreType::I64;
                        }
                        if (TargetGetCurSpec().arch.pointerBits == 32) {
                            return ::HIR::CoreType::I32;
                        }
                        BUG(Span(), "");
                    default:
                        return ct;
                }
            };
            auto emitAtomicCast = [&]() {
                of << "(";
                emitCtype(params.types.at(0));
                of << "*)";
            };
            // Rust's pointer atomic RMW intrinsics carry their delta in the
            // pointer value itself.  Represent them as integer atomics in C:
            // C pointer fetch_add takes an element count and would both reject
            // the operand type and scale a byte offset.
            const bool atomicTypeIsPointer = params.types.size() > 0
                && params.types.at(0)->is_Pointer();
            auto emitAtomicRmwCast = [&]() {
                if (atomicTypeIsPointer) {
                    of << "(";
                    emitCtype(params.types.at(0));
                    of << ")";
                }
            };
            auto emitAtomicRmwOperand = [&](const ::MIR::Param& param) {
                if (atomicTypeIsPointer) {
                    of << "(uintptr_t)";
                }
                emitParam(param);
            };
            auto emitAtomicCxchg = [&](const auto& e, Ordering oSucc, Ordering oFail, bool isWeak) {
                switch (oFail) {
                    case Ordering::Release:
                        oFail = Ordering::Relaxed;
                        break;
                    case Ordering::AcqRel:
                        oFail = Ordering::Acquire;
                        break;
                    default:
                        break;
                }
                if (typeIsEmulatedI128(params.types.at(0))) {
                    emitCtype(params.types.at(0), FMT_CB(ss, ss << " mrustc_atomic_desired";));
                    of << " = ";
                    emitParam(e.args.at(2));
                    of << ";\n\t";
                }
                emitLvalue(e.retVal);
                of << "._0 = ";
                emitParam(e.args.at(1));
                of << ";\n\t";
                emitLvalue(e.retVal);
                of << "._1 = " << (typeIsEmulatedI128(params.types.at(0)) ? "__atomic_compare_exchange(" : "__atomic_compare_exchange_n(");
                emitAtomicCast();
                emitParam(e.args.at(0));
                of << ", &";
                emitLvalue(e.retVal);
                of << "._0"; // Expected (i.e. the check value)
                of << ", ";
                if (typeIsEmulatedI128(params.types.at(0))) {
                    of << "&mrustc_atomic_desired";
                } else {
                    emitParam(e.args.at(2)); // `desired` (the new value for the slot if equal)
                }
                of << ", " << (isWeak ? "true" : "false");
                of << ", " << getAtomicTyGcc(oSucc) << ", " << getAtomicTyGcc(oFail) << ")";

            };
            auto emitAtomicArith = [&](AtomicOp op, Ordering ordering) {
                emitLvalue(e.retVal);
                of << " = ";
                emitAtomicRmwCast();
                switch (op) {
                    case AtomicOp::Add:
                        of << "__atomic_fetch_add";
                        break;
                    case AtomicOp::Sub:
                        of << "__atomic_fetch_sub";
                        break;
                    case AtomicOp::And:
                        of << "__atomic_fetch_and";
                        break;
                    case AtomicOp::Or:
                        of << "__atomic_fetch_or";
                        break;
                    case AtomicOp::Xor:
                        of << "__atomic_fetch_xor";
                        break;
                }
                of << "(";
                if (atomicTypeIsPointer) {
                    of << "(uintptr_t *)";
                } else {
                    emitAtomicCast();
                }
                emitParam(e.args.at(0));
                of << ", ";
                emitAtomicRmwOperand(e.args.at(1));
                of << ", " << getAtomicTyGcc(ordering) << ")";

            };
            if (name == "size_of") {
                size_t size = 0;
                MIR_ASSERT(localMirRes, TargetGetSizeOf(sp, mResolve, params.types.at(0), size), "Can't get size of " << params.types.at(0));
                emitLvalue(e.retVal);
                of << " = " << size;
            } else if (name == "offset_of") {
                size_t val = localMirRes.intrinsicOffsetOf(params.types.at(0), e.args);
                emitLvalue(e.retVal);
                of << " = " << val;
            } else if (name == "min_align_of" || name == "align_of") {
                size_t align = 0;
                MIR_ASSERT(localMirRes, TargetGetAlignOf(sp, mResolve, params.types.at(0), align), "Can't get alignment of " << params.types.at(0));
                emitLvalue(e.retVal);
                of << " = " << align;
            } else if (name == "size_of_val") {
                emitLvalue(e.retVal);
                of << " = ";
                const auto& ty = params.types.at(0);
                // Get the unsized type and use that in place of MetadataType
                auto innerTy = getInnerUnsizedType(ty);
                if (innerTy == ::HIR::TypeRef()) {
                    size_t size = 0;
                    MIR_ASSERT(localMirRes, TargetGetSizeOf(sp, mResolve, ty, size), "Can't get size of " << ty);
                    of << size;
                }
                // slice metadata (`[T]` and `str`)
                else if (innerTy->is_Slice() || innerTy == ::HIR::CoreType::Str) {
                    bool alignNeeded = false;
                    size_t itemSize = 0;
                    size_t itemAlign = 0;
                    if (const auto* te = innerTy->opt_Slice()) {
                        MIR_ASSERT(localMirRes, TargetGetSizeAndAlignOf(sp, mResolve, te->inner, itemSize, itemAlign), "Can't get size of " << te->inner);
                    } else {
                        assert(innerTy == ::HIR::CoreType::Str);
                        itemSize = 1;
                        itemAlign = 1;
                    }
                    if (!ty->is_Slice() && !ty->is_Primitive()) {
                        // TODO: What if the wrapper has no other fields?
                        // Get the alignment and check if it's higher than the item alignment
                        size_t wrapperAlign = 0, wrapperSizeIgnore = 0;
                        MIR_ASSERT(localMirRes, TargetGetSizeAndAlignOf(sp, mResolve, ty, wrapperSizeIgnore, wrapperAlign), "Can't get align of " << ty);
                        if (wrapperAlign > itemAlign) {
                            itemAlign = wrapperAlign;
                            alignNeeded = true;
                            of << "ALIGN_TO(";
                        }
                        const auto* repr = TargetGetTypeRepr(sp, mResolve, ty);
                        of << repr->fields.back().offset << " + ";
                    }
                    emitParam(e.args.at(0));
                    of << ".META * " << itemSize;
                    if (alignNeeded) {
                        of << ", " << itemAlign << ")";
                    }
                }
                // Trait object metadata.
                else if (innerTy->is_TraitObject()) {
                    emitTraitObjectDstSize(ty, e.args.at(0));
                } else {
                    MIR_BUG(localMirRes, "Unknown inner unsized type " << innerTy << " for " << ty);
                }
                // TODO: Align up
            } else if (name == "min_align_of_val" || name == "align_of_val") {
                emitLvalue(e.retVal);
                of << " = ";
                const auto& ty = params.types.at(0);
#if 1
                auto innerTy = getInnerUnsizedType(ty);
                if (innerTy == ::HIR::TypeRef()) {
                    of << "ALIGNOF(";
                    emitCtype(ty);
                    of << ")";
                } else if (const auto* te = innerTy->opt_Slice()) {
                    of << "ALIGNOF(";
                    if (ty->is_Slice()) {
                        emitCtype(te->inner);
                    } else {
                        emitCtype(ty);
                    }
                    of << ")";
                } else if (innerTy == ::HIR::CoreType::Str) {
                    if (!ty->is_Primitive()) {
                        of << "ALIGNOF(";
                        emitCtype(ty);
                        of << ")";
                    } else {
                        of << "1";
                    }
                } else if (innerTy->is_TraitObject()) {
                    emitTraitObjectDstAlign(ty, e.args.at(0));
                } else {
                    MIR_BUG(localMirRes, "Unknown inner unsized type " << innerTy << " for " << ty);
                }
#else
                switch (metadataType(ty)) {
                    case MetadataType::None:
                        of << "ALIGNOF(";
                        emitCtype(ty);
                        of << ")";
                        break;
                    case MetadataType::Slice: {
                        // TODO: Have a function that fetches the inner type for types like `Path` or `str`
                        const auto& ity = *ty->as_Slice().inner;
                        of << "ALIGNOF(";
                        emitCtype(ity);
                        of << ")";
                        break;
                    }
                    case MetadataType::TraitObject:
                        of << "((VTABLE_HDR*)";
                        emitParam(e.args.at(0));
                        of << ".META)->align";
                        break;
                }
#endif
            }
            // --- Type assertions ---
            else if (name == "panic_if_uninhabited" || name == "assert_inhabited") {
                // TODO: Detect uninhabited (empty enum or `!` - potentially via nested types)
            } else if (name == "assert_zero_valid") {
                // TODO: Detect nonzero within
            } else if (name == "assert_uninit_valid") {
                // TODO: Detect nonzero or enum within
            } else if (name == "const_eval_select") {
                const auto& argTyTuple = params.types.at(0)->as_Tuple();
                const auto& arg = e.args.at(0).as_LValue();
                // Note: arg 1 is the constant function
                const auto& fcnPath = *e.args.at(2).as_Constant().as_Function().p;

                // Reuse ordinary call emission for the runtime branch of const_eval_select.
                ::std::vector<MIR::Param> args;
                args.reserve(argTyTuple.size());
                for (size_t i = 0; i < argTyTuple.size(); i++) {
                    args.push_back(MIR::LValue::newField(arg.clone(), i));
                }
                auto pseudoTerm = MIR::Terminator::Data_Call{e.retBlock, MIR::UnwindAction::make_Continue({}), e.retVal.clone(), MIR::CallTarget::make_Path(fcnPath.clone()), std::move(args)};
                emitTermCall(localMirRes, pseudoTerm, 1);
            }
            // --- Type identity ---
            else if (name == "type_id") {
                const auto& ty = params.types.at(0);
                // NOTE: Would define the typeid here, but it has to be public
                emitLvalue(e.retVal);
                of << " = ";
                if (options.emulatedI128) {
                    of << "make128(";
                }
                of << "(uintptr_t)&__typeid_" << TransMangle(ty);
                if (options.emulatedI128) {
                    of << ")";
                }
            } else if (name == "type_name") {
                auto name = localMirRes.intrinsicTypeName(params.types.at(0));
                emitLvalue(e.retVal);
                of << ".PTR = \"" << FmtEscaped(name) << "\";\n\t";
                emitLvalue(e.retVal);
                of << ".META = " << name.size() << "";
            } else if (name == "transmute" || name == "transmute_unchecked") {
                const auto& tySrc = params.types.at(0);
                const auto& tyDst = params.types.at(1);
                auto isPtr = [](const ::HIR::TypeData* ty) {
                    return ty->is_Borrow() || ty->is_Pointer();
                };
                if (this->typeIsBadZst(tyDst)) {
                    of << "/* zst */";
                } else if (e.args.at(0).is_Constant()) {
                    of << "{ ";
                    emitCtype(tySrc, FMT_CB(s, s << "v";));
                    of << " = ";
                    emitParam(e.args.at(0));
                    of << "; ";
                    of << "memcpy( &";
                    emitLvalue(e.retVal);
                    of << ", &v, sizeof(";
                    emitCtype(tyDst);
                    of << ")); ";
                    of << "}";
                } else if (isPtr(tyDst) && isPtr(tySrc)) {
                    auto srcMeta = metadataType(tySrc->is_Pointer() ? tySrc->as_Pointer().inner : tySrc->as_Borrow().inner);
                    auto dstMeta = metadataType(tyDst->is_Pointer() ? tyDst->as_Pointer().inner : tyDst->as_Borrow().inner);
                    if (srcMeta == MetadataType::None || srcMeta == MetadataType::Zero) {
                        MIR_ASSERT(*mirRes, dstMeta == MetadataType::None || dstMeta == MetadataType::Zero, "Transmuting to fat pointer from thin: " << tySrc << " -> " << tyDst);
                        emitLvalue(e.retVal);
                        of << " = (";
                        emitCtype(tyDst);
                        of << ")";
                        emitParam(e.args.at(0));
                    } else if (dstMeta == MetadataType::None || dstMeta == MetadataType::Zero) {
                        MIR_BUG(*mirRes, "Transmuting from fat pointer to thin: (" << srcMeta << "->" << dstMeta << ") " << tySrc << " -> " << tyDst);
                    } else if (srcMeta != dstMeta) {
                        emitLvalue(e.retVal);
                        of << ".PTR = ";
                        emitParam(e.args.at(0));
                        of << ".PTR; ";
                        emitLvalue(e.retVal);
                        of << ".META = ";
                        switch (dstMeta) {
                            case MetadataType::Unknown:
                                assert(!"Impossible");
                            case MetadataType::None:
                                assert(!"Impossible");
                            case MetadataType::Zero:
                                assert(!"Impossible");
                            case MetadataType::Slice:
                                of << "(size_t)";
                                break;
                            case MetadataType::TraitObject:
                                of << "(const void*)";
                                break;
                        }
                        emitParam(e.args.at(0));
                        of << ".META";
                    } else {
                        emitLvalue(e.retVal);
                        of << " = ";
                        emitParam(e.args.at(0));
                    }
                } else {
                    of << "memcpy( &";
                    emitLvalue(e.retVal);
                    of << ", &";
                    emitParam(e.args.at(0));
                    of << ", sizeof(";
                    emitCtype(tySrc);
                    of << "))";
                }
            } else if (name == "float_to_int_unchecked") {
                const auto& srcTy = params.types.at(0);
                const auto& dstTy = params.types.at(1);
                // Unchecked (can return `undef`) cast from a float to an integer
                if (this->typeIsEmulatedI128(dstTy)) {
                    of << "abort()";
                    //emit_lvalue(e.ret_val); m_of << " = ("; emit_ctype(dst_ty); m_of << ")"; emit_param(e.args.at(0));
                } else if (srcTy == HIR::CoreType::F16 || srcTy == HIR::CoreType::F128) {
                    of << "abort()";
                } else {
                    emitLvalue(e.retVal);
                    of << " = (";
                    emitCtype(dstTy);
                    of << ")";
                    emitParam(e.args.at(0));
                }
            } else if (name == "copy_nonoverlapping" || name == "copy") {
                if (this->typeIsBadZst(params.types.at(0))) {
                    of << "/* zst */";
                    return;
                }
                if (name == "copy") {
                    of << "memmove";
                } else {
                    of << "memcpy";
                }
                // 0: Source, 1: Destination, 2: Count
                of << "( ";
                emitParam(e.args.at(1));
                of << ", ";
                emitParam(e.args.at(0));
                of << ", ";
                emitParam(e.args.at(2));
                of << " * sizeof(";
                emitCtype(params.types.at(0));
                of << ")";
                of << ")";
            }
            // NOTE: This is generic, and fills count*sizeof(T) (unlike memset)
            else if (name == "write_bytes") {
                if (this->typeIsBadZst(params.types.at(0))) {
                    of << "/* zst */";
                    return;
                }
                // 0: Destination, 1: Value, 2: Count
                of << "if( ";
                emitParam(e.args.at(2));
                of << " > 0) memset( ";
                emitParam(e.args.at(0));
                of << ", ";
                emitParam(e.args.at(1));
                of << ", ";
                emitParam(e.args.at(2));
                of << " * sizeof(";
                emitCtype(params.types.at(0));
                of << ")";
                of << ")";
            } else if (name == "compare_bytes") {
                // A raw memcmp
                emitLvalue(e.retVal);
                of << " = memcmp( ";
                emitParam(e.args.at(0));
                of << ", ";
                emitParam(e.args.at(1));
                of << ", ";
                emitParam(e.args.at(2));
                of << ")";
            } else if (name == "raw_eq") {
                size_t size = 0;
                MIR_ASSERT(localMirRes, TargetGetSizeOf(sp, mResolve, params.types.at(0), size), "Can't get size of " << params.types.at(0));

                // Raw byte equality (could be implemented without a memcmp call, if desired)
                emitLvalue(e.retVal);
                of << " = (0 == memcmp(";
                emitParam(e.args.at(0));
                of << ", ";
                emitParam(e.args.at(1));
                of << ", ";
                of << size;
                of << "))";
            } else if (name == "three_way_compare") {
                const auto& t = params.types.at(0);
                if (typeIsEmulatedI128(t)) {
                    emitLvalue(e.retVal);
                    of << ".TAG = ";
                    of << (t == ::HIR::CoreType::U128 ? "cmp128" : "cmp128s");
                    of << "(";
                    emitParam(e.args.at(0));
                    of << ", ";
                    emitParam(e.args.at(1));
                    of << ");\n";
                } else {
                    emitLvalue(e.retVal);
                    of << ".TAG = (";
                    emitParam(e.args.at(0));
                    of << " == ";
                    emitParam(e.args.at(1));
                    of << " ? 0 : (";
                    emitParam(e.args.at(0));
                    of << " < ";
                    emitParam(e.args.at(1));
                    of << " ? -1 : 1));\n";
                }
                return;
            } else if (name == "forget") {
                // Nothing needs to be done, this just stops the destructor from running.
            } else if (name == "drop_in_place") {
                emitDestructorCall(::MIR::LValue::newDeref(e.args.at(0).as_LValue().clone()), params.types.at(0), true, /*indent_level=*/1 /* TODO: get from caller */);
            }
            // --- Type traits
            else if (name == "needs_drop") {
                // Returns `true` if the actual type given as `T` requires drop glue;
                // returns `false` if the actual type provided for `T` implements `Copy`. (Either otherwise)
                // NOTE: libarena assumes that this returns `true` iff T doesn't require drop glue.
                const auto& ty = params.types.at(0);
                emitLvalue(e.retVal);
                of << " = ";
                if (mResolve.typeNeedsDropGlue(localMirRes.sp, ty)) {
                    of << "true";
                } else {
                    of << "false";
                }
            }
            // --- Initialisation (or lack thereof)
            else if (name == "uninit") {
                // Do nothing, leaves the destination undefined
                // TODO: This makes the C compiler warn
            } else if (name == "init") {
                of << "memset( &";
                emitLvalue(e.retVal);
                of << ", 0, sizeof(";
                emitCtype(params.types.at(0));
                of << "))";
            } else if (name == "move_val_init") {
                if (!this->typeIsBadZst(params.types.at(0))) {
                    of << "*";
                    emitParam(e.args.at(0));
                    of << " = ";
                    emitParam(e.args.at(1));
                }
            } else if (name == "abort") {
                of << "abort()";
            } else if (name == "try" || name == "catch_unwind") {
                of << "{ try { ";
                emitParam(e.args.at(0));
                of << "(";
                emitParam(e.args.at(1));
                of << "); ";
                emitLvalue(e.retVal);
                of << " = 0; } catch (mrustc_panic& panic) { (";
                emitParam(e.args.at(2));
                of << ")(";
                emitParam(e.args.at(1));
                of << ", (uint8_t*)panic.rust_exception); ";
                emitLvalue(e.retVal);
                of << " = 1; } }";
            }
            // --- #[track_caller]
            else if (name == "caller_location") {
                //m_of << "abort()";
                auto p = crate.getLangItemPathOpt("panic_location");
                of << "static struct ";
                if (p == HIR::SimplePath()) {
                    of << "s_ZRG2cE9core0_0_05panic8Location0g";
                } else {
                    of << "s_" << TransMangle(p);
                }
                of << " mrustc_empty_caller_location = {._0={._0={(void*)\"\",0}},._1=0,._2=0};";
                emitLvalue(e.retVal);
                of << " = &mrustc_empty_caller_location"; // TODO: Hidden ABI for caller location
            }
            // --- Pointer manipulation
            else if (name == "offset") { // addition, with the reqirement that the resultant pointer be in bounds
                emitLvalue(e.retVal);
                of << " = ";
                emitParam(e.args.at(0));
                of << " + ";
                emitParam(e.args.at(1));
            } else if (name == "arith_offset") { // addition, with no requirements
                emitLvalue(e.retVal);
                of << " = ";
                emitParam(e.args.at(0));
                of << " + ";
                emitParam(e.args.at(1));
            } else if (name == "ptr_offset_from") { // effectively subtraction
                emitLvalue(e.retVal);
                of << " = ";
                emitParam(e.args.at(0));
                of << " - ";
                emitParam(e.args.at(1));
            } else if (name == "ptr_guaranteed_eq") {
                emitLvalue(e.retVal);
                of << " = (";
                emitParam(e.args.at(0));
                of << " == ";
                emitParam(e.args.at(1));
                of << ")";
            } else if (name == "ptr_guaranteed_ne") {
                emitLvalue(e.retVal);
                of << " = (";
                emitParam(e.args.at(0));
                of << " != ";
                emitParam(e.args.at(1));
                of << ")";
            } else if (name == "ptr_guaranteed_cmp") {
                // 0 if not equal, 1 if equal, 2 if could be either
                emitLvalue(e.retVal);
                of << "= ( (";
                emitParam(e.args.at(0));
                of << ") == (";
                emitParam(e.args.at(1));
                of << "))";
            } else if (name == "ptr_offset_from_unsigned") {
                // `fn ptr_offset_from_unsigned<T>(ptr: *const T, base: *const T) -> usize`
                emitLvalue(e.retVal);
                of << "= ( (";
                emitParam(e.args.at(0));
                of << ") - (";
                emitParam(e.args.at(1));
                of << "))";
            }
            // ----
            else if (name == "bswap") {
                const auto& ty = params.types.at(0);
                MIR_ASSERT(localMirRes, ty->is_Primitive(), "Invalid type passed to bwsap, must be a primitive, got " << ty);
                if (ty == ::HIR::CoreType::U8 || ty == ::HIR::CoreType::I8) {
                    // Nop.
                    emitLvalue(e.retVal);
                    of << " = ";
                    emitParam(e.args.at(0));
                } else {
                    emitLvalue(e.retVal);
                    of << " = ";
                    switch (getPrimSize(ty)) {
                        case 16:
                            of << "__builtin_bswap16";
                            break;
                        case 32:
                            of << "__builtin_bswap32";
                            break;
                        case 64:
                            of << "__builtin_bswap64";
                            break;
                        case 128:
                            of << "__builtin_bswap128";
                            break;
                        default:
                            MIR_TODO(localMirRes, "bswap<" << ty << ">");
                    }

                    of << "(";
                    emitParam(e.args.at(0));
                    of << ")";
                }
            } else if (name == "bitreverse") {
                const auto& ty = params.types.at(0);
                MIR_ASSERT(localMirRes, ty->is_Primitive(), "Invalid type passed to bitreverse. Must be a primitive, got " << ty);
                emitLvalue(e.retVal);
                of << " = ";
                switch (getPrimSize(ty)) {
                    case 8:
                        of << "__mrustc_bitrev8";
                        break;
                    case 16:
                        of << "__mrustc_bitrev16";
                        break;
                    case 32:
                        of << "__mrustc_bitrev32";
                        break;
                    case 64:
                        of << "__mrustc_bitrev64";
                        break;
                    case 128:
                        of << "__mrustc_bitrev128";
                        break;
                    default:
                        MIR_TODO(localMirRes, "bswap<" << ty << ">");
                }
                of << "(";
                emitParam(e.args.at(0));
                of << ")";
            }
            // > Obtain the discriminane of a &T as u64
            else if (name == "discriminant_value") {
                const auto& ty = params.types.at(0);
                emitLvalue(e.retVal);
                of << " = ";
                if (!(ty->is_Path() && ty->as_Path().binding.is_Enum())) {
                    of << "0";
                } else {
                    const auto* repr = TargetGetTypeRepr(sp, mResolve, ty);
                    MIR_ASSERT(localMirRes, repr, "No repr for enum " << ty);
                    switch (repr->variants.tag()) {
                        case TypeRepr::VariantMode::TAGDEAD:
                            throw "";
                            TU_ARM(repr->variants, None, _e)
                            of << "0";
                            break;
                            TU_ARM(repr->variants, Values, ve) {
                                of << "(*";
                                emitParam(e.args.at(0));
                                of << ")";
                                emitEnumPath(repr, ve.field);
                            }
                            break;
                            TU_ARM(repr->variants, Linear, ve) {
                                const auto& tagTy = TargetGetInnerType(sp, mResolve, *repr, ve.field.index, ve.field.subFields);
                                const bool pointerTag = tagTy->is_Pointer() || tagTy->is_Borrow() || tagTy->is_Function();
                                auto emitTag = [&]() {
                                    if (pointerTag) {
                                        of << "(uintptr_t)";
                                    }
                                    of << "(*";
                                    emitParam(e.args.at(0));
                                    of << ")";
                                    emitEnumPath(repr, ve.field);
                                };
                                if (ve.usesNiche()) {
                                    of << "( ";
                                    emitTag();
                                    of << " < " << ve.offset;
                                    of << " ? " << ve.field.index;
                                    of << " : ";
                                    emitTag();
                                    of << " - " << ve.offset;
                                    of << " )";
                                } else {
                                    emitTag();
                                }
                            }
                            break;
                            TU_ARM(repr->variants, NonZero, ve) {
                                of << "(*";
                                emitParam(e.args.at(0));
                                of << ")";
                                emitEnumPath(repr, ve.field);
                                of << " ";
                                of << (ve.zeroVariant ? "==" : "!=");
                                of << " 0";
                            }
                            break;
                    }
                }
            }
            // Hints
            else if (name == "unreachable") {
                of << "__builtin_unreachable()";

            } else if (name == "assume") {
                // I don't assume :)
            } else if (name == "likely" || name == "unlikely") {
                emitLvalue(e.retVal);
                of << "= (";
                emitParam(e.args.at(0));
                of << ")";
            } else if (name == "black_box") {
                if (!lvalueIsBadZst(e.retVal)) {
                    emitLvalue(e.retVal);
                    of << "= (";
                    emitParam(e.args.at(0));
                    of << ")";
                }
            }
            // Overflowing Arithmetic
            // Overflowing arithmetic maps to compiler intrinsics, with software handling for emulated i128.
            else if (name == "add_with_overflow") {
                if (options.emulatedI128 && params.types.at(0) == ::HIR::CoreType::U128) {
                    emitLvalue(e.retVal);
                    of << "._1 = add128_o";
                    of << "(";
                    emitParam(e.args.at(0));
                    of << ", ";
                    emitParam(e.args.at(1));
                    of << ", &";
                    emitLvalue(e.retVal);
                    of << "._0)";
                } else if (options.emulatedI128 && params.types.at(0) == ::HIR::CoreType::I128) {
                    emitLvalue(e.retVal);
                    of << "._1 = add128s_o";
                    of << "(";
                    emitParam(e.args.at(0));
                    of << ", ";
                    emitParam(e.args.at(1));
                    of << ", &";
                    emitLvalue(e.retVal);
                    of << "._0)";
                } else

                {
                    emitLvalue(e.retVal);
                    of << "._1 = __builtin_add_overflow";
                    of << "(";
                    emitParam(e.args.at(0));
                    of << ", ";
                    emitParam(e.args.at(1));
                    of << ", &";
                    emitLvalue(e.retVal);
                    of << "._0)";

                }
            } else if (name == "sub_with_overflow") {
                if (options.emulatedI128 && params.types.at(0) == ::HIR::CoreType::U128) {
                    emitLvalue(e.retVal);
                    of << "._1 = sub128_o";
                    of << "(";
                    emitParam(e.args.at(0));
                    of << ", ";
                    emitParam(e.args.at(1));
                    of << ", &";
                    emitLvalue(e.retVal);
                    of << "._0)";
                } else if (options.emulatedI128 && params.types.at(0) == ::HIR::CoreType::I128) {
                    emitLvalue(e.retVal);
                    of << "._1 = sub128s_o";
                    of << "(";
                    emitParam(e.args.at(0));
                    of << ", ";
                    emitParam(e.args.at(1));
                    of << ", &";
                    emitLvalue(e.retVal);
                    of << "._0)";
                } else {
                    emitLvalue(e.retVal);
                    of << "._1 = __builtin_sub_overflow";
                    of << "(";
                    emitParam(e.args.at(0));
                    of << ", ";
                    emitParam(e.args.at(1));
                    of << ", &";
                    emitLvalue(e.retVal);
                    of << "._0)";

                }
            } else if (name == "mul_with_overflow") {
                if (options.emulatedI128 && params.types.at(0) == ::HIR::CoreType::U128) {
                    emitLvalue(e.retVal);
                    of << "._1 = mul128_o";
                    of << "(";
                    emitParam(e.args.at(0));
                    of << ", ";
                    emitParam(e.args.at(1));
                    of << ", &";
                    emitLvalue(e.retVal);
                    of << "._0)";
                } else if (options.emulatedI128 && params.types.at(0) == ::HIR::CoreType::I128) {
                    emitLvalue(e.retVal);
                    of << "._1 = mul128s_o";
                    of << "(";
                    emitParam(e.args.at(0));
                    of << ", ";
                    emitParam(e.args.at(1));
                    of << ", &";
                    emitLvalue(e.retVal);
                    of << "._0)";
                } else {
                    emitLvalue(e.retVal);
                    of << "._1 = __builtin_mul_overflow(";
                    emitParam(e.args.at(0));
                    of << ", ";
                    emitParam(e.args.at(1));
                    of << ", &";
                    emitLvalue(e.retVal);
                    of << "._0)";

                }
            } else if (name == "overflowing_add" || name == "wrapping_add" // Renamed in 1.39
                       || name == "saturating_add" || name == "unchecked_add") {
                const auto& ty = params.types.at(0);
                if (name == "saturating_add") {
                    of << "if( ";
                }

                if (options.emulatedI128 && ty == ::HIR::CoreType::U128) {
                    of << "add128_o";
                    of << "(";
                    emitParam(e.args.at(0));
                    of << ", ";
                    emitParam(e.args.at(1));
                    of << ", &";
                    emitLvalue(e.retVal);
                    of << ")";
                } else if (options.emulatedI128 && ty == ::HIR::CoreType::I128) {
                    of << "add128s_o";
                    of << "(";
                    emitParam(e.args.at(0));
                    of << ", ";
                    emitParam(e.args.at(1));
                    of << ", &";
                    emitLvalue(e.retVal);
                    of << ")";
                } else {
                    of << "__builtin_add_overflow";
                    of << "(";
                    emitParam(e.args.at(0));
                    of << ", ";
                    emitParam(e.args.at(1));
                    of << ", &";
                    emitLvalue(e.retVal);
                    of << ")";

                }

                if (name == "saturating_add") {
                    of << ") { ";
                    emitLvalue(e.retVal);
                    of << " = ";
                    switch (getRealPrimTy(ty->as_Primitive())) {
                        case ::HIR::CoreType::U8:
                        case ::HIR::CoreType::U16:
                        case ::HIR::CoreType::U32:
                        case ::HIR::CoreType::U64:
                            of << "-1"; // -1 should extend to MAX
                            break;
                        case ::HIR::CoreType::U128:
                            if (options.emulatedI128) {
                                of << "make128_raw(-1, -1)";
                            } else {
                                of << "-1";
                            }
                            break;
                        // If the LHS is negative, then the only way overflow can happen is if the RHS is also negative, so saturate at negative.
                        case ::HIR::CoreType::I8:
                            of << "(";
                            emitParam(e.args.at(0));
                            of << " < 0 ? -0x80 : 0x7F)";
                            break;
                        case ::HIR::CoreType::I16:
                            of << "(";
                            emitParam(e.args.at(0));
                            of << " < 0 ? -0x8000 : 0x7FFF)";
                            break;
                        case ::HIR::CoreType::I32:
                            of << "(";
                            emitParam(e.args.at(0));
                            of << " < 0 ? -0x8000000l : 0x7FFFFFFFl)";
                            break;
                        case ::HIR::CoreType::I64:
                            of << "(";
                            emitParam(e.args.at(0));
                            of << " < 0 ? -0x8000000"
                                    "00000000ll : 0x7FFFFFFF"
                                    "FFFFFFFFll)";
                            break;
                        case ::HIR::CoreType::I128:
                            if (options.emulatedI128) {
                                of << "( (int64_t)(";
                                emitParam(e.args.at(0));
                                of << ".hi) < 0 ? make128s_raw(-0x8000000"
                                        "00000000ll, 0) : make128s_raw(0x7FFFFFFF"
                                        "FFFFFFFFll, -1))";
                            } else {
                                of << "(";
                                emitParam(e.args.at(0));
                                of << " < 0 ? ((uint128_t)1 << 127) : (((uint128_t)1 << 127) - 1))";
                            }
                            break;
                        default:
                            MIR_TODO(localMirRes, "saturating_add - " << ty);
                    }
                    of << "; }";
                }
            } else if (name == "overflowing_sub" || name == "wrapping_sub" || name == "saturating_sub" || name == "unchecked_sub") {
                const auto& ty = params.types.at(0);
                if (name == "saturating_sub") {
                    of << "if( ";
                }
                if (options.emulatedI128 && ty == ::HIR::CoreType::U128) {
                    of << "sub128_o";
                    of << "(";
                    emitParam(e.args.at(0));
                    of << ", ";
                    emitParam(e.args.at(1));
                    of << ", &";
                    emitLvalue(e.retVal);
                    of << ")";
                } else if (options.emulatedI128 && ty == ::HIR::CoreType::I128) {
                    of << "sub128s_o";
                    of << "(";
                    emitParam(e.args.at(0));
                    of << ", ";
                    emitParam(e.args.at(1));
                    of << ", &";
                    emitLvalue(e.retVal);
                    of << ")";
                } else {
                    of << "__builtin_sub_overflow";
                    of << "(";
                    emitParam(e.args.at(0));
                    of << ", ";
                    emitParam(e.args.at(1));
                    of << ", &";
                    emitLvalue(e.retVal);
                    of << ")";

                }

                if (name == "saturating_sub") {
                    of << ") { ";
                    emitLvalue(e.retVal);
                    of << " = ";
                    switch (getRealPrimTy(ty->as_Primitive())) {
                        case ::HIR::CoreType::U8:
                        case ::HIR::CoreType::U16:
                        case ::HIR::CoreType::U32:
                        case ::HIR::CoreType::U64:
                            of << "0";
                            break;
                        case ::HIR::CoreType::U128:
                            if (options.emulatedI128) {
                                of << "make128(0)";
                            } else {
                                of << "0";
                            }
                            break;
                        case ::HIR::CoreType::I8:
                            of << "(";
                            emitParam(e.args.at(0));
                            of << " < 0 ? -0x80 : 0x7F)";
                            break;
                        case ::HIR::CoreType::I16:
                            of << "(";
                            emitParam(e.args.at(0));
                            of << " < 0 ? -0x8000 : 0x7FFF)";
                            break;
                        case ::HIR::CoreType::I32:
                            of << "(";
                            emitParam(e.args.at(0));
                            of << " < 0 ? -0x8000000l : 0x7FFFFFFFl)";
                            break;
                        case ::HIR::CoreType::I64:
                            of << "(";
                            emitParam(e.args.at(0));
                            of << " < 0 ? -0x8000000"
                                    "00000000ll : 0x7FFFFFFF"
                                    "FFFFFFFFll)";
                            break;
                        case ::HIR::CoreType::I128:
                            if (options.emulatedI128) {
                                of << "( (int64_t)(";
                                emitParam(e.args.at(0));
                                of << ".hi) < 0 ? make128s_raw(-0x8000000"
                                        "00000000ll, 0) : make128s_raw(0x7FFFFFFF"
                                        "FFFFFFFFll, -1))";
                            } else {
                                of << "(";
                                emitParam(e.args.at(0));
                                of << " < 0 ? ((uint128_t)1 << 127) : (((uint128_t)1 << 127) - 1))";
                            }
                            break;
                        default:
                            MIR_TODO(localMirRes, "saturating_sub - " << ty);
                    }
                    of << "; }";
                }
            } else if (name == "overflowing_mul" || name == "wrapping_mul" || name == "unchecked_mul") {
                if (options.emulatedI128 && params.types.at(0) == ::HIR::CoreType::U128) {
                    of << "mul128_o";
                    of << "(";
                    emitParam(e.args.at(0));
                    of << ", ";
                    emitParam(e.args.at(1));
                    of << ", &";
                    emitLvalue(e.retVal);
                    of << ")";
                } else if (options.emulatedI128 && params.types.at(0) == ::HIR::CoreType::I128) {
                    of << "mul128s_o";
                    of << "(";
                    emitParam(e.args.at(0));
                    of << ", ";
                    emitParam(e.args.at(1));
                    of << ", &";
                    emitLvalue(e.retVal);
                    of << ")";
                } else {
                    of << "__builtin_mul_overflow";
                    of << "(";
                    emitParam(e.args.at(0));
                    of << ", ";
                    emitParam(e.args.at(1));
                    of << ", &";
                    emitLvalue(e.retVal);
                    of << ")";

                }
            }
            // Unchecked Arithmetic
            // - exact_div is UB to call on a non-multiple
            else if (name == "unchecked_div" || name == "exact_div") {
                emitLvalue(e.retVal);
                of << " = ";
                if (typeIsEmulatedI128(params.types.at(0))) {
                    of << "div128";
                    if (params.types.at(0) == ::HIR::CoreType::I128) {
                        of << "s";
                    }
                    of << "(";
                    emitParam(e.args.at(0));
                    of << ", ";
                    emitParam(e.args.at(1));
                    of << ")";
                } else {
                    emitParam(e.args.at(0));
                    of << " / ";
                    emitParam(e.args.at(1));
                }
            } else if (name == "unchecked_rem") {
                emitLvalue(e.retVal);
                of << " = ";
                if (typeIsEmulatedI128(params.types.at(0))) {
                    of << "mod128";
                    if (params.types.at(0) == ::HIR::CoreType::I128) {
                        of << "s";
                    }
                    of << "(";
                    emitParam(e.args.at(0));
                    of << ", ";
                    emitParam(e.args.at(1));
                    of << ")";
                } else {
                    emitParam(e.args.at(0));
                    of << " % ";
                    emitParam(e.args.at(1));
                }
            } else if (name == "unchecked_shl") {
                emitLvalue(e.retVal);
                of << " = ";
                if (typeIsEmulatedI128(params.types.at(0))) {
                    of << "shl128";
                    if (params.types.at(0) == ::HIR::CoreType::I128) {
                        of << "s";
                    }
                    of << "(";
                    emitParam(e.args.at(0));
                    of << ", ";
                    emitParam(e.args.at(1));
                    // If the shift type is a u128/i128, get the inner
                    ::HIR::TypeRef tmp;
                    const auto& shiftTy = localMirRes.getParamType(tmp, e.args.at(1));
                    if (shiftTy == ::HIR::CoreType::I128 || shiftTy == ::HIR::CoreType::U128) {
                        of << ".lo";
                    }
                    of << ")";
                } else {
                    emitParam(e.args.at(0));
                    of << " << ";
                    emitParam(e.args.at(1));
                }
            } else if (name == "unchecked_shr") {
                emitLvalue(e.retVal);
                of << " = ";
                if (typeIsEmulatedI128(params.types.at(0))) {
                    of << "shr128";
                    if (params.types.at(0) == ::HIR::CoreType::I128) {
                        of << "s";
                    }
                    of << "(";
                    emitParam(e.args.at(0));
                    of << ", ";
                    emitParam(e.args.at(1));
                    // If the shift type is a u128/i128, get the inner
                    ::HIR::TypeRef tmp;
                    const auto& shiftTy = localMirRes.getParamType(tmp, e.args.at(1));
                    if (shiftTy == ::HIR::CoreType::I128 || shiftTy == ::HIR::CoreType::U128) {
                        of << ".lo";
                    }
                    of << ")";
                } else {
                    emitParam(e.args.at(0));
                    of << " >> ";
                    emitParam(e.args.at(1));
                }
            }
            // Rotate
            else if (name == "rotate_left") {
                const auto& ty = params.types.at(0);
                switch (getRealPrimTy(ty->as_Primitive())) {
                    case ::HIR::CoreType::I8:
                    case ::HIR::CoreType::U8:
                        of << "{";
                        of << " uint8_t v = ";
                        emitParam(e.args.at(0));
                        of << ";";
                        of << " unsigned shift = ";
                        emitParam(e.args.at(1));
                        of << " % 8;";
                        of << " ";
                        emitLvalue(e.retVal);
                        of << " = shift == 0 ? v : (v << shift) | (v >> (8 - shift));";
                        of << "}";
                        break;
                    case ::HIR::CoreType::I16:
                    case ::HIR::CoreType::U16:
                        of << "{";
                        of << " uint16_t v = ";
                        emitParam(e.args.at(0));
                        of << ";";
                        of << " unsigned shift = ";
                        emitParam(e.args.at(1));
                        of << " % 16;";
                        of << " ";
                        emitLvalue(e.retVal);
                        of << " = shift == 0 ? v : (v << shift) | (v >> (16 - shift));";
                        of << "}";
                        break;
                    case ::HIR::CoreType::I32:
                    case ::HIR::CoreType::U32:
                        of << "{";
                        of << " uint32_t v = ";
                        emitParam(e.args.at(0));
                        of << ";";
                        of << " unsigned shift = ";
                        emitParam(e.args.at(1));
                        of << " % 32;";
                        of << " ";
                        emitLvalue(e.retVal);
                        of << " = shift == 0 ? v : (v << shift) | (v >> (32 - shift));";
                        of << "}";
                        break;
                    case ::HIR::CoreType::I64:
                    case ::HIR::CoreType::U64:
                        of << "{";
                        of << " uint64_t v = ";
                        emitParam(e.args.at(0));
                        of << ";";
                        of << " unsigned shift = ";
                        emitParam(e.args.at(1));
                        of << " % 64;";
                        of << " ";
                        emitLvalue(e.retVal);
                        of << " = shift == 0 ? v : (v << shift) | (v >> (64 - shift));";
                        of << "}";
                        break;
                    case ::HIR::CoreType::I128:
                    case ::HIR::CoreType::U128:
                        of << "{";
                        of << " uint128_t v = ";
                        emitParam(e.args.at(0));
                        of << ";";
                        of << " unsigned shift = ";
                        emitParam(e.args.at(1));
                        of << " % 128;";
                        if (options.emulatedI128) {
                            of << " if(shift == 0) {";
                            of << " ";
                            emitLvalue(e.retVal);
                            of << " = v;";
                            of << " } else if(shift < 64) {";
                            of << " ";
                            emitLvalue(e.retVal);
                            of << ".lo = (v.lo << shift) | (v.hi >> (64 - shift));";
                            of << " ";
                            emitLvalue(e.retVal);
                            of << ".hi = (v.hi << shift) | (v.lo >> (64 - shift));";
                            of << " } else if(shift == 64) {";
                            of << " ";
                            emitLvalue(e.retVal);
                            of << ".lo = v.hi;";
                            of << " ";
                            emitLvalue(e.retVal);
                            of << ".hi = v.lo;";
                            of << " } else {";
                            of << " shift -= 64;"; // Swap order and reduce shift
                            of << " ";
                            emitLvalue(e.retVal);
                            of << ".lo = (v.hi << shift) | (v.lo >> (64 - shift));";
                            of << " ";
                            emitLvalue(e.retVal);
                            of << ".hi = (v.lo << shift) | (v.hi >> (64 - shift));";
                            of << " }";
                        } else {
                            of << " ";
                            emitLvalue(e.retVal);
                            of << " = shift == 0 ? v : (v << shift) | (v >> (128 - shift));";
                        }
                        of << "}";
                        break;
                    default:
                        MIR_TODO(localMirRes, "rotate_left - " << ty);
                }
            } else if (name == "rotate_right") {
                const auto& ty = params.types.at(0);
                switch (getRealPrimTy(ty->as_Primitive())) {
                    case ::HIR::CoreType::I8:
                    case ::HIR::CoreType::U8:
                        of << "{";
                        of << " uint8_t v = ";
                        emitParam(e.args.at(0));
                        of << ";";
                        of << " unsigned shift = ";
                        emitParam(e.args.at(1));
                        of << " % 8;";
                        of << " ";
                        emitLvalue(e.retVal);
                        of << " = shift == 0 ? v : (v >> shift) | (v << (8 - shift));";
                        of << "}";
                        break;
                    case ::HIR::CoreType::I16:
                    case ::HIR::CoreType::U16:
                        of << "{";
                        of << " uint16_t v = ";
                        emitParam(e.args.at(0));
                        of << ";";
                        of << " unsigned shift = ";
                        emitParam(e.args.at(1));
                        of << " % 16;";
                        of << " ";
                        emitLvalue(e.retVal);
                        of << " = shift == 0 ? v : (v >> shift) | (v << (16 - shift));";
                        of << "}";
                        break;
                    case ::HIR::CoreType::I32:
                    case ::HIR::CoreType::U32:
                        of << "{";
                        of << " uint32_t v = ";
                        emitParam(e.args.at(0));
                        of << ";";
                        of << " unsigned shift = ";
                        emitParam(e.args.at(1));
                        of << " % 32;";
                        of << " ";
                        emitLvalue(e.retVal);
                        of << " = shift == 0 ? v : (v >> shift) | (v << (32 - shift));";
                        of << "}";
                        break;
                    case ::HIR::CoreType::I64:
                    case ::HIR::CoreType::U64:
                        of << "{";
                        of << " uint64_t v = ";
                        emitParam(e.args.at(0));
                        of << ";";
                        of << " unsigned shift = ";
                        emitParam(e.args.at(1));
                        of << " % 64;";
                        of << " ";
                        emitLvalue(e.retVal);
                        of << " = shift == 0 ? v : (v >> shift) | (v << (64 - shift));";
                        of << "}";
                        break;
                    case ::HIR::CoreType::I128:
                    case ::HIR::CoreType::U128:
                        of << "{";
                        of << " uint128_t v = ";
                        emitParam(e.args.at(0));
                        of << ";";
                        of << " unsigned shift = ";
                        emitParam(e.args.at(1));
                        of << " % 128;";
                        if (options.emulatedI128) {
                            of << " if(shift == 0) {";
                            of << " ";
                            emitLvalue(e.retVal);
                            of << " = v;";
                            of << " } else if(shift < 64) {";
                            of << " ";
                            emitLvalue(e.retVal);
                            of << ".lo = (v.lo >> shift) | (v.hi << (64 - shift));";
                            of << " ";
                            emitLvalue(e.retVal);
                            of << ".hi = (v.hi >> shift) | (v.lo << (64 - shift));";
                            of << " } else if(shift == 64) {";
                            of << " ";
                            emitLvalue(e.retVal);
                            of << ".lo = v.hi;";
                            of << " ";
                            emitLvalue(e.retVal);
                            of << ".hi = v.lo;";
                            of << " } else {";
                            of << " shift -= 64;"; // Swap order and reduce shift
                            of << " ";
                            emitLvalue(e.retVal);
                            of << ".lo = (v.hi >> shift) | (v.lo << (64 - shift));";
                            of << " ";
                            emitLvalue(e.retVal);
                            of << ".hi = (v.lo >> shift) | (v.hi << (64 - shift));";
                            of << " }";
                        } else {
                            of << " ";
                            emitLvalue(e.retVal);
                            of << " = shift == 0 ? v : (v >> shift) | (v << (128 - shift));";
                        }
                        of << "}";
                        break;
                    default:
                        MIR_TODO(localMirRes, "rotate_right - " << ty);
                }
            }
            // Bit Twiddling
            // - CounT Leading Zeroes
            // - CounT Trailing Zeroes
            else if (name == "ctlz" || name == "ctlz_nonzero" || name == "cttz" || name == "cttz_nonzero") {
                auto emitArg0 = [&]() {
                    emitParam(e.args.at(0));
                };
                const auto& ty = params.types.at(0);
                emitLvalue(e.retVal);
                of << " = (";
                if (ty == ::HIR::CoreType::U128 || ty == ::HIR::CoreType::I128) {
                    if (ty == ::HIR::CoreType::I128) {
                        if (options.emulatedI128) {
                            of << "uint128_to_int128(";
                        } else {
                            of << "(int128_t)";
                        }
                    }
                    if (name == "ctlz" || name == "ctlz_nonzero") {
                        of << "intrinsic_ctlz_u128(";
                    } else {
                        of << "intrinsic_cttz_u128(";
                    }
                    if (ty == ::HIR::CoreType::I128) {
                        if (options.emulatedI128) {
                            of << "int128_to_uint128(";
                        } else {
                            of << "(uint128_t)";
                        }
                    }
                    emitParam(e.args.at(0));
                    of << ")";
                    if (ty == ::HIR::CoreType::I128 && options.emulatedI128) {
                        of << ")";
                        of << ")";
                    } else {
                    }
                    of << ")";
                    if (options.emulatedI128) {
                        of << ".lo";
                    }
                    of << ";";
                    return;
                } else if (ty == ::HIR::CoreType::U64 || (ty == ::HIR::CoreType::Usize && TargetGetPointerBits() > 32)) {
                    emitParam(e.args.at(0));
                    of << " != 0 ? ";
                    if (name == "ctlz" || name == "ctlz_nonzero") {
                        of << "__builtin_clz64(";
                        emitArg0();
                        of << ")";
                    } else {
                        of << "__builtin_ctz64(";
                        emitArg0();
                        of << ")";
                    }
                } else {
                    emitParam(e.args.at(0));
                    of << " != 0 ? ";
                    if (name == "ctlz" || name == "ctlz_nonzero") {
                        of << "__builtin_clz(";
                        if (ty == ::HIR::CoreType::U8 || ty == ::HIR::CoreType::I8) {
                            of << "(uint8_t)(";
                        } else if (ty == ::HIR::CoreType::U16 || ty == ::HIR::CoreType::I16) {
                            of << "(uint16_t)(";
                        }
                        emitParam(e.args.at(0));
                        if (ty == ::HIR::CoreType::U8 || ty == ::HIR::CoreType::I8
                            || ty == ::HIR::CoreType::U16 || ty == ::HIR::CoreType::I16) {
                            of << ")";
                        }
                        of << ")";
                        if (ty == ::HIR::CoreType::U8 || ty == ::HIR::CoreType::I8) {
                            of << " - 24";
                        } else if (ty == ::HIR::CoreType::U16 || ty == ::HIR::CoreType::I16) {
                            of << " - 16";
                        }
                    } else {
                        of << "__builtin_ctz(";
                        emitParam(e.args.at(0));
                        of << ")";
                    }
                }
                of << " : sizeof(";
                emitCtype(ty);
                of << ")*8)";
            }
            // - CounT POPulated
            else if (name == "ctpop") {
                emitLvalue(e.retVal);
                of << " = ";

                if (typeIsEmulatedI128(params.types.at(0))) {
                    of << "popcount128(";
                    emitParam(e.args.at(0));
                    of << ")";
                    of << ".lo";
                } else {
                    of << "__builtin_popcountll(";
                    emitParam(e.args.at(0));
                    of << ")";
                }
            }
            // --- Floating Point
            else if ((name.size() > 3 && name.compare(name.size() - 3, 3, "f16") == 0) || (name.size() > 3 && name.compare(name.size() - 3, 3, "f32") == 0) || (name.size() > 3 && name.compare(name.size() - 3, 3, "f64") == 0) || (name.size() > 4 && name.compare(name.size() - 4, 4, "f128") == 0)) {
                if (name.compare(name.size() - 3, 3, "f16") == 0) {
                    of << "abort();";
                    return;
                }
                if (name.compare(name.size() - 4, 4, "f128") == 0) {
                    of << "abort();";
                    return;
                }
                auto emitMathName = [&](const char* op) {
                    of << "__builtin_";
                    of << op << (name.back() == '2' ? "f" : "");
                };
                auto emit1 = [&](const char* op) {
                    if (name.compare(name.size() - 3, 3, "f16") == 0) {
                        of << "abort();";
                        return;
                    }
                    emitLvalue(e.retVal);
                    of << " = ";
                    emitMathName(op);
                    of << "(";
                    emitParam(e.args.at(0));
                    of << ")";
                };
                // > Round to nearest integer, half-way rounds away from zero
                if (name == "rintf32" || name == "rintf64") {
                    emit1("round");
                }
                // > Round to nearest integer, half-way rounds to even
                else if (name == "round_ties_even_f32" || name == "round_ties_even_f64") {
                    emit1("roundeven");
                } else if (name == "fabsf32" || name == "fabsf64") {
                    emit1("fabs");
                } else if (name == "copysignf32" || name == "copysignf64") {
                    emitLvalue(e.retVal);
                    of << " = ";
                    emitMathName("copysign");
                    of << "(";
                    emitParam(e.args.at(0));
                    of << ", ";
                    emitParam(e.args.at(1));
                    of << ")";
                }
                // > Returns the integer part of an `f32`.
                else if (name == "truncf32" || name == "truncf64") {
                    emit1("trunc");
                } else if (name == "powif32" || name == "powif64") {
                    emitLvalue(e.retVal);
                    of << " = ";
                    emitMathName("pow");
                    of << "(";
                    emitParam(e.args.at(0));
                    of << ", ";
                    emitParam(e.args.at(1));
                    of << ")";
                } else if (name == "powf32" || name == "powf64") {
                    emitLvalue(e.retVal);
                    of << " = ";
                    emitMathName("pow");
                    of << "(";
                    emitParam(e.args.at(0));
                    of << ", ";
                    emitParam(e.args.at(1));
                    of << ")";
                } else if (name == "expf32" || name == "expf64") {
                    emit1("exp");
                } else if (name == "exp2f32" || name == "exp2f64") {
                    emit1("exp2");
                } else if (name == "logf32" || name == "logf64") {
                    emit1("log");
                } else if (name == "log10f32" || name == "log10f64") {
                    emit1("log10");
                } else if (name == "log2f32" || name == "log2f64") {
                    emit1("log2");
                } else if (name == "sqrtf32" || name == "sqrtf64") {
                    emit1("sqrt");
                } else if (name == "ceilf16" || name == "ceilf32" || name == "ceilf64") {
                    emit1("ceil");
                } else if (name == "floorf32" || name == "floorf64") {
                    emit1("floor");
                } else if (name == "roundf32" || name == "roundf64") {
                    emit1("round");
                } else if (name == "cosf32" || name == "cosf64") {
                    emit1("cos");
                } else if (name == "sinf32" || name == "sinf64") {
                    emit1("sin");
                } else if (name == "fmaf32" || name == "fmaf64") {
                    emitLvalue(e.retVal);
                    of << " = ";
                    emitMathName("fma");
                    of << "(";
                    emitParam(e.args.at(0));
                    of << ", ";
                    emitParam(e.args.at(1));
                    of << ", ";
                    emitParam(e.args.at(2));
                    of << ")";
                } else if (name == "maxnumf32" || name == "maxnumf64") {
                    emitLvalue(e.retVal);
                    of << " = ";
                    emitMathName("fmax");
                    of << "(";
                    emitParam(e.args.at(0));
                    of << ", ";
                    emitParam(e.args.at(1));
                    of << ")";
                } else if (name == "minnumf32" || name == "minnumf64") {
                    emitLvalue(e.retVal);
                    of << " = ";
                    emitMathName("fmin");
                    of << "(";
                    emitParam(e.args.at(0));
                    of << ", ";
                    emitParam(e.args.at(1));
                    of << ")";
                } else {
                    MIR_BUG(localMirRes, "Unknown float intrinsic '" << name << "'");
                }
            }
            // --- Volatile Load/Store
            else if (name == "volatile_load") {
                // A ZST has no bytes to access.  In particular, Rust permits
                // these operations with a dangling ZST pointer, so emitting a
                // C volatile dereference would invent an observable access.
                if (!this->typeIsBadZst(params.types.at(0))) {
                    emitLvalue(e.retVal);
                    of << " = *(volatile ";
                    emitCtype(params.types.at(0));
                    of << "*)";
                    emitParam(e.args.at(0));
                }
            } else if (name == "volatile_store") {
                if (!this->typeIsBadZst(params.types.at(0))) {
                    of << "*(volatile ";
                    emitCtype(params.types.at(0));
                    of << "*)";
                    emitParam(e.args.at(0));
                    of << " = ";
                    emitParam(e.args.at(1));
                }
            } else if (name == "nontemporal_store") {
                // TODO: Actually do a non-temporal store
                // GCC: _mm_stream_* (depending on input type, which must be `repr(simd)`)
                if (!this->typeIsBadZst(params.types.at(0))) {
                    of << "*(volatile ";
                    emitCtype(params.types.at(0));
                    of << "*)";
                    emitParam(e.args.at(0));
                    of << " = ";
                    emitParam(e.args.at(1));
                }
            }
            // --- Atomics!
            else if (name.compare(0, 7, "atomic_") == 0) {
                // > Single-ordering atomics
                if (name == "atomic_xadd" || name.compare(0, 7 + 4 + 1, "atomic_xadd_") == 0) {
                    auto ordering = getAtomicOrdering(name, 7 + 4 + 1);
                    emitAtomicArith(AtomicOp::Add, ordering);
                } else if (name == "atomic_xsub" || name.compare(0, 7 + 4 + 1, "atomic_xsub_") == 0) {
                    auto ordering = getAtomicOrdering(name, 7 + 4 + 1);
                    emitAtomicArith(AtomicOp::Sub, ordering);
                } else if (name == "atomic_and" || name.compare(0, 7 + 3 + 1, "atomic_and_") == 0) {
                    auto ordering = getAtomicOrdering(name, 7 + 3 + 1);
                    emitAtomicArith(AtomicOp::And, ordering);
                } else if (name == "atomic_nand" || name.compare(0, 7 + 4 + 1, "atomic_nand_") == 0) {
                    auto ordering = getAtomicOrdering(name, 7 + 4 + 1);
                    const auto& ty = params.types.at(0);
                    emitLvalue(e.retVal);
                    of << " = ";
                    emitAtomicRmwCast();
                    of << "__mrustc_atomicloop" << getPrimSize(ty) << "(";
                    of << "(volatile uint" << getPrimSize(ty) << "_t*)";
                    emitParam(e.args.at(0));
                    of << ", ";
                    emitAtomicRmwOperand(e.args.at(1));
                    of << ", " << getAtomicTyGcc(ordering);
                    of << ", __mrustc_op_and_not" << getPrimSize(ty);
                    of << ")";
                } else if (name == "atomic_or" || name.compare(0, 7 + 2 + 1, "atomic_or_") == 0) {
                    auto ordering = getAtomicOrdering(name, 7 + 2 + 1);
                    emitAtomicArith(AtomicOp::Or, ordering);
                } else if (name == "atomic_xor" || name.compare(0, 7 + 3 + 1, "atomic_xor_") == 0) {
                    auto ordering = getAtomicOrdering(name, 7 + 3 + 1);
                    emitAtomicArith(AtomicOp::Xor, ordering);
                } else if (name == "atomic_max" || name.compare(0, 7 + 3 + 1, "atomic_max_") == 0 || name == "atomic_min" || name.compare(0, 7 + 3 + 1, "atomic_min_") == 0) {
                    auto ordering = getAtomicOrdering(name, 7 + 3 + 1);
                    const auto& ty = params.types.at(0);
                    const char* op = (name.c_str()[7 + 1] == 'a' ? "imax" : "imin"); // m'a'x vs m'i'n
                    emitLvalue(e.retVal);
                    of << " = ";
                    emitAtomicRmwCast();
                    of << "__mrustc_atomicloop" << getPrimSize(ty) << "(";
                    of << "(volatile uint" << getPrimSize(ty) << "_t*)";
                    emitParam(e.args.at(0));
                    of << ", ";
                    emitAtomicRmwOperand(e.args.at(1));
                    of << ", " << getAtomicTyGcc(ordering);
                    of << ", __mrustc_op_" << op << getPrimSize(ty);
                    of << ")";
                } else if (name == "atomic_umax" || name.compare(0, 7 + 4 + 1, "atomic_umax_") == 0 || name == "atomic_umin" || name.compare(0, 7 + 4 + 1, "atomic_umin_") == 0) {
                    auto ordering = getAtomicOrdering(name, 7 + 4 + 1);
                    const auto& ty = params.types.at(0);
                    const char* op = (name.c_str()[7 + 2] == 'a' ? "umax" : "umin"); // m'a'x vs m'i'n
                    emitLvalue(e.retVal);
                    of << " = ";
                    emitAtomicRmwCast();
                    of << "__mrustc_atomicloop" << getPrimSize(ty) << "(";
                    of << "(volatile uint" << getPrimSize(ty) << "_t*)";
                    emitParam(e.args.at(0));
                    of << ", ";
                    emitAtomicRmwOperand(e.args.at(1));
                    of << ", " << getAtomicTyGcc(ordering);
                    of << ", __mrustc_op_" << op << getPrimSize(ty);
                    of << ")";
                } else if (name == "atomic_load" || name.compare(0, 7 + 4 + 1, "atomic_load_") == 0) {
                    auto ordering = getAtomicOrdering(name, 7 + 4 + 1);
                    emitLvalue(e.retVal);
                    of << " = ";
                    of << "__atomic_load_n(";
                    emitAtomicCast();
                    emitParam(e.args.at(0));
                    of << ", " << getAtomicTyGcc(ordering) << ")";

                } else if (name == "atomic_store" || name.compare(0, 7 + 5 + 1, "atomic_store_") == 0) {
                    auto ordering = getAtomicOrdering(name, 7 + 5 + 1);
                    of << "__atomic_store_n(";
                    emitAtomicCast();
                    emitParam(e.args.at(0));
                    of << ", ";
                    emitParam(e.args.at(1));
                    of << ", " << getAtomicTyGcc(ordering) << ")";

                }
                // Comare+Exchange (has two orderings)
                else if (name == "atomic_cxchg_acq_failrelaxed") {
                    emitAtomicCxchg(e, Ordering::Acquire, Ordering::Relaxed, false);
                } else if (name == "atomic_cxchg_acqrel_failrelaxed") {
                    emitAtomicCxchg(e, Ordering::AcqRel, Ordering::Relaxed, false);
                }
                // _rel = Release, Relaxed (not Release,Release)
                else if (name == "atomic_cxchg_rel") {
                    emitAtomicCxchg(e, Ordering::Release, Ordering::Relaxed, false);
                }
                // _acqrel = Release, Acquire (not AcqRel,AcqRel)
                else if (name == "atomic_cxchg_acqrel") {
                    emitAtomicCxchg(e, Ordering::AcqRel, Ordering::Acquire, false);
                } else if (name.compare(0, 7 + 6 + 4, "atomic_cxchg_fail") == 0) {
                    auto failOrdering = getAtomicOrdering(name, 7 + 6 + 4);
                    emitAtomicCxchg(e, Ordering::SeqCst, failOrdering, false);
                } else if (name == "atomic_cxchg" || name.compare(0, 7 + 6, "atomic_cxchg_") == 0) {
                    auto ordering = getAtomicOrdering(name, 7 + 6);
                    emitAtomicCxchg(e, ordering, ordering, false);
                } else if (name == "atomic_cxchgweak_acq_failrelaxed") {
                    emitAtomicCxchg(e, Ordering::Acquire, Ordering::Relaxed, true);
                } else if (name == "atomic_cxchgweak_acqrel_failrelaxed") {
                    emitAtomicCxchg(e, Ordering::AcqRel, Ordering::Relaxed, true);
                } else if (name.compare(0, 7 + 10 + 4, "atomic_cxchgweak_fail") == 0) {
                    auto failOrdering = getAtomicOrdering(name, 7 + 10 + 4);
                    emitAtomicCxchg(e, Ordering::SeqCst, failOrdering, true);
                } else if (name == "atomic_cxchgweak") {
                    emitAtomicCxchg(e, Ordering::SeqCst, Ordering::SeqCst, true);
                } else if (name == "atomic_cxchgweak_acq") {
                    emitAtomicCxchg(e, Ordering::Acquire, Ordering::Acquire, true);
                } else if (name == "atomic_cxchgweak_rel") {
                    emitAtomicCxchg(e, Ordering::Release, Ordering::Relaxed, true);
                } else if (name == "atomic_cxchgweak_acqrel") {
                    emitAtomicCxchg(e, Ordering::AcqRel, Ordering::Acquire, true);
                } else if (name == "atomic_cxchgweak_relaxed") {
                    emitAtomicCxchg(e, Ordering::Relaxed, Ordering::Relaxed, true);
                } else if (name == "atomic_cxchgweak" || name.compare(0, 91 - 74, "atomic_cxchgweak_") == 0) {
                    auto ordering = getAtomicOrdering(name, 91 - 74);
                    emitAtomicCxchg(e, ordering, ordering, false);
                } else if (name == "atomic_xchg" || name.compare(0, 7 + 5, "atomic_xchg_") == 0) {
                    auto ordering = getAtomicOrdering(name, 7 + 5);
                    emitLvalue(e.retVal);
                    of << " = ";
                    of << "__atomic_exchange_n(";
                    emitAtomicCast();
                    emitParam(e.args.at(0));
                    of << ", ";
                    emitParam(e.args.at(1));
                    of << ", " << getAtomicTyGcc(ordering) << ")";

                } else if (name == "atomic_fence" || name.compare(0, 7 + 6, "atomic_fence_") == 0) {
                    auto ordering = getAtomicOrdering(name, 7 + 6);
                    of << "__atomic_thread_fence(" << getAtomicTyGcc(ordering) << ")";

                } else if (name == "atomic_singlethreadfence" || name.compare(0, 7 + 18, "atomic_singlethreadfence_") == 0) {
                    // TODO: Does this matter?
                } else {
                    MIR_BUG(localMirRes, "Unknown atomic intrinsic '" << name << "'");
                }
            } else if (name == "option_payload_ptr") { // 1.74 only, removed later
                // Converts `*const Option<T>` to `*const T`, even if `None`
                emitLvalue(e.retVal);
                of << " = &(";
                emitParam(e.args.at(0));
                of << ")->DATA.var_1. _0";
            }
            // -- stdarg --
            else if (name == "va_copy") {
                of << "va_copy( *(va_list*)&";
                emitParam(e.args.at(0));
                of << ", *(va_list*)&";
                emitParam(e.args.at(1));
                of << ")";
            }
            // -- Platform Intrinsics (and SIMD) --
            else if (name.compare(0, 9, "platform:") == 0 || name.compare(0, 5, "simd_") == 0) {
                auto nameStrip = ::std::string_view(name.c_str() + (name.compare(0, 9, "platform:") == 0 ? 9 : 0));

                struct SimdInfo {
                    unsigned count;
                    unsigned itemSize;

                    enum Ty {
                        Float,
                        Signed,
                        Unsigned,
                    } ty;

                    static SimdInfo forTy(const CodeGeneratorC& self, const HIR::TypeData* ty) {
                        const auto* tyRepr = TargetGetTypeRepr(self.sp, self.mirRes->mResolve, ty);
                        MIR_ASSERT(*self.mirRes, tyRepr, "No repr for " << ty);
                        size_t sizeSlot = tyRepr->size;
                        const auto& ity = tyRepr->fields[0].ty;
                        DEBUG("SimdInfo Type: " << ity);
                        const auto& tyVal = ity->is_Primitive() ? ity : tyRepr->fields[0].ty->as_Array().inner;
                        DEBUG("ty_val = " << tyVal);
                        size_t sizeVal = 0;
                        MIR_ASSERT(*self.mirRes, TargetGetSizeOf(self.sp, self.mResolve, tyVal, sizeVal), tyVal);

                        MIR_ASSERT(*self.mirRes, sizeSlot >= sizeVal, sizeSlot << " < " << sizeVal);
                        MIR_ASSERT(*self.mirRes, sizeVal > 0, "SimdInfo::for_ty - Value type " << tyVal << " was a ZST");
                        MIR_ASSERT(*self.mirRes, sizeSlot / sizeVal * sizeVal == sizeSlot, sizeSlot << " not a multiple of " << sizeVal);

                        SimdInfo rv;
                        rv.itemSize = sizeVal;
                        rv.count = sizeSlot == 0 ? 0 : sizeSlot / sizeVal;
                        switch (tyVal->as_Primitive()) {
                            case ::HIR::CoreType::I8:
                                rv.ty = Signed;
                                break;
                            case ::HIR::CoreType::I16:
                                rv.ty = Signed;
                                break;
                            case ::HIR::CoreType::I32:
                                rv.ty = Signed;
                                break;
                            case ::HIR::CoreType::I64:
                                rv.ty = Signed;
                                break;
                            //case ::HIR::CoreType::I128: rv.ty = Signed; break;
                            case ::HIR::CoreType::U8:
                                rv.ty = Unsigned;
                                break;
                            case ::HIR::CoreType::U16:
                                rv.ty = Unsigned;
                                break;
                            case ::HIR::CoreType::U32:
                                rv.ty = Unsigned;
                                break;
                            case ::HIR::CoreType::U64:
                                rv.ty = Unsigned;
                                break;
                            //case ::HIR::CoreType::U128: rv.ty = Unsigned; break;
                            case ::HIR::CoreType::F16:
                                rv.ty = Float;
                                break;
                            case ::HIR::CoreType::F32:
                                rv.ty = Float;
                                break;
                            case ::HIR::CoreType::F64:
                                rv.ty = Float;
                                break;
                            case ::HIR::CoreType::F128:
                                rv.ty = Float;
                                break;
                            default:
                                MIR_BUG(*self.mirRes, "Invalid SIMD type inner - " << tyVal);
                        }
                        return rv;
                    }

                    void emitValTy(CodeGeneratorC& self) {
                        switch (ty) {
                            case Float:
                                self.of << (itemSize == 4 ? "float" : "double");
                                break;
                            case Signed:
                                self.of << "int" << (itemSize * 8) << "_t";
                                break;
                            case Unsigned:
                                self.of << "uint" << (itemSize * 8) << "_t";
                                break;
                        }
                    }
                };

                auto simdCmp = [&](const char* op) {
                    auto srcInfo = SimdInfo::forTy(*this, params.types.at(0));
                    auto dstInfo = SimdInfo::forTy(*this, params.types.at(1));
                    MIR_ASSERT(localMirRes, srcInfo.count == dstInfo.count, "Element counts must match for " << name);
                    of << "for(int i = 0; i < " << dstInfo.count << "; i++)";
                    of << "((";
                    dstInfo.emitValTy(*this);
                    of << "*)&";
                    emitLvalue(e.retVal);
                    of << ")[i] ";
                    of << "= (";
                    of << " ((";
                    srcInfo.emitValTy(*this);
                    of << "*)&";
                    emitParam(e.args.at(0));
                    of << ")[i]";
                    of << " " << op;
                    of << " ((";
                    srcInfo.emitValTy(*this);
                    of << "*)&";
                    emitParam(e.args.at(1));
                    of << ")[i]";
                    of << " ? -1 : 0)";
                };
                auto simdArith = [&](const char* op) {
                    auto info = SimdInfo::forTy(*this, params.types.at(0));
                    // Emulate!
                    emitLvalue(e.retVal);
                    of << " = ";
                    emitParam(e.args.at(0));
                    of << "; ";
                    of << "for(int i = 0; i < " << info.count << "; i++)";
                    of << "((";
                    info.emitValTy(*this);
                    of << "*)&";
                    emitLvalue(e.retVal);
                    of << ")[i] ";
                    of << op << "=";
                    of << " ((";
                    info.emitValTy(*this);
                    of << "*)&";
                    emitParam(e.args.at(1));
                    of << ")[i]";
                };
                auto simdCall = [&](const char* op) {
                    auto info = SimdInfo::forTy(*this, params.types.at(0));
                    // Emulate!
                    of << "for(int i = 0; i < " << info.count << "; i++)";
                    of << "((";
                    info.emitValTy(*this);
                    of << "*)&";
                    emitLvalue(e.retVal);
                    of << ")[i] ";
                    of << "= ";
                    of << "__builtin_";
                    of << op << "( ((";
                    info.emitValTy(*this);
                    of << "*)&";
                    emitParam(e.args.at(0));
                    of << ")[i] )";
                };

                // dst: T, index: usize, val: U
                // Insert a value at position
                if (nameStrip == "simd_insert") {
                    size_t sizeSlot = 0, sizeVal = 0;
                    TargetGetSizeOf(sp, mResolve, params.types.at(0), sizeSlot);
                    TargetGetSizeOf(sp, mResolve, params.types.at(1), sizeVal);
                    MIR_ASSERT(localMirRes, sizeSlot >= sizeVal, sizeSlot << " < " << sizeVal);
                    MIR_ASSERT(localMirRes, sizeSlot / sizeVal * sizeVal == sizeSlot, sizeSlot << " not a multiple of " << sizeVal);

                    // Emulate!
                    emitLvalue(e.retVal);
                    of << " = ";
                    emitParam(e.args.at(0));
                    of << "; ";
                    of << "(( ";
                    emitCtype(params.types.at(1));
                    of << "*)&";
                    emitLvalue(e.retVal);
                    of << ")[";
                    emitParam(e.args.at(1));
                    of << "] = ";
                    emitParam(e.args.at(2));
                } else if (nameStrip == "simd_extract") {
                    size_t sizeSlot = 0, sizeVal = 0;
                    TargetGetSizeOf(sp, mResolve, params.types.at(0), sizeSlot);
                    TargetGetSizeOf(sp, mResolve, params.types.at(1), sizeVal);
                    MIR_ASSERT(localMirRes, sizeSlot >= sizeVal, sizeSlot << " < " << sizeVal);
                    MIR_ASSERT(localMirRes, sizeSlot / sizeVal * sizeVal == sizeSlot, sizeSlot << " not a multiple of " << sizeVal);

                    // Emulate!
                    emitLvalue(e.retVal);
                    of << " = (( ";
                    emitCtype(params.types.at(1));
                    of << "*)&";
                    emitParam(e.args.at(0));
                    of << ")[";
                    emitParam(e.args.at(1));
                    of << "]";
                }
                // Truncate into a bitmask - Converts a collection of [0,!0] into bits
                else if (nameStrip == "simd_bitmask") {
                    auto srcInfo = SimdInfo::forTy(*this, params.types.at(0));
                    size_t sizeOut = 0;
                    TargetGetSizeOf(sp, mResolve, params.types.at(1), sizeOut);
                    of << "{ uint8_t* out = (uint8_t*)&(";
                    emitLvalue(e.retVal);
                    of << "); memset(out, 0, " << sizeOut << "); ";
                    for (size_t i = 0; i < srcInfo.count; i++) {
                        of << "out[" << (i / 8) << "] |= ((";
                        srcInfo.emitValTy(*this);
                        of << "*)&";
                        emitParam(e.args.at(0));
                        of << ")[" << i << "] == 0 ? 0 : (1 << " << (i % 8) << "); ";
                    }
                    of << "}";
                } else if (nameStrip == "simd_shuffle128" || nameStrip == "simd_shuffle64" || nameStrip == "simd_shuffle32" || nameStrip == "simd_shuffle16" || nameStrip == "simd_shuffle8" || nameStrip == "simd_shuffle4" || nameStrip == "simd_shuffle2") {
                    // Shuffle in 8 entries
                    size_t sizeSlot = 0;
                    TargetGetSizeOf(sp, mResolve, params.types.at(1), sizeSlot);
                    size_t div = nameStrip == "simd_shuffle128" ? 128 : nameStrip == "simd_shuffle64" ? 64 : nameStrip == "simd_shuffle32" ? 32 : nameStrip == "simd_shuffle16" ? 16 : nameStrip == "simd_shuffle8" ? 8 : nameStrip == "simd_shuffle4" ? 4 : nameStrip == "simd_shuffle2" ? 2 : throw "";
                    size_t sizeVal = sizeSlot / div;
                    MIR_ASSERT(localMirRes, sizeVal > 0, sizeSlot << " / " << div << " == 0?");
                    MIR_ASSERT(localMirRes, sizeSlot >= sizeVal, sizeSlot << " < " << sizeVal);
                    MIR_ASSERT(localMirRes, sizeSlot / sizeVal * sizeVal == sizeSlot, sizeSlot << " not a multiple of " << sizeVal);
                    // Indices address the concatenation of both input vectors, so the split
                    // point is the INPUT element count, not the index count.
                    size_t sizeIn = 0;
                    TargetGetSizeOf(sp, mResolve, params.types.at(0), sizeIn);
                    size_t nIn = sizeIn / sizeVal;
                    MIR_ASSERT(localMirRes, nIn > 0, "Zero-sized shuffle input");
                    of << "for(int i = 0; i < " << div << "; i++) { int j = ";
                    emitParam(e.args.at(2));
                    of << ".DATA[i];";
                    of << "((uint" << (sizeVal * 8) << "_t*)&";
                    emitLvalue(e.retVal);
                    of << ")[i]";
                    of << " = ((uint" << (sizeVal * 8) << "_t*)(j < " << nIn << " ? &";
                    emitParam(e.args.at(0));
                    of << " : &";
                    emitParam(e.args.at(1));
                    of << "))[j < " << nIn << " ? j : j - " << nIn << "];";
                    of << "}";
                } else if (nameStrip == "simd_shuffle") {
                    const auto& vecTy = params.types.at(0);
                    const auto& mapTy = params.types.at(1);
                    const auto& retTy = params.types.at(2);
                    size_t sizeVec = 0;
                    size_t sizeMap = 0;
                    size_t sizeRet = 0;
                    TargetGetSizeOf(sp, mResolve, vecTy, sizeVec);
                    TargetGetSizeOf(sp, mResolve, mapTy, sizeMap);
                    TargetGetSizeOf(sp, mResolve, retTy, sizeRet);
                    size_t div = sizeMap / 4; // map must be u32s
                    size_t sizeVal = sizeRet / div;
                    // Indices address the concatenation of both inputs; split on the input
                    // element count (an extract's map can be shorter than the vector).
                    size_t nIn = sizeVec / sizeVal;
                    MIR_ASSERT(localMirRes, nIn > 0, "Zero-sized shuffle input");
                    of << "for(int i = 0; i < " << div << "; i++) {";
                    of << " int j = ";
                    emitParam(e.args.at(2));
                    of << "._0";
                    of << ".DATA[i];";
                    of << " ((uint" << (sizeVal * 8) << "_t*)&";
                    emitLvalue(e.retVal);
                    of << ")[i]";
                    of << " = ((uint" << (sizeVal * 8) << "_t*)(j < " << nIn << " ? &";
                    emitParam(e.args.at(0));
                    of << " : &";
                    emitParam(e.args.at(1));
                    of << "))[j < " << nIn << " ? j : j - " << nIn << "];";
                    of << "}";
                } else if (nameStrip == "simd_cast") {
                    auto srcInfo = SimdInfo::forTy(*this, params.types.at(0));
                    auto dstInfo = SimdInfo::forTy(*this, params.types.at(1));
                    MIR_ASSERT(localMirRes, srcInfo.count == dstInfo.count, "Element counts must match for " << name);
                    of << "for(int i = 0; i < " << dstInfo.count << "; i++) ";
                    of << "((";
                    dstInfo.emitValTy(*this);
                    of << "*)&";
                    emitLvalue(e.retVal);
                    of << ")[i] ";
                    of << "= ((";
                    srcInfo.emitValTy(*this);
                    of << "*)&";
                    emitParam(e.args.at(0));
                    of << ")[i];";
                }
                // Select between two values
                else if (nameStrip == "simd_select") {
                    auto maskInfo = SimdInfo::forTy(*this, params.types.at(0));
                    auto valInfo = SimdInfo::forTy(*this, params.types.at(1));
                    MIR_ASSERT(localMirRes, maskInfo.count == valInfo.count, "Element counts must match for " << name);
                    of << "for(int i = 0; i < " << valInfo.count << "; i++) ";
                    of << "((";
                    valInfo.emitValTy(*this);
                    of << "*)&";
                    emitLvalue(e.retVal);
                    of << ")[i] ";
                    of << "= ((";
                    maskInfo.emitValTy(*this);
                    of << "*)&";
                    emitParam(e.args.at(0));
                    of << ")[i]";
                    of << "? ((";
                    valInfo.emitValTy(*this);
                    of << "*)&";
                    emitParam(e.args.at(1));
                    of << ")[i]";
                    of << ": ((";
                    valInfo.emitValTy(*this);
                    of << "*)&";
                    emitParam(e.args.at(2));
                    of << ")[i]";
                    of << ";";
                } else if (nameStrip == "simd_select_bitmask") {
                    auto valInfo = SimdInfo::forTy(*this, params.types.at(1));
                    of << "for(int i = 0; i < " << valInfo.count << "; i++) ";
                    of << "((";
                    valInfo.emitValTy(*this);
                    of << "*)&";
                    emitLvalue(e.retVal);
                    of << ")[i] ";
                    of << "= ((";
                    emitParam(e.args.at(0));
                    of << ") >> i) != 0";
                    of << "? ((";
                    valInfo.emitValTy(*this);
                    of << "*)&";
                    emitParam(e.args.at(1));
                    of << ")[i]";
                    of << ": ((";
                    valInfo.emitValTy(*this);
                    of << "*)&";
                    emitParam(e.args.at(2));
                    of << ")[i]";
                    of << ";";
                }
                // Comparisons
                else if (nameStrip == "simd_eq") {
                    simdCmp("==");
                } else if (nameStrip == "simd_ne") {
                    simdCmp("!=");
                } else if (nameStrip == "simd_lt") {
                    simdCmp("<");
                } else if (nameStrip == "simd_le") {
                    simdCmp("<=");
                } else if (nameStrip == "simd_gt") {
                    simdCmp(">");
                } else if (nameStrip == "simd_ge") {
                    simdCmp(">=");
                }
                // Arithmetic
                else if (nameStrip == "simd_add") {
                    simdArith("+");
                } else if (nameStrip == "simd_sub") {
                    simdArith("-");
                } else if (nameStrip == "simd_mul") {
                    simdArith("*");
                } else if (nameStrip == "simd_div") {
                    simdArith("/");
                } else if (nameStrip == "simd_and") {
                    simdArith("&");
                } else if (nameStrip == "simd_or") {
                    simdArith("|");
                } else if (nameStrip == "simd_xor") {
                    simdArith("^");
                } else if (nameStrip == "simd_xor") {
                    simdArith("^");
                } else if (nameStrip == "simd_shr") {
                    simdArith(">>");
                } else if (nameStrip == "simd_shl") {
                    simdArith("<<");
                }
                // platform:simd_reduce_and
                // platform:simd_reduce_max
                // platform:simd_reduce_min
                // platform:simd_reduce_mul_unordered
                // platform:simd_reduce_add_unordered
                // platform:simd_reduce_or
                // platform:simd_saturating_add
                // platform:simd_saturating_sub
                else if (nameStrip == "simd_ceil") {
                    simdCall("ceil");
                } else if (nameStrip == "simd_floor") {
                    simdCall("floor");
                } else if (nameStrip == "simd_fsqrt") {
                    simdCall("sqrt");
                }
                // platform:simd_fma
                else if (nameStrip == "simd_fma") {
                    auto info = SimdInfo::forTy(*this, params.types.at(0));
                    // Emulate!
                    of << "for(int i = 0; i < " << info.count << "; i++)";
                    of << "((";
                    info.emitValTy(*this);
                    of << "*)&";
                    emitLvalue(e.retVal);
                    of << ")[i] ";
                    of << "= ";
                    of << "__builtin_";
                    of << "fma(";
                    of << " ((";
                    info.emitValTy(*this);
                    of << "*)&";
                    emitParam(e.args.at(0));
                    of << ")[i],";
                    of << " ((";
                    info.emitValTy(*this);
                    of << "*)&";
                    emitParam(e.args.at(1));
                    of << ")[i],";
                    of << " ((";
                    info.emitValTy(*this);
                    of << "*)&";
                    emitParam(e.args.at(2));
                    of << ")[i]";
                    of << ")";
                }

                else {
                    // TODO: Platform intrinsics
                    of << "assert(!\"TODO: Platform intrinsic \\\"" << name << "\\\"\")";
                }
            } else {
                MIR_BUG(localMirRes, "Unknown intrinsic '" << name << "'");
            }
            of << ";\n";
        }

        void emitDestructorLoop(
            const ::MIR::LValue& slot,
            const ::HIR::TypeData* elementTy,
            ::std::function<void()> emitCount,
            unsigned indentLevel
        ) {
            auto indent = RepeatLitStr{"\t", static_cast<int>(indentLevel)};
            auto element = ::MIR::LValue::newIndex(slot.clone(), ::MIR::LValue::Storage::MAX_ARG);

            of << indent << "for(unsigned i = 0; i < ";
            emitCount();
            of << "; i++) {\n";
            of << indent << "\ttry {\n";
            emitDestructorCall(element, elementTy, false, indentLevel + 2);
            of << "\n" << indent << "\t} catch (...) {\n";
            of << indent << "\t\tfor(i++; i < ";
            emitCount();
            of << "; i++) {\n";
            of << indent << "\t\t\ttry {\n";
            emitDestructorCall(element, elementTy, false, indentLevel + 4);
            of << "\n" << indent << "\t\t\t} catch (...) { abort(); }\n";
            of << indent << "\t\t}\n";
            of << indent << "\t\tthrow;\n";
            of << indent << "\t}\n";
            of << indent << "}";
        }

        void emitTupleDestructor(
            const ::MIR::LValue& slot,
            const ::HIR::TypeData::Data_Tuple& tuple,
            bool unsizedValid,
            unsigned indentLevel
        ) {
            ::std::vector<::MIR::LValue> fields;
            ::std::vector<const ::HIR::TypeData*> fieldTypes;
            ::std::vector<bool> fieldUnsized;
            auto field = ::MIR::LValue::newField(slot.clone(), 0);
            for (size_t i = 0; i < tuple.size(); i++) {
                if (mResolve.typeNeedsDropGlue(sp, tuple[i])) {
                    fields.push_back(field.clone());
                    fieldTypes.push_back(tuple[i]);
                    fieldUnsized.push_back(unsizedValid && i == tuple.size() - 1);
                }
                field.incField();
            }
            if (fields.empty()) {
                return;
            }

            auto indent = RepeatLitStr{"\t", static_cast<int>(indentLevel)};
            of << indent << "{ unsigned mrustc_drop_progress = 0;\n";
            of << indent << "\ttry {\n";
            for (size_t i = 0; i < fields.size(); i++) {
                emitDestructorCall(fields[i], fieldTypes[i], fieldUnsized[i], indentLevel + 2);
                of << indent << "\t\tmrustc_drop_progress = " << i + 1 << ";\n";
            }
            of << indent << "\t} catch (...) {\n";
            for (size_t i = 1; i < fields.size(); i++) {
                of << indent << "\t\tif(mrustc_drop_progress < " << i << ") {\n";
                of << indent << "\t\t\ttry {\n";
                emitDestructorCall(fields[i], fieldTypes[i], fieldUnsized[i], indentLevel + 4);
                of << indent << "\t\t\t} catch (...) { abort(); }\n";
                of << indent << "\t\t}\n";
            }
            of << indent << "\t\tthrow;\n";
            of << indent << "\t}\n";
            of << indent << "}";
        }

        /// slot :: The value to drop
        /// ty :: Type of value to be dropped
        /// unsized_valid ::
        /// indent_level :: (formatting) Current amount of indenting
        void emitDestructorCall(const ::MIR::LValue& slot, const ::HIR::TypeData* ty, bool unsizedValid, unsigned indentLevel) {
            // If the type doesn't need dropping, don't try.
            if (!mResolve.typeNeedsDropGlue(sp, ty)) {
                return;
            }
            auto indent = RepeatLitStr{"\t", static_cast<int>(indentLevel)};
            TU_MATCH_HDRA( (*ty), {)
            // Impossible
            TU_ARMA(Diverge, te) {
                }
                TU_ARMA(Infer, te) {
                }
                TU_ARMA(ErasedType, te) {
                }
                TU_ARMA(NodeType, te) {
                }
                TU_ARMA(Generic, te) {
                }

                // Nothing
                TU_ARMA(Primitive, te) {
                }
                TU_ARMA(Pointer, te) {
                }
                TU_ARMA(NamedFunction, te) {
                }
                TU_ARMA(Function, te) {
                }
                // Has drop glue/destructors
                TU_ARMA(Borrow, te) {
                    if (te.type == ::HIR::BorrowType::Owned) {
                        // Call drop glue on inner.
                        emitDestructorCall(::MIR::LValue::newDeref(slot.clone()), te.inner, true, indentLevel);
                    }
                }
                TU_ARMA(Path, te) {
                    // Call drop glue
                    // - TODO: If the destructor is known to do nothing, don't call it.
                    auto p = ::HIR::Path(ty, "#drop_glue");
                    const char* makeFcn = nullptr;
                    switch (metadataType(ty)) {
                        case MetadataType::Unknown:
                            MIR_BUG(*mirRes, ty << " unknown metadata");
                        case MetadataType::None:
                        case MetadataType::Zero:
                            if (this->typeIsBadZst(ty) && this->lvalueRootIsBadZst(slot)) {
                                // The C backend omits zero-sized locals, but Rust still
                                // runs Drop for every logical ZST value.  Give Drop an
                                // address with the ZST's own alignment instead of naming
                                // an elided local (which can be behind Field/Index/etc.).
                                of << indent << "{ ";
                                emitCtype(ty);
                                of << " mrustc_zst{}; " << TransMangle(p) << "(&mrustc_zst); }\n";
                            } else if (this->typeIsBadZst(ty) && ::MIR::LValue::CRef(slot).is_Index()) {
                                of << indent << TransMangle(p) << "((";
                                emitCtype(ty);
                                of << "*)";
                                emitBorrow(*mirRes, ::HIR::BorrowType::Unique, slot);
                                of << ");\n";
                            } else if (this->typeIsBadZst(ty) && (slot.is_Field() || slot.is_Downcast())) {
                                // May need to back the slot out too, as we might be dropping a ZST tuple
                                auto v = ::MIR::LValue::CRef(slot).innerRef();
                                ::HIR::TypeRef tmp;
                                if (this->typeIsBadZst(mirRes->getLvalueType(tmp, v)) && (v.is_Field() || v.is_Downcast())) {
                                    v = v.innerRef();
                                }
                                of << indent << TransMangle(p) << "((";
                                emitCtype(ty);
                                of << "*)&";
                                emitLvalue(v);
                                of << ");\n";
                            } else if (this->typeIsBadZst(ty) && slot.wrappers.empty()) {
                                of << indent << TransMangle(p) << "((";
                                emitCtype(ty);
                                of << "*)&rv);\n";
                            } else {
                                of << indent << TransMangle(p) << "(&";
                                emitLvalue(slot);
                                of << ");\n";
                            }
                            break;
                        case MetadataType::Slice:
                            makeFcn = "make_sliceptr";
                            if (0) {
                                case MetadataType::TraitObject:
                                    makeFcn = "make_traitobjptr";
                            }
                            of << indent << TransMangle(p) << "( " << makeFcn << "(";
                            if (slot.is_Deref()) {
                                emitLvalue(::MIR::LValue::CRef(slot).innerRef());
                                of << ".PTR";
                            } else {
                                of << "&";
                                emitLvalue(slot);
                            }
                            of << ", ";
                            auto lvr = ::MIR::LValue::CRef(slot);
                            while (lvr.is_Field()) {
                                lvr.tryUnwrap();
                            }
                            MIR_ASSERT(*mirRes, lvr.is_Deref(), "Access to unized type without a deref - " << lvr << " (part of " << slot << ")");
                            emitLvalue(lvr.innerRef());
                            of << ".META";
                            of << ") );\n";
                            break;
                    }
                }
                TU_ARMA(Array, te) {
                    // Emit destructors for all entries
                    if (te.size.as_Known() > 0) {
                        emitDestructorLoop(slot, te.inner, [&] { of << te.size.as_Known(); }, indentLevel);
                    }
                }
                TU_ARMA(Tuple, te) {
                    emitTupleDestructor(slot, te, unsizedValid, indentLevel);
                }
                TU_ARMA(TraitObject, te) {
                    MIR_ASSERT(*mirRes, unsizedValid, "Dropping TraitObject without an owned pointer");
                    // Call destructor in vtable
                    auto lvr = ::MIR::LValue::CRef(slot);
                    while (lvr.is_Field()) {
                        lvr.tryUnwrap();
                    }
                    MIR_ASSERT(*mirRes, lvr.is_Deref(), "Access to unized type without a deref - " << lvr << " (part of " << slot << ")");
                    of << indent << "((VTABLE_HDR*)";
                    emitLvalue(lvr.innerRef());
                    of << ".META)->drop(";
                    if (slot.is_Deref()) {
                        emitLvalue(::MIR::LValue::CRef(slot).innerRef());
                        of << ".PTR";
                    } else {
                        of << "&";
                        emitLvalue(slot);
                    }
                    of << ");";
                }
                TU_ARMA(Slice, te) {
                    MIR_ASSERT(*mirRes, unsizedValid, "Dropping Slice without an owned pointer");
                    auto lvr = ::MIR::LValue::CRef(slot);
                    while (lvr.is_Field()) {
                        lvr.tryUnwrap();
                    }
                    MIR_ASSERT(*mirRes, lvr.is_Deref(), "Access to unized type without a deref - " << lvr << " (part of " << slot << ")");
                    // If one element destructor unwinds, Rust still drops the
                    // unvisited tail.  A second exception during that cleanup
                    // is a double panic and must terminate.
                    emitDestructorLoop(slot, te.inner, [&] {
                        emitLvalue(lvr.innerRef());
                        of << ".META";
                    }, indentLevel);
                }
            }
        }

        void emitEnumVariantVal(const TypeRepr* repr, unsigned idx) {
            const auto& ve = repr->variants.as_Values();
            const auto& tagTy = TargetGetInnerType(sp, mResolve, *repr, ve.field.index, ve.field.subFields);
            switch (tagTy->as_Primitive()) {
                case ::HIR::CoreType::I8:
                case ::HIR::CoreType::I16:
                case ::HIR::CoreType::I32:
                case ::HIR::CoreType::I64:
                case ::HIR::CoreType::Isize:
                    of << S128(ve.values[idx]).truncateI64() << "ll";
                    break;
                case ::HIR::CoreType::Bool:
                case ::HIR::CoreType::U8:
                case ::HIR::CoreType::U16:
                case ::HIR::CoreType::U32:
                case ::HIR::CoreType::U64:
                case ::HIR::CoreType::Usize:
                case ::HIR::CoreType::Char:
                    of << ve.values[idx].truncateU64() << "ull";
                    break;
                case ::HIR::CoreType::I128:
                    if (options.emulatedI128) {
                        of << "make128s_raw(" << ve.values[idx].getHi() << "ull, " << ve.values[idx].getLo() << "ull)";
                    } else {
                        of << "((int128_t)(((uint128_t)" << ve.values[idx].getHi() << "ull << 64) | (uint128_t)" << ve.values[idx].getLo() << "ull))";
                    }
                    break;
                case ::HIR::CoreType::U128:
                    if (options.emulatedI128) {
                        of << "make128_raw(" << ve.values[idx].getHi() << "ull, " << ve.values[idx].getLo() << "ull)";
                    } else {
                        of << "(((uint128_t)" << ve.values[idx].getHi() << "ull << 64) | (uint128_t)" << ve.values[idx].getLo() << "ull)";
                    }
                    break;
                case ::HIR::CoreType::F16:
                case ::HIR::CoreType::F32:
                case ::HIR::CoreType::F64:
                case ::HIR::CoreType::F128:
                    MIR_TODO(*mirRes, "Floating point enum tag.");
                    break;
                case ::HIR::CoreType::Str:
                    MIR_BUG(*mirRes, "Unsized tag?!");
            }
        }

        // returns whether a literal can be represented as zeroed memory.
        bool isZeroLiteral(const ::HIR::TypeData* ty, const EncodedLiteral& lit, const TransParams& params) {
            for (auto v : lit.bytes) {
                if (v) {
                    return false;
                }
            }
            if (!lit.relocations.empty()) {
                return false;
            }
            return true;
        }

        void emitLvalue(const ::MIR::LValue::CRef& val) {
            TU_MATCH_HDRA( (val), {)
            TU_ARMA(Return, _e) {
                    of << "rv";
                }
                TU_ARMA(Argument, e) {
                    of << "arg" << e;
                }
                TU_ARMA(Local, e) {
                    if (e == ::MIR::LValue::Storage::MAX_ARG) {
                        of << "i";
                    } else {
                        of << "var" << e;
                    }
                }
                TU_ARMA(Static, e) {
                    of << TransMangle(e);
                    of << ".val";
                }
                TU_ARMA(Field, fieldIndex) {
                    ::HIR::TypeRef tmp;
                    auto inner = val.innerRef();
                    const auto& ty = mirRes->getLvalueType(tmp, inner);
                    if (ty->is_Slice()) {
                        if (inner.is_Deref()) {
                            of << "((";
                            emitCtype(ty->as_Slice().inner);
                            of << "*)";
                            emitLvalue(inner.innerRef());
                            of << ".PTR)";
                        } else {
                            emitLvalue(inner);
                        }
                        of << "[" << fieldIndex << "]";
                    } else if (ty->is_Array()) {
                        emitLvalue(inner);
                        of << ".DATA[" << fieldIndex << "]";
                    } else if (inner.is_Deref()) {
                        auto dstType = metadataType(ty);
                        if (dstType != MetadataType::None) {
                            of << "((";
                            emitCtype(ty);
                            of << "*)";
                            emitLvalue(inner.innerRef());
                            of << ".PTR)->_" << fieldIndex;
                        } else {
                            emitLvalue(inner.innerRef());
                            of << "->_" << fieldIndex;
                        }
                    } else {
                        emitLvalue(inner);
                        of << "._" << fieldIndex;
                    }
                }
                TU_ARMA(Deref, _e) {
                    auto inner = val.innerRef();
                    ::HIR::TypeRef tmp;
                    const auto& ty = mirRes->getLvalueType(tmp, val);
                    auto dstType = metadataType(ty);
                    // If the type is unsized, then this pointer is a fat pointer, so we need to cast the data pointer.
                    if (dstType != MetadataType::None) {
                        of << "(*(";
                        emitCtype(ty);
                        of << "*)";
                        emitLvalue(inner);
                        of << ".PTR)";
                    } else {
                        of << "(*";
                        emitLvalue(inner);
                        of << ")";
                    }
                }
                TU_ARMA(Index, indexLocal) {
                    auto inner = val.innerRef();
                    ::HIR::TypeRef tmp;
                    const auto& ty = mirRes->getLvalueType(tmp, inner);
                    of << "(";
                    if (ty->is_Slice()) {
                        if (inner.is_Deref()) {
                            of << "(";
                            emitCtype(ty->as_Slice().inner);
                            of << "*)";
                            emitLvalue(inner.innerRef());
                            of << ".PTR";
                        } else {
                            emitLvalue(inner);
                        }
                    } else if (ty->is_Array()) {
                        emitLvalue(inner);
                        of << ".DATA";
                    } else {
                        emitLvalue(inner);
                    }
                    of << ")[";
                    emitLvalue(::MIR::LValue::newLocal(indexLocal));
                    of << "]";
                }
                TU_ARMA(Downcast, variantIndex) {
                    auto inner = val.innerRef();
                    ::HIR::TypeRef tmp;
                    const auto& ty = mirRes->getLvalueType(tmp, inner);
                    emitLvalue(inner);
                    MIR_ASSERT(*mirRes, ty->is_Path(), "Downcast on non-Path type - " << ty);
                    if (ty->as_Path().binding.is_Enum()) {
                        of << ".DATA";
                    }
                    of << ".var_" << variantIndex;
                }
            }
        }

        void emitLvalue(const ::MIR::LValue& val) {
            emitLvalue(::MIR::LValue::CRef(val));
        }

        void emitConstant(const ::MIR::Constant& ve, const ::MIR::LValue* dstPtr = nullptr) {
            TU_MATCH_HDRA( (ve), {)
            TU_ARMA(Int, c) {
                    switch (c.t) {
                        // TODO: These should already have been truncated/reinterpreted, but just in case.
                        case ::HIR::CoreType::I8:
                            of << static_cast<int>(static_cast<int8_t>(c.v.truncateI64())); // cast to int, because `int8_t` is printed as a `char`
                            break;
                        case ::HIR::CoreType::I16:
                            of << static_cast<int16_t>(c.v.truncateI64());
                            break;
                        case ::HIR::CoreType::I32:
                            of << static_cast<int32_t>(c.v.truncateI64());
                            break;
                        case ::HIR::CoreType::I64:
                        case ::HIR::CoreType::Isize:
                            if (c.v.truncateI64() == INT64_MIN) {
                                of << "INT64_MIN";
                            } else if (c.v.truncateI64() == INT64_MAX) {
                                of << "INT64_MAX";
                            } else {
                                of << c.v.truncateI64();
                                of << "ll";
                            }
                            break;
                        case ::HIR::CoreType::I128:
                            if (options.emulatedI128) {
                                of << "make128s_raw(" << c.v.getInner().getHi() << "ull, " << c.v.getInner().getLo() << "ull)";
                            } else if (c.v.isI64() && c.v.truncateI64() != INT64_MIN) {
                                of << "(int128_t)";
                                of << c.v;
                                of << "ll";
                            } else {
                                of << "(int128_t)( ((uint128_t)" << c.v.getInner().getHi() << "ull << 64) | (uint128_t)" << c.v.getInner().getLo() << "ull)";
                            }
                            break;
                        default:
                            of << c.v;
                            break;
                    }
                }
                TU_ARMA(Uint, c) {
                    switch (c.t) {
                        case ::HIR::CoreType::U8:
                            of << ::std::hex << "0x" << (c.v.truncateU64() & 0xFF) << ::std::dec;
                            break;
                        case ::HIR::CoreType::U16:
                            of << ::std::hex << "0x" << (c.v.truncateU64() & 0xFFFF) << ::std::dec;
                            break;
                        case ::HIR::CoreType::U32:
                            of << ::std::hex << "0x" << (c.v.truncateU64() & 0xFFFFFFFF) << ::std::dec;
                            break;
                        case ::HIR::CoreType::U64:
                        case ::HIR::CoreType::Usize:
                            of << ::std::hex << "0x" << c.v.truncateU64() << "ull" << ::std::dec;
                            break;
                        case ::HIR::CoreType::U128:
                            if (options.emulatedI128) {
                                of << "make128_raw(" << c.v.getHi() << "ull, " << c.v.getLo() << "ull)";
                            } else if (c.v.isU64()) {
                                of << "(uint128_t)";
                                of << ::std::hex << "0x" << c.v << "ull" << ::std::dec;
                            } else {
                                of << std::hex << "( ((uint128_t)0x" << c.v.getHi() << "ull << 64) | (uint128_t)0x" << c.v.getLo() << "ull)" << std::dec;
                            }
                            break;
                        case ::HIR::CoreType::Char:
                            assert(c.v <= 0x10FFFF);
                            if (c.v < 256) {
                                of << c.v;
                            } else {
                                of << ::std::hex << "0x" << c.v << ::std::dec;
                            }
                            break;
                        default:
                            MIR_BUG(*mirRes, "Invalid type for UInt literal - " << c.t);
                    }
                }
                TU_ARMA(Float, c) {
                    this->emitFloat(c.v, c.t);
                }
                TU_ARMA(Bool, c) {
                    of << (c.v ? "true" : "false");
                }
                TU_ARMA(Bytes, c) {
                    // Array borrow : Cast the C string to the array
                    // - Laziness
                    of << "(void*)";
                    this->printEscapedString(c);
                }
                TU_ARMA(StaticString, c) {
                    of << "make_sliceptr(";
                    this->printEscapedString(c);
                    of << ", " << ::std::dec << c.size() << ")";
                }
                TU_ARMA(Const, c) {
                    MIR_BUG(*mirRes, "Unexpected Constant::Const - " << ve);
                }
                TU_ARMA(Generic, c) {
                    MIR_BUG(*mirRes, "Generic value present at codegen");
                }
                TU_ARMA(Function, c) {
                    MIR_TODO(*mirRes, "Constant::Function");
                }
                TU_ARMA(ItemAddr, c) {
                    const bool hasOffset = c.offset != U128(0);
                    if (hasOffset) {
                        MIR_ASSERT(*mirRes, c.offset.isU64(), "Item address offset is too large: " << c.offset);
                        of << "((void*)((uint8_t*)";
                    }
                    if (c->mData.is_UfcsInherent() && c->mData.as_UfcsInherent().item == "#type_id") {
                        of << "(void*)&__typeid_" << TransMangle(c->mData.as_UfcsInherent().type);
                    } else {
                        bool isFcn = false;
                        MonomorphState msTmp(crate.types);
                        auto v = mResolve.getValue(sp, *c, msTmp, /*signature_only=*/true);
                        isFcn = v.is_Function() || v.is_EnumConstructor() || v.is_StructConstructor();
                        MIR_ASSERT(*mirRes, !isFcn || !hasOffset, "Function address has a non-zero offset: " << c.offset);
                        if (!isFcn) {
                            of << "&";
                        }
                        of << TransMangle(*c);
                        if (!isFcn) {
                            of << ".val";
                        }
                    }
                    if (hasOffset) {
                        of << " + 0x" << ::std::hex << c.offset.truncateU64() << ::std::dec << "))";
                    }
                }
            }
        }

        void emitParam(const ::MIR::Param& p, bool typeBytes = true) {
            TU_MATCH_HDRA( (p), {)
            TU_ARMA(LValue, e) {
                    emitLvalue(e);
                }
                TU_ARMA(Borrow, e) {
                    emitBorrow(*mirRes, e.type, e.val);
                }
                TU_ARMA(Constant, e) {
                    if (typeBytes && e.is_Bytes()) {
                        ::HIR::TypeRef tmp;
                        of << "static_cast<";
                        emitCtype(mirRes->getParamType(tmp, p));
                        of << ">(";
                        emitConstant(e);
                        of << ")";
                    } else {
                        emitConstant(e);
                    }
                }
            }
        }

        void emitTraitMetadataParam(const ::MIR::TypeResolve& localMirRes, const ::MIR::Param& param) {
            ::HIR::TypeRef tmp;
            const auto& ty = localMirRes.getParamType(tmp, param);
            emitParam(param);
            if (const auto* te = ty->opt_Path()) {
                if (te->path.mData.is_Generic() && te->path.mData.as_Generic().mPath == mResolve.mLangDynMetadata) {
                    of << "._0._0";
                }
            }
        }

        void emitCtype(const ::HIR::TypeData* ty) {
            emitCtype(ty, FMT_CB(_, ));
        }

        void emitCtype(const ::HIR::TypeData* ty, ::FmtLambda inner, bool isExternC = false) {
            TU_MATCH_HDRA( (*ty), {)
            TU_ARMA(Infer, te) {
                    of << "@" << ty << "@" << inner;
                }
                TU_ARMA(Diverge, te) {
                    of << "tBANG " << inner;
                }
                TU_ARMA(Primitive, te) {
                    switch (te) {
                        case ::HIR::CoreType::Usize:
                            of << "uintptr_t";
                            break;
                        case ::HIR::CoreType::Isize:
                            of << "intptr_t";
                            break;
                        case ::HIR::CoreType::U8:
                            of << "uint8_t";
                            break;
                        case ::HIR::CoreType::I8:
                            of << "int8_t";
                            break;
                        case ::HIR::CoreType::U16:
                            of << "uint16_t";
                            break;
                        case ::HIR::CoreType::I16:
                            of << "int16_t";
                            break;
                        case ::HIR::CoreType::U32:
                            of << "uint32_t";
                            break;
                        case ::HIR::CoreType::I32:
                            of << "int32_t";
                            break;
                        case ::HIR::CoreType::U64:
                            of << "uint64_t";
                            break;
                        case ::HIR::CoreType::I64:
                            of << "int64_t";
                            break;
                        case ::HIR::CoreType::U128:
                            of << "uint128_t";
                            break;
                        case ::HIR::CoreType::I128:
                            of << "int128_t";
                            break;

                        case ::HIR::CoreType::F16:
                            of << "f16";
                            break;
                        case ::HIR::CoreType::F32:
                            of << "float";
                            break;
                        case ::HIR::CoreType::F64:
                            of << "double";
                            break;
                        case ::HIR::CoreType::F128:
                            of << "f128";
                            break;

                        case ::HIR::CoreType::Bool:
                            of << "RUST_BOOL";
                            break;
                        case ::HIR::CoreType::Char:
                            of << "RUST_CHAR";
                            break;
                        case ::HIR::CoreType::Str:
                            MIR_BUG(*mirRes, "Raw str");
                    }
                    of << " " << inner;
                }
                TU_ARMA(Path, te) {
                    //if( const auto* ity = m_resolve.is_type_owned_box(ty) ) {
                    //    emit_ctype_ptr(*ity, inner);
                    //    return ;
                    //}
                TU_MATCH_HDRA( (te.binding), { )
                TU_ARMA(Struct, tpb) {
                            of << "struct s_" << TransMangle(te.path);
                        }
                        TU_ARMA(Union, tpb) {
                            of << "union u_" << TransMangle(te.path);
                        }
                        TU_ARMA(Enum, tpb) {
                            of << "struct e_" << TransMangle(te.path);
                        }
                        TU_ARMA(ExternType, tpb) {
                            of << "struct x_" << TransMangle(te.path);
                        }
                        TU_ARMA(Unbound, tpb) {
                            MIR_BUG(*mirRes, "Unbound type path in trans - " << ty);
                        }
                        TU_ARMA(Opaque, tpb) {
                            MIR_BUG(*mirRes, "Opaque path in trans - " << ty);
                        }
                }
                of << " " << inner;
                }
                TU_ARMA(Generic, te) {
                    MIR_BUG(*mirRes, "Generic in trans - " << ty);
                }
                TU_ARMA(TraitObject, te) {
                    MIR_BUG(*mirRes, "Raw trait object - " << ty);
                }
                TU_ARMA(ErasedType, te) {
                    MIR_BUG(*mirRes, "ErasedType in trans - " << ty);
                }
                TU_ARMA(Array, te) {
                    of << "t_" << TransMangle(ty) << " " << inner;
                    //emit_ctype(te.inner, inner);
                    //m_of << "[" << te.size.as_Known() << "]";
                }
                TU_ARMA(Slice, te) {
                    MIR_BUG(*mirRes, "Raw slice object - " << ty);
                }
                TU_ARMA(Tuple, te) {
                    if (te.size() == 0) {
                        of << "tUNIT";
                    } else {
                        of << "TUP_" << te.size();
                        for (const auto& t : te) {
                            of << "_" << TransMangle(t);
                        }
                    }
                    of << " " << inner;
                }
                TU_ARMA(Borrow, te) {
                    emitCtypePtr(te.inner, inner);
                }
                TU_ARMA(Pointer, te) {
                    emitCtypePtr(te.inner, inner);
                }
                TU_ARMA(NamedFunction, te) {
                    of << "t_" << TransMangle(ty) << " " << inner;
                }
                TU_ARMA(Function, te) {
                    of << "t_" << TransMangle(ty) << " " << inner;
                }
                break;
                case ::HIR::TypeData::TAG_NodeType:
                    MIR_BUG(*mirRes, "NodeType during trans - " << ty);
                    break;
            }
        }

        ::HIR::TypeRef getInnerUnsizedType(const ::HIR::TypeData* ty) {
            if (ty == ::HIR::CoreType::Str || ty->is_Slice()) {
                return ty;
            } else if (ty->is_TraitObject()) {
                return ty;
            } else if (ty->is_Path()) {
                TU_MATCH_HDRA( (ty->as_Path().binding), {)
                default:
                    MIR_BUG(*mirRes, "Unbound/opaque path in trans - " << ty);
                    throw "";
                    TU_ARMA(Struct, tpb) {
                        switch (tpb->structMarkings.dstType) {
                            case ::HIR::StructMarkings::DstType::None:
                                return ::HIR::TypeRef();
                            case ::HIR::StructMarkings::DstType::Slice:
                            case ::HIR::StructMarkings::DstType::TraitObject:
                            case ::HIR::StructMarkings::DstType::Possible: {
                                // TODO: How to figure out? Lazy way is to check the monomorpised type of the last field (structs only)
                                const auto& path = ty->as_Path().path.mData.as_Generic();
                                const auto& str = *ty->as_Path().binding.as_Struct();
                                auto monomorph = [&](const auto& tpl) {
                                    return mResolve.monomorphExpand(sp, tpl, MonomorphStatePtr(crate.types, nullptr, &path.mParams, nullptr));
                                };
                        TU_MATCH_HDRA( (str.mData), { )
                        TU_ARMA(Unit, se) MIR_BUG(*mirRes, "Unit-like struct with DstType::Possible");
                                    TU_ARMA(Tuple, se) return getInnerUnsizedType(monomorph(se.back().ent));
                                    TU_ARMA(Named, se) return getInnerUnsizedType(monomorph(se.back().ty));
                        }
                        throw "";
                            }
                        }
                    }
                    TU_ARMA(Union, tpb) {
                        return ::HIR::TypeRef();
                    }
                    TU_ARMA(Enum, tpb) {
                        return ::HIR::TypeRef();
                    }
                }
                throw "";
            } else {
                return ::HIR::TypeRef();
            }
        }

        unsigned getPackingMaxAlign(const ::HIR::TypeData* ty) const {
            if (ty->is_Path() && ty->as_Path().binding.is_Struct()) {
                return ty->as_Path().binding.as_Struct()->maxFieldAlignment;
            }
            return 0;
        }

        void emitTraitObjectVtableSize(const ::MIR::Param& value) {
            of << "((VTABLE_HDR*)";
            emitParam(value);
            of << ".META)->size";
        }

        void emitTraitObjectVtableAlign(const ::MIR::Param& value) {
            of << "((VTABLE_HDR*)";
            emitParam(value);
            of << ".META)->align";
        }

        void emitTraitObjectDstTailAlign(const ::HIR::TypeData* outerTy, const ::HIR::TypeData* tailTy, const ::MIR::Param& value) {
            const auto maxAlign = getPackingMaxAlign(outerTy);
            if (maxAlign != 0) {
                of << "mrustc_min(";
            }
            emitTraitObjectDstAlign(tailTy, value);
            if (maxAlign != 0) {
                of << ", " << maxAlign << ")";
            }
        }

        void emitTraitObjectDstAlign(const ::HIR::TypeData* ty, const ::MIR::Param& value) {
            if (ty->is_TraitObject()) {
                emitTraitObjectVtableAlign(value);
                return;
            }

            const auto* repr = TargetGetTypeRepr(sp, mResolve, ty);
            MIR_ASSERT(*mirRes, repr && repr->size == SIZE_MAX && !repr->fields.empty(), "Expected a DST wrapper - " << ty);
            of << "mrustc_max(" << repr->align << ", ";
            emitTraitObjectDstTailAlign(ty, repr->fields.back().ty, value);
            of << ")";
        }

        void emitTraitObjectDstSize(const ::HIR::TypeData* ty, const ::MIR::Param& value) {
            if (ty->is_TraitObject()) {
                emitTraitObjectVtableSize(value);
                return;
            }

            const auto* repr = TargetGetTypeRepr(sp, mResolve, ty);
            MIR_ASSERT(*mirRes, repr && repr->size == SIZE_MAX && !repr->fields.empty(), "Expected a DST wrapper - " << ty);
            const auto& tail = repr->fields.back();
            of << "ALIGN_TO(ALIGN_TO(" << tail.offset << ", ";
            emitTraitObjectDstTailAlign(ty, tail.ty, value);
            of << ") + ";
            emitTraitObjectDstSize(tail.ty, value);
            of << ", ";
            emitTraitObjectDstAlign(ty, value);
            of << ")";
        }

        void emitTraitObjectDstFieldOffset(const ::HIR::TypeData* ty, size_t fieldIdx, const ::MIR::Param& value) {
            const auto* repr = TargetGetTypeRepr(sp, mResolve, ty);
            MIR_ASSERT(*mirRes, repr && fieldIdx < repr->fields.size(), "Invalid DST field " << fieldIdx << " on " << ty);
            const auto& field = repr->fields[fieldIdx];
            auto innerTy = getInnerUnsizedType(field.ty);
            MIR_ASSERT(*mirRes, fieldIdx + 1 == repr->fields.size() && innerTy->is_TraitObject(), "Expected final trait object field on " << ty);
            of << "ALIGN_TO(" << field.offset << ", ";
            emitTraitObjectDstTailAlign(ty, field.ty, value);
            of << ")";
        }

        MetadataType metadataType(const ::HIR::TypeData* ty) const {
            return mResolve.metadataType(mirRes ? mirRes->sp : sp, ty);
        }

        void emitCtypePtr(const ::HIR::TypeData* innerTy, ::FmtLambda inner) {
            //if( inner_ty->is_Array() ) {
            //    emit_ctype(inner_ty, FMT_CB(ss, ss << "(*" << inner << ")";));
            //}
            //else
            {
                switch (this->metadataType(innerTy)) {
                    case MetadataType::Unknown:
                        BUG(sp, innerTy << " unknown metadata type");
                    case MetadataType::None:
                    case MetadataType::Zero:
                        emitCtype(innerTy, FMT_CB(ss, ss << "*" << inner;));
                        break;
                    case MetadataType::Slice:
                        of << "SLICE_PTR " << inner;
                        break;
                    case MetadataType::TraitObject:
                        of << "TRAITOBJ_PTR " << inner;
                        break;
                }
            }
        }

        bool isDst(const ::HIR::TypeData* ty) const {
            switch (this->metadataType(ty)) {
                case MetadataType::Unknown:
                    BUG(sp, ty << " unknown metadata type");
                case MetadataType::None:
                case MetadataType::Zero:
                    return false;
                case MetadataType::Slice:
                case MetadataType::TraitObject:
                    return true;
            }
            return false;
        }
    };

    Span CodeGeneratorC::sp;
}

::std::unique_ptr<CodeGenerator> TransCodegenGetGeneratorC(const ::HIR::Crate& crate, const ::std::string& outfile) {
    return ::std::unique_ptr<CodeGenerator>(new CodeGeneratorC(crate, outfile));
}
