#pragma once

#include "floats.h"
#include "int128.h"
#include "output.h"
#include "hir_asm.h"
#include "hir_type.h"
#include "hir_encoded_literal.h"

#include <std/lib/vector.h>

#include <memory>
#include <string>
#include <vector>
#include <cstdint>

struct MonomorphState;
class StaticTraitResolve;
class MonomorphiserNop;

typedef unsigned int MIRRegionId;
typedef unsigned int MIRBasicBlockId;

struct MIRLValue {
    class Storage {
    public:
        const static uintptr_t MAX_ARG = (1 << 30) - 1;

    private:
        uintptr_t val;

        Storage(uintptr_t v);

    public:
        Storage(const Storage&) = delete;
        Storage& operator=(const Storage&) = delete;

        Storage(Storage&& x);

        Storage& operator=(Storage&& x);

        ~Storage();

        static Storage newReturn() {
            return Storage(0 << 2);
        }

        static Storage newArgument(unsigned idx);

        static Storage newLocal(unsigned idx);

        static Storage newStatic(HIRPath p);

        Storage clone() const;

        uintptr_t getInner() const;

        static Storage fromInner(uintptr_t v);

        enum Tag {
            TAG_Argument,
            TAG_Local,
            TAG_Static,
            TAG_Return,
        };

        Tag tag() const;

        bool is_Return() const {
            return val == 0;
        }

        bool is_Argument() const {
            return val != 0 && (val & 3) == 0;
        }

        bool is_Local() const {
            return (val & 3) == 1;
        }

        bool is_Static() const {
            return (val & 3) == 2;
        }

        char as_Return() const;

        unsigned as_Argument() const;

        unsigned as_Local() const;

        const HIRPath& as_Static() const;

        HIRPath& as_Static();

        Ordering ord(const Storage& x) const;

        bool operator==(const Storage& x) const {
            return this->ord(x) == OrdEqual;
        }

        bool operator!=(const Storage& x) const {
            return this->ord(x) != OrdEqual;
        }
    };

    class Wrapper {
        u32 val;

        Wrapper(u32 v);

    public:
        static Wrapper newDeref() {
            return Wrapper(0);
        }

        static Wrapper newField(unsigned idx) {
            return Wrapper((idx << 2) | 1);
        }

        static Wrapper newDowncast(unsigned idx) {
            return Wrapper((idx << 2) | 2);
        }

        static Wrapper newIndex(unsigned idx);

        u32 getInner() const {
            return val;
        }

        static Wrapper fromInner(u32 v) {
            return Wrapper(v);
        }

        enum Tag {
            TAG_Deref,
            TAG_Field,
            TAG_Downcast,
            TAG_Index,
        };

        Tag tag() const {
            return static_cast<Tag>(val & 3);
        }

        bool is_Deref() const {
            return (val & 3) == 0;
        }

        bool is_Field() const {
            return (val & 3) == 1;
        }

        bool is_Downcast() const {
            return (val & 3) == 2;
        }

        bool is_Index() const {
            return (val & 3) == 3;
        }

        char as_Deref() const;

        unsigned as_Field() const;

        unsigned as_Downcast() const;

        // TODO: Should this return a LValue?
        unsigned as_Index() const;

        void incField();

        void incDowncast();

        Ordering ord(const Wrapper& x) const {
            return ::ord(val, x.val);
        }

        bool operator==(const Wrapper& x) const {
            return val == x.val;
        }

        bool operator!=(const Wrapper& x) const {
            return val != x.val;
        }
    };

    Storage root;
    std::vector<Wrapper> wrappers;

    MIRLValue();

    MIRLValue(Storage root, std::vector<Wrapper> wrappers);

    static MIRLValue newReturn() {
        return MIRLValue(Storage::newReturn(), {});
    }

    static MIRLValue newArgument(unsigned idx) {
        return MIRLValue(Storage::newArgument(idx), {});
    }

    static MIRLValue newLocal(unsigned idx) {
        return MIRLValue(Storage::newLocal(idx), {});
    }

    static MIRLValue newStatic(HIRPath p) {
        return MIRLValue(Storage::newStatic(std::move(p)), {});
    }

    static MIRLValue newDeref(MIRLValue lv);

    static MIRLValue newField(MIRLValue lv, unsigned idx);

    static MIRLValue newDowncast(MIRLValue lv, unsigned idx);

    static MIRLValue newIndex(MIRLValue lv, unsigned localIdx);

    bool is_Return() const {
        return wrappers.empty() && root.is_Return();
    }

    bool is_Local() const {
        return wrappers.empty() && root.is_Local();
    }

    unsigned as_Local() const;

    bool is_Deref() const {
        return wrappers.size() > 0 && wrappers.back().is_Deref();
    }

    bool is_Field() const {
        return wrappers.size() > 0 && wrappers.back().is_Field();
    }

    bool is_Downcast() const {
        return wrappers.size() > 0 && wrappers.back().is_Downcast();
    }

    unsigned as_Field() const;

    void incField();

    void incDowncast();

    Ordering ord(const MIRLValue& x) const;

    MIRLValue monomorphise(const MonomorphState& ms, unsigned localOffset = 0);

    MIRLValue clone() const {
        return MIRLValue(root.clone(), wrappers);
    }

    MIRLValue cloneWrapped(std::vector<Wrapper> wrappers) const;

    template <typename It>
    MIRLValue cloneWrapped(It beginIt, It endIt) const {
        std::vector<Wrapper> newWrappers;
        newWrappers.reserve(wrappers.size() + std::distance(beginIt, endIt));
        newWrappers.insert(newWrappers.end(), wrappers.begin(), wrappers.end());
        newWrappers.insert(newWrappers.end(), beginIt, endIt);
        return MIRLValue(root.clone(), std::move(newWrappers));
    }

    MIRLValue cloneUnwrapped(unsigned count = 1) const;

    bool isSubsetOf(const MIRLValue& other) const {
        return root == other.root && other.wrappers.size() >= wrappers.size() && std::equal(wrappers.begin(), wrappers.end(), other.wrappers.begin());
    }

    bool isEitherSubset(const MIRLValue& other) const;

    class RefCommon {
    protected:
        const MIRLValue* lv_;
        size_t wrapperCount_;

        RefCommon(const MIRLValue& lv, size_t wrapperCount);

    public:
        MIRLValue clone() const {
            return MIRLValue(lv_->root.clone(), std::vector<Wrapper>(lv_->wrappers.begin(), lv_->wrappers.begin() + wrapperCount_));
        }

        const MIRLValue& lv() const {
            return *lv_;
        }

        size_t wrapperCount() const {
            return wrapperCount_;
        }

        bool tryUnwrap();

        enum Tag {
            TAG_Return,
            TAG_Argument,
            TAG_Local,
            TAG_Static,
            TAG_Deref,
            TAG_Field,
            TAG_Downcast,
            TAG_Index,
        };

        Tag tag() const;

        bool is_Local() const {
            return wrapperCount_ == 0 && lv_->root.is_Local();
        }

        bool is_Return() const {
            return wrapperCount_ == 0 && lv_->root.is_Return();
        }

        bool is_Argument() const {
            return wrapperCount_ == 0 && lv_->root.is_Argument();
        }

        bool is_Static() const {
            return wrapperCount_ == 0 && lv_->root.is_Static();
        }

        bool is_Deref() const {
            return wrapperCount_ >= 1 && lv_->wrappers[wrapperCount_ - 1].is_Deref();
        }

        bool is_Field() const {
            return wrapperCount_ >= 1 && lv_->wrappers[wrapperCount_ - 1].is_Field();
        }

        bool is_Downcast() const {
            return wrapperCount_ >= 1 && lv_->wrappers[wrapperCount_ - 1].is_Downcast();
        }

        bool is_Index() const {
            return wrapperCount_ >= 1 && lv_->wrappers[wrapperCount_ - 1].is_Index();
        }

        unsigned as_Local() const;

        char as_Return() const;

        unsigned as_Argument() const;

        const HIRPath& as_Static() const;

        char as_Deref() const;

        unsigned as_Field() const;

        unsigned as_Downcast() const;

        unsigned as_Index() const;

        void fmt(stl::ZeroCopyOutput& os) const;
        Ordering ord(const RefCommon& b) const;
    };

    class CRef: public RefCommon {
    public:
        CRef(const MIRLValue& lv);

        CRef(const MIRLValue& lv, size_t wc);

        const CRef innerRef() const;

        bool operator<(const CRef& b) const {
            return this->ord(b) == OrdLess;
        }

        bool operator==(const CRef& b) const {
            return this->ord(b) == OrdEqual;
        }
    };

    class MRef: public RefCommon {
    public:
        MRef(MIRLValue& lv);

        operator CRef() const {
            return CRef(*lv_, wrapperCount_);
        }

        MRef innerRef();

        void replace(MIRLValue x);
    };

    Ordering ord(const MIRLValue::CRef& x) const;
    Ordering ord(const MIRLValue::MRef& x) const;
};

static inline bool operator<(const MIRLValue& a, const MIRLValue::CRef& b) {
    return a.ord(b) == OrdLess;
}

static inline bool operator<(const MIRLValue& a, const MIRLValue::MRef& b) {
    return a.ord(b) == OrdLess;
}

static inline bool operator<(const MIRLValue::CRef& a, const MIRLValue& b) {
    return b.ord(a) == OrdGreater;
}

static inline bool operator<(const MIRLValue::MRef& a, const MIRLValue& b) {
    return b.ord(a) == OrdGreater;
}

static inline bool operator<(const MIRLValue& a, const MIRLValue& b) {
    return a.ord(b) == OrdLess;
}

static inline bool operator==(const MIRLValue& a, const MIRLValue& b) {
    return a.ord(b) == OrdEqual;
}

static inline bool operator!=(const MIRLValue& a, const MIRLValue& b) {
    return !(a == b);
}

enum class MIRBinOp {
    ADD,
    ADD_OV,
    SUB,
    SUB_OV,
    MUL,
    MUL_OV,
    DIV,
    DIV_OV,
    MOD,

    BIT_OR,
    BIT_AND,
    BIT_XOR,

    BIT_SHR,
    BIT_SHL,

    EQ,
    NE,
    GT,
    GE,
    LT,
    LE,
};
enum class MIRUniOp {
    INV,
    NEG
};

struct ItemAddress {
    std::unique_ptr<HIRPath> p;
    U128 offset;

    ItemAddress(std::unique_ptr<HIRPath> p = {}, U128 offset = U128(0));

    explicit operator bool() const {
        return static_cast<bool>(p);
    }

    const HIRPath* get() const {
        return p.get();
    }

    HIRPath* get() {
        return p.get();
    }

    const HIRPath& operator*() const {
        return *p;
    }

    HIRPath& operator*() {
        return *p;
    }

    const HIRPath* operator->() const {
        return p.get();
    }

    HIRPath* operator->() {
        return p.get();
    }

    ItemAddress clone() const {
        return ItemAddress(p ? box$(p->clone()) : nullptr, offset);
    }
};

enum class MIRDropKind {
    SHALLOW,
    DEEP,
};

#include "mir_mir_tu.h"

bool operator==(const MIRRValue& a, const MIRRValue& b);

static inline bool operator!=(const MIRRValue& a, const MIRRValue& b) {
    return !(a == b);
}

bool operator==(const MIRAsmParam& a, const MIRAsmParam& b);

bool operator==(const MIRTerminator& a, const MIRTerminator& b);

static inline bool operator!=(const MIRTerminator& a, const MIRTerminator& b) {
    return !(a == b);
}

bool operator==(const MIRStatement& a, const MIRStatement& b);

static inline bool operator!=(const MIRStatement& a, const MIRStatement& b) {
    return !(a == b);
}

struct MIRBasicBlock {
    std::vector<MIRStatement> statements;
    MIRTerminator terminator;
    bool isCleanup = false;
};

class MIRFunction {
public:
    struct MIREnumCache;

    class MIREnumCachePtr {
        const MIREnumCache* p;

    public:
        MIREnumCachePtr(const MIREnumCache* p = nullptr);

        ~MIREnumCachePtr();

        MIREnumCachePtr(MIREnumCachePtr&& x);

        MIREnumCachePtr& operator=(MIREnumCachePtr&& x);

        operator bool() {
            return p != nullptr;
        }

        const MIREnumCache& operator*() const {
            return *p;
        }

        const MIREnumCache* operator->() const {
            return p;
        }
    };

    stl::Vector<const HIRTypeData*> locals;

    stl::Vector<bool> dropFlags;

    std::vector<MIRBasicBlock> blocks;

    mutable MIREnumCachePtr transEnumState;
};

class MIRCloner {
    std::unique_ptr<MonomorphiserNop> nop;

public:
    const Span& sp;

    MIRCloner(const Span& sp, HIRTypeInterner& types);
    virtual ~MIRCloner();

    virtual MIRBasicBlockId mapBbIdx(MIRBasicBlockId idx) const;

    virtual unsigned mapLocal(unsigned f) const;

    virtual unsigned mapDropFlag(unsigned f) const;

    virtual const HIRTypeData* valueGenericType(HIRGenericRef ce) const;
    virtual const Monomorphiser& monomorphiser() const;

    virtual const StaticTraitResolve* resolve() const;

    virtual MIRStatement cloneStmt(const MIRStatement& src) const;
    virtual MIRTerminator cloneTerm(const MIRTerminator& src) const;

    virtual MIRLValue cloneLval(const MIRLValue& src) const;
    virtual MIRRValue cloneRval(const MIRRValue& src) const;
    virtual MIRParam cloneParam(const MIRParam& src) const;
    virtual MIRConstant cloneConstant(const MIRConstant& src) const;

    std::vector<MIRAsmParam> cloneAsmParams(const std::vector<MIRAsmParam>& params) const;
    std::vector<std::pair<std::string, MIRLValue>> cloneNameLvalVec(const std::vector<std::pair<std::string, MIRLValue>>& src) const;
    std::vector<MIRParam> cloneParamVec(const std::vector<MIRParam>& src) const;
    std::vector<MIRLValue> cloneLvalVec(const std::vector<MIRLValue>& src) const;

    const HIRTypeData* monomorph(const HIRTypeData* x) const;
    HIRGenericPath monomorph(const HIRGenericPath& x) const;
    HIRPath monomorph(const HIRPath& x) const;
    HIRPathParams monomorph(const HIRPathParams& x) const;
};
