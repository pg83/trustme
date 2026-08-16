#pragma once

#include "floats.h"
#include "int128.h"
#include "hir_asm.h"
#include "hir_encoded_literal.h"
#include "hir_type.h"
#include "tagged_union.h"

#include <memory> // std::unique_ptr
#include <string>
#include <vector>
#include <cstdint>

struct MonomorphState;
class StaticTraitResolve;
class MonomorphiserNop;

typedef unsigned int MIRRegionId;
typedef unsigned int MIRBasicBlockId;

// Store LValues as:
// - A packed root value (one word, using the low bits as an enum descriminator)
// - A list of (inner to outer) wrappers
struct MIRLValue {
    class Storage {
    public:
        const static uintptr_t MAX_ARG = (1 << 30) - 1; // max value of 30 bits
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
            TAGDEAD,
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
        uint32_t val;

        Wrapper(uint32_t v);

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

        uint32_t getInner() const {
            return val;
        }

        static Wrapper fromInner(uint32_t v) {
            return Wrapper(v);
        }

        enum Tag {
            TAG_Deref,
            TAG_Field,
            TAG_Downcast,
            TAG_Index,
            TAGDEAD,
        };

        Tag tag() const {
            return static_cast<Tag>(val & 3);
        }

        bool is_Deref() const {
            return (val & 3) == 0;
        }

        // Stores the field index
        bool is_Field() const {
            return (val & 3) == 1;
        }

        // Stores the variant index
        bool is_Downcast() const {
            return (val & 3) == 2;
        }

        // Stores a Local index
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
    ::std::vector<Wrapper> wrappers;

    MIRLValue();

    MIRLValue(Storage root, ::std::vector<Wrapper> wrappers);

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
        return MIRLValue(Storage::newStatic(::std::move(p)), {});
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

    MIRLValue cloneWrapped(::std::vector<Wrapper> wrappers) const;

    template <typename It>
    MIRLValue cloneWrapped(It beginIt, It endIt) const {
        ::std::vector<Wrapper> newWrappers;
        newWrappers.reserve(wrappers.size() + ::std::distance(beginIt, endIt));
        newWrappers.insert(newWrappers.end(), wrappers.begin(), wrappers.end());
        newWrappers.insert(newWrappers.end(), beginIt, endIt);
        return MIRLValue(root.clone(), ::std::move(newWrappers));
    }

    MIRLValue cloneUnwrapped(unsigned count = 1) const;

    // Returns true if this LValue is a subset of the other (e.g. `_1.0` is a subset of `_1.0*`)
    bool isSubsetOf(const MIRLValue& other) const {
        return root == other.root && other.wrappers.size() >= wrappers.size() && std::equal(wrappers.begin(), wrappers.end(), other.wrappers.begin());
    }

    // Returns true if one lvalue is a subset of the other
    // - Equivalent to `a.is_subset_of(b) || b.is_subset_of(a)` (but more efficient)
    bool isEitherSubset(const MIRLValue& other) const;

    /// Helper class that represents a LValue unwrapped to a certain degree
    class RefCommon {
    protected:
        const MIRLValue* lv_;
        size_t wrapperCount_;

        RefCommon(const MIRLValue& lv, size_t wrapperCount);

    public:
        MIRLValue clone() const {
            return MIRLValue(lv_->root.clone(), ::std::vector<Wrapper>(lv_->wrappers.begin(), lv_->wrappers.begin() + wrapperCount_));
        }

        const MIRLValue& lv() const {
            return *lv_;
        }

        size_t wrapperCount() const {
            return wrapperCount_;
        }

        /// Unwrap one level, returning false if already at the root
        bool tryUnwrap();

        enum Tag {
            TAGDEAD,
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

        void fmt(::std::ostream& os) const;
        Ordering ord(const RefCommon& b) const;
    };

    class CRef: public RefCommon {
    public:
        CRef(const MIRLValue& lv);

        CRef(const MIRLValue& lv, size_t wc);

        /// Unwrap one level
        const CRef innerRef() const;

        friend ::std::ostream& operator<<(::std::ostream& os, const CRef& x);

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

        friend ::std::ostream& operator<<(::std::ostream& os, const MRef& x);
    };

    Ordering ord(const MIRLValue::CRef& x) const;
    Ordering ord(const MIRLValue::MRef& x) const;
};

extern ::std::ostream& operator<<(::std::ostream& os, const MIRLValue& x);
extern ::std::ostream& operator<<(::std::ostream& os, const MIRLValue::Storage& x);
extern ::std::ostream& operator<<(::std::ostream& os, const MIRLValue::Wrapper& x);

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
    MOD, // MOD_OV,

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

// A compile-time pointer keeps allocation provenance separate from its byte
// offset.  The path names the allocation; `offset` selects an address within
// it.  A null path is reserved for the unresolved MakeDst metadata marker.
struct ItemAddress {
    ::std::unique_ptr<HIRPath> p;
    U128 offset;

    ItemAddress(::std::unique_ptr<HIRPath> p = {}, U128 offset = U128(0));

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

// Compile-time known values
TAGGED_UNION_EX(
    MIRConstant,
    (),
    Int,
    ((Int,
      struct {
          S128 v;
          HIRCoreType t;
      }),
     (Uint,
      struct {
          U128 v;
          HIRCoreType t;
      }),
     (Float,
      struct {
          FloatValue v;
          HIRCoreType t;
      }),
     (Bool,
      struct {
          bool v; // NOTE: Defensive to prevent implicit casts
      }),
     (Bytes, ::std::vector<::std::uint8_t>), // Byte string
     (StaticString, ::std::string),          // String
     // NOTE: These are behind pointers to save inline space (HIR::Path is ~11
     // words, compared to 4 for MIR::Constant without it)
     (Const, struct { ::std::unique_ptr<HIRPath> p; }), // `const`
     (Generic, HIRGenericRef),
     // ZST function type, NOT its address
     (Function, struct { ::std::unique_ptr<HIRPath> p; }),
     // Address within a named allocation
     (ItemAddr, ItemAddress),
     (Encoded,
      struct {
          HIRTypeRef type;
          EncodedLiteral value;
      })),
    (),
    (),
    (friend ::std::ostream & operator<<(::std::ostream& os, const MIRConstant& v); ::Ordering ord(const MIRConstant& b) const; inline bool operator==(const MIRConstant& b) const { return ord(b) == ::OrdEqual; } inline bool operator!=(const MIRConstant& b) const { return ord(b) != ::OrdEqual; } inline bool operator<(const MIRConstant& b) const { return ord(b) == ::OrdLess; } inline bool operator<=(const MIRConstant& b) const { return ord(b) != ::OrdGreater; } inline bool operator>(const MIRConstant& b) const { return ord(b) == ::OrdGreater; } inline bool operator>=(const MIRConstant& b) const { return ord(b) != ::OrdLess; } MIRConstant clone() const;)
);

/// Parameter - A value used when a rvalue just reads (doesn't require a lvalue)
/// Can be either a lvalue (memory address), or a constant
TAGGED_UNION_EX(
    MIRParam,
    (),
    Constant,
    ((LValue, MIRLValue),
     // TODO: Add `Borrow` here (makes some MIR manipulation more complex, but simplifies emitted code)
     (Borrow,
      struct {
          HIRBorrowType type;
          MIRLValue val;
      }),
     (Constant, MIRConstant)),
    (),
    (),
    (MIRParam clone() const; friend ::std::ostream & operator<<(::std::ostream& os, const MIRParam& v); bool operator==(const MIRParam& b) const; inline bool operator!=(const MIRParam& b) const { return !(*this == b); })
);

TAGGED_UNION_EX(
    MIRRValue,
    (),
    Tuple,
    (
        // TODO: Split "Use" into "Copy" and "Move" (Where 'move' indicates that the source is unused)
        (Use, MIRLValue),
        (Borrow,
         struct {
             HIRBorrowType type;
             bool isRaw;
             MIRLValue val;
         }),
        (Constant, MIRConstant),
        (SizedArray,
         struct {
             MIRParam val;
             HIRArraySize count;
         }),
        // Cast on primitives (thin pointers, integers, floats)
        (Cast,
         struct {
             MIRLValue val;
             HIRTypeRef type;
         }),
        // Binary operation on primitives
        (BinOp,
         struct {
             MIRParam valL;
             MIRBinOp op;
             MIRParam valR;
         }),
        // Unary operation on primitives
        (UniOp,
         struct {
             MIRLValue val; // NOTE: Not a param, because UniOps can be const propagated
             MIRUniOp op;
         }),
        // Extract the metadata from a DST pointer
        // NOTE: If used on an array, this yields the array size (for generics)
        (DstMeta, struct { MIRLValue val; }),
        // Extract the pointer from a DST pointer (as *const ())
        (DstPtr, struct { MIRLValue val; }),
        // Construct a DST pointer from a thin pointer and metadata
        // OR: (if `meta_val` is `Constant::ItemAddr(nullptr)`) A still-to-be-resolved unsizing coercion
        (MakeDst,
         struct {
             MIRParam ptrVal;
             MIRParam metaVal;
         }),
        (Tuple, struct { ::std::vector<MIRParam> vals; }),
        // Array literal
        (Array, struct { ::std::vector<MIRParam> vals; }),
        // Create a new instance of a union
        (UnionVariant,
         struct {
             HIRGenericPath path;
             unsigned int index;
             MIRParam val;
         }),
        // Create a new instance of an enum
        // - Separate from UnionVariant, as the contents is needed when creating the body
        (EnumVariant,
         struct {
             HIRGenericPath path;
             unsigned int index;
             ::std::vector<MIRParam> vals;
         }),
        // Create a new instance of a struct
        (Struct,
         struct {
             HIRGenericPath path;
             ::std::vector<MIRParam> vals;
         })
    ),
    (),
    (),
    (MIRRValue clone() const;)
);
extern ::std::ostream& operator<<(::std::ostream& os, const MIRRValue& x);
extern bool operator==(const MIRRValue& a, const MIRRValue& b);

static inline bool operator!=(const MIRRValue& a, const MIRRValue& b) {
    return !(a == b);
}

TAGGED_UNION(MIRCallTarget, Intrinsic, (Value, MIRLValue), (Path, HIRPath), (Intrinsic, struct {
                 RcString name;
                 HIRPathParams params;
             }));
TAGGED_UNION_EX(MIRSwitchValues, (), Unsigned, ((Unsigned, ::std::vector<uint64_t>), (Signed, ::std::vector<int64_t>), (String, ::std::vector<::std::string>), (ByteString, ::std::vector<::std::vector<uint8_t>>)), (), (), (MIRSwitchValues clone() const; bool operator==(const MIRSwitchValues& x) const; bool operator!=(const MIRSwitchValues& x) const { return !(*this == x); }));

TAGGED_UNION(MIRUnwindAction, Continue, (Continue, struct {}), (Cleanup, MIRBasicBlockId), (Terminate, struct {}), (Unreachable, struct {}));

enum class MIRDropKind {
    SHALLOW,
    DEEP,
};

TAGGED_UNION(MIRAsmParam, Const, (Const, MIRConstant), (Sym, HIRPath), (Reg, struct {
                 AsmDirection dir;
                 AsmRegisterSpec spec;
                 std::unique_ptr<MIRParam> input;
                 std::unique_ptr<MIRLValue> output;
             }), (Label, MIRBasicBlockId));
extern bool operator==(const MIRAsmParam& a, const MIRAsmParam& b);

TAGGED_UNION(
    MIRTerminator,
    Incomplete,
    (Incomplete, struct {}),      // Block isn't complete (ERROR in output)
    (Return, struct {}),          // Return clealy to caller
    (UnwindResume, struct {}),    // Resume the currently caught exception
    (UnwindTerminate, struct {}), // Abort if unwinding reaches this point
    (Unreachable, struct {}),     // This control-flow edge cannot be reached
    (Goto, MIRBasicBlockId),      // Jump to another block
    (If,
     struct {
         MIRLValue cond;
         MIRBasicBlockId bbTrue;
         MIRBasicBlockId bbFalse;
     }),
    (Switch,
     struct {
         MIRLValue val;
         ::std::vector<MIRBasicBlockId> targets;
         unsigned int validFlag = ~0u;
         MIRBasicBlockId invalidTarget = ~0u;
     }),
    (SwitchValue,
     struct {
         MIRLValue val;
         MIRBasicBlockId defTarget;
         ::std::vector<MIRBasicBlockId> targets;
         MIRSwitchValues values;
     }),
    (Drop,
     struct {
         MIRDropKind kind;
         MIRLValue slot;
         unsigned int flagIdx;
         MIRBasicBlockId target;
         MIRUnwindAction unwind;
     }),
    (Call, struct {
        MIRBasicBlockId retBlock;
        MIRUnwindAction unwind;
        MIRLValue retVal;
        MIRCallTarget fcn;
        ::std::vector<MIRParam> args;
        SourceLocation source;
        bool tracksCaller = false;
    }),
    (TailCall, struct {
        MIRCallTarget fcn;
        ::std::vector<MIRParam> args;
        SourceLocation source;
        bool tracksCaller = false;
    }),
    // Inline assembly with label operands. Unlike statement-form Asm2, this is
    // a terminator because the assembly can branch to any Label parameter.
    (Asm2, struct {
        AsmOptions options;
        std::vector<AsmLine> lines;
        ::std::vector<MIRAsmParam> params;
        MIRBasicBlockId retBlock;
    })
);
extern ::std::ostream& operator<<(::std::ostream& os, const MIRTerminator& x);
extern bool operator==(const MIRTerminator& a, const MIRTerminator& b);

static inline bool operator!=(const MIRTerminator& a, const MIRTerminator& b) {
    return !(a == b);
}

TAGGED_UNION(
    MIRStatement,
    Asm,
    // Value assigment
    (Assign,
     struct {
         MIRLValue dst;
         MIRRValue src;
     }),
    // Inline assembly (`llvm_asm!`)
    (Asm,
     struct {
         ::std::string tpl;
         ::std::vector<::std::pair<::std::string, MIRLValue>> outputs;
         ::std::vector<::std::pair<::std::string, MIRLValue>> inputs;
         ::std::vector<::std::string> clobbers;
         ::std::vector<::std::string> flags;
     }),
    // Inline assembly (stabilised)
    (Asm2,
     struct {
         AsmOptions options;
         std::vector<AsmLine> lines;
         ::std::vector<MIRAsmParam> params;
     }),
    // Update the state of a drop flag
    (SetDropFlag,
     struct {
         unsigned int idx;
         bool newVal; // If `other` is populated, this indicates that the other value should be negated
         /// Other drop flag, used for copying/inverting another flag. If `~0u`, then the value of `new_val` is stored
         unsigned int other;
     }),
    // Save a drop flag to a bitset
    (SaveDropFlag,
     struct {
         /// Destination bit-set, an array of unsigned integers (nominally `u8`)
         MIRLValue slot;
         /// Destination bit v
         unsigned int bitIndex;
         /// Source drop flag index
         unsigned int idx;
     }),
    (LoadDropFlag,
     struct {
         /// Destination drop flag index
         unsigned int idx;
         /// Source bit-set, an array of unsigned integers (nominally `u8`)
         MIRLValue slot;
         /// Source bit index
         unsigned int bitIndex;
     }),
    (ScopeEnd, struct { ::std::vector<unsigned> slots; })
);
extern ::std::ostream& operator<<(::std::ostream& os, const MIRStatement& x);
extern bool operator==(const MIRStatement& a, const MIRStatement& b);

static inline bool operator!=(const MIRStatement& a, const MIRStatement& b) {
    return !(a == b);
}

struct MIRBasicBlock {
    ::std::vector<MIRStatement> statements;
    MIRTerminator terminator;
    bool isCleanup = false;
};

struct MIREnumCache; // Defined in trans/enumerate.cpp

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

class MIRFunction {
public:
    ::std::vector<HIRTypeRef> locals;
    //::std::vector< RcString>   local_names;
    ::std::vector<bool> dropFlags;

    ::std::vector<MIRBasicBlock> blocks;

    // Cache filled/used by enumerate
    mutable MIREnumCachePtr transEnumState;
};

class MIRCloner {
    ::std::unique_ptr<MonomorphiserNop> nop;

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

    ::std::vector<MIRAsmParam> cloneAsmParams(const ::std::vector<MIRAsmParam>& params) const;
    ::std::vector<::std::pair<::std::string, MIRLValue>> cloneNameLvalVec(const ::std::vector<::std::pair<::std::string, MIRLValue>>& src) const;
    ::std::vector<MIRParam> cloneParamVec(const ::std::vector<MIRParam>& src) const;
    ::std::vector<MIRLValue> cloneLvalVec(const ::std::vector<MIRLValue>& src) const;

    // -- Monomorphise various types
    HIRTypeRef monomorph(const HIRTypeData* x) const;
    HIRGenericPath monomorph(const HIRGenericPath& x) const;
    HIRPath monomorph(const HIRPath& x) const;
    HIRPathParams monomorph(const HIRPathParams& x) const;
};
