#include "hir_conv_constant_evaluation.h"
#include "hir_conv_main_bindings.h"
#include "hir_hir.h"
#include "hir_expr.h"
#include "hir_typeck_expr_visit.h"
#include "hir_visitor.h"
#include <algorithm>
#include <cmath>
#include "mir_mir.h"
#include "hir_typeck_common.h" // Monomorph
#include "mir_helpers.h"
#include "trans_target.h"
#include "hir_expr_state.h"
#include "int128.h" // 128 bit integer support
#include "floats.h"

#include "trans_monomorphise.h" // For handling monomorph of MIR in provided associated constants
#include "trans_codegen.h"      // For encoding as part of transmute

namespace {
    void ConvertHIRConstantEvaluateStatic(const ::HIR::Crate& crate, const ::HIR::GenericParams* impl_params, const ::HIR::ItemPath& ip, ::HIR::Static& e);
    void ConvertHIRConstantEvaluateFcnSig(const ::HIR::Crate& crate, const ::HIR::GenericParams* impl_params, const ::HIR::ItemPath& ip, ::HIR::Function& fcn);

    struct Defer {};

    struct NewvalStateNop: public HIR::Evaluator::Newval {
        const Span& sp;

        NewvalStateNop(const Span& sp)
            : sp(sp)
        {
        }

        ::HIR::Path new_static(::HIR::TypeRef type, EncodedLiteral value) override {
            TODO(this->sp, "new_static while evaluating a const generic");
        }
    };

    EncodedLiteral evaluateConstgeneric(const Span& sp, const ::HIR::Crate& crate, const HIR::TypeData* type, const ::HIR::ConstGenericUnevaluated& value) {
        const auto& expr = *value.expr;
        ASSERT_BUG(sp, expr.state, "Const-generic expression has no state");
        const auto& state = *expr.state;
        auto name = FMT("const_" << &expr << "#");

        NewvalStateNop nvs{sp};
        auto eval = ::HIR::Evaluator{sp, crate, nvs};
        eval.setRequireConstCalls();
        eval.resolve.setBothGenericsRaw(state.implGenerics, state.itemGenerics);

        MonomorphState ms(crate.types);
        ms.ppImpl = &value.paramsImpl;
        ms.ppMethod = &value.paramsItem;
        return eval.evaluateConstant(::HIR::ItemPath(state.modPath, name.c_str()), expr, type, std::move(ms));
    }

    struct NewvalState: public HIR::Evaluator::Newval {
        const ::HIR::Module& mod;
        const ::HIR::ItemPath& mod_path;
        ::std::string namePrefix;
        unsigned int nextItemIdx;

        NewvalState(const ::HIR::Module& mod, const ::HIR::ItemPath& mod_path, ::std::string prefix)
            : mod(mod)
            , mod_path(mod_path)
            , namePrefix(prefix)
            , nextItemIdx(0)
        {
        }

        ::HIR::Path new_static(::HIR::TypeRef type, EncodedLiteral value) override {
            ASSERT_BUG(Span(), type != HIR::TypeRef(), "");
            auto name = RcString::newInterned(FMT(namePrefix << nextItemIdx));
            nextItemIdx++;
            auto rv = mod_path.getSimplePath() + name.c_str();
            auto s = ::HIR::Static(::HIR::Linkage(), false, mv$(type), ::HIR::ExprPtr());
            s.valueRes = ::std::move(value);
            s.valueGenerated = true;
            s.saveLiteral = true;
            DEBUG(rv << ": " << s.mType << " = " << s.valueRes);

            const_cast<::HIR::Module&>(mod).inlineStatics.push_back(::std::make_pair(mv$(name), box$(s)));
            return rv;
        }
    };

    TAGGED_UNION(EntPtr, NotFound, (NotFound, struct {}), (Function, const ::HIR::Function*), (Static, const ::HIR::Static*), (Constant, const ::HIR::Constant*), (Struct, const ::HIR::Struct*), (Enum, struct {
                     const ::HIR::Enum* p;
                     size_t idx;
                 }));
    enum class EntNS {
        //Type,
        Value
    };
}

namespace MIR {
    namespace eval {
        class Allocation;
        class Constant;
        class StaticRef;
        class RelocPtr;

        template <typename T>
        class EvalPtr {
            friend class RelocPtr;

        protected:
            T* ptr;

        public:
            EvalPtr()
                : ptr(nullptr)
            {
            }

            operator bool() const {
                return ptr != 0;
            }

            T* operator->() {
                return ptr;
            }

            const T* operator->() const {
                return ptr;
            }

            T& operator*() {
                return *ptr;
            }

            const T& operator*() const {
                return *ptr;
            }
        };

        /// "Statically allocated" constant data
        class ConstantPtr final: public EvalPtr<Constant> {
        public:
            static ConstantPtr allocate(stl::ObjPool* pool, const void* data, size_t len);
        };

        /// Mutable allocation
        class AllocationPtr final: public EvalPtr<Allocation> {
        public:
            static AllocationPtr allocate(stl::ObjPool* pool, const StaticTraitResolve& resolve, const ::MIR::TypeResolve& state, const ::HIR::TypeData* ty);
            static AllocationPtr allocateRo(stl::ObjPool* pool, const void* data, size_t len);
        };

        /// Reference to a `static`
        class StaticRefPtr final: public EvalPtr<StaticRef> {
        public:
            static StaticRefPtr allocate(stl::ObjPool* pool, ::HIR::Path p, const EncodedLiteral* lit);
        };

        /// Common interface for data storage
        class IValue {
        public:
            virtual void fmtIdent(std::ostream& os) const = 0;
            virtual void fmt(::std::ostream& os, size_t ofs, size_t len) const = 0;

            virtual size_t size() const = 0;
            virtual const uint8_t* getBytes(size_t ofs, size_t len, bool checkMask) const = 0;
            virtual void readMask(uint8_t* dst, size_t dstOfs, size_t ofs, size_t len) const = 0;

            virtual bool isWritable() const = 0;
            virtual uint8_t* extWriteBytes(size_t ofs, size_t len) = 0;

            void write_bytes(size_t ofs, const void* data, size_t len) {
                memcpy(extWriteBytes(ofs, len), data, len);
            }

            virtual void write_mask_from(size_t ofs, const IValue& src, size_t srcOfs, size_t len) = 0;

            virtual RelocPtr getReloc(size_t ofs) const = 0;
            virtual void setReloc(size_t ofs, RelocPtr ptr) = 0;
        };

        /// Pointer wrapping a reference-counted allocation
        class RelocPtr {
            uintptr_t ptr;

            enum Tag {
                TAG_Allocation = 0,
                TAG_Constant,
                TAG_StaticRef,
            };

        public:
            ~RelocPtr() = default;
            RelocPtr(const RelocPtr&) = default;
            RelocPtr(RelocPtr&&) = default;
            RelocPtr& operator=(const RelocPtr&) = default;
            RelocPtr& operator=(RelocPtr&&) = default;

            RelocPtr()
                : ptr(0)
            {
            }

            RelocPtr(AllocationPtr p)
                : ptr(0)
            {
                set(reinterpret_cast<uintptr_t>(p.ptr), TAG_Allocation);
            }

            RelocPtr(ConstantPtr p)
                : ptr(0)
            {
                set(reinterpret_cast<uintptr_t>(p.ptr), TAG_Constant);
            }

            RelocPtr(StaticRefPtr p)
                : ptr(0)
            {
                set(reinterpret_cast<uintptr_t>(p.ptr), TAG_StaticRef);
            }

            operator bool() const {
                return ptr != 0;
            }

            bool operator==(const RelocPtr& x) const {
                return ptr == x.ptr;
            }

            IValue& asValue() {
                return *asValuePtr();
            }

            const IValue& asValue() const {
                return *asValuePtr();
            }

            Allocation* asAllocation() const {
                return (ptr != 0 && (ptr & 3) == TAG_Allocation) ? reinterpret_cast<Allocation*>(ptr - TAG_Allocation) : nullptr;
            }

            Constant* asConstant() const {
                return (ptr != 0 && (ptr & 3) == TAG_Constant) ? reinterpret_cast<Constant*>(ptr - TAG_Constant) : nullptr;
            }

            StaticRef* asStaticref() const {
                return (ptr != 0 && (ptr & 3) == TAG_StaticRef) ? reinterpret_cast<StaticRef*>(ptr - TAG_StaticRef) : nullptr;
            }

            friend std::ostream& operator<<(std::ostream& os, const RelocPtr& ptr) {
                if (ptr.ptr) {
                    ptr.asValuePtr()->fmtIdent(os);
                } else {
                    os << "NULL";
                }
                return os;
            }

        private:
            IValue* asValuePtr() const;

            void set(uintptr_t ptr, Tag tag) {
                assert(this->ptr == 0);
                assert((ptr & 3) == 0);
                assert(tag < 4);
                this->ptr = ptr | tag;
            }
        };

        /// Helper: Print a 2-digit hex value (without updating the stream state)
        void putbHex(std::ostream& os, uint8_t v) {
            char tmp[3];
            tmp[0] = "0123456789ABCDEF"[v >> 4];
            tmp[1] = "0123456789ABCDEF"[v & 0xF];
            tmp[2] = '\0';
            os << tmp;
        }

        /// Constant data
        class Constant final: public IValue {
            friend struct stl::Embed<Constant>;
            friend class ConstantPtr;
            unsigned const length;
            const uint8_t* const data;

            Constant(const void* data, size_t len)
                : length(len)
                , data(reinterpret_cast<const uint8_t*>(data))
            {
            }

        public:
            void fmtIdent(std::ostream& os) const override {
                os << "C:" << (const void*)this->data;
            }

            void fmt(::std::ostream& os, size_t ofs, size_t len) const override {
                assert(ofs <= length);
                assert(ofs + len <= length);
                for (size_t i = 0; i < len; i++) {
                    if (i != 0 && (ofs + i) % 8 == 0) {
                        os << " ";
                    }
                    putbHex(os, this->data[ofs + i]);
                }
            }

            size_t size() const {
                return length;
            }

            const uint8_t* getBytes(size_t ofs, size_t len, bool /*check_mask*/) const override {
                if (!(ofs <= length) || !(len <= length) || !(ofs + len <= length)) {
                    return nullptr;
                }
                return data + ofs;
            }

            void readMask(uint8_t* dst, size_t dstOfs, size_t /*ofs*/, size_t len) const {
                dst += dstOfs / 8;
                dstOfs %= 8;
                if (dstOfs != 0) {
                    // Do a single-bit fill
                    while (len--) {
                        dst[dstOfs / 8] |= 1 << (dstOfs % 8);
                    }
                } else {
                    memset(dst, 0xFF, len / 8);
                    dst[len / 8] |= 0xFF >> (8 - len % 8);
                }
            }

            bool isWritable() const override {
                return false;
            }

            uint8_t* extWriteBytes(size_t ofs, size_t len) override {
                abort();
            }

            void write_mask_from(size_t ofs, const IValue& src, size_t srcOfs, size_t len) override {
                abort();
            }

            RelocPtr getReloc(size_t ofs) const override {
                return RelocPtr();
            }

            void setReloc(size_t ofs, RelocPtr ptr) override {
                abort();
            }
        };

        class Allocation final: public IValue {
            friend struct stl::Embed<Allocation>;
            friend class AllocationPtr;

        public:
            struct Reloc {
                size_t offset;
                RelocPtr ptr;
            };

        private:
            unsigned length;
            bool isReadonly;
            ::HIR::TypeRef mType;
            std::vector<Reloc> relocations;
            uint8_t* data;

            Allocation(uint8_t* data, size_t len, const ::HIR::TypeData* ty)
                : length(len)
                , isReadonly(false)
                , mType(ty)
                , data(data)
            {
                memset(data, 0, len + (len + 7) / 8);
            }

            Allocation(const Allocation&) = delete;
            Allocation& operator=(const Allocation&) = delete;

        public:
            void fmtIdent(std::ostream& os) const override {
                os << "A:" << this;
            }

            void fmt(::std::ostream& os, size_t ofs, size_t len) const override {
                assert(ofs <= length);
                assert(ofs + len <= length);
                for (size_t i = 0; i < len; i++) {
                    auto j = ofs + i;
                    if (i != 0 && j % 8 == 0) {
                        os << " ";
                    }
                    for (const auto& r : relocations) {
                        if (r.offset == j) {
                            os << "{" << r.ptr << "}";
                        }
                    }
                    if (getMask()[j / 8] & (1 << j % 8)) {
                        putbHex(os, data[j]);
                    } else {
                        os << "--";
                    }
                }
            }

            size_t size() const override {
                return length;
            }

            const uint8_t* getBytes(size_t ofs, size_t len, bool checkMask) const override {
                if (!(ofs <= length) || !(len <= length) || !(ofs + len <= length)) {
                    return nullptr;
                }

                if (checkMask) {
                    const auto* m = this->getMask();
                    size_t mo = ofs, ml = len;
                    for (; mo % 8 != 0 && ml > 0; mo++, ml--) {
                        if (!(m[mo / 8] & (1 << (mo % 8)))) {
                            return nullptr;
                        }
                    }
                    for (; ml >= 8; mo += 8, ml -= 8) {
                        if (!(m[mo / 8] == 0xFF)) {
                            return nullptr;
                        }
                    }
                    for (; ml % 8 != 0 && ml > 0; mo++, ml--) {
                        if (!(m[mo / 8] & (1 << (mo % 8)))) {
                            return nullptr;
                        }
                    }
                }

                return this->data + ofs;
            }

            void readMask(uint8_t* dst, size_t dstOfs, size_t ofs, size_t len) const {
                assert(ofs <= length);
                assert(len <= length);
                assert(ofs + len <= length);

                dst += dstOfs / 8;
                const auto* src = getMask() + ofs / 8;
                dstOfs %= 8;
                ofs %= 8;
                if (dstOfs != 0 || ofs != 0) {
                    // If the entries are unaligned, then use a bit-by-bit copy
                    for (size_t i = 0; i < len; i++) {
                        size_t s = ofs + i;
                        size_t d = dstOfs + i;
                        if (src[s / 8] & (1 << s % 8)) {
                            dst[d / 8] |= (1 << d % 8);
                        } else {
                            dst[d / 8] &= ~(1 << d % 8);
                        }
                    }
                } else {
                    for (; len >= 8; len -= 8) {
                        *dst++ = *src++;
                    }
                    // Tail entires (partial byte)
                    if (len > 0) {
                        uint8_t mask = (0xFF >> (8 - len));
                        *dst = (*dst & ~mask) | (*src & mask);
                    }
                }
            }

            bool isWritable() const override {
                return !isReadonly;
            }

            uint8_t* extWriteBytes(size_t ofs, size_t len) override {
                ASSERT_BUG(Span(), ofs <= length && len <= length && ofs + len <= length, "OOB write: " << ofs << "+" << len << " out of " << length);
                // Set the mask
                {
                    auto* m = this->getMask();
                    size_t mo = ofs, ml = len;
                    for (; mo % 8 != 0 && ml > 0; mo++, ml--) {
                        m[mo / 8] |= (1 << (mo % 8));
                    }
                    for (; ml >= 8; mo += 8, ml -= 8) {
                        m[mo / 8] = 0xFF;
                    }
                    for (; ml % 8 != 0 && ml > 0; mo++, ml--) {
                        m[mo / 8] |= (1 << (mo % 8));
                    }
                }
                // Clear impacted relocations
                auto it = std::remove_if(this->relocations.begin(), this->relocations.end(), [&](const Reloc& r) {
                    return (ofs <= r.offset && r.offset < ofs + len);
                });
                this->relocations.resize(it - this->relocations.begin());
                return this->data + ofs;
            }

            void write_mask_from(size_t ofs, const IValue& src, size_t srcOfs, size_t len) override {
                assert(ofs <= length);
                assert(len <= length);
                assert(ofs + len <= length);
                src.readMask(getMask(), ofs, srcOfs, len);
            }

            RelocPtr getReloc(size_t ofs) const override {
                for (const auto& r : this->relocations) {
                    if (r.offset == ofs) {
                        return r.ptr;
                    }
                }
                return RelocPtr();
            }

            void setReloc(size_t ofs, RelocPtr ptr) override {
                assert(ofs % (TargetGetPointerBits() / 8) == 0);
                auto it = std::lower_bound(this->relocations.begin(), this->relocations.end(), ofs, [](const Reloc& r, size_t ofs) {
                    return r.offset < ofs;
                });
                if (it != this->relocations.end() && it->offset == ofs) {
                    if (ptr) {
                        it->ptr = std::move(ptr);
                    } else {
                        this->relocations.erase(it);
                    }
                } else {
                    if (ptr) {
                        this->relocations.insert(it, Reloc{ofs, std::move(ptr)});
                    } else
                        ;
                }
            }

            const ::HIR::TypeData* getType() const {
                return mType;
            }

            const std::vector<Reloc>& getRelocations() const {
                return relocations;
            }

        private:
            uint8_t* getMask() {
                return data + length;
            }

            const uint8_t* getMask() const {
                return data + length;
            }
        };

        class StaticRef final: public IValue {
            friend struct stl::Embed<StaticRef>;
            friend class StaticRefPtr;

            stl::ObjPool* pool;
            ::HIR::Path mPath;
            const EncodedLiteral* encoded;

            StaticRef(stl::ObjPool* pool, ::HIR::Path p, const EncodedLiteral* lit = nullptr)
                : pool(pool)
                , mPath(std::move(p))
                , encoded(lit)
            {
            }

        public:
            void fmtIdent(std::ostream& os) const override {
                os << this->mPath;
            }

            void fmt(::std::ostream& os, size_t ofs, size_t len) const override {
                os << "[" << mPath << "]";
                if (encoded) {
                    os << EncodedLiteralSlice(*encoded).slice(ofs, len);
                } else {
                    os << "?";
                }
            }

            size_t size() const {
                return encoded ? encoded->bytes.size() : 0;
            }

            const uint8_t* getBytes(size_t ofs, size_t len, bool checkMask) const override {
                if (encoded) {
                    assert(ofs <= encoded->bytes.size());
                    assert(len <= encoded->bytes.size());
                    assert(ofs + len <= encoded->bytes.size());
                    if (encoded->bytes.size() == 0) {
                        // Empty vectors can have a null data pointer
                        return reinterpret_cast<const uint8_t*>("");
                    }
                    return encoded->bytes.data() + ofs;
                } else {
                    if (len == 0 && ofs == 0) {
                        static uint8_t null;
                        return &null;
                    }
                    return nullptr;
                }
            }

            void readMask(uint8_t* dst, size_t dstOfs, size_t ofs, size_t len) const override {
                dst += dstOfs / 8;
                dstOfs %= 8;
                if (dstOfs != 0) {
                    // Do a single-bit fill
                    while (len--) {
                        dst[dstOfs / 8] |= 1 << (dstOfs % 8);
                    }
                } else {
                    memset(dst, 0xFF, len / 8);
                    dst[len / 8] |= 0xFF >> (8 - len % 8);
                }
            }

            bool isWritable() const override {
                return false;
            }

            uint8_t* extWriteBytes(size_t ofs, size_t len) override {
                abort();
            }

            void write_mask_from(size_t ofs, const IValue& src, size_t srcOfs, size_t len) override {
                abort();
            }

            RelocPtr getReloc(size_t ofs) const override {
                if (encoded) {
                    for (const auto& r : encoded->relocations) {
                        if (r.ofs == ofs) {
                            RelocPtr reloc;
                            if (r.p) {
                                return RelocPtr(StaticRefPtr::allocate(pool, r.p->clone(), nullptr));
                                TODO(Span(), "Convert relocation pointer - " << *r.p);
                            } else {
                                return RelocPtr(AllocationPtr::allocateRo(pool, r.bytes.data(), r.bytes.size()));
                            }
                        }
                    }
                }
                return RelocPtr();
            }

            void setReloc(size_t ofs, RelocPtr ptr) override {
                abort();
            }

            const ::HIR::Path& path() const {
                return mPath;
            }
        };

        /// Reference to a value
        class ValueRef {
            RelocPtr storage;
            uint32_t ofs;
            uint32_t len;

        public:
            ValueRef()
                : storage()
                , ofs(0)
                , len(0)
            {
            }

            ValueRef(RelocPtr alloc, size_t ofs = 0)
                : storage(alloc)
                , ofs(ofs)
                , len(0)
            {
                if (alloc) {
                    assert(ofs <= alloc.asValue().size());
                    len = alloc.asValue().size() - ofs;
                }
            }

            ValueRef slice(size_t ofs, size_t len) {
                ASSERT_BUG(Span(), ofs <= this->len && ofs + len <= this->len, "ValueRef::slice: " << ofs << "+" << len << " out of range (" << this->len << ")");

                ValueRef rv;
                rv.storage = storage;
                rv.ofs = this->ofs + ofs;
                rv.len = len;
                return rv;
            }

            ValueRef slice(size_t ofs) {
                ASSERT_BUG(Span(), ofs <= this->len, "ValueRef::slice: " << ofs << " out of range (" << this->len << ")");
                return slice(ofs, this->len - ofs);
            }

            bool isValid() const {
                return storage;
            }

            RelocPtr getStorage() const {
                return storage;
            }

            size_t getOfs() const {
                return ofs;
            }

            size_t getLen() const {
                return len;
            }

            void copyFrom(const MIR::TypeResolve& state, const ValueRef& other) {
                size_t len = std::min(this->len, other.len);
                // Check that there's no overlap
                if (this->storage == other.storage) {
                    if (this->ofs < other.ofs) {
                        MIR_ASSERT(state, this->ofs + len <= other.ofs, "Overlapping copy_from: " << other.ofs << "+" << len << " and " << this->ofs << "+" << len);
                    } else {
                        MIR_ASSERT(state, other.ofs + len <= this->ofs, "Overlapping copy_from: " << other.ofs << "+" << len << " and " << this->ofs << "+" << len);
                    }
                }
                // Copy the data (don't check the source mask when getting the source pointer)
                const auto* src = other.storage.asValue().getBytes(other.ofs, len, /*check_mask*/ false);
                MIR_ASSERT(state, src, "Invalid read " << other.storage << " - " << other.ofs << "+" << len);
                storage.asValue().write_bytes(this->ofs, src, len);
                // Copy the mask data
                storage.asValue().write_mask_from(this->ofs, other.storage.asValue(), other.ofs, len);
                // Copy relocations
                for (size_t i = 0; i < len; i++) {
                    if (auto r = other.storage.asValue().getReloc(other.ofs + i)) {
                        storage.asValue().setReloc(this->ofs + i, std::move(r));
                    }
                }
            }

            void write_bytes(const MIR::TypeResolve& state, const void* data, size_t len) {
                MIR_ASSERT(state, storage, "Writing to invalid slot");
                MIR_ASSERT(state, storage.asValue().isWritable(), "Writing to read-only slot");
                if (len > 0) {
                    storage.asValue().write_bytes(ofs, data, len);
                }
            }

            uint8_t* extWriteBytes(const MIR::TypeResolve& state, size_t len) {
                MIR_ASSERT(state, storage, "Writing to invalid slot");
                MIR_ASSERT(state, storage.asValue().isWritable(), "Writing to read-only slot");
                if (len > 0) {
                    return storage.asValue().extWriteBytes(ofs, len);
                } else {
                    static uint8_t emptyBuf;
                    return &emptyBuf;
                }
            }

            void write_byte(const MIR::TypeResolve& state, uint8_t v) {
                write_bytes(state, &v, 1);
            }

            void write_float(const MIR::TypeResolve& state, unsigned bits, FloatValue v) {
                switch (bits) {
                    case 16: {
                        F16 v_f = static_cast<float>(v);
                        write_bytes(state, &v_f, sizeof(v_f));
                    } break;
                    case 32: {
                        float v_f32 = static_cast<float>(v);
                        write_bytes(state, &v_f32, sizeof(v_f32));
                    } break;
                    case 64: {
                        double v_f64 = static_cast<double>(v);
                        write_bytes(state, &v_f64, sizeof(v_f64));
                    } break;
                    case 128: {
                        F128 v_f128 = v;
                        write_bytes(state, &v_f128, 16);
                    } break;
                    default:
                        MIR_BUG(state, "Unexpected float size (write): " << bits);
                }
            }

            void write_uint(const MIR::TypeResolve& state, unsigned bits, uint64_t v) {
                assert(bits <= 64);
                write_uint(state, bits, U128(v));
            }

            void write_uint(const MIR::TypeResolve& state, unsigned bits, U128 v) {
                auto nBytes = (bits + 7) / 8;
                if (TargetGetCurSpec().arch.bigEndian) {
                    v.toBeBytes(extWriteBytes(state, nBytes), nBytes);
                } else {
                    v.toLeBytes(extWriteBytes(state, nBytes), nBytes);
                }
            }

            void write_sint(const MIR::TypeResolve& state, unsigned bits, S128 v) {
                auto nBytes = (bits + 7) / 8;
                if (TargetGetCurSpec().arch.bigEndian) {
                    v.getInner().toBeBytes(extWriteBytes(state, nBytes), nBytes);
                } else {
                    v.getInner().toLeBytes(extWriteBytes(state, nBytes), nBytes);
                }
            }

            void write_ptr(const MIR::TypeResolve& state, uint64_t val, RelocPtr reloc) {
                write_uint(state, TargetGetPointerBits(), U128(val));
                storage.asValue().setReloc(ofs, std::move(reloc));
            }

            void setReloc(RelocPtr reloc) {
                storage.asValue().setReloc(ofs, std::move(reloc));
            }

            const uint8_t* extReadBytes(const MIR::TypeResolve& state, size_t len) const {
                MIR_ASSERT(state, storage, "");
                MIR_ASSERT(state, len >= 1, "");
                const auto* src = storage.asValue().getBytes(ofs, len, /*check_mask*/ true);
                MIR_ASSERT(state, src, "Invalid read: " << ofs << "+" << len << " (in " << *this << ")");
                return src;
            }

            void readBytes(const MIR::TypeResolve& state, void* data, size_t len) const {
                const auto* src = extReadBytes(state, len);
                assert(src);
                memcpy(data, src, len);
            }

            FloatValue readFloat(const ::MIR::TypeResolve& state, unsigned bits) const {
                switch (bits) {
                    case 16: {
                        F16 v_f16;
                        readBytes(state, &v_f16, sizeof(v_f16));
                        return FloatValue(static_cast<float>(v_f16));
                    } break;
                    case 32: {
                        float v_f32 = 0;
                        readBytes(state, &v_f32, sizeof(v_f32));
                        return v_f32;
                    } break;
                    case 64: {
                        double v_f64 = 0;
                        readBytes(state, &v_f64, sizeof(v_f64));
                        return v_f64;
                    } break;
                    case 128: {
                        F128 v_f;
                        readBytes(state, &v_f, sizeof(v_f));
                        return v_f;
                    } break;
                    default:
                        MIR_BUG(state, "Unexpected float size: " << bits);
                }
            }

            U128 readUint(const ::MIR::TypeResolve& state, unsigned bits) const {
                assert(bits <= 128);
                auto nBytes = (bits + 7) / 8;
                U128 rv;
                if (TargetGetCurSpec().arch.bigEndian) {
                    rv.fromBeBytes(extReadBytes(state, nBytes), nBytes);
                } else {
                    rv.fromLeBytes(extReadBytes(state, nBytes), nBytes);
                }
                return rv;
            }

            S128 readSint(const ::MIR::TypeResolve& state, unsigned bits) const {
                auto nBytes = (bits + 7) / 8;
                S128 rv;
                if (TargetGetCurSpec().arch.bigEndian) {
                    rv.fromBeBytes(extReadBytes(state, nBytes), nBytes);
                } else {
                    rv.fromLeBytes(extReadBytes(state, nBytes), nBytes);
                }
                return rv;
            }

            uint64_t readUsize(const ::MIR::TypeResolve& state) const {
                return readUint(state, TargetGetPointerBits()).truncateU64();
            }

            std::pair<uint64_t, RelocPtr> readPtr(const ::MIR::TypeResolve& state) const {
                return std::make_pair(readUsize(state), storage.asValue().getReloc(ofs));
            }

            friend std::ostream& operator<<(std::ostream& os, const ValueRef& vr);
        };

        std::ostream& operator<<(std::ostream& os, const ValueRef& vr) {
            if (!vr.storage) {
                os << "ValueRef(null)";
            } else {
                os << "ValueRef({" << vr.ofs << "+" << vr.len << "}";
                vr.storage.asValue().fmt(os, vr.ofs, vr.len);
                os << ")";
            }
            return os;
        }

        std::ostream& operator<<(std::ostream& os, const AllocationPtr& ap) {
            os << ValueRef(ap);
            return os;
        }

        // ---
        ConstantPtr ConstantPtr::allocate(stl::ObjPool* pool, const void* data, size_t len) {
            ConstantPtr rv;
            rv.ptr = pool->make<Constant>(data, len);
            return rv;
        }

        // ---
        AllocationPtr AllocationPtr::allocate(stl::ObjPool* pool, const StaticTraitResolve& resolve, const ::MIR::TypeResolve& state, const ::HIR::TypeData* ty) {
            size_t len;
            if (!TargetGetSizeOf(Span(), resolve, ty, len)) {
                throw Defer();
            }
            auto* data = static_cast<uint8_t*>(pool->allocate(len + ((len + 7) / 8)));
            AllocationPtr rv;
            // TODO: Include the current location from `state` in the allocation header
            rv.ptr = pool->make<Allocation>(data, len, ty);
            return rv;
        }

        AllocationPtr AllocationPtr::allocateRo(stl::ObjPool* pool, const void* dataIn, size_t len) {
            auto* data = static_cast<uint8_t*>(pool->allocate(len + ((len + 7) / 8)));
            AllocationPtr rv;
            rv.ptr = pool->make<Allocation>(data, len, HIR::TypeRef());
            rv->write_bytes(0, dataIn, len);
            rv->isReadonly = true;
            return rv;
        }

        // ---
        StaticRefPtr StaticRefPtr::allocate(stl::ObjPool* pool, ::HIR::Path p, const EncodedLiteral* lit) {
            StaticRefPtr rv;
            rv.ptr = pool->make<StaticRef>(pool, std::move(p), lit);
            return rv;
        }

        // --- RelocPtr ---
        IValue* RelocPtr::asValuePtr() const {
            assert(ptr);
            switch (ptr & 3) {
                case TAG_Allocation:
                    assert(asAllocation());
                    return asAllocation();
                case TAG_Constant:
                    assert(asConstant());
                    return asConstant();
                case TAG_StaticRef:
                    assert(asStaticref());
                    return asStaticref();
                case 3:
                    assert(!"Unexpected tag 3");
            }
            abort();
        }
    }
} // namespace MIR::eval

namespace {
    /// Get the offset for a given field path
    size_t getOffset(const Span& sp, const StaticTraitResolve& resolve, const TypeRepr* r, const TypeRepr::FieldPath& outPath) {
        assert(outPath.index < r->fields.size());
        size_t ofs = r->fields[outPath.index].offset;

        const auto* ty = &r->fields[outPath.index].ty;
        for (const auto& f : outPath.subFields) {
            if (f == TypeRepr::FieldPath::ARRAY_ELEMENT) {
                const auto* array = (*ty)->opt_Array();
                if (!array || !array->size.is_Known() || array->size.as_Known() == 0) {
                    throw Defer();
                }
                ty = &array->inner;
                continue;
            }
            r = TargetGetTypeRepr(sp, resolve, *ty);
            if (!r) {
                throw Defer();
            }
            assert(f < r->fields.size());
            ofs += r->fields[f].offset;
            ty = &r->fields[f].ty;
        }

        return ofs;
    }

    EntPtr getEntFullpath(const Span& sp, const ::StaticTraitResolve& resolve, const ::HIR::Path& path, EntNS ns, MonomorphState& outMs, const ::HIR::GenericParams** outImplParamsDef = nullptr) {
        if (const auto* gp = path.mData.opt_Generic()) {
            const auto& name = gp->mPath.components().back();
            const auto* mod = (gp->mPath.components().size() > 1) ? resolve.crate.getTypeitemByPath(sp, gp->mPath, false, /*ignore_last*/ true).opt_Module() : &resolve.crate.getModByPath(sp, gp->mPath, true);
            if (mod) {
                // TODO: This pointer will be invalidated...
                for (const auto& is : mod->inlineStatics) {
                    if (is.first == name) {
                        return &*is.second;
                    }
                }
            }
        }
        auto v = resolve.getValue(sp, path, outMs, false, outImplParamsDef);
        TU_MATCH_HDRA( (v), { )
        TU_ARMA(NotFound, e)
            return EntPtr();
            TU_ARMA(NotYetKnown, e)
            return EntPtr();
            //TODO(sp, "Handle NotYetKnown - " << path);
            TU_ARMA(Constant, e)
            return e;
            TU_ARMA(Static, e)
            return e;
            TU_ARMA(Function, e)
            return e;
            TU_ARMA(EnumConstructor, e) {
                return EntPtr::Data_Enum{e.e, e.v};
            }
            TU_ARMA(EnumValue, e)
            TODO(sp, "Handle EnumValue - " << path);
            TU_ARMA(StructConstructor, e) {
                return e.s;
            }
            TU_ARMA(StructConstant, e)
            TODO(sp, "Handle StructConstant - " << path);
        }
        throw "";
    }

    struct TypeInfo {
        enum {
            Other,
            Float,
            Signed,
            Unsigned,
        } ty;

        unsigned bits;

        static TypeInfo forPrimitive(::HIR::CoreType te) {
            switch (te) {
                case ::HIR::CoreType::I8:
                    return TypeInfo{Signed, 8};
                case ::HIR::CoreType::U8:
                    return TypeInfo{Unsigned, 8};
                case ::HIR::CoreType::I16:
                    return TypeInfo{Signed, 16};
                case ::HIR::CoreType::U16:
                    return TypeInfo{Unsigned, 16};
                case ::HIR::CoreType::I32:
                    return TypeInfo{Signed, 32};
                case ::HIR::CoreType::U32:
                    return TypeInfo{Unsigned, 32};
                case ::HIR::CoreType::I64:
                    return TypeInfo{Signed, 64};
                case ::HIR::CoreType::U64:
                    return TypeInfo{Unsigned, 64};
                case ::HIR::CoreType::I128:
                    return TypeInfo{Signed, 128};
                case ::HIR::CoreType::U128:
                    return TypeInfo{Unsigned, 128};

                case ::HIR::CoreType::Isize:
                    return TypeInfo{Signed, TargetGetPointerBits()};
                case ::HIR::CoreType::Usize:
                    return TypeInfo{Unsigned, TargetGetPointerBits()};
                case ::HIR::CoreType::Char:
                    return TypeInfo{Unsigned, 21};
                case ::HIR::CoreType::Bool:
                    return TypeInfo{Unsigned, 1};

                case ::HIR::CoreType::F16:
                    return TypeInfo{Float, 16};
                case ::HIR::CoreType::F32:
                    return TypeInfo{Float, 32};
                case ::HIR::CoreType::F64:
                    return TypeInfo{Float, 64};
                case ::HIR::CoreType::F128:
                    return TypeInfo{Float, 128};

                case ::HIR::CoreType::Str:
                    return TypeInfo{Other, 0};
            }
            return TypeInfo{Other, 0};
        }

        static TypeInfo forType(const ::HIR::TypeData* ty) {
            if (!ty->is_Primitive()) {
                return TypeInfo{Other, 0};
            }
            return forPrimitive(ty->as_Primitive());
        }

        U128 mask(U128 v) const {
            if (bits < 64) {
                uint64_t maskVal = (static_cast<uint64_t>(1ull) << bits) - 1;
                assert(maskVal != 0);
                return U128(v.getLo() & maskVal);
            } else if (bits == 64) {
                return U128(v.getLo());
            } else if (bits < 128) {
                U128 maskVal = (U128(1) << bits) - 1u;
                assert(maskVal != 0);
                return U128(v & maskVal);
            } else if (bits == 128) {
                return v;
            } else {
                throw "";
            }
        }

        U128 mask(S128 v) const {
            if (v < 0) {
                // Negate, mask, and re-negate
                return (-S128(mask((-v).getInner()))).getInner();
            } else {
                return mask((v).getInner());
            }
        }

        double mask(double v) const {
            return v;
        }
    };

    const unsigned TERM_RET_PUSHED = UINT_MAX - 1;
    const unsigned TERM_RET_RETURN = UINT_MAX;
} // namespace <anon>

namespace MIR {
    namespace eval {

        class CallStackEntry {
        public:
            stl::ObjPool* const value_pool;
            const unsigned frameIndex;
            const std::vector<std::pair<HIR::Pattern, HIR::TypeRef>> argDefs;
            const HIR::TypeRef ret_type;

            // MIR Resolve Helper
            const StaticTraitResolve& rootResolve;
            StaticTraitResolve resolve;
            ::MIR::TypeResolve state;
            // Monomorphiser from the function
            MonomorphState ms;

            ::MIR::eval::AllocationPtr retval;

            ::std::vector<::MIR::eval::AllocationPtr> args;

            ::std::vector<HIR::TypeRef> localTypes;
            ::std::vector<::MIR::eval::AllocationPtr> locals;
            ::std::vector<bool> dropFlags;

            // ---
            CallStackEntry(const CallStackEntry&) = delete;
            CallStackEntry(CallStackEntry&&) = delete;

            CallStackEntry(
                stl::ObjPool* value_pool,
                unsigned frameIndex,
                const Span& root_span,
                const StaticTraitResolve& resolve,
                ::FmtLambda pathStr,
                // Pre-monomorphised function signature (as this may be a `static`)
                HIR::TypeRef expTy,
                std::vector<std::pair<HIR::Pattern, HIR::TypeRef>> argDefs,
                // Function/Body code
                const MIR::Function& fcn,
                // Monomorphisation rules
                MonomorphState ms,
                ::std::vector<AllocationPtr> args,
                const ::HIR::GenericParams* itemParamsDef,
                const ::HIR::GenericParams* implParamsDef
            )
                : value_pool(value_pool)
                , frameIndex(frameIndex)
                , argDefs(std::move(argDefs))
                , ret_type(std::move(expTy))
                , rootResolve(resolve)
                , resolve(resolve.crate)
                , state{root_span, this->resolve, std::move(pathStr), this->ret_type, this->argDefs, fcn}
                , ms(std::move(ms))
                , retval(AllocationPtr::allocate(value_pool, rootResolve, state, ret_type))
                , args(args)
                , dropFlags(fcn.dropFlags)
            {
                this->resolve.setBothGenericsRaw(implParamsDef, itemParamsDef);
                localTypes.reserve(state.fcn.locals.size());
                locals.reserve(state.fcn.locals.size());
                for (size_t i = 0; i < state.fcn.locals.size(); i++) {
                    localTypes.push_back(state.mResolve.monomorphExpand(state.sp, state.fcn.locals[i], this->ms));
                    locals.push_back(AllocationPtr::allocate(value_pool, rootResolve, state, localTypes.back()));
                }

                state.monomorphedRettype = ret_type;
                state.monomorphedLocals = &localTypes;
            }

            HIR::TypeRef monomorphExpand(const HIR::TypeData* ty) const {
                return this->resolve.monomorphExpand(this->state.sp, ty, this->ms);
            }

            unsigned readEnumVariant(const HIR::TypeData* ty, ValueRef value) const {
                auto* repr = TargetGetTypeRepr(state.sp, rootResolve, ty);
                MIR_ASSERT(state, repr, "No representation for enum " << ty);

                unsigned variant = 0;
                TU_MATCH_HDRA( (repr->variants), { )
                TU_ARMA(None, ve) {
                    }
                    TU_ARMA(Linear, ve) {
                        auto tag = value.slice(repr->getOffset(state.sp, rootResolve, ve.field), ve.field.size).readUint(state, 8 * ve.field.size);
                        variant = tag < U128(ve.offset) ? ve.field.index : (tag - U128(ve.offset)).truncateU64();
                    }
                    TU_ARMA(Values, ve) {
                        auto tag = value.slice(repr->getOffset(state.sp, rootResolve, ve.field), ve.field.size).readUint(state, 8 * ve.field.size).truncateU64();
                        auto it = std::find(ve.values.begin(), ve.values.end(), tag);
                        MIR_ASSERT(state, it != ve.values.end(), "Invalid enum tag " << tag << " for " << ty);
                        variant = it - ve.values.begin();
                    }
                    TU_ARMA(NonZero, ve) {
                        size_t offset = repr->getOffset(state.sp, rootResolve, ve.field);
                        bool isNonzero = false;
                        for (size_t i = 0; i < ve.field.size; i++) {
                            if (value.slice(offset + i, 1).readUint(state, 8) != U128(0)) {
                                isNonzero = true;
                                break;
                            }
                        }
                        variant = isNonzero ? 1 - ve.zero_variant : ve.zero_variant;
                    }
                }
                return variant;
            }

            static bool allocationReachableFrom(const Allocation* allocation, const Allocation* target, ::std::set<const Allocation*>& visited) {
                if (allocation == target) {
                    return true;
                }
                if (!visited.insert(allocation).second) {
                    return false;
                }
                for (const auto& relocation : allocation->getRelocations()) {
                    if (const auto* child = relocation.ptr.asAllocation()) {
                        if (allocationReachableFrom(child, target, visited)) {
                            return true;
                        }
                    }
                }
                return false;
            }

            bool value_reachable_from_return(ValueRef value) const {
                const auto* target = value.getStorage().asAllocation();
                if (!target) {
                    return false;
                }
                ::std::set<const Allocation*> visited;
                return allocationReachableFrom(retval.operator->(), target, visited);
            }

            bool value_needs_non_const_drop(const HIR::TypeData* ty, ValueRef value) const {
                if (!rootResolve.type_needs_drop_glue(state.sp, ty)) {
                    return false;
                }

                TU_MATCH_HDRA( (*ty), { )
                TU_ARMA(Diverge, te) {
                        return false;
                    }
                    TU_ARMA(Infer, te) {
                        return true;
                    }
                    TU_ARMA(ErasedType, te) {
                        return true;
                    }
                    TU_ARMA(NodeType, te) {
                        auto* repr = TargetGetTypeRepr(state.sp, rootResolve, ty);
                        MIR_ASSERT(state, repr, "No representation for " << ty);
                        for (const auto& field : repr->fields) {
                            auto size = sizeOfOrBug(field.ty);
                            if (value_needs_non_const_drop(field.ty, value.slice(field.offset, size))) {
                                return true;
                            }
                        }
                        return false;
                    }
                    TU_ARMA(Generic, te) {
                        return true;
                    }
                    TU_ARMA(Primitive, te) {
                        return false;
                    }
                    TU_ARMA(Pointer, te) {
                        return false;
                    }
                    TU_ARMA(NamedFunction, te) {
                        return false;
                    }
                    TU_ARMA(Function, te) {
                        return false;
                    }
                    TU_ARMA(Borrow, te) {
                        if (te.type != HIR::BorrowType::Owned) {
                            return false;
                        }
                        auto pointer = value.readPtr(state);
                        MIR_ASSERT(state, pointer.first >= EncodedLiteral::PTR_BASE, "Invalid owned pointer while checking a constant drop");
                        auto size = sizeOfOrBug(te.inner);
                        auto inner = ValueRef(pointer.second, pointer.first - EncodedLiteral::PTR_BASE).slice(0, size);
                        return value_needs_non_const_drop(te.inner, inner);
                    }
                    TU_ARMA(Path, te) {
                        const auto* markings = te.binding.getTraitMarkings();
                        if (!markings) {
                            return true;
                        }
                        if (markings->hasDropImpl) {
                            return true;
                        }

                        TU_MATCH_HDRA( (te.binding), { )
                        TU_ARMA(Unbound, pbe) {
                                return true;
                            }
                            TU_ARMA(Opaque, pbe) {
                                return true;
                            }
                            TU_ARMA(ExternType, pbe) {
                                return false;
                            }
                            TU_ARMA(Union, pbe) {
                                return false;
                            }
                            TU_ARMA(Struct, pbe) {
                                auto* repr = TargetGetTypeRepr(state.sp, rootResolve, ty);
                                MIR_ASSERT(state, repr, "No representation for struct " << ty);
                                for (const auto& field : repr->fields) {
                                    auto size = sizeOfOrBug(field.ty);
                                    if (value_needs_non_const_drop(field.ty, value.slice(field.offset, size))) {
                                        return true;
                                    }
                                }
                                return false;
                            }
                            TU_ARMA(Enum, pbe) {
                                const auto* variants = pbe->mData.opt_Data();
                                if (!variants) {
                                    return false;
                                }
                                auto variant = readEnumVariant(ty, value);
                                MIR_ASSERT(state, variant < variants->size(), "Enum variant " << variant << " out of range for " << ty);

                                auto* repr = TargetGetTypeRepr(state.sp, rootResolve, ty);
                                MIR_ASSERT(state, repr, "No representation for enum " << ty);
                                MIR_ASSERT(state, variant < repr->fields.size(), "Enum representation has no variant " << variant << " for " << ty);
                                const auto& field = repr->fields[variant];
                                auto size = sizeOfOrBug(field.ty);
                                return value_needs_non_const_drop(field.ty, value.slice(field.offset, size));
                            }
                        }
                        throw std::runtime_error("Unreachable path binding");
                    }
                    TU_ARMA(Array, te) {
                        auto count = te.size.as_Known();
                        if (count == 0) {
                            return false;
                        }
                        auto size = sizeOfOrBug(te.inner);
                        for (size_t i = 0; i < count; i++) {
                            if (value_needs_non_const_drop(te.inner, value.slice(i * size, size))) {
                                return true;
                            }
                        }
                        return false;
                    }
                    TU_ARMA(Slice, te) {
                        return true;
                    }
                    TU_ARMA(TraitObject, te) {
                        return true;
                    }
                    TU_ARMA(Tuple, te) {
                        auto* repr = TargetGetTypeRepr(state.sp, rootResolve, ty);
                        MIR_ASSERT(state, repr, "No representation for tuple " << ty);
                        for (const auto& field : repr->fields) {
                            auto size = sizeOfOrBug(field.ty);
                            if (value_needs_non_const_drop(field.ty, value.slice(field.offset, size))) {
                                return true;
                            }
                        }
                        return false;
                    }
                }
                throw std::runtime_error("Unreachable type while checking a constant drop");
            }

            StaticRefPtr getStaticrefMono(const ::HIR::Path& p, HIR::TypeRef* outTy = nullptr) const {
                // NOTE: Value won't need to be monomorphed, as it shouldn't be generic
                return getStaticref(ms.monomorphPath(state.sp, p), outTy);
            }

            StaticRefPtr getStaticref(::HIR::Path p, HIR::TypeRef* outTy = nullptr) const {
                // If there's any mention of generics in this path, then return Literal::Defer
                if (visit_path_tys_with(p, [&](const auto& ty) -> bool {
                    return ty->is_Generic();
                })) {
                    DEBUG("Return Literal::Defer for constastatic " << p << " which references a generic parameter");
                    throw Defer();
                }
                MonomorphState constMs(rootResolve.crate.types);

                const HIR::GenericParams* implParamsDef = nullptr;
                auto ent = getEntFullpath(state.sp, rootResolve, p, EntNS::Value, constMs, &implParamsDef);
                if (ent.is_Static()) {
                    const auto& s = *ent.as_Static();

                    if (!s.valueGenerated) {
                        // If there's no MIR and no HIR then this is an external static (which can only be borrowed)
                        if (!s.mValue && !s.mValue.mir) {
                            DEBUG("No value and no mir");
                            return StaticRefPtr::allocate(value_pool, std::move(p), nullptr);
                        }

                        auto& item = const_cast<::HIR::Static&>(s);

                        static ::std::set<::HIR::Static*> sNonRecurse;
                        if (sNonRecurse.count(&item) == 0) {
                            sNonRecurse.insert(&item);
                            ConvertHIRConstantEvaluateStatic(resolve.crate, implParamsDef, p, item);
                            sNonRecurse.erase(sNonRecurse.find(&item));
                        } else {
                            DEBUG("Recursion detected");
                        }
                    }

                    if (!s.valueGenerated) {
                        auto& item = const_cast<::HIR::Static&>(s);

                        // Challenge: Adding items to the module might invalidate an iterator.
                        ::HIR::ItemPath modIp{item.mValue.state->modPath};
                        auto nvs = NewvalState(item.mValue.state->mModule, modIp, FMT("static" << &item << "#"));
                        auto eval = ::HIR::Evaluator(item.mValue.span(), rootResolve.crate, nvs);
                        DEBUG("- Evaluate " << p);
                        try {
                            item.valueGenerated = true;
                            item.valueRes = eval.evaluateConstant(::HIR::ItemPath(p), item.mValue, item.mType, std::move(constMs));
                            item.valueGenerated = true;
                        } catch (const Defer&) {
                            MIR_BUG(state, p << " Defer during value generation");
                        }
                        DEBUG(p << " = " << item.valueRes);
                    }
                    if (outTy) {
                        // Does this need monomorph? No, becuase the value is known and thus not generic?
                        *outTy = s.mType;
                    }
                    return StaticRefPtr::allocate(value_pool, std::move(p), &s.valueRes);
                } else {
                    DEBUG(ent.tagStr() << " " << p);
                    if (outTy) {
                        MIR_TODO(state, "Get type for " << ent.tagStr() << " (" << p << ")");
                    }
                    return StaticRefPtr::allocate(value_pool, std::move(p), nullptr);
                }
            }

            ValueRef getLval(const ::MIR::LValue& lv, ValueRef* meta = nullptr) {
                ::HIR::TypeRef tmpTy;
                const ::HIR::TypeData* typ = nullptr;
                ValueRef metadata;
                ValueRef val;
                //TRACE_FUNCTION_FR(lv, val);
            TU_MATCH_HDRA( (lv.root), {)
            TU_ARMA(Return, e) {
                        typ = ret_type;
                        val = ValueRef(retval);
                    }
                    TU_ARMA(Local, e) {
                        MIR_ASSERT(state, e < locals.size(), "Local index out of range - " << e << " >= " << locals.size());
                        typ = localTypes[e];
                        val = ValueRef(locals[e]);
                    }
                    TU_ARMA(Argument, e) {
                        MIR_ASSERT(state, e < args.size(), "Argument index out of range - " << e << " >= " << args.size());
                        typ = state.mArgs[e].second;
                        val = ValueRef(args[e]);
                    }
                    TU_ARMA(Static, e) {
                        val = ValueRef(getStaticrefMono(e, lv.wrappers.empty() ? nullptr : &tmpTy));
                        if (!lv.wrappers.empty()) {
                            MIR_ASSERT(state, tmpTy != HIR::TypeRef(), "Type not set?");
                        }
                        typ = tmpTy;
                    }
            }

            for(const auto& w : lv.wrappers)
            {
                    MIR_ASSERT(state, typ, "Type not set when unwrapping - " << lv);
                    DEBUG(w << " " << val << ": " << typ);
                TU_MATCH_HDRA( (w), {)
                TU_ARMA(Field, e) {
                            if (typ->is_Slice() || typ->is_Array()) {
                                // Check the inner type
                                size_t size;
                                if (const auto* te = typ->opt_Array()) {
                                    typ = te->inner;
                                    size = te->size.as_Known();
                                } else if (const auto* te = typ->opt_Slice()) {
                                    typ = te->inner;
                                    // Get metadata
                                    size = metadata.readUsize(state);
                                } else {
                                    throw "";
                                }
                                metadata = ValueRef();
                                size_t sz, al;
                                if (!TargetGetSizeAndAlignOf(state.sp, rootResolve, typ, sz, al)) {
                                    throw Defer();
                                }
                                MIR_ASSERT(state, sz < SIZE_MAX, "Unsized type on index output - " << typ);
                                size_t index = e;
                                // HACK: Allow one-past-end for `[foo, ref bar @ ...]` support
                                if (index == size) {
                                    val = val.slice(index * sz, 0);
                                } else {
                                    MIR_ASSERT(state, index < size, "LValue::Index index out of range - " << index << " >= " << size);
                                    val = val.slice(index * sz, sz);
                                }
                                continue;
                            }
                            auto* repr = TargetGetTypeRepr(state.sp, this->rootResolve, typ);
                            MIR_ASSERT(state, repr, "No repr for " << typ);
                            MIR_ASSERT(state, e < repr->fields.size(), "LValue::Field index out of range");
                            if (repr->size != SIZE_MAX) {
                                metadata = ValueRef();
                            }
                            auto ofs = repr->fields[e].offset;
                            typ = repr->fields[e].ty;

                            size_t sz, al;
                            if (!TargetGetSizeAndAlignOf(state.sp, rootResolve, typ, sz, al)) {
                                throw Defer();
                            }
                            if (sz == SIZE_MAX) {
                                val = val.slice(ofs);
                            } else {
                                val = val.slice(ofs, sz);
                            }
                        }
                        TU_ARMA(Deref, e) {
                            //
                            if (const auto* te = typ->opt_Pointer()) {
                                typ = te->inner;
                            } else if (const auto* te = typ->opt_Borrow()) {
                                typ = te->inner;
                            } else {
                                MIR_BUG(state, "Deref of unsupported type - " << typ);
                            }
                            // If the inner type is unsized
                            size_t sz, al;
                            if (!TargetGetSizeAndAlignOf(state.sp, rootResolve, typ, sz, al)) {
                                throw Defer();
                            }
                            if (sz == SIZE_MAX) {
                                // Read metadata
                                DEBUG("Reading metadata");
                                metadata = val.slice(TargetGetPointerBits() / 8);
                            }
                            auto p = val.readPtr(state);
                            MIR_ASSERT(state, p.first >= EncodedLiteral::PTR_BASE, "Null (<PTR_BASE) pointer deref");
                            MIR_ASSERT(state, p.first % al == 0, "Unaligned pointer deref");
                            DEBUG("> " << ValueRef(p.second) << " - o=" << (p.first - EncodedLiteral::PTR_BASE) << " sz=" << sz << " " << typ);
                            // TODO: Determine size using metadata?
                            if (sz == SIZE_MAX) {
                                val = ValueRef(p.second, p.first - EncodedLiteral::PTR_BASE);
                            } else {
                                val = ValueRef(p.second, p.first - EncodedLiteral::PTR_BASE).slice(0, sz);
                            }
                        }
                        TU_ARMA(Index, e) {
                            // Check the inner type
                            size_t size;
                            if (const auto* te = typ->opt_Array()) {
                                typ = te->inner;
                                size = te->size.as_Known();
                            } else if (const auto* te = typ->opt_Slice()) {
                                typ = te->inner;
                                // Get metadata
                                size = metadata.readUsize(state);
                            } else {
                                MIR_BUG(state, "Index of unsupported type - " << typ);
                            }
                            metadata = ValueRef();
                            size_t sz, al;
                            if (!TargetGetSizeAndAlignOf(state.sp, rootResolve, typ, sz, al)) {
                                throw Defer();
                            }
                            MIR_ASSERT(state, sz < SIZE_MAX, "Unsized type on index output - " << typ);
                            MIR_ASSERT(state, e < locals.size(), "LValue::Index index local out of range");
                            size_t index = ValueRef(locals[e]).readUsize(state);
                            MIR_ASSERT(state, index < size, "LValue::Index index out of range - " << index << " >= " << size);
                            val = val.slice(index * sz, sz);
                        }
                        TU_ARMA(Downcast, e) {
                            auto* repr = TargetGetTypeRepr(state.sp, this->rootResolve, typ);
                            MIR_ASSERT(state, repr, "No repr for " << typ);
                            MIR_ASSERT(state, e < repr->fields.size(), "LValue::Downcast index out of range");
                            if (repr->size != SIZE_MAX) {
                                metadata = ValueRef();
                            }
                            typ = repr->fields[e].ty;
                            val = val.slice(repr->fields[e].offset, sizeOfOrBug(typ));
                        }
                }
            }
            if(meta)
                *meta = std::move(metadata);
            return val;
            }

            const EncodedLiteral& getConst(const ::HIR::Path& inP, ::HIR::TypeRef* outTy) const {
                auto p = ms.monomorphPath(state.sp, inP);
                rootResolve.expandAssociatedTypesPath(state.sp, p);
                // If there's any mention of generics in this path, then return Literal::Defer
                if (visit_path_tys_with(p, [&](const auto& ty) -> bool {
                    return ty->is_Generic();
                })) {
                    DEBUG("Return Literal::Defer for constant " << p << " which references a generic parameter");
                    throw Defer();
                }
                MonomorphState constMs(rootResolve.crate.types);
                const HIR::GenericParams* implParamsDef = nullptr;
                auto ent = getEntFullpath(state.sp, rootResolve, p, EntNS::Value, constMs, &implParamsDef);
                MIR_ASSERT(state, ent.is_Constant(), "MIR Constant::Const(" << p << ") didn't point to a Constant - " << ent.tagStr());
                const auto& c = *ent.as_Constant();
                if (c.valueState == HIR::Constant::ValueState::Unknown) {
                    auto& item = const_cast<::HIR::Constant&>(c);
                    // Challenge: Adding items to the module might invalidate an iterator.
                    ::HIR::ItemPath modIp{item.mValue.state->modPath};
                    auto nvs = NewvalState(item.mValue.state->mModule, modIp, FMT("const" << &c << "#"));
                    auto eval = ::HIR::Evaluator(item.mValue.span(), rootResolve.crate, nvs);
                    eval.resolve.setBothGenericsRaw(implParamsDef, &c.mParams);
                    auto tempPpImpl = implParamsDef ? implParamsDef->makeNopParams(rootResolve.crate.types, 0) : HIR::PathParams();
                    auto tempPpMethod = c.mParams.makeNopParams(rootResolve.crate.types, 1);
                    MonomorphState tempMs(rootResolve.crate.types);
                    tempMs.ppImpl = &tempPpImpl;
                    tempMs.ppMethod = &tempPpMethod;
                    DEBUG("- Evaluate " << p);
                    try {
                        item.valueRes = eval.evaluateConstant(::HIR::ItemPath(p), item.mValue, item.mType, std::move(tempMs));
                        item.valueState = HIR::Constant::ValueState::Known;
                    } catch (const Defer&) {
                        item.valueState = HIR::Constant::ValueState::Generic;
                    }
                }
                if (outTy) {
                    *outTy = constMs.monomorphType(state.sp, c.mType);
                }
                if (c.valueState == HIR::Constant::ValueState::Generic) {
                    auto it = c.monomorphCache.find(p);
                    if (it == c.monomorphCache.end()) {
                        auto& item = const_cast<::HIR::Constant&>(c);
                        // Challenge: Adding items to the module might invalidate an iterator.
                        ::HIR::ItemPath modIp{item.mValue.state->modPath};
                        auto nvs = NewvalState(item.mValue.state->mModule, modIp, FMT("const" << &c << "#"));
                        auto eval = ::HIR::Evaluator(item.mValue.span(), rootResolve.crate, nvs);
                        eval.resolve.setBothGenericsRaw(implParamsDef, &c.mParams);

                        DEBUG("- Evaluate monomorphed " << p);
                        DEBUG("> const_ms=" << constMs);
                        auto ty = constMs.monomorphType(item.mValue.span(), item.mType);
                        auto val = eval.evaluateConstant(::HIR::ItemPath(p), item.mValue, std::move(ty), std::move(constMs));

                        auto insertRes = item.monomorphCache.insert(std::make_pair(p.clone(), std::move(val)));
                        it = insertRes.first;
                    } else {
                        DEBUG("Cached generic " << p);
                    }

                    return it->second;
                } else {
                    return c.valueRes;
                }
            }

            void write_encoded(ValueRef dst, const EncodedLiteral& encoded) {
                // Write the encoded value into the destination
                dst.write_bytes(state, encoded.bytes.data(), encoded.bytes.size());
                for (const auto& r : encoded.relocations) {
                    RelocPtr reloc;
                    if (r.p) {
                        reloc = RelocPtr(getStaticref(r.p->clone()));
                    } else {
                        reloc = RelocPtr(AllocationPtr::allocateRo(value_pool, r.bytes.data(), r.bytes.size()));
                    }
                    dst.slice(r.ofs, r.len).setReloc(std::move(reloc));
                }
            }

            void write_const(ValueRef dst, const ::MIR::Constant& c) {
            TU_MATCH_HDR( (c), {)
            TU_ARM(c, Int, e2) {
                        dst.write_sint(state, dst.getLen() * 8, e2.v);
                    }
                    TU_ARM(c, Uint, e2) {
                        dst.write_uint(state, dst.getLen() * 8, e2.v);
                    }
                    TU_ARM(c, Float, e2) {
                        dst.write_float(state, dst.getLen() * 8, e2.v);
                    }
                    TU_ARM(c, Bool, e2) {
                        dst.write_uint(state, 1, e2.v);
                    }
                    TU_ARM(c, Bytes, e2) {
                        dst.write_ptr(state, EncodedLiteral::PTR_BASE, ConstantPtr::allocate(value_pool, e2.data(), e2.size()));
                    }
                    TU_ARM(c, StaticString, e2) {
                        dst.write_ptr(state, EncodedLiteral::PTR_BASE, ConstantPtr::allocate(value_pool, e2.data(), e2.size()));
                        dst.slice(TargetGetPointerBits() / 8).write_uint(state, TargetGetPointerBits(), e2.size());
                    }
                    TU_ARM(c, Const, e2) {
                        ::HIR::TypeRef ty;
                        assert(e2.p);
                        const auto& encoded = getConst(*e2.p, &ty);
                        DEBUG(*e2.p << " = " << encoded);

                        write_encoded(dst, encoded);
                    }
                    TU_ARM(c, Generic, e2) {
                        auto v = ms.getValue(state.sp, e2);
                TU_MATCH_HDRA( (v), { )
                default:
                    MIR_TODO(state, "Handle expanded generic: " << v);
                            TU_ARMA(Generic, _) {
                                throw Defer();
                            }
                            TU_ARMA(Evaluated, ve) {
                                DEBUG(e2 << " = " << *ve);
                                write_encoded(dst, *ve);
                            }
                }
                    }
                    TU_ARM(c, Function, e2) {
                    }
                    TU_ARM(c, ItemAddr, e2) {
                        assert(e2);
                        MIR_ASSERT(state, e2.offset.isU64(), "Item address offset is too large: " << e2.offset);
                        dst.write_ptr(state, EncodedLiteral::PTR_BASE + e2.offset.truncateU64(), getStaticrefMono(*e2));
                    }
            }
            }

            /// Write a borrow of the given lvalue
            void write_borrow(ValueRef dst, ::HIR::BorrowType bt, const ::MIR::LValue& lv) {
                ValueRef meta;
                auto val = this->getLval(lv, &meta);
                dst.write_ptr(state, EncodedLiteral::PTR_BASE + val.getOfs(), val.getStorage());
                if (meta.isValid()) {
                    auto ptrSize = TargetGetPointerBits() / 8;
                    dst.slice(ptrSize).copyFrom(state, meta);
                }
            }

            void write_param(ValueRef dst, const ::MIR::Param& p) {
            TU_MATCH_HDRA( (p), { )
            TU_ARMA(LValue, e)
                dst.copyFrom( state, this->getLval(e) );
                    TU_ARMA(Borrow, e)
                    write_borrow(dst, e.type, e.val);
                    TU_ARMA(Constant, e)
                    write_const(dst, e);
            }
            }

            const EncodedLiteral& getConst(const HIR::ConstGeneric& v, EncodedLiteral& tmp) const {
            TU_MATCH_HDRA( (v), {)
            TU_ARMA(Infer, ve) {
                        MIR_BUG(state, "Encountered Infer value in constant?");
                    }
                    TU_ARMA(Generic, ve) {
                        throw Defer{};
                    }
                    TU_ARMA(Unevaluated, ve) {
                        auto value = ve->monomorph(state.sp, ms, false);
                        const auto& expr = *value.expr;
                        MonomorphState value_ms(rootResolve.crate.types);
                        value_ms.ppImpl = &value.paramsImpl;
                        value_ms.ppMethod = &value.paramsItem;
                        auto type = value_ms.monomorphType(state.sp, expr->resType);
                        tmp = evaluateConstgeneric(state.sp, rootResolve.crate, type, value);
                        return tmp;
                    }
                    TU_ARMA(Evaluated, ve) {
                        return *ve;
                    }
            }
            throw "";
            }

            /// Read a floating point value from a MIR::Param
            FloatValue readParamFloat(unsigned bits, const ::MIR::Param& p) const {
            TU_MATCH_HDRA( (p), {)
            TU_ARMA(LValue, e)
                return const_cast<CallStackEntry*>(this)->getLval(e).readFloat(state, bits);
                    TU_ARMA(Borrow, e)
                    MIR_BUG(state, "Expected a float, got a MIR::Param::Borrow");
                    TU_ARMA(Constant, e) {
                        if (e.is_Const()) {
                            const auto& val = getConst(*e.as_Const().p, nullptr);
                            // TODO: Check the type from get_const
                            return EncodedLiteralSlice(val).readFloat();
                        }
                        if (e.is_Generic()) {
                            auto ve = ms.getValue(state.sp, e.as_Generic());
                            EncodedLiteral elTmp;
                            const auto& el = getConst(ve, elTmp);
                            return EncodedLiteralSlice(el).readFloat();
                        }
                        MIR_ASSERT(state, e.is_Float(), "Expected a float, got " << e);
                        return e.as_Float().v;
                    }
            }
            abort();
            }

            U128 readParamUint(unsigned bits, const ::MIR::Param& p) const {
            TU_MATCH_HDRA( (p), { )
            TU_ARMA(LValue, e)
                return const_cast<CallStackEntry*>(this)->getLval(e).readUint(state, bits);
                    TU_ARMA(Borrow, e)
                    MIR_BUG(state, "Expected an integer, got a MIR::Param::Borrow");
                    TU_ARMA(Constant, e) {
                        if (e.is_Const()) {
                            const auto& val = getConst(*e.as_Const().p, nullptr);
                            // TODO: Check the type from get_const
                            return EncodedLiteralSlice(val).readUint();
                        }
                        if (e.is_Generic()) {
                            auto ve = ms.getValue(state.sp, e.as_Generic());
                            EncodedLiteral elTmp;
                            const auto& el = getConst(ve, elTmp);
                            return EncodedLiteralSlice(el).readUint();
                        }
                        if (e.is_Int()) {
                            return e.as_Int().v.getInner();
                        }
                        if (e.is_Bool()) {
                            return U128(e.as_Bool().v ? 1 : 0);
                        }
                        MIR_ASSERT(state, e.is_Uint(), "Expected an integer, got " << e.tagStr() << " " << e);
                        return U128(e.as_Uint().v);
                    }
            }
            abort();
            }

            S128 readParamSint(unsigned bits, const ::MIR::Param& p) const {
            TU_MATCH_HDRA( (p), { )
            TU_ARMA(LValue, e)
                return const_cast<CallStackEntry*>(this)->getLval(e).readSint(state, bits);
                    TU_ARMA(Borrow, e)
                    MIR_BUG(state, "Expected an integer, got a MIR::Param::Borrow");
                    TU_ARMA(Constant, e) {
                        if (e.is_Const()) {
                            const auto& val = getConst(*e.as_Const().p, nullptr);
                            // TODO: Check the type from get_const
                            return EncodedLiteralSlice(val).readSint();
                        }
                        if (e.is_Generic()) {
                            auto ve = ms.getValue(state.sp, e.as_Generic());
                            EncodedLiteral elTmp;
                            const auto& el = getConst(ve, elTmp);
                            return EncodedLiteralSlice(el).readSint();
                        }
                        MIR_ASSERT(state, e.is_Int(), "Expected an integer, got " << e.tagStr() << " " << e);
                        return S128(e.as_Int().v);
                    }
            }
            abort();
            }

            std::pair<uint64_t, RelocPtr> readParamPtr(const ::MIR::Param& p) const {
            TU_MATCH_HDRA( (p), {)
            TU_ARMA(LValue, e) {
                        return const_cast<CallStackEntry*>(this)->getLval(e).readPtr(state);
                    }
                    TU_ARMA(Borrow, e) {
                        MIR_TODO(state, "read_param_ptr - " << p);
                    }
                    TU_ARMA(Constant, e) {
                        if (!e.is_ItemAddr()) {
                            MIR_BUG(state, "Invalid argument for pointer: " << p);
                        }
                        MIR_ASSERT(state, e.as_ItemAddr().offset.isU64(), "Item address offset is too large: " << e.as_ItemAddr().offset);
                        // TODO: Look up the static
                        return ::std::make_pair(EncodedLiteral::PTR_BASE + e.as_ItemAddr().offset.truncateU64(), RelocPtr(getStaticrefMono(*e.as_ItemAddr())));
                    }
            }
            abort();
            }

            size_t sizeOfOrBug(const ::HIR::TypeData* ty) const {
                size_t rv;
                if (!TargetGetSizeOf(state.sp, rootResolve, ty, /*out*/ rv)) {
                    MIR_BUG(state, "No size for " << ty);
                }
                return rv;
            }
        };

    }
} // namespace ::MIR::eval

namespace {
    ::std::pair<::MIR::eval::ValueRef, ::MIR::eval::ValueRef> getTupleTBool(const ::MIR::eval::CallStackEntry& localState, ::MIR::eval::ValueRef& src, const HIR::TypeData* t) {
        auto tuple_t = localState.rootResolve.crate.types.tuple({t, localState.rootResolve.crate.types.primitive(::HIR::CoreType::Bool)});
        auto* repr = TargetGetTypeRepr(localState.state.sp, localState.rootResolve, tuple_t);
        MIR_ASSERT(localState.state, repr, "No repr for " << tuple_t);
        auto s = localState.sizeOfOrBug(t);
        return std::make_pair(src.slice(repr->fields[0].offset, s), src.slice(repr->fields[1].offset, 1));
    }

    bool doArithChecked(
        ::MIR::eval::CallStackEntry& localState,
        const HIR::TypeData* ty,
        ::MIR::eval::ValueRef& dst,
        const ::MIR::Param& val_l,
        ::MIR::eBinOp op,
        const ::MIR::Param& val_r,
        // Should the output be saturated
        bool saturate = false
    ) {
        auto ti = TypeInfo::forType(ty);
        const auto& state = localState.state;
        bool didOverflow = false;

        // NOTE: Shifts can use any integer as the RHS, so give them special handling
        if (op == ::MIR::eBinOp::BIT_SHL || op == ::MIR::eBinOp::BIT_SHR) {
            ::HIR::TypeRef tmpR;
            const auto& ty_r = localState.state.getParamType(tmpR, val_r);
            auto tiR = TypeInfo::forType(ty_r);

            auto r = tiR.ty == TypeInfo::Unsigned ? localState.readParamUint(tiR.bits, val_r) : localState.readParamSint(tiR.bits, val_r).getInner();
            auto amt = r.truncateU64();
            if (amt > ti.bits) {
                DEBUG("Shift out of range - " << r << " > " << ti.bits);
                didOverflow = true;
                amt = 0;
            }
            switch (ti.ty) {
                case TypeInfo::Unsigned: {
                    auto l = localState.readParamUint(ti.bits, val_l);
                    switch (op) {
                        case ::MIR::eBinOp::BIT_SHL:
                            dst.write_uint(state, ti.bits, ti.mask(l << amt));
                            break;
                        case ::MIR::eBinOp::BIT_SHR:
                            dst.write_uint(state, ti.bits, ti.mask(l >> amt));
                            break;
                        default:
                            MIR_BUG(state, "This block should only be active for SHL/SHR");
                    }
                    break;
                }
                case TypeInfo::Signed: {
                    auto l = localState.readParamSint(ti.bits, val_l);
                    switch (op) {
                        case ::MIR::eBinOp::BIT_SHL:
                            dst.write_uint(state, ti.bits, ti.mask(l << amt));
                            break;
                        case ::MIR::eBinOp::BIT_SHR:
                            dst.write_uint(state, ti.bits, ti.mask(l >> amt));
                            break;
                        default:
                            MIR_BUG(state, "This block should only be active for SHL/SHR");
                    }
                    break;
                }
                default:
                    MIR_BUG(state, "Invalid use of BIT_SHL/BIT_SHR on " << ty);
            }
            return didOverflow;
        }
        {
            ::HIR::TypeRef tmpR;
            MIR_ASSERT(state, ty == localState.state.getParamType(tmpR, val_r), "BinOp with mismatched types");
        }

        switch (ti.ty) {
            case TypeInfo::Float: {
                auto l = localState.readParamFloat(ti.bits, val_l);
                auto r = localState.readParamFloat(ti.bits, val_r);
                auto write_result = [&](FloatValue value) {
                    if (!floatValueIsNan(value)) {
                        dst.write_float(state, ti.bits, value);
                        return;
                    }

                    switch (ti.bits) {
                        case 16:
                            dst.write_uint(state, ti.bits, U128(0x7e00));
                            break;
                        case 32:
                            dst.write_uint(state, ti.bits, U128(0x7fc00000));
                            break;
                        case 64:
                            dst.write_uint(state, ti.bits, U128(0x7ff8000000000000));
                            break;
                        case 128:
                            dst.write_uint(state, ti.bits, U128(0, 0x7fff800000000000));
                            break;
                        default:
                            MIR_BUG(state, "Invalid float width " << ti.bits);
                    }
                };
                switch (op) {
                    case ::MIR::eBinOp::ADD:
                        write_result(l + r);
                        break;
                    case ::MIR::eBinOp::SUB:
                        write_result(l - r);
                        break;
                    case ::MIR::eBinOp::MUL:
                        write_result(l * r);
                        break;
                    case ::MIR::eBinOp::DIV:
                        write_result(l / r);
                        break;
                    case ::MIR::eBinOp::MOD:
                        write_result(floatValueRemainder(l, r));
                        break;
                    case ::MIR::eBinOp::ADD_OV:
                    case ::MIR::eBinOp::SUB_OV:
                    case ::MIR::eBinOp::MUL_OV:
                    case ::MIR::eBinOp::DIV_OV:
                        MIR_TODO(state, "do_arith float unimplemented - val = " << l << " , " << r);

                    case ::MIR::eBinOp::BIT_OR:
                    case ::MIR::eBinOp::BIT_AND:
                    case ::MIR::eBinOp::BIT_XOR:
                        MIR_BUG(state, "do_arith float with bitwise - val = " << l << " , " << r);
                    case ::MIR::eBinOp::BIT_SHL:
                    case ::MIR::eBinOp::BIT_SHR:
                        MIR_BUG(state, "Bitshifts should be handled in caller");
                    case ::MIR::eBinOp::EQ:
                        dst.write_byte(state, l == r);
                        break;
                    case ::MIR::eBinOp::NE:
                        dst.write_byte(state, l != r);
                        break;
                    case ::MIR::eBinOp::GT:
                        dst.write_byte(state, l > r);
                        break;
                    case ::MIR::eBinOp::GE:
                        dst.write_byte(state, l >= r);
                        break;
                    case ::MIR::eBinOp::LT:
                        dst.write_byte(state, l < r);
                        break;
                    case ::MIR::eBinOp::LE:
                        dst.write_byte(state, l <= r);
                        break;
                }
                break;
            };
            case TypeInfo::Unsigned: {
                auto l = localState.readParamUint(ti.bits, val_l);
                auto r = localState.readParamUint(ti.bits, val_r);
                switch (op) {
                    case ::MIR::eBinOp::ADD: {
                        auto res = ti.mask(l + r);
                        didOverflow = res < l;
                        if (didOverflow && saturate) {
                            res = ti.mask(~U128());
                        }
                        dst.write_uint(state, ti.bits, res);
                        break;
                    }
                    case ::MIR::eBinOp::SUB: {
                        auto res = ti.mask(l - r);
                        didOverflow = res > l;
                        if (didOverflow && saturate) {
                            res = ti.mask(U128(0));
                        }
                        dst.write_uint(state, ti.bits, res);
                        break;
                    }
                    case ::MIR::eBinOp::MUL: {
                        auto res = ti.mask(l * r);
                        if (l != 0 && r != 0) {
                            didOverflow = res < l || res < r;
                        }
                        if (didOverflow && saturate) {
                            res = ti.mask(~U128());
                        }
                        dst.write_uint(state, ti.bits, res);
                        break;
                    }
                    case ::MIR::eBinOp::DIV:
                        // Early-prevent division by zero
                        if (r == 0) {
                            dst.write_uint(state, ti.bits, U128(0));
                            return true;
                        }
                        dst.write_uint(state, ti.bits, ti.mask(l / r));
                        break;
                    case ::MIR::eBinOp::MOD:
                        // Early-prevent division by zero
                        if (r == 0) {
                            dst.write_uint(state, ti.bits, U128(0));
                            return true;
                        }
                        dst.write_uint(state, ti.bits, ti.mask(l % r));
                        break;
                    case ::MIR::eBinOp::ADD_OV:
                    case ::MIR::eBinOp::SUB_OV:
                    case ::MIR::eBinOp::MUL_OV:
                    case ::MIR::eBinOp::DIV_OV:
                        MIR_TODO(state, "do_arith unsigned - val = " << l << " , " << r);

                    case ::MIR::eBinOp::BIT_OR:
                        dst.write_uint(state, ti.bits, l | r);
                        break;
                    case ::MIR::eBinOp::BIT_AND:
                        dst.write_uint(state, ti.bits, l & r);
                        break;
                    case ::MIR::eBinOp::BIT_XOR:
                        dst.write_uint(state, ti.bits, l ^ r);
                        break;
                    case ::MIR::eBinOp::BIT_SHL:
                    case ::MIR::eBinOp::BIT_SHR:
                        MIR_BUG(state, "Bitshifts should be handled in caller");

                    case ::MIR::eBinOp::EQ:
                        dst.write_byte(state, l == r);
                        break;
                    case ::MIR::eBinOp::NE:
                        dst.write_byte(state, l != r);
                        break;
                    case ::MIR::eBinOp::GT:
                        dst.write_byte(state, l > r);
                        break;
                    case ::MIR::eBinOp::GE:
                        dst.write_byte(state, l >= r);
                        break;
                    case ::MIR::eBinOp::LT:
                        dst.write_byte(state, l < r);
                        break;
                    case ::MIR::eBinOp::LE:
                        dst.write_byte(state, l <= r);
                        break;
                }
                break;
            }
            case TypeInfo::Signed: {
                auto l = localState.readParamSint(ti.bits, val_l);
                DEBUG(l << " from " << val_l);
                auto r = localState.readParamSint(ti.bits, val_r);
                DEBUG(r << " from " << val_r);
                DEBUG(l << " " << int(op) << " " << r);
                switch (op) {
                    case ::MIR::eBinOp::ADD: {
                        // Convert to raw/unsigned repr
                        auto v1u = l.getInner();
                        auto v2u = r.getInner();
                        // Then convert into a sign and absolute value
                        auto v1s = (l < 0);
                        auto v2s = (r < 0);
                        auto v1a = v1s ? ~v1u + 1 : v1u;
                        auto v2a = v2s ? ~v2u + 1 : v2u;

                        // Determine the sign
                        // - Equal has the same sign
                        // - V2 negative is negative if |v2| > |v1|
                        // - V1 negative is negative if |v2| < |v1|
                        bool resSign = (v1s == v2s) ? v1s : (v2s ? v1a < v2a : v1a > v2a);
                        auto res = S128(v1u + v2u);
                        didOverflow = ((res < 0) != resSign);
                        if (didOverflow && saturate) {
                            auto v = U128(0) << (ti.bits - 1);
                            res = resSign ? S128(v) : S128(v - 1);
                        }
                        dst.write_sint(state, ti.bits, res);
                        break;
                    }
                    case ::MIR::eBinOp::SUB: {
                        auto res = l - r;
                        // If the masked value isn't equal to the non-masked, then it's an overflow.
                        // TODO: What about 128 bit arith?
                        didOverflow = res.getInner() != ti.mask(res);
                        if (didOverflow && saturate) {
                            MIR_TODO(state, "do_arith signed sub overflow - saturate");
                        }
                        dst.write_uint(state, ti.bits, ti.mask(res));
                        break;
                    }
                    case ::MIR::eBinOp::MUL: {
                        auto res = l * r;
                        if (l != 0 && r != 0) {
                            if (res.u_abs() < l.u_abs() || res.u_abs() < r.u_abs()) {
                                didOverflow = true;
                            }
                        }
                        if (didOverflow && saturate) {
                            MIR_TODO(state, "do_arith signed mul overflow - saturate");
                        }
                        dst.write_uint(state, ti.bits, ti.mask(res));
                        break;
                    }
                    case ::MIR::eBinOp::DIV:
                        if (r == 0) {
                            dst.write_uint(state, ti.bits, U128(0));
                            return true;
                        }
                        dst.write_sint(state, ti.bits, ti.mask(l / r));
                        break;
                    case ::MIR::eBinOp::MOD:
                        if (r == 0) {
                            dst.write_uint(state, ti.bits, U128(0));
                            return true;
                        }
                        dst.write_sint(state, ti.bits, ti.mask(l % r));
                        break;
                    case ::MIR::eBinOp::ADD_OV:
                    case ::MIR::eBinOp::SUB_OV:
                    case ::MIR::eBinOp::MUL_OV:
                    case ::MIR::eBinOp::DIV_OV:
                        MIR_TODO(state, "do_arith signed - val = " << l << " , " << r);

                    case ::MIR::eBinOp::BIT_OR:
                        dst.write_uint(state, ti.bits, (l | r).getInner());
                        break;
                    case ::MIR::eBinOp::BIT_AND:
                        dst.write_uint(state, ti.bits, (l & r).getInner());
                        break;
                    case ::MIR::eBinOp::BIT_XOR:
                        dst.write_uint(state, ti.bits, (l ^ r).getInner());
                        break;
                    case ::MIR::eBinOp::BIT_SHL:
                    case ::MIR::eBinOp::BIT_SHR:
                        MIR_BUG(state, "Bitshifts should be handled in caller");

                    case ::MIR::eBinOp::EQ:
                        dst.write_byte(state, l == r);
                        break;
                    case ::MIR::eBinOp::NE:
                        dst.write_byte(state, l != r);
                        break;
                    case ::MIR::eBinOp::GT:
                        dst.write_byte(state, l > r);
                        break;
                    case ::MIR::eBinOp::GE:
                        dst.write_byte(state, l >= r);
                        break;
                    case ::MIR::eBinOp::LT:
                        dst.write_byte(state, l < r);
                        break;
                    case ::MIR::eBinOp::LE:
                        dst.write_byte(state, l <= r);
                        break;
                }
                break;
            }
            case TypeInfo::Other:
                const auto* borrowTy = ty->opt_Borrow();
                if (borrowTy && ((borrowTy->inner->is_Slice() && borrowTy->inner->as_Slice().inner == HIR::CoreType::U8) || borrowTy->inner == HIR::CoreType::Str)) {
                    struct P {
                        ::MIR::eval::RelocPtr reloc;
                        const void* data;
                        size_t len;

                        P(::MIR::eval::CallStackEntry& localState, const ::MIR::Param& p) {
                            auto vr = localState.getLval(p.as_LValue());
                            auto ptr = vr.readPtr(localState.state);
                            this->len = vr.slice(TargetGetPointerBits() / 8).readUsize(localState.state);
                            this->data = ptr.second.asValue().getBytes(ptr.first - EncodedLiteral::PTR_BASE, this->len, true);
                            MIR_ASSERT(localState.state, this->data, "Invalid pointer " << p << " : " << vr << " = " << ptr.second << " @ " << ptr.first << "+" << this->len);
                            this->reloc = std::move(ptr.second);
                        }
                    };

                    auto ptrL = P(localState, val_l);
                    auto ptrR = P(localState, val_r);
                    int cmp = memcmp(ptrL.data, ptrR.data, std::min(ptrL.len, ptrL.len));
                    if (cmp == 0) {
                        if (ptrL.len != ptrR.len) {
                            cmp = ptrL.len < ptrR.len ? -1 : 1;
                        }
                    }
                    switch (op) {
                        case ::MIR::eBinOp::EQ:
                            dst.write_byte(state, cmp == 0);
                            break;
                        case ::MIR::eBinOp::NE:
                            dst.write_byte(state, cmp != 0);
                            break;
                        case ::MIR::eBinOp::GT:
                            dst.write_byte(state, cmp > 0);
                            break;
                        case ::MIR::eBinOp::GE:
                            dst.write_byte(state, cmp >= 0);
                            break;
                        case ::MIR::eBinOp::LT:
                            dst.write_byte(state, cmp < 0);
                            break;
                        case ::MIR::eBinOp::LE:
                            dst.write_byte(state, cmp <= 0);
                            break;
                        default:
                            MIR_BUG(state, "BinOp " << int(op) << " on " << ty << " - Byte slice or &str");
                    }
                    break;
                } else {
                    MIR_BUG(state, "BinOp on " << ty);
                }
        }
        return didOverflow;
    }
}

namespace HIR {

    using namespace ::MIR::eval;

    unsigned int Evaluator::sNextEvalIndex = 0;

    Evaluator::CsePtr::~CsePtr() {
        if (inner) {
            delete inner;
            inner = nullptr;
        }
    }

    void Evaluator::pushStackEntry(::FmtLambda printPath, const ::MIR::Function& fcn, MonomorphState ms, ::HIR::TypeRef exp, ::HIR::Function::argsT argDefs, ::std::vector<::MIR::eval::AllocationPtr> args, const ::HIR::GenericParams* itemParamsDef, const ::HIR::GenericParams* implParamsDef) {
        this->callStack.push_back(new CallStackEntry(this->value_pool.mutPtr(), this->numFrames, this->root_span, this->resolve, std::move(printPath), std::move(exp), std::move(argDefs), fcn, std::move(ms), std::move(args), itemParamsDef, implParamsDef));
        this->numFrames += 1;
    }

    AllocationPtr Evaluator::runUntilStackEmpty() {
        const unsigned MAX_BLOCK_COUNT = 4'000'000;
        const unsigned MAX_STMT_COUNT = 8'000'000;
        assert(!this->callStack.empty());
        unsigned int numStmtsRun = 0;
        unsigned int idx;
        for (idx = 0; idx < MAX_BLOCK_COUNT; idx += 1) {
            if (numStmtsRun > MAX_STMT_COUNT) {
                break;
            }

            auto& state = this->callStack.back()->state;
            const auto& bb = state.fcn.blocks[state.getCurBlock()];
            for (const auto& stmt : bb.statements) {
                state.setCurStmt(state.getCurBlock(), &stmt - bb.statements.data());
                this->runStatement(*this->callStack.back(), stmt);
                numStmtsRun += 1;
            }
            state.setCurStmtTerm(state.getCurBlock());
            auto nextBlock = runTerminator(*this->callStack.back(), bb.terminator);
            numStmtsRun += 1;
            switch (nextBlock) {
                case TERM_RET_PUSHED:
                    continue;
                case TERM_RET_RETURN: {
                    MIR::eval::AllocationPtr rv = std::move(this->callStack.back()->retval);
                    this->callStack.pop_back();
                    if (this->callStack.empty() == 1) {
                        return rv;
                    } else {
                        auto& nextState = *this->callStack.back();
                        const auto& term = nextState.state.fcn.blocks[nextState.state.getCurBlock()].terminator;
                        const auto& te = term.as_Call();
                        auto dst = nextState.getLval(te.retVal);
                        dst.copyFrom(nextState.state, ValueRef(rv));
                        nextState.state.setCurStmt(te.retBlock, 0);
                    }
                    break;
                }
                default:
                    state.setCurStmt(nextBlock, 0);
            }
        }
        ERROR(this->root_span, E0000, "Constant evaluation ran for too long - " << numStmtsRun << " statements, " << idx << " blocks");
    }

    void Evaluator::runStatement(::MIR::eval::CallStackEntry& localState, const ::MIR::Statement& stmt) {
        const auto& state = localState.state;
        DEBUG("E" << this->evalIndex << " F" << localState.frameIndex << " " << state << stmt);

        TU_MATCH_HDRA( (stmt), { )
        TU_ARMA(Assign, e) {
                // Fall through
            }
            TU_ARMA(ScopeEnd, se) {
                // Just ignore, it's a hint
                return;
            }
            TU_ARMA(SetDropFlag, se) {
                MIR_ASSERT(state, se.idx < localState.dropFlags.size(), "Drop flag " << se.idx << " out of range");
                if (se.other == UINT_MAX) {
                    localState.dropFlags[se.idx] = se.newVal;
                } else {
                    MIR_ASSERT(state, se.other < localState.dropFlags.size(), "Drop flag " << se.other << " out of range");
                    localState.dropFlags[se.idx] = se.newVal != localState.dropFlags[se.other];
                }
                return;
            }
            TU_ARMA(SaveDropFlag, se) {
                MIR_TODO(state, "Non-assign statement - " << stmt);
            }
            TU_ARMA(LoadDropFlag, se) {
                MIR_TODO(state, "Non-assign statement - " << stmt);
            }
            TU_ARMA(Asm, se) {
                MIR_TODO(state, "Non-assign statement - " << stmt);
            }
            TU_ARMA(Asm2, se) {
                MIR_TODO(state, "Non-assign statement - " << stmt);
            }
        }

        const auto& sa = stmt.as_Assign();

        auto dst = localState.getLval(sa.dst);
        TU_MATCH_HDRA( (sa.src), {)
        TU_ARMA(Use, e) {
                dst.copyFrom(state, localState.getLval(e));
            }
            TU_ARMA(Constant, e) {
                localState.write_const(dst, e);
            }
            TU_ARMA(Borrow, e) {
                localState.write_borrow(dst, e.type, e.val);
            }
            TU_ARMA(Cast, e) {
                ::HIR::TypeRef tmp;
                const auto& srcTy = state.getLvalueType(tmp, e.val);

                auto inval = localState.getLval(e.val);

            TU_MATCH_HDRA( (*e.type), {)
            default:
                // NOTE: Can be an unsizing!
                MIR_TODO(state, "RValue::Cast to " << e.type << " from " << srcTy << ", val = " << inval);
                    TU_ARMA(Primitive, te) {
                        auto ti = TypeInfo::forPrimitive(te);
                        auto srcTi = TypeInfo::forType(srcTy);
                        switch (ti.ty) {
                            // Integers mask down
                            case TypeInfo::Signed:
                            case TypeInfo::Unsigned:
                                switch (srcTi.ty) {
                                    case TypeInfo::Signed: {
                                        auto v = inval.readSint(state, srcTi.bits);
                                        dst.write_uint(state, ti.bits, v.getInner());
                                    } break;
                                    case TypeInfo::Unsigned:
                                        MIR_ASSERT(state, !srcTy->is_NamedFunction(), "");
                                        dst.write_uint(state, ti.bits, inval.readUint(state, srcTi.bits));
                                        break;
                                    case TypeInfo::Float:
                                        if (ti.ty == TypeInfo::Signed) {
                                            dst.write_uint(state, ti.bits, static_cast<int64_t>(inval.readFloat(state, srcTi.bits)));
                                        } else {
                                            dst.write_uint(state, ti.bits, static_cast<uint64_t>(inval.readFloat(state, srcTi.bits)));
                                        }
                                        break;
                                    case TypeInfo::Other: {
                                        MIR_ASSERT(state, TU_TEST1(*srcTy, Path, .binding.is_Enum()), "Constant cast Variant to integer with invalid type - " << srcTy);
                                        MIR_ASSERT(state, srcTy->as_Path().binding.as_Enum(), "Enum binding pointer not set! - " << srcTy);
                                        const HIR::Enum& enm = *srcTy->as_Path().binding.as_Enum();
                                        MIR_ASSERT(state, enm.isValue(), "Constant cast Variant to integer with non-value enum - " << srcTy);
                                        const auto* repr = TargetGetTypeRepr(state.sp, resolve, srcTy);
                                        if (!repr) {
                                            throw Defer();
                                        }
                                        auto& ve = repr->variants.as_Values();

                                        auto v = inval.slice(repr->getOffset(state.sp, resolve, ve.field), ve.field.size).readUint(state, ve.field.size * 8);
                                        // TODO: Ensure that this is a valid variant?
                                        dst.write_uint(state, ti.bits, v);
                                    } break;
                                }
                                break;
                            case TypeInfo::Float:
                                switch (srcTi.ty) {
                                    // NOTE: Subtle rounding differences between f32 and f64
                                    case TypeInfo::Signed: {
                                        auto v = S128(inval.readUint(state, srcTi.bits));
                                        dst.write_float(state, ti.bits, ti.bits == 32 ? v.toFloat() : v.toDouble());
                                        break;
                                    }
                                    case TypeInfo::Unsigned: {
                                        auto v = inval.readUint(state, srcTi.bits);
                                        dst.write_float(state, ti.bits, ti.bits == 32 ? v.toFloat() : v.toDouble());
                                        break;
                                    }
                                    case TypeInfo::Float:
                                        dst.write_float(state, ti.bits, inval.readFloat(state, srcTi.bits));
                                        break;
                                    case TypeInfo::Other:
                                        MIR_TODO(state, "Cast " << srcTy << " to float");
                                }
                                break;
                            default:
                                MIR_TODO(state, "RValue::Cast to " << e.type << ", val = " << inval);
                        }
                    }
                    break;
                    // Allow casting any integer value to a pointer (TODO: Ensure that the pointer is sized?)
                    case HIR::TypeData::TAG_Pointer:
                    case HIR::TypeData::TAG_Function:
                        if (const auto* e = srcTy->opt_NamedFunction()) {
                            dst.write_ptr(state, EncodedLiteral::PTR_BASE, localState.getStaticrefMono(e->path));
                        } else {
                            dst.copyFrom(state, inval.slice(0, std::min(inval.getLen(), dst.getLen())));
                        }
                        break;
            }
            }
            TU_ARMA(BinOp, e) {
                ::HIR::TypeRef tmp;
                const auto& ty_l = state.getParamType(tmp, e.val_l);
                //auto ti = TypeInfo::for_type(ty_l);
                bool didOverflow = doArithChecked(localState, ty_l, dst, e.val_l, e.op, e.val_r);
                switch (e.op) {
                    case ::MIR::eBinOp::DIV:
                    case ::MIR::eBinOp::MOD:
                        if (didOverflow) {
                            MIR_BUG(state, "Division/modulo by zero!");
                        }
                        break;
                    case ::MIR::eBinOp::BIT_SHL:
                    case ::MIR::eBinOp::BIT_SHR:
                        if (didOverflow) {
                            MIR_BUG(state, "Bit shift out of range");
                        }
                        break;
                    default:
                        break;
                }
            }
            TU_ARMA(UniOp, e) {
                ::HIR::TypeRef tmp;
                const auto& ty_l = state.getLvalueType(tmp, e.val);
                auto ti = TypeInfo::forType(ty_l);

                switch (ti.ty) {
                    case TypeInfo::Unsigned:
                    case TypeInfo::Signed: {
                        auto i = localState.getLval(e.val).readUint(state, ti.bits);
                        switch (e.op) {
                            case ::MIR::eUniOp::INV:
                                i = ti.mask(~i);
                                break;
                            case ::MIR::eUniOp::NEG:
                                i = ~i + 1u;
                                break;
                        }
                        dst.write_uint(state, ti.bits, i);
                        break;
                    }
                    case TypeInfo::Float: {
                        auto v = localState.getLval(e.val).readFloat(state, ti.bits);
                        switch (e.op) {
                            case ::MIR::eUniOp::INV:
                                MIR_BUG(state, "Invalid invert of Float");
                            case ::MIR::eUniOp::NEG:
                                v = -v;
                                break;
                        }
                        dst.write_float(state, ti.bits, v);
                        break;
                    }
                    case TypeInfo::Other:
                        MIR_BUG(state, "UniOp on " << ty_l);
                }
            }
            TU_ARMA(DstMeta, e) {
                auto v = localState.getLval(e.val);
                size_t ptrSize = TargetGetPointerBits() / 8;
                dst.copyFrom(state, v.slice(ptrSize));
            }
            TU_ARMA(DstPtr, e) {
                auto v = localState.getLval(e.val);
                size_t ptrSize = TargetGetPointerBits() / 8;
                dst.copyFrom(state, v.slice(0, ptrSize));
            }
            TU_ARMA(MakeDst, e) {
                if (TU_TEST2(e.metaVal, Constant, , ItemAddr, .get() == nullptr)) {
                    ::HIR::TypeRef tmp;
                    const auto& srcTy = state.getParamType(tmp, e.ptrVal);
                    ::HIR::TypeRef tmp2;
                    const auto& dstTy = state.getLvalueType(tmp2, sa.dst);

                TU_MATCH_HDRA( (*dstTy), {)
                default:
                    // NOTE: Can be an unsizing!
                    MIR_TODO(state, "RValue::MakeDst Coerce to " << dstTy);
                        TU_ARMA(Path, te) {
                            bool done = false;
                            // CoerceUnsized cast
                            if (te.binding.is_Struct()) {
                                const HIR::Struct& str = *te.binding.as_Struct();
                                if (srcTy->is_Path() && srcTy->as_Path().binding.is_Struct() && srcTy->as_Path().binding.as_Struct() == &str) {
                                    if (str.structMarkings.coerceUnsized != HIR::StructMarkings::Coerce::None) {
                                        done = true;
                                    }
                                }
                            }
                            if (!done) {
                                MIR_TODO(state, "RValue::MakeDst Coerce to " << dstTy);
                            }
                        }
                        TU_ARMA(Borrow, te) {
                            const auto* dynamicTypeD = &te.inner;
                            const auto* dynamicTypeS = &srcTy->as_Borrow().inner;
                            for (;;) {
                                if (const auto* tep = (*dynamicTypeD)->opt_Path()) {
                                    MIR_ASSERT(state, tep->binding.is_Struct(), "RValue::MakeDst to " << *dynamicTypeD);
                                    const auto& sm = tep->binding.as_Struct()->structMarkings;
                                    dynamicTypeD = &tep->path.mData.as_Generic().mParams.types.at(sm.unsized_param);
                                    dynamicTypeS = &(*dynamicTypeS)->as_Path().path.mData.as_Generic().mParams.types.at(sm.unsized_param);
                                } else {
                                    break;
                                }
                            }
                            // TODO: What can cast TO a borrow? - Non-converted dyn unsizes .. but they require vtables,
                            // which aren't available yet!
                            if (const auto* tep = (*dynamicTypeD)->opt_TraitObject()) {
                                static const RcString rcstringVtable = RcString::newInterned("vtable#");
                                auto vtable_path = ::HIR::Path(*dynamicTypeS, tep->mTrait.mPath.clone(), rcstringVtable);
                                dst.slice(TargetGetPointerBits() / 8).write_ptr(state, EncodedLiteral::PTR_BASE, localState.getStaticref(std::move(vtable_path)));
                            } else if (/*const auto* tep =*/(*dynamicTypeD)->opt_Slice()) {
                                auto size = (*dynamicTypeS)->as_Array().size.as_Known();
                                dst.slice(TargetGetPointerBits() / 8).write_uint(state, TargetGetPointerBits(), size);
                            } else {
                                MIR_BUG(state, "RValue::MakeDst to " << dstTy << " from " << srcTy << " - " << *dynamicTypeD << " from " << *dynamicTypeS);
                            }
                        }
                }

                if( const auto* p = e.ptrVal.opt_Borrow() ) {
                        localState.write_borrow(dst, p->type, p->val);
                }
                else if( const auto* c = e.ptrVal.opt_Constant() ) {
                        localState.write_const(dst, *c);
                }
                else {
                        auto inval = localState.getLval(e.ptrVal.as_LValue());
                        dst.slice(0, TargetGetPointerBits() / 8).copyFrom(state, inval);
                }
                } else {
                    size_t ptrSize = TargetGetPointerBits() / 8;
                    localState.write_param(dst.slice(0, ptrSize), e.ptrVal);
                    localState.write_param(dst.slice(ptrSize), e.metaVal);
                }
            }
            TU_ARMA(Tuple, e) {
                ::HIR::TypeRef tmp;
                const auto& ty = state.getLvalueType(tmp, sa.dst);
                auto* repr = TargetGetTypeRepr(state.sp, resolve, ty);
                if (!repr) {
                    throw Defer();
                }
                MIR_ASSERT(state, repr->fields.size() == e.vals.size(), "");
                for (size_t i = 0; i < e.vals.size(); i++) {
                    size_t sz = localState.sizeOfOrBug(repr->fields[i].ty);
                    localState.write_param(dst.slice(repr->fields[i].offset, sz), e.vals[i]);
                }
            }
            TU_ARMA(Struct, e) {
                ::HIR::TypeRef tmp;
                const auto& ty = state.getLvalueType(tmp, sa.dst);
                auto* repr = TargetGetTypeRepr(state.sp, resolve, ty);
                if (!repr) {
                    throw Defer();
                }
                MIR_ASSERT(state, repr->fields.size() == e.vals.size(), "");
                for (size_t i = 0; i < e.vals.size(); i++) {
                    size_t sz = localState.sizeOfOrBug(repr->fields[i].ty);
                    auto localDst = dst.slice(repr->fields[i].offset, sz);
                    localState.write_param(localDst, e.vals[i]);
                    DEBUG("@" << repr->fields[i].offset << " = " << localDst);
                }
            }
            TU_ARMA(SizedArray, e) {
                size_t count = 0;
            TU_MATCH_HDRA( (e.count), {)
            TU_ARMA(Known, v) {
                        count = v;
                    }
                    TU_ARMA(Unevaluated, v) {
                        const auto* vp = &v;
                        HIR::ConstGeneric tmpV;
                        if (const auto* ve = v.opt_Generic()) {
                            vp = &(tmpV = localState.ms.getValue(state.sp, *ve));
                        }
                        EncodedLiteral tmpVal;
                        count = localState.getConst(*vp, tmpVal).readUsize(0);
                    }
            }

            if( count > 0 )
            {
                    ::HIR::TypeRef tmp;
                    const auto& ty = state.getLvalueType(tmp, sa.dst);
                    const auto& ity = ty->as_Array().inner;
                    size_t sz = localState.sizeOfOrBug(ity);

                    localState.write_param(dst.slice(0, sz), e.val);
                    if (sz > 0) {
                        for (size_t i = 1; i < count; i++) {
                            dst.slice(sz * i, sz).copyFrom(state, dst.slice(0, sz));
                        }
                    }
            }
            }
            TU_ARMA(Array, e) {
                ::HIR::TypeRef tmp;
                const auto& ty = state.getLvalueType(tmp, sa.dst);
                const auto& ity = ty->as_Array().inner;
                size_t sz = localState.sizeOfOrBug(ity);

                size_t ofs = 0;
                for (const auto& v : e.vals) {
                    localState.write_param(dst.slice(ofs, sz), v);
                    ofs += sz;
                }
            }
            TU_ARMA(UnionVariant, e) {
                // TODO: Write some hidden information to contain the variant?
                localState.write_param(dst, e.val);
            }
            TU_ARMA(EnumVariant, e) {
                ::HIR::TypeRef tmp;
                const auto& ty = state.getLvalueType(tmp, sa.dst);
                auto* enmRepr = TargetGetTypeRepr(state.sp, resolve, ty);
                if (!enmRepr) {
                    throw Defer();
                }
                if (e.vals.size() > 0) {
                    auto ofs = enmRepr->fields[e.index].offset;
                    const auto& ity = enmRepr->fields[e.index].ty;
                    auto* repr = TargetGetTypeRepr(state.sp, resolve, ity);
                    if (!repr) {
                        throw Defer();
                    }
                    for (size_t i = 0; i < e.vals.size(); i++) {
                        size_t sz = localState.sizeOfOrBug(repr->fields[i].ty);
                        auto localDst = dst.slice(ofs + repr->fields[i].offset, sz);
                        localState.write_param(localDst, e.vals[i]);
                        DEBUG("@" << (ofs + repr->fields[i].offset) << " = " << localDst);
                    }
                }

            TU_MATCH_HDRA( (enmRepr->variants), {)
            TU_ARMA(None, ve) {
                    }
                    TU_ARMA(NonZero, ve) {
                        // No tag to write, just leave as zeroes
                        if (e.index == ve.zero_variant) {
                            auto ofs = getOffset(state.sp, resolve, enmRepr, ve.field);
                            auto savedOfs = ofs;
                            for (size_t i = 0; i + 8 <= ve.field.size; i += 8) {
                                dst.slice(ofs, 8).write_uint(state, 64, 0);
                                ofs += 8;
                            }
                            if (ve.field.size % 8 > 0) {
                                dst.slice(ofs, ve.field.size % 8).write_uint(state, (ve.field.size % 8) * 8, 0);
                            }
                            DEBUG("@" << ofs << " = " << dst.slice(savedOfs, ve.field.size) << " NonZero");
                        } else {
                            // No tag, already filled
                        }
                    }
                    TU_ARMA(Linear, ve) {
                        if (ve.isNiche(e.index)) {
                            // No need to write tag, as this variant is the niche
                        } else {
                            auto ofs = getOffset(state.sp, resolve, enmRepr, ve.field);
                            MIR_ASSERT(state, ve.field.size <= 64 / 8, "");
                            dst.slice(ofs, ve.field.size).write_uint(state, ve.field.size * 8, ve.offset + e.index);
                        }
                    }
                    TU_ARMA(Values, ve) {
                        const auto& fld = enmRepr->fields[ve.field.index];
                        auto ti = TypeInfo::forType(fld.ty);
                        MIR_ASSERT(state, ti.ty == TypeInfo::Signed || ti.ty == TypeInfo::Unsigned, "EnumVariant: Values not integer - " << fld.ty);
                        auto tagDst = dst.slice(fld.offset, (ti.bits + 7) / 8);
                        if (ti.ty == TypeInfo::Signed) {
                            tagDst.write_sint(state, ti.bits, S128(ve.values.at(e.index)));
                        } else {
                            tagDst.write_uint(state, ti.bits, ve.values.at(e.index));
                        }
                    }
            }
            }
        }

        DEBUG("> E" << this->evalIndex << " F" << localState.frameIndex << " " << sa.dst << " := " << dst);
    }

    unsigned Evaluator::runTerminator(::MIR::eval::CallStackEntry& localState, const ::MIR::Terminator& terminator) {
        const auto& state = localState.state;
        DEBUG("E" << this->evalIndex << " F" << localState.frameIndex << " " << state << terminator);

        TU_MATCH_HDRA( (terminator), {)
        default:
            MIR_BUG(state, "Unexpected terminator - " << terminator);
            TU_ARMA(Goto, e) {
                return e;
            }
            TU_ARMA(Return, e) {
                return TERM_RET_RETURN;
            }
            TU_ARMA(If, e) {
                bool res = U128(0) != localState.getLval(e.cond).readUint(state, 1);
                DEBUG(state << " IF " << res);
                return res ? e.bbTrue : e.bbFalse;
            }
            TU_ARMA(Switch, e) {
                if (e.valid_flag != ~0u && !localState.dropFlags.at(e.valid_flag)) {
                    return e.invalidTarget;
                }
                HIR::TypeRef tmp;
                const auto& ty = state.getLvalueType(tmp, e.val);
                auto lit = localState.getLval(e.val);
                auto var_idx = localState.readEnumVariant(ty, lit);
                DEBUG(state << " = " << var_idx);
                MIR_ASSERT(state, var_idx < e.targets.size(), "Switch " << var_idx << " out of range in target list (" << e.targets.size() << ")");
                return e.targets[var_idx];
            }
            TU_ARMA(SwitchValue, e) {
                HIR::TypeRef tmp;
                const auto& ty = state.getLvalueType(tmp, e.val);
                auto ti = TypeInfo::forType(ty);
                auto lit = localState.getLval(e.val);

                unsigned targetIdx = ~0u;
            TU_MATCH_HDRA( (e.values), { )
            default:
                MIR_TODO(state, "SwitchValue - " << e.values.tagStr());
                    TU_ARMA(Unsigned, vals) {
                        auto v = lit.readUint(state, ti.bits);
                        for (size_t i = 0; i < vals.size(); i++) {
                            if (v == U128(vals[i])) {
                                targetIdx = i;
                                break;
                            }
                        }
                    }
            }
            if( targetIdx == ~0u ) {
                    return e.defTarget;
            }
            else {
                    return e.targets[targetIdx];
            }
            }
            TU_ARMA(Drop, e) {
                if (e.flagIdx != UINT_MAX && !localState.dropFlags.at(e.flagIdx)) {
                    return e.target;
                }

                ::HIR::TypeRef tmp;
                const auto& ty = state.getLvalueType(tmp, e.slot);
                auto value = localState.getLval(e.slot);
                if (!localState.value_reachable_from_return(value) && localState.value_needs_non_const_drop(ty, value)) {
                    ERROR(this->root_span, E0000, "destructor of `" << ty << "` cannot be evaluated at compile-time");
                }
                return e.target;
            }
            TU_ARMA(Call, e) {
                const auto& ms = localState.ms;
                if (const auto* te = e.fcn.opt_Intrinsic()) {
                    auto dst = localState.getLval(e.retVal);
                    if (te->name == "size_of") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        size_t sizeVal;
                        if (TargetGetSizeOf(state.sp, this->resolve, ty, sizeVal)) {
                            dst.write_uint(state, TargetGetPointerBits(), U128(sizeVal));
                        } else {
                            throw Defer();
                        }
                    } else if (te->name == "size_of_val") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        size_t sizeVal;
                        size_t alignVal;
                        if (!TargetGetSizeAndAlignOf(state.sp, this->resolve, ty, sizeVal, alignVal)) {
                            throw Defer();
                        }
                        if (sizeVal == SIZE_MAX) {
                            size_t itemSize;
                            if (const auto* slice = ty->opt_Slice()) {
                                if (!TargetGetSizeOf(state.sp, this->resolve, slice->inner, itemSize)) {
                                    throw Defer();
                                }
                            } else if (ty == ::HIR::CoreType::Str) {
                                itemSize = 1;
                            } else {
                                throw Defer();
                            }
                            auto arg = localState.getLval(e.args.at(0).as_LValue());
                            const auto len = arg.slice(TargetGetPointerBits() / 8).readUsize(state);
                            MIR_ASSERT(state, itemSize == 0 || len <= SIZE_MAX / itemSize, "`size_of_val` overflow for " << ty);
                            sizeVal = len * itemSize;
                        }
                        dst.write_uint(state, TargetGetPointerBits(), U128(sizeVal));
                    } else if (te->name == "align_of" || te->name == "min_align_of") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        size_t alignVal;
                        if (TargetGetAlignOf(state.sp, this->resolve, ty, alignVal)) {
                            dst.write_uint(state, TargetGetPointerBits(), U128(alignVal));
                        } else {
                            throw Defer();
                        }
                    } else if (te->name == "align_of_val" || te->name == "min_align_of_val") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        size_t sizeVal;
                        size_t alignVal;
                        if (TargetGetSizeAndAlignOf(state.sp, this->resolve, ty, sizeVal, alignVal) && alignVal > 0) {
                            dst.write_uint(state, TargetGetPointerBits(), U128(alignVal));
                        } else {
                            throw Defer();
                        }
                    } else if (te->name == "offset_of") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        size_t val = state.intrinsicOffsetOf(ty, e.args);
                        dst.write_uint(state, TargetGetPointerBits(), U128(val));
                    } else if (te->name == "type_name") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        auto name = state.intrinsicTypeName(ty);
                        dst.write_ptr(state, EncodedLiteral::PTR_BASE, AllocationPtr::allocateRo(localState.value_pool, name.data(), name.size()));
                        dst.slice(TargetGetPointerBits() / 8).write_uint(state, TargetGetPointerBits(), name.size());
                    } else if (te->name == "type_id") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        dst.write_ptr(state, EncodedLiteral::PTR_BASE, StaticRefPtr::allocate(localState.value_pool, HIR::Path(mv$(ty), "#type_id"), nullptr));
                    } else if (te->name == "needs_drop") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        dst.write_uint(state, 8, resolve.type_needs_drop_glue(state.sp, ty) ? 1 : 0);
                    } else if (te->name == "caller_location") {
                        auto ty_path = resolve.crate.getLangItemPath(state.sp, "panic_location");
                        auto ty = resolve.crate.types.path(ty_path, &resolve.crate.getStructByPath(state.sp, ty_path));
                        auto* repr = TargetGetTypeRepr(state.sp, resolve, ty);
                        MIR_ASSERT(state, repr, "No repr for panic::Location?");
                        MIR_ASSERT(state, repr->fields.size() == 4, "Unexpected item count in panic::Location");
                        auto val = RelocPtr(AllocationPtr::allocate(localState.value_pool, resolve, state, ty));
                        dst.write_ptr(state, EncodedLiteral::PTR_BASE, val);
                        auto rv = ValueRef(val);
                        auto pb = TargetGetPointerBits() / 8;
                        const SpanInnerSource* caller = nullptr;
                        for (const Span* span = &state.sp; span->get(); span = &span->get()->parent_span) {
                            caller = cast<const SpanInnerSource>(span->get());
                            if (caller) {
                                break;
                            }
                        }
                        const auto* filename = caller ? caller->filename.c_str() : "";
                        const auto filenameLen = caller ? caller->filename.size() : 0;
                        rv.slice(repr->fields[0].offset + 0, pb).write_ptr(state, EncodedLiteral::PTR_BASE, ConstantPtr::allocate(localState.value_pool, filename, filenameLen + 1)); // file.ptr, including trailing NUL
                        rv.slice(repr->fields[0].offset + pb, pb).write_uint(state, TargetGetPointerBits(), filenameLen);                                                             // file.len
                        rv.slice(repr->fields[1].offset, 4).write_uint(state, 32, caller ? caller->startLine : 0);                                                                     // line: u32
                        rv.slice(repr->fields[2].offset, 4).write_uint(state, 32, 0);                                                                                                   // col: u32 (expression AST stores only the end point)
                    }
                    // ---
                    else if (te->name == "ctpop") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        MIR_ASSERT(state, ty->is_Primitive(), "ctpop with non-primitive " << ty);
                        auto ti = TypeInfo::forType(ty);
                        auto val = ti.mask(localState.readParamUint(ti.bits, e.args.at(0)));
                        unsigned rv = __builtin_popcountll(val.getLo()) + __builtin_popcountll(val.getHi());
                        dst.write_uint(state, 32, U128(rv));
                    }
                    // - CounT Trailing Zeros
                    else if (te->name == "cttz" || te->name == "cttz_nonzero") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        MIR_ASSERT(state, ty->is_Primitive(), "`cttz` with non-primitive " << ty);
                        auto ti = TypeInfo::forType(ty);
                        auto val = ti.mask(localState.readParamUint(ti.bits, e.args.at(0)));
                        unsigned rv = 0;
                        if (val == U128(0)) {
                            rv = ti.bits;
                        } else {
                            while ((val & 1) == U128(0)) {
                                val >>= 1;
                                rv += 1;
                            }
                        }
                        dst.write_uint(state, 32, U128(rv));
                    }
                    // - CounT Lrailing Zeros
                    else if (te->name == "ctlz" || te->name == "ctlz_nonzero") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        MIR_ASSERT(state, ty->is_Primitive(), "`ctlz` with non-primitive " << ty);
                        auto ti = TypeInfo::forType(ty);
                        auto val = ti.mask(localState.readParamUint(ti.bits, e.args.at(0)));
                        unsigned rv = 0;
                        // Count how many shifts needed to remove the MSB
                        while (val != U128(0)) {
                            val >>= 1;
                            rv += 1;
                        }
                        // Then subtract from the total bit count (no shift needed = max bits)
                        dst.write_uint(state, 32, U128(ti.bits - rv));
                    } else if (te->name == "bswap") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        MIR_ASSERT(state, ty->is_Primitive(), "bswap with non-primitive " << ty);
                        auto ti = TypeInfo::forType(ty);
                        auto val = localState.readParamUint(ti.bits, e.args.at(0));
                        struct H {
                            static uint16_t bswap16(uint16_t v) {
                                return (v >> 8) | (v << 8);
                            }
                            static uint32_t bswap32(uint32_t v) {
                                return bswap16(v >> 16) | (static_cast<uint32_t>(bswap16(static_cast<uint16_t>(v))) << 16);
                            }
                            static uint64_t bswap64(uint64_t v) {
                                return bswap32(v >> 32) | (static_cast<uint64_t>(bswap32(static_cast<uint32_t>(v))) << 32);
                            }
                            static U128 bswap128(U128 v) {
                                return U128(bswap64((v >> 64).truncateU64()), bswap64(v.truncateU64()));
                            }
                        };
                        U128 rv;
                        switch (ty->as_Primitive()) {
                            case ::HIR::CoreType::I8:
                            case ::HIR::CoreType::U8:
                                rv = val;
                                break;
                            case ::HIR::CoreType::I16:
                            case ::HIR::CoreType::U16:
                                rv = U128(H::bswap16(val.truncateU64()));
                                break;
                            case ::HIR::CoreType::I32:
                            case ::HIR::CoreType::U32:
                                rv = U128(H::bswap32(val.truncateU64()));
                                break;
                            case ::HIR::CoreType::I64:
                            case ::HIR::CoreType::U64:
                                rv = U128(H::bswap64(val.truncateU64()));
                                break;
                            case ::HIR::CoreType::I128:
                            case ::HIR::CoreType::U128:
                                rv = H::bswap128(val);
                                break;
                            default:
                                MIR_TODO(state, "Handle bswap with " << ty);
                        }
                        dst.write_uint(state, ti.bits, rv);
                    } else if (te->name == "bitreverse") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        MIR_ASSERT(state, ty->is_Primitive(), "bswap with non-primitive " << ty);
                        auto ti = TypeInfo::forType(ty);

                        auto val = localState.readParamUint(ti.bits, e.args.at(0));
                        U128 rv;
                        for (size_t i = 0; i < ti.bits; i++) {
                            // Shift before inserting - shifting after leaks the first bit out of
                            // the top and drops the last one (broke miniz_oxide's reversed-bits
                            // lookup table).
                            rv <<= 1;
                            if ((val & 1) != 0) {
                                rv |= 1;
                            }
                            val >>= 1;
                        }
                        dst.write_uint(state, ti.bits, rv);
                    } else if (te->name == "rotate_left" || te->name == "rotate_right") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        MIR_ASSERT(state, ty->is_Primitive(), te->name << " with non-primitive " << ty);
                        auto ti = TypeInfo::forType(ty);

                        auto val = localState.readParamUint(ti.bits, e.args.at(0));
                        auto count = localState.readParamUint(32, e.args.at(1));
                        unsigned countI = (count % ti.bits).truncateU64();

                        U128 rv;
                        if (countI == 0) {
                            rv = val;
                        } else if (te->name == "rotate_left") {
                            // NOTE: `read_param_uint` has zeroes in the high bits, so anything above `ti.bits` should be zero
                            auto a = val << countI;
                            auto b = val >> (ti.bits - countI);
                            rv = a | b;
                        } else {
                            auto a = val >> countI;
                            auto b = val << (ti.bits - countI);
                            rv = a | b;
                        }
                        // Writing back will truncate away the higher bits
                        dst.write_uint(state, ti.bits, rv);
                    }
                    // ---
                    else if (te->name == "add_with_overflow") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        MIR_ASSERT(state, ty->is_Primitive(), "`" << te->name << "` with non-primitive " << ty);
                        auto dstTup = getTupleTBool(localState, dst, ty);
                        bool overflowed = doArithChecked(localState, ty, dstTup.first, e.args.at(0), ::MIR::eBinOp::ADD, e.args.at(1));
                        dstTup.second.write_uint(state, 8, U128(overflowed ? 1 : 0));
                    } else if (te->name == "sub_with_overflow") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        MIR_ASSERT(state, ty->is_Primitive(), "`" << te->name << "` with non-primitive " << ty);
                        auto dstTup = getTupleTBool(localState, dst, ty);
                        bool overflowed = doArithChecked(localState, ty, dstTup.first, e.args.at(0), ::MIR::eBinOp::SUB, e.args.at(1));
                        dstTup.second.write_uint(state, 8, U128(overflowed ? 1 : 0));
                    } else if (te->name == "mul_with_overflow") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        MIR_ASSERT(state, ty->is_Primitive(), "`" << te->name << "` with non-primitive " << ty);
                        auto dstTup = getTupleTBool(localState, dst, ty);
                        bool overflowed = doArithChecked(localState, ty, dstTup.first, e.args.at(0), ::MIR::eBinOp::MUL, e.args.at(1));
                        dstTup.second.write_uint(state, 8, U128(overflowed ? 1 : 0));
                    }
                    // Unchecked and wrapping are the same
                    else if (te->name == "wrapping_add" || te->name == "unchecked_add") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        MIR_ASSERT(state, ty->is_Primitive(), "`" << te->name << "` with non-primitive " << ty);
                        doArithChecked(localState, ty, dst, e.args.at(0), ::MIR::eBinOp::ADD, e.args.at(1));
                    } else if (te->name == "wrapping_sub" || te->name == "unchecked_sub") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        MIR_ASSERT(state, ty->is_Primitive(), "`" << te->name << "` with non-primitive " << ty);
                        doArithChecked(localState, ty, dst, e.args.at(0), ::MIR::eBinOp::SUB, e.args.at(1));
                    } else if (te->name == "wrapping_mul" || te->name == "unchecked_mul") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        MIR_ASSERT(state, ty->is_Primitive(), "`" << te->name << "` with non-primitive " << ty);
                        doArithChecked(localState, ty, dst, e.args.at(0), ::MIR::eBinOp::MUL, e.args.at(1));
                    } else if (te->name == "unchecked_shl") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        MIR_ASSERT(state, ty->is_Primitive(), "`" << te->name << "` with non-primitive " << ty);
                        doArithChecked(localState, ty, dst, e.args.at(0), ::MIR::eBinOp::BIT_SHL, e.args.at(1));
                    } else if (te->name == "unchecked_shr") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        MIR_ASSERT(state, ty->is_Primitive(), "`" << te->name << "` with non-primitive " << ty);
                        doArithChecked(localState, ty, dst, e.args.at(0), ::MIR::eBinOp::BIT_SHR, e.args.at(1));
                    }
                    // - Except for div/rem, which add checking just in case
                    else if (te->name == "unchecked_rem") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        MIR_ASSERT(state, ty->is_Primitive(), "`" << te->name << "` with non-primitive " << ty);
                        bool was_overflow = doArithChecked(localState, ty, dst, e.args.at(0), ::MIR::eBinOp::MOD, e.args.at(1));
                        MIR_ASSERT(state, !was_overflow, "`" << te->name << "` overflowed");
                    } else if (te->name == "unchecked_div") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        MIR_ASSERT(state, ty->is_Primitive(), "`" << te->name << "` with non-primitive " << ty);
                        bool was_overflow = doArithChecked(localState, ty, dst, e.args.at(0), ::MIR::eBinOp::DIV, e.args.at(1));
                        MIR_ASSERT(state, !was_overflow, "`" << te->name << "` overflowed");
                    }
                    // `exact_div` is UB if the division results in a non-zero remainder (or if the division overflows)
                    else if (te->name == "exact_div") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        MIR_ASSERT(state, ty->is_Primitive(), "`" << te->name << "` with non-primitive " << ty);
                        bool was_overflow = doArithChecked(localState, ty, dst, e.args.at(0), ::MIR::eBinOp::DIV, e.args.at(1));
                        MIR_ASSERT(state, !was_overflow, "`" << te->name << "` overflowed");
                    }
                    // Saturating operations
                    else if (te->name == "saturating_add") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        MIR_ASSERT(state, ty->is_Primitive(), "`" << te->name << "` with non-primitive " << ty);
                        doArithChecked(localState, ty, dst, e.args.at(0), ::MIR::eBinOp::ADD, e.args.at(1), true);
                    } else if (te->name == "saturating_sub") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        MIR_ASSERT(state, ty->is_Primitive(), "`" << te->name << "` with non-primitive " << ty);
                        doArithChecked(localState, ty, dst, e.args.at(0), ::MIR::eBinOp::SUB, e.args.at(1), true);
                    }
                    // ---
                    else if (te->name == "transmute" || te->name == "transmute_unchecked") {
                        localState.write_param(dst, e.args.at(0));
                    } else if (te->name == "unlikely") {
                        localState.write_param(dst, e.args.at(0));
                    } else if (te->name == "fabsf16" || te->name == "fabsf32" || te->name == "fabsf64" || te->name == "fabsf128") {
                        ::HIR::TypeRef tmp;
                        auto ti = TypeInfo::forType(state.getParamType(tmp, e.args.at(0)));
                        MIR_ASSERT(state, ti.ty == TypeInfo::Float, "`" << te->name << "` with non-float argument");
                        auto bits = localState.readParamUint(ti.bits, e.args.at(0));
                        bits &= ~(U128(1) << (ti.bits - 1));
                        dst.write_uint(state, ti.bits, bits);
                    } else if (te->name == "copysignf16" || te->name == "copysignf32" || te->name == "copysignf64" || te->name == "copysignf128") {
                        ::HIR::TypeRef tmp;
                        auto ti = TypeInfo::forType(state.getParamType(tmp, e.args.at(0)));
                        MIR_ASSERT(state, ti.ty == TypeInfo::Float, "`" << te->name << "` with non-float argument");
                        auto value = localState.readParamUint(ti.bits, e.args.at(0));
                        auto sign = localState.readParamUint(ti.bits, e.args.at(1));
                        auto signMask = U128(1) << (ti.bits - 1);
                        dst.write_uint(state, ti.bits, (value & ~signMask) | (sign & signMask));
                    } else if (te->name == "floorf16" || te->name == "floorf32" || te->name == "floorf64" || te->name == "floorf128") {
                        ::HIR::TypeRef tmp;
                        auto ti = TypeInfo::forType(state.getParamType(tmp, e.args.at(0)));
                        MIR_ASSERT(state, ti.ty == TypeInfo::Float, "`" << te->name << "` with non-float argument");
                        dst.write_float(state, ti.bits, floatValueFloor(localState.readParamFloat(ti.bits, e.args.at(0))));
                    } else if (te->name == "ceilf16" || te->name == "ceilf32" || te->name == "ceilf64" || te->name == "ceilf128") {
                        ::HIR::TypeRef tmp;
                        auto ti = TypeInfo::forType(state.getParamType(tmp, e.args.at(0)));
                        MIR_ASSERT(state, ti.ty == TypeInfo::Float, "`" << te->name << "` with non-float argument");
                        dst.write_float(state, ti.bits, floatValueCeil(localState.readParamFloat(ti.bits, e.args.at(0))));
                    } else if (te->name == "roundf16" || te->name == "roundf32" || te->name == "roundf64" || te->name == "roundf128") {
                        ::HIR::TypeRef tmp;
                        auto ti = TypeInfo::forType(state.getParamType(tmp, e.args.at(0)));
                        MIR_ASSERT(state, ti.ty == TypeInfo::Float, "`" << te->name << "` with non-float argument");
                        dst.write_float(state, ti.bits, floatValueRound(localState.readParamFloat(ti.bits, e.args.at(0))));
                    } else if (te->name == "round_ties_even_f16" || te->name == "round_ties_even_f32" || te->name == "round_ties_even_f64" || te->name == "round_ties_even_f128") {
                        ::HIR::TypeRef tmp;
                        auto ti = TypeInfo::forType(state.getParamType(tmp, e.args.at(0)));
                        MIR_ASSERT(state, ti.ty == TypeInfo::Float, "`" << te->name << "` with non-float argument");
                        dst.write_float(state, ti.bits, floatValueRoundEven(localState.readParamFloat(ti.bits, e.args.at(0))));
                    } else if (te->name == "truncf16" || te->name == "truncf32" || te->name == "truncf64" || te->name == "truncf128") {
                        ::HIR::TypeRef tmp;
                        auto ti = TypeInfo::forType(state.getParamType(tmp, e.args.at(0)));
                        MIR_ASSERT(state, ti.ty == TypeInfo::Float, "`" << te->name << "` with non-float argument");
                        dst.write_float(state, ti.bits, floatValueTrunc(localState.readParamFloat(ti.bits, e.args.at(0))));
                    } else if (te->name == "minnumf16" || te->name == "minnumf32" || te->name == "minnumf64" || te->name == "minnumf128" || te->name == "maxnumf16" || te->name == "maxnumf32" || te->name == "maxnumf64" || te->name == "maxnumf128") {
                        ::HIR::TypeRef tmp;
                        auto ti = TypeInfo::forType(state.getParamType(tmp, e.args.at(0)));
                        MIR_ASSERT(state, ti.ty == TypeInfo::Float, "`" << te->name << "` with non-float argument");
                        auto lhs = localState.readParamFloat(ti.bits, e.args.at(0));
                        auto rhs = localState.readParamFloat(ti.bits, e.args.at(1));
                        bool isMin = te->name == "minnumf16" || te->name == "minnumf32" || te->name == "minnumf64" || te->name == "minnumf128";
                        auto value = isMin ? floatValueMinimumNumber(lhs, rhs) : floatValueMaximumNumber(lhs, rhs);
                        dst.write_float(state, ti.bits, value);
                    } else if (te->name == "assume") {
                        auto val = localState.readParamUint(8, e.args.at(0));
                        MIR_ASSERT(state, val != 0, "`assume` failed");
                    } else if (te->name == "assert_inhabited") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        // TODO: Determine if the type is inhabited (i.e. isn't diverge)
                        bool isUninhabited = resolve.type_is_impossible(state.sp, ty);
                        MIR_ASSERT(state, !isUninhabited, "assert_inhabited " << ty << " failed");
                    }
                    // ---
                    else if (te->name == "const_eval_select") {
                        // "Selects which function to call depending on the context."
                        // `fn const_eval_select<ARG, F, G, RET>(arg: ARG, called_in_const: F, called_at_rt: G ) -> RET`
                        auto argTy = localState.monomorphExpand(te->params.types.at(0));
                        MIR_ASSERT(state, argTy->is_Tuple(), "`" << te->name << "` requires a tuple for ARG, got " << argTy);
                        auto* repr = TargetGetTypeRepr(state.sp, resolve, argTy);
                        if (!repr) {
                            throw Defer();
                        }
                        auto argVal = localState.getLval(e.args.at(0).as_LValue());
                        const auto& fcnArg = e.args.at(1);
                        std::shared_ptr<HIR::Path> fcn_path;

                    TU_MATCH_HDRA( (fcnArg), {)
                    TU_ARMA(LValue, e) {
                                auto fcnVal = localState.getLval(e).readPtr(state);
                                MIR_ASSERT(state, fcnVal.first == EncodedLiteral::PTR_BASE, "");

                                const auto* fcnSr = fcnVal.second.asStaticref();
                                MIR_ASSERT(state, fcnSr, "");
                                fcn_path = std::make_shared<HIR::Path>(fcnSr->path().clone());
                            }
                            TU_ARMA(Borrow, e) {
                                MIR_BUG(state, "Invalid argument for function pointer to `const_eval_select`: " << fcnArg);
                            }
                            TU_ARMA(Constant, e) {
                                if (const auto* ce = e.opt_Function()) {
                                    fcn_path = std::make_shared<HIR::Path>(ms.monomorphPath(state.sp, *ce->p));
                                } else if (const auto* ce = e.opt_ItemAddr()) {
                                    MIR_ASSERT(state, ce->offset == U128(0), "Function pointer has a non-zero offset: " << ce->offset);
                                    fcn_path = std::make_shared<HIR::Path>(ms.monomorphPath(state.sp, **ce));
                                } else {
                                    MIR_BUG(state, "Invalid argument for function pointer to `const_eval_select`: " << fcnArg);
                                }
                            }
                    }

                    // Argument values
                    ::std::vector<AllocationPtr>  callArgs;
                    callArgs.reserve( repr->fields.size() );
                    for(const auto& f : repr->fields) {
                            auto size = localState.sizeOfOrBug(f.ty);
                            callArgs.push_back(AllocationPtr::allocate(localState.value_pool, resolve, state, f.ty));
                            auto vr = ValueRef(callArgs.back());
                            vr.copyFrom(state, argVal.slice(f.offset, size));
                    }

                    if( this->callFunction(localState, e.retVal, std::move(fcn_path), std::move(callArgs)) ) {
                            return TERM_RET_PUSHED;
                    }
                    }
                    // ---
                    else if (te->name == "copy_nonoverlapping") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        size_t elementSize;
                        if (!TargetGetSizeOf(state.sp, resolve, ty, elementSize))
                            throw Defer();
                        auto ptrSrc = localState.getLval(e.args.at(0).as_LValue()).readPtr(state);
                        auto ptrDst = localState.getLval(e.args.at(1).as_LValue()).readPtr(state);
                        U128 count = localState.readParamUint(TargetGetPointerBits(), e.args.at(2));
                        MIR_ASSERT(state, count.isU64(), "Excessive count in `" << te->name << "`");
                        MIR_ASSERT(state, count * elementSize < U128(SIZE_MAX), "Excessive size in `" << te->name << "`");
                        size_t nbytes = elementSize * count.truncateU64();
                        MIR_ASSERT(state, ptrSrc.first >= EncodedLiteral::PTR_BASE, "");
                        MIR_ASSERT(state, ptrDst.first >= EncodedLiteral::PTR_BASE, "");
                        auto vr_src = ValueRef(ptrSrc.second, ptrSrc.first - EncodedLiteral::PTR_BASE).slice(0, nbytes);
                        auto vr_dst = ValueRef(ptrDst.second, ptrDst.first - EncodedLiteral::PTR_BASE).slice(0, nbytes);
                        vr_dst.copyFrom(state, vr_src);
                    } else if (te->name == "offset") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0)->as_Pointer().inner);
                        size_t elementSize;
                        if (!TargetGetSizeOf(state.sp, resolve, ty, elementSize))
                            throw Defer();
                        auto ptrPair = localState.readParamPtr(e.args.at(0));
                        auto ofs = localState.readParamUint(TargetGetPointerBits(), e.args.at(1));
                        dst.write_ptr(state, ptrPair.first + ofs.truncateU64() * elementSize, ptrPair.second);
                    }
                    // `arith_offset` is the wrapping form of `offset`; identical arithmetic here, and the type parameter is the *pointee*.
                    else if (te->name == "arith_offset") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        size_t elementSize;
                        if (!TargetGetSizeOf(state.sp, resolve, ty, elementSize))
                            throw Defer();
                        auto ptrPair = localState.readParamPtr(e.args.at(0));
                        auto ofs = localState.readParamUint(TargetGetPointerBits(), e.args.at(1));
                        dst.write_ptr(state, ptrPair.first + ofs.truncateU64() * elementSize, ptrPair.second);
                    }
                    // Returns 1/0/2 (equal / not equal / unknown). Only answer definitively for pointers sharing a relocation; different allocations report 2.
                    else if (te->name == "ptr_guaranteed_cmp") {
                        auto a = localState.readParamPtr(e.args.at(0));
                        auto b = localState.readParamPtr(e.args.at(1));
                        uint8_t rv = (a.second == b.second) ? (a.first == b.first ? 1 : 0) : 2;
                        dst.write_uint(state, 8, rv);
                    } else if (te->name == "write_bytes") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        size_t elementSize;
                        if (!TargetGetSizeOf(state.sp, resolve, ty, elementSize))
                            throw Defer();
                        auto ptrDst = localState.getLval(e.args.at(0).as_LValue()).readPtr(state);
                        auto val = localState.readParamUint(8, e.args.at(1));
                        U128 count = localState.readParamUint(TargetGetPointerBits(), e.args.at(2));
                        MIR_ASSERT(state, count.isU64(), "Excessive count in `" << te->name << "`");
                        MIR_ASSERT(state, count * elementSize < U128(SIZE_MAX), "Excessive size in `" << te->name << "`");
                        size_t nbytes = elementSize * count.truncateU64();
                        MIR_ASSERT(state, ptrDst.first >= EncodedLiteral::PTR_BASE, "");
                        ValueRef vr_dst = ValueRef(ptrDst.second, ptrDst.first - EncodedLiteral::PTR_BASE).slice(0, nbytes);
                        memset(vr_dst.extWriteBytes(state, nbytes), val.truncateU64(), nbytes);
                    }
                    // Innards of `core::ptr::read` (on 1.90+)
                    else if (te->name == "read_via_copy") {
                        auto ptrSrc = localState.readParamPtr(e.args.at(0));
                        auto vr_src = ValueRef(ptrSrc.second, ptrSrc.first - EncodedLiteral::PTR_BASE);
                        dst.copyFrom(state, vr_src);
                    } else if (te->name == "discriminant_value") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        if (!(ty->is_Path() && ty->as_Path().binding.is_Enum())) {
                            dst.write_uint(state, dst.getLen() * 8, U128(0));
                        } else {
                            const auto* repr = TargetGetTypeRepr(state.sp, resolve, ty);
                            if (!repr) {
                                throw Defer();
                            }

                            ValueRef value;
                            if (const auto* arg = e.args.at(0).opt_Borrow()) {
                                value = localState.getLval(arg->val);
                            } else {
                                auto ptr = localState.readParamPtr(e.args.at(0));
                                MIR_ASSERT(state, ptr.first >= EncodedLiteral::PTR_BASE, "Null pointer passed to `discriminant_value`");
                                value = ValueRef(ptr.second, ptr.first - EncodedLiteral::PTR_BASE);
                            }

                        TU_MATCH_HDRA( (repr->variants), { )
                        TU_ARMA(None, ve) {
                                    dst.write_uint(state, dst.getLen() * 8, U128(0));
                                }
                                TU_ARMA(Linear, ve) {
                                    const auto ofs = repr->getOffset(state.sp, resolve, ve.field);
                                    auto tag = value.slice(ofs, ve.field.size).readUint(state, ve.field.size * 8);
                                    const auto variant = tag < U128(ve.offset) ? ve.field.index : (tag - U128(ve.offset)).truncateU64();
                                    dst.write_uint(state, dst.getLen() * 8, U128(variant));
                                }
                                TU_ARMA(Values, ve) {
                                    const auto ofs = repr->getOffset(state.sp, resolve, ve.field);
                                    const auto& tagTy = TargetGetInnerType(state.sp, resolve, *repr, ve.field.index, ve.field.subFields);
                                    const auto tagInfo = TypeInfo::forType(tagTy);
                                    MIR_ASSERT(state, tagInfo.ty == TypeInfo::Signed || tagInfo.ty == TypeInfo::Unsigned, "Non-integer enum tag " << tagTy);
                                    auto tag = value.slice(ofs, ve.field.size);
                                    if (tagInfo.ty == TypeInfo::Signed) {
                                        dst.write_sint(state, dst.getLen() * 8, tag.readSint(state, tagInfo.bits));
                                    } else {
                                        dst.write_uint(state, dst.getLen() * 8, tag.readUint(state, tagInfo.bits));
                                    }
                                }
                                TU_ARMA(NonZero, ve) {
                                    const auto ofs = repr->getOffset(state.sp, resolve, ve.field);
                                    bool isNonzero = false;
                                    for (size_t i = 0; i < ve.field.size; i++) {
                                        isNonzero |= value.slice(ofs + i, 1).readUint(state, 8) != U128(0);
                                    }
                                    const auto variant = isNonzero ? 1 - ve.zero_variant : ve.zero_variant;
                                    dst.write_uint(state, dst.getLen() * 8, U128(variant));
                                }
                        }
                        }
                    } else if (te->name == "variant_count") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        MIR_ASSERT(state, ty->is_Path(), "`variant_count` on non-enum - " << ty);
                        MIR_ASSERT(state, ty->as_Path().binding.is_Enum(), "`variant_count` on non-enum - " << ty);
                        const auto* enm = ty->as_Path().binding.as_Enum();
                        dst.write_uint(state, TargetGetPointerBits(), enm->numVariants());
                    } else if (te->name == "assert_zero_valid") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        MIR_ASSERT(state, !ty->is_Borrow(), "`assert_zero_valid`: Borrow cannot be zero");
                        // TODO: Other cases?
                    } else if (te->name == "is_val_statically_known") {
                        dst.write_uint(state, 8, e.args.at(0).is_Constant() || e.args.at(0).is_Borrow());
                    } else {
                        MIR_TODO(state, "Call intrinsic \"" << te->name << "\" - " << terminator);
                    }
                    DEBUG("> E" << this->evalIndex << " F" << localState.frameIndex << " " << e.retVal << " := " << dst);
                    return e.retBlock;
                } else if (const auto* te = e.fcn.opt_Path()) {
                    const auto& fcnpRaw = *te;
                    DEBUG("ms=" << ms);
                    auto fcnp = std::make_shared<HIR::Path>(ms.monomorphPath(state.sp, fcnpRaw));

                    // Argument values
                    ::std::vector<AllocationPtr> callArgs;
                    callArgs.reserve(e.args.size());
                    for (const auto& a : e.args) {
                        ::HIR::TypeRef tmp;
                        const auto& ty = state.getParamType(tmp, a);
                        callArgs.push_back(AllocationPtr::allocate(localState.value_pool, resolve, state, ty));
                        auto vr = ValueRef(callArgs.back());
                        localState.write_param(vr, a);
                    }

                    if (this->callFunction(localState, e.retVal, std::move(fcnp), std::move(callArgs))) {
                        return TERM_RET_PUSHED;
                    } else {
                        auto dst = localState.getLval(e.retVal);
                        DEBUG("> E" << this->evalIndex << " F" << localState.frameIndex << " " << e.retVal << " := " << dst);
                        return e.retBlock;
                    }
                } else {
                    MIR_BUG(state, "Unexpected terminator - " << terminator);
                }
            }
        }
        throw std::runtime_error("Unreachable?");
    }

    /// @brief Call a function (handling `Fn*` traits, and struct/enum constructors)
    /// @param local_state
    /// @param rv_slot
    /// @param fcn_path
    /// @param call_args
    /// @return `true` is a new stack frame was pushed
    bool Evaluator::callFunction(CallStackEntry& localState, const MIR::LValue& rvSlot, ::std::shared_ptr<HIR::Path> fcn_path, ::std::vector<AllocationPtr> callArgs) {
        const auto& state = localState.state;
        MonomorphState fcnMs(resolve.crate.types);
        const ::HIR::GenericParams* implParamsDef = nullptr;

        const auto* pathP = fcn_path.get();
        if (const auto* e = pathP->mData.opt_UfcsKnown()) {
            if (e->type->is_Function() || e->type->is_NamedFunction()) {
                if (e->trait.mPath == resolve.mLangFn || e->trait.mPath == resolve.mLangFnMut || e->trait.mPath == resolve.mLangFnOnce) {
                    if (const auto* nf = e->type->opt_NamedFunction()) {
                        pathP = &nf->path;
                    } else {
                        MIR_TODO(localState.state, "Get function from fn-ptr - " << e->type);
                    }
                    // TODO: Convert `call_args` - discard the first and extract tuple from the second
                    const auto& argTupleTy = e->trait.mParams.types.at(0);
                    const auto* argTupleRepr = TargetGetTypeRepr(state.sp, state.mResolve, argTupleTy);
                    auto argTupleV = std::move(callArgs.at(1));
                    ValueRef argTuple(argTupleV);
                    callArgs.clear();
                    callArgs.reserve(argTupleRepr->fields.size());
                    for (const auto& fld : argTupleRepr->fields) {
                        auto size = localState.sizeOfOrBug(fld.ty);
                        callArgs.push_back(AllocationPtr::allocate(localState.value_pool, state.mResolve, state, fld.ty));
                        auto vr = ValueRef(callArgs.back());
                        vr.copyFrom(state, argTuple.slice(fld.offset, size));
                    }
                } else {
                    // Ignore: Not a fn trait
                }
            }
        }
        const auto& path = *pathP;

        if (requireConstCalls) if (const auto* e = path.mData.opt_UfcsKnown()) {
            const auto& trait = resolve.crate.getTraitByPath(state.sp, e->trait.mPath);
            if (trait.isConst) {
                ImplRef bestImpl;
                bool hasConstBound = false;
                resolve.findImpl(state.sp, e->trait.mPath, e->trait.mParams, e->type, [&](ImplRef impl, bool isFuzzed) {
                    if (isFuzzed) {
                        return false;
                    }
                    if (!impl.mData.is_TraitImpl()) {
                        hasConstBound |= impl.boundConstness() != HIR::BoundConstness::Never;
                        return false;
                    }
                    if (!bestImpl.isValid() || impl.moreSpecificThan(resolve.crate.types, bestImpl)) {
                        bestImpl = mv$(impl);
                    }
                    return false;
                });
                MIR_ASSERT(state, hasConstBound || bestImpl.isValid(), "const trait call did not resolve to an impl: " << path);
                MIR_ASSERT(state, hasConstBound || bestImpl.mData.as_TraitImpl().impl->isConst, "const trait call requires a const impl: " << path);
            }
        }

        auto rv = getEntFullpath(localState.state.sp, resolve, path, EntNS::Value, fcnMs, &implParamsDef);
        if (const auto* fcnP = rv.opt_Function()) {
            const HIR::Function& fcn = **fcnP;
            const auto& ep = fcn.mCode;
            if (ep && ep.state->stage < ::HIR::ExprState::Stage::ConstEval) {
                auto prev = ep.state->stage;
                ep.state->stage = ::HIR::ExprState::Stage::ConstEvalRequest;
                // Run consteval on the arguments and return type
                ConvertHIRConstantEvaluateFcnSig(resolve.crate, implParamsDef, path, const_cast<HIR::Function&>(fcn));
                ep.state->stage = prev;
            }

            DEBUG("Call function " << *fcn_path << ": fcn_ms=" << fcnMs);

            // TODO: Set m_const during parse and check here
            if (!fcn.mCode && !fcn.mCode.mir) {
                if (fcn.linkage.name == "") {
                } else if (fcn.linkage.name == "panic_impl") {
                    MIR_TODO(state, "panic in constant evaluation");
                } else {
                    MIR_TODO(state, "Call extern function `" << fcn.linkage.name << "` (" << *fcn_path << ")");
                }
            }

            // Call by invoking evaluate_constant on the function
            const auto* mir = this->resolve.crate.getOrGenMir(::HIR::ItemPath(*fcn_path), fcn);
            MIR_ASSERT(state, mir, "No MIR for function " << *fcn_path);

            // Monomorphised argument types
            ::HIR::Function::argsT argDefs;
            for (const auto& a : fcn.mArgs) {
                argDefs.push_back(::std::make_pair(::HIR::Pattern(), this->resolve.monomorphExpand(this->root_span, a.second, fcnMs)));
            }
            auto ret_ty = this->resolve.monomorphExpand(this->root_span, fcn.returnType, fcnMs);

            pushStackEntry(
                ::FmtLambda([=](std::ostream& os) {
                os << *fcn_path;
            }),
                *mir,
                std::move(fcnMs),
                std::move(ret_ty),
                ::std::move(argDefs),
                std::move(callArgs),
                &fcn.mParams,
                implParamsDef
            );
            return true;
        } else if (rv.is_NotFound() && monomorphisePathNeeded(path, true)) {
            throw Defer();
        } else if (rv.is_Struct()) {
            // Set destination, same way as `RValue::Struct` does
            auto dst = localState.getLval(rvSlot);

            ::HIR::TypeRef tmp;
            const auto& ty = state.getLvalueType(tmp, rvSlot);
            auto* repr = TargetGetTypeRepr(state.sp, resolve, ty);
            if (!repr) {
                throw Defer();
            }
            MIR_ASSERT(state, repr->fields.size() == callArgs.size(), "");
            for (size_t i = 0; i < callArgs.size(); i++) {
                size_t sz = localState.sizeOfOrBug(repr->fields[i].ty);
                auto localDst = dst.slice(repr->fields[i].offset, sz);
                localDst.copyFrom(state, ValueRef(callArgs[i]));
                DEBUG("@" << repr->fields[i].offset << " = " << localDst);
            }
            return false;
        } else {
            MIR_TODO(state, "Could not find function for " << path << " - " << rv.tagStr());
        }
    }

    EncodedLiteral Evaluator::allocationToEncoded(const ::HIR::TypeData* ty, const ::MIR::eval::Allocation& a) {
        //const auto* a_bytes = a.get_bytes(0, a.size(), true);
        const auto* aBytes = a.getBytes(0, a.size(), false); // NOTE: Read the uninitialised bytes (they _should_ be zeroes)
        ASSERT_BUG(this->root_span, aBytes, "Unable to get entire allocation - " << FMT_CB(ss, a.fmt(ss, 0, a.size())));
        EncodedLiteral rv;
        rv.bytes.insert(rv.bytes.begin(), aBytes, aBytes + a.size());
        for (const auto& r : a.getRelocations()) {
            if (const auto* innerAlloc = r.ptr.asAllocation()) {
                // Create a new static
                if (innerAlloc->isWritable()) {
                    auto innerVal = allocationToEncoded(innerAlloc->getType(), *innerAlloc);

                    // Clone type with all lifetimes set to `'static`
                    struct M: MonomorphiserNop {
                        using MonomorphiserNop::MonomorphiserNop;

                        ::HIR::LifetimeRef monomorphLifetime(const Span& sp, const ::HIR::LifetimeRef& tpl) const override {
                            return ::HIR::LifetimeRef::new_static();
                        }
                    };

                    auto itemPath = nvs.new_static(M(resolve.crate.types).monomorphType(Span(), innerAlloc->getType()), mv$(innerVal));

                    rv.relocations.push_back(Reloc::newNamed(r.offset, TargetGetPointerBits() / 8, mv$(itemPath)));
                } else {
                    // string
                    auto size = innerAlloc->size();
                    auto ptr = innerAlloc->getBytes(0, size, true);
                    rv.relocations.push_back(Reloc::newBytes(r.offset, TargetGetPointerBits() / 8, ::std::string(ptr, ptr + size)));
                }
            } else if (const auto* sr = r.ptr.asStaticref()) {
                // Just emit a path
                rv.relocations.push_back(Reloc::newNamed(r.offset, TargetGetPointerBits() / 8, sr->path().clone()));
            } else if (const auto* c = r.ptr.asConstant()) {
                // string
                auto size = c->size();
                auto ptr = c->getBytes(0, size, true);
                rv.relocations.push_back(Reloc::newBytes(r.offset, TargetGetPointerBits() / 8, ::std::string(ptr, ptr + size)));
            } else {
                BUG(this->root_span, "");
            }
        }
        return rv;
    }

    EncodedLiteral Evaluator::evaluateConstant(const ::HIR::ItemPath& ip, const ::HIR::ExprPtr& expr, ::HIR::TypeRef exp) {
        return evaluateConstant(ip, expr, exp, MonomorphState(resolve.crate.types));
    }

    EncodedLiteral Evaluator::evaluateConstant(const ::HIR::ItemPath& ip, const ::HIR::ExprPtr& expr, ::HIR::TypeRef exp, MonomorphState ms) {
        TRACE_FUNCTION_F(ip);
        DEBUG("ms = " << ms);
        const auto* mir = this->resolve.crate.getOrGenMir(ip, expr, exp);

        // Generating MIR can define a local type-alias `impl Trait`.  CTFE
        // operates on the revealed representation, just like rustc's
        // reveal-all evaluation environment, so use that hidden type for the
        // result allocation and encoding instead of asking layout for an
        // erased type.
        if (const auto* erased = exp->opt_ErasedType()) {
            if (const auto* alias = erased->inner.opt_Alias()) {
                if (alias->inner->type != ::HIR::TypeRef()) {
                    exp = MonomorphStatePtr(
                        resolve.crate.types,
                        nullptr,
                        &alias->params,
                        nullptr
                    ).monomorphType(expr.span(), alias->inner->type);
                    resolve.expandAssociatedTypes(expr.span(), exp);
                }
            }
        }

        if (mir) {
            ASSERT_BUG(Span(), expr.state, "");
            if (!resolve.itemGenerics && !resolve.implGenerics) {
                resolve.setBothGenericsRaw(expr.state->implGenerics, expr.state->itemGenerics);
            }
        }

        // If `ms` is empty, but `resolve` has impl/item generics, then re-make `ms` as a nop set of params
        // - This is a lazy hack, isntead of doing this creation in the caller
        ::HIR::PathParams nopParamsImpl;
        ::HIR::PathParams nopParamsMethod;
        if (!ms.ppImpl && !ms.ppMethod) {
            if (resolve.itemGenerics) {
                ms.ppMethod = &(nopParamsMethod = resolve.itemGenerics->makeNopParams(resolve.crate.types, 1));
            }
            if (resolve.implGenerics) {
                ms.ppImpl = &(nopParamsImpl = resolve.implGenerics->makeNopParams(resolve.crate.types, 0));
            }
            DEBUG("(was empty) ms = " << ms);
        }

        if (mir) {
            // Might want to have a fully-populated MonomorphState for expanding inside impl blocks
            // HACK: Generate a roughly-correct one
            const auto& topIp = ip.getTopIp();
            if (topIp.trait && !topIp.ty) {
                ms.self_ty = resolve.crate.types.self();
            }

            assert(this->callStack.empty());
            this->numFrames = 0;
            // Note: Since this is the entrypoint, `this->resolve` has the correct GenericParams
            this->pushStackEntry(FMT_CB(os, os << ip), *mir, std::move(ms), std::move(exp), {}, {}, resolve.itemGenerics, resolve.implGenerics);
            auto rvRaw = this->runUntilStackEmpty();

            ASSERT_BUG(this->root_span, rvRaw, "evaluate_constant_mir returned null allocation");
            DEBUG(ip << " = " << ::MIR::eval::ValueRef(rvRaw));

            return this->allocationToEncoded(exp, *rvRaw);
        } else {
            BUG(this->root_span, "Attempting to evaluate constant expression with no associated code");
        }
    }
} // namespace HIR

namespace {
    struct Expander: public ::HIR::Visitor {
        const ::HIR::Crate& crate;
        const ::HIR::Module* mMod;
        const ::HIR::ItemPath* modPath;
        MonomorphState monomorphState;
        bool recurseTypes;

        const ::HIR::GenericParams* implParams;
        const ::HIR::GenericParams* itemParams;

        std::function<const ::HIR::GenericParams&(const Span& sp)> getParams;

        enum class Pass {
            OuterOnly,
            Values,
        } pass;

        Expander(const ::HIR::Crate& crate)
            : ::HIR::Visitor(nullptr, crate.types)
            , crate(crate)
            , mMod(nullptr)
            , modPath(nullptr)
            , monomorphState(crate.types)
            , recurseTypes(false)
            , implParams(nullptr)
            , itemParams(nullptr)
            , pass(Pass::OuterOnly)
        {
        }

        ::HIR::Evaluator getEval(const Span& sp, NewvalState& nvs) const {
            auto eval = ::HIR::Evaluator{sp, crate, nvs};
            eval.setRequireConstCalls();
            eval.resolve.setBothGenericsRaw(implParams, itemParams);
            return eval;
        }

        ::HIR::PathParams getParamsForDef(const ::HIR::GenericParams& tpl, bool isFunctionLevel = false) const {
            return tpl.makeNopParams(crate.types, isFunctionLevel ? 1 : 0);
        }

        void visit_module(::HIR::ItemPath p, ::HIR::Module& mod) override {
            auto savedMp = modPath;
            auto savedM = mMod;
            mMod = &mod;
            modPath = &p;

            ::HIR::Visitor::visit_module(p, mod);

            mMod = savedM;
            modPath = savedMp;
        }

        void visit_function(::HIR::ItemPath p, ::HIR::Function& f) override {
            TRACE_FUNCTION_F(p);

            auto ppFcn = getParamsForDef(f.mParams, true);
            monomorphState.ppMethod = &ppFcn;
            itemParams = &f.mParams;
            ::HIR::Visitor::visit_function(p, f);
            itemParams = nullptr;
            monomorphState.ppMethod = nullptr;
        }

        void visit_trait_impl(const ::HIR::SimplePath& trait_path, ::HIR::TraitImpl& impl) override {
            static Span sp;
            TRACE_FUNCTION_F("impl" << impl.mParams.fmtArgs() << " " << trait_path << impl.traitArgs << " for " << impl.mType);

            auto mp = ::HIR::ItemPath(impl.srcModule);
            modPath = &mp;
            mMod = &crate.getModByPath(sp, impl.srcModule);

            auto ppImpl = getParamsForDef(impl.mParams);
            monomorphState.ppImpl = &ppImpl;
            implParams = &impl.mParams;

            ::HIR::Visitor::visit_trait_impl(trait_path, impl);

            assert(implParams);
            implParams = nullptr;
            monomorphState.ppImpl = nullptr;

            mMod = nullptr;
            modPath = nullptr;
        }

        void visit_type_impl(::HIR::TypeImpl& impl) override {
            static Span sp;
            TRACE_FUNCTION_F("impl" << impl.mParams.fmtArgs() << " " << impl.mType);

            auto mp = ::HIR::ItemPath(impl.srcModule);
            modPath = &mp;
            mMod = &crate.getModByPath(sp, impl.srcModule);

            auto ppImpl = getParamsForDef(impl.mParams);
            monomorphState.ppImpl = &ppImpl;
            implParams = &impl.mParams;

            ::HIR::Visitor::visit_type_impl(impl);

            assert(implParams);
            implParams = nullptr;
            monomorphState.ppImpl = nullptr;

            mMod = nullptr;
            modPath = nullptr;
        }

        void visit_inherent_type(::HIR::ItemPath p, ::HIR::TypeAlias& item) override {
            auto ppItem = getParamsForDef(item.mParams, true);
            monomorphState.ppMethod = &ppItem;
            itemParams = &item.mParams;
            ::HIR::Visitor::visit_inherent_type(p, item);
            itemParams = nullptr;
            monomorphState.ppMethod = nullptr;
        }

        void visit_trait(::HIR::ItemPath ip, ::HIR::Trait& trait) override {
            auto ppImpl = getParamsForDef(trait.mParams);
            monomorphState.self_ty = crate.types.self();
            monomorphState.ppImpl = &ppImpl;
            implParams = &trait.mParams;

            ::HIR::Visitor::visit_trait(ip, trait);

            assert(implParams);
            implParams = nullptr;
            monomorphState.ppImpl = nullptr;
        }

        void evalulateConstGeneric(const Span& sp, const ::HIR::TypeData* ty, ::HIR::ConstGeneric& v) {
            if (v.is_Unevaluated()) {
                try {
                    v = ::HIR::ConstGeneric::make_Evaluated(evaluateConstgeneric(sp, crate, ty, *v.as_Unevaluated()));
                } catch (const Defer&) {
                    // Deferred - no update
                }
            }
        }

        void visit_path_params(::HIR::PathParams& p) override {
            static Span sp;
            for (auto& v : p.values) {
                if (v.is_Unevaluated()) {
                    try {
                        const auto& paramsDef = getParams(sp);
                        auto idx = static_cast<size_t>(&v - &p.values.front());
                        ASSERT_BUG(sp, idx < paramsDef.values.size(), "");
                        const auto& ty = paramsDef.values[idx].mType;
                        ASSERT_BUG(sp, !monomorphiseTypeNeeded(ty), "" << ty);
                        evalulateConstGeneric(sp, ty, v);
                    } catch (const Defer&) {
                        // Deferred - no update
                    }
                }
            }
            ::HIR::Visitor::visit_path_params(p);
        }

        void visit_params(::HIR::GenericParams& params) override {
            static Span sp;
            for (auto& v : params.values) {
                evalulateConstGeneric(sp, v.mType, v.defaultValue);
            }
            HIR::Visitor::visit_params(params);
        }

        void visit_generic_path(::HIR::GenericPath& p, ::HIR::Visitor::PathContext pc) override {
            TRACE_FUNCTION_FR(p, p);
            auto saved = getParams;
            getParams = [&](const Span& sp) -> const ::HIR::GenericParams& {
                DEBUG("visit_generic_path[m_get_params] " << p);
                switch (pc) {
                    case ::HIR::Visitor::PathContext::VALUE: {
                        auto& vi = crate.getValitemByPath(sp, p.mPath);
                    TU_MATCH_HDRA( (vi), { )
                    TU_ARMA(Import, e)  BUG(sp, "Module Import");
                            TU_ARMA(Static, e) BUG(sp, "Getting params definition for Static - " << p);
                            TU_ARMA(Constant, e) return e.mParams;
                            TU_ARMA(Function, e) return e.mParams;
                            TU_ARMA(StructConstant, e) return crate.getStructByPath(sp, e.ty).mParams;
                            TU_ARMA(StructConstructor, e) return crate.getStructByPath(sp, e.ty).mParams;
                    }
                    break;
                    }
                    case ::HIR::Visitor::PathContext::TYPE:
                    case ::HIR::Visitor::PathContext::TRAIT: {
                        auto& vi = crate.getTypeitemByPath(sp, p.mPath);
                    TU_MATCH_HDRA( (vi), { )
                    TU_ARMA(Import, e)  BUG(sp, "Module Import");
                            TU_ARMA(Module, e) BUG(sp, "mod - " << p);
                            TU_ARMA(TypeAlias, e) BUG(sp, "type - " << p);
                            TU_ARMA(TraitAlias, e) BUG(sp, "trait= - " << p);
                            TU_ARMA(Struct, e) return e.mParams;
                            TU_ARMA(Enum, e) return e.mParams;
                            TU_ARMA(Union, e) return e.mParams;
                            TU_ARMA(Trait, e) return e.mParams;
                            TU_ARMA(ExternType, e) BUG(sp, "extern type - " << p);
                    }
                    break;
                    }
                }
                TODO(sp, "visit_generic_path[m_get_params] - " << p);
            };
            ::HIR::Visitor::visit_generic_path(p, pc);
            getParams = saved;
        }

        void visit_path(::HIR::Path& p, ::HIR::Visitor::PathContext pc) override {
            auto saved = getParams;
            getParams = [&](const Span& sp) -> const ::HIR::GenericParams& {
                DEBUG("visit_path[m_get_params] " << p);
                StaticTraitResolve resolve(crate);
                resolve.setBothGenericsRaw(implParams, itemParams);
                switch (pc) {
                    case ::HIR::Visitor::PathContext::VALUE: {
                        MonomorphState unused(crate.types);
                        auto vi = resolve.getValue(sp, p, unused, true);
                    TU_MATCH_HDRA( (vi), {)
                    TU_ARMA(NotFound, e)
                        BUG(sp, "NotFound");
                            TU_ARMA(NotYetKnown, e)
                            TODO(sp, "NotYetKnown");
                            TU_ARMA(Static, e) return e->mParams;
                            TU_ARMA(Constant, e) return e->mParams;
                            TU_ARMA(Function, e) return e->mParams;
                            TU_ARMA(EnumConstructor, e)
                            TODO(sp, "Handle EnumConstructor - " << p);
                            TU_ARMA(EnumValue, e)
                            TODO(sp, "Handle EnumValue - " << p);
                            TU_ARMA(StructConstructor, e)
                            TODO(sp, "Handle StructConstructor - " << p);
                            TU_ARMA(StructConstant, e)
                            TODO(sp, "Handle StructConstant - " << p);
                    }
                    break;
                    }
                    case ::HIR::Visitor::PathContext::TYPE:
                    case ::HIR::Visitor::PathContext::TRAIT: {
                        //const auto& vi = tr.m_types.at(pe->item);
                        BUG(sp, "type - " << p);
                        break;
                    }
                }
                TODO(sp, "visit_path[m_get_params] - " << p);
            };
            ::HIR::Visitor::visit_path(p, pc);
            getParams = saved;
        }

        void visit_arraysize(::HIR::ArraySize& as, std::string name) {
            if (as.is_Unevaluated() && as.as_Unevaluated().is_Unevaluated()) {
                TRACE_FUNCTION_FR(as, as);
                const auto& expr_ptr = *as.as_Unevaluated().as_Unevaluated()->expr;

                auto nvs = NewvalState{*mMod, *modPath, name};
                auto eval = getEval(expr_ptr->span(), nvs);
                try {
                    auto val = eval.evaluateConstant(*modPath + name, expr_ptr, crate.types.primitive(::HIR::CoreType::Usize), monomorphState.clone());
                    as = val.readUsize(0);
                    //DEBUG("Array size = " << as);
                } catch (const Defer&) {
                    const auto* tn = cast<const HIR::ExprNodeConstParam>(&*expr_ptr);
                    if (tn) {
                        as = HIR::ConstGeneric(HIR::GenericRef(tn->mName, tn->mBinding));
                    } else {
                        //TODO(expr_ptr->span(), "Handle defer for array sizes");
                    }
                }
            } else {
                DEBUG("Array size (known) = " << as);
            }
        }

        void visit_type(::HIR::TypeRef& ty) override {
            ::HIR::Visitor::visit_type(ty);

            if (ty->is_Array()) {
                auto data = ty->cloneData();
                auto& e = data.as_Array();
                TRACE_FUNCTION_FR(ty, ty);
                visit_arraysize(e.size, FMT("ty_" << &e << "#"));
                ty = crate.types.intern(mv$(data));
            }

            if (recurseTypes) {
                recurseTypes = false;
                if (const auto* te = ty->opt_Path()) {
                    TU_MATCH_HDRA( (te->binding), {)
                    TU_ARMA(Unbound, _) {
                        }
                        TU_ARMA(Opaque, _) {
                        }
                        TU_ARMA(Struct, pbe) {
                            // If this struct hasn't been visited already, visit it
                            auto savedIp = implParams;
                            implParams = nullptr;
                            this->visit_struct(te->path.mData.as_Generic().mPath, const_cast<::HIR::Struct&>(*pbe));
                            implParams = savedIp;
                        }
                        TU_ARMA(Union, pbe) {
                        }
                        TU_ARMA(Enum, pbe) {
                        }
                        TU_ARMA(ExternType, pbe) {
                        }
                    }
                }
                recurseTypes = true;
            }
        }

        void visit_constant(::HIR::ItemPath p, ::HIR::Constant& item) override {
            TRACE_FUNCTION_F(p);
            itemParams = &item.mParams;

            recurseTypes = true;
            ::HIR::Visitor::visit_constant(p, item);
            recurseTypes = false;

            // NOTE: Consteval needed here for MIR match generation to work
            if (pass != Pass::Values) {
            } else if (item.mValue || item.mValue.mir) {
                auto nvs = NewvalState{*mMod, *modPath, FMT(p.getName() << "#")};
                auto eval = getEval(item.mValue.span(), nvs);
                try {
                    item.valueRes = eval.evaluateConstant(p, item.mValue, item.mType, monomorphState.clone());
                    //check_lit_type(item.m_value.span(), item.m_type, item.m_value_res);
                    item.valueState = ::HIR::Constant::ValueState::Known;
                } catch (const Defer&) {
                    item.valueState = ::HIR::Constant::ValueState::Generic;
                }

                DEBUG("constant: " << item.mType << " = " << item.valueRes);
            } else {
                DEBUG("constant?"); // " << *item.m_value);
            }

            itemParams = nullptr;
        }

        void visit_static(::HIR::ItemPath p, ::HIR::Static& item) override {
            TRACE_FUNCTION_F(p);
            itemParams = &item.mParams;

            recurseTypes = true;
            ::HIR::Visitor::visit_static(p, item);
            recurseTypes = false;

            if (pass != Pass::Values) {
            } else if (item.mValue) {
                auto nvs = NewvalState{*mMod, *modPath, FMT(p.getName() << "#")};
                auto eval = getEval(item.mValue.span(), nvs);
                try {
                    item.valueRes = eval.evaluateConstant(p, item.mValue, item.mType);
                    item.valueGenerated = true;
                } catch (const Defer&) {
                    ERROR(item.mValue->span(), E0000, "Defer top-level static?");
                }

                DEBUG("static: " << item.mType << " = " << item.valueRes);
            }

            itemParams = nullptr;
        }

        void visit_enum(::HIR::ItemPath p, ::HIR::Enum& item) override {
            static Span sp;
            assert(!implParams);
            implParams = &item.mParams;

            visit_enum_inner(crate, p, *mMod, *modPath, p.getName(), item);
            ::HIR::Visitor::visit_enum(p, item);

            assert(implParams);
            implParams = nullptr;
        }

        void visit_struct(::HIR::ItemPath p, ::HIR::Struct& item) override {
            assert(!implParams);
            implParams = &item.mParams;
            if (item.constEvalState != HIR::ConstEvalState::Complete) {
                ASSERT_BUG(Span(), item.constEvalState == HIR::ConstEvalState::None, "Constant evaluation loop involving " << p);
                item.constEvalState = HIR::ConstEvalState::Active;
                ::HIR::Visitor::visit_struct(p, item);
                item.constEvalState = HIR::ConstEvalState::Complete;
            }
            assert(implParams);
            implParams = nullptr;
        }

        void visit_expr(::HIR::ExprPtr& expr) override {
            struct Visitor: public ::HIR::ExprVisitorDef {
                Expander& mExp;

                Visitor(Expander& exp)
                    : ::HIR::ExprVisitorDef(exp.crate.types)
                    , mExp(exp)
                {
                }

                void visit_type(::HIR::TypeRef& ty) override {
                    // Need to evaluate array sizes
                    DEBUG("expr type " << ty);
                    mExp.visit_type(ty);
                }

                void visit_path_params(::HIR::PathParams& pp) override {
                    // Explicit call to handle const params (eventually)
                    mExp.visit_path_params(pp);
                }

                void visit_path(::HIR::Visitor::PathContext pc, ::HIR::Path& p) override {
                    mExp.visit_path(p, pc);
                }

                void visit_generic_path(::HIR::Visitor::PathContext pc, ::HIR::GenericPath& p) override {
                    // `visit_path_params` relies on `m_get_params` being set by the enclosing path visitor; without this override a generic path in an expression reaches it empty.
                    mExp.visit_generic_path(p, pc);
                }

                void visit(::HIR::ExprNodeCallMethod& node) override {
                    auto saved = mExp.getParams;
                    mExp.getParams = [&](const Span& sp) -> const ::HIR::GenericParams& {
                        DEBUG("visit(ExprNodeCallMethod)[m_get_params] Defer until after main typecheck");
                        throw Defer();
                    };
                    ::HIR::ExprVisitorDef::visit(node);
                    mExp.getParams = std::move(saved);
                }

                void visit(::HIR::ExprNodeArraySized& node) override {
                    ::HIR::ExprVisitorDef::visit(node);
                    mExp.visit_arraysize(node.mSize, FMT("array_" << &node << "#"));
                }
            };

            if (expr.get() != nullptr) {
                Visitor v{*this};
                //m_recurse_types = true;
                (*expr).visit(v);
                //m_recurse_types = false;
            }
        }

        static void visit_enum_inner(const ::HIR::Crate& crate, const ::HIR::ItemPath& p, const ::HIR::Module& mod, const ::HIR::ItemPath& mod_path, const char* name, ::HIR::Enum& item) {
            if (item.discriminantsEvaluated) {
                return;
            }
            auto ty = ::HIR::Enum::getReprType(item.tagRepr);
            bool is_signed = false;
            switch (ty) {
                case ::HIR::CoreType::I8:
                case ::HIR::CoreType::I16:
                case ::HIR::CoreType::I32:
                case ::HIR::CoreType::I64:
                case ::HIR::CoreType::Isize:
                case ::HIR::CoreType::I128: // TODO: Emulation
                    is_signed = true;
                    break;
                case ::HIR::CoreType::Bool:
                case ::HIR::CoreType::U8:
                case ::HIR::CoreType::U16:
                case ::HIR::CoreType::U32:
                case ::HIR::CoreType::U64:
                case ::HIR::CoreType::Usize:
                case ::HIR::CoreType::Char:
                case ::HIR::CoreType::U128: // TODO: Emulation
                    is_signed = false;
                    break;
                case ::HIR::CoreType::F16:
                case ::HIR::CoreType::F32:
                case ::HIR::CoreType::F64:
                case ::HIR::CoreType::F128:
                    TODO(Span(), "Floating point enum tag.");
                    break;
                case ::HIR::CoreType::Str:
                    BUG(Span(), "Unsized tag?!");
            }
            TU_MATCH_HDRA((item.mData), {)
            TU_ARMA(Value, e) {
                    U128 i(0);
                    for (auto& var : e.variants) {
                        if (var.expr) {
                            auto nvs = NewvalState{mod, mod_path, FMT(name << "#" << var.name << "_")};
                            auto eval = ::HIR::Evaluator{var.expr->span(), crate, nvs};
                            eval.resolve.setImplGenericsRaw(MetadataType::None, item.mParams);
                            try {
                                auto val = eval.evaluateConstant(p, var.expr, crate.types.primitive(ty));
                                DEBUG("enum variant: " << p << "::" << var.name << " = " << val);
                                if (is_signed) {
                                    i = EncodedLiteralSlice(val).readSint().getInner();
                                } else {
                                    i = EncodedLiteralSlice(val).readUint();
                                }
                            } catch (const Defer&) {
                                BUG(var.expr->span(), "`Defer` thrown during evaluation of enum discriminant");
                            }
                        }
                        var.val = i;
                        if (!var.expr) {
                            DEBUG("enum variant: " << p << "::" << var.name << " = " << var.val << " (auto)");
                        }
                        i += 1;
                    }
                }
                TU_ARMA(Data, e) {
                    U128 i(0);
                    for (auto& var : e) {
                        if (var.discriminantExpr) {
                            auto nvs = NewvalState{mod, mod_path, FMT(name << "#" << var.name << "_")};
                            auto eval = ::HIR::Evaluator{var.discriminantExpr->span(), crate, nvs};
                            eval.resolve.setImplGenericsRaw(MetadataType::None, item.mParams);
                            try {
                                auto val = eval.evaluateConstant(p, var.discriminantExpr, crate.types.primitive(ty));
                                DEBUG("enum variant: " << p << "::" << var.name << " = " << val);
                                if (is_signed) {
                                    i = EncodedLiteralSlice(val).readSint().getInner();
                                } else {
                                    i = EncodedLiteralSlice(val).readUint();
                                }
                            } catch (const Defer&) {
                                BUG(var.discriminantExpr->span(), "`Defer` thrown during evaluation of enum discriminant");
                            }
                        }
                        var.discriminant_value = i;
                        i += 1;
                    }
                }
            }
            item.discriminantsEvaluated = true;
        }
    };

    class ExpanderApply: public ::HIR::Visitor {
    public:
        explicit ExpanderApply(::HIR::TypeInterner& types)
            : ::HIR::Visitor(nullptr, types)
        {
        }

        void visit_module(::HIR::ItemPath p, ::HIR::Module& mod) override {
            if (!mod.inlineStatics.empty()) {
                for (auto& v : mod.inlineStatics) {
                    // ::std::unique_ptr<VisEnt<ValueItem>>
                    ::std::unique_ptr<::HIR::VisEnt<::HIR::ValueItem>> iv;
                    iv.reset(new ::HIR::VisEnt<::HIR::ValueItem>{::HIR::Publicity::newNone(), ::HIR::ValueItem::make_Static(mv$(*v.second))});
                    mod.valueItems.insert(::std::make_pair(v.first, mv$(iv)));
                }
                mod.inlineStatics.clear();
            }

            ::HIR::Visitor::visit_module(p, mod);
        }
    };

    void ConvertHIRConstantEvaluateStatic(const ::HIR::Crate& crate, const ::HIR::GenericParams* impl_params, const ::HIR::ItemPath& ip, ::HIR::Static& e) {
        Expander exp{crate};
        exp.implParams = impl_params;
        exp.visit_static(ip, e);
    }

    void ConvertHIRConstantEvaluateFcnSig(const ::HIR::Crate& crate, const ::HIR::GenericParams* impl_params, const ::HIR::ItemPath& ip, ::HIR::Function& fcn) {
        Expander exp{crate};
        exp.implParams = impl_params;
        exp.visit_function(ip, fcn);
    }
} // namespace

namespace {
    // Discriminant values must be known before anything else is evaluated: an array size in
    // an early module can cast a variant of an enum that the main pass visits later.
    struct EnumValueExpander: public ::HIR::Visitor {
        const ::HIR::Crate& crate;
        ::typeck::ModuleState mTypeck;
        const ::HIR::Module* mMod;
        const ::HIR::ItemPath* modPath;

        EnumValueExpander(const ::HIR::Crate& crate)
            : ::HIR::Visitor(nullptr, crate.types)
            , crate(crate)
            , mTypeck(crate)
            , mMod(nullptr)
            , modPath(nullptr)
        {
        }

        void visit_module(::HIR::ItemPath p, ::HIR::Module& mod) override {
            auto savedMp = modPath;
            auto savedM = mMod;
            mMod = &mod;
            modPath = &p;
            mTypeck.pushTraits(p, mod);
            ::HIR::Visitor::visit_module(p, mod);
            mTypeck.popTraits(mod);
            mMod = savedM;
            modPath = savedMp;
        }

        void visit_enum(::HIR::ItemPath p, ::HIR::Enum& item) override {
            // Enum discriminants are evaluated before the regular expression
            // typecheck pass. Give their literals and primitive operators the
            // enum repr type now, so CTFE/MIR never sees a defaulted i32 where
            // it was asked to produce (for example) a u64.
            auto _ = mTypeck.setImplGenerics(item.mParams);
            if (auto* e = item.mData.opt_Value()) {
                auto enumType = crate.types.primitive(::HIR::Enum::getReprType(item.tagRepr));
                for (auto& var : e->variants) {
                    if (var.expr) {
                        tArgs args;
                        TypecheckCode(mTypeck, args, enumType, var.expr);
                    }
                }
            }
            Expander::visit_enum_inner(crate, p, *mMod, *modPath, p.getName(), item);
        }
    };
}

void ConvertHIRConstantEvaluate(::HIR::Crate& crate) {
    EnumValueExpander{crate}.visit_crate(crate);

    Expander exp{crate};
    exp.visit_crate(crate);
    exp.pass = Expander::Pass::Values;
    exp.visit_crate(crate);

    ExpanderApply(crate.types).visit_crate(crate);
    for (auto& newTyPair : crate.newTypes) {
        auto res = crate.rootModule.modItems.insert(mv$(newTyPair));
        ASSERT_BUG(Span(), res.second, "Duplicate type in consteval?");
    }
    crate.newTypes.clear();
    for (auto& newValPair : crate.newValues) {
        auto res = crate.rootModule.valueItems.insert(mv$(newValPair));
        ASSERT_BUG(Span(), res.second, "Duplicate value in consteval?");
    }
    crate.newValues.clear();
}

void ConvertHIRConstantEvaluateExpr(const ::HIR::Crate& crate, const ::HIR::ItemPath& ip, ::HIR::ExprPtr& expr_ptr) {
    TRACE_FUNCTION_F(ip);
    // Check innards but NOT the value
    Expander exp{crate};
    exp.visit_expr(expr_ptr);
}

void ConvertHIRConstantEvaluateEnum(const ::HIR::Crate& crate, const ::HIR::ItemPath& ip, const ::HIR::Enum& enm) {
    auto mod_path = ip.getSimplePath();
    auto itemName = mod_path.popComponent();
    const auto& mod = crate.getModByPath(Span(), mod_path);

    auto& item = const_cast<::HIR::Enum&>(enm);

    Expander::visit_enum_inner(crate, ip, mod, mod_path, itemName.c_str(), item);
}

void ConvertHIRConstantEvaluateConstant(const ::HIR::Crate& crate, const ::HIR::GenericParams* impl_params, const ::HIR::ItemPath& ip, ::HIR::Constant& e) {
    Expander exp{crate};
    exp.pass = Expander::Pass::Values;
    exp.implParams = impl_params;
    exp.visit_constant(ip, e);
}

void ConvertHIRConstantEvaluateConstGeneric(const Span& sp, const ::HIR::Crate& crate, const HIR::TypeData* ty, ::HIR::ConstGeneric& cg) {
    if (auto* cgeP = cg.opt_Unevaluated()) {
        const auto& cge = *cgeP;
        try {
            cg = HIR::EncodedLiteralPtr(evaluateConstgeneric(sp, crate, ty, *cge));
        } catch (const Defer&) {
            // Deferred - no update
        }
    }
}

void ConvertHIRConstantEvaluateConstGeneric(const Span& sp, const ::HIR::Crate& crate, ::HIR::ConstGeneric& cg) {
    if (const auto* value = cg.opt_Unevaluated()) {
        const auto& expr = *(*value)->expr;
        MonomorphState ms(crate.types);
        ms.ppImpl = &(*value)->paramsImpl;
        ms.ppMethod = &(*value)->paramsItem;
        auto type = ms.monomorphType(sp, expr->resType);
        if (visit_ty_with(type, [](const HIR::TypeData* t) {
            return t->is_Infer();
        })) {
            return;
        }
        ConvertHIRConstantEvaluateConstGeneric(sp, crate, type, cg);
    }
}

void ConvertHIRConstantEvaluateArraySize(const Span& sp, const ::HIR::Crate& crate, const ::HIR::SimplePath& path, ::HIR::ArraySize& size) {
    if (auto* se = size.opt_Unevaluated()) {
        if (se->is_Unevaluated()) {
            ConvertHIRConstantEvaluateConstGeneric(sp, crate, crate.types.primitive(HIR::CoreType::Usize), *se);
        }
        if (const auto* e = se->opt_Evaluated()) {
            size = (*e)->readUsize(0);
        }
    }
}

namespace {
    bool paramsContainIvars(const ::HIR::PathParams& params) {
        for (const auto& t : params.types) {
            if (visit_ty_with(t, [](const HIR::TypeData* t) {
                return t->is_Infer();
            })) {
                return true;
            }
        }
        for (const auto& v : params.values) {
            if (v.is_Infer()) {
                return true;
            }
        }
        return false;
    }
}

void ConvertHIRConstantEvaluateMethodParams(const Span& sp, const ::HIR::Crate& crate, const HIR::SimplePath& mod_path, const ::HIR::GenericParams* impl_generics, const ::HIR::GenericParams* item_generics, const ::HIR::GenericParams* paramsDef, ::HIR::PathParams& params) {
    for (auto& v : params.values) {
        if (v.is_Unevaluated()) {
            const auto& ue = *v.as_Unevaluated();
            const auto& e = *ue.expr;
            auto name = FMT("param_" << &v << "#");
            TRACE_FUNCTION_FR(name, name);
            auto nvs = NewvalState{crate.getModByPath(Span(), mod_path), mod_path, name};
            auto eval = ::HIR::Evaluator{sp, crate, nvs};
            eval.resolve.setBothGenericsRaw(impl_generics, item_generics);

            // Need to look up the required type - to do that requires knowing the item it's for
            // - Which, might not be known at this point - might be a UfcsInherent
            try {
                // TODO: if there's an ivar in the param list, then throw defer
                // - Caller should ensure that known ivars are expanded.
                if (paramsContainIvars(ue.paramsImpl) || paramsContainIvars(ue.paramsItem)) {
                    throw Defer();
                }

                ASSERT_BUG(sp, paramsDef, "Missing generic parameter definitions for " << params);
                auto idx = static_cast<size_t>(&v - &params.values.front());
                ASSERT_BUG(sp, idx < paramsDef->values.size(), "");
                const auto& ty = paramsDef->values[idx].mType;
                ASSERT_BUG(sp, !monomorphiseTypeNeeded(ty), "" << ty);
                MonomorphState ms(crate.types);
                ms.ppImpl = &ue.paramsImpl;
                ms.ppMethod = &ue.paramsItem;

                auto val = eval.evaluateConstant(::HIR::ItemPath(mod_path, name.c_str()), e, ty, std::move(ms));
                v = ::HIR::ConstGeneric::make_Evaluated(std::move(val));
            } catch (const Defer&) {
                // Deferred - no update
            }
        }
    }
}

namespace HIR {

Evaluator::CsePtr::CsePtr(::MIR::eval::CallStackEntry* ptr)
    : inner(ptr) {
}
Evaluator::CsePtr::CsePtr(CsePtr&& x)
    : inner(x.inner) {
    x.inner = nullptr;
}
Evaluator::CsePtr& Evaluator::CsePtr::operator=(CsePtr&& x) {
    this->~CsePtr();
    this->inner = x.inner;
    x.inner = nullptr;
    return *this;
}
Evaluator::Evaluator(const Span& sp, const ::HIR::Crate& crate, Newval& nvs)
    : root_span(sp)
    , value_pool(stl::ObjPool::fromMemory())
    , resolve(crate)
    , nvs(nvs)
    , evalIndex(sNextEvalIndex++)
    , numFrames(0)
    , requireConstCalls(false) {
}
}
