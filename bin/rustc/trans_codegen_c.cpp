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
        bool escape_percent;

        FmtGccAsm(const ::std::string& s, bool escape_percent)
            : s(s)
            , escape_percent(escape_percent)
        {
        }
    };

    class StringList {
        ::std::vector<::std::string> m_cached;
        ::std::vector<const char*> m_strings;

    public:
        StringList() {
        }

        StringList(const StringList&) = delete;
        StringList(StringList&&) = default;

        const ::std::vector<const char*>& get_vec() const {
            return m_strings;
        }

        std::vector<const char*>::const_iterator begin() const {
            return m_strings.begin();
        }

        std::vector<const char*>::const_iterator end() const {
            return m_strings.end();
        }

        void push_back(::std::string s) {
            // If the cache list is about to move, update the pointers
            if (m_cached.capacity() == m_cached.size()) {
                // Make a bitmap of entries in `m_strings` that are pointers into `m_cached`
                ::std::vector<bool> b;
                b.reserve(m_strings.size());
                size_t j = 0;
                for (const auto* s : m_strings) {
                    if (j == m_cached.size()) {
                        break;
                    }
                    if (s == m_cached[j].c_str()) {
                        j++;
                        b.push_back(true);
                    } else {
                        b.push_back(false);
                    }
                }

                // Add the new one
                m_cached.push_back(::std::move(s));
                // Update pointers
                j = 0;
                for (size_t i = 0; i < b.size(); i++) {
                    if (b[i]) {
                        m_strings[i] = m_cached.at(j++).c_str();
                    }
                }
            } else {
                m_cached.push_back(::std::move(s));
            }
            m_strings.push_back(m_cached.back().c_str());
        }

        void push_back(const char* s) {
            m_strings.push_back(s);
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
    bool in_comment = false;
    for (const char& ch : x.s) {
        if (ch == '/' && (&ch)[1] == '/') {
            if (!in_comment) {
                os << "\" ";
            }
            in_comment = true;
        } else {
            in_comment = false;
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
                if (x.escape_percent) {
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

        const ::HIR::Crate& m_crate;
        ::StaticTraitResolve m_resolve;

        ::std::string m_outfile_path;
        ::std::string m_outfile_path_c;

        ::std::ofstream m_of;
        const ::MIR::TypeResolve* m_mir_res = nullptr;

        struct {
            bool emulated_i128 = false;
            bool disallow_empty_structs = false;
        } m_options;

        ::std::set<::HIR::TypeRef> m_emitted_fn_types;
        ::std::set<const TypeRepr*> m_embedded_tags;

    public:
        CodeGeneratorC(const ::HIR::Crate& crate, const ::std::string& outfile)
            : m_crate(crate)
            , m_resolve(crate)
            , m_outfile_path(outfile)
            , m_outfile_path_c(outfile + ".cpp")
            , m_of(m_outfile_path_c)
        {
            ASSERT_BUG(Span(), m_of.is_open(), "Failed to open `" << m_outfile_path_c << "` for writing");
            m_options.emulated_i128 = TargetGetCurSpec().m_backend_c.m_emulated_i128;
            if (TargetGetCurSpec().m_arch.m_pointer_bits < 64 && !m_options.emulated_i128) {
                WARNING(Span(), W0000, "Potentially misconfigured target, 32-bit targets require i128 emulation");
            }
            m_options.disallow_empty_structs = true;

            m_of << "/*\n"
                 << " * AUTOGENERATED by mrustc\n"
                 << " */\n"
                 << "#include <stddef.h>\n"
                 << "#include <stdint.h>\n"
                 << "#include <stdbool.h>\n"
                 << "#include <stdarg.h>\n"
                 << "#include <assert.h>\n"
                 << "#include <stdlib.h>\n"    // abort
                 << "#include <string.h>\n";   // mem*
            m_of << "typedef uint32_t RUST_CHAR;\n"
                 << "typedef uint8_t RUST_BOOL;\n"
                 << "typedef struct { void* PTR; size_t META; } SLICE_PTR;\n"
                 << "typedef struct { void* PTR; void* META; } TRAITOBJ_PTR;\n"
                 << "typedef struct { void (*drop)(void*); size_t size; size_t align; } VTABLE_HDR;\n";
            m_of << "struct mrustc_panic final { void* rust_exception; };\n";
            if (m_options.disallow_empty_structs) {
                m_of << "typedef struct { char _d; } tUNIT;\n"
                     << "typedef char tBANG;\n"
                     << "typedef struct { char _d; } tTYPEID;\n";
            } else {
                m_of << "typedef struct { } tUNIT;\n"
                     << "typedef struct { } tBANG;\n"
                     << "typedef struct { } tTYPEID;\n";
            }
            m_of << "static inline size_t ALIGN_TO(size_t s, size_t a) { return (s + a-1) / a * a; }\n"
                 << "\n"
                 << "#define ALIGNOF(t) __alignof__(t)\n";
            // 64-bit bit ops (gcc intrinsics)
            m_of << "static inline uint64_t __builtin_clz64(uint64_t v) {\n"
                         << "\treturn ( (v >> 32) != 0 ? __builtin_clz(v>>32) : 32 + __builtin_clz(v));\n"
                         << "}\n"
                         << "static inline uint64_t __builtin_ctz64(uint64_t v) {\n"
                         << "\treturn ((v&0xFFFFFFFF) == 0 ? __builtin_ctz(v>>32) + 32 : __builtin_ctz(v));\n"
                         << "}\n";
                    // CAS-loop helpers for atomic operations without direct backend intrinsics.
                    for (int sz = 8; sz <= 64; sz *= 2) {
                        m_of << "static inline uint" << sz << "_t __mrustc_atomicloop" << sz << "(volatile uint" << sz << "_t* slot, uint" << sz << "_t param, int ordering, uint" << sz << "_t (*cb)(uint" << sz << "_t, uint" << sz << "_t)) {"
                             << " int ordering_load = (ordering == __ATOMIC_RELEASE || ordering == __ATOMIC_ACQ_REL ? __ATOMIC_RELAXED : ordering);"
                             << " for(;;) {"
                             << " uint" << sz << "_t v = __atomic_load_n(slot, ordering_load);"
                             << " uint" << sz << "_t next = cb(v, param);"
                             << " if( __atomic_compare_exchange_n(slot, &v, next, false, ordering, ordering_load) ) return v;"
                             << " }"
                             << "}\n";
            }
            m_of << "extern \"C\" {\n";

            if (m_options.emulated_i128) {
                m_of << "typedef struct { uint64_t lo, hi; } uint128_t;\n"
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
                m_of << "typedef unsigned __int128 uint128_t;\n"
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
            m_of << "\n"
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
            if (m_options.emulated_i128) {
                m_of << "static inline uint128_t __mrustc_bitrev128(uint128_t v) { uint128_t rv = { __mrustc_bitrev64(v.hi), __mrustc_bitrev64(v.lo) }; return rv; }\n";
            } else {
                m_of << "static inline uint128_t __mrustc_bitrev128(uint128_t v) {"
                     << " if(v==0) return 0;"
                     << " uint128_t rv = ((uint128_t)__mrustc_bitrev64(v>>64))|((uint128_t)__mrustc_bitrev64(v)<<64);"
                     << " return rv;"
                     << " }\n";
            }
            for (int sz = 8; sz <= 64; sz *= 2) {
                m_of << "static inline uint" << sz << "_t __mrustc_op_umax" << sz << "(uint" << sz << "_t a, uint" << sz << "_t b) { return (a > b ? a : b); }\n"
                     << "static inline uint" << sz << "_t __mrustc_op_umin" << sz << "(uint" << sz << "_t a, uint" << sz << "_t b) { return (a < b ? a : b); }\n"
                     << "static inline uint" << sz << "_t __mrustc_op_imax" << sz << "(uint" << sz << "_t a, uint" << sz << "_t b) { return ((int" << sz << "_t)a > (int" << sz << "_t)b ? a : b); }\n"
                     << "static inline uint" << sz << "_t __mrustc_op_imin" << sz << "(uint" << sz << "_t a, uint" << sz << "_t b) { return ((int" << sz << "_t)a < (int" << sz << "_t)b ? a : b); }\n"
                     << "static inline uint" << sz << "_t __mrustc_op_and_not" << sz << "(uint" << sz << "_t a, uint" << sz << "_t b) { return ~(a & b); }\n";
            }

            // Float16 and Float128
            m_of << "typedef struct f16 { uint16_t v; } f16;\n"
                 << "static f16 f16_disabled(){ abort(); }\n"
                 << "static int f16_cmp(f16 a, f16 b){ abort(); }\n"
                 << "typedef struct f128 { uint128_t v; } f128;\n";
            if (m_options.emulated_i128) {
                m_of << "static inline f128 make_f128_bits(uint64_t hi, uint64_t lo) { f128 rv = { make128_raw(hi, lo) }; return rv; }\n";
            } else {
                m_of << "static inline f128 make_f128_bits(uint64_t hi, uint64_t lo) { f128 rv = { ((uint128_t)hi << 64) | lo }; return rv; }\n";
            }
            m_of << "static f128 f128_disabled(){ abort(); }\n"
                 << "static int f128_cmp(f128 a, f128 b){ abort(); }\n";
        }

        ~CodeGeneratorC() {
        }

        void finalise(const TransOptions& opt, CodegenOutput out_ty, const ::std::string& hir_file) override {
            const bool create_shims = (out_ty == CodegenOutput::Executable);

            // TODO: Support dynamic libraries too
            // - No main, but has the rest.
            // - Well... for cdylibs that's the case, for rdylibs it's not
            if (out_ty == CodegenOutput::Executable && !m_crate.m_no_main) {
                // TODO: Define this function in MIR?
                m_of << "}\n";
                m_of << "int main(int argc, const char* argv[]) {\n";
                auto c_start_path = m_resolve.m_crate.get_lang_item_path_opt("mrustc-start");
                if (c_start_path == ::HIR::SimplePath()) {
                    auto main_path = m_crate.get_lang_item_path(Span(), "mrustc-main");
                    const auto& main_fcn = m_crate.get_function_by_path(sp, main_path);

                    const auto& start_path = m_resolve.m_crate.get_lang_item_path_opt("start");
                    if (m_crate.m_is_no_core && start_path == ::HIR::SimplePath()) {
                        // A no_core binary has no standard entrypoint protocol.
                        // Call its ordinary main directly instead of inventing a
                        // `start` language item.
                        m_of << "\t" << TransMangle(::HIR::GenericPath(main_path)) << "();\n";
                        m_of << "\treturn 0;\n";
                    } else {
                        auto start_gpath = ::HIR::GenericPath(m_resolve.m_crate.get_lang_item_path(Span(), "start"));
                        start_gpath.m_params.m_types.push_back(main_fcn.m_return);
                        m_of << "\treturn " << TransMangle(start_gpath) << "(" << TransMangle(::HIR::GenericPath(main_path)) << ", argc, (uint8_t**)argv";
                        m_of << ", 0"; // `sigpipe` setting
                        // 0: Default, 1: Inherit, 2: SIG_IGN, 3: SIG_DFL
                        m_of << ");\n";
                    }
                } else {
                    m_of << "\treturn " << TransMangle(::HIR::GenericPath(c_start_path)) << "(argc, (uint8_t**)argv);\n";
                }
                m_of << "}\n";
                m_of << "extern \"C\" {\n";
            }

            // Auto-generated code/items for the "root" rust binary (cdylib or executable)
            if (create_shims) {
                // Allocator/panic shims
                {
                    const auto allocator_it = m_crate.m_lang_items.find(GLOBAL_ALLOCATOR_LANG_ITEM);
                    const bool has_global_allocator = allocator_it != m_crate.m_lang_items.end();
                    const HIR::Static* global_allocator = has_global_allocator
                        ? &m_crate.get_static_by_path(Span(), allocator_it->second)
                        : nullptr;
                    for (size_t i = 0; i < NUM_ALLOCATOR_METHODS; i++) {
                        struct H {
                            static void ty_args(::std::vector<const char*>& out, AllocatorDataTy t) {
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

                            static const char* ty_ret(AllocatorDataTy t) {
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

                            static void emit_proto(::std::ostream& os, const AllocatorMethod& method, const char* name_prefix, const ::std::vector<const char*>& args) {
                                os << H::ty_ret(method.ret) << " " << name_prefix << method.name << "(";
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
                        for (size_t j = 0; j < method.n_args; j++) {
                            H::ty_args(args, method.args[j]);
                        }
                        H::emit_proto(m_of, method, "__rust_", args);
                        m_of << " {\n";
                        if (!has_global_allocator) {
                            const char* alloc_prefix = "__rdl_";
                            m_of << "\textern ";
                            H::emit_proto(m_of, method, alloc_prefix, args);
                            m_of << ";\n";
                            m_of << "\t";
                            if (method.ret != AllocatorDataTy::Unit) {
                                m_of << "return ";
                            }
                            m_of << alloc_prefix << method.name << "(";
                            for (size_t j = 0; j < args.size(); j++) {
                                if (j != 0) {
                                    m_of << ", ";
                                }
                                m_of << "a" << j;
                            }
                            m_of << ");\n";
                        } else {
                            size_t flat_arg = 0;
                            size_t layout_arg = 0;
                            for (size_t j = 0; j < method.n_args; j++) {
                                switch (method.args[j]) {
                                    case AllocatorDataTy::Layout:
                                        m_of << "\tauto layout" << layout_arg << " = "
                                             << TransMangle(TransAllocatorLayoutCtorPath(m_crate))
                                             << "(a" << flat_arg << ", a" << flat_arg + 1 << ");\n";
                                        flat_arg += 2;
                                        layout_arg += 1;
                                        break;
                                    case AllocatorDataTy::Ptr:
                                    case AllocatorDataTy::Usize:
                                        flat_arg += 1;
                                        break;
                                    case AllocatorDataTy::Unit:
                                    case AllocatorDataTy::ResultPtr:
                                        throw "";
                                }
                            }

                            const auto method_path = TransAllocatorMethodPath(m_crate, global_allocator->m_type, method);
                            const HIR::Path static_path = HIR::GenericPath(allocator_it->second);
                            m_of << "\t";
                            if (method.ret != AllocatorDataTy::Unit) {
                                m_of << "return reinterpret_cast<int8_t*>(";
                            }
                            m_of << TransMangle(method_path) << "(&" << TransMangle(static_path) << ".val";
                            flat_arg = 0;
                            layout_arg = 0;
                            for (size_t j = 0; j < method.n_args; j++) {
                                m_of << ", ";
                                switch (method.args[j]) {
                                    case AllocatorDataTy::Layout:
                                        m_of << "layout" << layout_arg;
                                        flat_arg += 2;
                                        layout_arg += 1;
                                        break;
                                    case AllocatorDataTy::Ptr:
                                        m_of << "reinterpret_cast<uint8_t*>(a" << flat_arg << ")";
                                        flat_arg += 1;
                                        break;
                                    case AllocatorDataTy::Usize:
                                        m_of << "a" << flat_arg;
                                        flat_arg += 1;
                                        break;
                                    case AllocatorDataTy::Unit:
                                    case AllocatorDataTy::ResultPtr:
                                        throw "";
                                }
                            }
                            m_of << ")";
                            if (method.ret != AllocatorDataTy::Unit) {
                                m_of << ")";
                            }
                            m_of << ";\n";
                        }
                        m_of << "}\n";
                    }

                    m_of << "void __rust_no_alloc_shim_is_unstable_v2() {}\n";

                    {
                        auto oom_method = m_crate.get_lang_item_path_opt("mrustc-alloc_error_handler");
                        m_of << "uint8_t __rust_alloc_error_handler_should_panic = 0;\n";
                        m_of << "uint8_t __rust_no_alloc_shim_is_unstable = 0;\n";

                        auto layout_path = ::HIR::SimplePath("core", {"alloc", "Layout"});
                        if (oom_method != HIR::SimplePath()) {
                            m_of << "struct s_" << TransMangle(layout_path) << "_A { uintptr_t a, b; };\n";
                            m_of << "void oom_impl(struct s_" << TransMangle(layout_path) << "_A l) {"
                                 << " extern void " << TransMangle(oom_method) << "(struct s_" << TransMangle(layout_path) << "_A l);"
                                 << " " << TransMangle(oom_method) << "(l);"
                                 << " }\n";
                        }

                        // Force abort on alloc error, rustc uses `-Zoom={panic,abort}` to select this
                        m_of << "uint8_t __rust_alloc_error_handler_should_panic_v2() { return 0; }";
                        m_of << "void __rust_alloc_error_handler(uintptr_t s, uintptr_t a) {\n";
                        if (oom_method == HIR::SimplePath()) {
                            m_of << "\tvoid __rdl_oom(uintptr_t, uintptr_t);\n";
                            m_of << "\t__rdl_oom(s,a);\n";
                        } else {
                            m_of << "\tstruct s_" << TransMangle(layout_path) << "_A v = { s, a };\n";
                            m_of << "\toom_impl(v);\n";
                        }
                        m_of << "}\n";
                    }
                }

                {
                    // Bind `panic_impl` only when this crate actually provides
                    // a panic implementation. A no_core binary without one can
                    // still be valid when no generated code uses it.
                    const auto& panic_impl_path = m_crate.get_lang_item_path_opt("mrustc-panic_implementation");
                    if (panic_impl_path != ::HIR::SimplePath()) {
                        m_of << "uint32_t panic_impl(uintptr_t payload) {";
                        m_of << "extern uint32_t " << TransMangle(panic_impl_path) << "(uintptr_t payload);";
                        m_of << "return " << TransMangle(panic_impl_path) << "(payload);";
                        m_of << "}\n";
                    } else if (!m_crate.m_is_no_core) {
                        m_crate.get_lang_item_path(Span(), "mrustc-panic_implementation");
                    }
                }
            }

            m_of << "}\n";
            m_of.flush();
            m_of.close();
            ASSERT_BUG(Span(), !m_of.bad(), "Error set on output stream for: " << m_outfile_path_c);

            class LinkList: private StringList {
            public:
                enum class Ty {
                    //Border,   // --{push,pop}-state
                    Directory, // -L <value>
                    Explicit,  // <value>
                    Implicit,  // -l <value>
                };

            private:
                std::vector<Ty> m_ty;

            public:
                void push_dir(const char* s) {
#if 1
                    // Don't de-dup since there's the push/pop rules
                    auto it = ::std::find_if(StringList::begin(), StringList::end(), [&](const char* es) {
                        return ::std::strcmp(es, s) == 0;
                    });
                    if (it != StringList::end()) {
                        return;
                    }
#endif
                    m_ty.push_back(Ty::Directory);
                    this->push_back(s);
                }

                void push_explicit(std::string s) {
                    m_ty.push_back(Ty::Explicit);
                    this->push_back(std::move(s));
                }

                void push_lib(const char* s) {
                    if (m_ty.size() > 0 && m_ty.back() == Ty::Implicit && std::strcmp(this->get_vec().back(), s) == 0) {
                        return;
                    }
                    m_ty.push_back(Ty::Implicit);
                    this->push_back(s);
                }

                void push_lib(std::string s) {
                    if (m_ty.size() > 0 && m_ty.back() == Ty::Implicit && s == this->get_vec().back()) {
                        return;
                    }
                    m_ty.push_back(Ty::Implicit);
                    this->push_back(std::move(s));
                }

                void push_border() {
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
                        return std::make_pair(parent.m_ty[idx], parent.get_vec()[idx]);
                    }
                };

                iterator begin() const {
                    return iterator(*this, 0);
                }

                iterator end() const {
                    return iterator(*this, this->get_vec().size());
                }
            };

            // Combined list to ensure a sane resolution order?
            LinkList libraries_and_dirs;

            StringList ext_crates;
            StringList ext_crates_dylib;
            switch (out_ty) {
                case CodegenOutput::Executable:
                case CodegenOutput::DynamicLibrary:
                    for (const auto& crate_name : m_crate.m_ext_crates_ordered) {
                        const auto& crate = m_crate.m_ext_crates.at(crate_name);
                        auto is_dylib = [](const ::HIR::ExternCrate& c) {
                            bool rv = false;
                            // TODO: Better rule than this
                            rv |= (c.m_path.compare(c.m_path.size() - 3, 3, ".so") == 0);
                            return rv;
                        };
                        // If this crate is included in a dylib crate, ignore it
                        bool is_in_dylib = false;
                        for (const auto& crate2 : m_crate.m_ext_crates) {
                            if (is_dylib(crate2.second)) {
                                for (const auto& subcrate : crate2.second.m_data->m_ext_crates) {
                                    if (subcrate.second.m_path == crate.m_path) {
                                        DEBUG(crate_name << " referenced by dylib " << crate2.first);
                                        is_in_dylib = true;
                                    }
                                }
                            }
                            if (is_in_dylib) {
                                break;
                            }
                        }
                        // NOTE: Only exclude non-dylibs referenced by other dylibs
                        if (is_in_dylib && !is_dylib(crate)) {
                            continue;
                        }

                        // Ignore panic crates unless they're the selected crate (and add in the selected panic crate)
                        if (crate.m_data->m_lang_items.count("mrustc-panic_runtime")) {
                            // Check if this is the requested panic crate
                            if (strncmp(crate_name.c_str(), opt.panic_crate.c_str(), opt.panic_crate.size()) != 0) {
                                DEBUG("Ignore not-selected panic crate: " << crate_name);
                                continue;
                            } else {
                                DEBUG("Keep panic crate: " << crate_name);
                            }
                        }

                        if (crate.m_path.compare(crate.m_path.size() - 5, 5, ".rlib") == 0) {
                            ext_crates.push_back(crate.m_path.c_str());
                        } else if (is_dylib(crate)) {
                            ext_crates_dylib.push_back(crate.m_path.c_str());
                        } else {
                            // Probably a procedural macro, ignore it
                        }
                    }

                    struct H {
                        static bool file_exists(const std::string& path) {
                            return std::ifstream(path).is_open();
                        }

                        static std::string find_library_one(const std::string& path, const std::string& name) {
                            std::string lib_path;
                            lib_path = FMT(path << "/lib" << name << ".so");
                            if (file_exists(lib_path)) {
                                return lib_path;
                            }
                            lib_path = FMT(path << "/lib" << name << ".a");
                            if (file_exists(lib_path)) {
                                return lib_path;
                            }
                            return "";
                        }

                        static std::string find_library(const std::vector<std::string>& paths1, const std::vector<std::string>& paths2, const std::string& name) {
                            std::string rv;
                            for (const auto& p : paths1) {
                                if ((rv = find_library_one(p, name)) != "") {
                                    return rv;
                                }
                            }
                            for (const auto& p : paths2) {
                                if ((rv = find_library_one(p, name)) != "") {
                                    return rv;
                                }
                            }
                            return "";
                        }
                    };

                    for (const auto& path : opt.library_search_dirs) {
                        libraries_and_dirs.push_dir(path.c_str());
                    }
                    for (const auto& path : opt.libraries) {
                        libraries_and_dirs.push_lib(path.c_str());
                    }
                    libraries_and_dirs.push_border();

                    for (const auto& path : m_crate.m_link_paths) {
                        libraries_and_dirs.push_dir(path.c_str());
                    }
                    for (const auto& lib : m_crate.m_ext_libs) {
                        ASSERT_BUG(Span(), lib.name != "", "");
                        libraries_and_dirs.push_lib(lib.name.c_str());
                    }

                    for (const auto& crate_name : m_crate.m_ext_crates_ordered) {
                        const auto& crate = m_crate.m_ext_crates.at(crate_name);
                        if (!crate.m_data->m_ext_libs.empty() || !crate.m_data->m_link_paths.empty()) {
                            libraries_and_dirs.push_border();
                        }
                        for (const auto& path : crate.m_data->m_link_paths) {
                            libraries_and_dirs.push_dir(path.c_str());
                        }
                        // NOTE: Does explicit lookup, to provide scoped search directories
                        // - Needed for 1.39 cargo on linux when libgit2 and libz exist on the system, butsystem libgit2 isn't new enough
                        for (const auto& lib : crate.m_data->m_ext_libs) {
                            ASSERT_BUG(Span(), lib.name != "", "Empty lib from " << crate_name);
                            auto path = H::find_library(crate.m_data->m_link_paths, opt.library_search_dirs, lib.name);
                            if (path != "") {
                                libraries_and_dirs.push_explicit(std::move(path));
                            } else {
                                libraries_and_dirs.push_lib(lib.name.c_str());
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
            size_t arg_file_start = 0;
            // Pick the C++ compiler.
            {
                std::string varname = "CXX_" + TargetGetCurSpec().m_backend_c.m_c_compiler;
                std::replace(varname.begin(), varname.end(), '-', '_');

                if (getenv(varname.c_str())) {
                    args.push_back(getenv(varname.c_str()));
                } else if (getenv("CXX")) {
                    args.push_back(getenv("CXX"));
                } else if (system(("command -v " + TargetGetCurSpec().m_backend_c.m_c_compiler + "-g++" + " >/dev/null 2>&1").c_str()) == 0) {
                    args.push_back(TargetGetCurSpec().m_backend_c.m_c_compiler + "-g++");
                } else {
                    args.push_back("g++");
                }
            }
            arg_file_start = args.get_vec().size();
            args.push_back("-std=gnu++20");
            args.push_back("-fexceptions");
            for (const auto& a : TargetGetCurSpec().m_backend_c.m_compiler_opts) {
                args.push_back(a.c_str());
            }
            switch (opt.opt_level) {
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
            if (opt.opt_level != OptimizationLevel::None) {
                args.push_back("-fno-tree-sra");
            }
    #endif
#endif
            switch (opt.debug_info) {
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
            switch (out_ty) {
                case CodegenOutput::DynamicLibrary:
                case CodegenOutput::Executable:
                case CodegenOutput::Object:
                    args.push_back(m_outfile_path.c_str());
                    break;
                case CodegenOutput::StaticLibrary:
                    args.push_back(m_outfile_path + ".o");
                    break;
            }
            args.push_back(m_outfile_path_c.c_str());
            switch (out_ty) {
                case CodegenOutput::DynamicLibrary:
                    args.push_back("-shared");
                case CodegenOutput::Executable:
                    for (const auto& a : TargetGetCurSpec().m_backend_c.m_linker_opts_pre) {
                        args.push_back(a.c_str());
                    }
                    for (const auto& c : ext_crates) {
                        args.push_back(std::string(c) + ".o");
                    }
                    for (const auto& c : ext_crates_dylib) {
                        args.push_back(c);
                    }
                    for (auto l_d : libraries_and_dirs) {
                        switch (l_d.first) {
                            case LinkList::Ty::Directory:
                                args.push_back("-L");
                                args.push_back(l_d.second);
                                break;
                            case LinkList::Ty::Implicit:
                                if (!strncmp(l_d.second, "framework=", strlen("framework="))) {
                                    args.push_back("-framework");
                                    args.push_back(l_d.second + strlen("framework="));
                                } else {
                                    args.push_back("-l");
                                    args.push_back(l_d.second);
                                }
                                break;
                            case LinkList::Ty::Explicit:
                                args.push_back(l_d.second);
                                break;
                        }
                    }
                    for (const auto& a : TargetGetCurSpec().m_backend_c.m_linker_opts_post) {
                        args.push_back(a.c_str());
                    }
                    for (const auto& a : opt.linker_args) {
                        args.push_back(a.c_str());
                    }
                    // TODO: Include the HIR file as a magic object?
                    break;
                case CodegenOutput::StaticLibrary:
                case CodegenOutput::Object:
                    args.push_back("-c");
                    break;
            }

            ::std::stringstream cmd_ss;
            std::string command_file = m_outfile_path + "_cmd.txt";
            std::ofstream command_file_stream;
            if (getenv("MRUSTC_CCACHE")) {
                cmd_ss << "ccache ";
            }
            bool use_arg_file = arg_file_start > 0;
            if (use_arg_file) {
                command_file_stream.open(command_file);
                ASSERT_BUG(Span(), command_file_stream.is_open(), "Failed to open command file `" << command_file << "` for writing");
            }
            size_t i = -1;
            for (const auto& arg : args.get_vec()) {
                i++;
                auto& out_ss = (use_arg_file && i >= arg_file_start ? static_cast<::std::ostream&>(command_file_stream) : cmd_ss);
                out_ss << "\"" << FmtShell(arg) << "\" ";
            }
            if (use_arg_file) {
                cmd_ss << "@\"" << FmtShell(command_file) << "\"";
                command_file_stream.close();
                ASSERT_BUG(Span(), !command_file_stream.bad(), "Error set on output stream for: " << m_outfile_path_c);
            }
            //DEBUG("- " << cmd_ss.str());
            ::std::cout << "Running command - " << cmd_ss.str() << ::std::endl;
            if (opt.build_command_file != "") {
                ::std::cerr << "INVOKE CC: " << cmd_ss.str() << ::std::endl;
                ::std::ofstream(opt.build_command_file) << cmd_ss.str() << ::std::endl;
            } else {
                int ec = system(cmd_ss.str().c_str());
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
            if (out_ty == CodegenOutput::StaticLibrary) {
                ::std::ofstream of(m_outfile_path);
                if (!of.good()) {
                    // TODO: Error?
                }
            }
        }

        void emit_box_drop(unsigned indent_level, const ::HIR::TypeData* inner_type, const ::HIR::TypeData* box_type, const ::MIR::LValue& slot, bool run_destructor) {
            auto indent = RepeatLitStr{"\t", static_cast<int>(indent_level)};
            if (run_destructor) {
                auto inner_ptr = ::MIR::LValue::new_Field(::MIR::LValue::new_Field(::MIR::LValue::new_Field(slot.clone(), 0), 0), 0);
                emit_destructor_call(::MIR::LValue::new_Deref(mv$(inner_ptr)), inner_type, /*unsized_valid=*/true, indent_level);
            }

            auto p = ::HIR::Path(box_type, m_crate.get_lang_item_path(Span(), "drop"), "drop");
            m_of << indent << TransMangle(p) << "(&";
            emit_lvalue(slot);
            m_of << ");\n";

            // The pointee is a synthetic Box move-path, not a physical field. A shallow
            // drop skips that path, but still drops the real fields after Box::drop.
            const auto* repr = TargetGetTypeRepr(sp, m_resolve, box_type);
            MIR_ASSERT(*m_mir_res, repr, "No repr for Box " << box_type);
            auto field = ::MIR::LValue::new_Field(slot.clone(), 0);
            for (const auto& field_repr : repr->fields) {
                if (m_resolve.type_needs_drop_glue(sp, field_repr.ty)) {
                    emit_destructor_call(field, field_repr.ty, /*unsized_valid=*/false, indent_level);
                }
                field.inc_Field();
            }
        }

        void emit_global_asm(const ::HIR::GlobalAssembly& se) override {
            m_of << "__asm__ (\"";
            if ((TargetGetCurSpec().m_arch.m_name == "x86" || TargetGetCurSpec().m_arch.m_name == "x86_64") && !se.m_options.att_syntax) {
                m_of << ".intel_syntax noprefix; ";
            }
            for (const auto& l : se.m_lines) {
                for (const auto& f : l.frags) {
                    m_of << FmtGccAsm(f.before, false);
                    ASSERT_BUG(Span(), f.index < se.m_symbols.size(), "Invalid argument reference in global assembly");
                    TODO(Span(), "Handle interpolation in global_asm! - " << se.m_symbols[f.index]);
                }
                m_of << FmtGccAsm(l.trailing, false);
                m_of << ";\\n ";
            }
            if ((TargetGetCurSpec().m_arch.m_name == "x86" || TargetGetCurSpec().m_arch.m_name == "x86_64") && !se.m_options.att_syntax) {
                m_of << ".att_syntax; ";
            }
            m_of << "\");\n";
        }

        void emit_type_id(const ::HIR::TypeData* ty) override {
            m_of << "tTYPEID __typeid_" << TransMangle(ty) << " __attribute__((weak));\n";

        }

        void emit_type_proto(const ::HIR::TypeData* ty) override {
            TRACE_FUNCTION_F(ty);
            TU_MATCH_HDRA( (*ty), {)
            default:
                // No prototype required
            TU_ARMA(Tuple, te) {
                    if (te.size() > 0) {
                        m_of << "typedef struct ";
                        emit_ctype(ty);
                        m_of << " ";
                        emit_ctype(ty);
                        m_of << ";\n";
                    }
                }
                TU_ARMA(Function, te) {
                    emit_type_fn(ty);
                    m_of << "\n";
                }
                TU_ARMA(NamedFunction, te) {
                    m_of << "typedef struct ";
                    emit_ctype(ty);
                    m_of << " ";
                    emit_ctype(ty);
                    m_of << ";\n";
                }
                TU_ARMA(Array, te) {
                    m_of << "typedef struct ";
                    emit_ctype(ty);
                    m_of << " ";
                    emit_ctype(ty);
                    m_of << ";\n";
                }
                TU_ARMA(Path, te) {
                TU_MATCH_HDRA( (te.binding), {)
                TU_ARMA(Unbound, tpb) throw "";
                        TU_ARMA(Opaque, tpb) throw "";
                        TU_ARMA(Struct, tpb) {
                            m_of << "struct s_" << TransMangle(te.path) << ";\n";
                        }
                        TU_ARMA(ExternType, tpb) {
                            m_of << "struct x_" << TransMangle(te.path) << ";\n";
                        }
                        TU_ARMA(Union, tpb) {
                            m_of << "union u_" << TransMangle(te.path) << ";\n";
                        }
                        TU_ARMA(Enum, tpb) {
                            m_of << "struct e_" << TransMangle(te.path) << ";\n";
                        }
                }
                }
                TU_ARMA(ErasedType, te) {
                    // TODO: Is this actually a bug?
                    return;
                }
            }
        }

        void emit_type_fn(const ::HIR::TypeData* ty) {
            if (m_emitted_fn_types.count(ty)) {
                return;
            }
            m_emitted_fn_types.insert(ty);

            const auto& te = ty->as_Function();
            m_of << "typedef ";
            // TODO: ABI marker, need an ABI enum?
            if (te.m_rettype == m_crate.m_types.unit()) {
                m_of << "void";
            } else {
                // TODO: Better emit_ctype call for return type?
                emit_ctype(te.m_rettype);
            }
            m_of << " (";
            m_of << "*";
            emit_ctype(ty);
            m_of << ")(";
            if (te.m_arg_types.size() == 0) {
                m_of << "void)";
            } else {
                for (unsigned int i = 0; i < te.m_arg_types.size(); i++) {
                    if (i != 0) {
                        m_of << ",";
                    }
                    m_of << " ";
                    this->emit_ctype(te.m_arg_types[i]);
                }
                if (te.is_variadic) {
                    m_of << ", ...";
                }
                m_of << " )";
            }
            m_of << ";";
        }

        // Shared logic between `emit_struct` and `emit_type` (w/ Tuple)
        void emit_struct_inner(const ::HIR::TypeData* ty, const TypeRepr* repr, unsigned packing_max_align) {
            // Fill `fields` with ascending indexes (for sorting)
            // AND: Determine if the type has a a zero-sized item that has an alignment equal to the structure's alignment
            ::std::vector<unsigned> fields;
            fields.reserve(repr->fields.size());
            ::std::vector<bool> zsts;
            zsts.reserve(repr->fields.size());
            size_t max_align = 0;
            // `max_align` is the largest natural field alignment; `c_max_align` is what the C compiler will derive for the emitted struct.
            size_t c_max_align = 0;
            bool has_manual_align = false;
            for (const auto& ent : repr->fields) {
                const auto& ty = ent.ty;

                size_t sz = -1, al = 0;
                TargetGetSizeAndAlignOf(sp, m_resolve, ty, sz, al);
                if (sz == 0 && al == repr->align && al > 0) {
                    has_manual_align = true;
                }
                max_align = std::max(max_align, al);
                // Track what C will derive separately - under a capping ABI an interior over-aligned member doesn't raise it
                {
                    size_t al_c = al;
                    if (TargetCapsMemberAlignment() && sz > 0 && ent.offset != 0 && al_c > 4 && !TargetTypeHasUserAlignment(sp, m_resolve, ty)) {
                        al_c = 4;
                    }
                    c_max_align = std::max(c_max_align, al_c);
                }

                fields.push_back(fields.size());
                zsts.push_back(sz == 0);
            }
            if (packing_max_align == 0 && c_max_align != repr->align /*&& repr->size > 0*/) {
                has_manual_align = true;
            }
            // An align-1 type must be emitted packed - gcc takes a container's alignment from the member's natural alignment
            if (packing_max_align == 0 && !has_manual_align && repr->align == 1 && repr->size > 1) {
                packing_max_align = 1;
            }
            // - Sort the fields by offset
            ::std::sort(fields.begin(), fields.end(), [&](auto a, auto b) {
                if (repr->fields[a].offset == repr->fields[b].offset) {
                    return !zsts[a] < !zsts[b]; // Sort zero sized fields first (!zst means size is 1+)
                }
                return repr->fields[a].offset < repr->fields[b].offset;
            });

            // For repr(packed), mark as packed
            if (packing_max_align) {
                m_of << "#pragma pack(push, " << packing_max_align << ")\n";
            }
            if (ty->is_Tuple()) {
                m_of << "typedef ";
                m_of << "struct ";
            }
            emit_ctype(ty);
            m_of << " {\n";

            bool has_unsized = false;
            size_t sized_fields = 0;
            size_t cur_ofs = 0;
            bool is_first_field = true;
            for (unsigned fld : fields) {
                const auto& ty = repr->fields[fld].ty;
                const auto offset = repr->fields[fld].offset;
                size_t s = 0, a;
                TargetGetSizeAndAlignOf(sp, m_resolve, ty, s, a);
                DEBUG("@" << offset << ": " << ty << " " << s << "," << a);

                // Check offset/alignment
                if (s == SIZE_MAX) {
                } else if (s == 0) {
                } else {
                    MIR_ASSERT(*m_mir_res, cur_ofs <= offset, "Current offset is already past expected (#" << fld << "): " << cur_ofs << " > " << offset);
                    auto field_align = a;
                    // PowerPC 32-bit ABI alignment
                    if (TargetGetCurSpec().m_arch.m_name == "powerpc") {
                        if (s > 0) {
                            if (!is_first_field && field_align >= 4 && field_align <= 8) {
                                field_align = 4;
                            }
                            is_first_field = false;
                        }
                    }
                    a = packing_max_align > 0 ? std::min<size_t>(packing_max_align, field_align) : field_align;
                    DEBUG("a = " << a);
                    while (cur_ofs % a != 0) {
                        cur_ofs++;
                    }
                }

                // Inject padding
                if (cur_ofs < offset) {
                    auto n = offset - cur_ofs;
                    m_of << "\tuint8_t _padding" << fld << "[" << n << "];\n";
                    cur_ofs += n;
                }
                MIR_ASSERT(*m_mir_res, cur_ofs == offset, "Current offset doesn't match expected (#" << fld << "): " << cur_ofs << " != " << offset);

                m_of << "\t";
                m_of << "/*@" << offset << "*/";
                if (const auto* te = ty->opt_Slice()) {
                    emit_ctype(te->inner, FMT_CB(ss, ss << "_" << fld << "[0]";));
                    has_unsized = true;
                } else if (ty->is_TraitObject()) {
                    m_of << "unsigned char _" << fld << "[0]";
                    has_unsized = true;
                } else if (ty == ::HIR::CoreType::Str) {
                    m_of << "uint8_t _" << fld << "[0]";
                    has_unsized = true;
                } else if (TU_TEST1(*ty, Path, .binding.is_ExternType())) {
                    m_of << "// External";
                    has_unsized = true;
                } else {
                    if (s == 0 && m_options.disallow_empty_structs) {
                        m_of << "// ZST";
                    } else {
                        // TODO: Nested unsized?
                        emit_ctype(ty, FMT_CB(ss, ss << "_" << fld));
                        sized_fields++;

                        has_unsized |= (s == SIZE_MAX);
                    }
                }
                m_of << "; // " << ty << "\n";

                cur_ofs += s;
            }
            if (sized_fields == 0 && !has_unsized && m_options.disallow_empty_structs) {
                m_of << "\tchar _d;\n";
            }
            m_of << "}";
            if (has_manual_align) {
                m_of << " __attribute__((__aligned__(" << repr->align << ")))";
                m_of << " ";
                if (ty->is_Tuple()) {
                    emit_ctype(ty);
                }
                m_of << ";\n";

            } else {
                m_of << " ";
                if (ty->is_Tuple()) {
                    emit_ctype(ty);
                }
                m_of << ";\n";
            }
            if (packing_max_align != 0) {
                m_of << "#pragma pack(pop)\n";
            }
        }

        void emit_type(const ::HIR::TypeData* ty) override {
            ::MIR::Function empty_fcn;
            ::MIR::TypeResolve top_mir_res {
                sp, m_resolve, FMT_CB(ss, ss << "type " << ty;), ::HIR::TypeRef(), {}, empty_fcn
            };
            m_mir_res = &top_mir_res;

            TRACE_FUNCTION_F(ty);
            TU_MATCH_HDRA( (*ty), { )
            default:
                // Nothing to emit
                break;
                TU_ARMA(Tuple, te) {
                    if (te.size() > 0) {
                        m_of << " // " << ty << "\n";
                        const auto* repr = TargetGetTypeRepr(sp, m_resolve, ty);

                        emit_struct_inner(ty, repr, /*packing_max_align=*/0);

                        if (repr->size > 0 && repr->size != SIZE_MAX) {
                            m_of << "typedef char sizeof_assert_";
                            emit_ctype(ty);
                            m_of << "[ (sizeof(";
                            emit_ctype(ty);
                            m_of << ") == " << repr->size << ") ? 1 : -1 ];\n";
                        }
                    }
                }
                TU_ARMA(Function, te) {
                    emit_type_fn(ty);
                    m_of << " // " << ty << "\n";
                }
                TU_ARMA(NamedFunction, te) {
                    m_of << "typedef struct ";
                    emit_ctype(ty);
                    m_of << " {";
                    if (m_options.disallow_empty_structs) {
                        m_of << " char _unused; ";
                    }
                    m_of << "} ";
                    emit_ctype(ty);
                    m_of << ";\n";
                }
                TU_ARMA(Array, te) {
                    size_t rust_size;
                    ASSERT_BUG(sp, TargetGetSizeOf(sp, m_resolve, ty, rust_size), "Unable to determine array size for " << ty);
                    const bool is_zero_sized = rust_size == 0;

                    m_of << "typedef ";
                    size_t align;
                    if (is_zero_sized) {
                        TargetGetAlignOf(sp, m_resolve, ty, align);

                    }
                    m_of << "struct ";
                    emit_ctype(ty);
                    m_of << " { ";
                    if (is_zero_sized && m_options.disallow_empty_structs) {
                        m_of << "char _d;";
                    } else if (is_zero_sized) {
                        if (te.size.as_Known() > 0) {
                            emit_ctype(te.inner);
                            m_of << " DATA[1];";
                        }
                    }
                    else {
                        emit_ctype(te.inner);
                        m_of << " DATA[" << te.size.as_Known() << "];";
                    }
                    m_of << " } ";
                    if (is_zero_sized) {
                        m_of << " __attribute__((";
                        m_of << "__aligned__(" << align << "),";
                        m_of << "))";

                    }
                    emit_ctype(ty);
                    m_of << ";";
                    m_of << " // " << ty << "\n";
                }
                TU_ARMA(ErasedType, te) {
                    // TODO: Is this actually a bug?
                    return;
                }
            }

            m_mir_res = nullptr;
        }

        void emit_struct(const Span& sp, const ::HIR::GenericPath& p, const ::HIR::Struct& item) override {
            ::MIR::Function empty_fcn;
            ::MIR::TypeResolve top_mir_res {
                sp, m_resolve, FMT_CB(ss, ss << "struct " << p;), ::HIR::TypeRef(), {}, empty_fcn
            };
            m_mir_res = &top_mir_res;
            // TODO: repr(transparent) and repr(align(foo))

            TRACE_FUNCTION_F(p);
            auto item_ty = m_crate.m_types.path(p.clone(), ::HIR::TypePathBinding::make_Struct(&item));
            const auto* repr = TargetGetTypeRepr(sp, m_resolve, item_ty);
            MIR_ASSERT(*m_mir_res, repr, "No repr for struct " << p);

            m_of << "// struct " << p << "\n";

            emit_struct_inner(item_ty, repr, item.m_max_field_alignment);

            if (repr->size > 0 && repr->size != SIZE_MAX) {
                // TODO: Handle unsized (should check the size of the fixed-size region)
                m_of << "typedef char sizeof_assert_" << TransMangle(p) << "[ (sizeof(struct s_" << TransMangle(p) << ") == " << repr->size << ") ? 1 : -1 ];\n";
            }
            m_of << "typedef char alignof_assert_" << TransMangle(p) << "[ (ALIGNOF(struct s_" << TransMangle(p) << ") == " << repr->align << ") ? 1 : -1 ];\n";

            m_mir_res = nullptr;
        }

        void emit_union(const Span& sp, const ::HIR::GenericPath& p, const ::HIR::Union& item) override {
            ::MIR::Function empty_fcn;
            ::MIR::TypeResolve top_mir_res {
                sp, m_resolve, FMT_CB(ss, ss << "union " << p;), ::HIR::TypeRef(), {}, empty_fcn
            };
            m_mir_res = &top_mir_res;

            TRACE_FUNCTION_F(p);
            auto item_ty = m_crate.m_types.path(p.clone(), ::HIR::TypePathBinding::make_Union(&item));
            const auto* repr = TargetGetTypeRepr(sp, m_resolve, item_ty);
            MIR_ASSERT(*m_mir_res, repr != nullptr, "No repr for union " << item_ty);

            m_of << "union u_" << TransMangle(p) << " {\n";
            for (unsigned int i = 0; i < repr->fields.size(); i++) {
                assert(repr->fields[i].offset == 0);
                m_of << "\t";
                emit_ctype(repr->fields[i].ty, FMT_CB(ss, ss << "var_" << i;));
                m_of << ";\n";
            }
            m_of << "}";
            // Pin union alignment - under the power ABI gcc takes a union's alignment from its *first* member
            if (repr->align > 0) {
                m_of << " __attribute__((__aligned__(" << repr->align << ")))";
            }
            m_of << ";\n";
            if (true && repr->size > 0) {
                m_of << "typedef char sizeof_assert_" << TransMangle(p) << "[ (sizeof(union u_" << TransMangle(p) << ") == " << repr->size << ") ? 1 : -1 ];\n";
            }

            m_mir_res = nullptr;
        }

        bool is_enum_tag(const TypeRepr* repr, size_t idx) {
            if (const auto* ve = repr->variants.opt_Values()) {
                return ve->is_tag(idx);
            }
            if (const auto* ve = repr->variants.opt_Linear()) {
                return ve->is_tag(idx);
            }
            return false;
        }

        const HIR::TypeData* emit_enum_path(const TypeRepr* repr, const TypeRepr::FieldPath& path) {
            if (is_enum_tag(repr, path.index)) {
                // Some enums have the tag outside, some inside
                if (m_embedded_tags.count(repr)) {
                    m_of << ".DATA";
                }
                m_of << ".TAG";
                assert(path.sub_fields.empty());
            } else {
                m_of << ".DATA.var_" << path.index;
            }
            const auto* ty = &repr->fields[path.index].ty;
            for (const auto& fld : path.sub_fields) {
                if (fld == TypeRepr::FieldPath::ARRAY_ELEMENT) {
                    const auto* array = (*ty)->opt_Array();
                    assert(array && array->size.is_Known() && array->size.as_Known() > 0);
                    m_of << ".DATA[0]";
                    ty = &array->inner;
                    continue;
                }
                repr = TargetGetTypeRepr(sp, m_resolve, *ty);
                if (is_enum_tag(repr, fld)) {
                    if (m_embedded_tags.count(repr)) {
                        m_of << ".DATA";
                    }
                    m_of << ".TAG";
                    assert(&fld == &path.sub_fields.back());
                } else if (/*!repr->variants.is_None() ||*/ TU_TEST1(**ty, Path, .binding.is_Enum())) {
                    m_of << ".DATA.var_" << fld;
                } else {
                    m_of << "._" << fld;
                }

                ty = &repr->fields[fld].ty;
            }
            if (const auto* te = (*ty)->opt_Borrow()) {
                if (is_dst(te->inner)) {
                    m_of << ".PTR";
                }
            } else if (const auto* te = (*ty)->opt_Pointer()) {
                if (is_dst(te->inner)) {
                    m_of << ".PTR";
                }
            }
            return *ty;
        }

        void emit_enum(const Span& sp, const ::HIR::GenericPath& p, const ::HIR::Enum& item) override {
            ::MIR::Function empty_fcn;
            ::MIR::TypeResolve top_mir_res {
                sp, m_resolve, FMT_CB(ss, ss << "enum " << p;), ::HIR::TypeRef(), {}, empty_fcn
            };
            m_mir_res = &top_mir_res;

            TRACE_FUNCTION_F(p);
            auto item_ty = m_crate.m_types.path(p.clone(), ::HIR::TypePathBinding::make_Enum(&item));
            const auto* repr = TargetGetTypeRepr(sp, m_resolve, item_ty);

            // 1. Enumerate fields with the same offset as the first (these go into a union)
            // TODO: What if all data variants are zero-sized?
            ::std::vector<unsigned> union_fields;
            for (size_t i = 1; i < repr->fields.size(); i++) {
                if (repr->fields[i].offset == repr->fields[0].offset) {
                    union_fields.push_back(i);
                }
            }
            if (union_fields.size() > 0) {
                union_fields.insert(union_fields.begin(), 0);
            }

            m_of << "// enum " << p << "\n";
            m_of << "struct e_" << TransMangle(p) << " {\n";

            // HACK: For NonZero optimised enums, emit a struct with a single field
            // - This avoids a bug in GCC5 where it would generate incorrect code if there's a union here.
            if (const auto* ve = repr->variants.opt_NonZero()) {
                m_of << "\tstruct {\n";
                m_of << "\t\t";
                unsigned idx = 1 - ve->zero_variant;
                emit_ctype(repr->fields.at(idx).ty, FMT_CB(os, os << "var_" << idx));
                m_of << ";\n";
                m_of << "\t} DATA;\n";
            }
            // If there's only one field - it's either a single variant, or a value enum
            else if (repr->fields.size() == 1) {
                if (repr->variants.is_Values()) {
                    // Tag only.
                    // - A value-only enum.
                    m_of << "\t";
                    emit_ctype(repr->fields.back().ty, FMT_CB(os, os << "TAG"));
                    m_of << ";\n";
                } else {
                    m_of << "\tunion {\n";
                    m_of << "\t\t";
                    emit_ctype(repr->fields.back().ty, FMT_CB(os, os << "var_0"));
                    m_of << ";\n";
                    m_of << "\t} DATA;\n";
                    // No tag
                }
            }
            // If there multiple fields with the same offset, they're the data variants
            else if (union_fields.size() > 0) {
                if (union_fields.size() == repr->fields.size()) {
                    // Embedded tag
                    DEBUG("Untagged, nonzero or other");
                } else {
                    // Leading & external tag: repr(C)
                    assert(union_fields.size() + 1 == repr->fields.size());
                    assert(is_enum_tag(repr, repr->fields.size() - 1));

                    assert(repr->fields.back().offset == 0);
                    DEBUG("Tag present at offset " << repr->fields.back().offset << " - " << repr->fields.back().ty);

                    m_of << "\t";
                    emit_ctype(repr->fields.back().ty, FMT_CB(os, os << "TAG"));
                    m_of << ";\n";
                }

                // Options:
                // - Leading tag (union fields have a non-zero offset, tag has zero)
                // - Embedded (tag field shares offset with union fields, or there's no tag field)

                // Make the union!
                // NOTE: The way the structure generation works is that enum variants are always first, so the field index = the variant index
                // NOTE: Only emit if there are non-empty fields
                if (::std::any_of(union_fields.begin(), union_fields.end(), [this, repr](auto x) {
                    return !this->type_is_bad_zst(repr->fields[x].ty);
                })) {
                    m_of << "\tunion {\n";
                    for (auto idx : union_fields) {
                        m_of << "\t\t";

                        const auto& ty = repr->fields[idx].ty;
                        if (this->type_is_bad_zst(ty)) {
                            m_of << "// ZST: " << ty << "\n";
                        } else {
                            if (is_enum_tag(repr, idx)) {
                                emit_ctype(ty, FMT_CB(ss, ss << "TAG"));
                                m_embedded_tags.insert(repr);
                            } else {
                                emit_ctype(ty, FMT_CB(ss, ss << "var_" << idx));
                            }
                            m_of << ";\n";
                            //sized_fields ++;
                        }
                    }
                    m_of << "\t} DATA;\n";
                }
            } else if (repr->fields.size() == 0) {
                // Empty/un-constructable
                // - Shouldn't be emitted really?
                if (m_options.disallow_empty_structs) {
                    m_of << "\tchar _d;\n";
                }
            } else {
                // One data field and a tag (or all different offsets)
                TODO(sp, "No common offsets and more than one field, is this possible? - " << item_ty);
            }

            m_of << "};\n";

            size_t exp_size = (repr->size > 0 ? repr->size : (m_options.disallow_empty_structs ? 1 : 0));
            m_of << "typedef char sizeof_assert_" << TransMangle(p) << "[ (sizeof(struct e_" << TransMangle(p) << ") == " << exp_size << ") ? 1 : -1 ];\n";

            m_mir_res = nullptr;
        }

        void emit_constructor_enum(const Span& sp, const ::HIR::GenericPath& path, const ::HIR::Enum& item, size_t var_idx) override {
            TRACE_FUNCTION_F(path << " var_idx=" << var_idx);

            auto p = path.clone();
            p.m_path.pop_component();
            auto ty = m_crate.m_types.path(p.clone(), ::HIR::TypePathBinding::make_Enum(&item));

            MonomorphStatePtr ms(m_crate.m_types, nullptr, &path.m_params, nullptr);
            ::HIR::TypeRef tmp;
            auto monomorph = [&](const auto& x) {
                return m_resolve.monomorph_expand_opt(sp, tmp, x, ms);
            };

            ASSERT_BUG(sp, item.m_data.is_Data(), "");
            const auto& var = item.m_data.as_Data().at(var_idx);
            ASSERT_BUG(sp, var.type->is_Path(), "");
            const auto& str = *var.type->as_Path().binding.as_Struct();
            ASSERT_BUG(sp, str.m_data.is_Tuple(), "");
            const auto& e = str.m_data.as_Tuple();

            HIR::Function::args_t args;
            for (unsigned int i = 0; i < e.size(); i++) {
                args.push_back(::std::make_pair(HIR::Pattern(), monomorph(e[i].ent)));
            }

            ::MIR::Function empty_fcn;
            ::MIR::TypeResolve top_mir_res {
                sp, m_resolve, FMT_CB(ss, ss << "enum cons " << path;), ty, args, empty_fcn
            };
            m_mir_res = &top_mir_res;

            m_of << "static struct e_" << TransMangle(p) << " " << TransMangle(path) << "(";
            for (unsigned int i = 0; i < e.size(); i++) {
                if (i != 0) {
                    m_of << ", ";
                }
                const auto& ty = args[i].second; // already monomorphised
                emit_ctype(ty, FMT_CB(ss, ss << "arg" << i;));
            }
            m_of << ") {\n";

            m_of << "\tstruct e_" << TransMangle(p) << " rv;\n";

            std::vector<MIR::Param> vals;
            for (unsigned int i = 0; i < e.size(); i++) {
                vals.push_back(MIR::LValue::new_Argument(i));
            }

            // Create the variant
            // - Use `emit_statement` to avoid re-writing the enum tag handling
            emit_statement(*m_mir_res, ::MIR::Statement::make_Assign({::MIR::LValue::new_Return(), ::MIR::RValue::make_EnumVariant({p.clone(), static_cast<unsigned>(var_idx), mv$(vals)})}));
            m_of << "\treturn rv;\n";
            m_of << "}\n";
            m_mir_res = nullptr;
        }

        void emit_constructor_struct(const Span& sp, const ::HIR::GenericPath& p, const ::HIR::Struct& item) override {
            TRACE_FUNCTION_F(p);
            ::HIR::TypeRef tmp;
            MonomorphStatePtr ms(m_crate.m_types, nullptr, &p.m_params, nullptr);
            auto monomorph = [&](const auto& x) {
                return m_resolve.monomorph_expand_opt(sp, tmp, x, ms);
            };

            // Crate constructor function
            const auto& e = item.m_data.as_Tuple();
            m_of << "static struct s_" << TransMangle(p) << " " << TransMangle(p) << "(";
            for (unsigned int i = 0; i < e.size(); i++) {
                if (i != 0) {
                    m_of << ", ";
                }
                const auto& ty = monomorph(e[i].ent);
                emit_ctype(ty, FMT_CB(ss, ss << "_" << i;));
            }
            m_of << ") {\n";
            m_of << "\tstruct s_" << TransMangle(p) << " rv = {";
            bool emitted = false;
            for (unsigned int i = 0; i < e.size(); i++) {
                const auto& ty = monomorph(e[i].ent);
                if (this->type_is_bad_zst(ty)) {
                    continue;
                }
                if (emitted) {
                    m_of << ",";
                }
                emitted = true;
                m_of << "\n\t\t_" << i;
            }
            if (!emitted) {
                m_of << "\n\t\t0";
            }
            m_of << "\n";
            m_of << "\t\t};\n";
            m_of << "\treturn rv;\n";
            m_of << "}\n";
        }

        // Returns `true` if the type is pointer-aligned (i.e. it could contain a pointer)
        bool emit_static_ty(const HIR::TypeData* type, const ::HIR::Path& p, bool is_proto) {
            size_t size = 0, align = 0;
            TargetGetSizeAndAlignOf(sp, m_resolve, type, size, align);
            bool rv = (align * 8 >= TargetGetCurSpec().m_arch.m_pointer_bits);
            m_of << "union u_static_" << TransMangle(p);
            if (is_proto) {
                m_of << "{ ";
                emit_ctype(type, FMT_CB(ss, ss << "val";));
                m_of << "; ";
                if (rv) {
                    m_of << "uintptr_t raw[" << (size / (TargetGetCurSpec().m_arch.m_pointer_bits / 8)) << "];";
                } else {
                    m_of << "uint8_t raw[" << size << "];";
                }
                m_of << " }";
            }
            m_of << " " << TransMangle(p);
            return rv;
        }

        void emit_static_ext(const ::HIR::Path& p, const ::HIR::Static& item, const TransParams& params) override {
            ::MIR::Function empty_fcn;
            ::MIR::TypeResolve top_mir_res {
                sp, m_resolve, FMT_CB(ss, ss << "extern static " << p;), ::HIR::TypeRef(), {}, empty_fcn
            };
            m_mir_res = &top_mir_res;
            TRACE_FUNCTION_F(p);
            auto type = params.monomorph(m_resolve, item.m_type);

            // LLVM supports prepending a symbol name with \1 to prevent further mangling.
            // Since we're targeting C, not LLVM, strip off this prefix.
            std::string linkage_name = item.m_linkage.name;
            if (!linkage_name.empty() && linkage_name[0] == '\1') {
                linkage_name = linkage_name.substr(1);
            }

            if (item.m_linkage.type == HIR::Linkage::Type::ExternWeak) {
                ASSERT_BUG(sp, linkage_name != "", "");
                m_of << "extern char ";
                m_of << "__attribute__((weak)) ";

                m_of << linkage_name << "[0];\n";

                emit_static_ty(type, p, /*is_proto=*/true);
                m_of << " = { .raw = { (uintptr_t)" << linkage_name << " } };";
                m_of << "\t// static " << p << " : " << type;
                m_of << "\n";
                return;
            }

            if (linkage_name != "") {
                // Handled with asm() later

            }

            m_of << "extern ";
            emit_static_ty(type, p, /*is_proto=*/true);
            if (linkage_name != "") {
                if (TargetGetCurSpec().m_os_name == "macos") { // Not macOS only, but all Apple platforms.
                    m_of << " asm(\"_" << linkage_name << "\")";
                } else {
                    m_of << " asm(\"" << linkage_name << "\")";
                }
            }
            m_of << ";";
            m_of << "\t// static " << p << " : " << type;
            m_of << "\n";

            m_mir_res = nullptr;
        }

        void emit_static_proto(const ::HIR::Path& p, const ::HIR::Static& item, const TransParams& params) override {
            ::MIR::Function empty_fcn;
            ::MIR::TypeResolve top_mir_res {
                sp, m_resolve, FMT_CB(ss, ss << "static " << p;), ::HIR::TypeRef(), {}, empty_fcn
            };
            m_mir_res = &top_mir_res;

            TRACE_FUNCTION_F(p);
            auto type = params.monomorph(m_resolve, item.m_type);
            switch (item.m_linkage.type) {
                case HIR::Linkage::Type::External:
                    break;
                case HIR::Linkage::Type::Auto:
                    break;
                case HIR::Linkage::Type::Weak:
                    m_of << "__attribute__((weak)) ";

                    break;
                case HIR::Linkage::Type::ExternWeak:
                    m_of << "__attribute__((weak_import)) ";

                    break;
            }
            if (item.m_linkage.section != "") {
                m_of << "__attribute__((section(\"" << item.m_linkage.section << "\"))) ";

            }
            if (item.m_params.is_generic()) {
                m_of << "__attribute__((weak)) ";

            }
            m_of << "extern ";
            emit_static_ty(type, p, /*is_proto=*/true);
            m_of << ";";
            m_of << "\t// static " << p << " : " << type;
            m_of << "\n";

            m_mir_res = nullptr;
        }

        void emit_static_local(const ::HIR::Path& p, const ::HIR::Static& item, const TransParams& params, const EncodedLiteral& encoded) override {
            ::MIR::Function empty_fcn;
            ::MIR::TypeResolve top_mir_res {
                sp, m_resolve, FMT_CB(ss, ss << "static " << p;), ::HIR::TypeRef(), {}, empty_fcn
            };
            m_mir_res = &top_mir_res;

            TRACE_FUNCTION_F(p);

            auto type = params.monomorph(m_resolve, item.m_type);
            const bool is_zero = is_zero_literal(type, encoded, params);
            if (item.m_params.is_generic()) {
                m_of << "__attribute__((weak)) ";

            }
            bool is_packed = emit_static_ty(type, p, /*is_proto=*/false);
            m_of << " = ";

            if (is_zero) {
                m_of << "{}";
            } else {
                m_of << "{ .raw = {";
                if (is_packed) {
                    DEBUG("encoded.bytes = `" << FMT_CB(ss, for (auto& b : encoded.bytes) ss << std::setw(2) << std::setfill('0') << std::hex << unsigned(b) << (int(&b - encoded.bytes.data()) % 8 == 7 ? " " : "");) << "`");
                    DEBUG("encoded.relocations = " << encoded.relocations);
                    auto reloc_it = encoded.relocations.begin();
                    auto ptr_size = TargetGetCurSpec().m_arch.m_pointer_bits / 8;
                    for (size_t i = 0; i < encoded.bytes.size(); i += ptr_size) {
                        uint64_t v = 0;
                        if (TargetGetCurSpec().m_arch.m_big_endian) {
                            for (size_t o = 0, j = ptr_size; j--; o++) {
                                v |= static_cast<uint64_t>(encoded.bytes[i + o]) << (j * 8);
                            }
                        } else {
                            for (size_t o = 0, j = 0; j < ptr_size; j++, o++) {
                                v |= static_cast<uint64_t>(encoded.bytes[i + o]) << (j * 8);
                            }
                        }

                        if (i > 0) {
                            m_of << ",";
                        }

                        if (reloc_it != encoded.relocations.end() && reloc_it->ofs <= i) {
                            MIR_ASSERT(*m_mir_res, reloc_it->ofs == i, "Relocation not aligned to a pointer - " << reloc_it->ofs << " != " << i);
                            MIR_ASSERT(*m_mir_res, reloc_it->len == ptr_size, "Relocation size not pointer size - " << reloc_it->len << " != " << ptr_size);
                            v -= EncodedLiteral::PTR_BASE;
                            //MIR_ASSERT(*m_mir_res, v == 0, "TODO: Relocation with non-zero offset " << i << ": v=0x" << std::hex << v << std::dec << " Reloc=" << *reloc_it << " Literal=" << encoded);

                            m_of << "(uintptr_t)";
                            if (reloc_it->p) {
                                if (reloc_it->p->m_data.is_UfcsInherent() && reloc_it->p->m_data.as_UfcsInherent().item == "#type_id") {
                                    const auto& ty = reloc_it->p->m_data.as_UfcsInherent().type;
                                    m_of << "&__typeid_" << TransMangle(ty);
                                } else {
                                    m_of << "&" << TransMangle(*reloc_it->p);
                                }
                            } else {
                                this->print_escaped_string(reloc_it->bytes);
                            }
                            if (v > 0) {
                                m_of << "+" << v;
                            }

                            ++reloc_it;
                        } else {
                            m_of << "0x" << std::hex << v << "ull" << std::dec;
                        }
                    }
                } else {
                    MIR_ASSERT(*m_mir_res, encoded.relocations.empty(), "Non-pointer-aligned data with relocations");
                    bool e = false;
                    m_of << std::dec;
                    for (auto b : encoded.bytes) {
                        if (e) {
                            m_of << ",";
                        }
                        m_of << int(b); // Just leave it as decimal
                        e = true;
                    }
                }
                m_of << "} }";
            }
            m_of << ";";
            m_of << "\t// static " << p << " : " << type << " = " << encoded;
            m_of << "\n";
            m_mir_res = nullptr;
        }

        void emit_float(FloatValue v, HIR::CoreType ty) {
            if (ty == HIR::CoreType::F16) {
                m_of << "f16_disabled()";
            } else if (ty == HIR::CoreType::F128) {
                const F128 bits(v);
                m_of << "make_f128_bits(0x" << ::std::hex << bits.hi << "ull, 0x" << bits.lo << "ull)" << ::std::dec;
            } else if (float_value_is_nan(v)) {
                m_of << (ty == HIR::CoreType::F32 ? "__builtin_nanf(\"\")" : "__builtin_nan(\"\")");
            } else if (float_value_is_infinite(v)) {
                m_of << (v < 0 ? "-" : "");
                m_of << (ty == HIR::CoreType::F32 ? "__builtin_inff()" : "__builtin_inf()");
            } else {
                if (ty == HIR::CoreType::F32) {
                    m_of.precision(::std::numeric_limits<float>::max_digits10 + 1);
                    m_of << ::std::scientific << v << "f";
                } else {
                    m_of.precision(::std::numeric_limits<double>::max_digits10 + 1);
                    m_of << ::std::scientific << v;
                }
            }
        }

        void print_escaped_string(const std::string& s) {
            print_escaped_string_inner(s.c_str(), s.c_str() + s.size());
        }

        void print_escaped_string(const std::vector<uint8_t>& s) {
            const char* start = reinterpret_cast<const char*>(s.data());
            print_escaped_string_inner(start, start + s.size());
        }

        void print_escaped_string_inner(const char* start, const char* end) {
            const unsigned MAX_STRING_LEN = 16380 / 3 - 10;
            m_of << "\"" << ::std::hex;
            unsigned n_ch = 0;
            while (start != end) {
                const char v = *start++;
                switch (v) {
                    case '"':
                        m_of << "\\\"";
                        break;
                    case '\\':
                        m_of << "\\\\";
                        break;
                    case '\n':
                        m_of << "\\n";
                        break;
                    case '?':
                        if (end - start >= 2 && start[0] == '?') {
                            if (start[1] == '!') {
                                // Trigraph! Needs an escape in it.
                                m_of << v;
                                m_of << "\"\"";
                                n_ch = 0;
                                break;
                            }
                        }
                        // Fall through
                    default:
                        if (' ' <= v && static_cast<uint8_t>(v) < 0x7F) {
                            m_of << v;
                        } else {
                            if (static_cast<uint8_t>(v) < 16) {
                                m_of << "\\x0" << (unsigned int)static_cast<uint8_t>(v);
                            } else {
                                m_of << "\\x" << (unsigned int)static_cast<uint8_t>(v);
                            }
                            // If the next character is a hex digit, close/reopen the string.
                            if (start != end && isxdigit(static_cast<unsigned char>(*start))) {
                                m_of << "\"\"";
                                n_ch = 0;
                            }
                        }
                }
                n_ch++;
                if (n_ch == MAX_STRING_LEN) {
                    m_of << "\"\"";
                    n_ch = 0;
                }
            }
            m_of << "\"" << ::std::dec;
        }

        void emit_function_ext(const ::HIR::Path& p, const ::HIR::Function& item, const TransParams& params) override {
            ::MIR::Function empty_fcn;
            ::MIR::TypeResolve top_mir_res {
                sp, m_resolve, FMT_CB(ss, ss << "extern fn " << p;), ::HIR::TypeRef(), {}, empty_fcn
            };
            m_mir_res = &top_mir_res;
            TRACE_FUNCTION_F(p);

            m_of << "// EXTERN extern \"" << item.m_abi << "\" " << p << "\n";
            if (item.m_linkage.name.rfind("llvm.", 0) == 0) {
                m_of << "static ";
                emit_function_header(p, item, params);
                m_of << "{\n";
                m_of << "\t";
                emit_ctype(item.m_return);
                m_of << " rv;\n";

                if (item.m_linkage.name == "llvm.prefetch") {
                    m_of << "\tif(arg1) {\n"
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
                else if (item.m_linkage.name == "llvm.x86.ssse3.pshuf.b.128") {
                    m_of << "\tconst uint8_t* src = (const uint8_t*)&arg0;\n"
                         << "\tconst uint8_t* mask = (const uint8_t*)&arg1;\n"
                         << "\tuint8_t* dst = (uint8_t*)&rv;\n"
                         << "\tfor(int i = 0; i < " << 128 / 8 << "; i ++) dst[i] = (mask[i] < 0x80 ? src[mask[i] & 0xF] : 0);\n"
                         << "\treturn rv;\n";
                } else if (item.m_linkage.name == "llvm.x86.avx2.pshuf.b") {
                    m_of << "\tconst uint8_t* src = (const uint8_t*)&arg0;\n"
                         << "\tconst uint8_t* mask = (const uint8_t*)&arg1;\n"
                         << "\tuint8_t* dst = (uint8_t*)&rv;\n"
                         << "\tfor(int i = 0; i < " << 256 / 8 << "; i ++) dst[i] = (mask[i] < 0x80 ? src[(i & 16) | (mask[i] & 0xF)] : 0);\n"
                         << "\treturn rv;\n";
                }
                // Multiply-add intrinsics used by simd-adler32 (via png's flate2)
                else if (item.m_linkage.name == "llvm.x86.ssse3.pmadd.ub.sw.128" || item.m_linkage.name == "llvm.x86.avx2.pmadd.ub.sw") {
                    int n = (item.m_linkage.name == "llvm.x86.avx2.pmadd.ub.sw" ? 32 : 16);
                    m_of << "\tconst uint8_t* a = (const uint8_t*)&arg0;\n"
                         << "\tconst int8_t* b = (const int8_t*)&arg1;\n"
                         << "\tint16_t* dst = (int16_t*)&rv;\n"
                         << "\tfor(int i = 0; i < " << n / 2 << "; i ++) {\n"
                         << "\t\tint32_t v = (int32_t)a[2*i]*b[2*i] + (int32_t)a[2*i+1]*b[2*i+1];\n"
                         << "\t\tdst[i] = (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));\n"
                         << "\t}\n"
                         << "\treturn rv;\n";
                } else if (item.m_linkage.name == "llvm.x86.sse2.pmadd.wd" || item.m_linkage.name == "llvm.x86.avx2.pmadd.wd") {
                    int n = (item.m_linkage.name == "llvm.x86.avx2.pmadd.wd" ? 16 : 8);
                    m_of << "\tconst int16_t* a = (const int16_t*)&arg0;\n"
                         << "\tconst int16_t* b = (const int16_t*)&arg1;\n"
                         << "\tint32_t* dst = (int32_t*)&rv;\n"
                         << "\tfor(int i = 0; i < " << n / 2 << "; i ++) dst[i] = (int32_t)a[2*i]*b[2*i] + (int32_t)a[2*i+1]*b[2*i+1];\n"
                         << "\treturn rv;\n";
                } else if (item.m_linkage.name == "llvm.x86.sse2.psad.bw" || item.m_linkage.name == "llvm.x86.avx2.psad.bw") {
                    int n = (item.m_linkage.name == "llvm.x86.avx2.psad.bw" ? 32 : 16);
                    m_of << "\tconst uint8_t* a = (const uint8_t*)&arg0;\n"
                         << "\tconst uint8_t* b = (const uint8_t*)&arg1;\n"
                         << "\tuint64_t* dst = (uint64_t*)&rv;\n"
                         << "\tfor(int k = 0; k < " << n / 8 << "; k ++) {\n"
                         << "\t\tuint64_t sum = 0;\n"
                         << "\t\tfor(int j = 0; j < 8; j ++) { int d = (int)a[k*8+j] - (int)b[k*8+j]; sum += (d < 0 ? -d : d); }\n"
                         << "\t\tdst[k] = sum;\n"
                         << "\t}\n"
                         << "\treturn rv;\n";
                } else if (item.m_linkage.name == "llvm.x86.sse2.psrli.d") {
                    m_of << "\tconst uint32_t* src = (const uint32_t*)&arg0;\n"
                         << "\tuint32_t* dst = (uint32_t*)&rv;\n"
                         << "\tfor(int i = 0; i < " << 128 / 32 << "; i ++) dst[i] = src[i] >> arg1;\n"
                         << "\treturn rv;\n";
                } else if (item.m_linkage.name == "llvm.x86.sse2.pslli.d") {
                    m_of << "\tconst uint32_t* src = (const uint32_t*)&arg0;\n"
                         << "\tuint32_t* dst = (uint32_t*)&rv;\n"
                         << "\tfor(int i = 0; i < " << 128 / 32 << "; i ++) dst[i] = src[i] << arg1;\n"
                         << "\treturn rv;\n";
                } else if (item.m_linkage.name == "llvm.x86.sse2.pmovmskb.128") {
                    m_of << "\tconst uint8_t* src = (const uint8_t*)&arg0;\n"
                         << "\tuint8_t* dst = (uint8_t*)&rv; *dst = 0;\n"
                         << "\tfor(int i = 0; i < " << 128 / 8 << "; i ++) *dst |= (src[i] >> 7) << i;\n"
                         << "\treturn rv;\n";
                } else if (item.m_linkage.name == "llvm.x86.sse2.storeu.dq") {
                    m_of << "\tmemcpy(arg0, &arg1, sizeof(arg1));\n";
                }
                // SHA-NI: the sha2 crate takes this path when runtime detection
                // reports hardware support; portable C keeps it correct.
                else if (item.m_linkage.name == "llvm.x86.sha256rnds2") {
                    m_of << "\tconst uint32_t* st_cdgh = (const uint32_t*)&arg0;\n"
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
                } else if (item.m_linkage.name == "llvm.x86.sha256msg1") {
                    m_of << "\tconst uint32_t* w = (const uint32_t*)&arg0;\n"
                         << "\tconst uint32_t* w2 = (const uint32_t*)&arg1;\n"
                         << "\tuint32_t* dst = (uint32_t*)&rv;\n"
                         << "\tfor(int i = 0; i < 4; i ++) {\n"
                         << "\t\tuint32_t x = (i < 3 ? w[i+1] : w2[0]);\n"
                         << "\t\tdst[i] = w[i] + ((x >> 7 | x << 25) ^ (x >> 18 | x << 14) ^ (x >> 3));\n"
                         << "\t}\n"
                         << "\treturn rv;\n";
                } else if (item.m_linkage.name == "llvm.x86.sha256msg2") {
                    m_of << "\tconst uint32_t* w = (const uint32_t*)&arg0;\n"
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
                else if (item.m_linkage.name == "llvm.x86.addcarry.32") {
                    m_of << "\trv._0 = __builtin_add_overflow(arg1, arg2, &rv._1);\n";
                    m_of << "\tif(arg0) rv._0 |= __builtin_add_overflow(rv._1, 1, &rv._1);\n";
                    m_of << "\treturn rv;\n";
                }
                // `fn llvm_addcarryx_u32(a: u8, b: u32, c: u32, d: *mut u8) -> u8`
                else if (item.m_linkage.name == "llvm.x86.addcarryx.u32") {
                    m_of << "\trv = __builtin_add_overflow(arg1, arg2, (uint32_t*)arg3);\n";
                    m_of << "\tif(arg0) rv |= __builtin_add_overflow(*arg3, 1, (uint32_t*)arg3);\n";
                    m_of << "\treturn rv;\n";
                }
                // `fn llvm_subborrow(a: u8, b: u32, c: u32) -> (u8, u32);`
                else if (item.m_linkage.name == "llvm.x86.subborrow.32") {
                    m_of << "\trv._0 = __builtin_sub_overflow(arg1, arg2, &rv._1);\n";
                    m_of << "\tif(arg0) rv._0 |= __builtin_sub_overflow(rv._1, 1, &rv._1);\n";
                    m_of << "\treturn rv;\n";
                } else if (item.m_linkage.name == "llvm.x86.xgetbv") {
                    m_of << "\tuint32_t lo, hi;\n";
                    m_of << "\t__asm__ __volatile__ (\"xgetbv\" : \"=a\" (lo), \"=d\" (hi) : \"c\" (arg0) );\n";
                    m_of << "\treturn lo | ((uint64_t)hi << 32);\n";

                } else if (item.m_linkage.name == "llvm.x86.sse2.pause") {
                    // Just a `PAUSE` instruciton, which is effectively a nop
                    m_of << "\t__asm__ __volatile__ (\"pause\");\n";

                    m_of << "\treturn ;\n";
                }
                // AES functions
                else if (item.m_linkage.name.rfind("llvm.x86.aesni.", 0) == 0) {
                    m_of << "\tassert(!\"Unsupprorted LLVM x86 intrinsic: " << item.m_linkage.name << "\"); abort();\n";
                } else {
                    // TODO: Hand off to compiler-specific intrinsics
                    //MIR_TODO(*m_mir_res, "LLVM extern linkage: " << item.m_linkage.name);
                    m_of << "\tassert(!\"Extern LLVM: " << item.m_linkage.name << "\"); abort();\n";
                }
                m_of << "}\n";
                m_mir_res = nullptr;
                return;
            } else if (item.m_linkage.name == "_Unwind_RaiseException") {
                m_of << "// - Magic compiler impl\n";
                m_of << "static ";
                emit_function_header(p, item, params);
                m_of << " {\n";
                m_of << "\tthrow mrustc_panic{arg0};\n";
                m_of << "}\n";
                return;
            } else {
                m_of << "extern ";
            }
            emit_function_header(p, item, params);
            if (item.m_linkage.name != "") {
                if (TargetGetCurSpec().m_os_name == "macos") { // Not macOS only, but all Apple platforms.
                    m_of << " asm(\"_" << item.m_linkage.name << "\")";
                } else {
                    m_of << " asm(\"" << item.m_linkage.name << "\")";
                }

            }
            m_of << ";\n";

            m_mir_res = nullptr;
        }

        void emit_function_proto(const ::HIR::Path& p, const ::HIR::Function& item, const TransParams& params, bool is_extern_def) override {
            ::MIR::Function empty_fcn;
            ::MIR::TypeResolve top_mir_res {
                sp, m_resolve, FMT_CB(ss, ss << "/*proto*/ fn " << p;), ::HIR::TypeRef(), {}, empty_fcn
            };
            m_mir_res = &top_mir_res;

            TRACE_FUNCTION_F(p);
            m_of << "// PROTO extern \"" << item.m_abi << "\" " << p << "\n";
            if (item.m_linkage.name != "") {
                // If this function is implementing an external ABI, just rename it.
                m_of << "#define " << TransMangle(p) << " " << item.m_linkage.name << "\n";
            }
            if (is_extern_def) {
                m_of << "static ";
            }
            switch (item.m_linkage.type) {
                case HIR::Linkage::Type::External:
                    break;
                case HIR::Linkage::Type::Auto:
                    break;
                case HIR::Linkage::Type::Weak:
                    m_of << "__attribute__((weak)) ";

                    break;
                case HIR::Linkage::Type::ExternWeak:
                    BUG(Span(), "unexpected ExternWeak on function");
            }
            emit_function_header(p, item, params);
            m_of << ";\n";

            m_mir_res = nullptr;
        }

        void emit_function_code(const ::HIR::Path& p, const ::HIR::Function& item, const TransParams& params, bool is_extern_def, const ::MIR::FunctionPointer& code) override {
            TRACE_FUNCTION_F(p);

            ::MIR::TypeResolve::args_t arg_types;
            for (const auto& ent : item.m_args) {
                arg_types.push_back(::std::make_pair(::HIR::Pattern{}, params.monomorph(m_resolve, ent.second)));
            }

            ::HIR::TypeRef ret_type_tmp;
            const auto& ret_type = monomorphise_fcn_return(ret_type_tmp, item, params);

            ::MIR::TypeResolve mir_res {
                sp, m_resolve, FMT_CB(ss, ss << p;), ret_type, arg_types, *code
            };
            m_mir_res = &mir_res;

            m_of << "// " << p << "\n";
            if (is_extern_def) {
                m_of << "static ";
            }
            emit_function_header(p, item, params);
            m_of << "\n";
            m_of << "{\n";


            // Variables
            m_of << "\t";
            emit_ctype(ret_type, FMT_CB(ss, ss << "rv";));
            m_of << ";\n";
            for (unsigned int i = 0; i < code->locals.size(); i++) {
                // If the type is a ZST, initialise it (to avoid warnings)
                if (this->type_is_bad_zst(code->locals[i])) {
                    continue;
                }
                DEBUG("var" << i << " : " << code->locals[i]);
                m_of << "\t";
                emit_ctype(code->locals[i], FMT_CB(ss, ss << "var" << i;));
                m_of << ";";
                m_of << "\t// " << code->locals[i];
                m_of << "\n";
            }
            for (unsigned int i = 0; i < code->drop_flags.size(); i++) {
                m_of << "\tbool df" << i << " = " << code->drop_flags[i] << ";\n";
            }

            ::std::set<unsigned> cleanup_blocks;
            ::std::vector<unsigned> pending_cleanup_blocks;
            for (const auto& block : code->blocks) {
                TU_MATCH_HDRA((block.terminator), {)
                    TU_ARMA(Drop, e) {
                        if (const auto* target = e.unwind.opt_Cleanup()) {
                            pending_cleanup_blocks.push_back(*target);
                        }
                    }
                    TU_ARMA(Call, e) {
                        if (const auto* target = e.unwind.opt_Cleanup()) {
                            pending_cleanup_blocks.push_back(*target);
                        }
                    }
                    default: break;
                }
            }
            while (!pending_cleanup_blocks.empty()) {
                const auto block_index = pending_cleanup_blocks.back();
                pending_cleanup_blocks.pop_back();
                MIR_ASSERT(mir_res, block_index < code->blocks.size(), "Cleanup target BB" << block_index << " is out of range");
                if (!cleanup_blocks.insert(block_index).second) {
                    continue;
                }
                ::MIR::visit::visit_terminator_target(code->blocks[block_index].terminator, [&](const auto& target) {
                    pending_cleanup_blocks.push_back(target);
                });
            }
            if (!cleanup_blocks.empty()) {
                emit_cleanup_runner(mir_res, cleanup_blocks);
            }

            for (unsigned i = 0; i < code->blocks.size(); i++) {
                const auto& block = code->blocks[i];
                if (cleanup_blocks.count(i) != 0) {
                    continue;
                }
                m_of << "bb" << i << ": {\n";
                for (const auto& stmt : block.statements) {
                    mir_res.set_cur_stmt(i, &stmt - block.statements.data());
                    emit_statement(mir_res, stmt, 1);
                }
                mir_res.set_cur_stmt_term(i);
                emit_block_terminator(mir_res, block.terminator, i, false, 1);
                m_of << "}\n";
            }
            m_of << "}\n";
            m_of.flush();
            m_mir_res = nullptr;
            m_mir_res = nullptr;
        }

        void emit_operation_with_unwind(const ::MIR::UnwindAction& action, unsigned indent_level, ::std::function<void(unsigned)> emit_operation) {
            auto indent = RepeatLitStr{"\t", static_cast<int>(indent_level)};
            TU_MATCH_HDRA((action), {)
                TU_ARMA(Continue, _) {
                    emit_operation(indent_level);
                }
                TU_ARMA(Cleanup, target) {
                    m_of << indent << "try {\n";
                    emit_operation(indent_level + 1);
                    m_of << indent << "} catch (...) {\n";
                    m_of << indent << "\ttry { mrustc_run_cleanup(" << target << "); } catch (...) { abort(); }\n";
                    m_of << indent << "\tthrow;\n";
                    m_of << indent << "}\n";
                }
                TU_ARMA(Terminate, _) {
                    m_of << indent << "try {\n";
                    emit_operation(indent_level + 1);
                    m_of << indent << "} catch (...) { abort(); }\n";
                }
                TU_ARMA(Unreachable, _) {
                    m_of << indent << "try {\n";
                    emit_operation(indent_level + 1);
                    m_of << indent << "} catch (...) { abort(); }\n";
                }
            }
        }

        void emit_block_terminator(::MIR::TypeResolve& mir_res, const ::MIR::Terminator& term, unsigned block_index, bool cleanup, unsigned indent_level) {
            auto indent = RepeatLitStr{"\t", static_cast<int>(indent_level)};
            auto emit_target = [&](unsigned target) {
                m_of << indent << "goto " << (cleanup ? "cleanup_bb" : "bb") << target << ";\n";
            };
            TU_MATCH_HDRA((term), {)
                TU_ARMA(Incomplete, _) {
                    m_of << indent << "abort();\n";
                }
                TU_ARMA(Return, _) {
                    if (cleanup) {
                        m_of << indent << "abort();\n";
                    } else if (mir_res.m_ret_type == m_crate.m_types.unit()) {
                        m_of << indent << "return;\n";
                    } else {
                        m_of << indent << "return rv;\n";
                    }
                }
                TU_ARMA(UnwindResume, _) {
                    if (cleanup) {
                        m_of << indent << "return;\n";
                    } else {
                        m_of << indent << "abort();\n";
                    }
                }
                TU_ARMA(UnwindTerminate, _) {
                    m_of << indent << "abort();\n";
                }
                TU_ARMA(Unreachable, _) {
                    m_of << indent << "abort();\n";
                }
                TU_ARMA(Goto, target) {
                    emit_target(target);
                }
                TU_ARMA(If, e) {
                    m_of << indent << "if(";
                    emit_lvalue(e.cond);
                    m_of << ") goto " << (cleanup ? "cleanup_bb" : "bb") << e.bb_true;
                    m_of << "; else goto " << (cleanup ? "cleanup_bb" : "bb") << e.bb_false << ";\n";
                }
                TU_ARMA(Switch, e) {
                    if (e.valid_flag != ~0u) {
                        m_of << indent << "if(!df" << e.valid_flag << ") goto " << (cleanup ? "cleanup_bb" : "bb") << e.invalid_target << ";\n";
                    }
                    emit_term_switch(mir_res, e.val, e.targets.size(), indent_level, [&](size_t idx) {
                        m_of << "goto " << (cleanup ? "cleanup_bb" : "bb") << e.targets[idx] << ";";
                    });
                }
                TU_ARMA(SwitchValue, e) {
                    emit_term_switchvalue(mir_res, e.val, e.values, indent_level, [&](size_t idx) {
                        const auto target = idx == SIZE_MAX ? e.def_target : e.targets[idx];
                        m_of << "goto " << (cleanup ? "cleanup_bb" : "bb") << target << ";";
                    });
                }
                TU_ARMA(Drop, e) {
                    emit_operation_with_unwind(e.unwind, indent_level, [&](unsigned operation_indent) {
                        emit_drop_operation(mir_res, e, operation_indent);
                    });
                    emit_target(e.target);
                }
                TU_ARMA(Call, e) {
                    emit_operation_with_unwind(e.unwind, indent_level, [&](unsigned operation_indent) {
                        emit_term_call(mir_res, e, operation_indent);
                    });
                    emit_target(e.ret_block);
                }
            }
            m_of << indent << "// ^ " << term << "\n";
            (void)block_index;
        }

        void emit_cleanup_runner(::MIR::TypeResolve& mir_res, const ::std::set<unsigned>& cleanup_blocks) {
            m_of << "\tauto mrustc_run_cleanup = [&](unsigned mrustc_cleanup_entry) {\n";
            m_of << "\t\tswitch(mrustc_cleanup_entry) {\n";
            for (auto block : cleanup_blocks) {
                m_of << "\t\tcase " << block << ": goto cleanup_bb" << block << ";\n";
            }
            m_of << "\t\tdefault: abort();\n";
            m_of << "\t\t}\n";
            for (auto block_index : cleanup_blocks) {
                const auto& block = mir_res.m_fcn.blocks.at(block_index);
                m_of << "\tcleanup_bb" << block_index << ": {\n";
                for (const auto& stmt : block.statements) {
                    mir_res.set_cur_stmt(block_index, &stmt - block.statements.data());
                    emit_statement(mir_res, stmt, 2);
                }
                mir_res.set_cur_stmt_term(block_index);
                emit_block_terminator(mir_res, block.terminator, block_index, true, 2);
                m_of << "\t}\n";
            }
            m_of << "\t};\n";
        }
        bool type_is_emulated_i128(const ::HIR::TypeData* ty) const {
            return m_options.emulated_i128 && (ty == ::HIR::CoreType::I128 || ty == ::HIR::CoreType::U128);
        }

        // Returns true if the input type is a ZST and ZSTs are not being emitted
        bool type_is_bad_zst(const ::HIR::TypeData* ty) const {
            if (m_options.disallow_empty_structs) {
                // TODO: Extern types are also ZSTs?
                size_t size, align;
                // NOTE: Uses the Size+Align version because that doesn't panic on unsized
                MIR_ASSERT(*m_mir_res, TargetGetSizeAndAlignOf(sp, m_resolve, ty, size, align), "Unexpected generic? " << ty);
                return size == 0;
            } else {
                return false;
            }
        }

        bool lvalue_is_bad_zst(const ::MIR::LValue& lv) const {
            if (m_options.disallow_empty_structs) {
                HIR::TypeRef tmp;
                return type_is_bad_zst(m_mir_res->get_lvalue_type(tmp, lv));
            } else {
                return false;
            }
        }

        // Locals whose complete Rust type is a ZST aren't emitted in C.  A
        // projection from such a local has no C lvalue to take the address of.
        bool lvalue_root_is_bad_zst(const ::MIR::LValue& lv) const {
            if (m_options.disallow_empty_structs) {
                HIR::TypeRef tmp;
                return type_is_bad_zst(m_mir_res->get_lvalue_type(tmp, lv, lv.m_wrappers.size()));
            } else {
                return false;
            }
        }

        // An index into a zero-sized array is represented by the array's
        // address, never by a C `DATA` field (such fields are omitted).  Peel
        // nested zero-sized array projections to their materialized backing
        // lvalue before taking that address.
        ::MIR::LValue lvalue_zst_index_backing(const ::MIR::LValue& lv) const {
            auto rv = lv.clone();
            while (::MIR::LValue::CRef(rv).is_Index()) {
                auto inner = ::MIR::LValue::CRef(rv).inner_ref();
                HIR::TypeRef tmp;
                if (!this->type_is_bad_zst(m_mir_res->get_lvalue_type(tmp, inner))) {
                    break;
                }
                rv.m_wrappers.pop_back();
            }
            return rv;
        }

        void emit_borrow(const ::MIR::TypeResolve& mir_res, HIR::BorrowType bt, const MIR::LValue& val) {
            ::HIR::TypeRef tmp;
            const auto& ty = mir_res.get_lvalue_type(tmp, val);

            if (this->type_is_bad_zst(ty) && !this->lvalue_root_is_bad_zst(val)) {
                auto backing = this->lvalue_zst_index_backing(val);
                if (backing.m_wrappers.size() != val.m_wrappers.size()) {
                    emit_borrow(mir_res, bt, backing);
                    return;
                }
            }

            bool special = false;
            // If the inner value was a deref, just copy the pointer verbatim
            if (val.is_Deref()) {
                emit_lvalue(::MIR::LValue::CRef(val).inner_ref());
                special = true;
            }
            // Magic for taking a &-ptr to unsized field of a struct.
            // - Needs to get metadata from bottom-level pointer.
            else if (val.is_Field()) {
                auto meta_ty = metadata_type(ty);
                if (meta_ty != MetadataType::None) {
                    auto base_val = ::MIR::LValue::CRef(val).inner_ref();
                    while (base_val.is_Field()) {
                        base_val.try_unwrap();
                    }
                    MIR_ASSERT(mir_res, base_val.is_Deref(), "DST access must be via a deref");
                    const auto base_ptr = base_val.inner_ref();

                    // Construct the new DST
                    switch (meta_ty) {
                        case MetadataType::None:
                            throw "";
                        case MetadataType::Unknown:
                            MIR_BUG(mir_res, "");
                        case MetadataType::Zero:
                            MIR_BUG(mir_res, "");
                        case MetadataType::Slice:
                            m_of << "make_sliceptr(";
                            break;
                        case MetadataType::TraitObject:
                            m_of << "make_traitobjptr(";
                            break;
                    }
                    if (meta_ty == MetadataType::TraitObject) {
                        ::HIR::TypeRef base_tmp;
                        const auto& base_ty = mir_res.get_lvalue_type(base_tmp, base_val.clone());
                        const auto base_param = ::MIR::Param::make_LValue(base_ptr.clone());
                        if (get_inner_unsized_type(base_ty)->is_TraitObject()) {
                            const auto* cur_ty = &base_ty;
                            m_of << "(uint8_t*)";
                            emit_lvalue(base_ptr);
                            m_of << ".PTR + ";
                            for (size_t i = base_val.wrapper_count(); i < val.m_wrappers.size(); i++) {
                                const auto& wrapper = val.m_wrappers[i];
                                MIR_ASSERT(mir_res, wrapper.is_Field(), "Unexpected DST lvalue wrapper - " << val);
                                if (i != base_val.wrapper_count()) {
                                    m_of << " + ";
                                }
                                emit_trait_object_dst_field_offset(*cur_ty, wrapper.as_Field(), base_param);
                                const auto* repr = TargetGetTypeRepr(sp, m_resolve, *cur_ty);
                                MIR_ASSERT(mir_res, repr && wrapper.as_Field() < repr->fields.size(), "Invalid DST field - " << val);
                                cur_ty = &repr->fields[wrapper.as_Field()].ty;
                            }
                        } else {
                            m_of << "&";
                            emit_lvalue(val);
                        }
                    } else {
                        m_of << "&";
                        emit_lvalue(val);
                    }
                    m_of << ", ";
                    emit_lvalue(base_ptr);
                    m_of << ".META)";
                    special = true;
                }
            } else {
            }

            // NOTE: If disallow_empty_structs is set, structs don't include ZST fields
            // In this case, we need to avoid mentioning the removed fields
            auto val_ref = ::MIR::LValue::CRef(val);
            if (!special && m_options.disallow_empty_structs && val_ref.is_Index() && this->type_is_bad_zst(ty)) {
                auto inner = val_ref.inner_ref();
                ::HIR::TypeRef tmp;
                const auto& parent_ty = mir_res.get_lvalue_type(tmp, inner);
                const ::HIR::TypeData* element_ty = nullptr;
                if (const auto* array = parent_ty->opt_Array()) {
                    element_ty = array->inner;
                } else if (const auto* slice = parent_ty->opt_Slice()) {
                    element_ty = slice->inner;
                }
                MIR_ASSERT(mir_res, element_ty, "Index of non-array type in ZST borrow path: " << parent_ty);
                size_t element_size = 0;
                MIR_ASSERT(mir_res, TargetGetSizeOf(sp, m_resolve, element_ty, element_size), "Unknown array element size for " << parent_ty);
                MIR_ASSERT(mir_res, element_size == 0, "Non-ZST element in ZST borrow path: " << element_ty);
                if (parent_ty->is_Slice()) {
                    MIR_ASSERT(mir_res, inner.is_Deref(), "Raw slice lvalue in ZST borrow path");
                    m_of << "(void*)";
                    emit_lvalue(inner.inner_ref());
                    m_of << ".PTR";
                } else {
                    m_of << "(void*)& ";
                    emit_lvalue(inner);
                }
                special = true;
            }

            if (!special && m_options.disallow_empty_structs && val.is_Field() && this->type_is_bad_zst(ty)) {
                // Work backwards to the first non-ZST field
                auto val_fp = ::MIR::LValue::CRef(val);
                assert(val_fp.is_Field());
                while (val_fp.inner_ref().is_Field()) {
                    ::HIR::TypeRef tmp;
                    const auto& ty = mir_res.get_lvalue_type(tmp, val_fp.inner_ref());
                    if (!this->type_is_bad_zst(ty)) {
                        break;
                    }
                    val_fp.try_unwrap();
                }
                assert(val_fp.is_Field());
                // Here, we have `val_fp` be a LValue::Field that refers to a ZST, but the inner of the field points to a non-ZST or a local

                // If the index is zero, then the best option is to borrow the source
                auto field_inner = val_fp.inner_ref();
                if (field_inner.is_Downcast()) {
                    m_of << "(void*)& ";
                    emit_lvalue(field_inner.inner_ref());
                } else if (val_fp.as_Field() == 0) {
                    ::HIR::TypeRef tmp;
                    const auto& parent_ty = mir_res.get_lvalue_type(tmp, field_inner);
                    if (parent_ty->is_Slice()) {
                        MIR_ASSERT(mir_res, field_inner.is_Deref(), "Raw slice lvalue in ZST borrow path");
                        m_of << "(void*)";
                        emit_lvalue(field_inner.inner_ref());
                        m_of << ".PTR";
                    } else {
                        m_of << "(void*)& ";
                        emit_lvalue(field_inner);
                    }
                } else {
                    ::HIR::TypeRef tmp;
                    const auto& parent_ty = mir_res.get_lvalue_type(tmp, field_inner);
                    const ::HIR::TypeData* element_ty = nullptr;
                    if (const auto* array = parent_ty->opt_Array()) {
                        element_ty = array->inner;
                    } else if (const auto* slice = parent_ty->opt_Slice()) {
                        element_ty = slice->inner;
                    }

                    if (element_ty) {
                        size_t element_size = 0;
                        MIR_ASSERT(mir_res, TargetGetSizeOf(sp, m_resolve, element_ty, element_size), "Unknown array element size for " << parent_ty);
                        MIR_ASSERT(mir_res, element_size == 0, "Non-ZST element in ZST borrow path: " << element_ty);
                        m_of << "(void*)( (uint8_t*)";
                        if (parent_ty->is_Slice()) {
                            MIR_ASSERT(mir_res, field_inner.is_Deref(), "Raw slice lvalue in ZST borrow path");
                            emit_lvalue(field_inner.inner_ref());
                            m_of << ".PTR";
                        } else {
                            m_of << "& ";
                            emit_lvalue(field_inner);
                        }
                        m_of << " + " << element_size * val_fp.as_Field() << ") /*ZST*/";
                    } else {
                        // Get the number of fields in parent
                        auto* repr = TargetGetTypeRepr(sp, m_resolve, parent_ty);
                        assert(repr);
                        size_t n_parent_fields = repr->fields.size();
                        // Find next non-zero field
                        auto tmp_lv = ::MIR::LValue::new_Field(field_inner.clone(), val_fp.as_Field() + 1);
                        bool found = false;
                        while (tmp_lv.as_Field() < n_parent_fields) {
                            auto idx = tmp_lv.as_Field();
                            const auto& ty = repr->fields[idx].ty;
                            if (ty->is_Path() && ty->as_Path().binding.is_ExternType()) {
                                // Extern types aren't emitted
                            } else if (this->type_is_bad_zst(ty)) {
                                // ZSTs are't either
                            } else {
                                found = true;
                                break;
                            }
                            tmp_lv.m_wrappers.back() = ::MIR::LValue::Wrapper::new_Field(idx + 1);
                        }

                        // If no non-zero fields were found before the end, then do pointer manipulation using the repr
                        if (!found) {
                            m_of << "(void*)( (uint8_t*)& ";
                            emit_lvalue(field_inner);
                            m_of << " + " << repr->fields[val_fp.as_Field()].offset << ") /*ZST*/";
                        }
                        // Otherwise, use the next non-zero field
                        else {
                            m_of << "(void*)( &";
                            emit_lvalue(tmp_lv);
                            m_of << ") /*ZST*/";
                        }
                    }
                }
                special = true;
            }

            if (!special) {
                m_of << "& ";
                emit_lvalue(val);
            }
        }

        void emit_composite_assign(const ::MIR::TypeResolve& mir_res, ::std::function<void()> emit_slot, const ::std::vector<::MIR::Param>& vals, unsigned indent_level, bool prepend_newline = true) {
            auto indent = RepeatLitStr{"\t", static_cast<int>(indent_level)};
            bool has_emitted = prepend_newline;
            for (unsigned int j = 0; j < vals.size(); j++) {
                if (m_options.disallow_empty_structs) {
                    ::HIR::TypeRef tmp;
                    const auto& ty = mir_res.get_param_type(tmp, vals[j]);

                    // Don't emit assignment of PhantomData
                    if (vals[j].is_LValue() && m_resolve.is_type_phantom_data(ty)) {
                        continue;
                    }

                    // Or ZSTs
                    if (this->type_is_bad_zst(ty)) {
                        continue;
                    }
                }

                if (has_emitted) {
                    m_of << ";\n" << indent;
                }
                has_emitted = true;

                emit_slot();
                m_of << "._" << j << " = ";
                emit_param(vals[j]);
            }
        }

        void emit_drop_operation(const ::MIR::TypeResolve& mir_res, const ::MIR::Terminator::Data_Drop& e, unsigned indent_level) {
            auto indent = RepeatLitStr{"\t", static_cast<int>(indent_level)};
            ::HIR::TypeRef tmp;
            const auto& ty = mir_res.get_lvalue_type(tmp, e.slot);
            if (e.flag_idx != ~0u) {
                m_of << indent << "if( df" << e.flag_idx << " ) {\n";
            }
            switch (e.kind) {
                case ::MIR::eDropKind::SHALLOW:
                    if (const auto* ity = m_resolve.is_type_owned_box(ty)) {
                        emit_box_drop(indent_level + (e.flag_idx != ~0u ? 1 : 0), ity, ty, e.slot, false);
                    } else {
                        MIR_BUG(mir_res, "Shallow drop on non-Box - " << ty);
                    }
                    break;
                case ::MIR::eDropKind::DEEP:
                    emit_destructor_call(e.slot, ty, true, indent_level + (e.flag_idx != ~0u ? 1 : 0));
                    break;
            }
            if (e.flag_idx != ~0u) {
                m_of << indent << "}\n";
            }
        }

        void emit_statement(const ::MIR::TypeResolve& mir_res, const ::MIR::Statement& stmt, unsigned indent_level = 1) {
            DEBUG(stmt);
            auto indent = RepeatLitStr{"\t", static_cast<int>(indent_level)};
            switch (stmt.tag()) {
                case ::MIR::Statement::TAGDEAD:
                    throw "";
                case ::MIR::Statement::TAG_ScopeEnd:
                    m_of << indent << "// " << stmt << "\n";
                    break;
                case ::MIR::Statement::TAG_SetDropFlag: {
                    const auto& e = stmt.as_SetDropFlag();
                    m_of << indent << "df" << e.idx << " = ";
                    if (e.other == ~0u) {
                        m_of << e.new_val;
                    } else {
                        m_of << (e.new_val ? "!" : "") << "df" << e.other;
                    }
                    m_of << ";\n";
                    break;
                }
                    TU_ARM(stmt, SaveDropFlag, e) {
                        m_of << indent << "if(df" << e.idx << ") { ";
                        emit_lvalue(e.slot);
                        m_of << ".DATA[" << (e.bit_index / 8) << "] |= (1 << " << (e.bit_index % 8) << ");";
                        m_of << " } else { ";
                        emit_lvalue(e.slot);
                        m_of << ".DATA[" << (e.bit_index / 8) << "] &= ~(1 << " << (e.bit_index % 8) << ");";
                        m_of << " }\n";
                    }
                    break;
                    TU_ARM(stmt, LoadDropFlag, e) {
                        m_of << indent << "df" << e.idx << " = ((";
                        emit_lvalue(e.slot);
                        m_of << ".DATA[" << (e.bit_index / 8) << "] & (1 << " << (e.bit_index % 8) << ")) != 0)";
                        m_of << ";\n";
                    }
                    break;
                case ::MIR::Statement::TAG_Asm:
                    this->emit_asm_gcc(mir_res, stmt.as_Asm(), indent_level);

                    m_of << indent << "// ^ " << stmt << "\n";
                    break;
                case ::MIR::Statement::TAG_Asm2:
                    this->emit_asm2_gcc(mir_res, stmt, indent_level);

                    m_of << indent << "// ^ " << stmt << "\n";
                    break;
                case ::MIR::Statement::TAG_Assign: {
                    const auto& e = stmt.as_Assign();
                    DEBUG("- " << e.dst << " = " << e.src);
                    m_of << indent;

                    ::HIR::TypeRef tmp;
                    const auto& ty = mir_res.get_lvalue_type(tmp, e.dst);
                    if (/*(e.dst.is_Deref() || e.dst.is_Field()) &&*/ this->type_is_bad_zst(ty)) {
                        m_of << "/* ZST assign */\n";
                        break;
                    }

                TU_MATCH_HDRA( (e.src), {)
                TU_ARMA(Use, ve) {
                            ::HIR::TypeRef tmp;
                            const auto& ty = mir_res.get_lvalue_type(tmp, ve);
                            if (ty == m_crate.m_types.diverge()) {
                                m_of << "abort()";
                                break;
                            }

                            if (ve.is_Field() && this->type_is_bad_zst(ty)) {
                                m_of << "/* ZST field */";
                                break;
                            }

                            emit_lvalue(e.dst);
                            m_of << " = ";
                            emit_lvalue(ve);
                        }
                        TU_ARMA(Constant, ve) {
                            emit_lvalue(e.dst);
                            m_of << " = static_cast<";
                            emit_ctype(ty);
                            m_of << ">(";
                            emit_constant(ve, &e.dst);
                            m_of << ")";
                        }
                        TU_ARMA(SizedArray, ve) {
                            if (ve.count == 0) {
                            } else if (ve.count == 1) {
                                emit_lvalue(e.dst);
                                m_of << ".DATA[0] = ";
                                emit_param(ve.val);
                            } else if (ve.count == 2) {
                                emit_lvalue(e.dst);
                                m_of << ".DATA[0] = ";
                                emit_param(ve.val);
                                m_of << ";\n" << indent;
                                emit_lvalue(e.dst);
                                m_of << ".DATA[1] = ";
                                emit_param(ve.val);
                            } else if (ve.count == 3) {
                                emit_lvalue(e.dst);
                                m_of << ".DATA[0] = ";
                                emit_param(ve.val);
                                m_of << ";\n" << indent;
                                emit_lvalue(e.dst);
                                m_of << ".DATA[1] = ";
                                emit_param(ve.val);
                                m_of << ";\n" << indent;
                                emit_lvalue(e.dst);
                                m_of << ".DATA[2] = ";
                                emit_param(ve.val);
                            } else {
                                m_of << "for(unsigned int i = 0; i < " << ve.count << "; i ++)\n";
                                m_of << indent << "\t";
                                emit_lvalue(e.dst);
                                m_of << ".DATA[i] = ";
                                emit_param(ve.val);
                            }
                        }
                        TU_ARMA(Borrow, ve) {
                            emit_lvalue(e.dst);
                            const ::HIR::TypeData* pointee_ty;
                            if (const auto* borrow = ty->opt_Borrow()) {
                                pointee_ty = borrow->inner;
                            } else if (const auto* pointer = ty->opt_Pointer()) {
                                pointee_ty = pointer->inner;
                            } else {
                                MIR_BUG(mir_res, "Borrow rvalue has non-pointer result type " << ty);
                            }
                            const auto pointer_metadata = metadata_type(pointee_ty);
                            m_of << (pointer_metadata == MetadataType::None || pointer_metadata == MetadataType::Zero
                                         ? " = reinterpret_cast<"
                                         : " = static_cast<");
                            emit_ctype(ty);
                            m_of << ">(";
                            if (this->type_is_bad_zst(m_mir_res->get_lvalue_type(tmp, ve.val, ve.val.m_wrappers.size()))) {
                                m_of << "(void*)&rv";
                            } else {
                                emit_borrow(mir_res, ve.type, ve.val);
                            }
                            m_of << ")";
                        }
                        TU_ARMA(Cast, ve) {
                            emit_rvalue_cast(mir_res, e.dst, ve);
                        }
                        TU_ARMA(BinOp, ve) {
                            emit_lvalue(e.dst);
                            m_of << " = ";
                            ::HIR::TypeRef tmp, tmp_r;
                            const auto& ty = mir_res.get_param_type(tmp, ve.val_l);
                            const auto& ty_r = mir_res.get_param_type(tmp_r, ve.val_r);
                            if (ty->is_Borrow()) {
                                m_of << "(slice_cmp(";
                                emit_param(ve.val_l);
                                m_of << ", ";
                                emit_param(ve.val_r);
                                m_of << ")";
                                switch (ve.op) {
                                    case ::MIR::eBinOp::EQ:
                                        m_of << " == 0";
                                        break;
                                    case ::MIR::eBinOp::NE:
                                        m_of << " != 0";
                                        break;
                                    case ::MIR::eBinOp::GT:
                                        m_of << " >  0";
                                        break;
                                    case ::MIR::eBinOp::GE:
                                        m_of << " >= 0";
                                        break;
                                    case ::MIR::eBinOp::LT:
                                        m_of << " <  0";
                                        break;
                                    case ::MIR::eBinOp::LE:
                                        m_of << " <= 0";
                                        break;
                                    default:
                                        MIR_BUG(mir_res, "Unknown comparison of a &-ptr - " << e.src << " with " << ty);
                                }
                                m_of << ")";
                                break;
                            } else if (const auto* te = ty->opt_Pointer()) {
                                if (is_dst(te->inner)) {
                                    switch (ve.op) {
                                        case ::MIR::eBinOp::EQ:
                                            emit_param(ve.val_l);
                                            m_of << ".PTR == ";
                                            emit_param(ve.val_r);
                                            m_of << ".PTR && ";
                                            emit_param(ve.val_l);
                                            m_of << ".META == ";
                                            emit_param(ve.val_r);
                                            m_of << ".META";
                                            break;
                                        case ::MIR::eBinOp::NE:
                                            emit_param(ve.val_l);
                                            m_of << ".PTR != ";
                                            emit_param(ve.val_r);
                                            m_of << ".PTR || ";
                                            emit_param(ve.val_l);
                                            m_of << ".META != ";
                                            emit_param(ve.val_r);
                                            m_of << ".META";
                                            break;
                                        default:
                                            MIR_BUG(mir_res, "Unknown comparison of a *-ptr - " << e.src << " with " << ty);
                                    }
                                } else {
                                    emit_param(ve.val_l);
                                    switch (ve.op) {
                                        case ::MIR::eBinOp::EQ:
                                            m_of << " == ";
                                            break;
                                        case ::MIR::eBinOp::NE:
                                            m_of << " != ";
                                            break;
                                        case ::MIR::eBinOp::GT:
                                            m_of << " > ";
                                            break;
                                        case ::MIR::eBinOp::GE:
                                            m_of << " >= ";
                                            break;
                                        case ::MIR::eBinOp::LT:
                                            m_of << " < ";
                                            break;
                                        case ::MIR::eBinOp::LE:
                                            m_of << " <= ";
                                            break;
                                        default:
                                            MIR_BUG(mir_res, "Unknown comparison of a *-ptr - " << e.src << " with " << ty);
                                    }
                                    emit_param(ve.val_r);
                                }
                                break;
                            } else if (ve.op == ::MIR::eBinOp::MOD && (ty == ::HIR::CoreType::F32 || ty == ::HIR::CoreType::F64)) {
                                m_of << "__builtin_";
                                if (ty == ::HIR::CoreType::F32) {
                                    m_of << "remainderf";
                                } else {
                                    m_of << "remainder";
                                }
                                m_of << "(";
                                emit_param(ve.val_l);
                                m_of << ", ";
                                emit_param(ve.val_r);
                                m_of << ")";
                                break;
                            } else if (ty == ::HIR::CoreType::F16 || ty == ::HIR::CoreType::F128) {
                                auto ty_s = ty == ::HIR::CoreType::F16 ? "f16" : "f128";
                                switch (ve.op) {
                                    case ::MIR::eBinOp::EQ:
                                        m_of << "0 == ";
                                        if (0) {
                                            case ::MIR::eBinOp::NE:
                                                m_of << "0 != ";
                                        }
                                        if (0) {
                                            case ::MIR::eBinOp::GT:
                                                m_of << "0 > ";
                                        }
                                        if (0) {
                                            case ::MIR::eBinOp::GE:
                                                m_of << "0 >= ";
                                        }
                                        if (0) {
                                            case ::MIR::eBinOp::LT:
                                                m_of << "0 < ";
                                        }
                                        if (0) {
                                            case ::MIR::eBinOp::LE:
                                                m_of << "0 <= ";
                                        }
                                        // NOTE: Reversed order due to reversed logic above
                                        m_of << ty_s << "_cmp(";
                                        emit_param(ve.val_r);
                                        m_of << ", ";
                                        emit_param(ve.val_l);
                                        m_of << ")";
                                        break;
                                    default:
                                        m_of << ty_s << "_disabled()";
                                        break;
                                }
                                break;
                            } else if (type_is_emulated_i128(ty)) {
                                switch (ve.op) {
                                    case ::MIR::eBinOp::ADD:
                                        m_of << "add128";
                                        if (0) {
                                            case ::MIR::eBinOp::SUB:
                                                m_of << "sub128";
                                        }
                                        if (0) {
                                            case ::MIR::eBinOp::MUL:
                                                m_of << "mul128";
                                        }
                                        if (0) {
                                            case ::MIR::eBinOp::DIV:
                                                m_of << "div128";
                                        }
                                        if (0) {
                                            case ::MIR::eBinOp::MOD:
                                                m_of << "mod128";
                                        }
                                        if (0) {
                                            case ::MIR::eBinOp::BIT_OR:
                                                m_of << "or128";
                                        }
                                        if (0) {
                                            case ::MIR::eBinOp::BIT_AND:
                                                m_of << "and128";
                                        }
                                        if (0) {
                                            case ::MIR::eBinOp::BIT_XOR:
                                                m_of << "xor128";
                                        }
                                        if (ty == ::HIR::CoreType::I128) {
                                            m_of << "s";
                                        }
                                        m_of << "(";
                                        emit_param(ve.val_l);
                                        m_of << ", ";
                                        emit_param(ve.val_r);
                                        m_of << ")";
                                        break;
                                    case ::MIR::eBinOp::BIT_SHR:
                                        m_of << "shr128";
                                        if (0) {
                                            case ::MIR::eBinOp::BIT_SHL:
                                                m_of << "shl128";
                                        }
                                        if (ty == ::HIR::CoreType::I128) {
                                            m_of << "s";
                                        }
                                        m_of << "(";
                                        emit_param(ve.val_l);
                                        m_of << ", ";
                                        emit_param(ve.val_r);
                                        if ((ty_r == ::HIR::CoreType::I128 || ty_r == ::HIR::CoreType::U128)) {
                                            m_of << ".lo";
                                        }
                                        m_of << ")";
                                        break;

                                    case ::MIR::eBinOp::EQ:
                                        m_of << "0 == ";
                                        if (0) {
                                            case ::MIR::eBinOp::NE:
                                                m_of << "0 != ";
                                        }
                                        if (0) {
                                            case ::MIR::eBinOp::GT:
                                                m_of << "0 > ";
                                        }
                                        if (0) {
                                            case ::MIR::eBinOp::GE:
                                                m_of << "0 >= ";
                                        }
                                        if (0) {
                                            case ::MIR::eBinOp::LT:
                                                m_of << "0 < ";
                                        }
                                        if (0) {
                                            case ::MIR::eBinOp::LE:
                                                m_of << "0 <= ";
                                        }
                                        // NOTE: Reversed order due to reversed logic above
                                        m_of << "cmp128";
                                        if (ty == ::HIR::CoreType::I128) {
                                            m_of << "s";
                                        }
                                        m_of << "(";
                                        emit_param(ve.val_r);
                                        m_of << ", ";
                                        emit_param(ve.val_l);
                                        m_of << ")";
                                        break;

                                    case ::MIR::eBinOp::ADD_OV:
                                    case ::MIR::eBinOp::SUB_OV:
                                    case ::MIR::eBinOp::MUL_OV:
                                    case ::MIR::eBinOp::DIV_OV:
                                        MIR_TODO(mir_res, "Overflowing binops for emulated i128");
                                        break;
                                }
                                break;
                            } else {
                            }

                            emit_param(ve.val_l);
                            switch (ve.op) {
                                case ::MIR::eBinOp::ADD:
                                    m_of << " + ";
                                    break;
                                case ::MIR::eBinOp::SUB:
                                    m_of << " - ";
                                    break;
                                case ::MIR::eBinOp::MUL:
                                    m_of << " * ";
                                    break;
                                case ::MIR::eBinOp::DIV:
                                    m_of << " / ";
                                    break;
                                case ::MIR::eBinOp::MOD:
                                    m_of << " % ";
                                    break;

                                case ::MIR::eBinOp::BIT_OR:
                                    m_of << " | ";
                                    break;
                                case ::MIR::eBinOp::BIT_AND:
                                    m_of << " & ";
                                    break;
                                case ::MIR::eBinOp::BIT_XOR:
                                    m_of << " ^ ";
                                    break;
                                case ::MIR::eBinOp::BIT_SHR:
                                    m_of << " >> ";
                                    break;
                                case ::MIR::eBinOp::BIT_SHL:
                                    m_of << " << ";
                                    break;
                                case ::MIR::eBinOp::EQ:
                                    m_of << " == ";
                                    break;
                                case ::MIR::eBinOp::NE:
                                    m_of << " != ";
                                    break;
                                case ::MIR::eBinOp::GT:
                                    m_of << " > ";
                                    break;
                                case ::MIR::eBinOp::GE:
                                    m_of << " >= ";
                                    break;
                                case ::MIR::eBinOp::LT:
                                    m_of << " < ";
                                    break;
                                case ::MIR::eBinOp::LE:
                                    m_of << " <= ";
                                    break;

                                case ::MIR::eBinOp::ADD_OV:
                                case ::MIR::eBinOp::SUB_OV:
                                case ::MIR::eBinOp::MUL_OV:
                                case ::MIR::eBinOp::DIV_OV:
                                    MIR_TODO(mir_res, "Overflow");
                                    break;
                            }
                            emit_param(ve.val_r);
                            if (type_is_emulated_i128(ty_r)) {
                                m_of << ".lo";
                            }
                        }
                        TU_ARMA(UniOp, ve) {
                            ::HIR::TypeRef tmp;
                            const auto& ty = mir_res.get_lvalue_type(tmp, e.dst);

                            if (type_is_emulated_i128(ty)) {
                                switch (ve.op) {
                                    case ::MIR::eUniOp::NEG:
                                        emit_lvalue(e.dst);
                                        m_of << " = neg128s(";
                                        emit_lvalue(ve.val);
                                        m_of << ")";
                                        break;
                                    case ::MIR::eUniOp::INV:
                                        emit_lvalue(e.dst);
                                        m_of << ".lo = ~";
                                        emit_lvalue(ve.val);
                                        m_of << ".lo; ";
                                        emit_lvalue(e.dst);
                                        m_of << ".hi = ~";
                                        emit_lvalue(ve.val);
                                        m_of << ".hi";
                                        break;
                                }
                                break;
                            } else if (ty == ::HIR::CoreType::F16) {
                                switch (ve.op) {
                                    case ::MIR::eUniOp::NEG:
                                        emit_lvalue(e.dst);
                                        m_of << " = f16_disabled(/*";
                                        emit_lvalue(ve.val);
                                        m_of << "*/)";
                                        break;
                                    case ::MIR::eUniOp::INV:
                                        MIR_TODO(*m_mir_res, "f16 INV");
                                        break;
                                }
                                break;
                            } else if (ty == ::HIR::CoreType::F128) {
                                switch (ve.op) {
                                    case ::MIR::eUniOp::NEG:
                                        emit_lvalue(e.dst);
                                        m_of << " = f128_disabled(/*";
                                        emit_lvalue(ve.val);
                                        m_of << "*/)";
                                        break;
                                    case ::MIR::eUniOp::INV:
                                        MIR_TODO(*m_mir_res, "f128 INV");
                                        break;
                                }
                                break;
                            }

                            emit_lvalue(e.dst);
                            m_of << " = ";
                            switch (ve.op) {
                                case ::MIR::eUniOp::NEG:
                                    m_of << "-";
                                    break;
                                case ::MIR::eUniOp::INV:
                                    if (ty == ::HIR::CoreType::Bool) {
                                        m_of << "!";
                                    } else {
                                        m_of << "~";
                                    }
                                    break;
                            }
                            emit_lvalue(ve.val);
                        }
                        TU_ARMA(DstMeta, ve) {
                            emit_lvalue(e.dst);
                            // TODO: Why? Probably for getting `VTable`
                            if (ty->is_Primitive() || ty->is_Pointer() || ty->is_Borrow()) {
                            } else {
                                m_of << "._0._0";
                            }
                            m_of << " = static_cast<decltype(";
                            emit_lvalue(e.dst);
                            if (ty->is_Primitive() || ty->is_Pointer() || ty->is_Borrow()) {
                            } else {
                                m_of << "._0._0";
                            }
                            m_of << ")>(";
                            emit_lvalue(ve.val);
                            m_of << ".META)";
                        }
                        TU_ARMA(DstPtr, ve) {
                            emit_lvalue(e.dst);
                            m_of << " = static_cast<";
                            emit_ctype(ty);
                            m_of << ">(";
                            emit_lvalue(ve.val);
                            m_of << ".PTR)";
                        }
                        TU_ARMA(MakeDst, ve) {
                            emit_lvalue(e.dst);
                            m_of << " = static_cast<";
                            emit_ctype(ty);
                            m_of << ">(";
                            auto meta = metadata_type(ty->is_Pointer() ? ty->as_Pointer().inner : ty->as_Borrow().inner);
                            switch (meta) {
                                case MetadataType::Slice:
                                    m_of << "make_sliceptr";
                                    m_of << "(";
                                    emit_param(ve.ptr_val, false);
                                    m_of << ", ";
                                    emit_param(ve.meta_val);
                                    m_of << ")";
                                    break;
                                case MetadataType::TraitObject:
                                    m_of << "make_traitobjptr";
                                    m_of << "(";
                                    emit_param(ve.ptr_val);
                                    m_of << ", ";
                                    emit_trait_metadata_param(mir_res, ve.meta_val);
                                    m_of << ")";
                                    break;
                                case MetadataType::Zero:
                                case MetadataType::Unknown:
                                case MetadataType::None:
                                    m_of << "(void*)";
                                    emit_param(ve.ptr_val);
                                    break;
                            }
                            m_of << ")";
                        }
                        TU_ARMA(Tuple, ve) {
                            emit_composite_assign(mir_res, [&]() {
                                emit_lvalue(e.dst);
                            }, ve.vals, indent_level);
                        }
                        TU_ARMA(Array, ve) {
                            for (unsigned int j = 0; j < ve.vals.size(); j++) {
                                if (j != 0) {
                                    m_of << ";\n" << indent;
                                }
                                emit_lvalue(e.dst);
                                m_of << ".DATA[" << j << "] = ";
                                emit_param(ve.vals[j]);
                            }
                        }
                        TU_ARMA(UnionVariant, ve) {
                            MIR_ASSERT(mir_res, m_crate.get_typeitem_by_path(sp, ve.path.m_path).is_Union(), "");
                            if (!this->type_is_bad_zst(m_mir_res->get_param_type(tmp, ve.val))) {
                                emit_lvalue(e.dst);
                                m_of << ".var_" << ve.index << " = ";
                                emit_param(ve.val);
                            }
                        }
                        TU_ARMA(EnumVariant, ve) {
                            const auto& tyi = m_crate.get_typeitem_by_path(sp, ve.path.m_path);
                            MIR_ASSERT(mir_res, tyi.is_Enum(), "");
                            const auto* enm_p = &tyi.as_Enum();

                            ::HIR::TypeRef tmp;
                            const auto& ty = mir_res.get_lvalue_type(tmp, e.dst);
                            auto* repr = TargetGetTypeRepr(sp, m_resolve, ty);

                    TU_MATCH_HDRA( (repr->variants), {)
                    TU_ARMA(None, re) {
                                    emit_composite_assign(mir_res, [&]() {
                                        emit_lvalue(e.dst);
                                        m_of << ".DATA.var_0";
                                    }, /*repr->fields[0].ty,*/ ve.vals, indent_level);
                                }
                                TU_ARMA(NonZero, re) {
                                    MIR_ASSERT(*m_mir_res, ve.index < 2, "");
                                    if (ve.index == re.zero_variant) {
                                        // TODO: Use nonzero_path
                                        m_of << "memset(&";
                                        emit_lvalue(e.dst);
                                        m_of << ", 0, sizeof(";
                                        emit_ctype(ty);
                                        m_of << "))";
                                    } else {
                                        emit_composite_assign(mir_res, [&]() {
                                            emit_lvalue(e.dst);
                                            m_of << ".DATA.var_" << ve.index;
                                        }, /*repr->fields[0].ty,*/ ve.vals, indent_level, /*prepend_newline=*/false);
                                    }
                                }
                                TU_ARMA(Linear, re) {
                                    bool emit_newline = false;
                                    if (!re.is_niche(ve.index)) {
                                        // Each variant has its own tag field, it will be the last numbered field in that variant slot
                                        // - Only use that if there isn't an explicit tag field in the enum
                                        if (re.field.sub_fields.empty() || type_is_bad_zst(repr->fields[ve.index].ty)) {
                                            emit_lvalue(e.dst);
                                            const auto& slot_ty = emit_enum_path(repr, re.field);
                                            m_of << " = ";
                                            if (slot_ty->is_Pointer() || slot_ty->is_Borrow() || slot_ty->is_Function()) {
                                                m_of << "(";
                                                emit_ctype(slot_ty);
                                                m_of << ")(uintptr_t)";
                                            }
                                            m_of << (re.offset + ve.index);
                                        } else {
                                            auto vr = TargetGetTypeRepr(sp, m_resolve, repr->fields[ve.index].ty);
                                            //m_of << "assert(&";
                                            //emit_lvalue(e.dst); m_of << ".DATA.var_" << ve.index << "._" << (vr->fields.size() - 1);
                                            //m_of << " == &";
                                            //emit_lvalue(e.dst); emit_enum_path(repr, re.field);
                                            //m_of << "); ";
                                            emit_lvalue(e.dst);
                                            m_of << ".DATA.var_" << ve.index << "._" << (vr->fields.size() - 1) << " = ";
                                            const auto& slot_ty = vr->fields.back().ty;
                                            if (slot_ty->is_Pointer() || slot_ty->is_Borrow() || slot_ty->is_Function()) {
                                                m_of << "(";
                                                emit_ctype(slot_ty);
                                                m_of << ")(uintptr_t)";
                                            }
                                            m_of << (re.offset + ve.index);
                                        }
                                        emit_newline = true;
                                    } else {
                                        m_of << "/* Niche tag */";
                                    }
                                    if (enm_p->is_value()) {
                                        // Value enums have no data fields
                                    } else {
                                        emit_composite_assign(mir_res, [&]() {
                                            emit_lvalue(e.dst);
                                            m_of << ".DATA.var_" << ve.index;
                                        }, ve.vals, indent_level, emit_newline);
                                    }
                                }
                                TU_ARMA(Values, re) {
                                    if (re.field.index == 0) {
                                        emit_lvalue(e.dst);
                                        m_of << ".TAG = ";
                                        emit_enum_variant_val(repr, ve.index);
                                    } else {
                                        emit_lvalue(e.dst);
                                        m_of << ".DATA.TAG = ";
                                        emit_enum_variant_val(repr, ve.index);
                                    }
                                    if (!enm_p->is_value()) {
                                        emit_composite_assign(mir_res, [&]() {
                                            emit_lvalue(e.dst);
                                            m_of << ".DATA.var_" << ve.index;
                                        }, ve.vals, indent_level, true);
                                    }
                                }
                    }
                        }
                        TU_ARMA(Struct, ve) {
                            if (ve.vals.empty()) {
                                if (m_options.disallow_empty_structs) {
                                    emit_lvalue(e.dst);
                                    m_of << "._d = 0";
                                }
                            } else {
                                emit_composite_assign(mir_res, [&]() {
                                    emit_lvalue(e.dst);
                                }, ve.vals, indent_level, /*emit_newline=*/false);
                            }
                        }
                }
                m_of << ";";
                m_of << "\t// " << e.dst << " = " << e.src;
                m_of << "\n";
                break;
                }
            }
        }

        void emit_rvalue_cast(const ::MIR::TypeResolve& mir_res, const ::MIR::LValue& dst, const ::MIR::RValue::Data_Cast& ve) {
            if (m_resolve.is_type_phantom_data(ve.type)) {
                m_of << "/* PhantomData cast */\n";
                return;
            }

            ::HIR::TypeRef tmp;
            const auto& ty = mir_res.get_lvalue_type(tmp, ve.val);

            // A cast to a fat pointer doesn't actually change the C type.
            if ((ve.type->is_Pointer() && is_dst(ve.type->as_Pointer().inner)) ||
                (ve.type->is_Borrow() && is_dst(ve.type->as_Borrow().inner))
                // OR: If it's a no-op cast
                || ve.type == ty) {
                emit_lvalue(dst);
                m_of << " = ";
                emit_lvalue(ve.val);
                return;
            }

            // Cast of a named function to a function pointer - originate the pointer
            if (ve.type->is_Function() && ty->is_NamedFunction()) {
                emit_lvalue(dst);
                m_of << " = " << TransMangle(ty->as_NamedFunction().path);
                return;
            }

            // Emulated i128/u128 support
            if (m_options.emulated_i128 && (ve.type == ::HIR::CoreType::U128 || ve.type == ::HIR::CoreType::I128 || ty == ::HIR::CoreType::U128 || ty == ::HIR::CoreType::I128)) {
                // Destination
                MIR_ASSERT(mir_res, ve.type->is_Primitive(), "i128/u128 cast to non-primitive - " << ve.type);
                MIR_ASSERT(mir_res, ty->is_Primitive() || (ty->is_Path() && ty->as_Path().binding.is_Enum()), "i128/u128 cast from non-primitive - " << ty);
                switch (ve.type->as_Primitive()) {
                    case ::HIR::CoreType::I128:
                    case ::HIR::CoreType::U128:
                        if (ty == ::HIR::CoreType::I128 || ty == ::HIR::CoreType::U128) {
                            // Cast between i128 and u128
                            emit_lvalue(dst);
                            m_of << ".lo = ";
                            emit_lvalue(ve.val);
                            m_of << ".lo; ";
                            emit_lvalue(dst);
                            m_of << ".hi = ";
                            emit_lvalue(ve.val);
                            m_of << ".hi";
                        } else if (ty->is_Path() && ty->as_Path().binding.is_Enum()) {
                            emit_lvalue(dst);
                            m_of << ".lo = ";
                            emit_lvalue(ve.val);
                            m_of << ".TAG; ";
                            emit_lvalue(dst);
                            m_of << ".hi = ";
                            emit_lvalue(ve.val);
                            m_of << ".TAG < 0 ? -1 : 0";
                        } else {
                            // Cast from small to i128/u128
                            emit_lvalue(dst);
                            m_of << ".lo = ";
                            emit_lvalue(ve.val);
                            m_of << "; ";
                            emit_lvalue(dst);
                            m_of << ".hi = ";
                            emit_lvalue(ve.val);
                            m_of << " < 0 ? -1 : 0";
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
                        emit_lvalue(dst);
                        m_of << " = ";
                        switch (ty->as_Primitive()) {
                            case ::HIR::CoreType::U128:
                            case ::HIR::CoreType::I128:
                                emit_lvalue(ve.val);
                                m_of << ".lo";
                                break;
                            default:
                                MIR_BUG(mir_res, "Unreachable");
                        }
                        break;
                    case ::HIR::CoreType::F16:
                        MIR_TODO(mir_res, "f16 from i128/u128");
                    case ::HIR::CoreType::F32:
                        emit_lvalue(dst);
                        m_of << " = ";
                        switch (ty->as_Primitive()) {
                            case ::HIR::CoreType::U128:
                                m_of << "cast128_float(";
                                emit_lvalue(ve.val);
                                m_of << ")";
                                break;
                            case ::HIR::CoreType::I128:
                                m_of << "cast128s_float(";
                                emit_lvalue(ve.val);
                                m_of << ")";
                                break;
                            default:
                                MIR_BUG(mir_res, "Unreachable");
                        }
                        break;
                    case ::HIR::CoreType::F64:
                        emit_lvalue(dst);
                        m_of << " = ";
                        switch (ty->as_Primitive()) {
                            case ::HIR::CoreType::U128:
                                m_of << "cast128_double(";
                                emit_lvalue(ve.val);
                                m_of << ")";
                                break;
                            case ::HIR::CoreType::I128:
                                m_of << "cast128s_double(";
                                emit_lvalue(ve.val);
                                m_of << ")";
                                break;
                            default:
                                MIR_BUG(mir_res, "Unreachable");
                        }
                        break;
                    case ::HIR::CoreType::F128:
                        MIR_TODO(mir_res, "f128 from i128/u128");
                    default:
                        MIR_BUG(mir_res, "Bad i128/u128 cast - " << ty << " to " << ve.type);
                }
                return;
            }
            if (ve.type == ::HIR::CoreType::F16 || ve.type == ::HIR::CoreType::F128 || ty == ::HIR::CoreType::F16 || ty == ::HIR::CoreType::F128) {
                m_of << "abort()";
                return;
            }

            // Standard cast
            ::HIR::TypeRef dst_tmp;
            const auto& dst_ty = mir_res.get_lvalue_type(dst_tmp, dst);
            emit_lvalue(dst);
            m_of << " = ";
            m_of << "(";
            emit_ctype(dst_ty);
            m_of << ")";
            // TODO: If the source is an unsized borrow, then extract the pointer
            bool special = false;
            // If the destination is a thin pointer
            if (ve.type->is_Pointer() && !is_dst(ve.type->as_Pointer().inner)) {
                // NOTE: Checks the result of the deref
                if ((ty->is_Borrow() && is_dst(ty->as_Borrow().inner)) || (ty->is_Pointer() && is_dst(ty->as_Pointer().inner))) {
                    emit_lvalue(ve.val);
                    m_of << ".PTR";
                    special = true;
                }
            }
            if (ty->is_NamedFunction()) {
                m_of << TransMangle(ty->as_NamedFunction().path);
                special = true;
            }
            if (ve.type->is_Primitive() && ty->is_Path() && ty->as_Path().binding.is_Enum()) {
                emit_lvalue(ve.val);
                // NOTE: Embedded tag enums can't be cast
                m_of << ".TAG";
                special = true;
            }
            if (!special) {
                emit_lvalue(ve.val);
            }
        }

        void emit_term_switch(const ::MIR::TypeResolve& mir_res, const ::MIR::LValue& val, size_t n_arms, unsigned indent_level, ::std::function<void(size_t)> cb, size_t odd_arm = -1) {
            auto indent = RepeatLitStr{"\t", static_cast<int>(indent_level)};

            ::HIR::TypeRef tmp;
            const auto& ty = mir_res.get_lvalue_type(tmp, val);
            MIR_ASSERT(mir_res, ty->is_Path(), "Switch over non-Path type");
            MIR_ASSERT(mir_res, ty->as_Path().binding.is_Enum(), "Switch over non-enum");
            const auto* repr = TargetGetTypeRepr(mir_res.sp, m_resolve, ty);
            MIR_ASSERT(mir_res, repr, "No repr for " << ty);

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
                    MIR_ASSERT(mir_res, n_arms == 2, "NonZero optimised switch without two arms");
                    // If this is an emulated i128, check both fields
                    m_of << indent << "if( ";
                    emit_lvalue(val);
                    const auto& slot_ty = emit_enum_path(repr, e.field);
                    MIR_ASSERT(mir_res, slot_ty->is_Pointer() || slot_ty->is_Function() || slot_ty->is_Borrow() || slot_ty->is_Primitive(), "Invalid niche type: " << slot_ty << " in " << ty);
                    if (type_is_emulated_i128(slot_ty)) {
                        m_of << ".lo == 0 && ";
                        emit_lvalue(val);
                        emit_enum_path(repr, e.field);
                        m_of << ".hi";
                    }
                    m_of << " != 0 )\n";
                    m_of << indent << "\t";
                    cb(1 - e.zero_variant);
                    m_of << "\n";
                    m_of << indent << "else\n";
                    m_of << indent << "\t";
                    cb(e.zero_variant);
                    m_of << "\n";
                }
                TU_ARMA(Linear, e) {
                    const auto& tag_ty = TargetGetInnerType(sp, m_resolve, *repr, e.field.index, e.field.sub_fields);
                    const bool pointer_tag = tag_ty->is_Pointer() || tag_ty->is_Borrow() || tag_ty->is_Function();
                    if (!pointer_tag) {
                        switch (tag_ty->as_Primitive()) {
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
                                MIR_BUG(mir_res, "Invalid tag type?! " << tag_ty);
                        }
                    }

                    auto emit_variant = [&]() {
#if 1
                        if (pointer_tag) {
                            m_of << "(uintptr_t)";
                        }
                        emit_lvalue(val);
                        emit_enum_path(repr, e.field);
#else
                        // Emit using a pointer manipulation, to avoid `union` "active member" rule
                        // - Technically not type punning, as the type is the same in all cases
                        // Get the offset
                        size_t offset = repr->get_offset(sp, m_resolve, e.field);
                        ;
                        // Emit
                        m_of << " *(";
                        emit_ctype(tag_ty);
                        m_of << "*)(";
                        m_of << "(const char*)&";
                        emit_lvalue(val);
                        m_of << " + " << offset;
                        _of << ")";
#endif
                    };

                    // Optimisation: If there's only one arm with a different value, then emit an `if` isntead of a `switch`
                    if (odd_arm != static_cast<size_t>(-1)) {
                        m_of << indent << "if( ";
                        emit_variant();
                        if (e.is_niche(odd_arm)) {
                            m_of << " < " << e.offset;
                        } else {
                            m_of << " == " << (e.offset + odd_arm);
                        }
                        m_of << ") {";
                        cb(odd_arm);
                        m_of << "} else {";
                        cb(odd_arm == 0 ? 1 : 0);
                        m_of << "}\n";
                    } else {
                        m_of << indent << "switch(";
                        emit_variant();
                        m_of << ") {\n";
                        for (size_t j = 0; j < n_arms; j++) {
                            if (e.is_niche(j)) {
                                continue;
                            }
                            // Handle signed values
                            m_of << indent << "case " << (e.offset + j) << ": ";
                            cb(j);
                            m_of << "break;\n";
                        }
                        m_of << indent << "default: ";
                        if (e.uses_niche()) {
                            cb(e.field.index);
                            m_of << "break;";
                        } else {
                            m_of << "abort();";
                        }
                        m_of << "\n";
                        m_of << indent << "}\n";
                    }
                }
                TU_ARMA(Values, e) {
                    const auto& tag_ty = TargetGetInnerType(sp, m_resolve, *repr, e.field.index, e.field.sub_fields);
                    bool is_signed = false;
                    switch (tag_ty->as_Primitive()) {
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
                            MIR_TODO(mir_res, "Floating point enum tag.");
                            break;
                        case ::HIR::CoreType::Str:
                            MIR_BUG(mir_res, "Unsized tag?!");
                    }

                    const bool is_128 = tag_ty == ::HIR::CoreType::I128 || tag_ty == ::HIR::CoreType::U128;
                    const bool emulated_128 = type_is_emulated_i128(tag_ty);
                    auto emit_tag = [&]() {
                        emit_lvalue(val);
                        emit_enum_path(repr, e.field);
                    };
                    auto emit_equal = [&](size_t variant) {
                        if (emulated_128) {
                            m_of << (is_signed ? "cmp128s(" : "cmp128(");
                            emit_tag();
                            m_of << ", ";
                            emit_enum_variant_val(repr, variant);
                            m_of << ") == 0";
                        } else {
                            emit_tag();
                            m_of << " == ";
                            emit_enum_variant_val(repr, variant);
                        }
                    };

                    // Optimisation: If there's only one arm with a different value, then emit an `if` isntead of a `switch`
                    if (odd_arm != static_cast<size_t>(-1)) {
                        m_of << indent << "if(";
                        emit_equal(odd_arm);
                        m_of << ") {";
                        cb(odd_arm);
                        m_of << "} else {";
                        cb(odd_arm == 0 ? 1 : 0);
                        m_of << "}\n";
                        return;
                    }

                    if (is_128) {
                        for (size_t j = 0; j < n_arms; j++) {
                            m_of << indent << (j == 0 ? "if(" : "else if(");
                            emit_equal(j);
                            m_of << ") {";
                            cb(j);
                            m_of << "}\n";
                        }
                        m_of << indent << "else { abort(); }\n";
                        return;
                    }

                    m_of << indent << "switch(";
                    emit_tag();
                    m_of << ") {\n";
                    for (size_t j = 0; j < n_arms; j++) {
                        // Handle signed values
                        if (is_signed) {
                            m_of << indent << "case " << S128(e.values[j]).truncate_i64() << "ll: ";
                        } else {
                            m_of << indent << "case " << e.values[j].truncate_u64() << "ull: ";
                        }
                        cb(j);
                        m_of << "break;\n";
                    }
                    m_of << indent << "default: abort();\n";
                    m_of << indent << "}\n";
                }
                TU_ARMA(None, e) {
                    m_of << indent;
                    cb(0);
                    m_of << "\n";
                }
            }
        }

        void emit_term_switchvalue(const ::MIR::TypeResolve& mir_res, const ::MIR::LValue& val, const ::MIR::SwitchValues& values, unsigned indent_level, ::std::function<void(size_t)> cb) {
            auto indent = RepeatLitStr{"\t", static_cast<int>(indent_level)};

            ::HIR::TypeRef tmp;
            const auto& ty = mir_res.get_lvalue_type(tmp, val);
            if (const auto* ve = values.opt_String()) {
                m_of << indent << "{ static SLICE_PTR switch_strings[] = {";
                for (const auto& v : *ve) {
                    m_of << " {(void*)";
                    this->print_escaped_string(v);
                    m_of << "," << v.size() << "},";
                }
                m_of << " {0,0} };\n";
                m_of << indent << "switch( mrustc_string_search_linear(";
                emit_lvalue(val);
                m_of << ", " << ve->size() << ", switch_strings) ) {\n";
                for (size_t i = 0; i < ve->size(); i++) {
                    m_of << indent << "case " << i << ": ";
                    cb(i);
                    m_of << " break;\n";
                }
                m_of << indent << "default: ";
                cb(SIZE_MAX);
                m_of << "\n";
                m_of << indent << "} }\n";
            } else if (const auto* ve = values.opt_ByteString()) {
                m_of << indent << "{ static SLICE_PTR switch_strings[] = {";
                for (const auto& v : *ve) {
                    m_of << " {(void*)";
                    this->print_escaped_string(v);
                    m_of << "," << v.size() << "},";
                }
                m_of << " {0,0} };\n";
                HIR::TypeRef tmp;
                const auto& ty = mir_res.get_lvalue_type(tmp, val);
                m_of << indent << "switch( mrustc_string_search_linear(";
                if (const auto* a = ty->as_Borrow().inner->opt_Array()) {
                    auto len = a->size.as_Known();
                    m_of << "make_sliceptr(";
                    emit_lvalue(val);
                    m_of << "->DATA, " << len << ")";
                } else {
                    emit_lvalue(val);
                }
                m_of << ", " << ve->size() << ", switch_strings) ) {\n";
                for (size_t i = 0; i < ve->size(); i++) {
                    m_of << indent << "case " << i << ": ";
                    cb(i);
                    m_of << " break;\n";
                }
                m_of << indent << "default: ";
                cb(SIZE_MAX);
                m_of << "\n";
                m_of << indent << "} }\n";
            } else if (const auto* ve = values.opt_Unsigned()) {
                const bool emulated_u128 = m_options.emulated_i128 && ty == ::HIR::CoreType::U128;
                if (emulated_u128) {
                    m_of << indent << "if(";
                    emit_lvalue(val);
                    m_of << ".hi != 0) { ";
                    cb(SIZE_MAX);
                    m_of << " }\n";
                }
                m_of << indent << (emulated_u128 ? "else " : "") << "switch(";
                emit_lvalue(val);
                if (emulated_u128) {
                    m_of << ".lo";
                }
                m_of << ") {\n";
                for (size_t i = 0; i < ve->size(); i++) {
                    m_of << indent << "\tcase " << (*ve)[i] << "ull: ";
                    cb(i);
                    m_of << " break;\n";
                }
                m_of << indent << "\tdefault: ";
                cb(SIZE_MAX);
                m_of << "\n";
                m_of << indent << "}\n";
            } else if (const auto* ve = values.opt_Signed()) {
                //assert(ve->size() == e.targets.size());
                const bool emulated_i128 = m_options.emulated_i128 && ty == ::HIR::CoreType::I128;
                if (emulated_i128) {
                    m_of << indent << "if(";
                    emit_lvalue(val);
                    m_of << ".hi != ((int64_t)";
                    emit_lvalue(val);
                    m_of << ".lo < 0 ? UINT64_MAX : 0)) { ";
                    cb(SIZE_MAX);
                    m_of << " }\n";
                }
                m_of << indent << (emulated_i128 ? "else " : "") << "switch(";
                if (emulated_i128) {
                    m_of << "(int64_t)";
                }
                emit_lvalue(val);
                if (emulated_i128) {
                    m_of << ".lo";
                }
                m_of << ") {\n";
                for (size_t i = 0; i < ve->size(); i++) {
                    m_of << indent << "\tcase ";
                    if ((*ve)[i] == INT64_MIN) {
                        m_of << "INT64_MIN";
                    } else {
                        m_of << (*ve)[i] << "ll";
                    }
                    m_of << ": ";
                    cb(i);
                    m_of << " break;\n";
                }
                m_of << indent << "\tdefault: ";
                cb(SIZE_MAX);
                m_of << "\n";
                m_of << indent << "}\n";
            } else {
                MIR_BUG(mir_res, "SwitchValue with unknown value type - " << values.tag_str());
            }
        }

        void emit_term_call(const ::MIR::TypeResolve& mir_res, const ::MIR::Terminator::Data_Call& e, unsigned indent_level) {
            auto indent = RepeatLitStr{"\t", static_cast<int>(indent_level)};
            m_of << indent;

            bool has_zst = false;
            for (unsigned int j = 0; j < e.args.size(); j++) {
                ::HIR::TypeRef tmp;
                const auto& ty = m_mir_res->get_param_type(tmp, e.args[j]);
                if (m_options.disallow_empty_structs /*&& TU_TEST1(e.args[j], LValue, .is_Field())*/) {
                    if (this->type_is_bad_zst(ty)) {
                        if (!has_zst) {
                            m_of << "{\n";
                            indent.n++;
                            m_of << indent;
                            has_zst = true;
                        }
                        emit_ctype(ty, FMT_CB(ss, ss << "zarg" << j;));
                        m_of << " = {0};\n";
                        m_of << indent;
                        continue;
                    }
                }
            }

            bool omit_assign = false;

            // If the return type is `()`, omit the assignment (all `()` returning functions are marked as returning
            // void)
            {
                ::HIR::TypeRef tmp;
                if (m_mir_res->get_lvalue_type(tmp, e.ret_val) == m_crate.m_types.unit()) {
                    omit_assign = true;
                }

                if (this->type_is_bad_zst(m_mir_res->get_lvalue_type(tmp, e.ret_val))) {
                    omit_assign = true;
                }
            }

            TU_MATCH_HDRA( (e.fcn), {)
            TU_ARMA(Value, e2) {
                    {
                        ::HIR::TypeRef tmp;
                        const auto& ty = mir_res.get_lvalue_type(tmp, e2);
                        MIR_ASSERT(mir_res, ty->is_Function(), "Call::Value on non-function - " << ty);

                        const auto& ret_ty = ty->as_Function().m_rettype;
                        omit_assign |= ret_ty->is_Diverge();
                        if (!omit_assign) {
                            emit_lvalue(e.ret_val);
                            m_of << " = ";
                        }
                    }
                    m_of << "(";
                    emit_lvalue(e2);
                    m_of << ")";
                }
                TU_ARMA(Path, e2) {
                    {
                    TU_MATCH_HDRA( (e2.m_data), {)
                    TU_ARMA(Generic, pe) {
                                const auto& fcn = m_crate.get_function_by_path(sp, pe.m_path);
                                omit_assign |= fcn.m_return->is_Diverge();
                                // TODO: Monomorph.
                            }
                            TU_ARMA(UfcsUnknown, pe) {
                            }
                            TU_ARMA(UfcsInherent, pe) {
                                // Check if the return type is !
                                omit_assign |= m_resolve.m_crate.find_type_impls(pe.type, HIR::ResolvePlaceholdersNop(), [&](const auto& impl) {
                                    // Associated functions
                                    {
                                        auto it = impl.m_methods.find(pe.item);
                                        if (it != impl.m_methods.end()) {
                                            return it->second.data.m_return->is_Diverge();
                                        }
                                    }
                                    // Associated static (undef)
                                    return false;
                                });
                            }
                            TU_ARMA(UfcsKnown, pe) {
                                // Check if the return type is !
                                const auto& tr = m_resolve.m_crate.get_trait_by_path(sp, pe.trait.m_path);
                                const auto& fcn = tr.m_values.find(pe.item)->second.as_Function();
                                const auto& rv_tpl = fcn.m_return;
                                if (rv_tpl->is_Diverge() || rv_tpl == m_crate.m_types.unit()) {
                                    omit_assign |= true;
                                } else if (const auto* te = rv_tpl->opt_Generic()) {
                                    (void)te;
                                    // TODO: Generic lookup
                                } else if (const auto* te = rv_tpl->opt_Path()) {
                                    if (te->binding.is_Opaque()) {
                                        // TODO: Associated type lookup
                                    }
                                } else {
                                    // Not a ! type
                                }
                            }
                    }
                    if(!omit_assign)
                    {
                            emit_lvalue(e.ret_val);
                            m_of << " = ";
                    }
                    }
                    m_of << TransMangle(e2);
                }
                TU_ARMA(Intrinsic, e2) {
                    const auto& name = e2.name;
                    const auto& params = e2.params;
                    emit_intrinsic_call(name, params, e);
                    if (has_zst) {
                        indent.n--;
                        m_of << indent << "}\n";
                    }
                    return;
                }
            }
            m_of << "(";
            for(unsigned int j = 0; j < e.args.size(); j ++) {
                if (j != 0) {
                    m_of << ",";
                }
                m_of << " ";
                ::HIR::TypeRef tmp;
                const auto& ty = m_mir_res->get_param_type(tmp, e.args[j]);

                if (this->type_is_bad_zst(ty)) {
                    m_of << "zarg" << j;
                    continue;
                }
                emit_param(e.args[j]);
            }
            m_of << " );\n";

            if( has_zst )
            {
                indent.n--;
                m_of << indent << "}\n";
            }
        }

        bool asm_matches_template(const ::MIR::Statement::Data_Asm& e, const char* tpl, ::std::initializer_list<const char*> inputs, ::std::initializer_list<const char*> outputs) {
            struct H {
                static bool check_list(const std::vector<std::pair<std::string, MIR::LValue>>& have, const ::std::initializer_list<const char*>& exp) {
                    if (have.size() != exp.size()) {
                        return false;
                    }
                    auto h_it = have.begin();
                    auto e_it = exp.begin();
                    for (; h_it != have.end(); ++h_it, ++e_it) {
                        if (h_it->first != *e_it) {
                            return false;
                        }
                    }
                    return true;
                }
            };

            if (e.tpl == tpl) {
                if (!H::check_list(e.inputs, inputs) || !H::check_list(e.outputs, outputs)) {
                    MIR_BUG(*m_mir_res, "Hard-coded asm translation doesn't apply - `" << e.tpl << "` inputs=" << e.inputs << " outputs=" << e.outputs);
                }
                return true;
            }
            return false;
        }

        void emit_asm_gcc(const ::MIR::TypeResolve& mir_res, const ::MIR::Statement::Data_Asm& e, unsigned indent_level) {
            auto indent = RepeatLitStr{"\t", static_cast<int>(indent_level)};

            struct H {
                static bool has_flag(const ::std::vector<::std::string>& flags, const char* des) {
                    return ::std::find_if(flags.begin(), flags.end(), [des](const auto& x) {
                        return x == des;
                    }) != flags.end();
                }

                static const char* convert_reg(const char* r) {
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

            bool is_volatile = H::has_flag(e.flags, "volatile");
            bool is_intel = H::has_flag(e.flags, "intel");

            // The following clobber overlaps with an output
            // __asm__ ("cpuid": "=a" (var0), "=b" (var1), "=c" (var2), "=d" (var3): "a" (arg0), "c" (var4): "rbx");
            if (asm_matches_template(e, "cpuid", {"{eax}", "{ecx}"}, {"={eax}", "={ebx}", "={ecx}", "={edx}"})) {
                if (e.clobbers.size() == 1 && e.clobbers[0] == "rbx") {
                    m_of << indent << "__asm__(\"cpuid\"";
                    m_of << " : ";
                    m_of << "\"=a\" (";
                    emit_lvalue(e.outputs[0].second);
                    m_of << "), ";
                    m_of << "\"=b\" (";
                    emit_lvalue(e.outputs[1].second);
                    m_of << "), ";
                    m_of << "\"=c\" (";
                    emit_lvalue(e.outputs[2].second);
                    m_of << "), ";
                    m_of << "\"=d\" (";
                    emit_lvalue(e.outputs[3].second);
                    m_of << ")";
                    m_of << " : ";
                    m_of << "\"a\" (";
                    emit_lvalue(e.inputs[0].second);
                    m_of << "), ";
                    m_of << "\"c\" (";
                    emit_lvalue(e.inputs[1].second);
                    m_of << ")";
                    m_of << " );\n";
                    return;
                }
            }
            if (asm_matches_template(e, "pushfd; popl $0", {}, {"=r"})) {
                m_of << indent << "__asm__ __volatile__ (\"pushfl; popl %0\" : \"=r\" (";
                emit_lvalue(e.outputs[0].second);
                m_of << ") : : );\n";
                return;
            }
            if (asm_matches_template(e, "pushl $0; popfd", {"r"}, {})) {
                m_of << indent << "__asm__ __volatile__ (\"pushl %0; popfl\" : : \"r\" (";
                emit_lvalue(e.inputs[0].second);
                m_of << ") : );\n";
                return;
            }

            m_of << indent << "__asm__ ";
            if (is_volatile) {
                m_of << "__volatile__";
            }
            m_of << "(\"" << (is_intel ? ".intel_syntax noprefix; " : "");
            // TODO: Use a more powerful parser that can properly handle the differences between rustc/llvm and GCC
            for (auto it = e.tpl.begin(); it != e.tpl.end(); ++it) {
                if (*it == '\n') {
                    m_of << ";\\n";
                } else if (*it == '"') {
                    m_of << "\\\"";
                } else if (*it == '\\') {
                    m_of << "\\\\";
                } else if (*it == '/' && *(it + 1) == '/') {
                    while (it != e.tpl.end() || *it == '\n') {
                        ++it;
                    }
                    --it;
                } else if (*it == '%' && *(it + 1) == '%') {
                    m_of << "%";
                } else if (*it == '%' && !isdigit(*(it + 1))) {
                    m_of << "%%";
                } else if (*it == '$' && isdigit(*(it + 1)) && *(it + 2) != 'x') {
                    m_of << "%";
                }
                // Hack for `${0:b}` seen with `setc`, just emit as `%0`
                else if (*it == '$' && *(it + 1) == '{') {
                    m_of << "%" << *(it + 2);
                    while (it != e.tpl.end() && *it != '}') {
                        it++;
                    }
                } else {
                    m_of << *it;
                }
            }
            m_of << (is_intel ? ".att_syntax; " : "") << "\"";
            m_of << ": ";
            for (unsigned int i = 0; i < e.outputs.size(); i++) {
                const auto& v = e.outputs[i];
                if (i != 0) {
                    m_of << ", ";
                }
                m_of << "\"";
                switch (v.first[0]) {
                    case '=':
                        m_of << "=";
                        break;
                    case '+':
                        m_of << "+";
                        break;
                    default:
                        MIR_TODO(mir_res, "Handle asm! output leader '" << v.first[0] << "'");
                }
                m_of << H::convert_reg(v.first.c_str() + 1);
                m_of << "\" (";
                emit_lvalue(v.second);
                m_of << ")";
            }
            m_of << ": ";
            for (unsigned int i = 0; i < e.inputs.size(); i++) {
                const auto& v = e.inputs[i];
                if (i != 0) {
                    m_of << ", ";
                }
                // TODO: If this is the same reg as an output, use the output index
                m_of << "\"" << H::convert_reg(v.first.c_str()) << "\" (";
                emit_lvalue(v.second);
                m_of << ")";
            }
            m_of << ": ";
            for (unsigned int i = 0; i < e.clobbers.size(); i++) {
                if (i != 0) {
                    m_of << ", ";
                }
                if (e.tpl == "cpuid\n" && e.clobbers[i] == "rbx") {
                    continue;
                }
                m_of << "\"" << e.clobbers[i] << "\"";
            }
            m_of << ");\n";
        }

        struct Asm2TplMatch {
            const MIR::TypeResolve& m_mir_res;
            const ::MIR::Statement& stmt;
            const ::MIR::Statement::Data_Asm2& e;
            std::vector<std::string> fmt_lines;
            std::vector<std::string> fmt_params;

            Asm2TplMatch(const MIR::TypeResolve& mir_res, const ::MIR::Statement& stmt)
                : m_mir_res(mir_res)
                , stmt(stmt)
                , e(stmt.as_Asm2())
            {
                for (const auto& v : e.lines) {
                    fmt_lines.push_back(FMT(FMT_CB(os, v.fmt(os))));
                    fmt_lines.back().erase(fmt_lines.back().begin());
                    fmt_lines.back().pop_back();
                    DEBUG(fmt_lines.back());
                }

                for (const auto& p : e.params) {
                    fmt_params.push_back(get_param_text(p));
                }
            }

            bool matches_template(::std::initializer_list<const char*> lines, ::std::initializer_list<const char*> params) const {
                if (!check_list(fmt_lines, lines)) {
                    return false;
                }

                if (!check_list(fmt_params, params)) {
                    MIR_BUG(
                        m_mir_res,
                        "Hard-coded asm translation doesn't apply - " << stmt << "\n"
                                                                      << "[" << fmt_params << "] != \n[" << FMT_CB(os, for (auto it = params.begin(); it != params.end(); ++it) os << *it << ", ") << "]"
                    );
                }

                return true;
            }

            const MIR::AsmParam& p(size_t i) const {
                return e.params.at(i);
            }

            const MIR::Param& input(size_t i) const {
                MIR_ASSERT(m_mir_res, e.params.at(i).as_Reg().input, "Parameter " << i << " isn't a register input");
                return *e.params.at(i).as_Reg().input;
            }

            const MIR::LValue& output(size_t i) const {
                MIR_ASSERT(m_mir_res, e.params.at(i).as_Reg().output, "Parameter " << i << " isn't a register output");
                return *e.params.at(i).as_Reg().output;
            }

        private:
            /// Get a description of the parameter's important attributes
            static std::string get_param_text(const MIR::AsmParam& p) {
                TU_MATCH_HDRA( (p), {)
                TU_ARMA(Reg, e) {
                    TU_MATCH_HDRA( (e.spec), { )
                    TU_ARMA(Explicit, n) {
                                return FMT(get_dir_text(e.dir) << "=" << n);
                            }
                            TU_ARMA(Class, c) {
                                return FMT(get_dir_text(e.dir) << ":" << AsmCommon::to_string(c));
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

            static const char* get_dir_text(const AsmCommon::Direction& d) {
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

            static bool check_list(const std::vector<std::string>& have, const ::std::initializer_list<const char*>& exp) {
                if (have.size() != exp.size()) {
                    return false;
                }
                auto h_it = have.begin();
                auto e_it = exp.begin();
                for (; h_it != have.end(); ++h_it, ++e_it) {
                    if (*h_it != *e_it) {
                        return false;
                    }
                }
                return true;
            }
        };

        void emit_asm2_gcc(const ::MIR::TypeResolve& mir_res, const ::MIR::Statement& stmt, unsigned indent_level) {
            auto indent = RepeatLitStr{"\t", static_cast<int>(indent_level)};
            Asm2TplMatch m{mir_res, stmt};
            const auto& se = stmt.as_Asm2();

            // The following clobber overlaps with an output
            // __asm__ ("cpuid": "=a" (var0), "=b" (var1), "=c" (var2), "=d" (var3): "a" (arg0), "c" (var4): "rbx");
            if (m.matches_template({"movq %rbx, {0:r}", "cpuid", "xchgq %rbx, {0:r}"}, {"lateout:reg", "inlateout=eax", "inlateout=ecx", "lateout=edx"})) {
                //if( e.clobbers.size() == 1 && e.clobbers[0] == "rbx" ) {
                m_of << indent << "__asm__(\"cpuid\"";
                m_of << " : ";
                m_of << "\"=a\" (";
                emit_lvalue(m.output(1));
                m_of << "), ";
                m_of << "\"=b\" (";
                emit_lvalue(m.output(0));
                m_of << "), ";
                m_of << "\"=c\" (";
                emit_lvalue(m.output(2));
                m_of << "), ";
                m_of << "\"=d\" (";
                emit_lvalue(m.output(3));
                m_of << ")";
                m_of << " : ";
                m_of << "\"a\" (";
                emit_param(m.input(1));
                m_of << "), ";
                m_of << "\"c\" (";
                emit_param(m.input(2));
                m_of << ")";
                m_of << " );\n";
                return;
                //}
            } else if (m.matches_template({"mov {0:r}, rbx", "cpuid", "xchg {0:r}, rbx"}, {"out:reg", "inout=eax", "inout=ecx", "out=edx"})) // 1.74 libstd_detect
            {
                m_of << indent << "__asm__(\"cpuid\"";
                m_of << " : ";
                m_of << "\"=a\" (";
                emit_lvalue(m.output(1));
                m_of << "), ";
                m_of << "\"=b\" (";
                emit_lvalue(m.output(0));
                m_of << "), ";
                m_of << "\"=c\" (";
                emit_lvalue(m.output(2));
                m_of << "), ";
                m_of << "\"=d\" (";
                emit_lvalue(m.output(3));
                m_of << ")";
                m_of << " : ";
                m_of << "\"a\" (";
                emit_param(m.input(1));
                m_of << "), ";
                m_of << "\"c\" (";
                emit_param(m.input(2));
                m_of << ")";
                m_of << " );\n";
                return;
            } else if (m.matches_template({"btl {1:e}, ({0})", "setc {2}"}, {"in:reg", "in:reg", "out:reg_byte"})) {
                m_of << indent << "__asm__(\"bt %1, (%2); setc %0\"";
                m_of << " : \"=r\"(";
                emit_lvalue(m.output(2));
                m_of << ")";
                m_of << " : \"r\"(";
                emit_param(m.input(0));
                m_of << "), \"r\"(";
                emit_param(m.input(1));
                m_of << ")";
                m_of << ");\n";
                return;
            } else if (m.matches_template({"btcl {1:e}, ({0})", "setc {2}"}, {"in:reg", "in:reg", "out:reg_byte"})) {
                m_of << indent << "__asm__(\"btc %1, (%2); setc %0\"";
                m_of << " : \"=r\"(";
                emit_lvalue(m.output(2));
                m_of << ")";
                m_of << " : \"r\"(";
                emit_param(m.input(0));
                m_of << "), \"r\"(";
                emit_param(m.input(1));
                m_of << ")";
                m_of << ");\n";
                return;
            } else if (m.matches_template({"btrl {1:e}, ({0})", "setc {2}"}, {"in:reg", "in:reg", "out:reg_byte"})) {
                m_of << indent << "__asm__(\"btr %1, (%2); setc %0\"";
                m_of << " : \"=r\"(";
                emit_lvalue(m.output(2));
                m_of << ")";
                m_of << " : \"r\"(";
                emit_param(m.input(0));
                m_of << "), \"r\"(";
                emit_param(m.input(1));
                m_of << ")";
                m_of << ");\n";
                return;
            } else if (m.matches_template({"btsl {1:e}, ({0})", "setc {2}"}, {"in:reg", "in:reg", "out:reg_byte"})) {
                m_of << indent << "__asm__(\"bts %1, (%2); setc %0\"";
                m_of << " : \"=r\"(";
                emit_lvalue(m.output(2));
                m_of << ")";
                m_of << " : \"r\"(";
                emit_param(m.input(0));
                m_of << "), \"r\"(";
                emit_param(m.input(1));
                m_of << ")";
                m_of << ");\n";
                return;
            }
            // HACK: Abort on various `v*` operations, as they have overly complex register specs that gcc doesn't like
            else if (se.lines[0].frags.size() > 0 && (false || se.lines[0].frags[0].before.find("vmov") == 0 || se.lines[0].frags[0].before.find("vexpand") == 0 || se.lines[0].frags[0].before.find("vpexpand") == 0)) {
                m_of << "abort();\n";
                return;
            } else {
                std::vector<unsigned> arg_mappings(se.params.size(), UINT_MAX);
                // If there is an explicit register, create a block and add `register uintptr_t asm_REGNAME asm("REGNAME");`
                // - Requires updating the arg mappings, as doing so would remove the argument from the list.
                bool block_open = false;
                for (size_t i = 0; i < se.params.size(); i++) {
                    if (const auto* pe = se.params[i].opt_Reg()) {
                        if (!pe->input && !pe->output) {
                        } else if (const auto* regname_p = pe->spec.opt_Explicit()) {
                            arg_mappings[i] = UINT_MAX - 1;
                            if (!block_open) {
                                block_open = true;
                                m_of << indent << "{\n";
                            }
                            m_of << indent << "register uintptr_t asm_" << *regname_p << " asm(\"" << *regname_p << "\")";
                            if (pe->input) {
                                m_of << " = (uintptr_t)";
                                emit_param(*pe->input);
                            }
                            m_of << ";\n";
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
                            if (!block_open) {
                                block_open = true;
                                m_of << indent << "{\n";
                            }
                            m_of << indent << "uintptr_t asm_anon_" << outputs.size() << " = 0;\n";

                            arg_mappings[i] = outputs.size();
                            outputs.push_back(pe);
                        } else if (pe->output) {
                            arg_mappings[i] = outputs.size();
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
                            arg_mappings[i] = outputs.size() + inputs.size();
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

                m_of << indent << "__asm__ ";
                m_of << "__volatile__"; // Default everything to volatile
                m_of << "(\"";
                if ((TargetGetCurSpec().m_arch.m_name == "x86" || TargetGetCurSpec().m_arch.m_name == "x86_64") && !se.options.att_syntax) {
                    m_of << ".intel_syntax noprefix; ";
                }
                bool escape_percent = true || !inputs.empty() || !outputs.empty();
                for (const auto& l : se.lines) {
                    for (const auto& f : l.frags) {
                        m_of << FmtGccAsm(f.before, escape_percent);
                        MIR_ASSERT(mir_res, arg_mappings.at(f.index) != UINT_MAX, stmt);
                        m_of << "%";
                        if (arg_mappings.at(f.index) == UINT8_MAX - 1) {
                            m_of << se.params[f.index].as_Reg().spec.as_Explicit();
                            continue;
                        }
                        switch (f.modifier) {
                            case '\0':
                                break;
                            case 'r':
                                m_of << 'q'; // x86: `q` selects rax explicitly
                                break;
                            case 'e':
                                m_of << 'k'; // x86: `k` selects eax instead of rax
                                break;
                            default:
                                MIR_TODO(mir_res, "Asm2 GCC: modifier " << f.modifier << " - " << stmt);
                        }
                        m_of << arg_mappings.at(f.index);
                    }
                    m_of << FmtGccAsm(l.trailing, escape_percent);
                    m_of << ";\\n ";
                }
                if ((TargetGetCurSpec().m_arch.m_name == "x86" || TargetGetCurSpec().m_arch.m_name == "x86_64") && !se.options.att_syntax) {
                    m_of << ".att_syntax; ";
                }
                m_of << "\" :";
                for (size_t i = 0; i < outputs.size(); i++) {
                    const auto& p = *outputs[i];
                    if (i != 0) {
                        m_of << ",";
                    }
                    m_of << " ";
                    m_of << "\"";
                    if (!p.output && !p.input) {
                        m_of << "+";
                    } else {
                        m_of << (p.input ? "+" : "=");
                    }
                    TU_MATCH_HDRA((p.spec), {)
                    TU_ARMA(Class, c)
                        // https://gcc.gnu.org/onlinedocs/gcc/Machine-Constraints.html
                        switch(c)
                        {
                            // x86
                            case AsmCommon::RegisterClass::x86_reg:
                                m_of << "r";
                                break;
                            case AsmCommon::RegisterClass::x86_reg_abcd:
                                m_of << "Q";
                                break;
                            case AsmCommon::RegisterClass::x86_reg_byte:
                                m_of << "q";
                                break;
                            case AsmCommon::RegisterClass::x86_xmm:
                                m_of << "x";
                                break;
                            case AsmCommon::RegisterClass::x86_ymm:
                                m_of << "x";
                                break;
                            case AsmCommon::RegisterClass::x86_zmm:
                                m_of << "v";
                                break;
                            case AsmCommon::RegisterClass::x86_kreg:
                                m_of << "Yk";
                                break;
                            // riscv
                            case AsmCommon::RegisterClass::riscv_reg:
                                m_of << "r";
                                break;
                            case AsmCommon::RegisterClass::riscv_freg:
                                m_of << "f";
                                break;
                        }
                        TU_ARMA(Explicit, name) {
                            m_of << "r";
                        }
                    }
                    m_of << "\" (";
                    if( !p.output ) {
                        m_of << "asm_anon_" << i;
                    }
                    else if( const auto* regname_p = p.spec.opt_Explicit() ) {
                        m_of << "asm_" << *regname_p;
                    }
                    else {
                        emit_lvalue(*p.output);
                    }
                    m_of << ")";
                }
                m_of << " :";
                for (size_t i = 0; i < inputs.size(); i++) {
                    const auto& p = *inputs[i];
                    if (i != 0) {
                        m_of << ",";
                    }
                    m_of << " ";
                    TU_MATCH_HDRA((p), {)
                    TU_ARMA(Reg, r) {
                            m_of << "\"";
                        TU_MATCH_HDRA((r.spec), {)
                        TU_ARMA(Class, c)
                            switch(c)
                            {
                                    // x86
                                    case AsmCommon::RegisterClass::x86_reg:
                                        m_of << "r";
                                        break;
                                    case AsmCommon::RegisterClass::x86_reg_abcd:
                                        m_of << "Q";
                                        break;
                                    case AsmCommon::RegisterClass::x86_reg_byte:
                                        m_of << "q";
                                        break;
                                    case AsmCommon::RegisterClass::x86_xmm:
                                        m_of << "x";
                                        break;
                                    case AsmCommon::RegisterClass::x86_ymm:
                                        m_of << "x";
                                        break;
                                    case AsmCommon::RegisterClass::x86_zmm:
                                        m_of << "v";
                                        break;
                                    case AsmCommon::RegisterClass::x86_kreg:
                                        m_of << "Yk";
                                        break;
                                    // riscv
                                    case AsmCommon::RegisterClass::riscv_reg:
                                        m_of << "r";
                                        break;
                                    case AsmCommon::RegisterClass::riscv_freg:
                                        m_of << "f";
                                        break;
                                }
                                TU_ARMA(Explicit, name) {
                                    auto it = ::std::find(outputs.begin(), outputs.end(), &r);
                                    if (it != outputs.end()) {
                                        m_of << (it - outputs.begin());
                                    } else {
                                        m_of << "r";
                                    }
                                }
                        }
                        assert(r.input);
                        m_of << "\" (";
                        if( const auto* regname_p = p.as_Reg().spec.opt_Explicit() ) {
                                m_of << "asm_" << *regname_p;
                        }
                        else {
                                emit_param(*r.input);
                        }
                        m_of << ")";
                        }
                        TU_ARMA(Const, c) MIR_TODO(mir_res, "Asm2 GCC - Const: " << stmt);
                        TU_ARMA(Sym, c) MIR_TODO(mir_res, "Asm2 GCC - Sym: " << stmt);
                    }
                }
                m_of << ":";
                for (size_t i = 0; i < clobbers.size(); i++) {
                    if (i > 0) {
                        m_of << ",";
                    }
                    m_of << " \"" << clobbers[i] << "\"";
                }
                m_of << ");\n";
                for (size_t i = 0; i < se.params.size(); i++) {
                    if (const auto* pe = se.params[i].opt_Reg()) {
                        if (const auto* regname_p = pe->spec.opt_Explicit()) {
                            if (pe->output) {
                                m_of << indent;
                                emit_lvalue(*pe->output);
                                m_of << " = ";
                                HIR::TypeRef tmp;
                                m_of << "(";
                                emit_ctype(m_mir_res->get_lvalue_type(tmp, *pe->output));
                                m_of << ")";
                                m_of << "asm_" << *regname_p << ";\n";
                            }
                        }
                    }
                }
                if (block_open) {
                    m_of << indent << "}\n";
                }
            }
        }

    private:
        const ::HIR::TypeData* monomorphise_fcn_return(::HIR::TypeRef& tmp, const ::HIR::Function& item, const TransParams& params) {
            bool has_erased = visit_ty_with(item.m_return, [&](const auto& x) {
                return x->is_ErasedType();
            });

            if (has_erased || monomorphise_type_needed(item.m_return)) {
                // If there's an erased type, make a copy with the erased type expanded
                if (has_erased) {
                    tmp = clone_ty_with(m_crate.m_types, sp, item.m_return, [&](const auto& x, auto& out) {
                        if (const auto* te = x->opt_ErasedType()) {
                            if (const auto* e = te->m_inner.opt_Fcn()) {
                                out = item.m_code.m_erased_types.at(e->m_index);
                                return true;
                            }
                        }
                        return false;
                    });
                    tmp = params.monomorph_type(Span(), tmp);
                } else {
                    tmp = params.monomorph_type(Span(), item.m_return);
                }
                m_resolve.expand_associated_types(Span(), tmp);
                return tmp;
            } else {
                return item.m_return;
            }
        }

        void emit_function_header(const ::HIR::Path& p, const ::HIR::Function& item, const TransParams& params) {
            ::HIR::TypeRef tmp;
            const auto& ret_ty = monomorphise_fcn_return(tmp, item, params);
            if (item.m_markings.is_naked) {
                m_of << "__attribute__((naked)) ";

            }
            auto cb = FMT_CB(
                ss,
                // TODO: Cleaner ABI handling
                ss << " " << TransMangle(p) << "(";
                if (item.m_args.size() == 0) { ss << "void)"; } else {
                    for (unsigned int i = 0; i < item.m_args.size(); i++) {
                        ss << "\n\t\t";
                        auto ty = params.monomorph(m_resolve, item.m_args[i].second);
                        this->emit_ctype(ty, FMT_CB(os, os << "arg" << i;));
                        if (item.m_variadic || i + 1 < item.m_args.size()) {
                            m_of << ",";
                        }
                        m_of << " // " << ty;
                    }

                    if (item.m_variadic) {
                        m_of << "\n\t\t...";
                    }

                    ss << "\n\t\t)";
                }
            );
            if (ret_ty != m_crate.m_types.unit()) {
                emit_ctype(ret_ty, cb);
            } else {
                m_of << "void " << cb;
            }
            m_of << " // -> " << ret_ty << "\n";
        }

        void emit_intrinsic_call(const RcString& name, const ::HIR::PathParams& params, const ::MIR::Terminator::Data_Call& e) {
            const auto& mir_res = *m_mir_res;
            enum class Ordering {
                SeqCst,
                Acquire,
                Release,
                Relaxed,
                AcqRel,
            };
            auto get_atomic_ty_gcc = [&](Ordering o) -> const char* {
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
            auto get_atomic_ordering = [&](const RcString& name, size_t prefix_len) -> Ordering {
                if (name.size() < prefix_len) {
                    return Ordering::SeqCst;
                }
                const char* suffix = name.c_str() + prefix_len;
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
                    MIR_BUG(mir_res, "Unknown atomic ordering suffix - '" << suffix << "'");
                }
                throw "";
            };
            auto get_prim_size = [&mir_res](const ::HIR::TypeData* ty) -> unsigned {
                if (ty->is_Pointer()) {
                    return TargetGetCurSpec().m_arch.m_pointer_bits;
                }
                if (!ty->is_Primitive()) {
                    MIR_BUG(mir_res, "Unknown type for getting primitive size - " << ty);
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
                        return TargetGetCurSpec().m_arch.m_pointer_bits;
                    default:
                        MIR_BUG(mir_res, "Unknown primitive for getting size- " << ty);
                }
            };
            auto get_real_prim_ty = [](HIR::CoreType ct) -> HIR::CoreType {
                switch (ct) {
                    case HIR::CoreType::Usize:
                        if (TargetGetCurSpec().m_arch.m_pointer_bits == 64) {
                            return ::HIR::CoreType::U64;
                        }
                        if (TargetGetCurSpec().m_arch.m_pointer_bits == 32) {
                            return ::HIR::CoreType::U32;
                        }
                        BUG(Span(), "");
                    case HIR::CoreType::Isize:
                        if (TargetGetCurSpec().m_arch.m_pointer_bits == 64) {
                            return ::HIR::CoreType::I64;
                        }
                        if (TargetGetCurSpec().m_arch.m_pointer_bits == 32) {
                            return ::HIR::CoreType::I32;
                        }
                        BUG(Span(), "");
                    default:
                        return ct;
                }
            };
            auto emit_atomic_cast = [&]() {
                m_of << "(";
                emit_ctype(params.m_types.at(0));
                m_of << "*)";
            };
            // Rust's pointer atomic RMW intrinsics carry their delta in the
            // pointer value itself.  Represent them as integer atomics in C:
            // C pointer fetch_add takes an element count and would both reject
            // the operand type and scale a byte offset.
            const bool atomic_type_is_pointer = params.m_types.size() > 0
                && params.m_types.at(0)->is_Pointer();
            auto emit_atomic_rmw_cast = [&]() {
                if (atomic_type_is_pointer) {
                    m_of << "(";
                    emit_ctype(params.m_types.at(0));
                    m_of << ")";
                }
            };
            auto emit_atomic_rmw_operand = [&](const ::MIR::Param& param) {
                if (atomic_type_is_pointer) {
                    m_of << "(uintptr_t)";
                }
                emit_param(param);
            };
            auto emit_atomic_cxchg = [&](const auto& e, Ordering o_succ, Ordering o_fail, bool is_weak) {
                switch (o_fail) {
                    case Ordering::Release:
                        o_fail = Ordering::Relaxed;
                        break;
                    case Ordering::AcqRel:
                        o_fail = Ordering::Acquire;
                        break;
                    default:
                        break;
                }
                if (type_is_emulated_i128(params.m_types.at(0))) {
                    emit_ctype(params.m_types.at(0), FMT_CB(ss, ss << " mrustc_atomic_desired";));
                    m_of << " = ";
                    emit_param(e.args.at(2));
                    m_of << ";\n\t";
                }
                emit_lvalue(e.ret_val);
                m_of << "._0 = ";
                emit_param(e.args.at(1));
                m_of << ";\n\t";
                emit_lvalue(e.ret_val);
                m_of << "._1 = " << (type_is_emulated_i128(params.m_types.at(0)) ? "__atomic_compare_exchange(" : "__atomic_compare_exchange_n(");
                emit_atomic_cast();
                emit_param(e.args.at(0));
                m_of << ", &";
                emit_lvalue(e.ret_val);
                m_of << "._0"; // Expected (i.e. the check value)
                m_of << ", ";
                if (type_is_emulated_i128(params.m_types.at(0))) {
                    m_of << "&mrustc_atomic_desired";
                } else {
                    emit_param(e.args.at(2)); // `desired` (the new value for the slot if equal)
                }
                m_of << ", " << (is_weak ? "true" : "false");
                m_of << ", " << get_atomic_ty_gcc(o_succ) << ", " << get_atomic_ty_gcc(o_fail) << ")";

            };
            auto emit_atomic_arith = [&](AtomicOp op, Ordering ordering) {
                emit_lvalue(e.ret_val);
                m_of << " = ";
                emit_atomic_rmw_cast();
                switch (op) {
                    case AtomicOp::Add:
                        m_of << "__atomic_fetch_add";
                        break;
                    case AtomicOp::Sub:
                        m_of << "__atomic_fetch_sub";
                        break;
                    case AtomicOp::And:
                        m_of << "__atomic_fetch_and";
                        break;
                    case AtomicOp::Or:
                        m_of << "__atomic_fetch_or";
                        break;
                    case AtomicOp::Xor:
                        m_of << "__atomic_fetch_xor";
                        break;
                }
                m_of << "(";
                if (atomic_type_is_pointer) {
                    m_of << "(uintptr_t *)";
                } else {
                    emit_atomic_cast();
                }
                emit_param(e.args.at(0));
                m_of << ", ";
                emit_atomic_rmw_operand(e.args.at(1));
                m_of << ", " << get_atomic_ty_gcc(ordering) << ")";

            };
            if (name == "size_of") {
                size_t size = 0;
                MIR_ASSERT(mir_res, TargetGetSizeOf(sp, m_resolve, params.m_types.at(0), size), "Can't get size of " << params.m_types.at(0));
                emit_lvalue(e.ret_val);
                m_of << " = " << size;
            } else if (name == "offset_of") {
                size_t val = mir_res.intrinsic_offset_of(params.m_types.at(0), e.args);
                emit_lvalue(e.ret_val);
                m_of << " = " << val;
            } else if (name == "min_align_of" || name == "align_of") {
                size_t align = 0;
                MIR_ASSERT(mir_res, TargetGetAlignOf(sp, m_resolve, params.m_types.at(0), align), "Can't get alignment of " << params.m_types.at(0));
                emit_lvalue(e.ret_val);
                m_of << " = " << align;
            } else if (name == "size_of_val") {
                emit_lvalue(e.ret_val);
                m_of << " = ";
                const auto& ty = params.m_types.at(0);
                // Get the unsized type and use that in place of MetadataType
                auto inner_ty = get_inner_unsized_type(ty);
                if (inner_ty == ::HIR::TypeRef()) {
                    size_t size = 0;
                    MIR_ASSERT(mir_res, TargetGetSizeOf(sp, m_resolve, ty, size), "Can't get size of " << ty);
                    m_of << size;
                }
                // slice metadata (`[T]` and `str`)
                else if (inner_ty->is_Slice() || inner_ty == ::HIR::CoreType::Str) {
                    bool align_needed = false;
                    size_t item_size = 0;
                    size_t item_align = 0;
                    if (const auto* te = inner_ty->opt_Slice()) {
                        MIR_ASSERT(mir_res, TargetGetSizeAndAlignOf(sp, m_resolve, te->inner, item_size, item_align), "Can't get size of " << te->inner);
                    } else {
                        assert(inner_ty == ::HIR::CoreType::Str);
                        item_size = 1;
                        item_align = 1;
                    }
                    if (!ty->is_Slice() && !ty->is_Primitive()) {
                        // TODO: What if the wrapper has no other fields?
                        // Get the alignment and check if it's higher than the item alignment
                        size_t wrapper_align = 0, wrapper_size_ignore = 0;
                        MIR_ASSERT(mir_res, TargetGetSizeAndAlignOf(sp, m_resolve, ty, wrapper_size_ignore, wrapper_align), "Can't get align of " << ty);
                        if (wrapper_align > item_align) {
                            item_align = wrapper_align;
                            align_needed = true;
                            m_of << "ALIGN_TO(";
                        }
                        const auto* repr = TargetGetTypeRepr(sp, m_resolve, ty);
                        m_of << repr->fields.back().offset << " + ";
                    }
                    emit_param(e.args.at(0));
                    m_of << ".META * " << item_size;
                    if (align_needed) {
                        m_of << ", " << item_align << ")";
                    }
                }
                // Trait object metadata.
                else if (inner_ty->is_TraitObject()) {
                    emit_trait_object_dst_size(ty, e.args.at(0));
                } else {
                    MIR_BUG(mir_res, "Unknown inner unsized type " << inner_ty << " for " << ty);
                }
                // TODO: Align up
            } else if (name == "min_align_of_val" || name == "align_of_val") {
                emit_lvalue(e.ret_val);
                m_of << " = ";
                const auto& ty = params.m_types.at(0);
#if 1
                auto inner_ty = get_inner_unsized_type(ty);
                if (inner_ty == ::HIR::TypeRef()) {
                    m_of << "ALIGNOF(";
                    emit_ctype(ty);
                    m_of << ")";
                } else if (const auto* te = inner_ty->opt_Slice()) {
                    m_of << "ALIGNOF(";
                    if (ty->is_Slice()) {
                        emit_ctype(te->inner);
                    } else {
                        emit_ctype(ty);
                    }
                    m_of << ")";
                } else if (inner_ty == ::HIR::CoreType::Str) {
                    if (!ty->is_Primitive()) {
                        m_of << "ALIGNOF(";
                        emit_ctype(ty);
                        m_of << ")";
                    } else {
                        m_of << "1";
                    }
                } else if (inner_ty->is_TraitObject()) {
                    emit_trait_object_dst_align(ty, e.args.at(0));
                } else {
                    MIR_BUG(mir_res, "Unknown inner unsized type " << inner_ty << " for " << ty);
                }
#else
                switch (metadata_type(ty)) {
                    case MetadataType::None:
                        m_of << "ALIGNOF(";
                        emit_ctype(ty);
                        m_of << ")";
                        break;
                    case MetadataType::Slice: {
                        // TODO: Have a function that fetches the inner type for types like `Path` or `str`
                        const auto& ity = *ty->as_Slice().inner;
                        m_of << "ALIGNOF(";
                        emit_ctype(ity);
                        m_of << ")";
                        break;
                    }
                    case MetadataType::TraitObject:
                        m_of << "((VTABLE_HDR*)";
                        emit_param(e.args.at(0));
                        m_of << ".META)->align";
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
                const auto& arg_ty_tuple = params.m_types.at(0)->as_Tuple();
                const auto& arg = e.args.at(0).as_LValue();
                // Note: arg 1 is the constant function
                const auto& fcn_path = *e.args.at(2).as_Constant().as_Function().p;

                // Reuse ordinary call emission for the runtime branch of const_eval_select.
                ::std::vector<MIR::Param> args;
                args.reserve(arg_ty_tuple.size());
                for (size_t i = 0; i < arg_ty_tuple.size(); i++) {
                    args.push_back(MIR::LValue::new_Field(arg.clone(), i));
                }
                auto pseudo_term = MIR::Terminator::Data_Call{e.ret_block, MIR::UnwindAction::make_Continue({}), e.ret_val.clone(), MIR::CallTarget::make_Path(fcn_path.clone()), std::move(args)};
                emit_term_call(mir_res, pseudo_term, 1);
            }
            // --- Type identity ---
            else if (name == "type_id") {
                const auto& ty = params.m_types.at(0);
                // NOTE: Would define the typeid here, but it has to be public
                emit_lvalue(e.ret_val);
                m_of << " = ";
                if (m_options.emulated_i128) {
                    m_of << "make128(";
                }
                m_of << "(uintptr_t)&__typeid_" << TransMangle(ty);
                if (m_options.emulated_i128) {
                    m_of << ")";
                }
            } else if (name == "type_name") {
                auto name = mir_res.intrinsic_type_name(params.m_types.at(0));
                emit_lvalue(e.ret_val);
                m_of << ".PTR = \"" << FmtEscaped(name) << "\";\n\t";
                emit_lvalue(e.ret_val);
                m_of << ".META = " << name.size() << "";
            } else if (name == "transmute" || name == "transmute_unchecked") {
                const auto& ty_src = params.m_types.at(0);
                const auto& ty_dst = params.m_types.at(1);
                auto is_ptr = [](const ::HIR::TypeData* ty) {
                    return ty->is_Borrow() || ty->is_Pointer();
                };
                if (this->type_is_bad_zst(ty_dst)) {
                    m_of << "/* zst */";
                } else if (e.args.at(0).is_Constant()) {
                    m_of << "{ ";
                    emit_ctype(ty_src, FMT_CB(s, s << "v";));
                    m_of << " = ";
                    emit_param(e.args.at(0));
                    m_of << "; ";
                    m_of << "memcpy( &";
                    emit_lvalue(e.ret_val);
                    m_of << ", &v, sizeof(";
                    emit_ctype(ty_dst);
                    m_of << ")); ";
                    m_of << "}";
                } else if (is_ptr(ty_dst) && is_ptr(ty_src)) {
                    auto src_meta = metadata_type(ty_src->is_Pointer() ? ty_src->as_Pointer().inner : ty_src->as_Borrow().inner);
                    auto dst_meta = metadata_type(ty_dst->is_Pointer() ? ty_dst->as_Pointer().inner : ty_dst->as_Borrow().inner);
                    if (src_meta == MetadataType::None || src_meta == MetadataType::Zero) {
                        MIR_ASSERT(*m_mir_res, dst_meta == MetadataType::None || dst_meta == MetadataType::Zero, "Transmuting to fat pointer from thin: " << ty_src << " -> " << ty_dst);
                        emit_lvalue(e.ret_val);
                        m_of << " = (";
                        emit_ctype(ty_dst);
                        m_of << ")";
                        emit_param(e.args.at(0));
                    } else if (dst_meta == MetadataType::None || dst_meta == MetadataType::Zero) {
                        MIR_BUG(*m_mir_res, "Transmuting from fat pointer to thin: (" << src_meta << "->" << dst_meta << ") " << ty_src << " -> " << ty_dst);
                    } else if (src_meta != dst_meta) {
                        emit_lvalue(e.ret_val);
                        m_of << ".PTR = ";
                        emit_param(e.args.at(0));
                        m_of << ".PTR; ";
                        emit_lvalue(e.ret_val);
                        m_of << ".META = ";
                        switch (dst_meta) {
                            case MetadataType::Unknown:
                                assert(!"Impossible");
                            case MetadataType::None:
                                assert(!"Impossible");
                            case MetadataType::Zero:
                                assert(!"Impossible");
                            case MetadataType::Slice:
                                m_of << "(size_t)";
                                break;
                            case MetadataType::TraitObject:
                                m_of << "(const void*)";
                                break;
                        }
                        emit_param(e.args.at(0));
                        m_of << ".META";
                    } else {
                        emit_lvalue(e.ret_val);
                        m_of << " = ";
                        emit_param(e.args.at(0));
                    }
                } else {
                    m_of << "memcpy( &";
                    emit_lvalue(e.ret_val);
                    m_of << ", &";
                    emit_param(e.args.at(0));
                    m_of << ", sizeof(";
                    emit_ctype(ty_src);
                    m_of << "))";
                }
            } else if (name == "float_to_int_unchecked") {
                const auto& src_ty = params.m_types.at(0);
                const auto& dst_ty = params.m_types.at(1);
                // Unchecked (can return `undef`) cast from a float to an integer
                if (this->type_is_emulated_i128(dst_ty)) {
                    m_of << "abort()";
                    //emit_lvalue(e.ret_val); m_of << " = ("; emit_ctype(dst_ty); m_of << ")"; emit_param(e.args.at(0));
                } else if (src_ty == HIR::CoreType::F16 || src_ty == HIR::CoreType::F128) {
                    m_of << "abort()";
                } else {
                    emit_lvalue(e.ret_val);
                    m_of << " = (";
                    emit_ctype(dst_ty);
                    m_of << ")";
                    emit_param(e.args.at(0));
                }
            } else if (name == "copy_nonoverlapping" || name == "copy") {
                if (this->type_is_bad_zst(params.m_types.at(0))) {
                    m_of << "/* zst */";
                    return;
                }
                if (name == "copy") {
                    m_of << "memmove";
                } else {
                    m_of << "memcpy";
                }
                // 0: Source, 1: Destination, 2: Count
                m_of << "( ";
                emit_param(e.args.at(1));
                m_of << ", ";
                emit_param(e.args.at(0));
                m_of << ", ";
                emit_param(e.args.at(2));
                m_of << " * sizeof(";
                emit_ctype(params.m_types.at(0));
                m_of << ")";
                m_of << ")";
            }
            // NOTE: This is generic, and fills count*sizeof(T) (unlike memset)
            else if (name == "write_bytes") {
                if (this->type_is_bad_zst(params.m_types.at(0))) {
                    m_of << "/* zst */";
                    return;
                }
                // 0: Destination, 1: Value, 2: Count
                m_of << "if( ";
                emit_param(e.args.at(2));
                m_of << " > 0) memset( ";
                emit_param(e.args.at(0));
                m_of << ", ";
                emit_param(e.args.at(1));
                m_of << ", ";
                emit_param(e.args.at(2));
                m_of << " * sizeof(";
                emit_ctype(params.m_types.at(0));
                m_of << ")";
                m_of << ")";
            } else if (name == "compare_bytes") {
                // A raw memcmp
                emit_lvalue(e.ret_val);
                m_of << " = memcmp( ";
                emit_param(e.args.at(0));
                m_of << ", ";
                emit_param(e.args.at(1));
                m_of << ", ";
                emit_param(e.args.at(2));
                m_of << ")";
            } else if (name == "raw_eq") {
                size_t size = 0;
                MIR_ASSERT(mir_res, TargetGetSizeOf(sp, m_resolve, params.m_types.at(0), size), "Can't get size of " << params.m_types.at(0));

                // Raw byte equality (could be implemented without a memcmp call, if desired)
                emit_lvalue(e.ret_val);
                m_of << " = (0 == memcmp(";
                emit_param(e.args.at(0));
                m_of << ", ";
                emit_param(e.args.at(1));
                m_of << ", ";
                m_of << size;
                m_of << "))";
            } else if (name == "three_way_compare") {
                const auto& t = params.m_types.at(0);
                if (type_is_emulated_i128(t)) {
                    emit_lvalue(e.ret_val);
                    m_of << ".TAG = ";
                    m_of << (t == ::HIR::CoreType::U128 ? "cmp128" : "cmp128s");
                    m_of << "(";
                    emit_param(e.args.at(0));
                    m_of << ", ";
                    emit_param(e.args.at(1));
                    m_of << ");\n";
                } else {
                    emit_lvalue(e.ret_val);
                    m_of << ".TAG = (";
                    emit_param(e.args.at(0));
                    m_of << " == ";
                    emit_param(e.args.at(1));
                    m_of << " ? 0 : (";
                    emit_param(e.args.at(0));
                    m_of << " < ";
                    emit_param(e.args.at(1));
                    m_of << " ? -1 : 1));\n";
                }
                return;
            } else if (name == "forget") {
                // Nothing needs to be done, this just stops the destructor from running.
            } else if (name == "drop_in_place") {
                emit_destructor_call(::MIR::LValue::new_Deref(e.args.at(0).as_LValue().clone()), params.m_types.at(0), true, /*indent_level=*/1 /* TODO: get from caller */);
            }
            // --- Type traits
            else if (name == "needs_drop") {
                // Returns `true` if the actual type given as `T` requires drop glue;
                // returns `false` if the actual type provided for `T` implements `Copy`. (Either otherwise)
                // NOTE: libarena assumes that this returns `true` iff T doesn't require drop glue.
                const auto& ty = params.m_types.at(0);
                emit_lvalue(e.ret_val);
                m_of << " = ";
                if (m_resolve.type_needs_drop_glue(mir_res.sp, ty)) {
                    m_of << "true";
                } else {
                    m_of << "false";
                }
            }
            // --- Initialisation (or lack thereof)
            else if (name == "uninit") {
                // Do nothing, leaves the destination undefined
                // TODO: This makes the C compiler warn
            } else if (name == "init") {
                m_of << "memset( &";
                emit_lvalue(e.ret_val);
                m_of << ", 0, sizeof(";
                emit_ctype(params.m_types.at(0));
                m_of << "))";
            } else if (name == "move_val_init") {
                if (!this->type_is_bad_zst(params.m_types.at(0))) {
                    m_of << "*";
                    emit_param(e.args.at(0));
                    m_of << " = ";
                    emit_param(e.args.at(1));
                }
            } else if (name == "abort") {
                m_of << "abort()";
            } else if (name == "try" || name == "catch_unwind") {
                m_of << "{ try { ";
                emit_param(e.args.at(0));
                m_of << "(";
                emit_param(e.args.at(1));
                m_of << "); ";
                emit_lvalue(e.ret_val);
                m_of << " = 0; } catch (mrustc_panic& panic) { (";
                emit_param(e.args.at(2));
                m_of << ")(";
                emit_param(e.args.at(1));
                m_of << ", (uint8_t*)panic.rust_exception); ";
                emit_lvalue(e.ret_val);
                m_of << " = 1; } }";
            }
            // --- #[track_caller]
            else if (name == "caller_location") {
                //m_of << "abort()";
                auto p = m_crate.get_lang_item_path_opt("panic_location");
                m_of << "static struct ";
                if (p == HIR::SimplePath()) {
                    m_of << "s_ZRG2cE9core0_0_05panic8Location0g";
                } else {
                    m_of << "s_" << TransMangle(p);
                }
                m_of << " mrustc_empty_caller_location = {._0={._0={(void*)\"\",0}},._1=0,._2=0};";
                emit_lvalue(e.ret_val);
                m_of << " = &mrustc_empty_caller_location"; // TODO: Hidden ABI for caller location
            }
            // --- Pointer manipulation
            else if (name == "offset") { // addition, with the reqirement that the resultant pointer be in bounds
                emit_lvalue(e.ret_val);
                m_of << " = ";
                emit_param(e.args.at(0));
                m_of << " + ";
                emit_param(e.args.at(1));
            } else if (name == "arith_offset") { // addition, with no requirements
                emit_lvalue(e.ret_val);
                m_of << " = ";
                emit_param(e.args.at(0));
                m_of << " + ";
                emit_param(e.args.at(1));
            } else if (name == "ptr_offset_from") { // effectively subtraction
                emit_lvalue(e.ret_val);
                m_of << " = ";
                emit_param(e.args.at(0));
                m_of << " - ";
                emit_param(e.args.at(1));
            } else if (name == "ptr_guaranteed_eq") {
                emit_lvalue(e.ret_val);
                m_of << " = (";
                emit_param(e.args.at(0));
                m_of << " == ";
                emit_param(e.args.at(1));
                m_of << ")";
            } else if (name == "ptr_guaranteed_ne") {
                emit_lvalue(e.ret_val);
                m_of << " = (";
                emit_param(e.args.at(0));
                m_of << " != ";
                emit_param(e.args.at(1));
                m_of << ")";
            } else if (name == "ptr_guaranteed_cmp") {
                // 0 if not equal, 1 if equal, 2 if could be either
                emit_lvalue(e.ret_val);
                m_of << "= ( (";
                emit_param(e.args.at(0));
                m_of << ") == (";
                emit_param(e.args.at(1));
                m_of << "))";
            } else if (name == "ptr_offset_from_unsigned") {
                // `fn ptr_offset_from_unsigned<T>(ptr: *const T, base: *const T) -> usize`
                emit_lvalue(e.ret_val);
                m_of << "= ( (";
                emit_param(e.args.at(0));
                m_of << ") - (";
                emit_param(e.args.at(1));
                m_of << "))";
            }
            // ----
            else if (name == "bswap") {
                const auto& ty = params.m_types.at(0);
                MIR_ASSERT(mir_res, ty->is_Primitive(), "Invalid type passed to bwsap, must be a primitive, got " << ty);
                if (ty == ::HIR::CoreType::U8 || ty == ::HIR::CoreType::I8) {
                    // Nop.
                    emit_lvalue(e.ret_val);
                    m_of << " = ";
                    emit_param(e.args.at(0));
                } else {
                    emit_lvalue(e.ret_val);
                    m_of << " = ";
                    switch (get_prim_size(ty)) {
                        case 16:
                            m_of << "__builtin_bswap16";
                            break;
                        case 32:
                            m_of << "__builtin_bswap32";
                            break;
                        case 64:
                            m_of << "__builtin_bswap64";
                            break;
                        case 128:
                            m_of << "__builtin_bswap128";
                            break;
                        default:
                            MIR_TODO(mir_res, "bswap<" << ty << ">");
                    }

                    m_of << "(";
                    emit_param(e.args.at(0));
                    m_of << ")";
                }
            } else if (name == "bitreverse") {
                const auto& ty = params.m_types.at(0);
                MIR_ASSERT(mir_res, ty->is_Primitive(), "Invalid type passed to bitreverse. Must be a primitive, got " << ty);
                emit_lvalue(e.ret_val);
                m_of << " = ";
                switch (get_prim_size(ty)) {
                    case 8:
                        m_of << "__mrustc_bitrev8";
                        break;
                    case 16:
                        m_of << "__mrustc_bitrev16";
                        break;
                    case 32:
                        m_of << "__mrustc_bitrev32";
                        break;
                    case 64:
                        m_of << "__mrustc_bitrev64";
                        break;
                    case 128:
                        m_of << "__mrustc_bitrev128";
                        break;
                    default:
                        MIR_TODO(mir_res, "bswap<" << ty << ">");
                }
                m_of << "(";
                emit_param(e.args.at(0));
                m_of << ")";
            }
            // > Obtain the discriminane of a &T as u64
            else if (name == "discriminant_value") {
                const auto& ty = params.m_types.at(0);
                emit_lvalue(e.ret_val);
                m_of << " = ";
                if (!(ty->is_Path() && ty->as_Path().binding.is_Enum())) {
                    m_of << "0";
                } else {
                    const auto* repr = TargetGetTypeRepr(sp, m_resolve, ty);
                    MIR_ASSERT(mir_res, repr, "No repr for enum " << ty);
                    switch (repr->variants.tag()) {
                        case TypeRepr::VariantMode::TAGDEAD:
                            throw "";
                            TU_ARM(repr->variants, None, _e)
                            m_of << "0";
                            break;
                            TU_ARM(repr->variants, Values, ve) {
                                m_of << "(*";
                                emit_param(e.args.at(0));
                                m_of << ")";
                                emit_enum_path(repr, ve.field);
                            }
                            break;
                            TU_ARM(repr->variants, Linear, ve) {
                                const auto& tag_ty = TargetGetInnerType(sp, m_resolve, *repr, ve.field.index, ve.field.sub_fields);
                                const bool pointer_tag = tag_ty->is_Pointer() || tag_ty->is_Borrow() || tag_ty->is_Function();
                                auto emit_tag = [&]() {
                                    if (pointer_tag) {
                                        m_of << "(uintptr_t)";
                                    }
                                    m_of << "(*";
                                    emit_param(e.args.at(0));
                                    m_of << ")";
                                    emit_enum_path(repr, ve.field);
                                };
                                if (ve.uses_niche()) {
                                    m_of << "( ";
                                    emit_tag();
                                    m_of << " < " << ve.offset;
                                    m_of << " ? " << ve.field.index;
                                    m_of << " : ";
                                    emit_tag();
                                    m_of << " - " << ve.offset;
                                    m_of << " )";
                                } else {
                                    emit_tag();
                                }
                            }
                            break;
                            TU_ARM(repr->variants, NonZero, ve) {
                                m_of << "(*";
                                emit_param(e.args.at(0));
                                m_of << ")";
                                emit_enum_path(repr, ve.field);
                                m_of << " ";
                                m_of << (ve.zero_variant ? "==" : "!=");
                                m_of << " 0";
                            }
                            break;
                    }
                }
            }
            // Hints
            else if (name == "unreachable") {
                m_of << "__builtin_unreachable()";

            } else if (name == "assume") {
                // I don't assume :)
            } else if (name == "likely" || name == "unlikely") {
                emit_lvalue(e.ret_val);
                m_of << "= (";
                emit_param(e.args.at(0));
                m_of << ")";
            } else if (name == "black_box") {
                if (!lvalue_is_bad_zst(e.ret_val)) {
                    emit_lvalue(e.ret_val);
                    m_of << "= (";
                    emit_param(e.args.at(0));
                    m_of << ")";
                }
            }
            // Overflowing Arithmetic
            // Overflowing arithmetic maps to compiler intrinsics, with software handling for emulated i128.
            else if (name == "add_with_overflow") {
                if (m_options.emulated_i128 && params.m_types.at(0) == ::HIR::CoreType::U128) {
                    emit_lvalue(e.ret_val);
                    m_of << "._1 = add128_o";
                    m_of << "(";
                    emit_param(e.args.at(0));
                    m_of << ", ";
                    emit_param(e.args.at(1));
                    m_of << ", &";
                    emit_lvalue(e.ret_val);
                    m_of << "._0)";
                } else if (m_options.emulated_i128 && params.m_types.at(0) == ::HIR::CoreType::I128) {
                    emit_lvalue(e.ret_val);
                    m_of << "._1 = add128s_o";
                    m_of << "(";
                    emit_param(e.args.at(0));
                    m_of << ", ";
                    emit_param(e.args.at(1));
                    m_of << ", &";
                    emit_lvalue(e.ret_val);
                    m_of << "._0)";
                } else

                {
                    emit_lvalue(e.ret_val);
                    m_of << "._1 = __builtin_add_overflow";
                    m_of << "(";
                    emit_param(e.args.at(0));
                    m_of << ", ";
                    emit_param(e.args.at(1));
                    m_of << ", &";
                    emit_lvalue(e.ret_val);
                    m_of << "._0)";

                }
            } else if (name == "sub_with_overflow") {
                if (m_options.emulated_i128 && params.m_types.at(0) == ::HIR::CoreType::U128) {
                    emit_lvalue(e.ret_val);
                    m_of << "._1 = sub128_o";
                    m_of << "(";
                    emit_param(e.args.at(0));
                    m_of << ", ";
                    emit_param(e.args.at(1));
                    m_of << ", &";
                    emit_lvalue(e.ret_val);
                    m_of << "._0)";
                } else if (m_options.emulated_i128 && params.m_types.at(0) == ::HIR::CoreType::I128) {
                    emit_lvalue(e.ret_val);
                    m_of << "._1 = sub128s_o";
                    m_of << "(";
                    emit_param(e.args.at(0));
                    m_of << ", ";
                    emit_param(e.args.at(1));
                    m_of << ", &";
                    emit_lvalue(e.ret_val);
                    m_of << "._0)";
                } else {
                    emit_lvalue(e.ret_val);
                    m_of << "._1 = __builtin_sub_overflow";
                    m_of << "(";
                    emit_param(e.args.at(0));
                    m_of << ", ";
                    emit_param(e.args.at(1));
                    m_of << ", &";
                    emit_lvalue(e.ret_val);
                    m_of << "._0)";

                }
            } else if (name == "mul_with_overflow") {
                if (m_options.emulated_i128 && params.m_types.at(0) == ::HIR::CoreType::U128) {
                    emit_lvalue(e.ret_val);
                    m_of << "._1 = mul128_o";
                    m_of << "(";
                    emit_param(e.args.at(0));
                    m_of << ", ";
                    emit_param(e.args.at(1));
                    m_of << ", &";
                    emit_lvalue(e.ret_val);
                    m_of << "._0)";
                } else if (m_options.emulated_i128 && params.m_types.at(0) == ::HIR::CoreType::I128) {
                    emit_lvalue(e.ret_val);
                    m_of << "._1 = mul128s_o";
                    m_of << "(";
                    emit_param(e.args.at(0));
                    m_of << ", ";
                    emit_param(e.args.at(1));
                    m_of << ", &";
                    emit_lvalue(e.ret_val);
                    m_of << "._0)";
                } else {
                    emit_lvalue(e.ret_val);
                    m_of << "._1 = __builtin_mul_overflow(";
                    emit_param(e.args.at(0));
                    m_of << ", ";
                    emit_param(e.args.at(1));
                    m_of << ", &";
                    emit_lvalue(e.ret_val);
                    m_of << "._0)";

                }
            } else if (name == "overflowing_add" || name == "wrapping_add" // Renamed in 1.39
                       || name == "saturating_add" || name == "unchecked_add") {
                const auto& ty = params.m_types.at(0);
                if (name == "saturating_add") {
                    m_of << "if( ";
                }

                if (m_options.emulated_i128 && ty == ::HIR::CoreType::U128) {
                    m_of << "add128_o";
                    m_of << "(";
                    emit_param(e.args.at(0));
                    m_of << ", ";
                    emit_param(e.args.at(1));
                    m_of << ", &";
                    emit_lvalue(e.ret_val);
                    m_of << ")";
                } else if (m_options.emulated_i128 && ty == ::HIR::CoreType::I128) {
                    m_of << "add128s_o";
                    m_of << "(";
                    emit_param(e.args.at(0));
                    m_of << ", ";
                    emit_param(e.args.at(1));
                    m_of << ", &";
                    emit_lvalue(e.ret_val);
                    m_of << ")";
                } else {
                    m_of << "__builtin_add_overflow";
                    m_of << "(";
                    emit_param(e.args.at(0));
                    m_of << ", ";
                    emit_param(e.args.at(1));
                    m_of << ", &";
                    emit_lvalue(e.ret_val);
                    m_of << ")";

                }

                if (name == "saturating_add") {
                    m_of << ") { ";
                    emit_lvalue(e.ret_val);
                    m_of << " = ";
                    switch (get_real_prim_ty(ty->as_Primitive())) {
                        case ::HIR::CoreType::U8:
                        case ::HIR::CoreType::U16:
                        case ::HIR::CoreType::U32:
                        case ::HIR::CoreType::U64:
                            m_of << "-1"; // -1 should extend to MAX
                            break;
                        case ::HIR::CoreType::U128:
                            if (m_options.emulated_i128) {
                                m_of << "make128_raw(-1, -1)";
                            } else {
                                m_of << "-1";
                            }
                            break;
                        // If the LHS is negative, then the only way overflow can happen is if the RHS is also negative, so saturate at negative.
                        case ::HIR::CoreType::I8:
                            m_of << "(";
                            emit_param(e.args.at(0));
                            m_of << " < 0 ? -0x80 : 0x7F)";
                            break;
                        case ::HIR::CoreType::I16:
                            m_of << "(";
                            emit_param(e.args.at(0));
                            m_of << " < 0 ? -0x8000 : 0x7FFF)";
                            break;
                        case ::HIR::CoreType::I32:
                            m_of << "(";
                            emit_param(e.args.at(0));
                            m_of << " < 0 ? -0x8000000l : 0x7FFFFFFFl)";
                            break;
                        case ::HIR::CoreType::I64:
                            m_of << "(";
                            emit_param(e.args.at(0));
                            m_of << " < 0 ? -0x8000000"
                                    "00000000ll : 0x7FFFFFFF"
                                    "FFFFFFFFll)";
                            break;
                        case ::HIR::CoreType::I128:
                            if (m_options.emulated_i128) {
                                m_of << "( (int64_t)(";
                                emit_param(e.args.at(0));
                                m_of << ".hi) < 0 ? make128s_raw(-0x8000000"
                                        "00000000ll, 0) : make128s_raw(0x7FFFFFFF"
                                        "FFFFFFFFll, -1))";
                            } else {
                                m_of << "(";
                                emit_param(e.args.at(0));
                                m_of << " < 0 ? ((uint128_t)1 << 127) : (((uint128_t)1 << 127) - 1))";
                            }
                            break;
                        default:
                            MIR_TODO(mir_res, "saturating_add - " << ty);
                    }
                    m_of << "; }";
                }
            } else if (name == "overflowing_sub" || name == "wrapping_sub" || name == "saturating_sub" || name == "unchecked_sub") {
                const auto& ty = params.m_types.at(0);
                if (name == "saturating_sub") {
                    m_of << "if( ";
                }
                if (m_options.emulated_i128 && ty == ::HIR::CoreType::U128) {
                    m_of << "sub128_o";
                    m_of << "(";
                    emit_param(e.args.at(0));
                    m_of << ", ";
                    emit_param(e.args.at(1));
                    m_of << ", &";
                    emit_lvalue(e.ret_val);
                    m_of << ")";
                } else if (m_options.emulated_i128 && ty == ::HIR::CoreType::I128) {
                    m_of << "sub128s_o";
                    m_of << "(";
                    emit_param(e.args.at(0));
                    m_of << ", ";
                    emit_param(e.args.at(1));
                    m_of << ", &";
                    emit_lvalue(e.ret_val);
                    m_of << ")";
                } else {
                    m_of << "__builtin_sub_overflow";
                    m_of << "(";
                    emit_param(e.args.at(0));
                    m_of << ", ";
                    emit_param(e.args.at(1));
                    m_of << ", &";
                    emit_lvalue(e.ret_val);
                    m_of << ")";

                }

                if (name == "saturating_sub") {
                    m_of << ") { ";
                    emit_lvalue(e.ret_val);
                    m_of << " = ";
                    switch (get_real_prim_ty(ty->as_Primitive())) {
                        case ::HIR::CoreType::U8:
                        case ::HIR::CoreType::U16:
                        case ::HIR::CoreType::U32:
                        case ::HIR::CoreType::U64:
                            m_of << "0";
                            break;
                        case ::HIR::CoreType::U128:
                            if (m_options.emulated_i128) {
                                m_of << "make128(0)";
                            } else {
                                m_of << "0";
                            }
                            break;
                        case ::HIR::CoreType::I8:
                            m_of << "(";
                            emit_param(e.args.at(0));
                            m_of << " < 0 ? -0x80 : 0x7F)";
                            break;
                        case ::HIR::CoreType::I16:
                            m_of << "(";
                            emit_param(e.args.at(0));
                            m_of << " < 0 ? -0x8000 : 0x7FFF)";
                            break;
                        case ::HIR::CoreType::I32:
                            m_of << "(";
                            emit_param(e.args.at(0));
                            m_of << " < 0 ? -0x8000000l : 0x7FFFFFFFl)";
                            break;
                        case ::HIR::CoreType::I64:
                            m_of << "(";
                            emit_param(e.args.at(0));
                            m_of << " < 0 ? -0x8000000"
                                    "00000000ll : 0x7FFFFFFF"
                                    "FFFFFFFFll)";
                            break;
                        case ::HIR::CoreType::I128:
                            if (m_options.emulated_i128) {
                                m_of << "( (int64_t)(";
                                emit_param(e.args.at(0));
                                m_of << ".hi) < 0 ? make128s_raw(-0x8000000"
                                        "00000000ll, 0) : make128s_raw(0x7FFFFFFF"
                                        "FFFFFFFFll, -1))";
                            } else {
                                m_of << "(";
                                emit_param(e.args.at(0));
                                m_of << " < 0 ? ((uint128_t)1 << 127) : (((uint128_t)1 << 127) - 1))";
                            }
                            break;
                        default:
                            MIR_TODO(mir_res, "saturating_sub - " << ty);
                    }
                    m_of << "; }";
                }
            } else if (name == "overflowing_mul" || name == "wrapping_mul" || name == "unchecked_mul") {
                if (m_options.emulated_i128 && params.m_types.at(0) == ::HIR::CoreType::U128) {
                    m_of << "mul128_o";
                    m_of << "(";
                    emit_param(e.args.at(0));
                    m_of << ", ";
                    emit_param(e.args.at(1));
                    m_of << ", &";
                    emit_lvalue(e.ret_val);
                    m_of << ")";
                } else if (m_options.emulated_i128 && params.m_types.at(0) == ::HIR::CoreType::I128) {
                    m_of << "mul128s_o";
                    m_of << "(";
                    emit_param(e.args.at(0));
                    m_of << ", ";
                    emit_param(e.args.at(1));
                    m_of << ", &";
                    emit_lvalue(e.ret_val);
                    m_of << ")";
                } else {
                    m_of << "__builtin_mul_overflow";
                    m_of << "(";
                    emit_param(e.args.at(0));
                    m_of << ", ";
                    emit_param(e.args.at(1));
                    m_of << ", &";
                    emit_lvalue(e.ret_val);
                    m_of << ")";

                }
            }
            // Unchecked Arithmetic
            // - exact_div is UB to call on a non-multiple
            else if (name == "unchecked_div" || name == "exact_div") {
                emit_lvalue(e.ret_val);
                m_of << " = ";
                if (type_is_emulated_i128(params.m_types.at(0))) {
                    m_of << "div128";
                    if (params.m_types.at(0) == ::HIR::CoreType::I128) {
                        m_of << "s";
                    }
                    m_of << "(";
                    emit_param(e.args.at(0));
                    m_of << ", ";
                    emit_param(e.args.at(1));
                    m_of << ")";
                } else {
                    emit_param(e.args.at(0));
                    m_of << " / ";
                    emit_param(e.args.at(1));
                }
            } else if (name == "unchecked_rem") {
                emit_lvalue(e.ret_val);
                m_of << " = ";
                if (type_is_emulated_i128(params.m_types.at(0))) {
                    m_of << "mod128";
                    if (params.m_types.at(0) == ::HIR::CoreType::I128) {
                        m_of << "s";
                    }
                    m_of << "(";
                    emit_param(e.args.at(0));
                    m_of << ", ";
                    emit_param(e.args.at(1));
                    m_of << ")";
                } else {
                    emit_param(e.args.at(0));
                    m_of << " % ";
                    emit_param(e.args.at(1));
                }
            } else if (name == "unchecked_shl") {
                emit_lvalue(e.ret_val);
                m_of << " = ";
                if (type_is_emulated_i128(params.m_types.at(0))) {
                    m_of << "shl128";
                    if (params.m_types.at(0) == ::HIR::CoreType::I128) {
                        m_of << "s";
                    }
                    m_of << "(";
                    emit_param(e.args.at(0));
                    m_of << ", ";
                    emit_param(e.args.at(1));
                    // If the shift type is a u128/i128, get the inner
                    ::HIR::TypeRef tmp;
                    const auto& shift_ty = mir_res.get_param_type(tmp, e.args.at(1));
                    if (shift_ty == ::HIR::CoreType::I128 || shift_ty == ::HIR::CoreType::U128) {
                        m_of << ".lo";
                    }
                    m_of << ")";
                } else {
                    emit_param(e.args.at(0));
                    m_of << " << ";
                    emit_param(e.args.at(1));
                }
            } else if (name == "unchecked_shr") {
                emit_lvalue(e.ret_val);
                m_of << " = ";
                if (type_is_emulated_i128(params.m_types.at(0))) {
                    m_of << "shr128";
                    if (params.m_types.at(0) == ::HIR::CoreType::I128) {
                        m_of << "s";
                    }
                    m_of << "(";
                    emit_param(e.args.at(0));
                    m_of << ", ";
                    emit_param(e.args.at(1));
                    // If the shift type is a u128/i128, get the inner
                    ::HIR::TypeRef tmp;
                    const auto& shift_ty = mir_res.get_param_type(tmp, e.args.at(1));
                    if (shift_ty == ::HIR::CoreType::I128 || shift_ty == ::HIR::CoreType::U128) {
                        m_of << ".lo";
                    }
                    m_of << ")";
                } else {
                    emit_param(e.args.at(0));
                    m_of << " >> ";
                    emit_param(e.args.at(1));
                }
            }
            // Rotate
            else if (name == "rotate_left") {
                const auto& ty = params.m_types.at(0);
                switch (get_real_prim_ty(ty->as_Primitive())) {
                    case ::HIR::CoreType::I8:
                    case ::HIR::CoreType::U8:
                        m_of << "{";
                        m_of << " uint8_t v = ";
                        emit_param(e.args.at(0));
                        m_of << ";";
                        m_of << " unsigned shift = ";
                        emit_param(e.args.at(1));
                        m_of << " % 8;";
                        m_of << " ";
                        emit_lvalue(e.ret_val);
                        m_of << " = shift == 0 ? v : (v << shift) | (v >> (8 - shift));";
                        m_of << "}";
                        break;
                    case ::HIR::CoreType::I16:
                    case ::HIR::CoreType::U16:
                        m_of << "{";
                        m_of << " uint16_t v = ";
                        emit_param(e.args.at(0));
                        m_of << ";";
                        m_of << " unsigned shift = ";
                        emit_param(e.args.at(1));
                        m_of << " % 16;";
                        m_of << " ";
                        emit_lvalue(e.ret_val);
                        m_of << " = shift == 0 ? v : (v << shift) | (v >> (16 - shift));";
                        m_of << "}";
                        break;
                    case ::HIR::CoreType::I32:
                    case ::HIR::CoreType::U32:
                        m_of << "{";
                        m_of << " uint32_t v = ";
                        emit_param(e.args.at(0));
                        m_of << ";";
                        m_of << " unsigned shift = ";
                        emit_param(e.args.at(1));
                        m_of << " % 32;";
                        m_of << " ";
                        emit_lvalue(e.ret_val);
                        m_of << " = shift == 0 ? v : (v << shift) | (v >> (32 - shift));";
                        m_of << "}";
                        break;
                    case ::HIR::CoreType::I64:
                    case ::HIR::CoreType::U64:
                        m_of << "{";
                        m_of << " uint64_t v = ";
                        emit_param(e.args.at(0));
                        m_of << ";";
                        m_of << " unsigned shift = ";
                        emit_param(e.args.at(1));
                        m_of << " % 64;";
                        m_of << " ";
                        emit_lvalue(e.ret_val);
                        m_of << " = shift == 0 ? v : (v << shift) | (v >> (64 - shift));";
                        m_of << "}";
                        break;
                    case ::HIR::CoreType::I128:
                    case ::HIR::CoreType::U128:
                        m_of << "{";
                        m_of << " uint128_t v = ";
                        emit_param(e.args.at(0));
                        m_of << ";";
                        m_of << " unsigned shift = ";
                        emit_param(e.args.at(1));
                        m_of << " % 128;";
                        if (m_options.emulated_i128) {
                            m_of << " if(shift == 0) {";
                            m_of << " ";
                            emit_lvalue(e.ret_val);
                            m_of << " = v;";
                            m_of << " } else if(shift < 64) {";
                            m_of << " ";
                            emit_lvalue(e.ret_val);
                            m_of << ".lo = (v.lo << shift) | (v.hi >> (64 - shift));";
                            m_of << " ";
                            emit_lvalue(e.ret_val);
                            m_of << ".hi = (v.hi << shift) | (v.lo >> (64 - shift));";
                            m_of << " } else if(shift == 64) {";
                            m_of << " ";
                            emit_lvalue(e.ret_val);
                            m_of << ".lo = v.hi;";
                            m_of << " ";
                            emit_lvalue(e.ret_val);
                            m_of << ".hi = v.lo;";
                            m_of << " } else {";
                            m_of << " shift -= 64;"; // Swap order and reduce shift
                            m_of << " ";
                            emit_lvalue(e.ret_val);
                            m_of << ".lo = (v.hi << shift) | (v.lo >> (64 - shift));";
                            m_of << " ";
                            emit_lvalue(e.ret_val);
                            m_of << ".hi = (v.lo << shift) | (v.hi >> (64 - shift));";
                            m_of << " }";
                        } else {
                            m_of << " ";
                            emit_lvalue(e.ret_val);
                            m_of << " = shift == 0 ? v : (v << shift) | (v >> (128 - shift));";
                        }
                        m_of << "}";
                        break;
                    default:
                        MIR_TODO(mir_res, "rotate_left - " << ty);
                }
            } else if (name == "rotate_right") {
                const auto& ty = params.m_types.at(0);
                switch (get_real_prim_ty(ty->as_Primitive())) {
                    case ::HIR::CoreType::I8:
                    case ::HIR::CoreType::U8:
                        m_of << "{";
                        m_of << " uint8_t v = ";
                        emit_param(e.args.at(0));
                        m_of << ";";
                        m_of << " unsigned shift = ";
                        emit_param(e.args.at(1));
                        m_of << " % 8;";
                        m_of << " ";
                        emit_lvalue(e.ret_val);
                        m_of << " = shift == 0 ? v : (v >> shift) | (v << (8 - shift));";
                        m_of << "}";
                        break;
                    case ::HIR::CoreType::I16:
                    case ::HIR::CoreType::U16:
                        m_of << "{";
                        m_of << " uint16_t v = ";
                        emit_param(e.args.at(0));
                        m_of << ";";
                        m_of << " unsigned shift = ";
                        emit_param(e.args.at(1));
                        m_of << " % 16;";
                        m_of << " ";
                        emit_lvalue(e.ret_val);
                        m_of << " = shift == 0 ? v : (v >> shift) | (v << (16 - shift));";
                        m_of << "}";
                        break;
                    case ::HIR::CoreType::I32:
                    case ::HIR::CoreType::U32:
                        m_of << "{";
                        m_of << " uint32_t v = ";
                        emit_param(e.args.at(0));
                        m_of << ";";
                        m_of << " unsigned shift = ";
                        emit_param(e.args.at(1));
                        m_of << " % 32;";
                        m_of << " ";
                        emit_lvalue(e.ret_val);
                        m_of << " = shift == 0 ? v : (v >> shift) | (v << (32 - shift));";
                        m_of << "}";
                        break;
                    case ::HIR::CoreType::I64:
                    case ::HIR::CoreType::U64:
                        m_of << "{";
                        m_of << " uint64_t v = ";
                        emit_param(e.args.at(0));
                        m_of << ";";
                        m_of << " unsigned shift = ";
                        emit_param(e.args.at(1));
                        m_of << " % 64;";
                        m_of << " ";
                        emit_lvalue(e.ret_val);
                        m_of << " = shift == 0 ? v : (v >> shift) | (v << (64 - shift));";
                        m_of << "}";
                        break;
                    case ::HIR::CoreType::I128:
                    case ::HIR::CoreType::U128:
                        m_of << "{";
                        m_of << " uint128_t v = ";
                        emit_param(e.args.at(0));
                        m_of << ";";
                        m_of << " unsigned shift = ";
                        emit_param(e.args.at(1));
                        m_of << " % 128;";
                        if (m_options.emulated_i128) {
                            m_of << " if(shift == 0) {";
                            m_of << " ";
                            emit_lvalue(e.ret_val);
                            m_of << " = v;";
                            m_of << " } else if(shift < 64) {";
                            m_of << " ";
                            emit_lvalue(e.ret_val);
                            m_of << ".lo = (v.lo >> shift) | (v.hi << (64 - shift));";
                            m_of << " ";
                            emit_lvalue(e.ret_val);
                            m_of << ".hi = (v.hi >> shift) | (v.lo << (64 - shift));";
                            m_of << " } else if(shift == 64) {";
                            m_of << " ";
                            emit_lvalue(e.ret_val);
                            m_of << ".lo = v.hi;";
                            m_of << " ";
                            emit_lvalue(e.ret_val);
                            m_of << ".hi = v.lo;";
                            m_of << " } else {";
                            m_of << " shift -= 64;"; // Swap order and reduce shift
                            m_of << " ";
                            emit_lvalue(e.ret_val);
                            m_of << ".lo = (v.hi >> shift) | (v.lo << (64 - shift));";
                            m_of << " ";
                            emit_lvalue(e.ret_val);
                            m_of << ".hi = (v.lo >> shift) | (v.hi << (64 - shift));";
                            m_of << " }";
                        } else {
                            m_of << " ";
                            emit_lvalue(e.ret_val);
                            m_of << " = shift == 0 ? v : (v >> shift) | (v << (128 - shift));";
                        }
                        m_of << "}";
                        break;
                    default:
                        MIR_TODO(mir_res, "rotate_right - " << ty);
                }
            }
            // Bit Twiddling
            // - CounT Leading Zeroes
            // - CounT Trailing Zeroes
            else if (name == "ctlz" || name == "ctlz_nonzero" || name == "cttz" || name == "cttz_nonzero") {
                auto emit_arg0 = [&]() {
                    emit_param(e.args.at(0));
                };
                const auto& ty = params.m_types.at(0);
                emit_lvalue(e.ret_val);
                m_of << " = (";
                if (ty == ::HIR::CoreType::U128 || ty == ::HIR::CoreType::I128) {
                    if (ty == ::HIR::CoreType::I128) {
                        if (m_options.emulated_i128) {
                            m_of << "uint128_to_int128(";
                        } else {
                            m_of << "(int128_t)";
                        }
                    }
                    if (name == "ctlz" || name == "ctlz_nonzero") {
                        m_of << "intrinsic_ctlz_u128(";
                    } else {
                        m_of << "intrinsic_cttz_u128(";
                    }
                    if (ty == ::HIR::CoreType::I128) {
                        if (m_options.emulated_i128) {
                            m_of << "int128_to_uint128(";
                        } else {
                            m_of << "(uint128_t)";
                        }
                    }
                    emit_param(e.args.at(0));
                    m_of << ")";
                    if (ty == ::HIR::CoreType::I128 && m_options.emulated_i128) {
                        m_of << ")";
                        m_of << ")";
                    } else {
                    }
                    m_of << ")";
                    if (m_options.emulated_i128) {
                        m_of << ".lo";
                    }
                    m_of << ";";
                    return;
                } else if (ty == ::HIR::CoreType::U64 || (ty == ::HIR::CoreType::Usize && TargetGetPointerBits() > 32)) {
                    emit_param(e.args.at(0));
                    m_of << " != 0 ? ";
                    if (name == "ctlz" || name == "ctlz_nonzero") {
                        m_of << "__builtin_clz64(";
                        emit_arg0();
                        m_of << ")";
                    } else {
                        m_of << "__builtin_ctz64(";
                        emit_arg0();
                        m_of << ")";
                    }
                } else {
                    emit_param(e.args.at(0));
                    m_of << " != 0 ? ";
                    if (name == "ctlz" || name == "ctlz_nonzero") {
                        m_of << "__builtin_clz(";
                        if (ty == ::HIR::CoreType::U8 || ty == ::HIR::CoreType::I8) {
                            m_of << "(uint8_t)(";
                        } else if (ty == ::HIR::CoreType::U16 || ty == ::HIR::CoreType::I16) {
                            m_of << "(uint16_t)(";
                        }
                        emit_param(e.args.at(0));
                        if (ty == ::HIR::CoreType::U8 || ty == ::HIR::CoreType::I8
                            || ty == ::HIR::CoreType::U16 || ty == ::HIR::CoreType::I16) {
                            m_of << ")";
                        }
                        m_of << ")";
                        if (ty == ::HIR::CoreType::U8 || ty == ::HIR::CoreType::I8) {
                            m_of << " - 24";
                        } else if (ty == ::HIR::CoreType::U16 || ty == ::HIR::CoreType::I16) {
                            m_of << " - 16";
                        }
                    } else {
                        m_of << "__builtin_ctz(";
                        emit_param(e.args.at(0));
                        m_of << ")";
                    }
                }
                m_of << " : sizeof(";
                emit_ctype(ty);
                m_of << ")*8)";
            }
            // - CounT POPulated
            else if (name == "ctpop") {
                emit_lvalue(e.ret_val);
                m_of << " = ";

                if (type_is_emulated_i128(params.m_types.at(0))) {
                    m_of << "popcount128(";
                    emit_param(e.args.at(0));
                    m_of << ")";
                    m_of << ".lo";
                } else {
                    m_of << "__builtin_popcountll(";
                    emit_param(e.args.at(0));
                    m_of << ")";
                }
            }
            // --- Floating Point
            else if ((name.size() > 3 && name.compare(name.size() - 3, 3, "f16") == 0) || (name.size() > 3 && name.compare(name.size() - 3, 3, "f32") == 0) || (name.size() > 3 && name.compare(name.size() - 3, 3, "f64") == 0) || (name.size() > 4 && name.compare(name.size() - 4, 4, "f128") == 0)) {
                if (name.compare(name.size() - 3, 3, "f16") == 0) {
                    m_of << "abort();";
                    return;
                }
                if (name.compare(name.size() - 4, 4, "f128") == 0) {
                    m_of << "abort();";
                    return;
                }
                auto emit_math_name = [&](const char* op) {
                    m_of << "__builtin_";
                    m_of << op << (name.back() == '2' ? "f" : "");
                };
                auto emit1 = [&](const char* op) {
                    if (name.compare(name.size() - 3, 3, "f16") == 0) {
                        m_of << "abort();";
                        return;
                    }
                    emit_lvalue(e.ret_val);
                    m_of << " = ";
                    emit_math_name(op);
                    m_of << "(";
                    emit_param(e.args.at(0));
                    m_of << ")";
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
                    emit_lvalue(e.ret_val);
                    m_of << " = ";
                    emit_math_name("copysign");
                    m_of << "(";
                    emit_param(e.args.at(0));
                    m_of << ", ";
                    emit_param(e.args.at(1));
                    m_of << ")";
                }
                // > Returns the integer part of an `f32`.
                else if (name == "truncf32" || name == "truncf64") {
                    emit1("trunc");
                } else if (name == "powif32" || name == "powif64") {
                    emit_lvalue(e.ret_val);
                    m_of << " = ";
                    emit_math_name("pow");
                    m_of << "(";
                    emit_param(e.args.at(0));
                    m_of << ", ";
                    emit_param(e.args.at(1));
                    m_of << ")";
                } else if (name == "powf32" || name == "powf64") {
                    emit_lvalue(e.ret_val);
                    m_of << " = ";
                    emit_math_name("pow");
                    m_of << "(";
                    emit_param(e.args.at(0));
                    m_of << ", ";
                    emit_param(e.args.at(1));
                    m_of << ")";
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
                    emit_lvalue(e.ret_val);
                    m_of << " = ";
                    emit_math_name("fma");
                    m_of << "(";
                    emit_param(e.args.at(0));
                    m_of << ", ";
                    emit_param(e.args.at(1));
                    m_of << ", ";
                    emit_param(e.args.at(2));
                    m_of << ")";
                } else if (name == "maxnumf32" || name == "maxnumf64") {
                    emit_lvalue(e.ret_val);
                    m_of << " = ";
                    emit_math_name("fmax");
                    m_of << "(";
                    emit_param(e.args.at(0));
                    m_of << ", ";
                    emit_param(e.args.at(1));
                    m_of << ")";
                } else if (name == "minnumf32" || name == "minnumf64") {
                    emit_lvalue(e.ret_val);
                    m_of << " = ";
                    emit_math_name("fmin");
                    m_of << "(";
                    emit_param(e.args.at(0));
                    m_of << ", ";
                    emit_param(e.args.at(1));
                    m_of << ")";
                } else {
                    MIR_BUG(mir_res, "Unknown float intrinsic '" << name << "'");
                }
            }
            // --- Volatile Load/Store
            else if (name == "volatile_load") {
                // A ZST has no bytes to access.  In particular, Rust permits
                // these operations with a dangling ZST pointer, so emitting a
                // C volatile dereference would invent an observable access.
                if (!this->type_is_bad_zst(params.m_types.at(0))) {
                    emit_lvalue(e.ret_val);
                    m_of << " = *(volatile ";
                    emit_ctype(params.m_types.at(0));
                    m_of << "*)";
                    emit_param(e.args.at(0));
                }
            } else if (name == "volatile_store") {
                if (!this->type_is_bad_zst(params.m_types.at(0))) {
                    m_of << "*(volatile ";
                    emit_ctype(params.m_types.at(0));
                    m_of << "*)";
                    emit_param(e.args.at(0));
                    m_of << " = ";
                    emit_param(e.args.at(1));
                }
            } else if (name == "nontemporal_store") {
                // TODO: Actually do a non-temporal store
                // GCC: _mm_stream_* (depending on input type, which must be `repr(simd)`)
                if (!this->type_is_bad_zst(params.m_types.at(0))) {
                    m_of << "*(volatile ";
                    emit_ctype(params.m_types.at(0));
                    m_of << "*)";
                    emit_param(e.args.at(0));
                    m_of << " = ";
                    emit_param(e.args.at(1));
                }
            }
            // --- Atomics!
            else if (name.compare(0, 7, "atomic_") == 0) {
                // > Single-ordering atomics
                if (name == "atomic_xadd" || name.compare(0, 7 + 4 + 1, "atomic_xadd_") == 0) {
                    auto ordering = get_atomic_ordering(name, 7 + 4 + 1);
                    emit_atomic_arith(AtomicOp::Add, ordering);
                } else if (name == "atomic_xsub" || name.compare(0, 7 + 4 + 1, "atomic_xsub_") == 0) {
                    auto ordering = get_atomic_ordering(name, 7 + 4 + 1);
                    emit_atomic_arith(AtomicOp::Sub, ordering);
                } else if (name == "atomic_and" || name.compare(0, 7 + 3 + 1, "atomic_and_") == 0) {
                    auto ordering = get_atomic_ordering(name, 7 + 3 + 1);
                    emit_atomic_arith(AtomicOp::And, ordering);
                } else if (name == "atomic_nand" || name.compare(0, 7 + 4 + 1, "atomic_nand_") == 0) {
                    auto ordering = get_atomic_ordering(name, 7 + 4 + 1);
                    const auto& ty = params.m_types.at(0);
                    emit_lvalue(e.ret_val);
                    m_of << " = ";
                    emit_atomic_rmw_cast();
                    m_of << "__mrustc_atomicloop" << get_prim_size(ty) << "(";
                    m_of << "(volatile uint" << get_prim_size(ty) << "_t*)";
                    emit_param(e.args.at(0));
                    m_of << ", ";
                    emit_atomic_rmw_operand(e.args.at(1));
                    m_of << ", " << get_atomic_ty_gcc(ordering);
                    m_of << ", __mrustc_op_and_not" << get_prim_size(ty);
                    m_of << ")";
                } else if (name == "atomic_or" || name.compare(0, 7 + 2 + 1, "atomic_or_") == 0) {
                    auto ordering = get_atomic_ordering(name, 7 + 2 + 1);
                    emit_atomic_arith(AtomicOp::Or, ordering);
                } else if (name == "atomic_xor" || name.compare(0, 7 + 3 + 1, "atomic_xor_") == 0) {
                    auto ordering = get_atomic_ordering(name, 7 + 3 + 1);
                    emit_atomic_arith(AtomicOp::Xor, ordering);
                } else if (name == "atomic_max" || name.compare(0, 7 + 3 + 1, "atomic_max_") == 0 || name == "atomic_min" || name.compare(0, 7 + 3 + 1, "atomic_min_") == 0) {
                    auto ordering = get_atomic_ordering(name, 7 + 3 + 1);
                    const auto& ty = params.m_types.at(0);
                    const char* op = (name.c_str()[7 + 1] == 'a' ? "imax" : "imin"); // m'a'x vs m'i'n
                    emit_lvalue(e.ret_val);
                    m_of << " = ";
                    emit_atomic_rmw_cast();
                    m_of << "__mrustc_atomicloop" << get_prim_size(ty) << "(";
                    m_of << "(volatile uint" << get_prim_size(ty) << "_t*)";
                    emit_param(e.args.at(0));
                    m_of << ", ";
                    emit_atomic_rmw_operand(e.args.at(1));
                    m_of << ", " << get_atomic_ty_gcc(ordering);
                    m_of << ", __mrustc_op_" << op << get_prim_size(ty);
                    m_of << ")";
                } else if (name == "atomic_umax" || name.compare(0, 7 + 4 + 1, "atomic_umax_") == 0 || name == "atomic_umin" || name.compare(0, 7 + 4 + 1, "atomic_umin_") == 0) {
                    auto ordering = get_atomic_ordering(name, 7 + 4 + 1);
                    const auto& ty = params.m_types.at(0);
                    const char* op = (name.c_str()[7 + 2] == 'a' ? "umax" : "umin"); // m'a'x vs m'i'n
                    emit_lvalue(e.ret_val);
                    m_of << " = ";
                    emit_atomic_rmw_cast();
                    m_of << "__mrustc_atomicloop" << get_prim_size(ty) << "(";
                    m_of << "(volatile uint" << get_prim_size(ty) << "_t*)";
                    emit_param(e.args.at(0));
                    m_of << ", ";
                    emit_atomic_rmw_operand(e.args.at(1));
                    m_of << ", " << get_atomic_ty_gcc(ordering);
                    m_of << ", __mrustc_op_" << op << get_prim_size(ty);
                    m_of << ")";
                } else if (name == "atomic_load" || name.compare(0, 7 + 4 + 1, "atomic_load_") == 0) {
                    auto ordering = get_atomic_ordering(name, 7 + 4 + 1);
                    emit_lvalue(e.ret_val);
                    m_of << " = ";
                    m_of << "__atomic_load_n(";
                    emit_atomic_cast();
                    emit_param(e.args.at(0));
                    m_of << ", " << get_atomic_ty_gcc(ordering) << ")";

                } else if (name == "atomic_store" || name.compare(0, 7 + 5 + 1, "atomic_store_") == 0) {
                    auto ordering = get_atomic_ordering(name, 7 + 5 + 1);
                    m_of << "__atomic_store_n(";
                    emit_atomic_cast();
                    emit_param(e.args.at(0));
                    m_of << ", ";
                    emit_param(e.args.at(1));
                    m_of << ", " << get_atomic_ty_gcc(ordering) << ")";

                }
                // Comare+Exchange (has two orderings)
                else if (name == "atomic_cxchg_acq_failrelaxed") {
                    emit_atomic_cxchg(e, Ordering::Acquire, Ordering::Relaxed, false);
                } else if (name == "atomic_cxchg_acqrel_failrelaxed") {
                    emit_atomic_cxchg(e, Ordering::AcqRel, Ordering::Relaxed, false);
                }
                // _rel = Release, Relaxed (not Release,Release)
                else if (name == "atomic_cxchg_rel") {
                    emit_atomic_cxchg(e, Ordering::Release, Ordering::Relaxed, false);
                }
                // _acqrel = Release, Acquire (not AcqRel,AcqRel)
                else if (name == "atomic_cxchg_acqrel") {
                    emit_atomic_cxchg(e, Ordering::AcqRel, Ordering::Acquire, false);
                } else if (name.compare(0, 7 + 6 + 4, "atomic_cxchg_fail") == 0) {
                    auto fail_ordering = get_atomic_ordering(name, 7 + 6 + 4);
                    emit_atomic_cxchg(e, Ordering::SeqCst, fail_ordering, false);
                } else if (name == "atomic_cxchg" || name.compare(0, 7 + 6, "atomic_cxchg_") == 0) {
                    auto ordering = get_atomic_ordering(name, 7 + 6);
                    emit_atomic_cxchg(e, ordering, ordering, false);
                } else if (name == "atomic_cxchgweak_acq_failrelaxed") {
                    emit_atomic_cxchg(e, Ordering::Acquire, Ordering::Relaxed, true);
                } else if (name == "atomic_cxchgweak_acqrel_failrelaxed") {
                    emit_atomic_cxchg(e, Ordering::AcqRel, Ordering::Relaxed, true);
                } else if (name.compare(0, 7 + 10 + 4, "atomic_cxchgweak_fail") == 0) {
                    auto fail_ordering = get_atomic_ordering(name, 7 + 10 + 4);
                    emit_atomic_cxchg(e, Ordering::SeqCst, fail_ordering, true);
                } else if (name == "atomic_cxchgweak") {
                    emit_atomic_cxchg(e, Ordering::SeqCst, Ordering::SeqCst, true);
                } else if (name == "atomic_cxchgweak_acq") {
                    emit_atomic_cxchg(e, Ordering::Acquire, Ordering::Acquire, true);
                } else if (name == "atomic_cxchgweak_rel") {
                    emit_atomic_cxchg(e, Ordering::Release, Ordering::Relaxed, true);
                } else if (name == "atomic_cxchgweak_acqrel") {
                    emit_atomic_cxchg(e, Ordering::AcqRel, Ordering::Acquire, true);
                } else if (name == "atomic_cxchgweak_relaxed") {
                    emit_atomic_cxchg(e, Ordering::Relaxed, Ordering::Relaxed, true);
                } else if (name == "atomic_cxchgweak" || name.compare(0, 91 - 74, "atomic_cxchgweak_") == 0) {
                    auto ordering = get_atomic_ordering(name, 91 - 74);
                    emit_atomic_cxchg(e, ordering, ordering, false);
                } else if (name == "atomic_xchg" || name.compare(0, 7 + 5, "atomic_xchg_") == 0) {
                    auto ordering = get_atomic_ordering(name, 7 + 5);
                    emit_lvalue(e.ret_val);
                    m_of << " = ";
                    m_of << "__atomic_exchange_n(";
                    emit_atomic_cast();
                    emit_param(e.args.at(0));
                    m_of << ", ";
                    emit_param(e.args.at(1));
                    m_of << ", " << get_atomic_ty_gcc(ordering) << ")";

                } else if (name == "atomic_fence" || name.compare(0, 7 + 6, "atomic_fence_") == 0) {
                    auto ordering = get_atomic_ordering(name, 7 + 6);
                    m_of << "__atomic_thread_fence(" << get_atomic_ty_gcc(ordering) << ")";

                } else if (name == "atomic_singlethreadfence" || name.compare(0, 7 + 18, "atomic_singlethreadfence_") == 0) {
                    // TODO: Does this matter?
                } else {
                    MIR_BUG(mir_res, "Unknown atomic intrinsic '" << name << "'");
                }
            } else if (name == "option_payload_ptr") { // 1.74 only, removed later
                // Converts `*const Option<T>` to `*const T`, even if `None`
                emit_lvalue(e.ret_val);
                m_of << " = &(";
                emit_param(e.args.at(0));
                m_of << ")->DATA.var_1. _0";
            }
            // -- stdarg --
            else if (name == "va_copy") {
                m_of << "va_copy( *(va_list*)&";
                emit_param(e.args.at(0));
                m_of << ", *(va_list*)&";
                emit_param(e.args.at(1));
                m_of << ")";
            }
            // -- Platform Intrinsics (and SIMD) --
            else if (name.compare(0, 9, "platform:") == 0 || name.compare(0, 5, "simd_") == 0) {
                auto name_strip = ::std::string_view(name.c_str() + (name.compare(0, 9, "platform:") == 0 ? 9 : 0));

                struct SimdInfo {
                    unsigned count;
                    unsigned item_size;

                    enum Ty {
                        Float,
                        Signed,
                        Unsigned,
                    } ty;

                    static SimdInfo for_ty(const CodeGeneratorC& self, const HIR::TypeData* ty) {
                        const auto* ty_repr = TargetGetTypeRepr(self.sp, self.m_mir_res->m_resolve, ty);
                        MIR_ASSERT(*self.m_mir_res, ty_repr, "No repr for " << ty);
                        size_t size_slot = ty_repr->size;
                        const auto& ity = ty_repr->fields[0].ty;
                        DEBUG("SimdInfo Type: " << ity);
                        const auto& ty_val = ity->is_Primitive() ? ity : ty_repr->fields[0].ty->as_Array().inner;
                        DEBUG("ty_val = " << ty_val);
                        size_t size_val = 0;
                        MIR_ASSERT(*self.m_mir_res, TargetGetSizeOf(self.sp, self.m_resolve, ty_val, size_val), ty_val);

                        MIR_ASSERT(*self.m_mir_res, size_slot >= size_val, size_slot << " < " << size_val);
                        MIR_ASSERT(*self.m_mir_res, size_val > 0, "SimdInfo::for_ty - Value type " << ty_val << " was a ZST");
                        MIR_ASSERT(*self.m_mir_res, size_slot / size_val * size_val == size_slot, size_slot << " not a multiple of " << size_val);

                        SimdInfo rv;
                        rv.item_size = size_val;
                        rv.count = size_slot == 0 ? 0 : size_slot / size_val;
                        switch (ty_val->as_Primitive()) {
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
                                MIR_BUG(*self.m_mir_res, "Invalid SIMD type inner - " << ty_val);
                        }
                        return rv;
                    }

                    void emit_val_ty(CodeGeneratorC& self) {
                        switch (ty) {
                            case Float:
                                self.m_of << (item_size == 4 ? "float" : "double");
                                break;
                            case Signed:
                                self.m_of << "int" << (item_size * 8) << "_t";
                                break;
                            case Unsigned:
                                self.m_of << "uint" << (item_size * 8) << "_t";
                                break;
                        }
                    }
                };

                auto simd_cmp = [&](const char* op) {
                    auto src_info = SimdInfo::for_ty(*this, params.m_types.at(0));
                    auto dst_info = SimdInfo::for_ty(*this, params.m_types.at(1));
                    MIR_ASSERT(mir_res, src_info.count == dst_info.count, "Element counts must match for " << name);
                    m_of << "for(int i = 0; i < " << dst_info.count << "; i++)";
                    m_of << "((";
                    dst_info.emit_val_ty(*this);
                    m_of << "*)&";
                    emit_lvalue(e.ret_val);
                    m_of << ")[i] ";
                    m_of << "= (";
                    m_of << " ((";
                    src_info.emit_val_ty(*this);
                    m_of << "*)&";
                    emit_param(e.args.at(0));
                    m_of << ")[i]";
                    m_of << " " << op;
                    m_of << " ((";
                    src_info.emit_val_ty(*this);
                    m_of << "*)&";
                    emit_param(e.args.at(1));
                    m_of << ")[i]";
                    m_of << " ? -1 : 0)";
                };
                auto simd_arith = [&](const char* op) {
                    auto info = SimdInfo::for_ty(*this, params.m_types.at(0));
                    // Emulate!
                    emit_lvalue(e.ret_val);
                    m_of << " = ";
                    emit_param(e.args.at(0));
                    m_of << "; ";
                    m_of << "for(int i = 0; i < " << info.count << "; i++)";
                    m_of << "((";
                    info.emit_val_ty(*this);
                    m_of << "*)&";
                    emit_lvalue(e.ret_val);
                    m_of << ")[i] ";
                    m_of << op << "=";
                    m_of << " ((";
                    info.emit_val_ty(*this);
                    m_of << "*)&";
                    emit_param(e.args.at(1));
                    m_of << ")[i]";
                };
                auto simd_call = [&](const char* op) {
                    auto info = SimdInfo::for_ty(*this, params.m_types.at(0));
                    // Emulate!
                    m_of << "for(int i = 0; i < " << info.count << "; i++)";
                    m_of << "((";
                    info.emit_val_ty(*this);
                    m_of << "*)&";
                    emit_lvalue(e.ret_val);
                    m_of << ")[i] ";
                    m_of << "= ";
                    m_of << "__builtin_";
                    m_of << op << "( ((";
                    info.emit_val_ty(*this);
                    m_of << "*)&";
                    emit_param(e.args.at(0));
                    m_of << ")[i] )";
                };

                // dst: T, index: usize, val: U
                // Insert a value at position
                if (name_strip == "simd_insert") {
                    size_t size_slot = 0, size_val = 0;
                    TargetGetSizeOf(sp, m_resolve, params.m_types.at(0), size_slot);
                    TargetGetSizeOf(sp, m_resolve, params.m_types.at(1), size_val);
                    MIR_ASSERT(mir_res, size_slot >= size_val, size_slot << " < " << size_val);
                    MIR_ASSERT(mir_res, size_slot / size_val * size_val == size_slot, size_slot << " not a multiple of " << size_val);

                    // Emulate!
                    emit_lvalue(e.ret_val);
                    m_of << " = ";
                    emit_param(e.args.at(0));
                    m_of << "; ";
                    m_of << "(( ";
                    emit_ctype(params.m_types.at(1));
                    m_of << "*)&";
                    emit_lvalue(e.ret_val);
                    m_of << ")[";
                    emit_param(e.args.at(1));
                    m_of << "] = ";
                    emit_param(e.args.at(2));
                } else if (name_strip == "simd_extract") {
                    size_t size_slot = 0, size_val = 0;
                    TargetGetSizeOf(sp, m_resolve, params.m_types.at(0), size_slot);
                    TargetGetSizeOf(sp, m_resolve, params.m_types.at(1), size_val);
                    MIR_ASSERT(mir_res, size_slot >= size_val, size_slot << " < " << size_val);
                    MIR_ASSERT(mir_res, size_slot / size_val * size_val == size_slot, size_slot << " not a multiple of " << size_val);

                    // Emulate!
                    emit_lvalue(e.ret_val);
                    m_of << " = (( ";
                    emit_ctype(params.m_types.at(1));
                    m_of << "*)&";
                    emit_param(e.args.at(0));
                    m_of << ")[";
                    emit_param(e.args.at(1));
                    m_of << "]";
                }
                // Truncate into a bitmask - Converts a collection of [0,!0] into bits
                else if (name_strip == "simd_bitmask") {
                    auto src_info = SimdInfo::for_ty(*this, params.m_types.at(0));
                    size_t size_out = 0;
                    TargetGetSizeOf(sp, m_resolve, params.m_types.at(1), size_out);
                    m_of << "{ uint8_t* out = (uint8_t*)&(";
                    emit_lvalue(e.ret_val);
                    m_of << "); memset(out, 0, " << size_out << "); ";
                    for (size_t i = 0; i < src_info.count; i++) {
                        m_of << "out[" << (i / 8) << "] |= ((";
                        src_info.emit_val_ty(*this);
                        m_of << "*)&";
                        emit_param(e.args.at(0));
                        m_of << ")[" << i << "] == 0 ? 0 : (1 << " << (i % 8) << "); ";
                    }
                    m_of << "}";
                } else if (name_strip == "simd_shuffle128" || name_strip == "simd_shuffle64" || name_strip == "simd_shuffle32" || name_strip == "simd_shuffle16" || name_strip == "simd_shuffle8" || name_strip == "simd_shuffle4" || name_strip == "simd_shuffle2") {
                    // Shuffle in 8 entries
                    size_t size_slot = 0;
                    TargetGetSizeOf(sp, m_resolve, params.m_types.at(1), size_slot);
                    size_t div = name_strip == "simd_shuffle128" ? 128 : name_strip == "simd_shuffle64" ? 64 : name_strip == "simd_shuffle32" ? 32 : name_strip == "simd_shuffle16" ? 16 : name_strip == "simd_shuffle8" ? 8 : name_strip == "simd_shuffle4" ? 4 : name_strip == "simd_shuffle2" ? 2 : throw "";
                    size_t size_val = size_slot / div;
                    MIR_ASSERT(mir_res, size_val > 0, size_slot << " / " << div << " == 0?");
                    MIR_ASSERT(mir_res, size_slot >= size_val, size_slot << " < " << size_val);
                    MIR_ASSERT(mir_res, size_slot / size_val * size_val == size_slot, size_slot << " not a multiple of " << size_val);
                    // Indices address the concatenation of both input vectors, so the split
                    // point is the INPUT element count, not the index count.
                    size_t size_in = 0;
                    TargetGetSizeOf(sp, m_resolve, params.m_types.at(0), size_in);
                    size_t n_in = size_in / size_val;
                    MIR_ASSERT(mir_res, n_in > 0, "Zero-sized shuffle input");
                    m_of << "for(int i = 0; i < " << div << "; i++) { int j = ";
                    emit_param(e.args.at(2));
                    m_of << ".DATA[i];";
                    m_of << "((uint" << (size_val * 8) << "_t*)&";
                    emit_lvalue(e.ret_val);
                    m_of << ")[i]";
                    m_of << " = ((uint" << (size_val * 8) << "_t*)(j < " << n_in << " ? &";
                    emit_param(e.args.at(0));
                    m_of << " : &";
                    emit_param(e.args.at(1));
                    m_of << "))[j < " << n_in << " ? j : j - " << n_in << "];";
                    m_of << "}";
                } else if (name_strip == "simd_shuffle") {
                    const auto& vec_ty = params.m_types.at(0);
                    const auto& map_ty = params.m_types.at(1);
                    const auto& ret_ty = params.m_types.at(2);
                    size_t size_vec = 0;
                    size_t size_map = 0;
                    size_t size_ret = 0;
                    TargetGetSizeOf(sp, m_resolve, vec_ty, size_vec);
                    TargetGetSizeOf(sp, m_resolve, map_ty, size_map);
                    TargetGetSizeOf(sp, m_resolve, ret_ty, size_ret);
                    size_t div = size_map / 4; // map must be u32s
                    size_t size_val = size_ret / div;
                    // Indices address the concatenation of both inputs; split on the input
                    // element count (an extract's map can be shorter than the vector).
                    size_t n_in = size_vec / size_val;
                    MIR_ASSERT(mir_res, n_in > 0, "Zero-sized shuffle input");
                    m_of << "for(int i = 0; i < " << div << "; i++) {";
                    m_of << " int j = ";
                    emit_param(e.args.at(2));
                    m_of << "._0";
                    m_of << ".DATA[i];";
                    m_of << " ((uint" << (size_val * 8) << "_t*)&";
                    emit_lvalue(e.ret_val);
                    m_of << ")[i]";
                    m_of << " = ((uint" << (size_val * 8) << "_t*)(j < " << n_in << " ? &";
                    emit_param(e.args.at(0));
                    m_of << " : &";
                    emit_param(e.args.at(1));
                    m_of << "))[j < " << n_in << " ? j : j - " << n_in << "];";
                    m_of << "}";
                } else if (name_strip == "simd_cast") {
                    auto src_info = SimdInfo::for_ty(*this, params.m_types.at(0));
                    auto dst_info = SimdInfo::for_ty(*this, params.m_types.at(1));
                    MIR_ASSERT(mir_res, src_info.count == dst_info.count, "Element counts must match for " << name);
                    m_of << "for(int i = 0; i < " << dst_info.count << "; i++) ";
                    m_of << "((";
                    dst_info.emit_val_ty(*this);
                    m_of << "*)&";
                    emit_lvalue(e.ret_val);
                    m_of << ")[i] ";
                    m_of << "= ((";
                    src_info.emit_val_ty(*this);
                    m_of << "*)&";
                    emit_param(e.args.at(0));
                    m_of << ")[i];";
                }
                // Select between two values
                else if (name_strip == "simd_select") {
                    auto mask_info = SimdInfo::for_ty(*this, params.m_types.at(0));
                    auto val_info = SimdInfo::for_ty(*this, params.m_types.at(1));
                    MIR_ASSERT(mir_res, mask_info.count == val_info.count, "Element counts must match for " << name);
                    m_of << "for(int i = 0; i < " << val_info.count << "; i++) ";
                    m_of << "((";
                    val_info.emit_val_ty(*this);
                    m_of << "*)&";
                    emit_lvalue(e.ret_val);
                    m_of << ")[i] ";
                    m_of << "= ((";
                    mask_info.emit_val_ty(*this);
                    m_of << "*)&";
                    emit_param(e.args.at(0));
                    m_of << ")[i]";
                    m_of << "? ((";
                    val_info.emit_val_ty(*this);
                    m_of << "*)&";
                    emit_param(e.args.at(1));
                    m_of << ")[i]";
                    m_of << ": ((";
                    val_info.emit_val_ty(*this);
                    m_of << "*)&";
                    emit_param(e.args.at(2));
                    m_of << ")[i]";
                    m_of << ";";
                } else if (name_strip == "simd_select_bitmask") {
                    auto val_info = SimdInfo::for_ty(*this, params.m_types.at(1));
                    m_of << "for(int i = 0; i < " << val_info.count << "; i++) ";
                    m_of << "((";
                    val_info.emit_val_ty(*this);
                    m_of << "*)&";
                    emit_lvalue(e.ret_val);
                    m_of << ")[i] ";
                    m_of << "= ((";
                    emit_param(e.args.at(0));
                    m_of << ") >> i) != 0";
                    m_of << "? ((";
                    val_info.emit_val_ty(*this);
                    m_of << "*)&";
                    emit_param(e.args.at(1));
                    m_of << ")[i]";
                    m_of << ": ((";
                    val_info.emit_val_ty(*this);
                    m_of << "*)&";
                    emit_param(e.args.at(2));
                    m_of << ")[i]";
                    m_of << ";";
                }
                // Comparisons
                else if (name_strip == "simd_eq") {
                    simd_cmp("==");
                } else if (name_strip == "simd_ne") {
                    simd_cmp("!=");
                } else if (name_strip == "simd_lt") {
                    simd_cmp("<");
                } else if (name_strip == "simd_le") {
                    simd_cmp("<=");
                } else if (name_strip == "simd_gt") {
                    simd_cmp(">");
                } else if (name_strip == "simd_ge") {
                    simd_cmp(">=");
                }
                // Arithmetic
                else if (name_strip == "simd_add") {
                    simd_arith("+");
                } else if (name_strip == "simd_sub") {
                    simd_arith("-");
                } else if (name_strip == "simd_mul") {
                    simd_arith("*");
                } else if (name_strip == "simd_div") {
                    simd_arith("/");
                } else if (name_strip == "simd_and") {
                    simd_arith("&");
                } else if (name_strip == "simd_or") {
                    simd_arith("|");
                } else if (name_strip == "simd_xor") {
                    simd_arith("^");
                } else if (name_strip == "simd_xor") {
                    simd_arith("^");
                } else if (name_strip == "simd_shr") {
                    simd_arith(">>");
                } else if (name_strip == "simd_shl") {
                    simd_arith("<<");
                }
                // platform:simd_reduce_and
                // platform:simd_reduce_max
                // platform:simd_reduce_min
                // platform:simd_reduce_mul_unordered
                // platform:simd_reduce_add_unordered
                // platform:simd_reduce_or
                // platform:simd_saturating_add
                // platform:simd_saturating_sub
                else if (name_strip == "simd_ceil") {
                    simd_call("ceil");
                } else if (name_strip == "simd_floor") {
                    simd_call("floor");
                } else if (name_strip == "simd_fsqrt") {
                    simd_call("sqrt");
                }
                // platform:simd_fma
                else if (name_strip == "simd_fma") {
                    auto info = SimdInfo::for_ty(*this, params.m_types.at(0));
                    // Emulate!
                    m_of << "for(int i = 0; i < " << info.count << "; i++)";
                    m_of << "((";
                    info.emit_val_ty(*this);
                    m_of << "*)&";
                    emit_lvalue(e.ret_val);
                    m_of << ")[i] ";
                    m_of << "= ";
                    m_of << "__builtin_";
                    m_of << "fma(";
                    m_of << " ((";
                    info.emit_val_ty(*this);
                    m_of << "*)&";
                    emit_param(e.args.at(0));
                    m_of << ")[i],";
                    m_of << " ((";
                    info.emit_val_ty(*this);
                    m_of << "*)&";
                    emit_param(e.args.at(1));
                    m_of << ")[i],";
                    m_of << " ((";
                    info.emit_val_ty(*this);
                    m_of << "*)&";
                    emit_param(e.args.at(2));
                    m_of << ")[i]";
                    m_of << ")";
                }

                else {
                    // TODO: Platform intrinsics
                    m_of << "assert(!\"TODO: Platform intrinsic \\\"" << name << "\\\"\")";
                }
            } else {
                MIR_BUG(mir_res, "Unknown intrinsic '" << name << "'");
            }
            m_of << ";\n";
        }

        void emit_destructor_loop(
            const ::MIR::LValue& slot,
            const ::HIR::TypeData* element_ty,
            ::std::function<void()> emit_count,
            unsigned indent_level
        ) {
            auto indent = RepeatLitStr{"\t", static_cast<int>(indent_level)};
            auto element = ::MIR::LValue::new_Index(slot.clone(), ::MIR::LValue::Storage::MAX_ARG);

            m_of << indent << "for(unsigned i = 0; i < ";
            emit_count();
            m_of << "; i++) {\n";
            m_of << indent << "\ttry {\n";
            emit_destructor_call(element, element_ty, false, indent_level + 2);
            m_of << "\n" << indent << "\t} catch (...) {\n";
            m_of << indent << "\t\tfor(i++; i < ";
            emit_count();
            m_of << "; i++) {\n";
            m_of << indent << "\t\t\ttry {\n";
            emit_destructor_call(element, element_ty, false, indent_level + 4);
            m_of << "\n" << indent << "\t\t\t} catch (...) { abort(); }\n";
            m_of << indent << "\t\t}\n";
            m_of << indent << "\t\tthrow;\n";
            m_of << indent << "\t}\n";
            m_of << indent << "}";
        }

        void emit_tuple_destructor(
            const ::MIR::LValue& slot,
            const ::HIR::TypeData::Data_Tuple& tuple,
            bool unsized_valid,
            unsigned indent_level
        ) {
            ::std::vector<::MIR::LValue> fields;
            ::std::vector<const ::HIR::TypeData*> field_types;
            ::std::vector<bool> field_unsized;
            auto field = ::MIR::LValue::new_Field(slot.clone(), 0);
            for (size_t i = 0; i < tuple.size(); i++) {
                if (m_resolve.type_needs_drop_glue(sp, tuple[i])) {
                    fields.push_back(field.clone());
                    field_types.push_back(tuple[i]);
                    field_unsized.push_back(unsized_valid && i == tuple.size() - 1);
                }
                field.inc_Field();
            }
            if (fields.empty()) {
                return;
            }

            auto indent = RepeatLitStr{"\t", static_cast<int>(indent_level)};
            m_of << indent << "{ unsigned mrustc_drop_progress = 0;\n";
            m_of << indent << "\ttry {\n";
            for (size_t i = 0; i < fields.size(); i++) {
                emit_destructor_call(fields[i], field_types[i], field_unsized[i], indent_level + 2);
                m_of << indent << "\t\tmrustc_drop_progress = " << i + 1 << ";\n";
            }
            m_of << indent << "\t} catch (...) {\n";
            for (size_t i = 1; i < fields.size(); i++) {
                m_of << indent << "\t\tif(mrustc_drop_progress < " << i << ") {\n";
                m_of << indent << "\t\t\ttry {\n";
                emit_destructor_call(fields[i], field_types[i], field_unsized[i], indent_level + 4);
                m_of << indent << "\t\t\t} catch (...) { abort(); }\n";
                m_of << indent << "\t\t}\n";
            }
            m_of << indent << "\t\tthrow;\n";
            m_of << indent << "\t}\n";
            m_of << indent << "}";
        }

        /// slot :: The value to drop
        /// ty :: Type of value to be dropped
        /// unsized_valid ::
        /// indent_level :: (formatting) Current amount of indenting
        void emit_destructor_call(const ::MIR::LValue& slot, const ::HIR::TypeData* ty, bool unsized_valid, unsigned indent_level) {
            // If the type doesn't need dropping, don't try.
            if (!m_resolve.type_needs_drop_glue(sp, ty)) {
                return;
            }
            auto indent = RepeatLitStr{"\t", static_cast<int>(indent_level)};
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
                        emit_destructor_call(::MIR::LValue::new_Deref(slot.clone()), te.inner, true, indent_level);
                    }
                }
                TU_ARMA(Path, te) {
                    // Call drop glue
                    // - TODO: If the destructor is known to do nothing, don't call it.
                    auto p = ::HIR::Path(ty, "#drop_glue");
                    const char* make_fcn = nullptr;
                    switch (metadata_type(ty)) {
                        case MetadataType::Unknown:
                            MIR_BUG(*m_mir_res, ty << " unknown metadata");
                        case MetadataType::None:
                        case MetadataType::Zero:
                            if (this->type_is_bad_zst(ty) && this->lvalue_root_is_bad_zst(slot)) {
                                // The C backend omits zero-sized locals, but Rust still
                                // runs Drop for every logical ZST value.  Give Drop an
                                // address with the ZST's own alignment instead of naming
                                // an elided local (which can be behind Field/Index/etc.).
                                m_of << indent << "{ ";
                                emit_ctype(ty);
                                m_of << " mrustc_zst{}; " << TransMangle(p) << "(&mrustc_zst); }\n";
                            } else if (this->type_is_bad_zst(ty) && ::MIR::LValue::CRef(slot).is_Index()) {
                                m_of << indent << TransMangle(p) << "((";
                                emit_ctype(ty);
                                m_of << "*)";
                                emit_borrow(*m_mir_res, ::HIR::BorrowType::Unique, slot);
                                m_of << ");\n";
                            } else if (this->type_is_bad_zst(ty) && (slot.is_Field() || slot.is_Downcast())) {
                                // May need to back the slot out too, as we might be dropping a ZST tuple
                                auto v = ::MIR::LValue::CRef(slot).inner_ref();
                                ::HIR::TypeRef tmp;
                                if (this->type_is_bad_zst(m_mir_res->get_lvalue_type(tmp, v)) && (v.is_Field() || v.is_Downcast())) {
                                    v = v.inner_ref();
                                }
                                m_of << indent << TransMangle(p) << "((";
                                emit_ctype(ty);
                                m_of << "*)&";
                                emit_lvalue(v);
                                m_of << ");\n";
                            } else if (this->type_is_bad_zst(ty) && slot.m_wrappers.empty()) {
                                m_of << indent << TransMangle(p) << "((";
                                emit_ctype(ty);
                                m_of << "*)&rv);\n";
                            } else {
                                m_of << indent << TransMangle(p) << "(&";
                                emit_lvalue(slot);
                                m_of << ");\n";
                            }
                            break;
                        case MetadataType::Slice:
                            make_fcn = "make_sliceptr";
                            if (0) {
                                case MetadataType::TraitObject:
                                    make_fcn = "make_traitobjptr";
                            }
                            m_of << indent << TransMangle(p) << "( " << make_fcn << "(";
                            if (slot.is_Deref()) {
                                emit_lvalue(::MIR::LValue::CRef(slot).inner_ref());
                                m_of << ".PTR";
                            } else {
                                m_of << "&";
                                emit_lvalue(slot);
                            }
                            m_of << ", ";
                            auto lvr = ::MIR::LValue::CRef(slot);
                            while (lvr.is_Field()) {
                                lvr.try_unwrap();
                            }
                            MIR_ASSERT(*m_mir_res, lvr.is_Deref(), "Access to unized type without a deref - " << lvr << " (part of " << slot << ")");
                            emit_lvalue(lvr.inner_ref());
                            m_of << ".META";
                            m_of << ") );\n";
                            break;
                    }
                }
                TU_ARMA(Array, te) {
                    // Emit destructors for all entries
                    if (te.size.as_Known() > 0) {
                        emit_destructor_loop(slot, te.inner, [&] { m_of << te.size.as_Known(); }, indent_level);
                    }
                }
                TU_ARMA(Tuple, te) {
                    emit_tuple_destructor(slot, te, unsized_valid, indent_level);
                }
                TU_ARMA(TraitObject, te) {
                    MIR_ASSERT(*m_mir_res, unsized_valid, "Dropping TraitObject without an owned pointer");
                    // Call destructor in vtable
                    auto lvr = ::MIR::LValue::CRef(slot);
                    while (lvr.is_Field()) {
                        lvr.try_unwrap();
                    }
                    MIR_ASSERT(*m_mir_res, lvr.is_Deref(), "Access to unized type without a deref - " << lvr << " (part of " << slot << ")");
                    m_of << indent << "((VTABLE_HDR*)";
                    emit_lvalue(lvr.inner_ref());
                    m_of << ".META)->drop(";
                    if (slot.is_Deref()) {
                        emit_lvalue(::MIR::LValue::CRef(slot).inner_ref());
                        m_of << ".PTR";
                    } else {
                        m_of << "&";
                        emit_lvalue(slot);
                    }
                    m_of << ");";
                }
                TU_ARMA(Slice, te) {
                    MIR_ASSERT(*m_mir_res, unsized_valid, "Dropping Slice without an owned pointer");
                    auto lvr = ::MIR::LValue::CRef(slot);
                    while (lvr.is_Field()) {
                        lvr.try_unwrap();
                    }
                    MIR_ASSERT(*m_mir_res, lvr.is_Deref(), "Access to unized type without a deref - " << lvr << " (part of " << slot << ")");
                    // If one element destructor unwinds, Rust still drops the
                    // unvisited tail.  A second exception during that cleanup
                    // is a double panic and must terminate.
                    emit_destructor_loop(slot, te.inner, [&] {
                        emit_lvalue(lvr.inner_ref());
                        m_of << ".META";
                    }, indent_level);
                }
            }
        }

        void emit_enum_variant_val(const TypeRepr* repr, unsigned idx) {
            const auto& ve = repr->variants.as_Values();
            const auto& tag_ty = TargetGetInnerType(sp, m_resolve, *repr, ve.field.index, ve.field.sub_fields);
            switch (tag_ty->as_Primitive()) {
                case ::HIR::CoreType::I8:
                case ::HIR::CoreType::I16:
                case ::HIR::CoreType::I32:
                case ::HIR::CoreType::I64:
                case ::HIR::CoreType::Isize:
                    m_of << S128(ve.values[idx]).truncate_i64() << "ll";
                    break;
                case ::HIR::CoreType::Bool:
                case ::HIR::CoreType::U8:
                case ::HIR::CoreType::U16:
                case ::HIR::CoreType::U32:
                case ::HIR::CoreType::U64:
                case ::HIR::CoreType::Usize:
                case ::HIR::CoreType::Char:
                    m_of << ve.values[idx].truncate_u64() << "ull";
                    break;
                case ::HIR::CoreType::I128:
                    if (m_options.emulated_i128) {
                        m_of << "make128s_raw(" << ve.values[idx].get_hi() << "ull, " << ve.values[idx].get_lo() << "ull)";
                    } else {
                        m_of << "((int128_t)(((uint128_t)" << ve.values[idx].get_hi() << "ull << 64) | (uint128_t)" << ve.values[idx].get_lo() << "ull))";
                    }
                    break;
                case ::HIR::CoreType::U128:
                    if (m_options.emulated_i128) {
                        m_of << "make128_raw(" << ve.values[idx].get_hi() << "ull, " << ve.values[idx].get_lo() << "ull)";
                    } else {
                        m_of << "(((uint128_t)" << ve.values[idx].get_hi() << "ull << 64) | (uint128_t)" << ve.values[idx].get_lo() << "ull)";
                    }
                    break;
                case ::HIR::CoreType::F16:
                case ::HIR::CoreType::F32:
                case ::HIR::CoreType::F64:
                case ::HIR::CoreType::F128:
                    MIR_TODO(*m_mir_res, "Floating point enum tag.");
                    break;
                case ::HIR::CoreType::Str:
                    MIR_BUG(*m_mir_res, "Unsized tag?!");
            }
        }

        // returns whether a literal can be represented as zeroed memory.
        bool is_zero_literal(const ::HIR::TypeData* ty, const EncodedLiteral& lit, const TransParams& params) {
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

        void emit_lvalue(const ::MIR::LValue::CRef& val) {
            TU_MATCH_HDRA( (val), {)
            TU_ARMA(Return, _e) {
                    m_of << "rv";
                }
                TU_ARMA(Argument, e) {
                    m_of << "arg" << e;
                }
                TU_ARMA(Local, e) {
                    if (e == ::MIR::LValue::Storage::MAX_ARG) {
                        m_of << "i";
                    } else {
                        m_of << "var" << e;
                    }
                }
                TU_ARMA(Static, e) {
                    m_of << TransMangle(e);
                    m_of << ".val";
                }
                TU_ARMA(Field, field_index) {
                    ::HIR::TypeRef tmp;
                    auto inner = val.inner_ref();
                    const auto& ty = m_mir_res->get_lvalue_type(tmp, inner);
                    if (ty->is_Slice()) {
                        if (inner.is_Deref()) {
                            m_of << "((";
                            emit_ctype(ty->as_Slice().inner);
                            m_of << "*)";
                            emit_lvalue(inner.inner_ref());
                            m_of << ".PTR)";
                        } else {
                            emit_lvalue(inner);
                        }
                        m_of << "[" << field_index << "]";
                    } else if (ty->is_Array()) {
                        emit_lvalue(inner);
                        m_of << ".DATA[" << field_index << "]";
                    } else if (inner.is_Deref()) {
                        auto dst_type = metadata_type(ty);
                        if (dst_type != MetadataType::None) {
                            m_of << "((";
                            emit_ctype(ty);
                            m_of << "*)";
                            emit_lvalue(inner.inner_ref());
                            m_of << ".PTR)->_" << field_index;
                        } else {
                            emit_lvalue(inner.inner_ref());
                            m_of << "->_" << field_index;
                        }
                    } else {
                        emit_lvalue(inner);
                        m_of << "._" << field_index;
                    }
                }
                TU_ARMA(Deref, _e) {
                    auto inner = val.inner_ref();
                    ::HIR::TypeRef tmp;
                    const auto& ty = m_mir_res->get_lvalue_type(tmp, val);
                    auto dst_type = metadata_type(ty);
                    // If the type is unsized, then this pointer is a fat pointer, so we need to cast the data pointer.
                    if (dst_type != MetadataType::None) {
                        m_of << "(*(";
                        emit_ctype(ty);
                        m_of << "*)";
                        emit_lvalue(inner);
                        m_of << ".PTR)";
                    } else {
                        m_of << "(*";
                        emit_lvalue(inner);
                        m_of << ")";
                    }
                }
                TU_ARMA(Index, index_local) {
                    auto inner = val.inner_ref();
                    ::HIR::TypeRef tmp;
                    const auto& ty = m_mir_res->get_lvalue_type(tmp, inner);
                    m_of << "(";
                    if (ty->is_Slice()) {
                        if (inner.is_Deref()) {
                            m_of << "(";
                            emit_ctype(ty->as_Slice().inner);
                            m_of << "*)";
                            emit_lvalue(inner.inner_ref());
                            m_of << ".PTR";
                        } else {
                            emit_lvalue(inner);
                        }
                    } else if (ty->is_Array()) {
                        emit_lvalue(inner);
                        m_of << ".DATA";
                    } else {
                        emit_lvalue(inner);
                    }
                    m_of << ")[";
                    emit_lvalue(::MIR::LValue::new_Local(index_local));
                    m_of << "]";
                }
                TU_ARMA(Downcast, variant_index) {
                    auto inner = val.inner_ref();
                    ::HIR::TypeRef tmp;
                    const auto& ty = m_mir_res->get_lvalue_type(tmp, inner);
                    emit_lvalue(inner);
                    MIR_ASSERT(*m_mir_res, ty->is_Path(), "Downcast on non-Path type - " << ty);
                    if (ty->as_Path().binding.is_Enum()) {
                        m_of << ".DATA";
                    }
                    m_of << ".var_" << variant_index;
                }
            }
        }

        void emit_lvalue(const ::MIR::LValue& val) {
            emit_lvalue(::MIR::LValue::CRef(val));
        }

        void emit_constant(const ::MIR::Constant& ve, const ::MIR::LValue* dst_ptr = nullptr) {
            TU_MATCH_HDRA( (ve), {)
            TU_ARMA(Int, c) {
                    switch (c.t) {
                        // TODO: These should already have been truncated/reinterpreted, but just in case.
                        case ::HIR::CoreType::I8:
                            m_of << static_cast<int>(static_cast<int8_t>(c.v.truncate_i64())); // cast to int, because `int8_t` is printed as a `char`
                            break;
                        case ::HIR::CoreType::I16:
                            m_of << static_cast<int16_t>(c.v.truncate_i64());
                            break;
                        case ::HIR::CoreType::I32:
                            m_of << static_cast<int32_t>(c.v.truncate_i64());
                            break;
                        case ::HIR::CoreType::I64:
                        case ::HIR::CoreType::Isize:
                            if (c.v.truncate_i64() == INT64_MIN) {
                                m_of << "INT64_MIN";
                            } else if (c.v.truncate_i64() == INT64_MAX) {
                                m_of << "INT64_MAX";
                            } else {
                                m_of << c.v.truncate_i64();
                                m_of << "ll";
                            }
                            break;
                        case ::HIR::CoreType::I128:
                            if (m_options.emulated_i128) {
                                m_of << "make128s_raw(" << c.v.get_inner().get_hi() << "ull, " << c.v.get_inner().get_lo() << "ull)";
                            } else if (c.v.is_i64() && c.v.truncate_i64() != INT64_MIN) {
                                m_of << "(int128_t)";
                                m_of << c.v;
                                m_of << "ll";
                            } else {
                                m_of << "(int128_t)( ((uint128_t)" << c.v.get_inner().get_hi() << "ull << 64) | (uint128_t)" << c.v.get_inner().get_lo() << "ull)";
                            }
                            break;
                        default:
                            m_of << c.v;
                            break;
                    }
                }
                TU_ARMA(Uint, c) {
                    switch (c.t) {
                        case ::HIR::CoreType::U8:
                            m_of << ::std::hex << "0x" << (c.v.truncate_u64() & 0xFF) << ::std::dec;
                            break;
                        case ::HIR::CoreType::U16:
                            m_of << ::std::hex << "0x" << (c.v.truncate_u64() & 0xFFFF) << ::std::dec;
                            break;
                        case ::HIR::CoreType::U32:
                            m_of << ::std::hex << "0x" << (c.v.truncate_u64() & 0xFFFFFFFF) << ::std::dec;
                            break;
                        case ::HIR::CoreType::U64:
                        case ::HIR::CoreType::Usize:
                            m_of << ::std::hex << "0x" << c.v.truncate_u64() << "ull" << ::std::dec;
                            break;
                        case ::HIR::CoreType::U128:
                            if (m_options.emulated_i128) {
                                m_of << "make128_raw(" << c.v.get_hi() << "ull, " << c.v.get_lo() << "ull)";
                            } else if (c.v.is_u64()) {
                                m_of << "(uint128_t)";
                                m_of << ::std::hex << "0x" << c.v << "ull" << ::std::dec;
                            } else {
                                m_of << std::hex << "( ((uint128_t)0x" << c.v.get_hi() << "ull << 64) | (uint128_t)0x" << c.v.get_lo() << "ull)" << std::dec;
                            }
                            break;
                        case ::HIR::CoreType::Char:
                            assert(c.v <= 0x10FFFF);
                            if (c.v < 256) {
                                m_of << c.v;
                            } else {
                                m_of << ::std::hex << "0x" << c.v << ::std::dec;
                            }
                            break;
                        default:
                            MIR_BUG(*m_mir_res, "Invalid type for UInt literal - " << c.t);
                    }
                }
                TU_ARMA(Float, c) {
                    this->emit_float(c.v, c.t);
                }
                TU_ARMA(Bool, c) {
                    m_of << (c.v ? "true" : "false");
                }
                TU_ARMA(Bytes, c) {
                    // Array borrow : Cast the C string to the array
                    // - Laziness
                    m_of << "(void*)";
                    this->print_escaped_string(c);
                }
                TU_ARMA(StaticString, c) {
                    m_of << "make_sliceptr(";
                    this->print_escaped_string(c);
                    m_of << ", " << ::std::dec << c.size() << ")";
                }
                TU_ARMA(Const, c) {
                    MIR_BUG(*m_mir_res, "Unexpected Constant::Const - " << ve);
                }
                TU_ARMA(Generic, c) {
                    MIR_BUG(*m_mir_res, "Generic value present at codegen");
                }
                TU_ARMA(Function, c) {
                    MIR_TODO(*m_mir_res, "Constant::Function");
                }
                TU_ARMA(ItemAddr, c) {
                    const bool has_offset = c.offset != U128(0);
                    if (has_offset) {
                        MIR_ASSERT(*m_mir_res, c.offset.is_u64(), "Item address offset is too large: " << c.offset);
                        m_of << "((void*)((uint8_t*)";
                    }
                    if (c->m_data.is_UfcsInherent() && c->m_data.as_UfcsInherent().item == "#type_id") {
                        m_of << "(void*)&__typeid_" << TransMangle(c->m_data.as_UfcsInherent().type);
                    } else {
                        bool is_fcn = false;
                        MonomorphState ms_tmp(m_crate.m_types);
                        auto v = m_resolve.get_value(sp, *c, ms_tmp, /*signature_only=*/true);
                        is_fcn = v.is_Function() || v.is_EnumConstructor() || v.is_StructConstructor();
                        MIR_ASSERT(*m_mir_res, !is_fcn || !has_offset, "Function address has a non-zero offset: " << c.offset);
                        if (!is_fcn) {
                            m_of << "&";
                        }
                        m_of << TransMangle(*c);
                        if (!is_fcn) {
                            m_of << ".val";
                        }
                    }
                    if (has_offset) {
                        m_of << " + 0x" << ::std::hex << c.offset.truncate_u64() << ::std::dec << "))";
                    }
                }
            }
        }

        void emit_param(const ::MIR::Param& p, bool type_bytes = true) {
            TU_MATCH_HDRA( (p), {)
            TU_ARMA(LValue, e) {
                    emit_lvalue(e);
                }
                TU_ARMA(Borrow, e) {
                    emit_borrow(*m_mir_res, e.type, e.val);
                }
                TU_ARMA(Constant, e) {
                    if (type_bytes && e.is_Bytes()) {
                        ::HIR::TypeRef tmp;
                        m_of << "static_cast<";
                        emit_ctype(m_mir_res->get_param_type(tmp, p));
                        m_of << ">(";
                        emit_constant(e);
                        m_of << ")";
                    } else {
                        emit_constant(e);
                    }
                }
            }
        }

        void emit_trait_metadata_param(const ::MIR::TypeResolve& mir_res, const ::MIR::Param& param) {
            ::HIR::TypeRef tmp;
            const auto& ty = mir_res.get_param_type(tmp, param);
            emit_param(param);
            if (const auto* te = ty->opt_Path()) {
                if (te->path.m_data.is_Generic() && te->path.m_data.as_Generic().m_path == m_resolve.m_lang_DynMetadata) {
                    m_of << "._0._0";
                }
            }
        }

        void emit_ctype(const ::HIR::TypeData* ty) {
            emit_ctype(ty, FMT_CB(_, ));
        }

        void emit_ctype(const ::HIR::TypeData* ty, ::FmtLambda inner, bool is_extern_c = false) {
            TU_MATCH_HDRA( (*ty), {)
            TU_ARMA(Infer, te) {
                    m_of << "@" << ty << "@" << inner;
                }
                TU_ARMA(Diverge, te) {
                    m_of << "tBANG " << inner;
                }
                TU_ARMA(Primitive, te) {
                    switch (te) {
                        case ::HIR::CoreType::Usize:
                            m_of << "uintptr_t";
                            break;
                        case ::HIR::CoreType::Isize:
                            m_of << "intptr_t";
                            break;
                        case ::HIR::CoreType::U8:
                            m_of << "uint8_t";
                            break;
                        case ::HIR::CoreType::I8:
                            m_of << "int8_t";
                            break;
                        case ::HIR::CoreType::U16:
                            m_of << "uint16_t";
                            break;
                        case ::HIR::CoreType::I16:
                            m_of << "int16_t";
                            break;
                        case ::HIR::CoreType::U32:
                            m_of << "uint32_t";
                            break;
                        case ::HIR::CoreType::I32:
                            m_of << "int32_t";
                            break;
                        case ::HIR::CoreType::U64:
                            m_of << "uint64_t";
                            break;
                        case ::HIR::CoreType::I64:
                            m_of << "int64_t";
                            break;
                        case ::HIR::CoreType::U128:
                            m_of << "uint128_t";
                            break;
                        case ::HIR::CoreType::I128:
                            m_of << "int128_t";
                            break;

                        case ::HIR::CoreType::F16:
                            m_of << "f16";
                            break;
                        case ::HIR::CoreType::F32:
                            m_of << "float";
                            break;
                        case ::HIR::CoreType::F64:
                            m_of << "double";
                            break;
                        case ::HIR::CoreType::F128:
                            m_of << "f128";
                            break;

                        case ::HIR::CoreType::Bool:
                            m_of << "RUST_BOOL";
                            break;
                        case ::HIR::CoreType::Char:
                            m_of << "RUST_CHAR";
                            break;
                        case ::HIR::CoreType::Str:
                            MIR_BUG(*m_mir_res, "Raw str");
                    }
                    m_of << " " << inner;
                }
                TU_ARMA(Path, te) {
                    //if( const auto* ity = m_resolve.is_type_owned_box(ty) ) {
                    //    emit_ctype_ptr(*ity, inner);
                    //    return ;
                    //}
                TU_MATCH_HDRA( (te.binding), { )
                TU_ARMA(Struct, tpb) {
                            m_of << "struct s_" << TransMangle(te.path);
                        }
                        TU_ARMA(Union, tpb) {
                            m_of << "union u_" << TransMangle(te.path);
                        }
                        TU_ARMA(Enum, tpb) {
                            m_of << "struct e_" << TransMangle(te.path);
                        }
                        TU_ARMA(ExternType, tpb) {
                            m_of << "struct x_" << TransMangle(te.path);
                        }
                        TU_ARMA(Unbound, tpb) {
                            MIR_BUG(*m_mir_res, "Unbound type path in trans - " << ty);
                        }
                        TU_ARMA(Opaque, tpb) {
                            MIR_BUG(*m_mir_res, "Opaque path in trans - " << ty);
                        }
                }
                m_of << " " << inner;
                }
                TU_ARMA(Generic, te) {
                    MIR_BUG(*m_mir_res, "Generic in trans - " << ty);
                }
                TU_ARMA(TraitObject, te) {
                    MIR_BUG(*m_mir_res, "Raw trait object - " << ty);
                }
                TU_ARMA(ErasedType, te) {
                    MIR_BUG(*m_mir_res, "ErasedType in trans - " << ty);
                }
                TU_ARMA(Array, te) {
                    m_of << "t_" << TransMangle(ty) << " " << inner;
                    //emit_ctype(te.inner, inner);
                    //m_of << "[" << te.size.as_Known() << "]";
                }
                TU_ARMA(Slice, te) {
                    MIR_BUG(*m_mir_res, "Raw slice object - " << ty);
                }
                TU_ARMA(Tuple, te) {
                    if (te.size() == 0) {
                        m_of << "tUNIT";
                    } else {
                        m_of << "TUP_" << te.size();
                        for (const auto& t : te) {
                            m_of << "_" << TransMangle(t);
                        }
                    }
                    m_of << " " << inner;
                }
                TU_ARMA(Borrow, te) {
                    emit_ctype_ptr(te.inner, inner);
                }
                TU_ARMA(Pointer, te) {
                    emit_ctype_ptr(te.inner, inner);
                }
                TU_ARMA(NamedFunction, te) {
                    m_of << "t_" << TransMangle(ty) << " " << inner;
                }
                TU_ARMA(Function, te) {
                    m_of << "t_" << TransMangle(ty) << " " << inner;
                }
                break;
                case ::HIR::TypeData::TAG_NodeType:
                    MIR_BUG(*m_mir_res, "NodeType during trans - " << ty);
                    break;
            }
        }

        ::HIR::TypeRef get_inner_unsized_type(const ::HIR::TypeData* ty) {
            if (ty == ::HIR::CoreType::Str || ty->is_Slice()) {
                return ty;
            } else if (ty->is_TraitObject()) {
                return ty;
            } else if (ty->is_Path()) {
                TU_MATCH_HDRA( (ty->as_Path().binding), {)
                default:
                    MIR_BUG(*m_mir_res, "Unbound/opaque path in trans - " << ty);
                    throw "";
                    TU_ARMA(Struct, tpb) {
                        switch (tpb->m_struct_markings.dst_type) {
                            case ::HIR::StructMarkings::DstType::None:
                                return ::HIR::TypeRef();
                            case ::HIR::StructMarkings::DstType::Slice:
                            case ::HIR::StructMarkings::DstType::TraitObject:
                            case ::HIR::StructMarkings::DstType::Possible: {
                                // TODO: How to figure out? Lazy way is to check the monomorpised type of the last field (structs only)
                                const auto& path = ty->as_Path().path.m_data.as_Generic();
                                const auto& str = *ty->as_Path().binding.as_Struct();
                                auto monomorph = [&](const auto& tpl) {
                                    return m_resolve.monomorph_expand(sp, tpl, MonomorphStatePtr(m_crate.m_types, nullptr, &path.m_params, nullptr));
                                };
                        TU_MATCH_HDRA( (str.m_data), { )
                        TU_ARMA(Unit, se) MIR_BUG(*m_mir_res, "Unit-like struct with DstType::Possible");
                                    TU_ARMA(Tuple, se) return get_inner_unsized_type(monomorph(se.back().ent));
                                    TU_ARMA(Named, se) return get_inner_unsized_type(monomorph(se.back().ty));
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

        unsigned get_packing_max_align(const ::HIR::TypeData* ty) const {
            if (ty->is_Path() && ty->as_Path().binding.is_Struct()) {
                return ty->as_Path().binding.as_Struct()->m_max_field_alignment;
            }
            return 0;
        }

        void emit_trait_object_vtable_size(const ::MIR::Param& value) {
            m_of << "((VTABLE_HDR*)";
            emit_param(value);
            m_of << ".META)->size";
        }

        void emit_trait_object_vtable_align(const ::MIR::Param& value) {
            m_of << "((VTABLE_HDR*)";
            emit_param(value);
            m_of << ".META)->align";
        }

        void emit_trait_object_dst_tail_align(const ::HIR::TypeData* outer_ty, const ::HIR::TypeData* tail_ty, const ::MIR::Param& value) {
            const auto max_align = get_packing_max_align(outer_ty);
            if (max_align != 0) {
                m_of << "mrustc_min(";
            }
            emit_trait_object_dst_align(tail_ty, value);
            if (max_align != 0) {
                m_of << ", " << max_align << ")";
            }
        }

        void emit_trait_object_dst_align(const ::HIR::TypeData* ty, const ::MIR::Param& value) {
            if (ty->is_TraitObject()) {
                emit_trait_object_vtable_align(value);
                return;
            }

            const auto* repr = TargetGetTypeRepr(sp, m_resolve, ty);
            MIR_ASSERT(*m_mir_res, repr && repr->size == SIZE_MAX && !repr->fields.empty(), "Expected a DST wrapper - " << ty);
            m_of << "mrustc_max(" << repr->align << ", ";
            emit_trait_object_dst_tail_align(ty, repr->fields.back().ty, value);
            m_of << ")";
        }

        void emit_trait_object_dst_size(const ::HIR::TypeData* ty, const ::MIR::Param& value) {
            if (ty->is_TraitObject()) {
                emit_trait_object_vtable_size(value);
                return;
            }

            const auto* repr = TargetGetTypeRepr(sp, m_resolve, ty);
            MIR_ASSERT(*m_mir_res, repr && repr->size == SIZE_MAX && !repr->fields.empty(), "Expected a DST wrapper - " << ty);
            const auto& tail = repr->fields.back();
            m_of << "ALIGN_TO(ALIGN_TO(" << tail.offset << ", ";
            emit_trait_object_dst_tail_align(ty, tail.ty, value);
            m_of << ") + ";
            emit_trait_object_dst_size(tail.ty, value);
            m_of << ", ";
            emit_trait_object_dst_align(ty, value);
            m_of << ")";
        }

        void emit_trait_object_dst_field_offset(const ::HIR::TypeData* ty, size_t field_idx, const ::MIR::Param& value) {
            const auto* repr = TargetGetTypeRepr(sp, m_resolve, ty);
            MIR_ASSERT(*m_mir_res, repr && field_idx < repr->fields.size(), "Invalid DST field " << field_idx << " on " << ty);
            const auto& field = repr->fields[field_idx];
            auto inner_ty = get_inner_unsized_type(field.ty);
            MIR_ASSERT(*m_mir_res, field_idx + 1 == repr->fields.size() && inner_ty->is_TraitObject(), "Expected final trait object field on " << ty);
            m_of << "ALIGN_TO(" << field.offset << ", ";
            emit_trait_object_dst_tail_align(ty, field.ty, value);
            m_of << ")";
        }

        MetadataType metadata_type(const ::HIR::TypeData* ty) const {
            return m_resolve.metadata_type(m_mir_res ? m_mir_res->sp : sp, ty);
        }

        void emit_ctype_ptr(const ::HIR::TypeData* inner_ty, ::FmtLambda inner) {
            //if( inner_ty->is_Array() ) {
            //    emit_ctype(inner_ty, FMT_CB(ss, ss << "(*" << inner << ")";));
            //}
            //else
            {
                switch (this->metadata_type(inner_ty)) {
                    case MetadataType::Unknown:
                        BUG(sp, inner_ty << " unknown metadata type");
                    case MetadataType::None:
                    case MetadataType::Zero:
                        emit_ctype(inner_ty, FMT_CB(ss, ss << "*" << inner;));
                        break;
                    case MetadataType::Slice:
                        m_of << "SLICE_PTR " << inner;
                        break;
                    case MetadataType::TraitObject:
                        m_of << "TRAITOBJ_PTR " << inner;
                        break;
                }
            }
        }

        bool is_dst(const ::HIR::TypeData* ty) const {
            switch (this->metadata_type(ty)) {
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
