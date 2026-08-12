#pragma once

#include "tagged_union.h"
#include <vector>
#include <string>
#include <memory> // std::unique_ptr
#include "hir_type.h"
#include "hir_asm.h"
#include "int128.h"
#include "floats.h"
#include <cstdint>

struct MonomorphState;
class StaticTraitResolve;
class MonomorphiserNop;

namespace MIR {

    typedef unsigned int RegionId;
    typedef unsigned int BasicBlockId;

    // Store LValues as:
    // - A packed root value (one word, using the low bits as an enum descriminator)
    // - A list of (inner to outer) wrappers
    struct LValue {
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

            static Storage newStatic(::HIR::Path p);

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

            const ::HIR::Path& as_Static() const;

            ::HIR::Path& as_Static();

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

        LValue();

        LValue(Storage root, ::std::vector<Wrapper> wrappers);

        static LValue newReturn() {
            return LValue(Storage::newReturn(), {});
        }

        static LValue newArgument(unsigned idx) {
            return LValue(Storage::newArgument(idx), {});
        }

        static LValue newLocal(unsigned idx) {
            return LValue(Storage::newLocal(idx), {});
        }

        static LValue newStatic(::HIR::Path p) {
            return LValue(Storage::newStatic(::std::move(p)), {});
        }

        static LValue newDeref(LValue lv);

        static LValue newField(LValue lv, unsigned idx);

        static LValue newDowncast(LValue lv, unsigned idx);

        static LValue newIndex(LValue lv, unsigned localIdx);

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

        Ordering ord(const LValue& x) const;

        LValue monomorphise(const MonomorphState& ms, unsigned localOffset = 0);

        //LValue monomorphise(const TransParams& ms, unsigned local_offset=0);
        LValue clone() const {
            return LValue(root.clone(), wrappers);
        }

        LValue cloneWrapped(::std::vector<Wrapper> wrappers) const;

        template <typename It>
        LValue cloneWrapped(It beginIt, It endIt) const {
            ::std::vector<Wrapper> newWrappers;
            newWrappers.reserve(wrappers.size() + ::std::distance(beginIt, endIt));
            newWrappers.insert(newWrappers.end(), wrappers.begin(), wrappers.end());
            newWrappers.insert(newWrappers.end(), beginIt, endIt);
            return LValue(root.clone(), ::std::move(newWrappers));
        }

        LValue cloneUnwrapped(unsigned count = 1) const;

        // Returns true if this LValue is a subset of the other (e.g. `_1.0` is a subset of `_1.0*`)
        bool isSubsetOf(const LValue& other) const {
            return root == other.root && other.wrappers.size() >= wrappers.size() && std::equal(wrappers.begin(), wrappers.end(), other.wrappers.begin());
        }

        // Returns true if one lvalue is a subset of the other
        // - Equivalent to `a.is_subset_of(b) || b.is_subset_of(a)` (but more efficient)
        bool isEitherSubset(const LValue& other) const;

        /// Helper class that represents a LValue unwrapped to a certain degree
        class RefCommon {
        protected:
            const LValue* mLv;
            size_t wrapperCount;

            RefCommon(const LValue& lv, size_t wrapper_count);

        public:
            LValue clone() const {
                return ::MIR::LValue(mLv->root.clone(), ::std::vector<Wrapper>(mLv->wrappers.begin(), mLv->wrappers.begin() + wrapperCount));
            }

            const LValue& lv() const {
                return *mLv;
            }

            size_t wrapper_count() const {
                return wrapperCount;
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
                return wrapperCount == 0 && mLv->root.is_Local();
            }

            bool is_Return() const {
                return wrapperCount == 0 && mLv->root.is_Return();
            }

            bool is_Argument() const {
                return wrapperCount == 0 && mLv->root.is_Argument();
            }

            bool is_Static() const {
                return wrapperCount == 0 && mLv->root.is_Static();
            }

            bool is_Deref() const {
                return wrapperCount >= 1 && mLv->wrappers[wrapperCount - 1].is_Deref();
            }

            bool is_Field() const {
                return wrapperCount >= 1 && mLv->wrappers[wrapperCount - 1].is_Field();
            }

            bool is_Downcast() const {
                return wrapperCount >= 1 && mLv->wrappers[wrapperCount - 1].is_Downcast();
            }

            bool is_Index() const {
                return wrapperCount >= 1 && mLv->wrappers[wrapperCount - 1].is_Index();
            }

            unsigned as_Local() const;

            char as_Return() const;

            unsigned as_Argument() const;

            const HIR::Path& as_Static() const;

            char as_Deref() const;

            unsigned as_Field() const;

            unsigned as_Downcast() const;

            unsigned as_Index() const;

            void fmt(::std::ostream& os) const;
            Ordering ord(const RefCommon& b) const;
        };

        class CRef: public RefCommon {
        public:
            CRef(const LValue& lv);

            CRef(const LValue& lv, size_t wc);

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
            MRef(LValue& lv);

            operator CRef() const {
                return CRef(*mLv, wrapperCount);
            }

            MRef innerRef();

            void replace(LValue x);

            friend ::std::ostream& operator<<(::std::ostream& os, const MRef& x);
        };

        Ordering ord(const LValue::CRef& x) const;
        Ordering ord(const LValue::MRef& x) const;
    };

    extern ::std::ostream& operator<<(::std::ostream& os, const LValue& x);
    extern ::std::ostream& operator<<(::std::ostream& os, const LValue::Storage& x);
    extern ::std::ostream& operator<<(::std::ostream& os, const LValue::Wrapper& x);

    static inline bool operator<(const LValue& a, const LValue::CRef& b) {
        return a.ord(b) == OrdLess;
    }

    static inline bool operator<(const LValue& a, const LValue::MRef& b) {
        return a.ord(b) == OrdLess;
    }

    static inline bool operator<(const LValue::CRef& a, const LValue& b) {
        return b.ord(a) == OrdGreater;
    }

    static inline bool operator<(const LValue::MRef& a, const LValue& b) {
        return b.ord(a) == OrdGreater;
    }

    static inline bool operator<(const LValue& a, const LValue& b) {
        return a.ord(b) == OrdLess;
    }

    static inline bool operator==(const LValue& a, const LValue& b) {
        return a.ord(b) == OrdEqual;
    }

    static inline bool operator!=(const LValue& a, const LValue& b) {
        return !(a == b);
    }

    enum class eBinOp {
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
    enum class eUniOp {
        INV,
        NEG
    };

    // A compile-time pointer keeps allocation provenance separate from its byte
    // offset.  The path names the allocation; `offset` selects an address within
    // it.  A null path is reserved for the unresolved MakeDst metadata marker.
    struct ItemAddress {
        ::std::unique_ptr<::HIR::Path> p;
        U128 offset;

        ItemAddress(::std::unique_ptr<::HIR::Path> p = {}, U128 offset = U128(0));

        explicit operator bool() const {
            return static_cast<bool>(p);
        }
        const ::HIR::Path* get() const {
            return p.get();
        }
        ::HIR::Path* get() {
            return p.get();
        }
        const ::HIR::Path& operator*() const {
            return *p;
        }
        ::HIR::Path& operator*() {
            return *p;
        }
        const ::HIR::Path* operator->() const {
            return p.get();
        }
        ::HIR::Path* operator->() {
            return p.get();
        }

        ItemAddress clone() const {
            return ItemAddress(p ? box$(p->clone()) : nullptr, offset);
        }
    };

    // Compile-time known values
    TAGGED_UNION_EX(
        Constant,
        (),
        Int,
        ((Int,
          struct {
              S128 v;
              ::HIR::CoreType t;
          }),
         (Uint,
          struct {
              U128 v;
              ::HIR::CoreType t;
          }),
         (Float,
          struct {
              FloatValue v;
              ::HIR::CoreType t;
          }),
         (Bool,
          struct {
              bool v; // NOTE: Defensive to prevent implicit casts
          }),
         (Bytes, ::std::vector<::std::uint8_t>), // Byte string
         (StaticString, ::std::string),          // String
         // NOTE: These are behind pointers to save inline space (HIR::Path is ~11
         // words, compared to 4 for MIR::Constant without it)
         (Const, struct { ::std::unique_ptr<::HIR::Path> p; }), // `const`
         (Generic, ::HIR::GenericRef),
         // ZST function type, NOT its address
         (Function, struct { ::std::unique_ptr<::HIR::Path> p; }),
         // Address within a named allocation
         (ItemAddr, ItemAddress)),
        (),
        (),
        (friend ::std::ostream & operator<<(::std::ostream& os, const Constant& v); ::Ordering ord(const Constant& b) const; inline bool operator==(const Constant& b) const { return ord(b) == ::OrdEqual; } inline bool operator!=(const Constant& b) const { return ord(b) != ::OrdEqual; } inline bool operator<(const Constant& b) const { return ord(b) == ::OrdLess; } inline bool operator<=(const Constant& b) const { return ord(b) != ::OrdGreater; } inline bool operator>(const Constant& b) const { return ord(b) == ::OrdGreater; } inline bool operator>=(const Constant& b) const { return ord(b) != ::OrdLess; } Constant clone() const;)
    );

    /// Parameter - A value used when a rvalue just reads (doesn't require a lvalue)
    /// Can be either a lvalue (memory address), or a constant
    TAGGED_UNION_EX(
        Param,
        (),
        Constant,
        ((LValue, LValue),
         // TODO: Add `Borrow` here (makes some MIR manipulation more complex, but simplifies emitted code)
         (Borrow,
          struct {
              ::HIR::BorrowType type;
              LValue val;
          }),
         (Constant, Constant)),
        (),
        (),
        (Param clone() const; friend ::std::ostream & operator<<(::std::ostream& os, const Param& v); bool operator==(const Param& b) const; inline bool operator!=(const Param& b) const { return !(*this == b); })
    );

    TAGGED_UNION_EX(
        RValue,
        (),
        Tuple,
        (
            // TODO: Split "Use" into "Copy" and "Move" (Where 'move' indicates that the source is unused)
            (Use, LValue),
            (Borrow,
             struct {
                 ::HIR::BorrowType type;
                 bool isRaw;
                 LValue val;
             }),
            (Constant, Constant),
            (SizedArray,
             struct {
                 Param val;
                 ::HIR::ArraySize count;
             }),
            // Cast on primitives (thin pointers, integers, floats)
            (Cast,
             struct {
                 LValue val;
                 ::HIR::TypeRef type;
             }),
            // Binary operation on primitives
            (BinOp,
             struct {
                 Param valL;
                 eBinOp op;
                 Param valR;
             }),
            // Unary operation on primitives
            (UniOp,
             struct {
                 LValue val; // NOTE: Not a param, because UniOps can be const propagated
                 eUniOp op;
             }),
            // Extract the metadata from a DST pointer
            // NOTE: If used on an array, this yields the array size (for generics)
            (DstMeta, struct { LValue val; }),
            // Extract the pointer from a DST pointer (as *const ())
            (DstPtr, struct { LValue val; }),
            // Construct a DST pointer from a thin pointer and metadata
            // OR: (if `meta_val` is `Constant::ItemAddr(nullptr)`) A still-to-be-resolved unsizing coercion
            (MakeDst,
             struct {
                 Param ptrVal;
                 Param metaVal;
             }),
            (Tuple, struct { ::std::vector<Param> vals; }),
            // Array literal
            (Array, struct { ::std::vector<Param> vals; }),
            // Create a new instance of a union
            (UnionVariant,
             struct {
                 ::HIR::GenericPath path;
                 unsigned int index;
                 Param val;
             }),
            // Create a new instance of an enum
            // - Separate from UnionVariant, as the contents is needed when creating the body
            (EnumVariant,
             struct {
                 ::HIR::GenericPath path;
                 unsigned int index;
                 ::std::vector<Param> vals;
             }),
            // Create a new instance of a struct
            (Struct,
             struct {
                 ::HIR::GenericPath path;
                 ::std::vector<Param> vals;
             })
        ),
        (),
        (),
        (RValue clone() const;)
    );
    extern ::std::ostream& operator<<(::std::ostream& os, const RValue& x);
    extern bool operator==(const RValue& a, const RValue& b);

    static inline bool operator!=(const RValue& a, const RValue& b) {
        return !(a == b);
    }

    TAGGED_UNION(CallTarget, Intrinsic, (Value, LValue), (Path, ::HIR::Path), (Intrinsic, struct {
                     RcString name;
                     ::HIR::PathParams params;
                 }));
    TAGGED_UNION_EX(SwitchValues, (), Unsigned, ((Unsigned, ::std::vector<uint64_t>), (Signed, ::std::vector<int64_t>), (String, ::std::vector<::std::string>), (ByteString, ::std::vector<::std::vector<uint8_t>>)), (), (), (SwitchValues clone() const; bool operator==(const SwitchValues& x) const; bool operator!=(const SwitchValues& x) const { return !(*this == x); }));

    TAGGED_UNION(
        UnwindAction,
        Continue,
        (Continue, struct {}),
        (Cleanup, BasicBlockId),
        (Terminate, struct {}),
        (Unreachable, struct {})
    );

    enum class eDropKind {
        SHALLOW,
        DEEP,
    };

    TAGGED_UNION(
        Terminator,
        Incomplete,
        (Incomplete, struct {}),               // Block isn't complete (ERROR in output)
        (Return, struct {}),                   // Return clealy to caller
        (UnwindResume, struct {}),             // Resume the currently caught exception
        (UnwindTerminate, struct {}),          // Abort if unwinding reaches this point
        (Unreachable, struct {}),              // This control-flow edge cannot be reached
        (Goto, BasicBlockId),                  // Jump to another block
        (If,
         struct {
             LValue cond;
             BasicBlockId bbTrue;
             BasicBlockId bbFalse;
         }),
        (Switch,
         struct {
             LValue val;
             ::std::vector<BasicBlockId> targets;
             unsigned int validFlag = ~0u;
             BasicBlockId invalidTarget = ~0u;
         }),
        (SwitchValue,
         struct {
             LValue val;
             BasicBlockId defTarget;
             ::std::vector<BasicBlockId> targets;
             SwitchValues values;
        }),
        (Drop, struct {
            eDropKind kind;
            LValue slot;
            unsigned int flagIdx;
            BasicBlockId target;
            UnwindAction unwind;
        }),
        (Call, struct {
            BasicBlockId retBlock;
            UnwindAction unwind;
            LValue retVal;
            CallTarget fcn;
            ::std::vector<Param> args;
        })
    );
    extern ::std::ostream& operator<<(::std::ostream& os, const Terminator& x);
    extern bool operator==(const Terminator& a, const Terminator& b);

    static inline bool operator!=(const Terminator& a, const Terminator& b) {
        return !(a == b);
    }

    TAGGED_UNION(AsmParam, Const, (Const, ::MIR::Constant), (Sym, ::HIR::Path), (Reg, struct {
                     AsmCommon::Direction dir;
                     AsmCommon::RegisterSpec spec;
                     std::unique_ptr<MIR::Param> input;
                     std::unique_ptr<MIR::LValue> output;
                 }));
    extern bool operator==(const AsmParam& a, const AsmParam& b);

    TAGGED_UNION(
        Statement,
        Asm,
        // Value assigment
        (Assign,
         struct {
             LValue dst;
             RValue src;
         }),
        // Inline assembly (`llvm_asm!`)
        (Asm,
         struct {
             ::std::string tpl;
             ::std::vector<::std::pair<::std::string, LValue>> outputs;
             ::std::vector<::std::pair<::std::string, LValue>> inputs;
             ::std::vector<::std::string> clobbers;
             ::std::vector<::std::string> flags;
         }),
        // Inline assembly (stabilised)
        (Asm2,
         struct {
             AsmCommon::Options options;
             std::vector<AsmCommon::Line> lines;
             ::std::vector<AsmParam> params;
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
             LValue slot;
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
             LValue slot;
             /// Source bit index
             unsigned int bitIndex;
         }),
        (ScopeEnd, struct { ::std::vector<unsigned> slots; })
    );
    extern ::std::ostream& operator<<(::std::ostream& os, const Statement& x);
    extern bool operator==(const Statement& a, const Statement& b);

    static inline bool operator!=(const Statement& a, const Statement& b) {
        return !(a == b);
    }

    struct BasicBlock {
        ::std::vector<Statement> statements;
        Terminator terminator;
        bool isCleanup = false;
    };

    struct EnumCache; // Defined in trans/enumerate.cpp

    class EnumCachePtr {
        const EnumCache* p;

    public:
        EnumCachePtr(const EnumCache* p = nullptr);

        ~EnumCachePtr();

        EnumCachePtr(EnumCachePtr&& x);

        EnumCachePtr& operator=(EnumCachePtr&& x);

        operator bool() {
            return p != nullptr;
        }

        const EnumCache& operator*() const {
            return *p;
        }

        const EnumCache* operator->() const {
            return p;
        }
    };

    class Function {
    public:
        ::std::vector<::HIR::TypeRef> locals;
        //::std::vector< RcString>   local_names;
        ::std::vector<bool> dropFlags;

        ::std::vector<BasicBlock> blocks;

        // Cache filled/used by enumerate
        mutable EnumCachePtr transEnumState;
    };

    class Cloner {
        ::std::unique_ptr<MonomorphiserNop> nop;

    public:
        const Span& sp;

        Cloner(const Span& sp, HIR::TypeInterner& types);
        virtual ~Cloner();

        virtual ::MIR::BasicBlockId mapBbIdx(::MIR::BasicBlockId idx) const {
            return idx;
        }

        virtual unsigned mapLocal(unsigned f) const {
            return f;
        }

        virtual unsigned mapDropFlag(unsigned f) const {
            return f;
        }

        virtual const HIR::TypeData* valueGenericType(HIR::GenericRef ce) const;
        virtual const Monomorphiser& monomorphiser() const;

        virtual const StaticTraitResolve* resolve() const {
            return nullptr;
        }

        virtual ::MIR::Statement cloneStmt(const ::MIR::Statement& src) const;
        virtual ::MIR::Terminator cloneTerm(const ::MIR::Terminator& src) const;

        virtual ::MIR::LValue cloneLval(const ::MIR::LValue& src) const;
        virtual ::MIR::RValue cloneRval(const ::MIR::RValue& src) const;
        virtual ::MIR::Param cloneParam(const ::MIR::Param& src) const;
        virtual ::MIR::Constant cloneConstant(const ::MIR::Constant& src) const;

        ::std::vector<MIR::AsmParam> cloneAsmParams(const ::std::vector<MIR::AsmParam>& params) const;
        ::std::vector<::std::pair<::std::string, ::MIR::LValue>> cloneNameLvalVec(const ::std::vector<::std::pair<::std::string, ::MIR::LValue>>& src) const;
        ::std::vector<::MIR::Param> cloneParamVec(const ::std::vector<::MIR::Param>& src) const;
        ::std::vector<::MIR::LValue> cloneLvalVec(const ::std::vector<::MIR::LValue>& src) const;

        // -- Monomorphise various types
        ::HIR::TypeRef monomorph(const ::HIR::TypeData* x) const;
        ::HIR::GenericPath monomorph(const ::HIR::GenericPath& x) const;
        ::HIR::Path monomorph(const ::HIR::Path& x) const;
        ::HIR::PathParams monomorph(const ::HIR::PathParams& x) const;
    };

} // namespace MIR
