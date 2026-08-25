#include "hir_conv_constant_evaluation.h"

namespace {
    /// The largest value an unsigned `repr` can hold, or nothing when the tag
    /// is signed or not a fixed-width integer.
    ::std::optional<U128> enumTagUnsignedMax(HIRCoreType ty) {
        switch (ty) {
            case HIRCoreType::U8:
                return U128(0xFFu);
            case HIRCoreType::U16:
                return U128(0xFFFFu);
            case HIRCoreType::U32:
                return U128(0xFFFFFFFFu);
            default:
                return {};
        }
    }

    /// Reject the two ways a discriminant list can be invalid: two variants
    /// sharing a value, and a value the tag cannot hold.
    template <typename Variants, typename GetValue>
    void checkEnumDiscriminants(const Span& sp, HIRCoreType ty, const Variants& variants, GetValue getValue) {
        const auto max = enumTagUnsignedMax(ty);
        ::std::map<U128, RcString> seen;
        for (const auto& var : variants) {
            const auto value = getValue(var);
            if (max && value > *max) {
                ERROR(sp, E0000, "discriminant value `" << value << "` for variant `" << var.name << "` is not in the range of the enum's tag type");
            }
            auto inserted = seen.insert(::std::make_pair(value, var.name));
            if (!inserted.second) {
                ERROR(sp, E0000, "discriminant value `" << value << "` assigned more than once - `" << inserted.first->second << "` and `" << var.name << "`");
            }
        }
    }
}

#include "floats.h"
#include "int128.h" // 128 bit integer support
#include "hir_hir.h"
#include "mir_mir.h"
#include "hir_expr.h"
#include "wire_board.h"
#include "hir_visitor.h"
#include "mir_helpers.h"
#include "trans_target.h"
#include "trans_codegen.h" // For encoding as part of transmute
#include "hir_expr_state.h"
#include "hir_typeck_common.h"  // Monomorph
#include "trans_monomorphise.h" // For handling monomorph of MIR in provided associated constants
#include "hir_typeck_expr_visit.h"
#include "hir_conv_main_bindings.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include <std/alg/defer.h>
#include <algorithm>

::std::ostream& operator<<(::std::ostream& os, Defer::Reason r) {
    switch (r) {
        case Defer::Reason::Layout:
            return os << "Layout";
        case Defer::Reason::GenericValue:
            return os << "GenericValue";
        case Defer::Reason::Infer:
            return os << "Infer";
        case Defer::Reason::NotYetKnown:
            return os << "NotYetKnown";
        case Defer::Reason::UnresolvedCall:
            return os << "UnresolvedCall";
    }
    return os << "?";
}

namespace {
    void ConvertHIRConstantEvaluateStatic(const WireBoard& wb, const HIRCrate& crate, const HIRGenericParams* implParams, const HIRItemPath& ip, HIRStatic& e);
    void ConvertHIRConstantEvaluateFcnSig(const WireBoard& wb, const HIRCrate& crate, const HIRGenericParams* implParams, const HIRItemPath& ip, HIRFunction& fcn);

    // Diagnostic: attempt evaluation even when the captures predicate says
    // no, and report a loud PREDICATE MISS when the attempt succeeds.
    const bool sCapsOracle = getenv("TRUSTME_CAPS_ORACLE") != nullptr;

    // Dumped at exit when TRUSTME_DEFER_STATS is set: how often speculative
    // evaluation bailed, by reason -- the map of what still runs too early.
    struct DeferStats {
        unsigned counts[Defer::NUM_REASONS] = {};

        ~DeferStats() {
            if (!getenv("TRUSTME_DEFER_STATS")) {
                return;
            }
            fprintf(stderr, "defer-stats: layout=%u generic-value=%u infer=%u not-yet-known=%u unresolved-call=%u\n",
                counts[0], counts[1], counts[2], counts[3], counts[4]);
        }
    } gDeferStats;
}

// Every Defer is thrown through here: the throw point is the only place that
// knows the reason, so it is printed and counted before the unwind.
#define THROW_DEFER(sp, reasonName, msg)                                        \
    do {                                                                        \
        gDeferStats.counts[static_cast<unsigned>(Defer::Reason::reasonName)]++; \
        DEBUG("Defer(" << Defer::Reason::reasonName << ") " << msg);            \
        throw Defer{Defer::Reason::reasonName, Span(sp)};                       \
    } while (0)

namespace {

    bool constGenericIsConcrete(const HIRConstGeneric& value);

    struct MonomorphAvailability: HIRVisitor {
        const MonomorphState& ms;
        bool available = true;

        explicit MonomorphAvailability(const MonomorphState& ms)
            : HIRVisitor(nullptr, ms.typeInterner())
            , ms(ms)
        {
        }

        const HIRPathParams* paramsFor(const HIRGenericRef& generic) const {
            switch (generic.group()) {
                case GENERICImpl:
                    return ms.getImplParams();
                case GENERICItem:
                    return ms.getMethodParams();
                default:
                    return nullptr;
            }
        }

        [[nodiscard]] HIRTypeRef visitType(HIRTypeRef type) override {
            if (!available) {
                return type;
            }
            if (type->is_Infer()) {
                available = false;
                return type;
            }
            if (const auto* generic = type->opt_Generic()) {
                if (generic->isSelf()) {
                    available = ms.getSelfType() != nullptr;
                } else if (const auto* params = paramsFor(*generic)) {
                    available = generic->idx() < params->types.size();
                } else {
                    available = false;
                }
                return type;
            }
            switch (type->tag()) {
                case HIRTypeData::TAG_Path:
                case HIRTypeData::TAG_TraitObject:
                case HIRTypeData::TAG_ErasedType:
                case HIRTypeData::TAG_Array:
                case HIRTypeData::TAG_Pattern:
                case HIRTypeData::TAG_NamedFunction: {
                    // Values inside these must still reach visitConstgeneric.
                    auto data = type->cloneData();
                    visitTypeDataChildren(data);
                    return typeInterner().intern(mv$(data));
                }
                default:
                    return HIRVisitor::visitType(type);
            }
        }

        void visitConstgeneric(HIRConstGeneric& value) override {
            if (!available) {
                return;
            }
            if (value.is_Infer()) {
                available = false;
                return;
            }
            if (const auto* generic = value.opt_Generic()) {
                if (const auto* params = paramsFor(*generic)) {
                    available = generic->idx() < params->values.size();
                } else {
                    available = false;
                }
                return;
            }
            if (auto* unevaluated = value.opt_Unevaluated()) {
                // The expression refers through these captured parameter lists.
                // Its own HIR bindings are not parameters of the current state.
                if ((*unevaluated)->selfType) {
                    (*unevaluated)->selfType = visitType((*unevaluated)->selfType);
                }
                visitPathParams((*unevaluated)->paramsImpl);
                visitPathParams((*unevaluated)->paramsItem);
            }
        }
    };

    bool typeCanMonomorph(HIRTypeRef type, const MonomorphState& ms) {
        if (!type) {
            return true;
        }
        auto copy = type;
        MonomorphAvailability visitor(ms);
        copy = visitor.visitType(copy);
        return visitor.available;
    }

    bool pathParamsCanMonomorph(const HIRPathParams& params, const MonomorphState& ms) {
        auto copy = params.clone();
        MonomorphAvailability visitor(ms);
        visitor.visitPathParams(copy);
        return visitor.available;
    }

    bool pathParamsAreConcrete(const HIRPathParams& params) {
        for (const auto& type : params.types) {
            if (monomorphiseTypeNeeded(type)
                || visitTyWith(type, [](const HIRTypeData* inner) {
                    return inner->is_Infer();
                })) {
                return false;
            }
        }
        for (const auto& value : params.values) {
            if (!constGenericIsConcrete(value)) {
                return false;
            }
        }
        return true;
    }

    bool typeIsConcrete(HIRTypeRef type) {
        return !type
            || (!monomorphiseTypeNeeded(type)
                && !visitTyWith(type, [](const HIRTypeData* inner) {
                    return inner->is_Infer();
                }));
    }

    bool unevaluatedEnvIsConcrete(const HIRConstGenericUnevaluated& ue) {
        return typeIsConcrete(ue.selfType)
            && pathParamsAreConcrete(ue.paramsImpl)
            && pathParamsAreConcrete(ue.paramsItem);
    }

    bool constGenericIsConcrete(const HIRConstGeneric& value) {
        if (value.is_Evaluated()) {
            return true;
        }
        if (const auto* unevaluated = value.opt_Unevaluated()) {
            return unevaluatedEnvIsConcrete(**unevaluated);
        }
        return false;
    }

    /// One syntactic scan filling HIRExprState::Captures: the generic slots
    /// the expression names. Cost model matches rustc's TypeFlags: computed
    /// once per expression (per stage), read as bits afterwards.
    struct ExprCaptureScan final: HIRExprVisitorDef {
        HIRExprState::Captures out;

        explicit ExprCaptureScan(HIRTypeInterner& types)
            : HIRExprVisitorDef(types)
        {
        }

        void addSlot(u64 (&masks)[2], u32 binding) {
            const auto group = binding >> 8;
            const auto idx = binding & 0xFF;
            if (group > GENERICItem || idx >= 64) {
                out.unknown = true;
                return;
            }
            masks[group] |= u64(1) << idx;
        }

        void addType(const HIRGenericRef& g) {
            if (g.isSelf()) {
                out.usesSelf = true;
            } else {
                addSlot(out.typeMask, g.binding);
            }
        }

        [[nodiscard]] HIRTypeRef visitType(HIRTypeRef ty) override {
            if (ty) {
                if (ty->flags & (HIRTypeData::HAS_UNEVALUATED_CONST | HIRTypeData::HAS_DEFERRED_CONST)) {
                    out.unknown = true;
                }
                if (ty->flags & HIRTypeData::HAS_TYPE_PARAM) {
                    visitTyWith(ty, [&](const HIRTypeData* inner) {
                        if (const auto* g = inner->opt_Generic()) {
                            addType(*g);
                        }
                        return false;
                    });
                }
            }
            return HIRExprVisitorDef::visitType(ty);
        }

        void visitPathParams(HIRPathParams& pp) override {
            for (const auto& v : pp.values) {
                if (const auto* g = v.opt_Generic()) {
                    addSlot(out.valueMask, g->binding);
                } else if (v.is_Unevaluated()) {
                    out.unknown = true;
                }
            }
            HIRExprVisitorDef::visitPathParams(pp);
        }

        void visit(HIRExprNodeConstParam& node) override {
            addSlot(out.valueMask, node.binding);
            HIRExprVisitorDef::visit(node);
        }

        void visit(HIRExprNodeCallMethod& node) override {
            out.unknown = true;
            HIRExprVisitorDef::visit(node);
        }
    };

    const HIRExprState::Captures& exprCaptures(HIRTypeInterner& types, const HIRExprPtr& expr) {
        auto& state = *expr.state;
        if (!state.captures.computed || state.captures.stage != state.stage) {
            ExprCaptureScan scan(types);
            const_cast<HIRExprPtr&>(expr)->visit(scan);
            state.captures = scan.out;
            state.captures.computed = true;
            state.captures.stage = state.stage;
        }
        return state.captures;
    }

    /// Whether every generic slot the expression names is concrete in its
    /// captured environment: the rustc `const_eval_resolve` precondition,
    /// decided without running the interpreter. Reads the cached captures.
    bool unevaluatedUsedSlotsAreConcrete(HIRTypeInterner& types, const HIRConstGenericUnevaluated& ue) {
        if (!ue.expr || !*ue.expr || !(*ue.expr).state) {
            return false;
        }
        const auto& caps = exprCaptures(types, *ue.expr);
        if (caps.unknown) {
            return unevaluatedEnvIsConcrete(ue);
        }
        if (caps.usesSelf && !typeIsConcrete(ue.selfType)) {
            return false;
        }
        const HIRPathParams* groups[2] = {&ue.paramsImpl, &ue.paramsItem};
        for (unsigned g = 0; g < 2; g++) {
            for (auto mask = caps.typeMask[g]; mask; mask &= mask - 1) {
                const auto idx = static_cast<size_t>(__builtin_ctzll(mask));
                if (idx >= groups[g]->types.size() || !typeIsConcrete(groups[g]->types[idx])) {
                    return false;
                }
            }
            for (auto mask = caps.valueMask[g]; mask; mask &= mask - 1) {
                const auto idx = static_cast<size_t>(__builtin_ctzll(mask));
                if (idx >= groups[g]->values.size() || !constGenericIsConcrete(groups[g]->values[idx])) {
                    return false;
                }
            }
        }
        return true;
    }

    struct NewvalStateNop: public HIREvaluator::Newval {
        const Span& sp;

        NewvalStateNop(const Span& sp)
            : sp(sp)
        {
        }

        HIRPath newStatic(HIRTypeRef type, EncodedLiteral value, size_t alignment) override {
            TODO(this->sp, "new_static while evaluating a const generic");
        }
    };

    /// Typecheck a symbolic const's body and build its MIR without
    /// interpreting it: bodies are checked unconditionally (rustc checks
    /// anon-const bodies like any body); only the value waits for a
    /// concrete environment.
    void translateConstExprBody(const Span& sp, const WireBoard& wb, const HIRCrate& crate, const HIRTypeData* type, const HIRConstGenericUnevaluated& value) {
        const auto& expr = *value.expr;
        ASSERT_BUG(sp, expr.state, "Const-generic expression has no state");
        const auto& state = *expr.state;
        auto name = FMT("const_" << &expr << "#");
        HIRTypeRef exp = type;
        crate.getOrGenMir(wb, HIRItemPath(state.modPath, name.c_str()), expr, exp);
    }

    EncodedLiteral evaluateConstgeneric(const Span& sp, const WireBoard& wb, const HIRCrate& crate, const HIRTypeData* type, const HIRConstGenericUnevaluated& value) {
        const auto& expr = *value.expr;
        ASSERT_BUG(sp, expr.state, "Const-generic expression has no state");
        const auto& state = *expr.state;
        auto name = FMT("const_" << &expr << "#");

        NewvalStateNop nvs{sp};
        auto eval = HIREvaluator{sp, wb, nvs};
        eval.setRequireConstCalls();
        eval.resolve.setBothGenericsRaw(state.implGenerics, state.itemGenerics);

        MonomorphState ms(crate.types);
        ms.selfTy = value.selfType;
        ms.ppImpl = &value.paramsImpl;
        ms.ppMethod = &value.paramsItem;
        return eval.evaluateConstant(HIRItemPath(state.modPath, name.c_str()), expr, type, std::move(ms));
    }

    struct NewvalState: public HIREvaluator::Newval {
        const HIRModule& mod;
        const HIRItemPath& modPath;
        ::std::string namePrefix;
        unsigned int nextItemIdx;

        NewvalState(const HIRModule& mod, const HIRItemPath& modPath, ::std::string prefix)
            : mod(mod)
            , modPath(modPath)
            , namePrefix(prefix)
            , nextItemIdx(0)
        {
        }

        HIRPath newStatic(HIRTypeRef type, EncodedLiteral value, size_t alignment) override {
            ASSERT_BUG(Span(), type != HIRTypeRef(), "");
            auto name = RcString::newInterned(FMT(namePrefix << nextItemIdx));
            nextItemIdx++;
            auto rv = modPath.getSimplePath() + name.c_str();
            auto s = HIRStatic(HIRLinkage(), false, mv$(type), HIRExprPtr());
            s.explicitAlignment = alignment;
            s.valueRes = ::std::move(value);
            s.valueGenerated = true;
            s.saveLiteral = true;
            DEBUG(rv << ": " << s.type << " = " << s.valueRes);

            const_cast<HIRModule&>(mod).inlineStatics.push_back(::std::make_pair(mv$(name), box$(s)));
            return rv;
        }
    };

// Definitions generated from hir_conv_ent_ptr.tu.
#include "hir_conv_ent_ptr_tu.h"
    enum class EntNS {
        //Type,
        Value
    };
}

class MIREvalAllocation;
class MIREvalConstant;
class MIREvalStaticRef;
class MIREvalRelocPtr;

template <typename T>
class MIREvalPtr {
    friend class MIREvalRelocPtr;

protected:
    T* ptr;

public:
    MIREvalPtr()
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
class MIREvalConstantPtr final: public MIREvalPtr<MIREvalConstant> {
public:
    static MIREvalConstantPtr allocate(stl::ObjPool* pool, const void* data, size_t len);
};

/// Mutable allocation
class MIREvalAllocationPtr final: public MIREvalPtr<MIREvalAllocation> {
public:
    static MIREvalAllocationPtr allocate(stl::ObjPool* pool, const StaticTraitResolve& resolve, const MIRTypeResolve& state, const HIRTypeData* ty);
    static MIREvalAllocationPtr allocateScratch(stl::ObjPool* pool, size_t size);
    static MIREvalAllocationPtr allocateHeap(stl::ObjPool* pool, size_t size, size_t alignment);
    static MIREvalAllocationPtr allocateRo(stl::ObjPool* pool, const void* data, size_t len);
};

/// Reference to a `static`
class MIREvalStaticRefPtr final: public MIREvalPtr<MIREvalStaticRef> {
public:
    static MIREvalStaticRefPtr allocate(stl::ObjPool* pool, HIRPath p, const EncodedLiteral* lit, size_t len, bool valuePending = false);
};

/// Common interface for data storage
class IValue {
public:
    virtual void fmtIdent(std::ostream& os) const = 0;
    virtual void fmt(::std::ostream& os, size_t ofs, size_t len) const = 0;

    virtual size_t size() const = 0;
    virtual const u8* getBytes(size_t ofs, size_t len, bool checkMask) const = 0;
    virtual void readMask(u8* dst, size_t dstOfs, size_t ofs, size_t len) const = 0;

    virtual bool isWritable() const = 0;
    virtual u8* extWriteBytes(size_t ofs, size_t len) = 0;

    void writeBytes(size_t ofs, const void* data, size_t len) {
        memcpy(extWriteBytes(ofs, len), data, len);
    }

    virtual void writeMaskFrom(size_t ofs, const IValue& src, size_t srcOfs, size_t len) = 0;

    virtual MIREvalRelocPtr getReloc(size_t ofs) const = 0;
    virtual void setReloc(size_t ofs, MIREvalRelocPtr ptr) = 0;
};

/// Pointer wrapping a reference-counted allocation
class MIREvalRelocPtr {
    uintptr_t ptr;

    enum Tag {
        TAG_Allocation = 0,
        TAG_Constant,
        TAG_StaticRef,
    };

public:
    ~MIREvalRelocPtr() = default;
    MIREvalRelocPtr(const MIREvalRelocPtr&) = default;
    MIREvalRelocPtr(MIREvalRelocPtr&&) = default;
    MIREvalRelocPtr& operator=(const MIREvalRelocPtr&) = default;
    MIREvalRelocPtr& operator=(MIREvalRelocPtr&&) = default;

    MIREvalRelocPtr()
        : ptr(0)
    {
    }

    MIREvalRelocPtr(MIREvalAllocationPtr p)
        : ptr(0)
    {
        set(reinterpret_cast<uintptr_t>(p.ptr), TAG_Allocation);
    }

    MIREvalRelocPtr(MIREvalConstantPtr p)
        : ptr(0)
    {
        set(reinterpret_cast<uintptr_t>(p.ptr), TAG_Constant);
    }

    MIREvalRelocPtr(MIREvalStaticRefPtr p)
        : ptr(0)
    {
        set(reinterpret_cast<uintptr_t>(p.ptr), TAG_StaticRef);
    }

    operator bool() const {
        return ptr != 0;
    }

    bool operator==(const MIREvalRelocPtr& x) const {
        return ptr == x.ptr;
    }

    IValue& asValue() {
        return *asValuePtr();
    }

    const IValue& asValue() const {
        return *asValuePtr();
    }

    MIREvalAllocation* asAllocation() const {
        return (ptr != 0 && (ptr & 3) == TAG_Allocation) ? reinterpret_cast<MIREvalAllocation*>(ptr - TAG_Allocation) : nullptr;
    }

    MIREvalConstant* asConstant() const {
        return (ptr != 0 && (ptr & 3) == TAG_Constant) ? reinterpret_cast<MIREvalConstant*>(ptr - TAG_Constant) : nullptr;
    }

    MIREvalStaticRef* asStaticref() const {
        return (ptr != 0 && (ptr & 3) == TAG_StaticRef) ? reinterpret_cast<MIREvalStaticRef*>(ptr - TAG_StaticRef) : nullptr;
    }

    friend std::ostream& operator<<(std::ostream& os, const MIREvalRelocPtr& ptr) {
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
void putbHex(std::ostream& os, u8 v) {
    char tmp[3];
    tmp[0] = "0123456789ABCDEF"[v >> 4];
    tmp[1] = "0123456789ABCDEF"[v & 0xF];
    tmp[2] = '\0';
    os << tmp;
}

/// Constant data
class MIREvalConstant final: public IValue {
    friend struct stl::Embed<MIREvalConstant>;
    friend class MIREvalConstantPtr;
    unsigned const length;
    const u8* const data;

    MIREvalConstant(const void* data, size_t len)
        : length(len)
        , data(reinterpret_cast<const u8*>(data))
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

    size_t size() const override {
        return length;
    }

    const u8* getBytes(size_t ofs, size_t len, bool /*check_mask*/) const override {
        if (!(ofs <= length) || !(len <= length) || !(ofs + len <= length)) {
            return nullptr;
        }
        return data + ofs;
    }

    void readMask(u8* dst, size_t dstOfs, size_t /*ofs*/, size_t len) const override {
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

    u8* extWriteBytes(size_t ofs, size_t len) override {
        abort();
    }

    void writeMaskFrom(size_t ofs, const IValue& src, size_t srcOfs, size_t len) override {
        abort();
    }

    MIREvalRelocPtr getReloc(size_t ofs) const override {
        return MIREvalRelocPtr();
    }

    void setReloc(size_t ofs, MIREvalRelocPtr ptr) override {
        abort();
    }
};

class MIREvalAllocation final: public IValue {
    friend struct stl::Embed<MIREvalAllocation>;
    friend class MIREvalAllocationPtr;

public:
    struct Reloc {
        size_t offset;
        MIREvalRelocPtr ptr;
    };

private:
    unsigned length;
    bool isReadonly;
    HIRTypeRef type_;
    bool isConstHeap;
    bool isLive;
    bool isGlobal;
    size_t heapAlignment;
    std::vector<Reloc> relocations;
    u8* data;

    MIREvalAllocation(u8* data, size_t len, const HIRTypeData* ty)
        : length(len)
        , isReadonly(false)
        , type_(ty)
        , isConstHeap(false)
        , isLive(true)
        , isGlobal(false)
        , heapAlignment(0)
        , data(data)
    {
        memset(data, 0, len + (len + 7) / 8);
    }

    MIREvalAllocation(const MIREvalAllocation&) = delete;
    MIREvalAllocation& operator=(const MIREvalAllocation&) = delete;

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

    const u8* getBytes(size_t ofs, size_t len, bool checkMask) const override {
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

    void readMask(u8* dst, size_t dstOfs, size_t ofs, size_t len) const override {
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
                u8 mask = (0xFF >> (8 - len));
                *dst = (*dst & ~mask) | (*src & mask);
            }
        }
    }

    bool isWritable() const override {
        return !isReadonly;
    }

    u8* extWriteBytes(size_t ofs, size_t len) override {
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

    void writeMaskFrom(size_t ofs, const IValue& src, size_t srcOfs, size_t len) override {
        assert(ofs <= length);
        assert(len <= length);
        assert(ofs + len <= length);
        src.readMask(getMask(), ofs, srcOfs, len);
    }

    MIREvalRelocPtr getReloc(size_t ofs) const override {
        for (const auto& r : this->relocations) {
            if (r.offset == ofs) {
                return r.ptr;
            }
        }
        return MIREvalRelocPtr();
    }

    void setReloc(size_t ofs, MIREvalRelocPtr ptr) override {
        // Not necessarily pointer-aligned: a packed struct puts a reference
        // wherever its fields fall, and constant evaluation copies those bytes
        // around as bytes.
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

    const HIRTypeData* getType() const {
        return type_;
    }

    bool isConstHeapAllocation() const {
        return isConstHeap;
    }

    bool isAlive() const {
        return isLive;
    }

    bool wasMadeGlobal() const {
        return isGlobal;
    }

    size_t getHeapAlignment() const {
        return heapAlignment;
    }

    void makeGlobal() {
        assert(isConstHeap && isLive && !isGlobal);
        isGlobal = true;
        isReadonly = true;
    }

    void deallocate() {
        assert(isConstHeap && isLive && !isGlobal);
        isLive = false;
    }

    const std::vector<Reloc>& getRelocations() const {
        return relocations;
    }

private:
    u8* getMask() {
        return data + length;
    }

    const u8* getMask() const {
        return data + length;
    }
};

class MIREvalStaticRef final: public IValue {
    friend struct stl::Embed<MIREvalStaticRef>;
    friend class MIREvalStaticRefPtr;

    stl::ObjPool* pool;
    HIRPath path_;
    const EncodedLiteral* encoded;
    size_t length;
    // The referenced static is being evaluated right now: its address is
    // usable, reading its bytes is a value cycle.
    bool valuePending;

    MIREvalStaticRef(stl::ObjPool* pool, HIRPath p, const EncodedLiteral* lit, size_t len, bool valuePending)
        : pool(pool)
        , path_(std::move(p))
        , encoded(lit)
        , length(len)
        , valuePending(valuePending)
    {
        assert(!encoded || encoded->bytes.size() == length);
    }

public:
    void fmtIdent(std::ostream& os) const override {
        os << this->path_;
    }

    void fmt(::std::ostream& os, size_t ofs, size_t len) const override {
        os << "[" << path_ << "]";
        if (encoded) {
            os << EncodedLiteralSlice(*encoded).slice(ofs, len);
        } else {
            os << "?";
        }
    }

    size_t size() const override {
        return length;
    }

    bool hasValue() const {
        return encoded;
    }

    const u8* getBytes(size_t ofs, size_t len, bool checkMask) const override {
        if (encoded) {
            assert(ofs <= encoded->bytes.size());
            assert(len <= encoded->bytes.size());
            assert(ofs + len <= encoded->bytes.size());
            if (encoded->bytes.size() == 0) {
                // Empty vectors can have a null data pointer
                return reinterpret_cast<const u8*>("");
            }
            return encoded->bytes.data() + ofs;
        } else {
            if (len == 0 && ofs == 0) {
                static u8 null;
                return &null;
            }
            if (valuePending) {
                ERROR(Span(), E0000, "cycle detected when evaluating static `" << path_ << "`");
            }
            return nullptr;
        }
    }

    void readMask(u8* dst, size_t dstOfs, size_t ofs, size_t len) const override {
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

    u8* extWriteBytes(size_t ofs, size_t len) override {
        abort();
    }

    void writeMaskFrom(size_t ofs, const IValue& src, size_t srcOfs, size_t len) override {
        abort();
    }

    MIREvalRelocPtr getReloc(size_t ofs) const override {
        if (encoded) {
            for (const auto& r : encoded->relocations) {
                if (r.ofs == ofs) {
                    MIREvalRelocPtr reloc;
                    if (r.p) {
                        return MIREvalRelocPtr(MIREvalStaticRefPtr::allocate(pool, r.p->clone(), nullptr, 0));
                        TODO(Span(), "Convert relocation pointer - " << *r.p);
                    } else {
                        return MIREvalRelocPtr(MIREvalAllocationPtr::allocateRo(pool, r.bytes.data(), r.bytes.size()));
                    }
                }
            }
        }
        return MIREvalRelocPtr();
    }

    void setReloc(size_t ofs, MIREvalRelocPtr ptr) override {
        abort();
    }

    const HIRPath& path() const {
        return path_;
    }
};

/// Reference to a value
namespace {
    /// A float cast to an integer saturates in Rust, and NaN becomes zero.
    U128 floatToUintSaturating(double v, unsigned bits) {
        const U128 maxValue = bits >= 128 ? U128::max() : (U128(1) << bits) - U128(1);
        if (!(v > 0.0)) {
            return U128(0);
        }
        static const double twoPow64 = 18446744073709551616.0;
        if (v >= twoPow64) {
            const double highPart = v / twoPow64;
            if (highPart >= twoPow64) {
                return maxValue;
            }
            const u64 high = static_cast<u64>(highPart);
            const u64 low = static_cast<u64>(v - static_cast<double>(high) * twoPow64);
            const U128 value = (U128(high) << 64) | U128(low);
            return value > maxValue ? maxValue : value;
        }
        const U128 value(static_cast<u64>(v));
        return value > maxValue ? maxValue : value;
    }

    S128 floatToSintSaturating(double v, unsigned bits) {
        if (!(v == v)) {
            return S128(0);
        }
        const unsigned magnitudeBits = bits - 1;
        const U128 maxMagnitude = (U128(1) << magnitudeBits) - U128(1);
        if (v < 0.0) {
            // The negative side reaches one further than the positive one.
            const U128 magnitude = floatToUintSaturating(-v, 128);
            const U128 limit = maxMagnitude + U128(1);
            return S128(0) - S128(magnitude >= limit ? limit : magnitude);
        }
        const U128 magnitude = floatToUintSaturating(v, 128);
        return S128(magnitude > maxMagnitude ? maxMagnitude : magnitude);
    }
}

class MIREvalValueRef {
    MIREvalRelocPtr storage;
    u32 ofs;
    u32 len;

public:
    MIREvalValueRef()
        : storage()
        , ofs(0)
        , len(0)
    {
    }

    MIREvalValueRef(MIREvalRelocPtr alloc, size_t ofs = 0)
        : storage(alloc)
        , ofs(ofs)
        , len(0)
    {
        if (alloc) {
            assert(ofs <= alloc.asValue().size());
            len = alloc.asValue().size() - ofs;
        }
    }

    MIREvalValueRef slice(size_t ofs, size_t len) const {
        ASSERT_BUG(Span(), ofs <= this->len && ofs + len <= this->len, "ValueRef::slice: " << ofs << "+" << len << " out of range (" << this->len << ")");

        MIREvalValueRef rv;
        rv.storage = storage;
        rv.ofs = this->ofs + ofs;
        rv.len = len;
        return rv;
    }

    MIREvalValueRef slice(size_t ofs) const {
        ASSERT_BUG(Span(), ofs <= this->len, "ValueRef::slice: " << ofs << " out of range (" << this->len << ")");
        return slice(ofs, this->len - ofs);
    }

    bool isValid() const {
        return storage;
    }

    MIREvalRelocPtr getStorage() const {
        return storage;
    }

    size_t getOfs() const {
        return ofs;
    }

    size_t getLen() const {
        return len;
    }

    void copyFrom(const MIRTypeResolve& state, const MIREvalValueRef& other) {
        ensureLive(state);
        other.ensureLive(state);
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
        storage.asValue().writeBytes(this->ofs, src, len);
        // Copy the mask data
        storage.asValue().writeMaskFrom(this->ofs, other.storage.asValue(), other.ofs, len);
        // Copy relocations
        for (size_t i = 0; i < len; i++) {
            if (auto r = other.storage.asValue().getReloc(other.ofs + i)) {
                storage.asValue().setReloc(this->ofs + i, std::move(r));
            }
        }
    }

    void copyFromOverlapping(const MIRTypeResolve& state, const MIREvalValueRef& other, stl::ObjPool* pool) {
        ensureLive(state);
        other.ensureLive(state);
        const size_t copyLen = std::min(this->len, other.len);
        if (copyLen == 0 || (this->storage == other.storage && this->ofs == other.ofs)) {
            return;
        }
        if (this->storage != other.storage || this->ofs + copyLen <= other.ofs || other.ofs + copyLen <= this->ofs) {
            copyFrom(state, other);
            return;
        }

        // `copy` has memmove semantics.  A scratch allocation is needed instead
        // of a byte buffer because CTFE values also carry initialisation bits and
        // pointer relocations, all of which are part of the copied representation.
        auto scratch = MIREvalAllocationPtr::allocateScratch(pool, copyLen);
        MIREvalValueRef temporary(scratch);
        temporary.copyFrom(state, other.slice(0, copyLen));
        this->slice(0, copyLen).copyFrom(state, temporary);
    }

    void writeBytes(const MIRTypeResolve& state, const void* data, size_t len) {
        ensureLive(state);
        MIR_ASSERT(state, storage, "Writing to invalid slot");
        MIR_ASSERT(state, storage.asValue().isWritable(), "Writing to read-only slot");
        if (len > 0) {
            storage.asValue().writeBytes(ofs, data, len);
        }
    }

    u8* extWriteBytes(const MIRTypeResolve& state, size_t len) {
        ensureLive(state);
        MIR_ASSERT(state, storage, "Writing to invalid slot");
        MIR_ASSERT(state, storage.asValue().isWritable(), "Writing to read-only slot");
        if (len > 0) {
            return storage.asValue().extWriteBytes(ofs, len);
        } else {
            static u8 emptyBuf;
            return &emptyBuf;
        }
    }

    void writeByte(const MIRTypeResolve& state, u8 v) {
        writeBytes(state, &v, 1);
    }

    void writeFloat(const MIRTypeResolve& state, unsigned bits, FloatValue v) {
        switch (bits) {
            case 16: {
                F16 vF(v);
                writeBytes(state, &vF, sizeof(vF));
            } break;
            case 32: {
                float vF32 = static_cast<float>(v);
                writeBytes(state, &vF32, sizeof(vF32));
            } break;
            case 64: {
                double vF64 = static_cast<double>(v);
                writeBytes(state, &vF64, sizeof(vF64));
            } break;
            case 128: {
                F128 vF128 = v;
                writeBytes(state, &vF128, 16);
            } break;
            default:
                MIR_BUG(state, "Unexpected float size (write): " << bits);
        }
    }

    void writeUint(const MIRTypeResolve& state, unsigned bits, u64 v) {
        assert(bits <= 64);
        writeUint(state, bits, U128(v));
    }

    void writeUint(const MIRTypeResolve& state, unsigned bits, U128 v) {
        auto nBytes = (bits + 7) / 8;
        v.toLeBytes(extWriteBytes(state, nBytes), nBytes); // little-endian only
    }

    void writeSint(const MIRTypeResolve& state, unsigned bits, S128 v) {
        auto nBytes = (bits + 7) / 8;
        v.getInner().toLeBytes(extWriteBytes(state, nBytes), nBytes); // little-endian only
    }

    void writePtr(const MIRTypeResolve& state, u64 val, MIREvalRelocPtr reloc) {
        writeUint(state, TargetGetPointerBits(), U128(val));
        storage.asValue().setReloc(ofs, std::move(reloc));
    }

    void setReloc(MIREvalRelocPtr reloc) {
        storage.asValue().setReloc(ofs, std::move(reloc));
    }

    const u8* extReadBytes(const MIRTypeResolve& state, size_t len) const {
        ensureLive(state);
        MIR_ASSERT(state, storage, "");
        MIR_ASSERT(state, len >= 1, "");
        const auto* src = storage.asValue().getBytes(ofs, len, /*check_mask*/ true);
        MIR_ASSERT(state, src, "Invalid read: " << ofs << "+" << len << " (in " << *this << ")");
        return src;
    }

    void readBytes(const MIRTypeResolve& state, void* data, size_t len) const {
        const auto* src = extReadBytes(state, len);
        assert(src);
        memcpy(data, src, len);
    }

    FloatValue readFloat(const MIRTypeResolve& state, unsigned bits) const {
        switch (bits) {
            case 16: {
                F16 vF16;
                readBytes(state, &vF16, sizeof(vF16));
                return FloatValue(static_cast<float>(vF16));
            } break;
            case 32: {
                float vF32 = 0;
                readBytes(state, &vF32, sizeof(vF32));
                return vF32;
            } break;
            case 64: {
                double vF64 = 0;
                readBytes(state, &vF64, sizeof(vF64));
                return vF64;
            } break;
            case 128: {
                F128 vF;
                readBytes(state, &vF, sizeof(vF));
                return vF;
            } break;
            default:
                MIR_BUG(state, "Unexpected float size: " << bits);
        }
    }

    U128 readUint(const MIRTypeResolve& state, unsigned bits) const {
        assert(bits <= 128);
        auto nBytes = (bits + 7) / 8;
        U128 rv;
        rv.fromLeBytes(extReadBytes(state, nBytes), nBytes); // little-endian only
        return rv;
    }

    S128 readSint(const MIRTypeResolve& state, unsigned bits) const {
        auto nBytes = (bits + 7) / 8;
        S128 rv;
        rv.fromLeBytes(extReadBytes(state, nBytes), nBytes); // little-endian only
        return rv;
    }

    u64 readUsize(const MIRTypeResolve& state) const {
        return readUint(state, TargetGetPointerBits()).truncateU64();
    }

    std::pair<u64, MIREvalRelocPtr> readPtr(const MIRTypeResolve& state) const {
        return std::make_pair(readUsize(state), storage.asValue().getReloc(ofs));
    }

    friend std::ostream& operator<<(std::ostream& os, const MIREvalValueRef& vr);

private:
    void ensureLive(const MIRTypeResolve& state) const {
        if (auto* allocation = storage.asAllocation()) {
            MIR_ASSERT(state, allocation->isAlive(), "use of deallocated const heap allocation");
        }
    }
};

std::ostream& operator<<(std::ostream& os, const MIREvalValueRef& vr) {
    if (!vr.storage) {
        os << "ValueRef(null)";
    } else {
        os << "ValueRef({" << vr.ofs << "+" << vr.len << "}";
        vr.storage.asValue().fmt(os, vr.ofs, vr.len);
        os << ")";
    }
    return os;
}

std::ostream& operator<<(std::ostream& os, const MIREvalAllocationPtr& ap) {
    os << MIREvalValueRef(ap);
    return os;
}

// ---
MIREvalConstantPtr MIREvalConstantPtr::allocate(stl::ObjPool* pool, const void* data, size_t len) {
    MIREvalConstantPtr rv;
    rv.ptr = pool->make<MIREvalConstant>(data, len);
    return rv;
}

// ---
MIREvalAllocationPtr MIREvalAllocationPtr::allocate(stl::ObjPool* pool, const StaticTraitResolve& resolve, const MIRTypeResolve& state, const HIRTypeData* ty) {
    size_t len;
    if (!TargetGetSizeOf(Span(), resolve, ty, len)) {
        BUG(Span(), "Layout not computable during const evaluation - " << "sizeof " << ty);
    }
    auto* data = static_cast<u8*>(pool->allocate(len + ((len + 7) / 8)));
    MIREvalAllocationPtr rv;
    // TODO: Include the current location from `state` in the allocation header
    rv.ptr = pool->make<MIREvalAllocation>(data, len, ty);
    return rv;
}

MIREvalAllocationPtr MIREvalAllocationPtr::allocateScratch(stl::ObjPool* pool, size_t size) {
    auto* data = static_cast<u8*>(pool->allocate(size + ((size + 7) / 8)));
    MIREvalAllocationPtr rv;
    rv.ptr = pool->make<MIREvalAllocation>(data, size, HIRTypeRef());
    return rv;
}

MIREvalAllocationPtr MIREvalAllocationPtr::allocateHeap(stl::ObjPool* pool, size_t size, size_t alignment) {
    auto* data = static_cast<u8*>(pool->allocate(size + ((size + 7) / 8)));
    MIREvalAllocationPtr rv;
    rv.ptr = pool->make<MIREvalAllocation>(data, size, HIRTypeRef());
    rv->isConstHeap = true;
    rv->heapAlignment = alignment;
    return rv;
}

MIREvalAllocationPtr MIREvalAllocationPtr::allocateRo(stl::ObjPool* pool, const void* dataIn, size_t len) {
    auto* data = static_cast<u8*>(pool->allocate(len + ((len + 7) / 8)));
    MIREvalAllocationPtr rv;
    rv.ptr = pool->make<MIREvalAllocation>(data, len, HIRTypeRef());
    rv->writeBytes(0, dataIn, len);
    rv->isReadonly = true;
    return rv;
}

// ---
MIREvalStaticRefPtr MIREvalStaticRefPtr::allocate(stl::ObjPool* pool, HIRPath p, const EncodedLiteral* lit, size_t len, bool valuePending) {
    MIREvalStaticRefPtr rv;
    rv.ptr = pool->make<MIREvalStaticRef>(pool, std::move(p), lit, len, valuePending);
    return rv;
}

// --- RelocPtr ---
IValue* MIREvalRelocPtr::asValuePtr() const {
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
                    BUG(sp, "Layout not computable during const evaluation - " << "array element in " << *ty);
                }
                ty = &array->inner;
                continue;
            }
            r = TargetGetTypeRepr(sp, resolve, *ty);
            if (!r) {
                BUG(sp, "Layout not computable during const evaluation - " << "repr of " << *ty);
            }
            assert(f < r->fields.size());
            ofs += r->fields[f].offset;
            ty = &r->fields[f].ty;
        }

        return ofs;
    }

    EntPtr getEntFullpath(const Span& sp, const ::StaticTraitResolve& resolve, const HIRPath& path, EntNS ns, MonomorphState& outMs, const HIRGenericParams** outImplParamsDef = nullptr) {
        if (const auto* gp = path.data.opt_Generic()) {
            const auto& name = gp->path.components().back();
            const auto* mod = (gp->path.components().size() > 1) ? resolve.hirCrate().getTypeitemByPath(sp, gp->path, false, /*ignore_last*/ true).opt_Module() : &resolve.hirCrate().getModByPath(sp, gp->path, true);
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
        switch (v.tag()) {
            case TypeckValuePtr::TAG_NotFound: {
                return EntPtr();
            }
            case TypeckValuePtr::TAG_NotYetKnown: {
                THROW_DEFER(sp, NotYetKnown, "value of " << path);
            }
            case TypeckValuePtr::TAG_Constant: {
                auto& e = v.as_Constant();
                return e;
            }
            case TypeckValuePtr::TAG_Static: {
                auto& e = v.as_Static();
                return e;
            }
            case TypeckValuePtr::TAG_Function: {
                auto& e = v.as_Function();
                return e;
            }
            case TypeckValuePtr::TAG_EnumConstructor: {
                auto& e = v.as_EnumConstructor();
                return EntPtr::Data_Enum{e.e, e.v};
            }
            case TypeckValuePtr::TAG_EnumValue: {
                TODO(sp, "Handle EnumValue - " << path);
                break;
            }
            case TypeckValuePtr::TAG_StructConstructor: {
                auto& e = v.as_StructConstructor();
                return e.s;
            }
            case TypeckValuePtr::TAG_StructConstant: {
                TODO(sp, "Handle StructConstant - " << path);
                break;
            }
        }
        throw "";
    }

    const RcString* getRustcIntrinsicName(const Span& sp, const StaticTraitResolve& resolve, const HIRPath& path) {
        const auto* generic = path.data.opt_Generic();
        if (!generic) {
            return nullptr;
        }
        const auto& simple = generic->path;
        const auto components = simple.components();
        if (components.empty()) {
            return nullptr;
        }
        MonomorphState intrinsicMs(resolve.hirCrate().types);
        const auto entity = getEntFullpath(sp, resolve, path, EntNS::Value, intrinsicMs);
        if (const auto* function = entity.opt_Function(); function && (**function).markings.isRustcIntrinsic) {
            return &components.back();
        }
        return nullptr;
    }

    struct TypeInfo {
        enum {
            Other,
            Float,
            Signed,
            Unsigned,
        } ty;

        unsigned bits;

        static TypeInfo forPrimitive(HIRCoreType te) {
            switch (te) {
                case HIRCoreType::I8:
                    return TypeInfo{Signed, 8};
                case HIRCoreType::U8:
                    return TypeInfo{Unsigned, 8};
                case HIRCoreType::I16:
                    return TypeInfo{Signed, 16};
                case HIRCoreType::U16:
                    return TypeInfo{Unsigned, 16};
                case HIRCoreType::I32:
                    return TypeInfo{Signed, 32};
                case HIRCoreType::U32:
                    return TypeInfo{Unsigned, 32};
                case HIRCoreType::I64:
                    return TypeInfo{Signed, 64};
                case HIRCoreType::U64:
                    return TypeInfo{Unsigned, 64};
                case HIRCoreType::I128:
                    return TypeInfo{Signed, 128};
                case HIRCoreType::U128:
                    return TypeInfo{Unsigned, 128};

                case HIRCoreType::Isize:
                    return TypeInfo{Signed, TargetGetPointerBits()};
                case HIRCoreType::Usize:
                    return TypeInfo{Unsigned, TargetGetPointerBits()};
                case HIRCoreType::Char:
                    return TypeInfo{Unsigned, 21};
                case HIRCoreType::Bool:
                    return TypeInfo{Unsigned, 1};

                case HIRCoreType::F16:
                    return TypeInfo{Float, 16};
                case HIRCoreType::F32:
                    return TypeInfo{Float, 32};
                case HIRCoreType::F64:
                    return TypeInfo{Float, 64};
                case HIRCoreType::F128:
                    return TypeInfo{Float, 128};

                case HIRCoreType::Str:
                    return TypeInfo{Other, 0};
            }
            return TypeInfo{Other, 0};
        }

        static TypeInfo forType(const HIRTypeData* ty) {
            if (const auto* pattern = ty->opt_Pattern()) {
                return forType(pattern->inner);
            }
            if (!ty->is_Primitive()) {
                return TypeInfo{Other, 0};
            }
            return forPrimitive(ty->as_Primitive());
        }

        U128 mask(U128 v) const {
            if (bits < 64) {
                u64 maskVal = (static_cast<u64>(1ull) << bits) - 1;
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

class MIREvalPathCallback final: public MIRPathCallback {
    HIRItemPath path;

public:
    explicit MIREvalPathCallback(const HIRItemPath& path)
        : path(path)
    {
    }

    void write(::std::ostream& os) const override {
        os << path;
    }
};

class MIREvalCallStackEntry {
public:
    stl::ObjPool* const valuePool;
    const unsigned frameIndex;
    const std::vector<std::pair<HIRPattern, HIRTypeRef>> argDefs;
    const HIRTypeRef retType;
    const SourceLocation callerLocation;
    const bool tracksCaller;

    // MIR Resolve Helper
    const StaticTraitResolve& rootResolve;
    StaticTraitResolve resolve;
    MIREvalPathCallback pathCallback;
    MIRTypeResolve state;
    // Monomorphiser from the function
    MonomorphState ms;

    MIREvalAllocationPtr retval;

    ::std::vector<MIREvalAllocationPtr> args;

    ::std::vector<HIRTypeRef> localTypes;
    ::std::vector<MIREvalAllocationPtr> locals;
    ::std::vector<bool> dropFlags;

    // ---
    MIREvalCallStackEntry(const MIREvalCallStackEntry&) = delete;
    MIREvalCallStackEntry(MIREvalCallStackEntry&&) = delete;

    MIREvalCallStackEntry(
        stl::ObjPool* valuePool,
        unsigned frameIndex,
        const Span& rootSpan,
        const StaticTraitResolve& resolve,
        const MIREvalPathCallback& printPath,
        // Pre-monomorphised function signature (as this may be a `static`)
        HIRTypeRef expTy,
        std::vector<std::pair<HIRPattern, HIRTypeRef>> argDefs,
        // Function/Body code
        const MIRFunction& fcn,
        // Monomorphisation rules
        MonomorphState ms,
        ::std::vector<MIREvalAllocationPtr> args,
        const HIRGenericParams* itemParamsDef,
        const HIRGenericParams* implParamsDef,
        SourceLocation callerLocation,
        bool tracksCaller
    )
        : valuePool(valuePool)
        , frameIndex(frameIndex)
        , argDefs(std::move(argDefs))
        , retType(std::move(expTy))
        , callerLocation(std::move(callerLocation))
        , tracksCaller(tracksCaller)
        , rootResolve(resolve)
        , resolve(resolve.board())
        , pathCallback(printPath)
        , state{rootSpan, this->resolve, pathCallback, this->retType, this->argDefs, fcn}
        , ms(std::move(ms))
        , retval(MIREvalAllocationPtr::allocate(valuePool, rootResolve, state, retType))
        , args(args)
        , dropFlags(fcn.dropFlags)
    {
        this->resolve.setBothGenericsRaw(implParamsDef, itemParamsDef);
        localTypes.reserve(state.fcn.locals.size());
        locals.reserve(state.fcn.locals.size());
        for (size_t i = 0; i < state.fcn.locals.size(); i++) {
            auto localType = state.resolve.monomorphExpand(state.sp, state.fcn.locals[i], this->ms);
            state.resolve.revealOpaqueTypes(state.sp, localType);
            localTypes.push_back(std::move(localType));
            locals.push_back(MIREvalAllocationPtr::allocate(valuePool, rootResolve, state, localTypes.back()));
        }

        state.monomorphedRettype = retType;
        state.monomorphedLocals = &localTypes;
    }

    HIRTypeRef monomorphExpand(const HIRTypeData* ty) const {
        auto rv = this->resolve.monomorphExpand(this->state.sp, ty, this->ms);
        this->resolve.revealOpaqueTypes(this->state.sp, rv);
        return rv;
    }

    unsigned readEnumVariant(const HIRTypeData* ty, MIREvalValueRef value) const {
        auto* repr = TargetGetTypeRepr(state.sp, rootResolve, ty);
        MIR_ASSERT(state, repr, "No representation for enum " << ty);

        unsigned variant = 0;
                switch (repr->variants.tag()) {
                    case TypeReprVariantMode::TAG_None: {
                        break;
                    }
                    case TypeReprVariantMode::TAG_Linear: {
                        auto& ve = repr->variants.as_Linear();
                        auto tag = value.slice(repr->getOffset(state.sp, rootResolve, ve.field), ve.field.size).readUint(state, 8 * ve.field.size);
                        variant = ve.decodeTag(tag);
                        break;
                    }
                    case TypeReprVariantMode::TAG_Values: {
                        auto& ve = repr->variants.as_Values();
                        auto tag = value.slice(repr->getOffset(state.sp, rootResolve, ve.field), ve.field.size).readUint(state, 8 * ve.field.size).truncateU64();
                        auto it = std::find(ve.values.begin(), ve.values.end(), tag);
                        MIR_ASSERT(state, it != ve.values.end(), "Invalid enum tag " << tag << " for " << ty);
                        variant = it - ve.values.begin();
                        break;
                    }
                    case TypeReprVariantMode::TAG_NonZero: {
                        auto& ve = repr->variants.as_NonZero();
                        size_t offset = repr->getOffset(state.sp, rootResolve, ve.field);
                        bool isNonzero = false;
                        for (size_t i = 0; i < ve.field.size; i++) {
                            if (value.slice(offset + i, 1).readUint(state, 8) != U128(0)) {
                                isNonzero = true;
                                break;
                            }
                        }
                        variant = isNonzero ? 1 - ve.zeroVariant : ve.zeroVariant;
                        break;
                    }
                }
                return variant;
    }

    static bool allocationReachableFrom(const MIREvalAllocation* allocation, const MIREvalAllocation* target, ::std::set<const MIREvalAllocation*>& visited) {
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

    bool valueReachableFromReturn(MIREvalValueRef value) const {
        const auto* target = value.getStorage().asAllocation();
        if (!target) {
            return false;
        }
        ::std::set<const MIREvalAllocation*> visited;
        return allocationReachableFrom(retval.operator->(), target, visited);
    }

    bool valueNeedsNonConstDrop(const HIRTypeData* ty, MIREvalValueRef value) const {
        if (!rootResolve.typeNeedsDropGlue(state.sp, ty)) {
            return false;
        }

                switch ((*ty).tag()) {
                    case HIRTypeData::TAG_Diverge: {
                        return false;
                    }
                    case HIRTypeData::TAG_Infer: {
                        return true;
                    }
                    case HIRTypeData::TAG_ErasedType: {
                        return true;
                    }
                    case HIRTypeData::TAG_NodeType: {
                        auto* repr = TargetGetTypeRepr(state.sp, rootResolve, ty);
                        MIR_ASSERT(state, repr, "No representation for " << ty);
                        for (const auto& field : repr->fields) {
                            auto size = sizeOfOrBug(field.ty);
                            if (valueNeedsNonConstDrop(field.ty, value.slice(field.offset, size))) {
                                return true;
                            }
                        }
                        return false;
                    }
                    case HIRTypeData::TAG_Generic: {
                        return true;
                    }
                    case HIRTypeData::TAG_Primitive: {
                        return false;
                    }
                    case HIRTypeData::TAG_Pattern: {
                        auto& te = (*ty).as_Pattern();
                        return valueNeedsNonConstDrop(te.inner, value);
                    }
                    case HIRTypeData::TAG_Pointer: {
                        return false;
                    }
                    case HIRTypeData::TAG_NamedFunction: {
                        return false;
                    }
                    case HIRTypeData::TAG_Function: {
                        return false;
                    }
                    case HIRTypeData::TAG_Borrow: {
                        auto& te = (*ty).as_Borrow();
                        if (te.type != HIRBorrowType::Owned) {
                            return false;
                        }
                        auto pointer = value.readPtr(state);
                        MIR_ASSERT(state, pointer.first >= EncodedLiteral::PTR_BASE, "Invalid owned pointer while checking a constant drop");
                        auto size = sizeOfOrBug(te.inner);
                        auto inner = MIREvalValueRef(pointer.second, pointer.first - EncodedLiteral::PTR_BASE).slice(0, size);
                        return valueNeedsNonConstDrop(te.inner, inner);
                    }
                    case HIRTypeData::TAG_Path: {
                        auto& te = (*ty).as_Path();
                        const auto* markings = te.binding.getTraitMarkings();
                        if (!markings) {
                            return true;
                        }
                        // A `impl const Drop` destructor may run at compile time; the
                        // fields still have to be droppable there too.
                        if (markings->hasDropImpl && !markings->hasConstDropImpl) {
                            return true;
                        }

                                switch (te.binding.tag()) {
                                    case HIRTypePathBinding::TAG_Unbound: {
                                        return true;
                                    }
                                    case HIRTypePathBinding::TAG_Opaque: {
                                        return true;
                                    }
                                    case HIRTypePathBinding::TAG_ExternType: {
                                        return false;
                                    }
                                    case HIRTypePathBinding::TAG_Union: {
                                        return false;
                                    }
                                    case HIRTypePathBinding::TAG_Struct: {
                                        auto* repr = TargetGetTypeRepr(state.sp, rootResolve, ty);
                                        MIR_ASSERT(state, repr, "No representation for struct " << ty);
                                        for (const auto& field : repr->fields) {
                                            auto size = sizeOfOrBug(field.ty);
                                            if (valueNeedsNonConstDrop(field.ty, value.slice(field.offset, size))) {
                                                return true;
                                            }
                                        }
                                        return false;
                                    }
                                    case HIRTypePathBinding::TAG_Enum: {
                                        auto& pbe = te.binding.as_Enum();
                                        const auto* variants = pbe->data.opt_Data();
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
                                        return valueNeedsNonConstDrop(field.ty, value.slice(field.offset, size));
                                    }
                                }
                                throw std::runtime_error("Unreachable path binding");
                    }
                    case HIRTypeData::TAG_Array: {
                        auto& te = (*ty).as_Array();
                        auto count = te.size.as_Known();
                        if (count == 0) {
                            return false;
                        }
                        auto size = sizeOfOrBug(te.inner);
                        for (size_t i = 0; i < count; i++) {
                            if (valueNeedsNonConstDrop(te.inner, value.slice(i * size, size))) {
                                return true;
                            }
                        }
                        return false;
                    }
                    case HIRTypeData::TAG_Slice: {
                        return true;
                    }
                    case HIRTypeData::TAG_TraitObject: {
                        return true;
                    }
                    case HIRTypeData::TAG_Tuple: {
                        auto* repr = TargetGetTypeRepr(state.sp, rootResolve, ty);
                        MIR_ASSERT(state, repr, "No representation for tuple " << ty);
                        for (const auto& field : repr->fields) {
                            auto size = sizeOfOrBug(field.ty);
                            if (valueNeedsNonConstDrop(field.ty, value.slice(field.offset, size))) {
                                return true;
                            }
                        }
                        return false;
                    }
                }
                throw std::runtime_error("Unreachable type while checking a constant drop");
    }

    MIREvalStaticRefPtr getStaticrefMono(const HIRPath& p, HIRTypeRef* outTy = nullptr) const {
        // NOTE: Value won't need to be monomorphed, as it shouldn't be generic
        return getStaticref(ms.monomorphPath(state.sp, p), outTy);
    }

    MIREvalStaticRefPtr getStaticref(HIRPath p, HIRTypeRef* outTy = nullptr) const {
        rootResolve.revealOpaqueTypesPath(state.sp, p);
        // If there's any mention of generics in this path, then return Literal::Defer
        if (visitPathTysWith(p, [&](const auto& ty) -> bool {
            return ty->is_Generic();
        })) {
            THROW_DEFER(state.sp, GenericValue, "static " << p << " references a generic parameter");
        }

        // A trait-object method function pointer names the generated vtable
        // dispatch shim.  Resolving it as an ordinary trait item can only see
        // the object bound (or a fuzzy blanket impl), but the symbolic path is
        // exactly what translation needs in order to generate that shim.
        if (const auto* pe = p.data.opt_UfcsKnown()) {
            if (const auto* tyDyn = pe->type->opt_TraitObject()) {
                if (pe->item != "vtable#" && tyDyn->trait.traitPtr
                    && tyDyn->trait.traitPtr->getVtableValueIndex(pe->trait, pe->item) > 0) {
                    return MIREvalStaticRefPtr::allocate(valuePool, std::move(p), nullptr, 0);
                }
            }
        }

        MonomorphState constMs(rootResolve.crate.types);

        const HIRGenericParams* implParamsDef = nullptr;
        auto ent = getEntFullpath(state.sp, rootResolve, p, EntNS::Value, constMs, &implParamsDef);
        if (ent.is_Static()) {
            const auto& s = *ent.as_Static();
            auto staticTy = constMs.monomorphType(state.sp, s.type);
            size_t staticSize;
            if (!TargetGetSizeOf(state.sp, rootResolve, staticTy, staticSize)) {
                BUG(state.sp, "Layout not computable during const evaluation - " << "sizeof " << staticTy);
            }
            if (outTy) {
                *outTy = staticTy;
            }

            if (!s.valueGenerated) {
                // If there's no MIR and no HIR then this is an external static (which can only be borrowed)
                if (!s.value && !s.value.mir) {
                    DEBUG("No value and no mir");
                    return MIREvalStaticRefPtr::allocate(valuePool, std::move(p), nullptr, staticSize);
                }

                auto& item = const_cast<HIRStatic&>(s);

                if (!item.valueEvaluating) {
                    item.valueEvaluating = true;
                    STD_DEFER {
                        item.valueEvaluating = false;
                    };
                    ConvertHIRConstantEvaluateStatic(resolve.board(), resolve.hirCrate(), implParamsDef, p, item);
                } else {
                    DEBUG("Static " << p << " is already being evaluated; address-only reference");
                }
            }

            if (!s.valueGenerated) {
                auto& item = const_cast<HIRStatic&>(s);

                // A static that names itself (through a promoted, say) only
                // needs its address here, which is a relocation -- asking for
                // its bytes while they are being worked out would never finish.
                // Reading the bytes of such a reference is a value cycle.
                if (item.valueEvaluating) {
                    DEBUG("- Already being evaluated, taking the address only: " << p);
                    return MIREvalStaticRefPtr::allocate(valuePool, std::move(p), nullptr, staticSize, /*valuePending=*/true);
                }
                if (item.value.state) {
                    switch (item.value.state->stage) {
                        case HIRExprState::Stage::ConstEvalRequest:
                        case HIRExprState::Stage::TypecheckRequest:
                        case HIRExprState::Stage::SbcRequest:
                        case HIRExprState::Stage::ExpandRequest:
                        case HIRExprState::Stage::MirRequest:
                            DEBUG("- Already being worked out, taking the address only: " << p);
                            return MIREvalStaticRefPtr::allocate(valuePool, std::move(p), nullptr, staticSize, /*valuePending=*/true);
                        default:
                            break;
                    }
                }

                // Challenge: Adding items to the module might invalidate an iterator.
                HIRItemPath modIp{item.value.state->modPath};
                auto nvs = NewvalState(item.value.state->module, modIp, FMT("static" << &item << "#"));
                auto eval = HIREvaluator(item.value.span(), rootResolve.wb, nvs);
                DEBUG("- Evaluate " << p);
                try {
                    item.valueGenerated = true;
                    item.valueRes = eval.evaluateConstant(HIRItemPath(p), item.value, staticTy, std::move(constMs));
                    item.valueGenerated = true;
                } catch (const Defer& e) {
                    MIR_BUG(state, p << " Defer(" << e.reason << ") during value generation");
                }
                DEBUG(p << " = " << item.valueRes);
            }
            const auto* value = s.valueRes.bytes.size() == staticSize ? &s.valueRes : nullptr;
            return MIREvalStaticRefPtr::allocate(valuePool, std::move(p), value, staticSize);
        } else {
            DEBUG(ent.tagStr() << " " << p);
            if (outTy) {
                MIR_TODO(state, "Get type for " << ent.tagStr() << " (" << p << ")");
            }
            return MIREvalStaticRefPtr::allocate(valuePool, std::move(p), nullptr, 0);
        }
    }

    MIREvalValueRef getLval(const MIRLValue& lv, MIREvalValueRef* meta = nullptr, u64* rawAddress = nullptr) {
        HIRTypeRef tmpTy = nullptr;
        const HIRTypeData* typ = nullptr;
        MIREvalValueRef metadata;
        MIREvalValueRef val;
            switch (lv.root.tag()) {
                case MIRLValue::Storage::TAG_Return: {
                    typ = retType;
                    val = MIREvalValueRef(retval);
                    break;
                }
                case MIRLValue::Storage::TAG_Local: {
                    decltype(lv.root.as_Local()) e = lv.root.as_Local();
                    MIR_ASSERT(state, e < locals.size(), "Local index out of range - " << e << " >= " << locals.size());
                    typ = localTypes[e];
                    val = MIREvalValueRef(locals[e]);
                    break;
                }
                case MIRLValue::Storage::TAG_Argument: {
                    decltype(lv.root.as_Argument()) e = lv.root.as_Argument();
                    MIR_ASSERT(state, e < args.size(), "Argument index out of range - " << e << " >= " << args.size());
                    typ = state.args[e].second;
                    val = MIREvalValueRef(args[e]);
                    break;
                }
                case MIRLValue::Storage::TAG_Static: {
                    decltype(lv.root.as_Static()) e = lv.root.as_Static();
                    val = MIREvalValueRef(getStaticrefMono(e, lv.wrappers.empty() ? nullptr : &tmpTy));
                    if (!lv.wrappers.empty()) {
                        MIR_ASSERT(state, tmpTy != HIRTypeRef(), "Type not set?");
                    }
                    typ = tmpTy;
                    break;
                }
            }

            for(const auto& w : lv.wrappers)
            {
            MIR_ASSERT(state, typ, "Type not set when unwrapping - " << lv);
            DEBUG(w << " " << val << ": " << typ);
                switch (w.tag()) {
                    case MIRLValue::Wrapper::TAG_Field: {
                        decltype(w.as_Field()) e = w.as_Field();
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
                            metadata = MIREvalValueRef();
                            size_t sz, al;
                            if (!TargetGetSizeAndAlignOf(state.sp, rootResolve, typ, sz, al)) {
                                BUG(state.sp, "Layout not computable during const evaluation - " << "size/align of " << typ);
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
                            break;
                        }
                        auto* repr = TargetGetTypeRepr(state.sp, this->rootResolve, typ);
                        MIR_ASSERT(state, repr, "No repr for " << typ);
                        MIR_ASSERT(state, e < repr->fields.size(), "LValue::Field index out of range");
                        if (repr->size != SIZE_MAX) {
                            metadata = MIREvalValueRef();
                        }
                        auto ofs = repr->fields[e].offset;
                        typ = repr->fields[e].ty;

                        size_t sz, al;
                        if (!TargetGetSizeAndAlignOf(state.sp, rootResolve, typ, sz, al)) {
                            BUG(state.sp, "Layout not computable during const evaluation - " << "size/align of " << typ);
                        }
                        if (sz == SIZE_MAX) {
                            val = val.slice(ofs);
                        } else {
                            val = val.slice(ofs, sz);
                        }
                        break;
                    }
                    case MIRLValue::Wrapper::TAG_Deref: {
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
                            BUG(state.sp, "Layout not computable during const evaluation - " << "size/align of " << typ);
                        }
                        if (sz == SIZE_MAX) {
                            // Read metadata
                            DEBUG("Reading metadata");
                            metadata = val.slice(TargetGetPointerBits() / 8);
                        }
                        auto p = val.readPtr(state);
                        if (auto* staticRef = p.second.asStaticref(); staticRef && !staticRef->hasValue()) {
                            p.second = MIREvalRelocPtr(getStaticref(staticRef->path().clone()));
                        }
                        MIR_ASSERT(state, p.first % al == 0, "Unaligned pointer deref");
                        bool zeroSizedDeref = sz == 0;
                        if (sz == SIZE_MAX) {
                            size_t itemSize = 1;
                            if (const auto* slice = typ->opt_Slice()) {
                                if (!TargetGetSizeOf(state.sp, rootResolve, slice->inner, itemSize)) {
                                    BUG(state.sp, "Layout not computable during const evaluation - " << "sizeof " << slice->inner);
                                }
                            } else if (typ != HIRCoreType::Str) {
                                itemSize = 1;
                            }
                            zeroSizedDeref = itemSize == 0 || metadata.readUsize(state) == 0;
                        }
                        if (p.first < EncodedLiteral::PTR_BASE) {
                            MIR_ASSERT(state, p.first != 0 && !p.second && zeroSizedDeref && rawAddress,
                                "Null (<PTR_BASE) pointer deref");
                            MIR_ASSERT(state, &w == &lv.wrappers.back(), "Raw pointer deref followed by an lvalue projection");
                            *rawAddress = p.first;
                            val = MIREvalValueRef();
                            break;
                        }
                        DEBUG("> " << MIREvalValueRef(p.second) << " - o=" << (p.first - EncodedLiteral::PTR_BASE) << " sz=" << sz << " " << typ);
                        // TODO: Determine size using metadata?
                        if (sz == SIZE_MAX) {
                            val = MIREvalValueRef(p.second, p.first - EncodedLiteral::PTR_BASE);
                        } else {
                            val = MIREvalValueRef(p.second, p.first - EncodedLiteral::PTR_BASE).slice(0, sz);
                        }
                        break;
                    }
                    case MIRLValue::Wrapper::TAG_Index: {
                        decltype(w.as_Index()) e = w.as_Index();
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
                        metadata = MIREvalValueRef();
                        size_t sz, al;
                        if (!TargetGetSizeAndAlignOf(state.sp, rootResolve, typ, sz, al)) {
                            BUG(state.sp, "Layout not computable during const evaluation - " << "size/align of " << typ);
                        }
                        MIR_ASSERT(state, sz < SIZE_MAX, "Unsized type on index output - " << typ);
                        MIR_ASSERT(state, e < locals.size(), "LValue::Index index local out of range");
                        size_t index = MIREvalValueRef(locals[e]).readUsize(state);
                        MIR_ASSERT(state, index < size, "LValue::Index index out of range - " << index << " >= " << size);
                        val = val.slice(index * sz, sz);
                        break;
                    }
                    case MIRLValue::Wrapper::TAG_Downcast: {
                        decltype(w.as_Downcast()) e = w.as_Downcast();
                        auto* repr = TargetGetTypeRepr(state.sp, this->rootResolve, typ);
                        MIR_ASSERT(state, repr, "No repr for " << typ);
                        MIR_ASSERT(state, e < repr->fields.size(), "LValue::Downcast index out of range");
                        if (repr->size != SIZE_MAX) {
                            metadata = MIREvalValueRef();
                        }
                        typ = repr->fields[e].ty;
                        val = val.slice(repr->fields[e].offset, sizeOfOrBug(typ));
                        break;
                    }
                }
            }
            if(meta)
                *meta = std::move(metadata);
            return val;
    }

    const EncodedLiteral& getConst(const HIRPath& inP, HIRTypeRef* outTy) const {
        auto p = ms.monomorphPath(state.sp, inP);
        rootResolve.revealOpaqueTypesPath(state.sp, p);
        // If there's any mention of generics in this path, then return Literal::Defer
        if (visitPathTysWith(p, [&](const auto& ty) -> bool {
            return ty->is_Generic();
        })) {
            THROW_DEFER(state.sp, GenericValue, "constant " << p << " references a generic parameter");
        }
        MonomorphState constMs(rootResolve.crate.types);
        const HIRGenericParams* implParamsDef = nullptr;
        auto ent = getEntFullpath(state.sp, rootResolve, p, EntNS::Value, constMs, &implParamsDef);
        MIR_ASSERT(state, ent.is_Constant(), "MIR Constant::Const(" << p << ") didn't point to a Constant - " << ent.tagStr());
        const auto& c = *ent.as_Constant();
        if (c.valueState == HIRConstant::ValueState::InProgress) {
            ERROR(state.sp, E0000, "cycle detected when evaluating constant `" << p << "`");
        }
        if (c.valueState == HIRConstant::ValueState::Unknown) {
            auto& item = const_cast<HIRConstant&>(c);
            // This evaluates the *definition* under an identity substitution:
            // a body that names its generic environment (impl params, Self)
            // is per-instantiation and goes through the monomorph cache
            // below instead of being attempted here.
            const auto& caps = exprCaptures(rootResolve.crate.types, item.value);
            const bool bodyNamesEnv = caps.usesSelf || caps.typeMask[0] || caps.typeMask[1] || caps.valueMask[0] || caps.valueMask[1];
            if (!caps.unknown && bodyNamesEnv) {
                item.valueState = HIRConstant::ValueState::Generic;
            } else {
                // Challenge: Adding items to the module might invalidate an iterator.
                HIRItemPath modIp{item.value.state->modPath};
                auto nvs = NewvalState(item.value.state->module, modIp, FMT("const" << &c << "#"));
                auto eval = HIREvaluator(item.value.span(), rootResolve.wb, nvs);
                eval.resolve.setBothGenericsRaw(implParamsDef, &c.params);
                auto tempPpImpl = implParamsDef ? implParamsDef->makeNopParams(rootResolve.crate.types, 0) : HIRPathParams();
                auto tempPpMethod = c.params.makeNopParams(rootResolve.crate.types, 1);
                MonomorphState tempMs(rootResolve.crate.types);
                tempMs.ppImpl = &tempPpImpl;
                tempMs.ppMethod = &tempPpMethod;
                if (!p.data.is_Generic()) {
                    tempMs.selfTy = rootResolve.crate.types.self();
                }
                DEBUG("- Evaluate " << p);
                item.valueState = HIRConstant::ValueState::InProgress;
                try {
                    item.valueRes = eval.evaluateConstant(HIRItemPath(p), item.value, item.type, std::move(tempMs));
                    item.valueState = HIRConstant::ValueState::Known;
                } catch (const Defer& e) {
                    // NotYetKnown: the legacy solver could not commit (DEFER.md
                    // stage 4). GenericValue with unknown captures: the scan
                    // could not rule out the environment, the interpreter did.
                    if (e.reason != Defer::Reason::NotYetKnown && !(caps.unknown && e.reason == Defer::Reason::GenericValue)) {
                        MIR_BUG(state, "Defer(" << e.reason << ") evaluating concrete constant " << p);
                    }
                    item.valueState = HIRConstant::ValueState::Generic;
                }
            }
        }
        if (outTy) {
            *outTy = constMs.monomorphType(state.sp, c.type);
        }
        if (c.valueState == HIRConstant::ValueState::Generic) {
            auto it = c.monomorphCache.find(p);
            if (it == c.monomorphCache.end()) {
                auto& item = const_cast<HIRConstant&>(c);
                // Challenge: Adding items to the module might invalidate an iterator.
                HIRItemPath modIp{item.value.state->modPath};
                auto nvs = NewvalState(item.value.state->module, modIp, FMT("const" << &c << "#"));
                auto eval = HIREvaluator(item.value.span(), rootResolve.wb, nvs);
                eval.resolve.setBothGenericsRaw(implParamsDef, &c.params);

                DEBUG("- Evaluate monomorphed " << p);
                DEBUG("> const_ms=" << constMs);
                auto ty = constMs.monomorphType(item.value.span(), item.type);
                auto val = eval.evaluateConstant(HIRItemPath(p), item.value, std::move(ty), std::move(constMs));

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

    void writeEncoded(MIREvalValueRef dst, const EncodedLiteral& encoded) {
        // Write the encoded value into the destination
        dst.writeBytes(state, encoded.bytes.data(), encoded.bytes.size());
        for (const auto& r : encoded.relocations) {
            MIREvalRelocPtr reloc;
            if (r.p) {
                reloc = MIREvalRelocPtr(getStaticref(r.p->clone()));
            } else {
                reloc = MIREvalRelocPtr(MIREvalAllocationPtr::allocateRo(valuePool, r.bytes.data(), r.bytes.size()));
            }
            dst.slice(r.ofs, r.len).setReloc(std::move(reloc));
        }
    }

    void writeConst(MIREvalValueRef dst, const MIRConstant& c) {
            switch (c.tag()) {
                case MIRConstant::TAG_Int: {
                    auto& e2 = c.as_Int();
                    dst.writeSint(state, dst.getLen() * 8, e2.v);
                    break;
                }
                case MIRConstant::TAG_Uint: {
                    auto& e2 = c.as_Uint();
                    dst.writeUint(state, dst.getLen() * 8, e2.v);
                    break;
                }
                case MIRConstant::TAG_Float: {
                    auto& e2 = c.as_Float();
                    dst.writeFloat(state, dst.getLen() * 8, e2.v);
                    break;
                }
                case MIRConstant::TAG_Bool: {
                    auto& e2 = c.as_Bool();
                    dst.writeUint(state, 1, e2.v);
                    break;
                }
                case MIRConstant::TAG_Bytes: {
                    auto& e2 = c.as_Bytes();
                    dst.writePtr(state, EncodedLiteral::PTR_BASE, MIREvalConstantPtr::allocate(valuePool, e2.data(), e2.size()));
                    break;
                }
                case MIRConstant::TAG_StaticString: {
                    auto& e2 = c.as_StaticString();
                    dst.writePtr(state, EncodedLiteral::PTR_BASE, MIREvalConstantPtr::allocate(valuePool, e2.data(), e2.size()));
                    dst.slice(TargetGetPointerBits() / 8).writeUint(state, TargetGetPointerBits(), e2.size());
                    break;
                }
                case MIRConstant::TAG_Encoded: {
                    auto& e2 = c.as_Encoded();
                    writeEncoded(dst, e2.value);
                    break;
                }
                case MIRConstant::TAG_Const: {
                    auto& e2 = c.as_Const();
                    HIRTypeRef ty;
                    assert(e2.p);
                    const auto& encoded = getConst(*e2.p, &ty);
                    DEBUG(*e2.p << " = " << encoded);

                    writeEncoded(dst, encoded);
                    break;
                }
                case MIRConstant::TAG_Generic: {
                    auto& e2 = c.as_Generic();
                    auto v = ms.getValue(state.sp, e2);
                    EncodedLiteral tmp;
                    const auto& encoded = getConst(v, tmp);
                    DEBUG(e2 << " = " << encoded);
                    writeEncoded(dst, encoded);
                    break;
                }
                case MIRConstant::TAG_Function: {
                    break;
                }
                case MIRConstant::TAG_ItemAddr: {
                    auto& e2 = c.as_ItemAddr();
                    assert(e2);
                    MIR_ASSERT(state, e2.offset.isU64(), "Item address offset is too large: " << e2.offset);
                    dst.writePtr(state, EncodedLiteral::PTR_BASE + e2.offset.truncateU64(), getStaticrefMono(*e2));
                    break;
                }
            }
    }

    /// Write a borrow of the given lvalue
    void writeBorrow(MIREvalValueRef dst, HIRBorrowType bt, const MIRLValue& lv) {
        MIREvalValueRef meta;
        u64 rawAddress = 0;
        auto val = this->getLval(lv, &meta, &rawAddress);
        if (rawAddress) {
            dst.writePtr(state, rawAddress, MIREvalRelocPtr());
        } else {
            dst.writePtr(state, EncodedLiteral::PTR_BASE + val.getOfs(), val.getStorage());
        }
        if (meta.isValid()) {
            auto ptrSize = TargetGetPointerBits() / 8;
            dst.slice(ptrSize).copyFrom(state, meta);
        }
    }

    void writeParam(MIREvalValueRef dst, const MIRParam& p) {
            switch (p.tag()) {
                case MIRParam::TAG_LValue: {
                    auto& e = p.as_LValue();
                    dst.copyFrom( state, this->getLval(e) );
                    break;
                }
                case MIRParam::TAG_Borrow: {
                    auto& e = p.as_Borrow();
                    writeBorrow(dst, e.type, e.val);
                    break;
                }
                case MIRParam::TAG_Constant: {
                    auto& e = p.as_Constant();
                    writeConst(dst, e);
                    break;
                }
            }
    }

    const EncodedLiteral& getConst(const HIRConstGeneric& v, EncodedLiteral& tmp) const {
            switch (v.tag()) {
                case HIRConstGeneric::TAG_Infer: {
                    THROW_DEFER(state.sp, Infer, "const generic is Infer");
                }
                case HIRConstGeneric::TAG_Generic: {
                    THROW_DEFER(state.sp, GenericValue, "const generic " << v);
                }
                case HIRConstGeneric::TAG_Unevaluated: {
                    auto& ve = v.as_Unevaluated();
                    if (!typeCanMonomorph(ve->selfType, ms)
                        || !pathParamsCanMonomorph(ve->paramsImpl, ms)
                        || !pathParamsCanMonomorph(ve->paramsItem, ms)) {
                        THROW_DEFER(state.sp, GenericValue, "unevaluated const cannot monomorph");
                    }
                    auto value = ve->monomorph(state.sp, ms, false);
                    if (!typeIsConcrete(value.selfType)
                        || !pathParamsAreConcrete(value.paramsImpl)
                        || !pathParamsAreConcrete(value.paramsItem)) {
                        THROW_DEFER(state.sp, GenericValue, "unevaluated const not concrete after monomorph");
                    }
                    const auto& expr = *value.expr;
                    MonomorphState valueMs(rootResolve.crate.types);
                    valueMs.selfTy = value.selfType;
                    valueMs.ppImpl = &value.paramsImpl;
                    valueMs.ppMethod = &value.paramsItem;
                    auto type = valueMs.monomorphType(state.sp, expr->resType);
                    tmp = evaluateConstgeneric(state.sp, rootResolve.wb, rootResolve.crate, type, value);
                    return tmp;
                }
                case HIRConstGeneric::TAG_Evaluated: {
                    auto& ve = v.as_Evaluated();
                    return *ve;
                }
            }
            throw "";
    }

    /// Read a floating point value from a MIR::Param
    FloatValue readParamFloat(unsigned bits, const MIRParam& p) const {
            switch (p.tag()) {
                case MIRParam::TAG_LValue: {
                    auto& e = p.as_LValue();
                    return const_cast<MIREvalCallStackEntry*>(this)->getLval(e).readFloat(state, bits);
                }
                case MIRParam::TAG_Borrow: {
                    MIR_BUG(state, "Expected a float, got a MIR::Param::Borrow");
                    break;
                }
                case MIRParam::TAG_Constant: {
                    auto& e = p.as_Constant();
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

    U128 readParamFloatBits(unsigned bits, const MIRParam& p) const {
            switch (p.tag()) {
                case MIRParam::TAG_LValue: {
                    auto& e = p.as_LValue();
                    return const_cast<MIREvalCallStackEntry*>(this)->getLval(e).readUint(state, bits);
                }
                case MIRParam::TAG_Borrow: {
                    MIR_BUG(state, "Expected a float, got a MIR::Param::Borrow");
                    break;
                }
                case MIRParam::TAG_Constant: {
                    auto& e = p.as_Constant();
                    if (e.is_Const()) {
                        const auto& val = getConst(*e.as_Const().p, nullptr);
                        return EncodedLiteralSlice(val).readUint();
                    }
                    if (e.is_Generic()) {
                        auto ve = ms.getValue(state.sp, e.as_Generic());
                        EncodedLiteral elTmp;
                        const auto& el = getConst(ve, elTmp);
                        return EncodedLiteralSlice(el).readUint();
                    }
                    MIR_ASSERT(state, e.is_Float(), "Expected a float, got " << e);
                    const auto value = e.as_Float().v;
                    switch (bits) {
                        case 16:
                            return U128(F16(value).v);
                        case 32: {
                            const float narrowed = static_cast<float>(value);
                            u32 raw;
                            memcpy(&raw, &narrowed, sizeof(raw));
                            return U128(raw);
                        }
                        case 64: {
                            const double narrowed = static_cast<double>(value);
                            u64 raw;
                            memcpy(&raw, &narrowed, sizeof(raw));
                            return U128(raw);
                        }
                        case 128:
                            return U128(value.bitsLo(), value.bitsHi());
                        default:
                            MIR_BUG(state, "Unexpected float size: " << bits);
                    }
                    break;
                }
            }
            abort();
    }

    U128 readParamUint(unsigned bits, const MIRParam& p) const {
            switch (p.tag()) {
                case MIRParam::TAG_LValue: {
                    auto& e = p.as_LValue();
                    return const_cast<MIREvalCallStackEntry*>(this)->getLval(e).readUint(state, bits);
                }
                case MIRParam::TAG_Borrow: {
                    MIR_BUG(state, "Expected an integer, got a MIR::Param::Borrow");
                    break;
                }
                case MIRParam::TAG_Constant: {
                    auto& e = p.as_Constant();
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

    S128 readParamSint(unsigned bits, const MIRParam& p) const {
            switch (p.tag()) {
                case MIRParam::TAG_LValue: {
                    auto& e = p.as_LValue();
                    return const_cast<MIREvalCallStackEntry*>(this)->getLval(e).readSint(state, bits);
                }
                case MIRParam::TAG_Borrow: {
                    MIR_BUG(state, "Expected an integer, got a MIR::Param::Borrow");
                    break;
                }
                case MIRParam::TAG_Constant: {
                    auto& e = p.as_Constant();
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

    std::pair<u64, MIREvalRelocPtr> readParamPtr(const MIRParam& p) const {
            switch (p.tag()) {
                case MIRParam::TAG_LValue: {
                    auto& e = p.as_LValue();
                    return const_cast<MIREvalCallStackEntry*>(this)->getLval(e).readPtr(state);
                }
                case MIRParam::TAG_Borrow: {
                    auto& e = p.as_Borrow();
                    MIREvalValueRef meta;
                    u64 rawAddress = 0;
                    auto value = const_cast<MIREvalCallStackEntry*>(this)->getLval(e.val, &meta, &rawAddress);
                    if (rawAddress) {
                        return ::std::make_pair(rawAddress, MIREvalRelocPtr());
                    }
                    return ::std::make_pair(EncodedLiteral::PTR_BASE + value.getOfs(), value.getStorage());
                }
                case MIRParam::TAG_Constant: {
                    auto& e = p.as_Constant();
                    if (!e.is_ItemAddr()) {
                        MIR_BUG(state, "Invalid argument for pointer: " << p);
                    }
                    MIR_ASSERT(state, e.as_ItemAddr().offset.isU64(), "Item address offset is too large: " << e.as_ItemAddr().offset);
                    // TODO: Look up the static
                    return ::std::make_pair(EncodedLiteral::PTR_BASE + e.as_ItemAddr().offset.truncateU64(), MIREvalRelocPtr(getStaticrefMono(*e.as_ItemAddr())));
                }
            }
            abort();
    }

    size_t sizeOfOrBug(const HIRTypeData* ty) const {
        size_t rv;
        if (!TargetGetSizeOf(state.sp, rootResolve, ty, /*out*/ rv)) {
            MIR_BUG(state, "No size for " << ty);
        }
        return rv;
    }
};

namespace {
    void resolveStaticPointer(
        MIREvalCallStackEntry& localState,
        ::std::pair<u64, MIREvalRelocPtr>& value
    ) {
        if (const auto* staticRef = value.second.asStaticref()) {
            value.second = MIREvalRelocPtr(localState.getStaticref(staticRef->path().clone()));
        }
    }

    bool samePointerProvenance(const MIREvalRelocPtr& left, const MIREvalRelocPtr& right) {
        if (left == right) {
            return true;
        }
        const auto* leftStatic = left.asStaticref();
        const auto* rightStatic = right.asStaticref();
        return leftStatic && rightStatic && leftStatic->path() == rightStatic->path();
    }

    MIREvalValueRef pointerBytes(
        MIREvalCallStackEntry& localState,
        ::std::pair<u64, MIREvalRelocPtr> pointer,
        size_t length,
        const char* intrinsic
    ) {
        const auto& state = localState.state;
        resolveStaticPointer(localState, pointer);
        MIR_ASSERT(state, pointer.second, "`" << intrinsic << "` cannot access an absolute pointer");
        MIR_ASSERT(state, pointer.first >= EncodedLiteral::PTR_BASE, "Invalid pointer passed to `" << intrinsic << "`");
        const u64 offset = pointer.first - EncodedLiteral::PTR_BASE;
        MIR_ASSERT(state, offset <= pointer.second.asValue().size(), "Pointer passed to `" << intrinsic << "` is out of bounds");
        MIR_ASSERT(state, length <= pointer.second.asValue().size() - offset, "Memory range passed to `" << intrinsic << "` is out of bounds");
        return MIREvalValueRef(pointer.second, offset).slice(0, length);
    }

    u8 pointerGuaranteedCmp(
        const ::std::pair<u64, MIREvalRelocPtr>& left,
        const ::std::pair<u64, MIREvalRelocPtr>& right
    ) {
        if (!left.second && !right.second) {
            return left.first == right.first ? 1 : 0;
        }

        // rustc deliberately reports two provenance-bearing pointers as
        // unknown, even when the interpreter can see the same allocation and
        // offset.  This intrinsic must not make CTFE pointer identity stronger
        // than upstream's public contract.
        if (left.second && right.second) {
            return 2;
        }

        const auto& relocated = left.second ? left : right;
        const auto& absolute = left.second ? right : left;
        if (absolute.first != 0 || relocated.first < EncodedLiteral::PTR_BASE) {
            return 2;
        }

        // A pointer within (or one byte past) a live allocation is definitely
        // non-null.  Wrapping pointers outside that range remain unknown.
        const u64 offset = relocated.first - EncodedLiteral::PTR_BASE;
        return offset <= relocated.second.asValue().size() ? 0 : 2;
    }

    ::std::pair<MIREvalValueRef, MIREvalValueRef> getTupleTBool(const MIREvalCallStackEntry& localState, MIREvalValueRef& src, const HIRTypeData* t) {
        auto tupleT = localState.rootResolve.crate.types.tuple({t, localState.rootResolve.crate.types.primitive(HIRCoreType::Bool)});
        auto* repr = TargetGetTypeRepr(localState.state.sp, localState.rootResolve, tupleT);
        MIR_ASSERT(localState.state, repr, "No repr for " << tupleT);
        auto s = localState.sizeOfOrBug(t);
        return std::make_pair(src.slice(repr->fields[0].offset, s), src.slice(repr->fields[1].offset, 1));
    }

    bool doArithChecked(
        MIREvalCallStackEntry& localState,
        const HIRTypeData* ty,
        MIREvalValueRef& dst,
        const MIRParam& valL,
        MIRBinOp op,
        const MIRParam& valR,
        // Should the output be saturated
        bool saturate = false
    ) {
        auto ti = TypeInfo::forType(ty);
        const auto& state = localState.state;
        bool didOverflow = false;

        // NOTE: Shifts can use any integer as the RHS, so give them special handling
        if (op == MIRBinOp::BIT_SHL || op == MIRBinOp::BIT_SHR) {
            HIRTypeRef tmpR;
            const auto& tyR = localState.state.getParamType(tmpR, valR);
            auto tiR = TypeInfo::forType(tyR);

            auto r = tiR.ty == TypeInfo::Unsigned ? localState.readParamUint(tiR.bits, valR) : localState.readParamSint(tiR.bits, valR).getInner();
            auto amt = r.truncateU64();
            if (amt > ti.bits) {
                DEBUG("Shift out of range - " << r << " > " << ti.bits);
                didOverflow = true;
                amt = 0;
            }
            switch (ti.ty) {
                case TypeInfo::Unsigned: {
                    auto l = localState.readParamUint(ti.bits, valL);
                    switch (op) {
                        case MIRBinOp::BIT_SHL:
                            dst.writeUint(state, ti.bits, ti.mask(l << amt));
                            break;
                        case MIRBinOp::BIT_SHR:
                            dst.writeUint(state, ti.bits, ti.mask(l >> amt));
                            break;
                        default:
                            MIR_BUG(state, "This block should only be active for SHL/SHR");
                    }
                    break;
                }
                case TypeInfo::Signed: {
                    auto l = localState.readParamSint(ti.bits, valL);
                    switch (op) {
                        case MIRBinOp::BIT_SHL:
                            dst.writeUint(state, ti.bits, ti.mask(l << amt));
                            break;
                        case MIRBinOp::BIT_SHR:
                            dst.writeUint(state, ti.bits, ti.mask(l >> amt));
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
            HIRTypeRef tmpR;
            MIR_ASSERT(state, ty == localState.state.getParamType(tmpR, valR), "BinOp with mismatched types");
        }

        switch (ti.ty) {
            case TypeInfo::Float: {
                auto l = localState.readParamFloat(ti.bits, valL);
                auto r = localState.readParamFloat(ti.bits, valR);
                auto writeResult = [&](FloatValue value) {
                    if (!floatValueIsNan(value)) {
                        dst.writeFloat(state, ti.bits, value);
                        return;
                    }

                    switch (ti.bits) {
                        case 16:
                            dst.writeUint(state, ti.bits, U128(0x7e00));
                            break;
                        case 32:
                            dst.writeUint(state, ti.bits, U128(0x7fc00000));
                            break;
                        case 64:
                            dst.writeUint(state, ti.bits, U128(0x7ff8000000000000));
                            break;
                        case 128:
                            dst.writeUint(state, ti.bits, U128(0, 0x7fff800000000000));
                            break;
                        default:
                            MIR_BUG(state, "Invalid float width " << ti.bits);
                    }
                };
                switch (op) {
                    case MIRBinOp::ADD:
                        writeResult(l + r);
                        break;
                    case MIRBinOp::SUB:
                        writeResult(l - r);
                        break;
                    case MIRBinOp::MUL:
                        writeResult(l * r);
                        break;
                    case MIRBinOp::DIV:
                        writeResult(l / r);
                        break;
                    case MIRBinOp::MOD:
                        writeResult(floatValueRemainder(l, r));
                        break;
                    case MIRBinOp::ADD_OV:
                    case MIRBinOp::SUB_OV:
                    case MIRBinOp::MUL_OV:
                    case MIRBinOp::DIV_OV:
                        MIR_TODO(state, "do_arith float unimplemented - val = " << l << " , " << r);

                    case MIRBinOp::BIT_OR:
                    case MIRBinOp::BIT_AND:
                    case MIRBinOp::BIT_XOR:
                        MIR_BUG(state, "do_arith float with bitwise - val = " << l << " , " << r);
                    case MIRBinOp::BIT_SHL:
                    case MIRBinOp::BIT_SHR:
                        MIR_BUG(state, "Bitshifts should be handled in caller");
                    case MIRBinOp::EQ:
                        dst.writeByte(state, l == r);
                        break;
                    case MIRBinOp::NE:
                        dst.writeByte(state, l != r);
                        break;
                    case MIRBinOp::GT:
                        dst.writeByte(state, l > r);
                        break;
                    case MIRBinOp::GE:
                        dst.writeByte(state, l >= r);
                        break;
                    case MIRBinOp::LT:
                        dst.writeByte(state, l < r);
                        break;
                    case MIRBinOp::LE:
                        dst.writeByte(state, l <= r);
                        break;
                }
                break;
            };
            case TypeInfo::Unsigned: {
                auto l = localState.readParamUint(ti.bits, valL);
                auto r = localState.readParamUint(ti.bits, valR);
                switch (op) {
                    case MIRBinOp::ADD: {
                        auto res = ti.mask(l + r);
                        didOverflow = res < l;
                        if (didOverflow && saturate) {
                            res = ti.mask(~U128());
                        }
                        dst.writeUint(state, ti.bits, res);
                        break;
                    }
                    case MIRBinOp::SUB: {
                        auto res = ti.mask(l - r);
                        didOverflow = res > l;
                        if (didOverflow && saturate) {
                            res = ti.mask(U128(0));
                        }
                        dst.writeUint(state, ti.bits, res);
                        break;
                    }
                    case MIRBinOp::MUL: {
                        auto res = ti.mask(l * r);
                        auto max = ti.mask(~U128());
                        didOverflow = r != 0 && l > max / r;
                        if (didOverflow && saturate) {
                            res = max;
                        }
                        dst.writeUint(state, ti.bits, res);
                        break;
                    }
                    case MIRBinOp::DIV:
                        // Early-prevent division by zero
                        if (r == 0) {
                            dst.writeUint(state, ti.bits, U128(0));
                            return true;
                        }
                        dst.writeUint(state, ti.bits, ti.mask(l / r));
                        break;
                    case MIRBinOp::MOD:
                        // Early-prevent division by zero
                        if (r == 0) {
                            dst.writeUint(state, ti.bits, U128(0));
                            return true;
                        }
                        dst.writeUint(state, ti.bits, ti.mask(l % r));
                        break;
                    case MIRBinOp::ADD_OV:
                    case MIRBinOp::SUB_OV:
                    case MIRBinOp::MUL_OV:
                    case MIRBinOp::DIV_OV:
                        MIR_TODO(state, "do_arith unsigned - val = " << l << " , " << r);

                    case MIRBinOp::BIT_OR:
                        dst.writeUint(state, ti.bits, l | r);
                        break;
                    case MIRBinOp::BIT_AND:
                        dst.writeUint(state, ti.bits, l & r);
                        break;
                    case MIRBinOp::BIT_XOR:
                        dst.writeUint(state, ti.bits, l ^ r);
                        break;
                    case MIRBinOp::BIT_SHL:
                    case MIRBinOp::BIT_SHR:
                        MIR_BUG(state, "Bitshifts should be handled in caller");

                    case MIRBinOp::EQ:
                        dst.writeByte(state, l == r);
                        break;
                    case MIRBinOp::NE:
                        dst.writeByte(state, l != r);
                        break;
                    case MIRBinOp::GT:
                        dst.writeByte(state, l > r);
                        break;
                    case MIRBinOp::GE:
                        dst.writeByte(state, l >= r);
                        break;
                    case MIRBinOp::LT:
                        dst.writeByte(state, l < r);
                        break;
                    case MIRBinOp::LE:
                        dst.writeByte(state, l <= r);
                        break;
                }
                break;
            }
            case TypeInfo::Signed: {
                auto l = localState.readParamSint(ti.bits, valL);
                DEBUG(l << " from " << valL);
                auto r = localState.readParamSint(ti.bits, valR);
                DEBUG(r << " from " << valR);
                DEBUG(l << " " << int(op) << " " << r);
                const auto signBit = U128(1) << (ti.bits - 1);
                const auto minValue = signBit;
                const auto maxValue = signBit - 1u;
                const auto lRaw = ti.mask(l.getInner());
                const auto rRaw = ti.mask(r.getInner());
                switch (op) {
                    case MIRBinOp::ADD: {
                        auto res = ti.mask(lRaw + rRaw);
                        didOverflow = (~(lRaw ^ rRaw) & (lRaw ^ res) & signBit) != 0;
                        if (didOverflow && saturate) {
                            res = (lRaw & signBit) != 0 ? minValue : maxValue;
                        }
                        dst.writeUint(state, ti.bits, res);
                        break;
                    }
                    case MIRBinOp::SUB: {
                        auto res = ti.mask(lRaw - rRaw);
                        didOverflow = ((lRaw ^ rRaw) & (lRaw ^ res) & signBit) != 0;
                        if (didOverflow && saturate) {
                            res = (lRaw & signBit) != 0 ? minValue : maxValue;
                        }
                        dst.writeUint(state, ti.bits, res);
                        break;
                    }
                    case MIRBinOp::MUL: {
                        const bool lNegative = (lRaw & signBit) != 0;
                        const bool rNegative = (rRaw & signBit) != 0;
                        const bool resultNegative = lNegative != rNegative;
                        const auto lMagnitude = lNegative ? ti.mask(~lRaw + 1u) : lRaw;
                        const auto rMagnitude = rNegative ? ti.mask(~rRaw + 1u) : rRaw;
                        const auto maxMagnitude = resultNegative ? minValue : maxValue;
                        didOverflow = rMagnitude != 0 && lMagnitude > maxMagnitude / rMagnitude;
                        auto res = ti.mask(lRaw * rRaw);
                        if (didOverflow && saturate) {
                            res = resultNegative ? minValue : maxValue;
                        }
                        dst.writeUint(state, ti.bits, res);
                        break;
                    }
                    case MIRBinOp::DIV:
                        if (r == 0) {
                            dst.writeUint(state, ti.bits, U128(0));
                            return true;
                        }
                        dst.writeSint(state, ti.bits, ti.mask(l / r));
                        break;
                    case MIRBinOp::MOD:
                        if (r == 0) {
                            dst.writeUint(state, ti.bits, U128(0));
                            return true;
                        }
                        dst.writeSint(state, ti.bits, ti.mask(l % r));
                        break;
                    case MIRBinOp::ADD_OV:
                    case MIRBinOp::SUB_OV:
                    case MIRBinOp::MUL_OV:
                    case MIRBinOp::DIV_OV:
                        MIR_TODO(state, "do_arith signed - val = " << l << " , " << r);

                    case MIRBinOp::BIT_OR:
                        dst.writeUint(state, ti.bits, (l | r).getInner());
                        break;
                    case MIRBinOp::BIT_AND:
                        dst.writeUint(state, ti.bits, (l & r).getInner());
                        break;
                    case MIRBinOp::BIT_XOR:
                        dst.writeUint(state, ti.bits, (l ^ r).getInner());
                        break;
                    case MIRBinOp::BIT_SHL:
                    case MIRBinOp::BIT_SHR:
                        MIR_BUG(state, "Bitshifts should be handled in caller");

                    case MIRBinOp::EQ:
                        dst.writeByte(state, l == r);
                        break;
                    case MIRBinOp::NE:
                        dst.writeByte(state, l != r);
                        break;
                    case MIRBinOp::GT:
                        dst.writeByte(state, l > r);
                        break;
                    case MIRBinOp::GE:
                        dst.writeByte(state, l >= r);
                        break;
                    case MIRBinOp::LT:
                        dst.writeByte(state, l < r);
                        break;
                    case MIRBinOp::LE:
                        dst.writeByte(state, l <= r);
                        break;
                }
                break;
            }
            case TypeInfo::Other: {
                const auto* borrowTy = ty->opt_Borrow();
                if (borrowTy && ((borrowTy->inner->is_Slice() && borrowTy->inner->as_Slice().inner == HIRCoreType::U8) || borrowTy->inner == HIRCoreType::Str)) {
                    struct P {
                        MIREvalRelocPtr reloc;
                        const void* data;
                        size_t len;

                        P(MIREvalCallStackEntry& localState, const MIRParam& p) {
                            auto vr = localState.getLval(p.as_LValue());
                            auto ptr = vr.readPtr(localState.state);
                            this->len = vr.slice(TargetGetPointerBits() / 8).readUsize(localState.state);
                            this->data = ptr.second.asValue().getBytes(ptr.first - EncodedLiteral::PTR_BASE, this->len, true);
                            MIR_ASSERT(localState.state, this->data, "Invalid pointer " << p << " : " << vr << " = " << ptr.second << " @ " << ptr.first << "+" << this->len);
                            this->reloc = std::move(ptr.second);
                        }
                    };

                    auto ptrL = P(localState, valL);
                    auto ptrR = P(localState, valR);
                    int cmp = memcmp(ptrL.data, ptrR.data, std::min(ptrL.len, ptrL.len));
                    if (cmp == 0) {
                        if (ptrL.len != ptrR.len) {
                            cmp = ptrL.len < ptrR.len ? -1 : 1;
                        }
                    }
                    switch (op) {
                        case MIRBinOp::EQ:
                            dst.writeByte(state, cmp == 0);
                            break;
                        case MIRBinOp::NE:
                            dst.writeByte(state, cmp != 0);
                            break;
                        case MIRBinOp::GT:
                            dst.writeByte(state, cmp > 0);
                            break;
                        case MIRBinOp::GE:
                            dst.writeByte(state, cmp >= 0);
                            break;
                        case MIRBinOp::LT:
                            dst.writeByte(state, cmp < 0);
                            break;
                        case MIRBinOp::LE:
                            dst.writeByte(state, cmp <= 0);
                            break;
                        default:
                            MIR_BUG(state, "BinOp " << int(op) << " on " << ty << " - Byte slice or &str");
                    }
                    break;
                } else {
                    MIR_BUG(state, "BinOp on " << ty);
                }
                break;
            }
        }
        return didOverflow;
    }
}

unsigned int HIREvaluator::sNextEvalIndex = 0;

HIREvaluator::CsePtr::~CsePtr() {
    if (inner) {
        delete inner;
        inner = nullptr;
    }
}

void HIREvaluator::pushStackEntry(HIRItemPath printPath, const MIRFunction& fcn, MonomorphState ms, HIRTypeRef exp, HIRFunction::argsT argDefs, ::std::vector<MIREvalAllocationPtr> args, const HIRGenericParams* itemParamsDef, const HIRGenericParams* implParamsDef, SourceLocation callerLocation, bool tracksCaller) {
    MIREvalPathCallback pathCallback(printPath);
    this->callStack.push_back(new MIREvalCallStackEntry(this->valuePool.mutPtr(), this->numFrames, this->rootSpan, this->resolve, pathCallback, std::move(exp), std::move(argDefs), fcn, std::move(ms), std::move(args), itemParamsDef, implParamsDef, std::move(callerLocation), tracksCaller));
    this->numFrames += 1;
}

MIREvalAllocationPtr HIREvaluator::runUntilStackEmpty() {
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
                MIREvalAllocationPtr rv = std::move(this->callStack.back()->retval);
                this->callStack.pop_back();
                if (this->callStack.empty() == 1) {
                    return rv;
                } else {
                    auto& nextState = *this->callStack.back();
                    const auto& term = nextState.state.fcn.blocks[nextState.state.getCurBlock()].terminator;
                    const auto& te = term.as_Call();
                    auto dst = nextState.getLval(te.retVal);
                    dst.copyFrom(nextState.state, MIREvalValueRef(rv));
                    nextState.state.setCurStmt(te.retBlock, 0);
                }
                break;
            }
            default:
                state.setCurStmt(nextBlock, 0);
        }
    }
    ERROR(this->rootSpan, E0000, "Constant evaluation ran for too long - " << numStmtsRun << " statements, " << idx << " blocks");
}

static void writeCtfeUnsizeMetadata(
    MIREvalCallStackEntry& localState,
    MIREvalValueRef dst,
    const HIRTypeData* dynamicTypeD,
    const HIRTypeData* dynamicTypeS
) {
    const auto& state = localState.state;
    while (const auto* pathD = dynamicTypeD->opt_Path()) {
        MIR_ASSERT(state, pathD->binding.is_Struct(), "Pointer unsize to " << dynamicTypeD);
        const auto* pathS = dynamicTypeS->opt_Path();
        MIR_ASSERT(state, pathS && pathS->binding.is_Struct() && pathS->binding.as_Struct() == pathD->binding.as_Struct(),
            "Pointer unsize from " << dynamicTypeS << " to " << dynamicTypeD);
        const auto& markings = pathD->binding.as_Struct()->structMarkings;
        MIR_ASSERT(state, markings.coerceUnsized != HIRStructMarkings::Coerce::None,
            "Pointer unsize through non-CoerceUnsized type " << dynamicTypeD);
        dynamicTypeD = pathD->path.data.as_Generic().params.types.at(markings.unsizedParam);
        dynamicTypeS = pathS->path.data.as_Generic().params.types.at(markings.unsizedParam);
    }

    const auto ptrSize = TargetGetPointerBits() / 8;
    if (const auto* traitObject = dynamicTypeD->opt_TraitObject()) {
        MIR_ASSERT(state, !dynamicTypeS->is_TraitObject(),
            "Trait-object pointer upcast must provide explicit metadata");
        static const RcString rcstringVtable = RcString::newInterned("vtable#");
        auto vtablePath = HIRPath(dynamicTypeS, traitObject->trait.path.clone(), rcstringVtable);
        auto vtable = MIREvalStaticRefPtr::allocate(localState.valuePool, std::move(vtablePath), nullptr, 0);
        dst.slice(ptrSize).writePtr(state, EncodedLiteral::PTR_BASE, std::move(vtable));
    } else if (dynamicTypeD->is_Slice()) {
        const auto* array = dynamicTypeS->opt_Array();
        MIR_ASSERT(state, array && array->size.is_Known(), "Pointer unsize to slice from " << dynamicTypeS);
        dst.slice(ptrSize).writeUint(state, TargetGetPointerBits(), array->size.as_Known());
    } else {
        const auto metadataType = localState.resolve.metadataType(state.sp, dynamicTypeD);
        MIR_ASSERT(state, metadataType == MetadataType::None || metadataType == MetadataType::Zero,
            "Unhandled pointer metadata " << metadataType << " for " << dynamicTypeD);
    }
}

template<typename WriteField>
static void writeCtfeEnumVariant(
    const StaticTraitResolve& resolve,
    MIREvalCallStackEntry& localState,
    MIREvalValueRef dst,
    const HIRTypeData* ty,
    size_t index,
    size_t valueCount,
    WriteField&& writeField
) {
    const auto& state = localState.state;
    auto* enmRepr = TargetGetTypeRepr(state.sp, resolve, ty);
    if (!enmRepr) {
        BUG(state.sp, "Layout not computable during const evaluation - " << "repr of " << ty);
    }
    if (valueCount > 0) {
        MIR_ASSERT(state, index < enmRepr->fields.size(), "Enum representation has no variant " << index << " for " << ty);
        const auto ofs = enmRepr->fields[index].offset;
        const auto& innerType = enmRepr->fields[index].ty;
        auto* innerRepr = TargetGetTypeRepr(state.sp, resolve, innerType);
        if (!innerRepr) {
            BUG(state.sp, "Layout not computable during const evaluation - " << "repr of " << innerType);
        }
        MIR_ASSERT(state, valueCount <= innerRepr->fields.size(),
            "Enum variant " << index << " has " << innerRepr->fields.size() << " fields, got " << valueCount);
        for (size_t i = 0; i < valueCount; i++) {
            const size_t size = localState.sizeOfOrBug(innerRepr->fields[i].ty);
            auto fieldDst = dst.slice(ofs + innerRepr->fields[i].offset, size);
            writeField(fieldDst, i);
            DEBUG("@" << (ofs + innerRepr->fields[i].offset) << " = " << fieldDst);
        }
    }

    switch (enmRepr->variants.tag()) {
        case TypeReprVariantMode::TAG_None: {
            break;
        }
        case TypeReprVariantMode::TAG_NonZero: {
            auto& variant = enmRepr->variants.as_NonZero();
            // No tag to write for the populated variant. The empty variant is
            // represented by zeroing the niche field.
            if (index == variant.zeroVariant) {
                auto offset = getOffset(state.sp, resolve, enmRepr, variant.field);
                const auto savedOffset = offset;
                for (size_t i = 0; i + 8 <= variant.field.size; i += 8) {
                    dst.slice(offset, 8).writeUint(state, 64, 0);
                    offset += 8;
                }
                if (variant.field.size % 8 > 0) {
                    dst.slice(offset, variant.field.size % 8).writeUint(state, (variant.field.size % 8) * 8, 0);
                }
                DEBUG("@" << offset << " = " << dst.slice(savedOffset, variant.field.size) << " NonZero");
            }
            break;
        }
        case TypeReprVariantMode::TAG_Linear: {
            auto& variant = enmRepr->variants.as_Linear();
            if (!variant.isNiche(index)) {
                const auto offset = getOffset(state.sp, resolve, enmRepr, variant.field);
                MIR_ASSERT(state, variant.field.size <= 64 / 8, "");
                dst.slice(offset, variant.field.size).writeUint(state, variant.field.size * 8, variant.tagValue(index));
            }
            break;
        }
        case TypeReprVariantMode::TAG_Values: {
            auto& variant = enmRepr->variants.as_Values();
            const auto& field = enmRepr->fields[variant.field.index];
            const auto typeInfo = TypeInfo::forType(field.ty);
            MIR_ASSERT(state, typeInfo.ty == TypeInfo::Signed || typeInfo.ty == TypeInfo::Unsigned,
                "EnumVariant: Values not integer - " << field.ty);
            auto tagDst = dst.slice(field.offset, (typeInfo.bits + 7) / 8);
            if (typeInfo.ty == TypeInfo::Signed) {
                tagDst.writeSint(state, typeInfo.bits, S128(variant.values.at(index)));
            } else {
                tagDst.writeUint(state, typeInfo.bits, variant.values.at(index));
            }
            break;
        }
    }
}

void HIREvaluator::runStatement(MIREvalCallStackEntry& localState, const MIRStatement& stmt) {
    const auto& state = localState.state;
    DEBUG("E" << this->evalIndex << " F" << localState.frameIndex << " " << state << stmt);

        switch (stmt.tag()) {
            case MIRStatement::TAG_Assign: {
                // Fall through
                break;
            }
            case MIRStatement::TAG_ScopeEnd: {
                // Just ignore, it's a hint
                return;
            }
            case MIRStatement::TAG_SetDropFlag: {
                auto& se = stmt.as_SetDropFlag();
                MIR_ASSERT(state, se.idx < localState.dropFlags.size(), "Drop flag " << se.idx << " out of range");
                if (se.other == UINT_MAX) {
                    localState.dropFlags[se.idx] = se.newVal;
                } else {
                    MIR_ASSERT(state, se.other < localState.dropFlags.size(), "Drop flag " << se.other << " out of range");
                    localState.dropFlags[se.idx] = se.newVal != localState.dropFlags[se.other];
                }
                return;
            }
            case MIRStatement::TAG_SaveDropFlag: {
                MIR_TODO(state, "Non-assign statement - " << stmt);
                break;
            }
            case MIRStatement::TAG_LoadDropFlag: {
                MIR_TODO(state, "Non-assign statement - " << stmt);
                break;
            }
            case MIRStatement::TAG_Asm: {
                MIR_TODO(state, "Non-assign statement - " << stmt);
                break;
            }
            case MIRStatement::TAG_Asm2: {
                MIR_TODO(state, "Non-assign statement - " << stmt);
                break;
            }
        }

        const auto& sa = stmt.as_Assign();

        auto dst = localState.getLval(sa.dst);
        switch (sa.src.tag()) {
            case MIRRValue::TAG_Use: {
                auto& e = sa.src.as_Use();
                dst.copyFrom(state, localState.getLval(e));
                break;
            }
            case MIRRValue::TAG_Constant: {
                auto& e = sa.src.as_Constant();
                localState.writeConst(dst, e);
                break;
            }
            case MIRRValue::TAG_Borrow: {
                auto& e = sa.src.as_Borrow();
                localState.writeBorrow(dst, e.type, e.val);
                break;
            }
            case MIRRValue::TAG_Cast: {
                auto& e = sa.src.as_Cast();
                HIRTypeRef tmp;
                const auto& srcTy = state.getLvalueType(tmp, e.val);

                auto inval = localState.getLval(e.val);
                auto castType = localState.monomorphExpand(e.type);

                switch ((*castType).tag()) {
default:
                    // NOTE: Can be an unsizing!
                    MIR_TODO(state, "RValue::Cast to " << castType << " from " << srcTy << ", val = " << inval);
                    break;
                    case HIRTypeData::TAG_Primitive: {
                        auto& te = (*castType).as_Primitive();
                        auto ti = TypeInfo::forPrimitive(te);
                        auto srcTi = TypeInfo::forType(srcTy);
                        switch (ti.ty) {
                            // Integers mask down
                            case TypeInfo::Signed:
                            case TypeInfo::Unsigned:
                                switch (srcTi.ty) {
                                    case TypeInfo::Signed: {
                                        auto v = inval.readSint(state, srcTi.bits);
                                        dst.writeUint(state, ti.bits, v.getInner());
                                    } break;
                                    case TypeInfo::Unsigned:
                                        MIR_ASSERT(state, !srcTy->is_NamedFunction(), "");
                                        dst.writeUint(state, ti.bits, inval.readUint(state, srcTi.bits));
                                        break;
                                    case TypeInfo::Float: {
                                        // A 128-bit destination needs both halves,
                                        // and does not fit the 64-bit writer. The
                                        // cast saturates, and NaN becomes zero.
                                        const double f = static_cast<double>(inval.readFloat(state, srcTi.bits));
                                        if (ti.ty == TypeInfo::Signed) {
                                            dst.writeSint(state, ti.bits, floatToSintSaturating(f, ti.bits));
                                        } else {
                                            dst.writeUint(state, ti.bits, floatToUintSaturating(f, ti.bits));
                                        }
                                    } break;
                                    case TypeInfo::Other: {
                                        MIR_ASSERT(state, ((*srcTy).is_Path() && ((*srcTy).as_Path().binding.is_Enum())), "Constant cast Variant to integer with invalid type - " << srcTy);
                                        MIR_ASSERT(state, srcTy->as_Path().binding.as_Enum(), "Enum binding pointer not set! - " << srcTy);
                                        const HIREnum& enm = *srcTy->as_Path().binding.as_Enum();
                                        MIR_ASSERT(state, enm.isValue(), "Constant cast Variant to integer with non-value enum - " << srcTy);
                                        const auto* repr = TargetGetTypeRepr(state.sp, resolve, srcTy);
                                        if (!repr) {
                                            BUG(state.sp, "Layout not computable during const evaluation - " << "repr of " << srcTy);
                                        }
                                        if (repr->variants.is_None()) {
                                            // One variant, so nothing is stored
                                            // to say which: the discriminant is
                                            // a constant on the enum.
                                            MIR_ASSERT(state, enm.numVariants() == 1, "Enum with no tag and " << enm.numVariants() << " variants - " << srcTy);
                                            dst.writeUint(state, ti.bits, U128(enm.getDiscriminant(0)));
                                            break;
                                        }
                                        auto& ve = repr->variants.as_Values();
                                        const auto& field = repr->fields.at(ve.field.index);
                                        auto src = inval.slice(repr->getOffset(state.sp, resolve, ve.field), ve.field.size);
                                        auto tagTi = TypeInfo::forType(field.ty);
                                        // TODO: Ensure that this is a valid variant?
                                        if (tagTi.ty == TypeInfo::Signed) {
                                            dst.writeSint(state, ti.bits, src.readSint(state, tagTi.bits));
                                        } else {
                                            MIR_ASSERT(state, tagTi.ty == TypeInfo::Unsigned, "Enum tag is not an integer - " << field.ty);
                                            dst.writeUint(state, ti.bits, src.readUint(state, tagTi.bits));
                                        }
                                    } break;
                                }
                                break;
                            case TypeInfo::Float:
                                switch (srcTi.ty) {
                                    // NOTE: Subtle rounding differences between f32 and f64
                                    case TypeInfo::Signed: {
                                        auto v = inval.readSint(state, srcTi.bits);
                                        dst.writeFloat(state, ti.bits, ti.bits == 32 ? v.toFloat() : v.toDouble());
                                        break;
                                    }
                                    case TypeInfo::Unsigned: {
                                        auto v = inval.readUint(state, srcTi.bits);
                                        dst.writeFloat(state, ti.bits, ti.bits == 32 ? v.toFloat() : v.toDouble());
                                        break;
                                    }
                                    case TypeInfo::Float:
                                        dst.writeFloat(state, ti.bits, inval.readFloat(state, srcTi.bits));
                                        break;
                                    case TypeInfo::Other:
                                        MIR_TODO(state, "Cast " << srcTy << " to float");
                                }
                                break;
                            default:
                                MIR_TODO(state, "RValue::Cast to " << castType << ", val = " << inval);
                        }
                        break;
                    }
break;
                    case HIRTypeData::TAG_Pointer: {
                        auto& de = (*castType).as_Pointer();
                        if (const auto* e = srcTy->opt_NamedFunction()) {
                            dst.writePtr(state, EncodedLiteral::PTR_BASE, localState.getStaticrefMono(e->path));
                        } else if (TypeInfo::forType(srcTy).ty == TypeInfo::Signed && TypeInfo::forType(srcTy).bits < dst.getLen() * 8) {
                            // A signed integer sign-extends on its way to a pointer,
                            // as it would on its way to a wider integer. Copying the
                            // bytes would leave the high half zero.
                            auto srcTi = TypeInfo::forType(srcTy);
                            dst.writeUint(state, dst.getLen() * 8, inval.readSint(state, srcTi.bits).getInner());
                        } else {
                            dst.copyFrom(state, inval.slice(0, std::min(inval.getLen(), dst.getLen())));

                            const auto* se = srcTy->opt_Pointer();
                            if (se && dst.getLen() > inval.getLen()
                                && localState.resolve.metadataType(state.sp, se->inner) == MetadataType::None) {
                                writeCtfeUnsizeMetadata(localState, dst, de.inner, se->inner);
                            }
                        }
                        break;
                    }
                    case HIRTypeData::TAG_Function: {
                        if (const auto* e = srcTy->opt_NamedFunction()) {
                            dst.writePtr(state, EncodedLiteral::PTR_BASE, localState.getStaticrefMono(e->path));
                        } else {
                            dst.copyFrom(state, inval.slice(0, std::min(inval.getLen(), dst.getLen())));
                        }
                        break;
                    }
                }
                break;
            }
            case MIRRValue::TAG_BinOp: {
                auto& e = sa.src.as_BinOp();
                HIRTypeRef tmp;
                const auto& tyL = state.getParamType(tmp, e.valL);
                bool didOverflow = doArithChecked(localState, tyL, dst, e.valL, e.op, e.valR);
                switch (e.op) {
                    case MIRBinOp::DIV:
                    case MIRBinOp::MOD:
                        if (didOverflow) {
                            MIR_BUG(state, "Division/modulo by zero!");
                        }
                        break;
                    case MIRBinOp::BIT_SHL:
                    case MIRBinOp::BIT_SHR:
                        if (didOverflow) {
                            MIR_BUG(state, "Bit shift out of range");
                        }
                        break;
                    default:
                        break;
                }
                break;
            }
            case MIRRValue::TAG_UniOp: {
                auto& e = sa.src.as_UniOp();
                HIRTypeRef tmp;
                const auto& tyL = state.getLvalueType(tmp, e.val);
                auto ti = TypeInfo::forType(tyL);

                switch (ti.ty) {
                    case TypeInfo::Unsigned:
                    case TypeInfo::Signed: {
                        auto i = localState.getLval(e.val).readUint(state, ti.bits);
                        switch (e.op) {
                            case MIRUniOp::INV:
                                i = ti.mask(~i);
                                break;
                            case MIRUniOp::NEG:
                                i = ~i + 1u;
                                break;
                        }
                        dst.writeUint(state, ti.bits, i);
                        break;
                    }
                    case TypeInfo::Float: {
                        switch (e.op) {
                            case MIRUniOp::INV:
                                MIR_BUG(state, "Invalid invert of Float");
                            case MIRUniOp::NEG: {
                                // IEEE negation toggles only the sign bit. Doing
                                // this through a host float would canonicalise NaN
                                // payloads and can discard the newly-set sign.
                                auto bits = localState.getLval(e.val).readUint(state, ti.bits);
                                bits = bits ^ (U128(1) << (ti.bits - 1));
                                dst.writeUint(state, ti.bits, bits);
                                break;
                            }
                        }
                        break;
                    }
                    case TypeInfo::Other:
                        MIR_BUG(state, "UniOp on " << tyL);
                }
                break;
            }
            case MIRRValue::TAG_DstMeta: {
                auto& e = sa.src.as_DstMeta();
                auto v = localState.getLval(e.val);
                size_t ptrSize = TargetGetPointerBits() / 8;
                dst.copyFrom(state, v.slice(ptrSize));
                break;
            }
            case MIRRValue::TAG_DstPtr: {
                auto& e = sa.src.as_DstPtr();
                auto v = localState.getLval(e.val);
                size_t ptrSize = TargetGetPointerBits() / 8;
                dst.copyFrom(state, v.slice(0, ptrSize));
                break;
            }
            case MIRRValue::TAG_MakeDst: {
                auto& e = sa.src.as_MakeDst();
                if ((e.metaVal.is_Constant() && e.metaVal.as_Constant().is_ItemAddr() && e.metaVal.as_Constant().as_ItemAddr().get() == nullptr)) {
                    HIRTypeRef tmp;
                    const auto& srcTy = state.getParamType(tmp, e.ptrVal);
                    HIRTypeRef tmp2;
                    const auto& dstTy = state.getLvalueType(tmp2, sa.dst);

                    switch ((*dstTy).tag()) {
default:
                        // NOTE: Can be an unsizing!
                        MIR_TODO(state, "RValue::MakeDst Coerce to " << dstTy);
                        break;
                        case HIRTypeData::TAG_Path: {
                            auto& te = (*dstTy).as_Path();
                            bool done = false;
                            // CoerceUnsized cast
                            if (te.binding.is_Struct()) {
                                const HIRStruct& str = *te.binding.as_Struct();
                                if (srcTy->is_Path() && srcTy->as_Path().binding.is_Struct() && srcTy->as_Path().binding.as_Struct() == &str) {
                                    const auto& markings = str.structMarkings;
                                    if (markings.coerceUnsized != HIRStructMarkings::Coerce::None) {
                                        done = true;
                                    }
                                    // The coerced pointer starts the value, so its
                                    // metadata follows it, and the parameter the
                                    // coercion runs through names the pointee.
                                    if (markings.coerceUnsized == HIRStructMarkings::Coerce::Pointer && markings.coerceParam != ~0u) {
                                        const auto& paramsD = te.path.data.as_Generic().params;
                                        const auto& paramsS = srcTy->as_Path().path.data.as_Generic().params;
                                        writeCtfeUnsizeMetadata(localState, dst, paramsD.types.at(markings.coerceParam), paramsS.types.at(markings.coerceParam));
                                    }
                                }
                            }
                            if (!done) {
                                MIR_TODO(state, "RValue::MakeDst Coerce to " << dstTy);
                            }
                            break;
                        }
                        case HIRTypeData::TAG_Borrow: {
                            auto& te = (*dstTy).as_Borrow();
                            MIR_ASSERT(state, srcTy->is_Borrow(), "RValue::MakeDst from " << srcTy << " to " << dstTy);
                            const auto srcInner = srcTy->as_Borrow().inner;
                            const auto srcMetadata = localState.resolve.metadataType(state.sp, srcInner);
                            if (srcMetadata == MetadataType::Slice || srcMetadata == MetadataType::TraitObject) {
                                localState.writeParam(dst, e.ptrVal);
                            } else {
                                writeCtfeUnsizeMetadata(localState, dst, te.inner, srcInner);
                            }
                            break;
                        }
                        case HIRTypeData::TAG_Pointer: {
                            auto& te = (*dstTy).as_Pointer();
                            MIR_ASSERT(state, srcTy->is_Pointer(), "RValue::MakeDst from " << srcTy << " to " << dstTy);
                            const auto srcInner = srcTy->as_Pointer().inner;
                            const auto srcMetadata = localState.resolve.metadataType(state.sp, srcInner);
                            if (srcMetadata == MetadataType::Slice || srcMetadata == MetadataType::TraitObject) {
                                localState.writeParam(dst, e.ptrVal);
                            } else {
                                writeCtfeUnsizeMetadata(localState, dst, te.inner, srcInner);
                            }
                            break;
                        }
                    }

                    if( const auto* p = e.ptrVal.opt_Borrow() ) {
                        localState.writeBorrow(dst, p->type, p->val);
                    }
                    else if( const auto* c = e.ptrVal.opt_Constant() ) {
                        localState.writeConst(dst, *c);
                    }
                    else {
                        auto inval = localState.getLval(e.ptrVal.as_LValue());
                        dst.slice(0, TargetGetPointerBits() / 8).copyFrom(state, inval);
                    }
                } else {
                    size_t ptrSize = TargetGetPointerBits() / 8;
                    localState.writeParam(dst.slice(0, ptrSize), e.ptrVal);
                    localState.writeParam(dst.slice(ptrSize), e.metaVal);
                }
                break;
            }
            case MIRRValue::TAG_Tuple: {
                auto& e = sa.src.as_Tuple();
                HIRTypeRef tmp;
                const auto& ty = state.getLvalueType(tmp, sa.dst);
                auto* repr = TargetGetTypeRepr(state.sp, resolve, ty);
                if (!repr) {
                    BUG(state.sp, "Layout not computable during const evaluation - " << "repr of " << ty);
                }
                MIR_ASSERT(state, repr->fields.size() == e.vals.size(), "");
                for (size_t i = 0; i < e.vals.size(); i++) {
                    size_t sz = localState.sizeOfOrBug(repr->fields[i].ty);
                    localState.writeParam(dst.slice(repr->fields[i].offset, sz), e.vals[i]);
                }
                break;
            }
            case MIRRValue::TAG_Struct: {
                auto& e = sa.src.as_Struct();
                HIRTypeRef tmp;
                const auto& ty = state.getLvalueType(tmp, sa.dst);
                auto* repr = TargetGetTypeRepr(state.sp, resolve, ty);
                if (!repr) {
                    BUG(state.sp, "Layout not computable during const evaluation - " << "repr of " << ty);
                }
                MIR_ASSERT(state, repr->fields.size() == e.vals.size(), "");
                for (size_t i = 0; i < e.vals.size(); i++) {
                    size_t sz = localState.sizeOfOrBug(repr->fields[i].ty);
                    auto localDst = dst.slice(repr->fields[i].offset, sz);
                    localState.writeParam(localDst, e.vals[i]);
                    DEBUG("@" << repr->fields[i].offset << " = " << localDst);
                }
                break;
            }
            case MIRRValue::TAG_SizedArray: {
                auto& e = sa.src.as_SizedArray();
                size_t count = 0;
                switch (e.count.tag()) {
                    case HIRArraySize::TAG_Known: {
                        auto& v = e.count.as_Known();
                        count = v;
                        break;
                    }
                    case HIRArraySize::TAG_Unevaluated: {
                        auto& v = e.count.as_Unevaluated();
                        const auto* vp = &v;
                        HIRConstGeneric tmpV;
                        if (const auto* ve = v.opt_Generic()) {
                            vp = &(tmpV = localState.ms.getValue(state.sp, *ve));
                        }
                        EncodedLiteral tmpVal;
                        count = localState.getConst(*vp, tmpVal).readUsize(0);
                        break;
                    }
                }

                if( count > 0 )
                {
                    HIRTypeRef tmp;
                    const auto& ty = state.getLvalueType(tmp, sa.dst);
                    const auto& ity = ty->as_Array().inner;
                    size_t sz = localState.sizeOfOrBug(ity);

                    localState.writeParam(dst.slice(0, sz), e.val);
                    if (sz > 0) {
                        for (size_t i = 1; i < count; i++) {
                            dst.slice(sz * i, sz).copyFrom(state, dst.slice(0, sz));
                        }
                    }
                }
                break;
            }
            case MIRRValue::TAG_Array: {
                auto& e = sa.src.as_Array();
                HIRTypeRef tmp;
                const auto& ty = state.getLvalueType(tmp, sa.dst);
                const auto& ity = ty->as_Array().inner;
                size_t sz = localState.sizeOfOrBug(ity);

                size_t ofs = 0;
                for (const auto& v : e.vals) {
                    localState.writeParam(dst.slice(ofs, sz), v);
                    ofs += sz;
                }
                break;
            }
            case MIRRValue::TAG_UnionVariant: {
                auto& e = sa.src.as_UnionVariant();
                // TODO: Write some hidden information to contain the variant?
                localState.writeParam(dst, e.val);
                break;
            }
            case MIRRValue::TAG_EnumVariant: {
                auto& e = sa.src.as_EnumVariant();
                HIRTypeRef tmp;
                const auto& ty = state.getLvalueType(tmp, sa.dst);
                writeCtfeEnumVariant(resolve, localState, dst, ty, e.index, e.vals.size(),
                    [&](MIREvalValueRef fieldDst, size_t i) {
                        localState.writeParam(fieldDst, e.vals[i]);
                    });
                break;
            }
        }

        DEBUG("> E" << this->evalIndex << " F" << localState.frameIndex << " " << sa.dst << " := " << dst);
}

unsigned HIREvaluator::runTerminator(MIREvalCallStackEntry& localState, const MIRTerminator& terminator) {
    const auto& state = localState.state;
    DEBUG("E" << this->evalIndex << " F" << localState.frameIndex << " " << state << terminator);

        switch (terminator.tag()) {
default:
            MIR_BUG(state, "Unexpected terminator - " << terminator);
            break;
            case MIRTerminator::TAG_Goto: {
                auto& e = terminator.as_Goto();
                return e;
            }
            case MIRTerminator::TAG_Return: {
                return TERM_RET_RETURN;
            }
            case MIRTerminator::TAG_If: {
                auto& e = terminator.as_If();
                bool res = U128(0) != localState.getLval(e.cond).readUint(state, 1);
                DEBUG(state << " IF " << res);
                return res ? e.bbTrue : e.bbFalse;
            }
            case MIRTerminator::TAG_Switch: {
                auto& e = terminator.as_Switch();
                if (e.validFlag != ~0u && !localState.dropFlags.at(e.validFlag)) {
                    return e.invalidTarget;
                }
                HIRTypeRef tmp;
                const auto& ty = state.getLvalueType(tmp, e.val);
                auto lit = localState.getLval(e.val);
                auto varIdx = localState.readEnumVariant(ty, lit);
                DEBUG(state << " = " << varIdx);
                MIR_ASSERT(state, varIdx < e.targets.size(), "Switch " << varIdx << " out of range in target list (" << e.targets.size() << ")");
                return e.targets[varIdx];
            }
            case MIRTerminator::TAG_SwitchValue: {
                auto& e = terminator.as_SwitchValue();
                HIRTypeRef tmp;
                const auto& ty = state.getLvalueType(tmp, e.val);
                auto ti = TypeInfo::forType(ty);
                auto lit = localState.getLval(e.val);

                unsigned targetIdx = ~0u;
                switch (e.values.tag()) {
default:
                    MIR_TODO(state, "SwitchValue - " << e.values.tagStr());
                    break;
                    case MIRSwitchValues::TAG_Unsigned: {
                        auto& vals = e.values.as_Unsigned();
                        auto v = lit.readUint(state, ti.bits);
                        for (size_t i = 0; i < vals.size(); i++) {
                            if (v == U128(vals[i])) {
                                targetIdx = i;
                                break;
                            }
                        }
                        break;
                    }
                    case MIRSwitchValues::TAG_Signed: {
                        auto& vals = e.values.as_Signed();
                        auto v = lit.readSint(state, ti.bits);
                        for (size_t i = 0; i < vals.size(); i++) {
                            if (v == S128(vals[i])) {
                                targetIdx = i;
                                break;
                            }
                        }
                        break;
                    }
                }
                if( targetIdx == ~0u ) {
                    return e.defTarget;
                }
                else {
                    return e.targets[targetIdx];
                }
                break;
            }
            case MIRTerminator::TAG_Drop: {
                auto& e = terminator.as_Drop();
                if (e.flagIdx != UINT_MAX && !localState.dropFlags.at(e.flagIdx)) {
                    return e.target;
                }

                HIRTypeRef tmp;
                const auto& ty = state.getLvalueType(tmp, e.slot);
                auto value = localState.getLval(e.slot);
                if (!localState.valueReachableFromReturn(value)) {
                    if (localState.valueNeedsNonConstDrop(ty, value)) {
                        ERROR(this->rootSpan, E0000, "destructor of `" << ty << "` cannot be evaluated at compile-time");
                    }
                    // A `impl const Drop` destructor is observable -- `RefCell`'s
                    // borrow guards are the reason `borrow` works in a `const` block
                    // -- so it has to actually run. A shallow drop only frees a box,
                    // which constant evaluation does not model.
                    if (e.kind == MIRDropKind::DEEP) {
                        this->runConstDrop(localState, ty, e.slot);
                    }
                }
                return e.target;
            }
            case MIRTerminator::TAG_Call: {
                auto& e = terminator.as_Call();
                const auto& ms = localState.ms;
                auto callPath = [&](HIRPath* fcnp, bool indirect) -> unsigned {
                    ::std::vector<MIREvalAllocationPtr> callArgs;
                    callArgs.reserve(e.args.size());
                    for (const auto& a : e.args) {
                        HIRTypeRef tmp;
                        const auto& ty = state.getParamType(tmp, a);
                        callArgs.push_back(MIREvalAllocationPtr::allocate(localState.valuePool, resolve, state, ty));
                        localState.writeParam(MIREvalValueRef(callArgs.back()), a);
                    }

                    if (this->callFunction(localState, e.retVal, fcnp, std::move(callArgs), e.source, indirect)) {
                        return TERM_RET_PUSHED;
                    }
                    DEBUG("> E" << this->evalIndex << " F" << localState.frameIndex << " " << e.retVal << " := " << localState.getLval(e.retVal));
                    return e.retBlock;
                };

                if (const auto* te = e.fcn.opt_Intrinsic()) {
                    auto dst = localState.getLval(e.retVal);
                    auto readTraitObjectVtableUsize = [&](size_t field) -> u64 {
                        MIR_ASSERT(state, field == 1 || field == 2, "Invalid vtable header field " << field);
                        const size_t ptrSize = TargetGetPointerBits() / 8;
                        auto arg = localState.getLval(e.args.at(0).as_LValue());
                        auto vtablePtr = arg.slice(ptrSize).readPtr(state);
                        MIR_ASSERT(state, vtablePtr.first >= EncodedLiteral::PTR_BASE, "Invalid trait object vtable pointer");
                        MIR_ASSERT(state, vtablePtr.second, "Trait object has no vtable relocation");
                        if (auto* staticRef = vtablePtr.second.asStaticref()) {
                            if (const auto* path = staticRef->path().data.opt_UfcsKnown(); path && path->item == "vtable#") {
                                // Vtables are generated after CTFE.  Their symbolic
                                // path still identifies the concrete source type,
                                // which determines both header values.
                                size_t size;
                                size_t align;
                                MIR_ASSERT(state, TargetGetSizeAndAlignOf(state.sp, this->resolve, path->type, size, align), "Invalid vtable source type " << path->type);
                                MIR_ASSERT(state, size != SIZE_MAX, "Unsized vtable source type " << path->type);
                                return field == 1 ? size : align;
                            }
                            if (!staticRef->hasValue()) {
                                vtablePtr.second = MIREvalRelocPtr(localState.getStaticref(staticRef->path().clone()));
                            }
                        }
                        auto vtable = MIREvalValueRef(vtablePtr.second, vtablePtr.first - EncodedLiteral::PTR_BASE);
                        return vtable.slice(field * ptrSize, ptrSize).readUsize(state);
                    };
                    if (te->name == "size_of") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        size_t sizeVal;
                        if (TargetGetSizeOf(state.sp, this->resolve, ty, sizeVal)) {
                            dst.writeUint(state, TargetGetPointerBits(), U128(sizeVal));
                        } else {
                            BUG(state.sp, "Layout not computable during const evaluation - " << "sizeof " << ty);
                        }
                    } else if (te->name == "size_of_val") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        size_t sizeVal;
                        size_t alignVal;
                        if (ty->is_TraitObject()) {
                            sizeVal = readTraitObjectVtableUsize(1);
                        } else if (!TargetGetSizeAndAlignOf(state.sp, this->resolve, ty, sizeVal, alignVal)) {
                            BUG(state.sp, "Layout not computable during const evaluation - " << "size/align of " << ty);
                        }
                        if (sizeVal == SIZE_MAX) {
                            size_t itemSize;
                            if (const auto* slice = ty->opt_Slice()) {
                                if (!TargetGetSizeOf(state.sp, this->resolve, slice->inner, itemSize)) {
                                    BUG(state.sp, "Layout not computable during const evaluation - " << "sizeof " << slice->inner);
                                }
                            } else if (ty == HIRCoreType::Str) {
                                itemSize = 1;
                            } else {
                                BUG(state.sp, "Layout not computable during const evaluation - " << "unsized tail of " << ty);
                            }
                            auto arg = localState.getLval(e.args.at(0).as_LValue());
                            const auto len = arg.slice(TargetGetPointerBits() / 8).readUsize(state);
                            MIR_ASSERT(state, itemSize == 0 || len <= SIZE_MAX / itemSize, "`size_of_val` overflow for " << ty);
                            sizeVal = len * itemSize;
                        }
                        dst.writeUint(state, TargetGetPointerBits(), U128(sizeVal));
                    } else if (te->name == "align_of" || te->name == "min_align_of") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        size_t alignVal;
                        if (TargetGetAlignOf(state.sp, this->resolve, ty, alignVal)) {
                            dst.writeUint(state, TargetGetPointerBits(), U128(alignVal));
                        } else {
                            BUG(state.sp, "Layout not computable during const evaluation - " << "alignof " << ty);
                        }
                    } else if (te->name == "align_of_val" || te->name == "min_align_of_val") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        size_t sizeVal;
                        size_t alignVal;
                        if (ty->is_TraitObject()) {
                            alignVal = readTraitObjectVtableUsize(2);
                            dst.writeUint(state, TargetGetPointerBits(), U128(alignVal));
                        } else if (TargetGetSizeAndAlignOf(state.sp, this->resolve, ty, sizeVal, alignVal) && alignVal > 0) {
                            dst.writeUint(state, TargetGetPointerBits(), U128(alignVal));
                        } else {
                            BUG(state.sp, "Layout not computable during const evaluation - " << "alignof " << ty);
                        }
                    } else if (te->name == "offset_of") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        size_t val = state.intrinsicOffsetOf(ty, e.args);
                        dst.writeUint(state, TargetGetPointerBits(), U128(val));
                    } else if (te->name == "type_name") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        auto name = state.intrinsicTypeName(ty);
                        dst.writePtr(state, EncodedLiteral::PTR_BASE, MIREvalAllocationPtr::allocateRo(localState.valuePool, name.data(), name.size()));
                        dst.slice(TargetGetPointerBits() / 8).writeUint(state, TargetGetPointerBits(), name.size());
                    } else if (te->name == "type_id") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        dst.writePtr(state, EncodedLiteral::PTR_BASE, MIREvalStaticRefPtr::allocate(localState.valuePool, HIRPath(mv$(ty), "#type_id"), nullptr, 0));
                    } else if (te->name == "needs_drop") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        dst.writeUint(state, 8, resolve.typeNeedsDropGlue(state.sp, ty) ? 1 : 0);
                    } else if (te->name == "caller_location") {
                        auto tyPath = resolve.hirCrate().getLangItemPath(state.sp, "panic_location");
                        auto ty = resolve.hirCrate().types.path(tyPath, &resolve.hirCrate().getStructByPath(state.sp, tyPath));
                        auto* repr = TargetGetTypeRepr(state.sp, resolve, ty);
                        MIR_ASSERT(state, repr, "No repr for panic::Location?");
                        MIR_ASSERT(state, repr->fields.size() == 4, "Unexpected item count in panic::Location");
                        auto val = MIREvalRelocPtr(MIREvalAllocationPtr::allocate(localState.valuePool, resolve, state, ty));
                        dst.writePtr(state, EncodedLiteral::PTR_BASE, val);
                        auto rv = MIREvalValueRef(val);
                        auto pb = TargetGetPointerBits() / 8;
                        MIR_ASSERT(state, localState.tracksCaller, "`caller_location` used outside a #[track_caller] function");
                        const auto& caller = localState.callerLocation;
                        const auto* filename = caller.filename.c_str();
                        const auto filenameLen = caller.filename.size();
                        rv.slice(repr->fields[0].offset + 0, pb).writePtr(state, EncodedLiteral::PTR_BASE, MIREvalConstantPtr::allocate(localState.valuePool, filename, filenameLen + 1)); // file.ptr, including trailing NUL
                        rv.slice(repr->fields[0].offset + pb, pb).writeUint(state, TargetGetPointerBits(), filenameLen);                                                                   // file.len
                        rv.slice(repr->fields[1].offset, 4).writeUint(state, 32, caller.line);                                                                                             // line: u32
                        rv.slice(repr->fields[2].offset, 4).writeUint(state, 32, caller.column);                                                                                           // col: u32
                    }
                    // ---
                    else if (te->name == "ctpop") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        MIR_ASSERT(state, ty->is_Primitive(), "ctpop with non-primitive " << ty);
                        auto ti = TypeInfo::forType(ty);
                        auto val = ti.mask(localState.readParamUint(ti.bits, e.args.at(0)));
                        unsigned rv = __builtin_popcountll(val.getLo()) + __builtin_popcountll(val.getHi());
                        dst.writeUint(state, 32, U128(rv));
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
                        dst.writeUint(state, 32, U128(rv));
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
                        dst.writeUint(state, 32, U128(ti.bits - rv));
                    } else if (te->name == "bswap") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        MIR_ASSERT(state, ty->is_Primitive(), "bswap with non-primitive " << ty);
                        auto ti = TypeInfo::forType(ty);
                        auto val = localState.readParamUint(ti.bits, e.args.at(0));
                        struct H {
                            static u16 bswap16(u16 v) {
                                return (v >> 8) | (v << 8);
                            }
                            static u32 bswap32(u32 v) {
                                return bswap16(v >> 16) | (static_cast<u32>(bswap16(static_cast<u16>(v))) << 16);
                            }
                            static u64 bswap64(u64 v) {
                                return bswap32(v >> 32) | (static_cast<u64>(bswap32(static_cast<u32>(v))) << 32);
                            }
                            static U128 bswap128(U128 v) {
                                return U128(bswap64((v >> 64).truncateU64()), bswap64(v.truncateU64()));
                            }
                        };
                        U128 rv;
                        switch (ty->as_Primitive()) {
                            case HIRCoreType::I8:
                            case HIRCoreType::U8:
                                rv = val;
                                break;
                            case HIRCoreType::I16:
                            case HIRCoreType::U16:
                                rv = U128(H::bswap16(val.truncateU64()));
                                break;
                            case HIRCoreType::I32:
                            case HIRCoreType::U32:
                                rv = U128(H::bswap32(val.truncateU64()));
                                break;
                            case HIRCoreType::I64:
                            case HIRCoreType::U64:
                                rv = U128(H::bswap64(val.truncateU64()));
                                break;
                            case HIRCoreType::I128:
                            case HIRCoreType::U128:
                                rv = H::bswap128(val);
                                break;
                            default:
                                MIR_TODO(state, "Handle bswap with " << ty);
                        }
                        dst.writeUint(state, ti.bits, rv);
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
                        dst.writeUint(state, ti.bits, rv);
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
                        dst.writeUint(state, ti.bits, rv);
                    }
                    // ---
                    else if (te->name == "add_with_overflow") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        MIR_ASSERT(state, ty->is_Primitive(), "`" << te->name << "` with non-primitive " << ty);
                        auto dstTup = getTupleTBool(localState, dst, ty);
                        bool overflowed = doArithChecked(localState, ty, dstTup.first, e.args.at(0), MIRBinOp::ADD, e.args.at(1));
                        dstTup.second.writeUint(state, 8, U128(overflowed ? 1 : 0));
                    } else if (te->name == "sub_with_overflow") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        MIR_ASSERT(state, ty->is_Primitive(), "`" << te->name << "` with non-primitive " << ty);
                        auto dstTup = getTupleTBool(localState, dst, ty);
                        bool overflowed = doArithChecked(localState, ty, dstTup.first, e.args.at(0), MIRBinOp::SUB, e.args.at(1));
                        dstTup.second.writeUint(state, 8, U128(overflowed ? 1 : 0));
                    } else if (te->name == "mul_with_overflow") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        MIR_ASSERT(state, ty->is_Primitive(), "`" << te->name << "` with non-primitive " << ty);
                        auto dstTup = getTupleTBool(localState, dst, ty);
                        bool overflowed = doArithChecked(localState, ty, dstTup.first, e.args.at(0), MIRBinOp::MUL, e.args.at(1));
                        dstTup.second.writeUint(state, 8, U128(overflowed ? 1 : 0));
                    }
                    // Unchecked and wrapping are the same
                    else if (te->name == "wrapping_add" || te->name == "unchecked_add") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        MIR_ASSERT(state, ty->is_Primitive(), "`" << te->name << "` with non-primitive " << ty);
                        doArithChecked(localState, ty, dst, e.args.at(0), MIRBinOp::ADD, e.args.at(1));
                    } else if (te->name == "wrapping_sub" || te->name == "unchecked_sub") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        MIR_ASSERT(state, ty->is_Primitive(), "`" << te->name << "` with non-primitive " << ty);
                        doArithChecked(localState, ty, dst, e.args.at(0), MIRBinOp::SUB, e.args.at(1));
                    } else if (te->name == "wrapping_mul" || te->name == "unchecked_mul") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        MIR_ASSERT(state, ty->is_Primitive(), "`" << te->name << "` with non-primitive " << ty);
                        doArithChecked(localState, ty, dst, e.args.at(0), MIRBinOp::MUL, e.args.at(1));
                    } else if (te->name == "unchecked_shl") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        MIR_ASSERT(state, ty->is_Primitive(), "`" << te->name << "` with non-primitive " << ty);
                        doArithChecked(localState, ty, dst, e.args.at(0), MIRBinOp::BIT_SHL, e.args.at(1));
                    } else if (te->name == "unchecked_shr") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        MIR_ASSERT(state, ty->is_Primitive(), "`" << te->name << "` with non-primitive " << ty);
                        doArithChecked(localState, ty, dst, e.args.at(0), MIRBinOp::BIT_SHR, e.args.at(1));
                    }
                    // - Except for div/rem, which add checking just in case
                    else if (te->name == "unchecked_rem") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        MIR_ASSERT(state, ty->is_Primitive(), "`" << te->name << "` with non-primitive " << ty);
                        bool wasOverflow = doArithChecked(localState, ty, dst, e.args.at(0), MIRBinOp::MOD, e.args.at(1));
                        MIR_ASSERT(state, !wasOverflow, "`" << te->name << "` overflowed");
                    } else if (te->name == "unchecked_div") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        MIR_ASSERT(state, ty->is_Primitive(), "`" << te->name << "` with non-primitive " << ty);
                        bool wasOverflow = doArithChecked(localState, ty, dst, e.args.at(0), MIRBinOp::DIV, e.args.at(1));
                        MIR_ASSERT(state, !wasOverflow, "`" << te->name << "` overflowed");
                    }
                    // `exact_div` is UB if the division results in a non-zero remainder (or if the division overflows)
                    else if (te->name == "exact_div") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        MIR_ASSERT(state, ty->is_Primitive(), "`" << te->name << "` with non-primitive " << ty);
                        bool wasOverflow = doArithChecked(localState, ty, dst, e.args.at(0), MIRBinOp::DIV, e.args.at(1));
                        MIR_ASSERT(state, !wasOverflow, "`" << te->name << "` overflowed");
                    }
                    // Saturating operations
                    else if (te->name == "saturating_add") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        MIR_ASSERT(state, ty->is_Primitive(), "`" << te->name << "` with non-primitive " << ty);
                        doArithChecked(localState, ty, dst, e.args.at(0), MIRBinOp::ADD, e.args.at(1), true);
                    } else if (te->name == "saturating_sub") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        MIR_ASSERT(state, ty->is_Primitive(), "`" << te->name << "` with non-primitive " << ty);
                        doArithChecked(localState, ty, dst, e.args.at(0), MIRBinOp::SUB, e.args.at(1), true);
                    }
                    // ---
                    else if (te->name == "transmute" || te->name == "transmute_unchecked") {
                        localState.writeParam(dst, e.args.at(0));
                    } else if (te->name == "init") {
                        MIR_ASSERT(state, e.args.empty(), "`init` takes no arguments");
                        // This is trustme's internal zero-initialisation
                        // intrinsic, also emitted for a coroutine's state slot.
                        memset(dst.extWriteBytes(state, dst.getLen()), 0, dst.getLen());
                    } else if (te->name == "unlikely") {
                        localState.writeParam(dst, e.args.at(0));
                    } else if (te->name == "fabsf16" || te->name == "fabsf32" || te->name == "fabsf64" || te->name == "fabsf128") {
                        HIRTypeRef tmp;
                        auto ti = TypeInfo::forType(state.getParamType(tmp, e.args.at(0)));
                        MIR_ASSERT(state, ti.ty == TypeInfo::Float, "`" << te->name << "` with non-float argument");
                        auto bits = localState.readParamFloatBits(ti.bits, e.args.at(0));
                        bits &= ~(U128(1) << (ti.bits - 1));
                        dst.writeUint(state, ti.bits, bits);
                    } else if (te->name == "copysignf16" || te->name == "copysignf32" || te->name == "copysignf64" || te->name == "copysignf128") {
                        HIRTypeRef tmp;
                        auto ti = TypeInfo::forType(state.getParamType(tmp, e.args.at(0)));
                        MIR_ASSERT(state, ti.ty == TypeInfo::Float, "`" << te->name << "` with non-float argument");
                        auto value = localState.readParamFloatBits(ti.bits, e.args.at(0));
                        auto sign = localState.readParamFloatBits(ti.bits, e.args.at(1));
                        auto signMask = U128(1) << (ti.bits - 1);
                        dst.writeUint(state, ti.bits, (value & ~signMask) | (sign & signMask));
                    } else if (te->name == "floorf16" || te->name == "floorf32" || te->name == "floorf64" || te->name == "floorf128") {
                        HIRTypeRef tmp;
                        auto ti = TypeInfo::forType(state.getParamType(tmp, e.args.at(0)));
                        MIR_ASSERT(state, ti.ty == TypeInfo::Float, "`" << te->name << "` with non-float argument");
                        dst.writeFloat(state, ti.bits, floatValueFloor(localState.readParamFloat(ti.bits, e.args.at(0))));
                    } else if (te->name == "ceilf16" || te->name == "ceilf32" || te->name == "ceilf64" || te->name == "ceilf128") {
                        HIRTypeRef tmp;
                        auto ti = TypeInfo::forType(state.getParamType(tmp, e.args.at(0)));
                        MIR_ASSERT(state, ti.ty == TypeInfo::Float, "`" << te->name << "` with non-float argument");
                        dst.writeFloat(state, ti.bits, floatValueCeil(localState.readParamFloat(ti.bits, e.args.at(0))));
                    } else if (te->name == "roundf16" || te->name == "roundf32" || te->name == "roundf64" || te->name == "roundf128") {
                        HIRTypeRef tmp;
                        auto ti = TypeInfo::forType(state.getParamType(tmp, e.args.at(0)));
                        MIR_ASSERT(state, ti.ty == TypeInfo::Float, "`" << te->name << "` with non-float argument");
                        dst.writeFloat(state, ti.bits, floatValueRound(localState.readParamFloat(ti.bits, e.args.at(0))));
                    } else if (te->name == "round_ties_even_f16" || te->name == "round_ties_even_f32" || te->name == "round_ties_even_f64" || te->name == "round_ties_even_f128") {
                        HIRTypeRef tmp;
                        auto ti = TypeInfo::forType(state.getParamType(tmp, e.args.at(0)));
                        MIR_ASSERT(state, ti.ty == TypeInfo::Float, "`" << te->name << "` with non-float argument");
                        dst.writeFloat(state, ti.bits, floatValueRoundEven(localState.readParamFloat(ti.bits, e.args.at(0))));
                    } else if (te->name == "truncf16" || te->name == "truncf32" || te->name == "truncf64" || te->name == "truncf128") {
                        HIRTypeRef tmp;
                        auto ti = TypeInfo::forType(state.getParamType(tmp, e.args.at(0)));
                        MIR_ASSERT(state, ti.ty == TypeInfo::Float, "`" << te->name << "` with non-float argument");
                        dst.writeFloat(state, ti.bits, floatValueTrunc(localState.readParamFloat(ti.bits, e.args.at(0))));
                    } else if (te->name == "minnumf16" || te->name == "minnumf32" || te->name == "minnumf64" || te->name == "minnumf128" || te->name == "maxnumf16" || te->name == "maxnumf32" || te->name == "maxnumf64" || te->name == "maxnumf128") {
                        HIRTypeRef tmp;
                        auto ti = TypeInfo::forType(state.getParamType(tmp, e.args.at(0)));
                        MIR_ASSERT(state, ti.ty == TypeInfo::Float, "`" << te->name << "` with non-float argument");
                        auto lhs = localState.readParamFloat(ti.bits, e.args.at(0));
                        auto rhs = localState.readParamFloat(ti.bits, e.args.at(1));
                        bool isMin = te->name == "minnumf16" || te->name == "minnumf32" || te->name == "minnumf64" || te->name == "minnumf128";
                        auto value = isMin ? floatValueMinimumNumber(lhs, rhs) : floatValueMaximumNumber(lhs, rhs);
                        dst.writeFloat(state, ti.bits, value);
                    } else if (te->name == "three_way_compare") {
                        HIRTypeRef tmp;
                        auto ti = TypeInfo::forType(state.getParamType(tmp, e.args.at(0)));
                        i64 result;
                        if (ti.ty == TypeInfo::Signed) {
                            auto lhs = localState.readParamSint(ti.bits, e.args.at(0));
                            auto rhs = localState.readParamSint(ti.bits, e.args.at(1));
                            result = lhs == rhs ? 0 : lhs < rhs ? -1 : 1;
                        } else if (ti.ty == TypeInfo::Unsigned) {
                            auto lhs = localState.readParamUint(ti.bits, e.args.at(0));
                            auto rhs = localState.readParamUint(ti.bits, e.args.at(1));
                            result = lhs == rhs ? 0 : lhs < rhs ? -1 : 1;
                        } else {
                            MIR_BUG(state, "`three_way_compare` with unsupported type " << state.getParamType(tmp, e.args.at(0)));
                        }
                        dst.writeSint(state, dst.getLen() * 8, S128(result));
                    } else if (te->name == "assume") {
                        auto val = localState.readParamUint(8, e.args.at(0));
                        MIR_ASSERT(state, val != 0, "`assume` failed");
                    } else if (te->name == "assert_inhabited") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        // TODO: Determine if the type is inhabited (i.e. isn't diverge)
                        bool isUninhabited = resolve.typeIsImpossible(state.sp, ty);
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
                            BUG(state.sp, "Layout not computable during const evaluation - " << "repr of " << argTy);
                        }
                        auto argVal = localState.getLval(e.args.at(0).as_LValue());
                        const auto& fcnArg = e.args.at(1);
                        HIRPath* fcnPath = nullptr;

                        switch (fcnArg.tag()) {
                            case MIRParam::TAG_LValue: {
                                auto& e = fcnArg.as_LValue();
                                auto fcnVal = localState.getLval(e).readPtr(state);
                                MIR_ASSERT(state, fcnVal.first == EncodedLiteral::PTR_BASE, "");

                                const auto* fcnSr = fcnVal.second.asStaticref();
                                MIR_ASSERT(state, fcnSr, "");
                                fcnPath = localState.valuePool->make<HIRPath>(fcnSr->path().clone());
                                break;
                            }
                            case MIRParam::TAG_Borrow: {
                                MIR_BUG(state, "Invalid argument for function pointer to `const_eval_select`: " << fcnArg);
                                break;
                            }
                            case MIRParam::TAG_Constant: {
                                auto& e = fcnArg.as_Constant();
                                if (const auto* ce = e.opt_Function()) {
                                    fcnPath = localState.valuePool->make<HIRPath>(ms.monomorphPath(state.sp, *ce->p));
                                } else if (const auto* ce = e.opt_ItemAddr()) {
                                    MIR_ASSERT(state, ce->offset == U128(0), "Function pointer has a non-zero offset: " << ce->offset);
                                    fcnPath = localState.valuePool->make<HIRPath>(ms.monomorphPath(state.sp, **ce));
                                } else {
                                    MIR_BUG(state, "Invalid argument for function pointer to `const_eval_select`: " << fcnArg);
                                }
                                break;
                            }
                        }

                        // Argument values
                        ::std::vector<MIREvalAllocationPtr>  callArgs;
                        callArgs.reserve( repr->fields.size() );
                        for(const auto& f : repr->fields) {
                            auto size = localState.sizeOfOrBug(f.ty);
                            callArgs.push_back(MIREvalAllocationPtr::allocate(localState.valuePool, resolve, state, f.ty));
                            auto vr = MIREvalValueRef(callArgs.back());
                            vr.copyFrom(state, argVal.slice(f.offset, size));
                        }

                        if( this->callFunction(localState, e.retVal, fcnPath, std::move(callArgs), e.source, true) ) {
                            return TERM_RET_PUSHED;
                        }
                    }
                    // ---
                    else if (te->name == "copy_nonoverlapping" || te->name == "copy") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        size_t elementSize;
                        if (!TargetGetSizeOf(state.sp, resolve, ty, elementSize)) {
                            BUG(state.sp, "Layout not computable during const evaluation - " << "sizeof " << ty);
                        }
                        auto ptrSrc = localState.readParamPtr(e.args.at(0));
                        auto ptrDst = localState.readParamPtr(e.args.at(1));
                        U128 count = localState.readParamUint(TargetGetPointerBits(), e.args.at(2));
                        MIR_ASSERT(state, count.isU64(), "Excessive count in `" << te->name << "`");
                        MIR_ASSERT(state, count * elementSize < U128(SIZE_MAX), "Excessive size in `" << te->name << "`");
                        size_t nbytes = elementSize * count.truncateU64();
                        if (nbytes != 0) {
                            auto vrSrc = pointerBytes(localState, ptrSrc, nbytes, te->name.c_str());
                            auto vrDst = pointerBytes(localState, ptrDst, nbytes, te->name.c_str());
                            if (te->name == "copy") {
                                vrDst.copyFromOverlapping(state, vrSrc, localState.valuePool);
                            } else {
                                vrDst.copyFrom(state, vrSrc);
                            }
                        }
                    } else if (te->name == "compare_bytes") {
                        auto leftPtr = localState.readParamPtr(e.args.at(0));
                        auto rightPtr = localState.readParamPtr(e.args.at(1));
                        auto count = localState.readParamUint(TargetGetPointerBits(), e.args.at(2));
                        MIR_ASSERT(state, count.isU64() && count <= U128(SIZE_MAX), "Excessive count in `compare_bytes`");
                        const size_t nbytes = count.truncateU64();
                        int result = 0;
                        if (nbytes != 0) {
                            auto left = pointerBytes(localState, leftPtr, nbytes, "compare_bytes");
                            auto right = pointerBytes(localState, rightPtr, nbytes, "compare_bytes");
                            const int raw = memcmp(left.extReadBytes(state, nbytes), right.extReadBytes(state, nbytes), nbytes);
                            result = (raw > 0) - (raw < 0);
                        }
                        dst.writeSint(state, 32, S128(result));
                    } else if (te->name == "raw_eq") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        size_t size;
                        size_t alignment;
                        if (!TargetGetSizeAndAlignOf(state.sp, resolve, ty, size, alignment)) {
                            BUG(state.sp, "Layout not computable during const evaluation - " << "size/align of " << ty);
                        }
                        auto leftPtr = localState.readParamPtr(e.args.at(0));
                        auto rightPtr = localState.readParamPtr(e.args.at(1));
                        auto isAligned = [&](const ::std::pair<u64, MIREvalRelocPtr>& pointer) {
                            if (pointer.second && pointer.first < EncodedLiteral::PTR_BASE) {
                                return false;
                            }
                            const u64 address = pointer.second
                                ? pointer.first - EncodedLiteral::PTR_BASE
                                : pointer.first;
                            return alignment == 0 || address % alignment == 0;
                        };
                        MIR_ASSERT(state, isAligned(leftPtr) && isAligned(rightPtr), "Unaligned pointer passed to `raw_eq`");
                        bool equal = true;
                        if (size != 0) {
                            auto left = pointerBytes(localState, leftPtr, size, "raw_eq");
                            auto right = pointerBytes(localState, rightPtr, size, "raw_eq");
                            equal = memcmp(left.extReadBytes(state, size), right.extReadBytes(state, size), size) == 0;
                        }
                        dst.writeUint(state, 8, U128(equal ? 1 : 0));
                    } else if (te->name == "black_box") {
                        localState.writeParam(dst, e.args.at(0));
                    } else if (te->name == "forget") {
                        // `forget` consumes its argument and returns unit.  The MIR
                        // call itself therefore has no value or memory effect.
                    } else if (te->name == "simd_extract" || te->name == "simd_extract_dyn") {
                        auto vectorTy = localState.monomorphExpand(te->params.types.at(0));
                        auto elementTy = localState.monomorphExpand(te->params.types.at(1));
                        size_t vectorSize;
                        size_t elementSize;
                        if (!TargetGetSizeOf(state.sp, resolve, vectorTy, vectorSize)
                            || !TargetGetSizeOf(state.sp, resolve, elementTy, elementSize)) {
                            BUG(state.sp, "Layout not computable during const evaluation - " << "sizeof " << vectorTy << " / " << elementTy);
                        }
                        MIR_ASSERT(state, elementSize != 0 && vectorSize % elementSize == 0, "Invalid SIMD layout for `" << te->name << "`");
                        auto index = localState.readParamUint(32, e.args.at(1));
                        MIR_ASSERT(state, index.isU64() && index < U128(vectorSize / elementSize), "SIMD index out of bounds in `" << te->name << "`");
                        auto inputStorage = MIREvalAllocationPtr::allocateScratch(localState.valuePool, vectorSize);
                        MIREvalValueRef input(inputStorage);
                        localState.writeParam(input, e.args.at(0));
                        dst.copyFrom(state, input.slice(index.truncateU64() * elementSize, elementSize));
                    } else if (te->name == "simd_insert" || te->name == "simd_insert_dyn") {
                        auto vectorTy = localState.monomorphExpand(te->params.types.at(0));
                        auto elementTy = localState.monomorphExpand(te->params.types.at(1));
                        size_t vectorSize;
                        size_t elementSize;
                        if (!TargetGetSizeOf(state.sp, resolve, vectorTy, vectorSize)
                            || !TargetGetSizeOf(state.sp, resolve, elementTy, elementSize)) {
                            BUG(state.sp, "Layout not computable during const evaluation - " << "sizeof " << vectorTy << " / " << elementTy);
                        }
                        MIR_ASSERT(state, elementSize != 0 && vectorSize % elementSize == 0, "Invalid SIMD layout for `" << te->name << "`");
                        auto index = localState.readParamUint(32, e.args.at(1));
                        MIR_ASSERT(state, index.isU64() && index < U128(vectorSize / elementSize), "SIMD index out of bounds in `" << te->name << "`");
                        localState.writeParam(dst, e.args.at(0));
                        localState.writeParam(dst.slice(index.truncateU64() * elementSize, elementSize), e.args.at(2));
                    } else if (te->name == "offset") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0)->as_Pointer().inner);
                        size_t elementSize;
                        if (!TargetGetSizeOf(state.sp, resolve, ty, elementSize)) {
                            BUG(state.sp, "Layout not computable during const evaluation - " << "sizeof " << ty);
                        }
                        auto ptrPair = localState.readParamPtr(e.args.at(0));
                        auto ofs = localState.readParamUint(TargetGetPointerBits(), e.args.at(1));
                        dst.writePtr(state, ptrPair.first + ofs.truncateU64() * elementSize, ptrPair.second);
                    }
                    // `arith_offset` is the wrapping form of `offset`; identical arithmetic here, and the type parameter is the *pointee*.
                    else if (te->name == "arith_offset") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        size_t elementSize;
                        if (!TargetGetSizeOf(state.sp, resolve, ty, elementSize)) {
                            BUG(state.sp, "Layout not computable during const evaluation - " << "sizeof " << ty);
                        }
                        auto ptrPair = localState.readParamPtr(e.args.at(0));
                        auto ofs = localState.readParamUint(TargetGetPointerBits(), e.args.at(1));
                        dst.writePtr(state, ptrPair.first + ofs.truncateU64() * elementSize, ptrPair.second);
                    }
                    else if (te->name == "ptr_offset_from" || te->name == "ptr_offset_from_unsigned") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        size_t elementSize;
                        if (!TargetGetSizeOf(state.sp, resolve, ty, elementSize)) {
                            BUG(state.sp, "Layout not computable during const evaluation - " << "sizeof " << ty);
                        }
                        MIR_ASSERT(state, elementSize != 0, "`" << te->name << "` called for a zero-sized type");
                        MIR_ASSERT(state, elementSize <= static_cast<size_t>(INT64_MAX), "Element size overflows isize in `" << te->name << "`");
                        auto pointer = localState.readParamPtr(e.args.at(0));
                        auto base = localState.readParamPtr(e.args.at(1));
                        resolveStaticPointer(localState, pointer);
                        resolveStaticPointer(localState, base);
                        MIR_ASSERT(state, samePointerProvenance(pointer.second, base.second), "`" << te->name << "` called on pointers into different allocations");

                        if (pointer.second) {
                            MIR_ASSERT(state, pointer.first >= EncodedLiteral::PTR_BASE && base.first >= EncodedLiteral::PTR_BASE,
                                "Invalid pointer passed to `" << te->name << "`");
                            const auto allocationSize = pointer.second.asValue().size();
                            MIR_ASSERT(state, pointer.first - EncodedLiteral::PTR_BASE <= allocationSize
                                && base.first - EncodedLiteral::PTR_BASE <= allocationSize,
                                "Out-of-bounds pointer passed to `" << te->name << "`");
                        }

                        i64 byteDistance;
                        if (pointer.first >= base.first) {
                            const u64 magnitude = pointer.first - base.first;
                            MIR_ASSERT(state, magnitude <= static_cast<u64>(INT64_MAX), "Pointer distance overflows isize in `" << te->name << "`");
                            byteDistance = static_cast<i64>(magnitude);
                        } else {
                            const u64 magnitude = base.first - pointer.first;
                            MIR_ASSERT(state, te->name != "ptr_offset_from_unsigned", "Negative pointer distance in `ptr_offset_from_unsigned`");
                            MIR_ASSERT(state, magnitude <= static_cast<u64>(INT64_MAX), "Pointer distance underflows isize in `" << te->name << "`");
                            byteDistance = -static_cast<i64>(magnitude);
                        }
                        MIR_ASSERT(state, pointer.second || byteDistance == 0,
                            "`" << te->name << "` called on distinct absolute pointers");
                        MIR_ASSERT(state, byteDistance % static_cast<i64>(elementSize) == 0,
                            "Pointer distance is not a multiple of the element size in `" << te->name << "`");
                        const i64 elementDistance = byteDistance / static_cast<i64>(elementSize);
                        if (te->name == "ptr_offset_from_unsigned") {
                            dst.writeUint(state, TargetGetPointerBits(), U128(static_cast<u64>(elementDistance)));
                        } else {
                            dst.writeSint(state, TargetGetPointerBits(), S128(elementDistance));
                        }
                    }
                    // Returns 1/0/2 (equal / not equal / unknown).
                    else if (te->name == "ptr_guaranteed_cmp") {
                        auto a = localState.readParamPtr(e.args.at(0));
                        auto b = localState.readParamPtr(e.args.at(1));
                        dst.writeUint(state, 8, pointerGuaranteedCmp(a, b));
                    } else if (te->name == "write_bytes") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        size_t elementSize;
                        if (!TargetGetSizeOf(state.sp, resolve, ty, elementSize)) {
                            BUG(state.sp, "Layout not computable during const evaluation - " << "sizeof " << ty);
                        }
                        auto ptrDst = localState.getLval(e.args.at(0).as_LValue()).readPtr(state);
                        auto val = localState.readParamUint(8, e.args.at(1));
                        U128 count = localState.readParamUint(TargetGetPointerBits(), e.args.at(2));
                        MIR_ASSERT(state, count.isU64(), "Excessive count in `" << te->name << "`");
                        MIR_ASSERT(state, count * elementSize < U128(SIZE_MAX), "Excessive size in `" << te->name << "`");
                        size_t nbytes = elementSize * count.truncateU64();
                        MIR_ASSERT(state, ptrDst.first >= EncodedLiteral::PTR_BASE, "");
                        MIREvalValueRef vrDst = MIREvalValueRef(ptrDst.second, ptrDst.first - EncodedLiteral::PTR_BASE).slice(0, nbytes);
                        memset(vrDst.extWriteBytes(state, nbytes), val.truncateU64(), nbytes);
                    }
                    // Innards of `core::ptr::read` (on 1.90+)
                    else if (te->name == "read_via_copy") {
                        auto ptrSrc = localState.readParamPtr(e.args.at(0));
                        auto vrSrc = MIREvalValueRef(ptrSrc.second, ptrSrc.first - EncodedLiteral::PTR_BASE);
                        dst.copyFrom(state, vrSrc);
                    } else if (te->name == "discriminant_value") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        if (!(ty->is_Path() && ty->as_Path().binding.is_Enum())) {
                            dst.writeUint(state, dst.getLen() * 8, U128(0));
                        } else {
                            const auto* repr = TargetGetTypeRepr(state.sp, resolve, ty);
                            if (!repr) {
                                BUG(state.sp, "Layout not computable during const evaluation - " << "repr of " << ty);
                            }

                            MIREvalValueRef value;
                            if (const auto* arg = e.args.at(0).opt_Borrow()) {
                                value = localState.getLval(arg->val);
                            } else {
                                auto ptr = localState.readParamPtr(e.args.at(0));
                                MIR_ASSERT(state, ptr.first >= EncodedLiteral::PTR_BASE, "Null pointer passed to `discriminant_value`");
                                value = MIREvalValueRef(ptr.second, ptr.first - EncodedLiteral::PTR_BASE);
                            }

                            switch (repr->variants.tag()) {
                                case TypeReprVariantMode::TAG_None: {
                                    // An enum with one variant stores nothing to
                                    // say which it is, but still has that
                                    // variant's discriminant.
                                    U128 only(0);
                                    if (ty->is_Path()) {
                                        if (const auto* enmpp = ty->as_Path().binding.opt_Enum()) {
                                            if ((*enmpp)->numVariants() == 1) {
                                                only = U128((*enmpp)->getDiscriminant(0));
                                            }
                                        }
                                    }
                                    dst.writeUint(state, dst.getLen() * 8, only);
                                    break;
                                }
                                case TypeReprVariantMode::TAG_Linear: {
                                    auto& ve = repr->variants.as_Linear();
                                    const auto ofs = repr->getOffset(state.sp, resolve, ve.field);
                                    auto tag = value.slice(ofs, ve.field.size).readUint(state, ve.field.size * 8);
                                    const auto variant = ve.decodeTag(tag);
                                    dst.writeUint(state, dst.getLen() * 8, U128(variant));
                                    break;
                                }
                                case TypeReprVariantMode::TAG_Values: {
                                    auto& ve = repr->variants.as_Values();
                                    const auto ofs = repr->getOffset(state.sp, resolve, ve.field);
                                    const auto& tagTy = TargetGetInnerType(state.sp, resolve, *repr, ve.field.index, ve.field.subFields);
                                    const auto tagInfo = TypeInfo::forType(tagTy);
                                    MIR_ASSERT(state, tagInfo.ty == TypeInfo::Signed || tagInfo.ty == TypeInfo::Unsigned, "Non-integer enum tag " << tagTy);
                                    auto tag = value.slice(ofs, ve.field.size);
                                    if (tagInfo.ty == TypeInfo::Signed) {
                                        dst.writeSint(state, dst.getLen() * 8, tag.readSint(state, tagInfo.bits));
                                    } else {
                                        dst.writeUint(state, dst.getLen() * 8, tag.readUint(state, tagInfo.bits));
                                    }
                                    break;
                                }
                                case TypeReprVariantMode::TAG_NonZero: {
                                    auto& ve = repr->variants.as_NonZero();
                                    const auto ofs = repr->getOffset(state.sp, resolve, ve.field);
                                    bool isNonzero = false;
                                    for (size_t i = 0; i < ve.field.size; i++) {
                                        isNonzero |= value.slice(ofs + i, 1).readUint(state, 8) != U128(0);
                                    }
                                    const auto variant = isNonzero ? 1 - ve.zeroVariant : ve.zeroVariant;
                                    dst.writeUint(state, dst.getLen() * 8, U128(variant));
                                    break;
                                }
                            }
                        }
                    } else if (te->name == "variant_count") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        // Asking for a type that is not an enum is a lint
                        // (`enum_intrinsics_non_enums`), not an error: a struct or a
                        // union is one variant, anything else is none.
                        size_t count = 0;
                        if (ty->is_Path()) {
                            const auto& binding = ty->as_Path().binding;
                            if (binding.is_Enum()) {
                                count = binding.as_Enum()->numVariants();
                            } else if (binding.is_Struct() || binding.is_Union()) {
                                count = 1;
                            }
                        }
                        dst.writeUint(state, TargetGetPointerBits(), count);
                    } else if (te->name == "assert_zero_valid") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        MIR_ASSERT(state, !ty->is_Borrow(), "`assert_zero_valid`: Borrow cannot be zero");
                        // TODO: Other cases?
                    } else if (te->name == "assert_mem_uninitialized_valid") {
                        auto ty = localState.monomorphExpand(te->params.types.at(0));
                        // TODO: Detect types which reject the mitigated 0x01 fill.
                    } else if (te->name == "is_val_statically_known") {
                        dst.writeUint(state, 8, e.args.at(0).is_Constant() || e.args.at(0).is_Borrow());
                    } else {
                        MIR_TODO(state, "Call intrinsic \"" << te->name << "\" - " << terminator);
                    }
                    DEBUG("> E" << this->evalIndex << " F" << localState.frameIndex << " " << e.retVal << " := " << dst);
                    return e.retBlock;
                } else if (const auto* te = e.fcn.opt_Path()) {
                    const auto& fcnpRaw = *te;
                    DEBUG("ms=" << ms);
                    auto* fcnp = localState.valuePool->make<HIRPath>(ms.monomorphPath(state.sp, fcnpRaw));

                    const auto* rustcIntrinsic = getRustcIntrinsicName(state.sp, resolve, *fcnp);
                    if (rustcIntrinsic && *rustcIntrinsic == "ptr_guaranteed_cmp") {
                        MIR_ASSERT(state, e.args.size() == 2, "invalid ptr_guaranteed_cmp signature");
                        auto left = localState.readParamPtr(e.args.at(0));
                        auto right = localState.readParamPtr(e.args.at(1));
                        auto dst = localState.getLval(e.retVal);
                        dst.writeUint(state, 8, pointerGuaranteedCmp(left, right));
                        DEBUG("> E" << this->evalIndex << " F" << localState.frameIndex << " " << e.retVal << " := " << dst);
                        return e.retBlock;
                    }

                    if (rustcIntrinsic && (*rustcIntrinsic == "const_allocate"
                        || *rustcIntrinsic == "const_deallocate"
                        || *rustcIntrinsic == "const_make_global")) {
                        const char* intrinsic = rustcIntrinsic->c_str();
                        auto dst = localState.getLval(e.retVal);
                        const unsigned pointerBits = TargetGetPointerBits();
                        auto readUsize = [&](size_t argument) {
                            auto value = localState.readParamUint(pointerBits, e.args.at(argument));
                            MIR_ASSERT(state, value.isU64(), "`" << intrinsic << "` argument does not fit usize");
                            return value.truncateU64();
                        };
                        auto readAlignment = [&](size_t argument) {
                            u64 alignment = readUsize(argument);
                            // rustc_abi::Align treats zero as one and inherits
                            // LLVM's maximum supported alignment of 2^29.
                            if (alignment == 0) {
                                alignment = 1;
                            }
                            if ((alignment & (alignment - 1)) != 0 || alignment > (1ull << 29)) {
                                ERROR(state.sp, E0000, "`" << intrinsic << "` requires a valid power-of-two alignment, got " << alignment);
                            }
                            return alignment;
                        };

                        if (strcmp(intrinsic, "const_allocate") == 0) {
                            MIR_ASSERT(state, e.args.size() == 2, "invalid const_allocate signature");
                            const u64 size = readUsize(0);
                            const u64 alignment = readAlignment(1);
                            MIR_ASSERT(state, size <= SIZE_MAX, "const_allocate size is too large");
                            MIR_ASSERT(state, alignment <= SIZE_MAX, "const_allocate alignment is too large");
                            auto allocation = MIREvalAllocationPtr::allocateHeap(localState.valuePool, size, alignment);
                            dst.writePtr(state, EncodedLiteral::PTR_BASE, MIREvalRelocPtr(allocation));
                        } else if (strcmp(intrinsic, "const_deallocate") == 0) {
                            MIR_ASSERT(state, e.args.size() == 3, "invalid const_deallocate signature");
                            auto pointer = localState.readParamPtr(e.args.at(0));
                            const u64 size = readUsize(1);
                            const u64 alignment = readAlignment(2);
                            if (auto* allocation = pointer.second.asAllocation(); allocation && allocation->isConstHeapAllocation()) {
                                MIR_ASSERT(state, pointer.first == EncodedLiteral::PTR_BASE, "const_deallocate pointer is not at the start of its allocation");
                                MIR_ASSERT(state, allocation->isAlive(), "const_deallocate of an already deallocated allocation");
                                MIR_ASSERT(state, !allocation->wasMadeGlobal(), "const_deallocate of an allocation made global in this evaluation");
                                MIR_ASSERT(state, allocation->size() == size, "const_deallocate size does not match const_allocate");
                                MIR_ASSERT(state, allocation->getHeapAlignment() == alignment, "const_deallocate alignment does not match const_allocate");
                                allocation->deallocate();
                            }
                        } else {
                            MIR_ASSERT(state, e.args.size() == 1, "invalid const_make_global signature");
                            auto pointer = localState.readParamPtr(e.args.at(0));
                            auto* allocation = pointer.second.asAllocation();
                            MIR_ASSERT(state, pointer.first == EncodedLiteral::PTR_BASE, "const_make_global pointer is not at the start of its allocation");
                            MIR_ASSERT(state, allocation && allocation->isConstHeapAllocation(), "const_make_global pointer was not returned by const_allocate");
                            MIR_ASSERT(state, allocation->isAlive(), "const_make_global of a deallocated allocation");
                            MIR_ASSERT(state, !allocation->wasMadeGlobal(), "const_make_global called twice for one allocation");
                            allocation->makeGlobal();
                            dst.writePtr(state, pointer.first, pointer.second);
                        }
                        DEBUG("> E" << this->evalIndex << " F" << localState.frameIndex << " " << e.retVal << " := " << dst);
                        return e.retBlock;
                    }

                    return callPath(fcnp, false);
                } else if (const auto* te = e.fcn.opt_Value()) {
                    HIRTypeRef tmp;
                    const auto& ty = state.getLvalueType(tmp, *te);
                    MIR_ASSERT(state, ty->is_Function(), "Indirect call through non-function-pointer type " << ty);

                    auto pointer = localState.getLval(*te).readPtr(state);
                    MIR_ASSERT(state, pointer.first == EncodedLiteral::PTR_BASE, "Function pointer has a nonzero offset");
                    const auto* function = pointer.second.asStaticref();
                    MIR_ASSERT(state, function, "Function pointer has no symbolic function relocation");
                    return callPath(localState.valuePool->make<HIRPath>(function->path().clone()), true);
                } else {
                    MIR_BUG(state, "Unexpected terminator - " << terminator);
                }
                break;
            }
            case MIRTerminator::TAG_TailCall: {
                auto& e = terminator.as_TailCall();
                auto callPath = [&](HIRPath* fcnp, bool indirect) -> unsigned {
                    ::std::vector<MIREvalAllocationPtr> callArgs;
                    callArgs.reserve(e.args.size());
                    for (const auto& arg : e.args) {
                        HIRTypeRef tmp;
                        const auto& ty = state.getParamType(tmp, arg);
                        callArgs.push_back(MIREvalAllocationPtr::allocate(localState.valuePool, resolve, state, ty));
                        localState.writeParam(MIREvalValueRef(callArgs.back()), arg);
                    }

                    const auto oldFrame = this->callStack.size() - 1;
                    if (!this->callFunction(localState, MIRLValue::newReturn(), fcnp, std::move(callArgs), e.source, indirect)) {
                        return TERM_RET_RETURN;
                    }
                    this->callStack.erase(this->callStack.begin() + oldFrame);
                    return TERM_RET_PUSHED;
                };

                if (const auto* path = e.fcn.opt_Path()) {
                    return callPath(localState.valuePool->make<HIRPath>(path->clone()), false);
                }
                if (const auto* value = e.fcn.opt_Value()) {
                    HIRTypeRef tmp;
                    const auto& ty = state.getLvalueType(tmp, *value);
                    MIR_ASSERT(state, ty->is_Function(), "Indirect tail call through non-function-pointer type " << ty);

                    auto pointer = localState.getLval(*value).readPtr(state);
                    MIR_ASSERT(state, pointer.first == EncodedLiteral::PTR_BASE, "Function pointer has a nonzero offset");
                    const auto* function = pointer.second.asStaticref();
                    MIR_ASSERT(state, function, "Function pointer has no symbolic function relocation");
                    return callPath(localState.valuePool->make<HIRPath>(function->path().clone()), true);
                }
                MIR_BUG(state, "Intrinsic used as an explicit tail-call target");
                break;
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
bool HIREvaluator::callFunction(MIREvalCallStackEntry& localState, const MIRLValue& rvSlot, HIRPath* fcnPath, ::std::vector<MIREvalAllocationPtr> callArgs, const SourceLocation& callsite, bool indirect) {
    const auto& state = localState.state;
    resolve.revealOpaqueTypesPath(state.sp, *fcnPath);
    MonomorphState fcnMs(resolve.hirCrate().types);
    const HIRGenericParams* implParamsDef = nullptr;

    const auto* pathP = fcnPath;
    if (const auto* e = pathP->data.opt_UfcsKnown()) {
        if (e->type->is_Function() || e->type->is_NamedFunction()) {
            if (e->trait.path == resolve.langFn() || e->trait.path == resolve.langFnMut() || e->trait.path == resolve.langFnOnce()) {
                if (const auto* nf = e->type->opt_NamedFunction()) {
                    pathP = &nf->path;
                } else {
                    MIR_TODO(localState.state, "Get function from fn-ptr - " << e->type);
                }
                // TODO: Convert `call_args` - discard the first and extract tuple from the second
                const auto& argTupleTy = e->trait.params.types.at(0);
                const auto* argTupleRepr = TargetGetTypeRepr(state.sp, state.resolve, argTupleTy);
                auto argTupleV = std::move(callArgs.at(1));
                MIREvalValueRef argTuple(argTupleV);
                callArgs.clear();
                callArgs.reserve(argTupleRepr->fields.size());
                for (const auto& fld : argTupleRepr->fields) {
                    auto size = localState.sizeOfOrBug(fld.ty);
                    callArgs.push_back(MIREvalAllocationPtr::allocate(localState.valuePool, state.resolve, state, fld.ty));
                    auto vr = MIREvalValueRef(callArgs.back());
                    vr.copyFrom(state, argTuple.slice(fld.offset, size));
                }
            } else {
                // Ignore: Not a fn trait
            }
        }
    }
    const auto& path = *pathP;

    // A const call with unresolved type or const arguments is not evaluatable
    // until monomorphisation.  Resolving the item is still possible, but
    // executing its MIR here would incorrectly validate (or reject) just one
    // generic definition rather than each concrete instance.
    if (monomorphisePathNeeded(path)) {
        THROW_DEFER(state.sp, GenericValue, "const call to generic " << path);
    }

    if (requireConstCalls) {
        if (const auto* e = path.data.opt_UfcsKnown()) {
            const auto& trait = resolve.hirCrate().getTraitByPath(state.sp, e->trait.path);
            if (trait.isConst) {
                ImplRef bestImpl;
                bool hasConstBound = false;
                resolve.findImpl(state.sp, e->trait.path, e->trait.params, e->type, [&](ImplRef impl, bool isFuzzed) {
                    if (isFuzzed) {
                        return false;
                    }
                    if (!impl.data.is_TraitImpl()) {
                        hasConstBound |= impl.boundConstness() != HIRBoundConstness::Never;
                        return false;
                    }
                    if (!bestImpl.isValid() || impl.moreSpecificThan(resolve.hirCrate().types, bestImpl)) {
                        bestImpl = mv$(impl);
                    }
                    return false;
                });
                MIR_ASSERT(state, hasConstBound || bestImpl.isValid(), "const trait call did not resolve to an impl: " << path);
                MIR_ASSERT(state, hasConstBound || bestImpl.data.as_TraitImpl().impl->isConst, "const trait call requires a const impl: " << path);
            }
        }
    }

    auto rv = getEntFullpath(localState.state.sp, resolve, path, EntNS::Value, fcnMs, &implParamsDef);
    if (const auto* fcnP = rv.opt_Function()) {
        const HIRFunction& fcn = **fcnP;
        const auto& ep = fcn.code;
        if (ep && ep.state->stage < HIRExprState::Stage::ConstEval) {
            auto prev = ep.state->stage;
            ep.state->stage = HIRExprState::Stage::ConstEvalRequest;
            // Run consteval on the arguments and return type
            ConvertHIRConstantEvaluateFcnSig(resolve.board(), resolve.hirCrate(), implParamsDef, path, const_cast<HIRFunction&>(fcn));
            ep.state->stage = prev;
        }

        DEBUG("Call function " << *fcnPath << ": fcn_ms=" << fcnMs);

        // TODO: Set m_const during parse and check here
        if (!fcn.code && !fcn.code.mir) {
            if (fcn.linkage.name == "") {
            } else if (fcn.linkage.name == "panic_impl") {
                MIR_TODO(state, "panic in constant evaluation");
            } else {
                MIR_TODO(state, "Call extern function `" << fcn.linkage.name << "` (" << *fcnPath << ")");
            }
        }

        // Call by invoking evaluate_constant on the function
        const auto* mir = this->resolve.hirCrate().getOrGenMir(this->resolve.board(), HIRItemPath(*fcnPath), fcn);
        MIR_ASSERT(state, mir, "No MIR for function " << *fcnPath);

        // Monomorphised argument types
        HIRFunction::argsT argDefs;
        for (const auto& a : fcn.args) {
            auto argTy = this->resolve.monomorphExpand(this->rootSpan, a.second, fcnMs);
            this->resolve.revealOpaqueTypes(this->rootSpan, argTy);
            argDefs.push_back(::std::make_pair(HIRPattern(), std::move(argTy)));
        }
        auto retTy = this->resolve.monomorphExpand(this->rootSpan, fcn.returnType, fcnMs);
        this->resolve.revealOpaqueTypes(this->rootSpan, retTy);

        const bool tracksCaller = resolve.hirCrate().functionTracksCaller(state.sp, path, fcn);
        SourceLocation callerLocation;
        if (tracksCaller) {
            if (indirect) {
                callerLocation = fcn.source;
            } else if (localState.tracksCaller) {
                callerLocation = localState.callerLocation;
            } else {
                callerLocation = callsite;
            }
        }

        pushStackEntry(
            HIRItemPath(*fcnPath),
            *mir,
            std::move(fcnMs),
            std::move(retTy),
            ::std::move(argDefs),
            std::move(callArgs),
            &fcn.params,
            implParamsDef,
            std::move(callerLocation),
            tracksCaller
        );
        return true;
    } else if (rv.is_NotFound() && monomorphisePathNeeded(path)) {
        THROW_DEFER(state.sp, GenericValue, "unresolved generic path " << path);
    } else if (rv.is_Struct()) {
        // Set destination, same way as `RValue::Struct` does
        auto dst = localState.getLval(rvSlot);

        HIRTypeRef tmp;
        const auto& ty = state.getLvalueType(tmp, rvSlot);
        auto* repr = TargetGetTypeRepr(state.sp, resolve, ty);
        if (!repr) {
            BUG(state.sp, "Layout not computable during const evaluation - " << "repr of " << ty);
        }
        MIR_ASSERT(state, repr->fields.size() == callArgs.size(), "");
        for (size_t i = 0; i < callArgs.size(); i++) {
            size_t sz = localState.sizeOfOrBug(repr->fields[i].ty);
            auto localDst = dst.slice(repr->fields[i].offset, sz);
            localDst.copyFrom(state, MIREvalValueRef(callArgs[i]));
            DEBUG("@" << repr->fields[i].offset << " = " << localDst);
        }
        return false;
    } else if (rv.is_Enum()) {
        const auto& enumEnt = rv.as_Enum();
        auto dst = localState.getLval(rvSlot);
        HIRTypeRef tmp;
        const auto& ty = state.getLvalueType(tmp, rvSlot);
        writeCtfeEnumVariant(resolve, localState, dst, ty, enumEnt.idx, callArgs.size(),
            [&](MIREvalValueRef fieldDst, size_t i) {
                fieldDst.copyFrom(state, MIREvalValueRef(callArgs[i]));
            });
        return false;
    } else {
        MIR_TODO(state, "Could not find function for " << path << " - " << rv.tagStr());
    }
}

void HIREvaluator::callConstDestructor(MIREvalCallStackEntry& localState, HIRTypeRef ty, const MIRLValue& slot) {
    const auto& state = localState.state;
    auto& types = resolve.hirCrate().types;
    DEBUG("Const drop of " << ty << " at " << slot);

    ::std::vector<MIREvalAllocationPtr> callArgs;
    callArgs.push_back(MIREvalAllocationPtr::allocate(localState.valuePool, resolve, state, types.borrow(HIRBorrowType::Unique, ty)));
    localState.writeParam(MIREvalValueRef(callArgs.back()), MIRParam::make_Borrow({HIRBorrowType::Unique, slot.clone()}));
    auto* path = localState.valuePool->make<HIRPath>(ty, HIRGenericPath(resolve.langDrop()), RcString::newInterned("drop"), HIRPathParams{});

    // The destructor runs to completion on a stack of its own: a `Drop`
    // terminator has no return slot for the evaluator to resume into.
    auto saved = ::std::move(this->callStack);
    this->callStack.clear();
    STD_DEFER {
        this->callStack = ::std::move(saved);
    };
    if (this->callFunction(localState, MIRLValue::newReturn(), path, ::std::move(callArgs), localState.callerLocation, false)) {
        this->runUntilStackEmpty();
    }
}

/// Run the destructors of a value going out of scope during constant
/// evaluation. Only `impl const Drop` ones exist here: any other destructor was
/// rejected by `valueNeedsNonConstDrop` before this point.
void HIREvaluator::runConstDrop(MIREvalCallStackEntry& localState, HIRTypeRef ty, const MIRLValue& slot) {
    const auto& state = localState.state;
    if (!localState.rootResolve.typeNeedsDropGlue(state.sp, ty)) {
        return;
    }

    switch ((*ty).tag()) {
default:
        // Nothing else can carry a destructor.
        return;
        case HIRTypeData::TAG_Path: {
            auto& te = (*ty).as_Path();
            const auto* markings = te.binding.getTraitMarkings();
            if (!markings) {
                return;
            }
            if (markings->hasDropImpl) {
                if (!markings->hasConstDropImpl) {
                    return;
                }
                this->callConstDestructor(localState, ty, slot);
            }
            switch (te.binding.tag()) {
default:
                return;
                case HIRTypePathBinding::TAG_Struct: {
                    const auto* repr = TargetGetTypeRepr(state.sp, localState.rootResolve, ty);
                    MIR_ASSERT(state, repr, "No representation for struct " << ty);
                    for (size_t i = 0; i < repr->fields.size(); i++) {
                        this->runConstDrop(localState, repr->fields[i].ty, MIRLValue::newField(slot.clone(), static_cast<unsigned>(i)));
                    }
                    break;
                }
                case HIRTypePathBinding::TAG_Enum: {
                    auto& pbe = te.binding.as_Enum();
                    if (!pbe->data.is_Data()) {
                        return;
                    }
                    const auto variant = localState.readEnumVariant(ty, localState.getLval(slot));
                    const auto* repr = TargetGetTypeRepr(state.sp, localState.rootResolve, ty);
                    MIR_ASSERT(state, repr, "No representation for enum " << ty);
                    MIR_ASSERT(state, variant < repr->fields.size(), "Enum representation has no variant " << variant << " for " << ty);
                    this->runConstDrop(localState, repr->fields[variant].ty, MIRLValue::newDowncast(slot.clone(), variant));
                    break;
                }
            }
            break;
        }
        case HIRTypeData::TAG_Array: {
            auto& te = (*ty).as_Array();
            if (!te.size.is_Known()) {
                return;
            }
            for (size_t i = 0; i < te.size.as_Known(); i++) {
                this->runConstDrop(localState, te.inner, MIRLValue::newField(slot.clone(), static_cast<unsigned>(i)));
            }
            break;
        }
        case HIRTypeData::TAG_Tuple: {
            auto& te = (*ty).as_Tuple();
            for (size_t i = 0; i < te.size(); i++) {
                this->runConstDrop(localState, te.at(i), MIRLValue::newField(slot.clone(), static_cast<unsigned>(i)));
            }
            break;
        }
    }
}

EncodedLiteral HIREvaluator::allocationToEncoded(const HIRTypeData* ty, const MIREvalAllocation& a) {
    const auto* aBytes = a.getBytes(0, a.size(), false); // NOTE: Read the uninitialised bytes (they _should_ be zeroes)
    ASSERT_BUG(this->rootSpan, aBytes, "Unable to get entire allocation - " << FMT_CB(ss, a.fmt(ss, 0, a.size())));
    EncodedLiteral rv;
    rv.bytes.insert(rv.bytes.begin(), aBytes, aBytes + a.size());
    for (const auto& r : a.getRelocations()) {
        if (const auto* innerAlloc = r.ptr.asAllocation()) {
            // Create a new static
            if (innerAlloc->isConstHeapAllocation() || innerAlloc->isWritable()) {
                ASSERT_BUG(this->rootSpan, innerAlloc->isAlive(), "constant contains a pointer to a deallocated allocation");
                auto innerVal = allocationToEncoded(innerAlloc->getType(), *innerAlloc);
                HIRTypeRef staticType;
                size_t staticAlignment = 0;
                if (innerAlloc->isConstHeapAllocation()) {
                    if (!innerAlloc->wasMadeGlobal()) {
                        ERROR(this->rootSpan, E0000, "a const heap allocation escaped without const_make_global");
                    }
                    staticType = resolve.hirCrate().types.array(
                        resolve.hirCrate().types.primitive(HIRCoreType::U8),
                        innerAlloc->size()
                    );
                    staticAlignment = innerAlloc->getHeapAlignment();
                } else {
                    ASSERT_BUG(this->rootSpan, innerAlloc->getType(), "typed CTFE allocation has no type");
                    staticType = MonomorphiserNop(resolve.hirCrate().types).monomorphType(Span(), innerAlloc->getType());
                }

                auto itemPath = nvs.newStatic(mv$(staticType), mv$(innerVal), staticAlignment);

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
            BUG(this->rootSpan, "");
        }
    }
    return rv;
}

EncodedLiteral HIREvaluator::evaluateConstant(const HIRItemPath& ip, const HIRExprPtr& expr, HIRTypeRef exp) {
    return evaluateConstant(ip, expr, exp, MonomorphState(resolve.hirCrate().types));
}

EncodedLiteral HIREvaluator::evaluateConstant(const HIRItemPath& ip, const HIRExprPtr& expr, HIRTypeRef exp, MonomorphState ms) {
    TRACE_FUNCTION_F(ip);
    DEBUG("ms = " << ms);
    const auto* mir = this->resolve.hirCrate().getOrGenMir(this->resolve.board(), ip, expr, exp);

    // rustc evaluates constants in reveal-all mode: all opaque types have
    // their hidden representation before CTFE asks for layout or dispatch.
    resolve.revealOpaqueTypes(expr.span(), exp);

    if (mir) {
        ASSERT_BUG(Span(), expr.state, "");
        if (!resolve.itemGenericsPtr() && !resolve.implGenericsPtr()) {
            resolve.setBothGenericsRaw(expr.state->implGenerics, expr.state->itemGenerics);
        }
    }

    // Fill each missing generic layer independently.  An associated generic
    // const arrives with the impl layer already set, but still needs its item
    // parameters while evaluating the body.
    HIRPathParams nopParamsImpl;
    HIRPathParams nopParamsMethod;
    if (!ms.ppImpl || !ms.ppMethod) {
        if (!ms.ppMethod && resolve.itemGenericsPtr()) {
            ms.ppMethod = &(nopParamsMethod = resolve.itemGenericsPtr()->makeNopParams(resolve.hirCrate().types, 1));
        }
        if (!ms.ppImpl && resolve.implGenericsPtr()) {
            ms.ppImpl = &(nopParamsImpl = resolve.implGenericsPtr()->makeNopParams(resolve.hirCrate().types, 0));
        }
        DEBUG("(filled missing params) ms = " << ms);
    }

    if (mir) {
        // Might want to have a fully-populated MonomorphState for expanding inside impl blocks
        // HACK: Generate a roughly-correct one
        const auto& topIp = ip.getTopIp();
        if (topIp.trait && !topIp.ty && !ms.selfTy) {
            ms.selfTy = resolve.hirCrate().types.self();
        }

        assert(this->callStack.empty());
        this->numFrames = 0;
        // Note: Since this is the entrypoint, `this->resolve` has the correct GenericParams
        this->pushStackEntry(ip, *mir, std::move(ms), std::move(exp), {}, {}, resolve.itemGenericsPtr(), resolve.implGenericsPtr(), SourceLocation(this->rootSpan), false);
        auto rvRaw = this->runUntilStackEmpty();

        ASSERT_BUG(this->rootSpan, rvRaw, "evaluate_constant_mir returned null allocation");
        DEBUG(ip << " = " << MIREvalValueRef(rvRaw));

        return this->allocationToEncoded(exp, *rvRaw);
    } else {
        BUG(this->rootSpan, "Attempting to evaluate constant expression with no associated code");
    }
}

namespace {
    struct GenericParamsCallback {
        virtual const HIRGenericParams& get(const Span& sp) = 0;
    };

    template <typename F>
    struct GenericParamsCb final: GenericParamsCallback {
        F f;

        explicit GenericParamsCb(F f)
            : f(f)
        {
        }

        const HIRGenericParams& get(const Span& sp) override {
            return f(sp);
        }
    };

    struct Expander: public HIRVisitor {
        const WireBoard& wb;
        const HIRCrate& crate;
        const HIRModule* mod;
        const HIRItemPath* modPath;
        MonomorphState monomorphState;
        bool recurseTypes;

        const HIRGenericParams* implParams;
        const HIRGenericParams* itemParams;

        GenericParamsCallback* getParams = nullptr;
        // Set while visiting a method call before main typecheck: its
        // parameter definitions do not exist yet, so parameter values are
        // left Unevaluated for the post-typecheck demand sites.
        bool paramsUnresolved = false;

        enum class Pass {
            OuterOnly,
            Values,
        } pass;

        Expander(const WireBoard& wb)
            : HIRVisitor(nullptr, wb.crate->types)
            , wb(wb)
            , crate(*wb.crate)
            , mod(nullptr)
            , modPath(nullptr)
            , monomorphState(crate.types)
            , recurseTypes(false)
            , implParams(nullptr)
            , itemParams(nullptr)
            , pass(Pass::OuterOnly)
        {
        }

        HIREvaluator getEval(const Span& sp, NewvalState& nvs) const {
            auto eval = HIREvaluator{sp, wb, nvs};
            eval.setRequireConstCalls();
            eval.resolve.setBothGenericsRaw(implParams, itemParams);
            return eval;
        }

        HIRPathParams getParamsForDef(const HIRGenericParams& tpl, bool isFunctionLevel = false) const {
            return tpl.makeNopParams(crate.types, isFunctionLevel ? 1 : 0);
        }

        void visitModule(HIRItemPath p, HIRModule& mod) override {
            auto savedMp = modPath;
            auto savedM = this->mod;
            this->mod = &mod;
            modPath = &p;

            HIRVisitor::visitModule(p, mod);

            this->mod = savedM;
            modPath = savedMp;
        }

        void visitFunction(HIRItemPath p, HIRFunction& f) override {
            TRACE_FUNCTION_F(p);

            auto ppFcn = getParamsForDef(f.params, true);
            monomorphState.ppMethod = &ppFcn;
            itemParams = &f.params;
            HIRVisitor::visitFunction(p, f);
            itemParams = nullptr;
            monomorphState.ppMethod = nullptr;
        }

        void visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) override {
            static Span sp;
            TRACE_FUNCTION_F("impl" << impl.params.fmtArgs() << " " << traitPath << impl.traitArgs << " for " << impl.type);

            auto mp = HIRItemPath(impl.srcModule);
            modPath = &mp;
            mod = &crate.getModByPath(sp, impl.srcModule);

            auto ppImpl = getParamsForDef(impl.params);
            monomorphState.ppImpl = &ppImpl;
            auto savedSelf = std::move(monomorphState.selfTy);
            monomorphState.selfTy = monomorphState.monomorphType(sp, impl.type);
            implParams = &impl.params;

            HIRVisitor::visitTraitImpl(traitPath, impl);

            assert(implParams);
            implParams = nullptr;
            monomorphState.ppImpl = nullptr;
            monomorphState.selfTy = std::move(savedSelf);

            mod = nullptr;
            modPath = nullptr;
        }

        void visitTypeImpl(HIRTypeImpl& impl) override {
            static Span sp;
            TRACE_FUNCTION_F("impl" << impl.params.fmtArgs() << " " << impl.type);

            auto mp = HIRItemPath(impl.srcModule);
            modPath = &mp;
            mod = &crate.getModByPath(sp, impl.srcModule);

            auto ppImpl = getParamsForDef(impl.params);
            monomorphState.ppImpl = &ppImpl;
            auto savedSelf = std::move(monomorphState.selfTy);
            monomorphState.selfTy = monomorphState.monomorphType(sp, impl.type);
            implParams = &impl.params;

            HIRVisitor::visitTypeImpl(impl);

            assert(implParams);
            implParams = nullptr;
            monomorphState.ppImpl = nullptr;
            monomorphState.selfTy = std::move(savedSelf);

            mod = nullptr;
            modPath = nullptr;
        }

        void visitInherentType(HIRItemPath p, HIRTypeAlias& item) override {
            auto ppItem = getParamsForDef(item.params, true);
            monomorphState.ppMethod = &ppItem;
            itemParams = &item.params;
            HIRVisitor::visitInherentType(p, item);
            itemParams = nullptr;
            monomorphState.ppMethod = nullptr;
        }

        void visitTrait(HIRItemPath ip, HIRTrait& trait) override {
            auto ppImpl = getParamsForDef(trait.params);
            auto savedSelf = std::move(monomorphState.selfTy);
            monomorphState.selfTy = crate.types.self();
            monomorphState.ppImpl = &ppImpl;
            implParams = &trait.params;

            HIRVisitor::visitTrait(ip, trait);

            assert(implParams);
            implParams = nullptr;
            monomorphState.ppImpl = nullptr;
            monomorphState.selfTy = std::move(savedSelf);
        }

        void evalulateConstGeneric(const Span& sp, const HIRTypeData* ty, HIRConstGeneric& v) {
            if (v.is_Unevaluated()) {
                const bool predicted = unevaluatedUsedSlotsAreConcrete(crate.types, *v.as_Unevaluated());
                if (!predicted && !sCapsOracle) {
                    translateConstExprBody(sp, wb, crate, ty, *v.as_Unevaluated());
                    return;
                }
                try {
                    v = HIRConstGeneric::make_Evaluated(freezeEncodedLiteral(evaluateConstgeneric(sp, wb, crate, ty, *v.as_Unevaluated())));
                    if (!predicted) {
                        fprintf(stderr, "PREDICATE MISS [evalulateConstGeneric]\n");
                    }
                } catch (const Defer&) {
                    // Deferred - no update
                }
            }
        }

        void visitPathParams(HIRPathParams& p) override {
            static Span sp;
            for (auto& v : p.values) {
                if (v.is_Unevaluated()) {
                    if (!getParams) {
                        ASSERT_BUG(sp, paramsUnresolved, "Path parameters visited without their definition");
                        continue;
                    }
                    try {
                        const auto& paramsDef = getParams->get(sp);
                        auto idx = static_cast<size_t>(&v - &p.values.front());
                        ASSERT_BUG(sp, idx < paramsDef.values.size(), "");
                        const auto& ty = paramsDef.values[idx].type;
                        // A const parameter's own type may be generic
                        // (`const M: [T; N]`), and the value cannot be evaluated
                        // before the type is known. Leave it for monomorphisation.
                        if (monomorphiseTypeNeeded(ty)) {
                            continue;
                        }
                        evalulateConstGeneric(sp, ty, v);
                    } catch (const Defer&) {
                        // Deferred - no update
                    }
                }
            }
            HIRVisitor::visitPathParams(p);
        }

        void visitConstgeneric(HIRConstGeneric& value) override {
            if (auto* unevaluated = value.opt_Unevaluated()) {
                // `selfType` is the captured evaluation environment, not a
                // child of the expression.  In an impl header it contains a
                // pre-binding snapshot of this same const argument; walking
                // that snapshot would evaluate it without its impl params.
                visitPathParams((*unevaluated)->paramsImpl);
                visitPathParams((*unevaluated)->paramsItem);
                visitExpr(*(*unevaluated)->expr);
            }
        }

        void visitParams(HIRGenericParams& params) override {
            static Span sp;
            for (auto& v : params.values) {
                evalulateConstGeneric(sp, v.type, v.defaultValue);
            }
            HIRVisitor::visitParams(params);
        }

        void visitGenericPath(HIRGenericPath& p, HIRVisitor::PathContext pc) override {
            TRACE_FUNCTION_FR(p, p);
            auto saved = getParams;
            auto callback = makeCallable<GenericParamsCb>([&](const Span& sp) -> const HIRGenericParams& {
                DEBUG("visit_generic_path[m_get_params] " << p);
                switch (pc) {
                    case HIRVisitor::PathContext::VALUE: {
                        if (p.path.components().size() > 1) {
                            const auto& parent = crate.getTypeitemByPath(sp, p.path, /*ignore_crate_name=*/false, /*ignore_last_node=*/true);
                            if (const auto* enm = parent.opt_Enum()) {
                                return enm->params;
                            }
                        }
                        auto& vi = crate.getValitemByPath(sp, p.path);
                    switch (vi.tag()) {
                        case HIRValueItem::TAG_Import: {
                            BUG(sp, "Module Import");
                            break;
                        }
                        case HIRValueItem::TAG_Static: {
                            BUG(sp, "Getting params definition for Static - " << p);
                            break;
                        }
                        case HIRValueItem::TAG_Constant: {
                            const auto& e = *vi.as_Constant();
                            return e.params;
                        }
                        case HIRValueItem::TAG_Function: {
                            const auto& e = *vi.as_Function();
                            return e.params;
                        }
                        case HIRValueItem::TAG_StructConstant: {
                            auto& e = vi.as_StructConstant();
                            return crate.getStructByPath(sp, e.ty).params;
                        }
                        case HIRValueItem::TAG_StructConstructor: {
                            auto& e = vi.as_StructConstructor();
                            return crate.getStructByPath(sp, e.ty).params;
                        }
                    }
                    break;
                    }
                    case HIRVisitor::PathContext::TYPE:
                    case HIRVisitor::PathContext::TRAIT: {
                        auto& vi = crate.getTypeitemByPath(sp, p.path);
                    switch (vi.tag()) {
                        case HIRTypeItem::TAG_Import: {
                            BUG(sp, "Module Import");
                            break;
                        }
                        case HIRTypeItem::TAG_Module: {
                            BUG(sp, "mod - " << p);
                            break;
                        }
                        case HIRTypeItem::TAG_TypeAlias: {
                            BUG(sp, "type - " << p);
                            break;
                        }
                        case HIRTypeItem::TAG_TraitAlias: {
                            BUG(sp, "trait= - " << p);
                            break;
                        }
                        case HIRTypeItem::TAG_Struct: {
                            auto& e = vi.as_Struct();
                            return e.params;
                        }
                        case HIRTypeItem::TAG_Enum: {
                            auto& e = vi.as_Enum();
                            return e.params;
                        }
                        case HIRTypeItem::TAG_Union: {
                            auto& e = vi.as_Union();
                            return e.params;
                        }
                        case HIRTypeItem::TAG_Trait: {
                            auto& e = vi.as_Trait();
                            return e.params;
                        }
                        case HIRTypeItem::TAG_ExternType: {
                            BUG(sp, "extern type - " << p);
                            break;
                        }
                    }
                    break;
                    }
                }
                TODO(sp, "visit_generic_path[m_get_params] - " << p);
            });
            getParams = &callback;
            HIRVisitor::visitGenericPath(p, pc);
            getParams = saved;
        }

        void visitAtyParams(const HIRGenericPath& sourceTrait, const RcString& name, HIRPathParams& params) {
            auto saved = getParams;
            auto callback = makeCallable<GenericParamsCb>([&](const Span& sp) -> const HIRGenericParams& {
                const auto& trait = crate.getTraitByPath(sp, sourceTrait.path);
                auto it = trait.types.find(name);
                ASSERT_BUG(sp, it != trait.types.end(), "Trait " << sourceTrait.path << " has no associated type " << name);
                return it->second.generics;
            });
            getParams = &callback;
            visitPathParams(params);
            getParams = saved;
        }

        void visitTraitPath(HIRTraitPath& p) override {
            visitGenericPath(p.path, HIRVisitor::PathContext::TRAIT);
            for (auto& assoc : p.typeBounds) {
                visitGenericPath(assoc.second.sourceTrait, HIRVisitor::PathContext::TRAIT);
                visitAtyParams(assoc.second.sourceTrait, assoc.first, assoc.second.atyParams);
                assoc.second.type = visitType(assoc.second.type);
            }
            for (auto& assoc : p.traitBounds) {
                visitGenericPath(assoc.second.sourceTrait, HIRVisitor::PathContext::TRAIT);
                visitAtyParams(assoc.second.sourceTrait, assoc.first, assoc.second.atyParams);
                for (auto& trait : assoc.second.traits) {
                    visitTraitPath(trait);
                }
            }
        }

        void visitPath(HIRPath& p, HIRVisitor::PathContext pc) override {
            auto saved = getParams;
            auto callback = makeCallable<GenericParamsCb>([&](const Span& sp) -> const HIRGenericParams& {
                DEBUG("visit_path[m_get_params] " << p);
                StaticTraitResolve resolve(wb);
                resolve.setBothGenericsRaw(implParams, itemParams);
                switch (pc) {
                    case HIRVisitor::PathContext::VALUE: {
                        MonomorphState unused(crate.types);
                        auto vi = resolve.getValue(sp, p, unused, true);
                    switch (vi.tag()) {
                        case TypeckValuePtr::TAG_NotFound: {
                            BUG(sp, "NotFound");
                            break;
                        }
                        case TypeckValuePtr::TAG_NotYetKnown: {
                            TODO(sp, "NotYetKnown");
                            break;
                        }
                        case TypeckValuePtr::TAG_Static: {
                            auto& e = vi.as_Static();
                            return e->params;
                        }
                        case TypeckValuePtr::TAG_Constant: {
                            auto& e = vi.as_Constant();
                            return e->params;
                        }
                        case TypeckValuePtr::TAG_Function: {
                            auto& e = vi.as_Function();
                            return e->params;
                        }
                        case TypeckValuePtr::TAG_EnumConstructor: {
                            TODO(sp, "Handle EnumConstructor - " << p);
                            break;
                        }
                        case TypeckValuePtr::TAG_EnumValue: {
                            TODO(sp, "Handle EnumValue - " << p);
                            break;
                        }
                        case TypeckValuePtr::TAG_StructConstructor: {
                            TODO(sp, "Handle StructConstructor - " << p);
                            break;
                        }
                        case TypeckValuePtr::TAG_StructConstant: {
                            TODO(sp, "Handle StructConstant - " << p);
                            break;
                        }
                    }
                    break;
                    }
                    case HIRVisitor::PathContext::TYPE:
                    case HIRVisitor::PathContext::TRAIT: {
                        if (const auto* e = p.data.opt_UfcsKnown()) {
                            const auto& trait = crate.getTraitByPath(sp, e->trait.path);
                            const auto it = trait.types.find(e->item);
                            ASSERT_BUG(sp, it != trait.types.end(), "Trait " << e->trait.path << " has no associated type " << e->item);
                            return it->second.generics;
                        }
                        BUG(sp, "type - " << p);
                        break;
                    }
                }
                TODO(sp, "visit_path[m_get_params] - " << p);
            });
            getParams = &callback;
            HIRVisitor::visitPath(p, pc);
            getParams = saved;
        }

        void visitArraysize(HIRArraySize& as) {
            if (as.is_Unevaluated() && as.as_Unevaluated().is_Unevaluated()) {
                TRACE_FUNCTION_FR(as, as);
                const auto& unevaluated = *as.as_Unevaluated().as_Unevaluated();
                const auto& exprPtr = *unevaluated.expr;

                const bool predicted = unevaluatedUsedSlotsAreConcrete(crate.types, unevaluated);
                if (!predicted && !sCapsOracle) {
                    // A bare const parameter gets its symbolic form; anything
                    // else stays Unevaluated for monomorphisation-time demand.
                    const auto* tn = cast<const HIRExprNodeConstParam>(&*exprPtr);
                    if (tn) {
                        as = HIRConstGeneric(HIRGenericRef(tn->name, tn->binding));
                    } else {
                        translateConstExprBody(exprPtr->span(), wb, crate, crate.types.primitive(HIRCoreType::Usize), unevaluated);
                    }
                    return;
                }
                try {
                    auto val = evaluateConstgeneric(exprPtr->span(), wb, crate, crate.types.primitive(HIRCoreType::Usize), unevaluated);
                    as = val.readUsize(0);
                    if (!predicted) {
                        fprintf(stderr, "PREDICATE MISS [visitArraysize]\n");
                    }
                } catch (const Defer&) {
                    const auto* tn = cast<const HIRExprNodeConstParam>(&*exprPtr);
                    if (tn) {
                        as = HIRConstGeneric(HIRGenericRef(tn->name, tn->binding));
                    }
                }
            } else {
                DEBUG("Array size (known) = " << as);
            }
        }

        [[nodiscard]] HIRTypeRef visitType(HIRTypeRef ty) override {
            // Params and consts embedded in these types carry values this pass
            // evaluates; route them through the owned-structure hooks.
            ty = visitTypeDefaultViaHooks(ty);

            if (ty->is_Array()) {
                auto data = ty->cloneData();
                auto& e = data.as_Array();
                TRACE_FUNCTION_FR(ty, ty);
                visitArraysize(e.size);
                ty = crate.types.intern(mv$(data));
            }

            if (ty->is_Pattern()) {
                auto data = ty->cloneData();
                auto& e = data.as_Pattern();
                auto evaluateEndpoint = [&](HIRConstGeneric& value) {
                    if (const auto* unevaluated = value.opt_Unevaluated()) {
                        const bool predicted = unevaluatedUsedSlotsAreConcrete(crate.types, **unevaluated);
                        if (!predicted && !sCapsOracle) {
                            translateConstExprBody(Span(), wb, crate, e.inner, **unevaluated);
                            return;
                        }
                        try {
                            value = freezeEncodedLiteral(evaluateConstgeneric(Span(), wb, crate, e.inner, **unevaluated));
                        } catch (const Defer&) {
                        }
                    }
                };
                for (auto& range : e.pattern.alternatives) {
                    if (range.hasStart) evaluateEndpoint(range.start);
                    if (range.hasEnd) evaluateEndpoint(range.end);
                }
                ty = crate.types.intern(mv$(data));
            }

            if (recurseTypes) {
                recurseTypes = false;
                if (const auto* te = ty->opt_Path()) {
                    switch (te->binding.tag()) {
                        case HIRTypePathBinding::TAG_Unbound: {
                            auto& _ = te->binding.as_Unbound();
                            break;
                        }
                        case HIRTypePathBinding::TAG_Opaque: {
                            auto& _ = te->binding.as_Opaque();
                            break;
                        }
                        case HIRTypePathBinding::TAG_Struct: {
                            auto& pbe = te->binding.as_Struct();
                            // If this struct hasn't been visited already, visit it
                            auto savedIp = implParams;
                            implParams = nullptr;
                            this->visitStruct(te->path.data.as_Generic().path, const_cast<HIRStruct&>(*pbe));
                            implParams = savedIp;
                            break;
                        }
                        case HIRTypePathBinding::TAG_Union: {
                            break;
                        }
                        case HIRTypePathBinding::TAG_Enum: {
                            break;
                        }
                        case HIRTypePathBinding::TAG_ExternType: {
                            break;
                        }
                    }
                }
                recurseTypes = true;
            }
            return ty;
        }

        void visitConstant(HIRItemPath p, HIRConstant& item) override {
            TRACE_FUNCTION_F(p);
            itemParams = &item.params;

            recurseTypes = true;
            HIRVisitor::visitConstant(p, item);
            recurseTypes = false;

            // NOTE: Consteval needed here for MIR match generation to work
            if (pass != Pass::Values) {
            } else if (item.valueState == HIRConstant::ValueState::Generic
                || item.params.isGeneric()
                || monomorphisePathNeeded(p.getFullPath())) {
                // Generic constants are evaluated for each concrete path.  A
                // body that happens not to read its parameters must not run
                // while visiting the definition: the instantiation may be
                // unused or occur only in a dead branch.
                item.valueState = HIRConstant::ValueState::Generic;
            } else if (item.valueState == HIRConstant::ValueState::InProgress) {
                ERROR(item.value.span(), E0000, "cycle detected when evaluating constant `" << p << "`");
            } else if (item.value || item.value.mir) {
                auto nvs = NewvalState{*mod, *modPath, FMT(p.getName() << "#")};
                auto eval = getEval(item.value.span(), nvs);
                item.valueState = HIRConstant::ValueState::InProgress;
                try {
                    item.valueRes = eval.evaluateConstant(p, item.value, item.type, monomorphState.clone());
                    item.valueState = HIRConstant::ValueState::Known;
                } catch (const Defer& e) {
                    if (e.reason != Defer::Reason::NotYetKnown) {
                        BUG(item.value.span(), "Defer(" << e.reason << ") evaluating constant " << p);
                    }
                    item.valueState = HIRConstant::ValueState::Generic;
                }

                DEBUG("constant: " << item.type << " = " << item.valueRes);
            } else {
                DEBUG("constant?"); // " << *item.m_value);
            }

            itemParams = nullptr;
        }

        void visitStatic(HIRItemPath p, HIRStatic& item) override {
            TRACE_FUNCTION_F(p);
            itemParams = &item.params;

            recurseTypes = true;
            HIRVisitor::visitStatic(p, item);
            recurseTypes = false;

            if (pass != Pass::Values) {
            } else if (item.value) {
                auto nvs = NewvalState{*mod, *modPath, FMT(p.getName() << "#")};
                auto eval = getEval(item.value.span(), nvs);
                item.valueEvaluating = true;
                STD_DEFER {
                    item.valueEvaluating = false;
                };
                try {
                    item.valueRes = eval.evaluateConstant(p, item.value, item.type);
                    item.valueGenerated = true;
                } catch (const Defer& e) {
                    ERROR(item.value->span(), E0000, "Defer(" << e.reason << ") evaluating top-level static");
                }

                DEBUG("static: " << item.type << " = " << item.valueRes);
            }

            itemParams = nullptr;
        }

        void visitEnum(HIRItemPath p, HIREnum& item) override {
            static Span sp;
            assert(!implParams);
            implParams = &item.params;

            visitEnumInner(wb, crate, p, *mod, *modPath, p.getName(), item);
            HIRVisitor::visitEnum(p, item);

            assert(implParams);
            implParams = nullptr;
        }

        void visitStruct(HIRItemPath p, HIRStruct& item) override {
            assert(!implParams);
            implParams = &item.params;
            if (item.constEvalState != HIRConstEvalState::Complete) {
                ASSERT_BUG(Span(), item.constEvalState == HIRConstEvalState::None, "Constant evaluation loop involving " << p);
                item.constEvalState = HIRConstEvalState::Active;
                HIRVisitor::visitStruct(p, item);
                item.constEvalState = HIRConstEvalState::Complete;
            }
            assert(implParams);
            implParams = nullptr;
        }

        void visitExpr(HIRExprPtr& expr) override {
            struct Visitor: public HIRExprVisitorDef {
                /// A value is out of the tree while a borrow of it is being
                /// lifted into a static: making that static evaluates it, and
                /// a static that names itself walks back through here before
                /// the hole has been filled.
                void visitNodePtr(HIRExprNodeP& nodePtr) override {
                    if (!nodePtr) {
                        return;
                    }
                    HIRExprVisitorDef::visitNodePtr(nodePtr);
                }

                Expander& exp;

                Visitor(Expander& exp)
                    : HIRExprVisitorDef(exp.crate.types)
                    , exp(exp)
                {
                }

                [[nodiscard]] HIRTypeRef visitType(HIRTypeRef ty) override {
                    // Need to evaluate array sizes
                    DEBUG("expr type " << ty);
                    return exp.visitType(ty);
                }

                void visitPathParams(HIRPathParams& pp) override {
                    // Explicit call to handle const params (eventually)
                    exp.visitPathParams(pp);
                }

                void visitPath(HIRVisitor::PathContext pc, HIRPath& p) override {
                    exp.visitPath(p, pc);
                }

                void visitGenericPath(HIRVisitor::PathContext pc, HIRGenericPath& p) override {
                    // `visit_path_params` relies on `m_get_params` being set by the enclosing path visitor; without this override a generic path in an expression reaches it empty.
                    exp.visitGenericPath(p, pc);
                }

                void visit(HIRExprNodeCallMethod& node) override {
                    auto saved = exp.getParams;
                    auto savedUnresolved = exp.paramsUnresolved;
                    exp.getParams = nullptr;
                    exp.paramsUnresolved = true;
                    HIRExprVisitorDef::visit(node);
                    exp.getParams = saved;
                    exp.paramsUnresolved = savedUnresolved;
                }

                void visit(HIRExprNodeArraySized& node) override {
                    HIRExprVisitorDef::visit(node);
                    exp.visitArraysize(node.size);
                }
            };

            if (expr.get() != nullptr) {
                Visitor v{*this};
                (*expr).visit(v);
            }
        }

        static bool enumTagIsSigned(HIRCoreType ty) {
            switch (ty) {
                case HIRCoreType::I8:
                case HIRCoreType::I16:
                case HIRCoreType::I32:
                case HIRCoreType::I64:
                case HIRCoreType::Isize:
                case HIRCoreType::I128: // TODO: Emulation
                    return true;
                case HIRCoreType::Bool:
                case HIRCoreType::U8:
                case HIRCoreType::U16:
                case HIRCoreType::U32:
                case HIRCoreType::U64:
                case HIRCoreType::Usize:
                case HIRCoreType::Char:
                case HIRCoreType::U128: // TODO: Emulation
                    return false;
                case HIRCoreType::F16:
                case HIRCoreType::F32:
                case HIRCoreType::F64:
                case HIRCoreType::F128:
                    TODO(Span(), "Floating point enum tag.");
                    break;
                case HIRCoreType::Str:
                    BUG(Span(), "Unsized tag?!");
            }
            return false;
        }

        /// The chain of variant evaluations currently on the stack. A variant
        /// expression that casts another variant of the same enum re-enters
        /// evaluation through the MIR constant fold; finding the target
        /// already in this chain is a cycle.
        struct ActiveDiscriminant {
            const HIREnum* enm;
            size_t idx;
            const ActiveDiscriminant* prev;
        };
        static inline const ActiveDiscriminant* sActiveDiscriminants = nullptr;

        /// Compute one variant's discriminant, following its dependencies:
        /// the previous variant for an auto value, any variant its expression
        /// casts through the constant fold.
        static void visitEnumVariant(const WireBoard& wb, const HIRCrate& crate, const HIRItemPath& p, const HIRModule& mod, const HIRItemPath& modPath, const char* name, HIREnum& item, size_t idx) {
            if (item.discriminantsEvaluated) {
                return;
            }
            RcString varName;
            const HIRExprPtr* expr = nullptr;
            U128* slot = nullptr;
            bool* known = nullptr;
            switch (item.data.tag()) {
                case HIREnumClass::TAG_Value: {
                    auto& var = item.data.as_Value().variants.at(idx);
                    varName = var.name;
                    expr = &var.expr;
                    slot = &var.val;
                    known = &var.valueKnown;
                    break;
                }
                case HIREnumClass::TAG_Data: {
                    auto& var = item.data.as_Data().at(idx);
                    varName = var.name;
                    expr = &var.discriminantExpr;
                    slot = &var.discriminantValue;
                    known = &var.valueKnown;
                    break;
                }
            }
            if (*known) {
                return;
            }
            for (const auto* active = sActiveDiscriminants; active; active = active->prev) {
                if (active->enm == &item && active->idx == idx) {
                    ERROR(*expr ? (*expr)->span() : Span(), E0000, "cycle detected when evaluating discriminant of `" << p << "::" << varName << "`");
                }
            }
            ActiveDiscriminant node{&item, idx, sActiveDiscriminants};
            sActiveDiscriminants = &node;
            STD_DEFER {
                sActiveDiscriminants = node.prev;
            };

            const auto ty = HIREnum::getReprType(item.tagRepr);
            U128 value(0);
            if (*expr) {
                auto nvs = NewvalState{mod, modPath, FMT(name << "#" << varName << "_")};
                auto eval = HIREvaluator{(*expr)->span(), wb, nvs};
                eval.resolve.setImplGenericsRaw(MetadataType::None, item.params);
                try {
                    auto val = eval.evaluateConstant(p, *expr, crate.types.primitive(ty));
                    DEBUG("enum variant: " << p << "::" << varName << " = " << val);
                    if (enumTagIsSigned(ty)) {
                        value = EncodedLiteralSlice(val).readSint().getInner();
                    } else {
                        value = EncodedLiteralSlice(val).readUint();
                    }
                } catch (const Defer& e) {
                    BUG((*expr)->span(), "Defer(" << e.reason << ") during evaluation of enum discriminant");
                }
            } else if (idx > 0) {
                visitEnumVariant(wb, crate, p, mod, modPath, name, item, idx - 1);
                value = (item.data.is_Value() ? item.data.as_Value().variants.at(idx - 1).val : item.data.as_Data().at(idx - 1).discriminantValue) + 1;
                DEBUG("enum variant: " << p << "::" << varName << " = " << value << " (auto)");
            }
            *slot = value;
            *known = true;
        }

        static void visitEnumInner(const WireBoard& wb, const HIRCrate& crate, const HIRItemPath& p, const HIRModule& mod, const HIRItemPath& modPath, const char* name, HIREnum& item) {
            if (item.discriminantsEvaluated) {
                return;
            }
            const auto ty = HIREnum::getReprType(item.tagRepr);
            switch (item.data.tag()) {
                case HIREnumClass::TAG_Value: {
                    auto& e = item.data.as_Value();
                    for (size_t idx = 0; idx < e.variants.size(); idx++) {
                        visitEnumVariant(wb, crate, p, mod, modPath, name, item, idx);
                    }
                    checkEnumDiscriminants(Span(), ty, e.variants, [](const auto& var) { return var.val; });
                    break;
                }
                case HIREnumClass::TAG_Data: {
                    auto& e = item.data.as_Data();
                    for (size_t idx = 0; idx < e.size(); idx++) {
                        visitEnumVariant(wb, crate, p, mod, modPath, name, item, idx);
                    }
                    checkEnumDiscriminants(Span(), ty, e, [](const auto& var) { return var.discriminantValue; });
                    break;
                }
            }
            item.discriminantsEvaluated = true;
        }
    };

    class ExpanderApply: public HIRVisitor {
        stl::ObjPool& pool_;

    public:
        ExpanderApply(stl::ObjPool& pool, HIRTypeInterner& types)
            : HIRVisitor(nullptr, types)
            , pool_(pool)
        {
        }

        void visitModule(HIRItemPath p, HIRModule& mod) override {
            if (!mod.inlineStatics.empty()) {
                for (auto& v : mod.inlineStatics) {
                    // ::std::unique_ptr<VisEnt<ValueItem>>
                    auto* iv = pool_.make<HIRVisEnt<HIRValueItem>>(HIRVisEnt<HIRValueItem>{HIRPublicity::newNone(), HIRValueItem::make_Static(pool_.make<HIRStatic>(mv$(*v.second)))});
                    mod.valueItems.insert(::std::make_pair(v.first, iv));
                }
                mod.inlineStatics.clear();
            }

            HIRVisitor::visitModule(p, mod);
        }
    };

    void ConvertHIRConstantEvaluateStatic(const WireBoard& wb, const HIRCrate& crate, const HIRGenericParams* implParams, const HIRItemPath& ip, HIRStatic& e) {
        Expander exp{wb};
        exp.implParams = implParams;
        exp.visitStatic(ip, e);
    }

    void ConvertHIRConstantEvaluateFcnSig(const WireBoard& wb, const HIRCrate& crate, const HIRGenericParams* implParams, const HIRItemPath& ip, HIRFunction& fcn) {
        Expander exp{wb};
        exp.implParams = implParams;
        exp.visitFunction(ip, fcn);
    }
} // namespace

namespace {
    // Discriminant values must be known before anything else is evaluated: an array size in
    // an early module can cast a variant of an enum that the main pass visits later.
    struct EnumValueExpander: public HIRVisitor {
        const WireBoard& wb;
        const HIRCrate& crate;
        TypeckModuleState typeck;
        const HIRModule* mod;
        const HIRItemPath* modPath;

        EnumValueExpander(const WireBoard& wb)
            : HIRVisitor(nullptr, wb.crate->types)
            , wb(wb)
            , crate(*wb.crate)
            , typeck(wb)
            , mod(nullptr)
            , modPath(nullptr)
        {
        }

        void visitModule(HIRItemPath p, HIRModule& mod) override {
            auto savedMp = modPath;
            auto savedM = this->mod;
            this->mod = &mod;
            modPath = &p;
            typeck.pushTraits(p, mod);
            HIRVisitor::visitModule(p, mod);
            typeck.popTraits(mod);
            this->mod = savedM;
            modPath = savedMp;
        }

        void visitEnum(HIRItemPath p, HIREnum& item) override {
            // Enum discriminants are evaluated before the regular expression
            // typecheck pass. Give their literals and primitive operators the
            // enum repr type now, so CTFE/MIR never sees a defaulted i32 where
            // it was asked to produce (for example) a u64.
            auto _ = typeck.setImplGenerics(item.params);
            if (auto* e = item.data.opt_Value()) {
                auto enumType = crate.types.primitive(HIREnum::getReprType(item.tagRepr));
                for (auto& var : e->variants) {
                    if (var.expr) {
                        tArgs args;
                        TypecheckCode(typeck, args, enumType, var.expr);
                    }
                }
            }
            Expander::visitEnumInner(wb, crate, p, *mod, *modPath, p.getName(), item);
        }
    };
}

void ConvertHIRConstantEvaluate(const WireBoard& wb, HIRCrate& crate) {
    EnumValueExpander{wb}.visitCrate(crate);

    Expander exp{wb};
    exp.visitCrate(crate);
    exp.pass = Expander::Pass::Values;
    exp.visitCrate(crate);

    ExpanderApply(*crate.pool, crate.types).visitCrate(crate);
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

void ConvertHIRConstantEvaluateExpr(const WireBoard& wb, const HIRCrate& crate, const HIRItemPath& ip, HIRExprPtr& exprPtr) {
    TRACE_FUNCTION_F(ip);
    // Check innards but NOT the value
    Expander exp{wb};
    exp.visitExpr(exprPtr);
}

void ConvertHIRConstantEvaluateEnum(const WireBoard& wb, const HIRCrate& crate, const HIRItemPath& ip, const HIREnum& enm) {
    auto modPath = ip.getSimplePath();
    auto itemName = modPath.popComponent();
    const auto& mod = crate.getModByPath(Span(), modPath);

    auto& item = const_cast<HIREnum&>(enm);

    Expander::visitEnumInner(wb, crate, ip, mod, modPath, itemName.c_str(), item);
}

void ConvertHIRConstantEvaluateEnumVariant(const WireBoard& wb, const HIRCrate& crate, const HIRItemPath& ip, const HIREnum& enm, size_t idx) {
    auto modPath = ip.getSimplePath();
    auto itemName = modPath.popComponent();
    const auto& mod = crate.getModByPath(Span(), modPath);

    auto& item = const_cast<HIREnum&>(enm);

    Expander::visitEnumVariant(wb, crate, ip, mod, modPath, itemName.c_str(), item, idx);
}

void ConvertHIRConstantEvaluateConstant(const StaticTraitResolve& callerResolve, const HIRGenericParams* implParams, const HIRItemPath& ip, HIRConstant& e) {
    Expander exp{callerResolve.board()};
    exp.pass = Expander::Pass::Values;
    exp.implParams = implParams;
    exp.visitConstant(ip, e);

    const auto path = ip.getFullPath();
    if (e.valueState != HIRConstant::ValueState::Generic || e.monomorphCache.count(path)) {
        return;
    }

    StaticTraitResolve resolve(callerResolve.board());
    resolve.setBothGenericsRaw(callerResolve.implGenericsPtr(), callerResolve.itemGenericsPtr());
    const auto& crate = callerResolve.hirCrate();
    MonomorphState constMs(crate.types);
    const HIRGenericParams* resolvedImplParams = nullptr;
    auto value = resolve.getValue(e.value.span(), path, constMs, false, &resolvedImplParams);
    const auto* constant = value.opt_Constant();
    ASSERT_BUG(e.value.span(), constant && *constant == &e, "Resolved a different constant for " << path);

    HIRItemPath modIp{e.value.state->modPath};
    auto nvs = NewvalState(e.value.state->module, modIp, FMT("const" << &e << "#"));
    auto eval = HIREvaluator(e.value.span(), callerResolve.board(), nvs);
    eval.resolve.setBothGenericsRaw(resolvedImplParams, &e.params);
    auto type = constMs.monomorphType(e.value.span(), e.type);
    auto literal = eval.evaluateConstant(HIRItemPath(path), e.value, std::move(type), std::move(constMs));
    e.monomorphCache.emplace(path.clone(), std::move(literal));
}

void ConvertHIRConstantEvaluateConstGeneric(const Span& sp, const WireBoard& wb, const HIRCrate& crate, const HIRTypeData* ty, HIRConstGeneric& cg) {
    if (auto* cgeP = cg.opt_Unevaluated()) {
        const auto& cge = *cgeP;
        const bool predicted = unevaluatedUsedSlotsAreConcrete(crate.types, *cge);
        if (!predicted && !sCapsOracle) {
            translateConstExprBody(sp, wb, crate, ty, *cge);
            return;
        }
        if (!predicted) {
            fprintf(stderr, "PREDICATE ATTEMPT [ConvertConstGeneric4]\n");
        }
        try {
            cg = freezeEncodedLiteral(evaluateConstgeneric(sp, wb, crate, ty, *cge));
            if (!predicted) {
                fprintf(stderr, "PREDICATE MISS [ConvertConstGeneric4]\n");
            }
        } catch (const Defer&) {
            // Deferred - no update
        }
    }
}

void ConvertHIRConstantEvaluateConstGeneric(const Span& sp, const WireBoard& wb, const HIRCrate& crate, HIRConstGeneric& cg) {
    if (const auto* value = cg.opt_Unevaluated()) {
        const auto& expr = *(*value)->expr;
        MonomorphState ms(crate.types);
        ms.selfTy = (*value)->selfType;
        ms.ppImpl = &(*value)->paramsImpl;
        ms.ppMethod = &(*value)->paramsItem;
        auto type = ms.monomorphType(sp, expr->resType);
        if (visitTyWith(type, [](const HIRTypeData* t) {
            return t->is_Infer();
        })) {
            return;
        }
        ConvertHIRConstantEvaluateConstGeneric(sp, wb, crate, type, cg);
    }
}

void ConvertHIRConstantEvaluateArraySize(const Span& sp, const WireBoard& wb, const HIRCrate& crate, const HIRSimplePath& path, HIRArraySize& size) {
    if (auto* se = size.opt_Unevaluated()) {
        if (se->is_Unevaluated()) {
            ConvertHIRConstantEvaluateConstGeneric(sp, wb, crate, crate.types.primitive(HIRCoreType::Usize), *se);
        }
        if (const auto* e = se->opt_Evaluated()) {
            size = (*e)->readUsize(0);
        }
    }
}

void ConvertHIRConstantEvaluateMethodParams(const Span& sp, const WireBoard& wb, const HIRCrate& crate, const HIRGenericParams* paramsDef, HIRPathParams& params) {
    for (auto& v : params.values) {
        if (v.is_Unevaluated()) {
            const auto& ue = *v.as_Unevaluated();

            // Need to look up the required type - to do that requires knowing the item it's for
            // - Which, might not be known at this point - might be a UfcsInherent
            const bool predicted = unevaluatedUsedSlotsAreConcrete(crate.types, ue);
            if (!predicted && !sCapsOracle) {
                translateConstExprBody(sp, wb, crate, nullptr, ue);
                continue;
            }
            try {

                ASSERT_BUG(sp, paramsDef, "Missing generic parameter definitions for " << params);
                auto idx = static_cast<size_t>(&v - &params.values.front());
                ASSERT_BUG(sp, idx < paramsDef->values.size(), "");
                const HIRTypeData* ty = paramsDef->values[idx].type;
                HIRTypeRef tmp;
                if (monomorphiseTypeNeeded(ty)) {
                    // A const parameter's type may name the parameters before it
                    // (`const M: [u8; N]`), and those are already known here. The
                    // list fills both slots because a definition indexes its own
                    // parameters as `I:n` or as `M:n` depending on the item.
                    MonomorphStatePtr ms(crate.types, nullptr, &params, &params);
                    ty = tmp = ms.monomorphType(sp, ty);
                    ASSERT_BUG(sp, !monomorphiseTypeNeeded(ty), "" << ty);
                }
                v = HIRConstGeneric::make_Evaluated(freezeEncodedLiteral(evaluateConstgeneric(sp, wb, crate, ty, ue)));
                if (!predicted) {
                    fprintf(stderr, "PREDICATE MISS [MethodParams]\n");
                }
            } catch (const Defer&) {
                // Deferred - no update
            }
        }
    }
}

HIREvaluator::CsePtr::CsePtr(MIREvalCallStackEntry* ptr)
    : inner(ptr)
{
}

HIREvaluator::CsePtr::CsePtr(CsePtr&& x)
    : inner(x.inner)
{
    x.inner = nullptr;
}

HIREvaluator::CsePtr& HIREvaluator::CsePtr::operator=(CsePtr&& x) {
    this->~CsePtr();
    this->inner = x.inner;
    x.inner = nullptr;
    return *this;
}

HIREvaluator::HIREvaluator(const Span& sp, const WireBoard& wb, Newval& nvs)
    : rootSpan(sp)
    , valuePool(stl::ObjPool::fromMemory())
    , resolve(wb)
    , nvs(nvs)
    , evalIndex(sNextEvalIndex++)
    , numFrames(0)
    , requireConstCalls(false)
{
}

// Bodies of the generated local unions (see hir_conv_ent_ptr.tu).
#include "hir_conv_ent_ptr_tu.cpp"
