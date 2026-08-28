#include "trans_codegen_c.h"

#include "hir_hir.h"
#include "mir_mir.h"
#include "wire_board.h"
#include "mir_helpers.h"
#include "trans_target.h"
#include "trans_codegen.h"
#include "target_version.h"
#include "trans_mangling.h"
#include "trans_allocator.h"
#include "hir_typeck_static.h"

#include <std/sym/i_map.h>
#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>
#include <std/rng/split_mix_64.h>

#include <cmath>
#include <limits>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <algorithm>
#include <string_view>
#include <codegen_c_prelude.h>

using namespace stl;

namespace {
    struct FmtShell {
        const ::std::string& s;

        FmtShell(const ::std::string& s);
    };

    struct FmtGccAsm {
        const ::std::string& s;
        bool escapePercent;

        FmtGccAsm(const ::std::string& s, bool escapePercent);
    };

    struct StringList {
        ::std::vector<::std::string> cached;
        ::std::vector<const char*> strings;

        StringList();

        StringList(const StringList&) = delete;
        StringList(StringList&&) = default;

        const ::std::vector<const char*>& getVec() const;

        std::vector<const char*>::const_iterator begin() const;

        std::vector<const char*>::const_iterator end() const;

        void push_back(::std::string s);

        void push_back(const char* s);
    };

    struct CUnwindOperationCallback {
        virtual void emit(unsigned indentLevel) = 0;
    };

    template <typename F>
    struct CUnwindOperationCb final: CUnwindOperationCallback {
        F f;

        explicit CUnwindOperationCb(F f);

        void emit(unsigned indentLevel) override;
    };

    struct CSlotCallback {
        virtual void emit() = 0;
    };

    using MIRParamList = ::std::vector<MIRParam>;

    template <typename F>
    struct CSlotCb final: CSlotCallback {
        F f;

        explicit CSlotCb(F f);

        void emit() override;
    };

    struct CSwitchArmCallback {
        virtual void emit(size_t arm) = 0;
    };

    template <typename F>
    struct CSwitchArmCb final: CSwitchArmCallback {
        F f;

        explicit CSwitchArmCb(F f);

        void emit(size_t arm) override;
    };

    struct CDestructorCountCallback {
        virtual void emit() = 0;
    };

    template <typename F>
    struct CDestructorCountCb final: CDestructorCountCallback {
        F f;

        explicit CDestructorCountCb(F f);

        void emit() override;
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

    struct CodeGeneratorC: public CodeGenerator {
        Span sp;

        const HIRCrate& crate;
        const WireBoard& wb_;
        ::StaticTraitResolve resolve_;

        template <typename T>
        RcString TransMangle(const T& value) const;

        template <typename T>
        RcString TransMangleValue(const T& value) const;

        RcString TransMangleTypeId(const HIRTypeData* type) const;

        ::std::string outfilePath;
        ::std::string outfilePathC;

        ::std::ofstream of;
        FILE* literalBlob = nullptr;
        size_t literalBlobSize = 0;
        const MIRTypeResolve* mirRes = nullptr;
        Vector<u8> noOpCleanupBlocks;
        Vector<u8> cleanupCandidateBlocks;
        Vector<MIRBasicBlockId> cleanupReachabilityWorklist;
        Vector<MIRBasicBlockId> forwardedBlockTargets;
        Vector<u8> inlinedReturnBlocks;
        Vector<u8> forwardingState;
        Vector<MIRBasicBlockId> forwardingPath;
        Vector<u32> blockIncoming;
        Vector<u8> asmLabelBlocks;
        Vector<u8> blockLabels;
        MIRBasicBlockId fallthroughBlock = ~0u;
        bool currentFunctionTracksCaller = false;
        bool currentFunctionRealignsArguments = false;

        static constexpr size_t maxCTypeAlignment = 1u << 28;
        static constexpr size_t backendOptimizationBudget = 10'000;

        static bool exceedsBackendOptimizationBudget(const HIRFunction& item, const MIRFunction& code);

        static size_t cTypeAlignment(size_t rustSize, size_t rustAlignment);

        struct {
            bool emulatedI128 = false;
            bool disallowEmptyStructs = false;
        } options;

        HIRTypeRefSet emittedFnTypes;
        ::std::set<HIRPath> trackedFunctions;
        ::std::set<const TypeRepr*> embeddedTags;
        HIRTypeRefMap<HIRTypeRef> normalizedCtypes;

        /// Storage the compiler made for a promoted borrow, keyed by what it
        /// holds. Two promoted borrows of the same value are the same value:
        /// rustc gives them one address, and library code compares those
        /// addresses. Entries chain per hash and are told apart by comparing.
        struct PromotedNode {
            PromotedNode* next;
            // Owned: the enumeration's list of statics is released once they
            // are emitted, and function bodies name them after that.
            HIRPath path;
            RcString ctype;
            const EncodedLiteral* value;
        };

        IntMap<PromotedNode*> promotedValues;

        struct CallerLocationNode {
            CallerLocationNode* hashNext;
            CallerLocationNode* orderNext;
            SourceLocation source;
            u32 index;

            CallerLocationNode(const SourceLocation& source, u32 index, CallerLocationNode* hashNext);
        };

        ObjPool::Ref callerLocationPoolOwner = ObjPool::fromMemory();
        ObjPool* callerLocationPool = callerLocationPoolOwner.mutPtr();
        IntMap<CallerLocationNode*> callerLocations{callerLocationPool};
        CallerLocationNode* firstCallerLocation = nullptr;
        CallerLocationNode* lastCallerLocation = nullptr;
        u32 callerLocationCount = 0;

        bool usesIntelCompilerAsmDialect() const;

        bool literalBlobPathIsSafe() const;

        size_t appendLiteralBlob(const EncodedLiteral& encoded);

        void closeLiteralBlob();

        CodeGeneratorC(const WireBoard& wb, const HIRCrate& crate, const ::std::string& outfile);

        ~CodeGeneratorC();

        void finalise(const TransOptions& opt, CodegenOutput outTy, const ::std::string& hirFile) override;

        void emitBoxDrop(unsigned indentLevel, const HIRTypeData* innerType, const HIRTypeData* boxType, const MIRLValue& slot, bool runDestructor);

        std::string asmSymbol(const Span& span, const HIRPath& path) const;

        std::string inlineAsmConstant(const MIRConstant& operand) const;

        std::string globalAsmConstant(const HIRGlobalAssembly& assembly, const HIRGlobalAsmOperand::Data_Const& operand) const;

        void emitGlobalAsm(const HIRGlobalAssembly& se) override;

        void emitTypeId(const HIRTypeData* ty) override;

        static const char* compilerAbiAttribute(const RcString& abi);

        void emitTypeProto(const HIRTypeData* ty) override;

        void emitTypeFn(const HIRTypeData* ty);

        // Shared logic between `emit_struct` and `emit_type` (w/ Tuple)
        void emitStructInner(const HIRTypeData* ty, const TypeRepr* repr, unsigned packingMaxAlign);

        void emitType(const HIRTypeData* ty) override;

        void emitStruct(const Span& sp, const HIRGenericPath& p, const HIRStruct& item) override;

        void emitUnion(const Span& sp, const HIRGenericPath& p, const HIRUnion& item) override;

        bool isEnumTag(const TypeRepr* repr, size_t idx);

        const HIRTypeData* emitEnumPath(const TypeRepr* repr, const TypeRepr::FieldPath& path);

        void emitEnum(const Span& sp, const HIRGenericPath& p, const HIREnum& item) override;

        void emitConstructorEnum(const Span& sp, const HIRGenericPath& path, const HIREnum& item, size_t varIdx) override;

        void emitConstructorStruct(const Span& sp, const HIRGenericPath& p, const HIRStruct& item) override;

        // Returns `true` if the type is pointer-aligned (i.e. it could contain a pointer)
        /// An `extern type` naming a symbol may not be used anywhere else, and
        /// so may never have been declared. It is opaque either way, so an
        /// empty definition says all there is to say about it.
        void emitExternTypeDefinition(const HIRTypeData* type);

        bool emitStaticTy(const HIRTypeData* type, const HIRPath& p, bool isProto, size_t explicitAlignment);

        void emitStaticExt(const HIRPath& p, const HIRStatic& item, const TransParams& params) override;

        void emitStaticProto(const HIRPath& p, const HIRStatic& item, const TransParams& params) override;

        static u64 promotedHash(const RcString& ctype, const EncodedLiteral& value);

        /// The static that holds this value, if one has been seen. The emitted
        /// C type is what has to agree, not the type as HIR spells it: after
        /// this, the two names stand for one union of that type.
        const HIRPath* promotedHolder(const HIRTypeData* ty, const EncodedLiteral& value) const;

        /// The static that holds this value, making this one hold it when no
        /// other does yet.
        const HIRPath* takePromotedHolder(const HIRPath& p, const HIRTypeData* ty, const EncodedLiteral& value);

        /// Only storage the compiler made for a promoted borrow shares a
        /// place with another: a `static` the program wrote keeps its own.
        static bool promotedIsShared(const HIRStatic& item);

        /// Two promoted values are matched under the C type they are emitted
        /// as, so a type still carrying a parameter -- an array whose length
        /// is one, say -- has no name to match under yet.
        static bool promotedTypeIsSettled(const HIRTypeData* ty);

        /// The bytes a promoted static holds. One inside a generic body holds
        /// them per instantiation, under the path that names that one, so two
        /// instantiations only share a place where their bytes agree.
        static const EncodedLiteral* promotedValue(const HIRPath& p, const HIRStatic& item);

        void emitStaticLocal(const HIRPath& p, const HIRStatic& item, const TransParams& params, const EncodedLiteral& encoded) override;

        void emitFloat(FloatValue v, HIRCoreType ty);

        void printEscapedString(const std::string& s);

        void printEscapedString(const std::vector<u8>& s);

        void printEscapedStringInner(const char* start, const char* end);

        void emitFunctionExt(const HIRPath& p, const HIRFunction& item, const TransParams& params) override;

        void emitFunctionLinkageAlias(const HIRPath& p, const HIRFunction& item);

        void emitFunctionDefinitionPrefix(const HIRFunction& item, bool isExternDef);

        void emitFunctionProto(const HIRPath& p, const HIRFunction& item, const TransParams& params, bool isExternDef) override;

        void emitFunctionCode(const HIRPath& p, const HIRFunction& item, const TransParams& params, bool isExternDef, const MIRFunctionPointer& code, bool hasPrototype) override;

        bool cleanupBlockIsNoOp(MIRBasicBlockId block) const;

        bool dropOperationIsNoOp(const MIRTypeResolve& localMirRes, const MIRTerminator::Data_Drop& drop) const;

        MIRBasicBlockId forwardedBlockTarget(MIRBasicBlockId block) const;

        bool blockIsForwarded(MIRBasicBlockId block) const;

        bool blockIsInlinedReturn(MIRBasicBlockId block) const;

        void markBlockLabel(MIRBasicBlockId target, MIRBasicBlockId fallthrough, bool allowFallthrough);

        template <typename CleanupBlocks>
        void findBlockLabels(const MIRFunction& code, const CleanupBlocks& cleanupBlocks);

        void findForwardedBlocks(const MIRTypeResolve& localMirRes, const MIRFunction& code);

        void findNoOpCleanupBlocks(const MIRTypeResolve& localMirRes, const MIRFunction& code, const Vector<MIRBasicBlockId>& cleanupEntries);

        /// A `#[no_mangle] extern "C" fn main` in a `#![no_main]` crate is the
        /// program's entry point, but C++ dictates `main`'s signature. Emit the
        /// Rust function under its own name and call it from a real `main`.
        void emitCMainShim(const HIRPath& p, const HIRFunction& item, const TransParams& params, const HIRTypeData* retType);

        void emitOperationWithUnwindCb(const MIRUnwindAction& action, unsigned indentLevel, CUnwindOperationCallback& emitOperation);

        template <typename F>
        void emitOperationWithUnwind(const MIRUnwindAction& action, unsigned indentLevel, F f);

        void emitBlockTerminator(MIRTypeResolve& localMirRes, const MIRTerminator& term, unsigned blockIndex, bool cleanup, unsigned indentLevel);

        void emitCleanupRunner(MIRTypeResolve& localMirRes, const ::std::set<unsigned>& cleanupBlocks);

        bool typeIsEmulatedI128(const HIRTypeData* ty) const;

        /// Whether the type becomes a C scalar, so `volatile` can qualify it in
        /// place. C++ gives a `volatile` aggregate no assignment operator, so a
        /// struct has to be copied byte by byte instead.
        bool typeIsCScalar(const HIRTypeData* ty) const;

        // Returns true if the input type is a ZST and ZSTs are not being emitted
        bool typeIsBadZst(const HIRTypeData* ty) const;

        bool lvalueIsBadZst(const MIRLValue& lv) const;

        // Locals whose complete Rust type is a ZST aren't emitted in C.  A
        // projection from such a local has no C lvalue to take the address of.
        bool lvalueRootIsBadZst(const MIRLValue& lv) const;

        // An index into a zero-sized array is represented by the array's
        // address, never by a C `DATA` field (such fields are omitted).  Peel
        // nested zero-sized array projections to their materialized backing
        // lvalue before taking that address.
        MIRLValue lvalueZstIndexBacking(const MIRLValue& lv) const;

        void emitBorrow(const MIRTypeResolve& localMirRes, HIRBorrowType bt, const MIRLValue& val);

        void emitCompositeAssignCb(const MIRTypeResolve& localMirRes, CSlotCallback& emitSlot, const MIRParamList& vals, unsigned indentLevel, bool prependNewline);

        template <typename F>
        void emitCompositeAssign(const MIRTypeResolve& localMirRes, F f, const MIRParamList& vals, unsigned indentLevel, bool prependNewline = true);

        void emitDropOperation(const MIRTypeResolve& localMirRes, const MIRTerminator::Data_Drop& e, unsigned indentLevel);

        void emitStatement(const MIRTypeResolve& localMirRes, const MIRStatement& stmt, unsigned indentLevel = 1);

        void emitRvalueCast(const MIRTypeResolve& localMirRes, const MIRLValue& dst, const MIRRValue::Data_Cast& ve);

        void emitTermSwitchCb(const MIRTypeResolve& localMirRes, const MIRLValue& val, size_t nArms, unsigned indentLevel, CSwitchArmCallback& cb, size_t oddArm);

        template <typename F>
        void emitTermSwitch(const MIRTypeResolve& localMirRes, const MIRLValue& val, size_t nArms, unsigned indentLevel, F f, size_t oddArm = -1);

        void emitTermSwitchvalueCb(const MIRTypeResolve& localMirRes, const MIRLValue& val, const MIRSwitchValues& values, unsigned indentLevel, CSwitchArmCallback& cb);

        /// The ABI of the function a call names, which decides whether a
        /// zero-sized argument is passed at all. An intrinsic, or a target
        /// this cannot resolve, is Rust's own.
        RcString calleeAbi(const MIRTypeResolve& localMirRes, const MIRCallTarget& fcn);

        RcString mangleResolvedValuePath(const HIRPath& path) const;

        void emitTermCall(const MIRTypeResolve& localMirRes, const MIRTerminator::Data_Call& e, unsigned indentLevel, bool tailCall = false);

        void emitTermTailCall(const MIRTypeResolve& localMirRes, const MIRTerminator::Data_TailCall& e, unsigned indentLevel);

        bool asmMatchesTemplate(const MIRStatement::Data_Asm& e, const char* tpl, ::std::initializer_list<const char*> inputs, ::std::initializer_list<const char*> outputs);

        void emitAsmGcc(const MIRTypeResolve& localMirRes, const MIRStatement::Data_Asm& e, unsigned indentLevel);

        struct Asm2TplMatch {
            const MIRTypeResolve& mirRes;
            const std::vector<AsmLine>& lines;
            const std::vector<MIRAsmParam>& params;
            std::vector<std::string> fmtLines;
            std::vector<std::string> fmtParams;

            Asm2TplMatch(const MIRTypeResolve& localMirRes, const std::vector<AsmLine>& lines, const std::vector<MIRAsmParam>& params);

            bool matchesTemplate(::std::initializer_list<const char*> lines, ::std::initializer_list<const char*> params) const;

            const MIRAsmParam& p(size_t i) const;

            const MIRParam& input(size_t i) const;

            const MIRLValue& output(size_t i) const;

            /// Get a description of the parameter's important attributes
            static std::string getParamText(const MIRAsmParam& p);

            static const char* getDirText(const AsmDirection& d);

            static bool checkList(const std::vector<std::string>& have, const ::std::initializer_list<const char*>& exp);
        };

        void emitAsm2Gcc(const MIRTypeResolve& localMirRes, const MIRStatement& stmt, unsigned indentLevel);

        void emitAsm2Gcc(const MIRTypeResolve& localMirRes, const AsmOptions& asmOptions, const std::vector<AsmLine>& asmLines, const std::vector<MIRAsmParam>& asmParams, bool asmGoto, MIRBasicBlockId retBlock, unsigned indentLevel);

        const HIRFunction* resolveFunction(const HIRPath& path);

        bool pathTracksCaller(const HIRPath& path);

        void emitSourceLocationInitializer(const SourceLocation& source);

        u64 callerLocationHash(const SourceLocation& source) const;

        CallerLocationNode* internCallerLocation(const SourceLocation& source);

        void emitCallerLocationPointer(const SourceLocation& source);

        void emitCallerLocationDefinitions();

        /// The name a promoted borrow's value is reached by: whichever static
        /// holds that value. A name that stands for the same value keeps its
        /// own definition -- another crate may have been given it -- but is
        /// not what this crate reads the value through.
        const HIRPath& promotedName(const HIRPath& path);

        void emitReifiedFunctionName(const HIRPath& path, bool preserveTrackCaller = false);

        const HIRTypeData* monomorphiseFcnReturn(HIRTypeRef& tmp, const HIRFunction& item, const TransParams& params);

        /// Rust's foreign ABIs pass nothing for a zero-sized argument, and so
        /// does every C compiler an emitted declaration has to agree with.
        bool argumentIsPassed(const RcString& abi, const HIRTypeData* ty);

        void emitFunctionHeader(const HIRPath& p, const HIRFunction& item, const TransParams& params, bool includeCallerLocation = true, const char* nameSuffix = "");

        void emitTrackCallerReifyWrapper(const HIRPath& p, const HIRFunction& item, const TransParams& params);

        /// A tag is matched on the bits it holds. Where a signed tag's niche
        /// values run past its own maximum they read back as negative, while
        /// the layout names them by the unsigned value they are, so both sides
        /// are compared as the unsigned integer of the tag's width.
        static const char* tagUnsignedType(size_t size);

        static uint64_t tagBits(size_t size, size_t value);

        void emitIntrinsicCall(const RcString& name, const HIRPathParams& params, const MIRTerminator::Data_Call& e);

        template <typename F>
        void emitTermSwitchvalue(const MIRTypeResolve& localMirRes, const MIRLValue& val, const MIRSwitchValues& values, unsigned indentLevel, F f);

        void emitDestructorLoopCb(const MIRLValue& slot, const HIRTypeData* elementTy, CDestructorCountCallback& emitCount, unsigned indentLevel);

        template <typename F>
        void emitDestructorLoop(const MIRLValue& slot, const HIRTypeData* elementTy, F f, unsigned indentLevel);

        void emitTupleDestructor(const MIRLValue& slot, const HIRTypeData::Data_Tuple& tuple, bool unsizedValid, unsigned indentLevel);

        /// Whether the slot names a field that the type holding it packs
        /// tighter than the field's own type is aligned.
        bool fieldIsUnderaligned(const MIRLValue& slot, const HIRTypeData* ty);

        /// slot :: The value to drop
        /// ty :: Type of value to be dropped
        /// unsized_valid ::
        /// indent_level :: (formatting) Current amount of indenting
        void emitDestructorCall(const MIRLValue& slot, const HIRTypeData* ty, bool unsizedValid, unsigned indentLevel);

        /// An enum with one variant stores nothing to say which it is, so its
        /// discriminant is a constant read off the enum itself.
        static bool enumIsTagless(const TypeRepr* repr);

        void emitTaglessEnumDiscriminant(const HIRTypeData* ty);

        void emitEnumVariantVal(const TypeRepr* repr, unsigned idx);

        // returns whether a literal can be represented as zeroed memory.
        bool isZeroLiteral(const HIRTypeData* ty, const EncodedLiteral& lit, const TransParams& params);

        void emitLvalue(const MIRLValue::CRef& val);

        void emitLvalue(const MIRLValue& val);

        void emitEncodedConstant(const HIRTypeData* type, const EncodedLiteral& encoded);

        void emitConstant(const MIRConstant& ve, const MIRLValue* dstPtr = nullptr);

        /// Call a helper that takes and returns an unsigned 128-bit value, for a
        /// type that may be the signed one -- which is a distinct type here.
        void emitWide128Call(const HIRTypeData* ty, const char* helper, const MIRParam& arg);

        void emitParam(const MIRParam& p, bool typeBytes = true);

        void emitTraitMetadataParam(const MIRTypeResolve& localMirRes, const MIRParam& param);

        struct CTypeCallback {
            virtual void write(::std::ostream& os) const = 0;
            virtual bool empty() const = 0;

            friend ::std::ostream& operator<<(::std::ostream& os, const CTypeCallback& callback) {
                callback.write(os);
                return os;
            }
        };

        template <typename F>
        struct CTypeCb final: CTypeCallback {
            F f;

            explicit CTypeCb(F f);

            void write(::std::ostream& os) const override;

            bool empty() const override;
        };

        struct EmptyCTypeCb final: CTypeCallback {
            void write(::std::ostream&) const override;

            bool empty() const override;
        };

        void emitCtype(const HIRTypeData* ty);

        template <typename F>
        void emitCtype(const HIRTypeData* ty, F inner, bool isExternC = false);

        void emitCtypeCb(const HIRTypeData* ty, CTypeCallback& inner, bool isExternC = false);

        HIRTypeRef getInnerUnsizedType(const HIRTypeData* ty);

        bool isExternUnsizedType(const HIRTypeData* ty) const;

        void emitExternTypeLayoutPanic(const HIRTypeData* ty);

        unsigned getPackingMaxAlign(const HIRTypeData* ty) const;

        void emitTraitObjectVtableSize(const MIRParam& value);

        void emitTraitObjectVtableAlign(const MIRParam& value);

        void emitDstTailAlign(const HIRTypeData* outerTy, const HIRTypeData* tailTy, const MIRParam& value);

        /// Alignment of an unsized type, which the metadata may be needed for.
        void emitDstAlign(const HIRTypeData* ty, const MIRParam& value);

        /// Size of an unsized type. A wrapper's own prefix is only part of it:
        /// the tail may be another wrapper, whose prefix counts as well.
        void emitDstSize(const HIRTypeData* ty, const MIRParam& value);

        void emitDstFieldOffset(const HIRTypeData* ty, size_t fieldIdx, const MIRParam& value);

        MetadataType metadataType(const HIRTypeData* ty) const;

        template <typename F>
        void emitFunctionArgument(const HIRTypeData* ty, F inner);

        void emitFunctionArgumentCb(const HIRTypeData* ty, CTypeCallback& inner);

        void emitUnsizedArgumentLocal(const HIRTypeData* ty, unsigned index);

        bool isIndirectDstLvalue(const MIRLValue::CRef& value);

        void emitDstLvaluePointer(const MIRLValue::CRef& value);

        void emitDstParamPointer(const MIRParam& param);

        void emitCtypePtr(const HIRTypeData* innerTy, CTypeCallback& inner);

        bool isDst(const HIRTypeData* ty) const;
    };

}

::std::unique_ptr<CodeGenerator> TransCodegenGetGeneratorC(const WireBoard& wb, const HIRCrate& crate, const ::std::string& outfile) {
    return ::std::unique_ptr<CodeGenerator>(new CodeGeneratorC(wb, crate, outfile));
}

FmtShell::FmtShell(const ::std::string& s)
    : s(s)
{
}

FmtGccAsm::FmtGccAsm(const ::std::string& s, bool escapePercent)
    : s(s)
    , escapePercent(escapePercent)
{
}

StringList::StringList() {
}

auto StringList::getVec() const -> const ::std::vector<const char*>& {
    return strings;
}

auto StringList::begin() const -> std::vector<const char*>::const_iterator {
    return strings.begin();
}

auto StringList::end() const -> std::vector<const char*>::const_iterator {
    return strings.end();
}

auto StringList::push_back(::std::string s) -> void {
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

auto StringList::push_back(const char* s) -> void {
    strings.push_back(s);
}

template <typename F>
CUnwindOperationCb<F>::CUnwindOperationCb(F f)
    : f(f)
{
}

template <typename F>
auto CUnwindOperationCb<F>::emit(unsigned indentLevel) -> void {
    f(indentLevel);
}

template <typename F>
CSlotCb<F>::CSlotCb(F f)
    : f(f)
{
}

template <typename F>
auto CSlotCb<F>::emit() -> void {
    f();
}

template <typename F>
CSwitchArmCb<F>::CSwitchArmCb(F f)
    : f(f)
{
}

template <typename F>
auto CSwitchArmCb<F>::emit(size_t arm) -> void {
    f(arm);
}

template <typename F>
CDestructorCountCb<F>::CDestructorCountCb(F f)
    : f(f)
{
}

template <typename F>
auto CDestructorCountCb<F>::emit() -> void {
    f();
}

template <typename T>
auto CodeGeneratorC::TransMangle(const T& value) const -> RcString {
    return ::TransMangle(wb_, value);
}

template <typename T>
auto CodeGeneratorC::TransMangleValue(const T& value) const -> RcString {
    return ::TransMangleValue(wb_, value);
}

auto CodeGeneratorC::TransMangleTypeId(const HIRTypeData* type) const -> RcString {
    return ::TransMangleTypeId(wb_, type);
}

auto CodeGeneratorC::exceedsBackendOptimizationBudget(const HIRFunction& item, const MIRFunction& code) -> bool {
    if (item.markings.inlineType == HIRFunction::Markings::Inline::Always) {
        return false;
    }

    size_t remaining = backendOptimizationBudget;
    auto charge = [&](size_t amount) {
        remaining = amount >= remaining ? 0 : remaining - amount;
    };
    charge(code.locals.size());
    charge(code.dropFlags.size());
    charge(code.blocks.size());
    for (const auto& block : code.blocks) {
        charge(block.statements.size());
        if (remaining == 0) {
            return true;
        }
    }
    return remaining == 0;
}

auto CodeGeneratorC::cTypeAlignment(size_t rustSize, size_t rustAlignment) -> size_t {
    if (rustSize == 0) {
        return 1;
    }
    return rustAlignment > maxCTypeAlignment ? maxCTypeAlignment : rustAlignment;
}

auto CodeGeneratorC::usesIntelCompilerAsmDialect() const -> bool {
    const auto& arch = TargetGetCurSpec(wb_).arch.name;
    return arch == "x86" || arch == "x86_64";
}

auto CodeGeneratorC::literalBlobPathIsSafe() const -> bool {
    for (char c : outfilePath) {
        if (c == '"' || c == '\\' || c == '\n' || c == '\r') {
            return false;
        }
    }
    return true;
}

auto CodeGeneratorC::appendLiteralBlob(const EncodedLiteral& encoded) -> size_t {
    if (!literalBlob) {
        const auto path = outfilePath + ".blob";
        literalBlob = fopen(path.c_str(), "wb");
        ASSERT_BUG(Span(), literalBlob, "Failed to open `" << path << "` for writing");
    }
    const size_t offset = literalBlobSize;
    const size_t written = fwrite(encoded.bytes.data(), 1, encoded.bytes.size(), literalBlob);
    ASSERT_BUG(Span(), written == encoded.bytes.size(), "Failed to write literal blob for `" << outfilePath << "`");
    literalBlobSize += written;
    return offset;
}

auto CodeGeneratorC::closeLiteralBlob() -> void {
    if (literalBlob) {
        ASSERT_BUG(Span(), fclose(literalBlob) == 0, "Failed to close literal blob for `" << outfilePath << "`");
        literalBlob = nullptr;
    }
}

CodeGeneratorC::CodeGeneratorC(const WireBoard& wb, const HIRCrate& crate, const ::std::string& outfile)
    : wb_(wb)
    , crate(crate)
    , resolve_(wb, OpaqueReveal::All)
    , outfilePath(outfile)
    , outfilePathC(outfile + ".cpp")
    , of(outfilePathC)
    , promotedValues(crate.pool)
{
    ASSERT_BUG(Span(), of.is_open(), "Failed to open `" << outfilePathC << "` for writing");
    options.emulatedI128 = TargetGetCurSpec(wb_).backendC.emulatedI128;
    if (TargetGetPointerBits() < 64 && !options.emulatedI128) {
        WARNING(Span(), W0000, "Potentially misconfigured target, 32-bit targets require i128 emulation");
    }
    options.disallowEmptyStructs = true;

    const auto& targetSpec = TargetGetCurSpec(wb_);
    of << "#define TRUSTME_CODEGEN_DISALLOW_EMPTY_STRUCTS " << options.disallowEmptyStructs << "\n"
       << "#define TRUSTME_TARGET_EMULATED_I128 " << options.emulatedI128 << "\n"
       << "#define TRUSTME_TARGET_U128_ALIGN " << static_cast<unsigned>(targetSpec.arch.alignments.u128) << "\n"
       << "#define TRUSTME_TARGET_HAS_NATIVE_F128 " << usesIntelCompilerAsmDialect() << "\n"
       << CODEGEN_C_PRELUDE;
    of << "}\nnamespace {\n"
       << "extern const trustme_caller_location trustme_caller_locations[];\n"
       << "}\nextern \"C\" {\n";
}

CodeGeneratorC::~CodeGeneratorC() {
}

auto CodeGeneratorC::finalise(const TransOptions& opt, CodegenOutput outTy, const ::std::string& hirFile) -> void {
    const bool createShims = (outTy == CodegenOutput::Executable);

    // TODO: Support dynamic libraries too
    // - No main, but has the rest.
    // - Well... for cdylibs that's the case, for rdylibs it's not
    if (outTy == CodegenOutput::Executable && !crate.noMain) {
        // TODO: Define this function in MIR?
        of << "}\n\n";
        of << "int main(int argc, const char* argv[]) {\n";
        auto cStartPath = resolve_.hirCrate().getLangItemPathOpt("trustme-start");
        if (cStartPath == HIRSimplePath()) {
            auto mainPath = crate.getLangItemPath(Span(), "trustme-main");
            const auto& mainFcn = crate.getFunctionByPath(sp, mainPath);

            const auto& startPath = resolve_.hirCrate().getLangItemPathOpt("start");
            if (crate.isNoCore && startPath == HIRSimplePath()) {
                // A no_core binary has no standard entrypoint protocol.
                // Call its ordinary main directly instead of inventing a
                // `start` language item.
                of << "\t" << TransMangleValue(HIRGenericPath(mainPath)) << "();\n";
                of << "\treturn 0;\n";
            } else {
                auto startGpath = HIRGenericPath(resolve_.hirCrate().getLangItemPath(Span(), "start"));
                startGpath.params.types.push_back(mainFcn.returnType);
                of << "\treturn " << TransMangleValue(startGpath) << "(" << TransMangleValue(HIRGenericPath(mainPath)) << ", argc, (u8**)argv";
                of << ", 0"; // `sigpipe` setting
                // 0: Default, 1: Inherit, 2: SIG_IGN, 3: SIG_DFL
                of << ");\n";
            }
        } else {
            of << "\treturn " << TransMangleValue(HIRGenericPath(cStartPath)) << "(argc, (u8**)argv);\n";
        }
        of << "}\n\n";
        of << "extern \"C\" {\n";
    }

    // Auto-generated code/items for the "root" rust binary (cdylib or executable)
    if (createShims) {
        // Allocator/panic shims
        {
            const auto allocatorIt = crate.langItems.find(GLOBAL_ALLOCATOR_LANG_ITEM);
            const bool hasGlobalAllocator = allocatorIt != crate.langItems.end();
            const HIRStatic* globalAllocator = hasGlobalAllocator ? &crate.getStaticByPath(Span(), allocatorIt->second) : nullptr;
            for (size_t i = 0; i < NUM_ALLOCATOR_METHODS; i++) {
                struct H {
                    static void tyArgs(::std::vector<const char*>& out, AllocatorDataTy t) {
                        switch (t) {
                            case AllocatorDataTy::Unit:
                            case AllocatorDataTy::ResultPtr: // (..., *mut i8) + *mut u8
                                UNREACHABLE();
                            // - Args
                            case AllocatorDataTy::Layout: // usize, usize
                                out.push_back("uintptr_t");
                                out.push_back("uintptr_t");
                                break;
                            case AllocatorDataTy::Ptr: // *mut u8
                                out.push_back("i8*");
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
                                return "i8*";
                            // - Args
                            case AllocatorDataTy::Layout: // usize, usize
                            case AllocatorDataTy::Ptr:    // *mut u8
                            case AllocatorDataTy::Usize:
                                UNREACHABLE();
                        }
                        UNREACHABLE();
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
                                of << "\tauto layout" << layoutArg << " = ";
                                emitReifiedFunctionName(TransAllocatorLayoutCtorPath(crate));
                                of << "(a" << flatArg << ", a" << flatArg + 1 << ");\n";
                                flatArg += 2;
                                layoutArg += 1;
                                break;
                            case AllocatorDataTy::Ptr:
                            case AllocatorDataTy::Usize:
                                flatArg += 1;
                                break;
                            case AllocatorDataTy::Unit:
                            case AllocatorDataTy::ResultPtr:
                                UNREACHABLE();
                        }
                    }

                    const auto methodPath = TransAllocatorMethodPath(crate, globalAllocator->type, method);
                    const HIRPath staticPath = HIRGenericPath(allocatorIt->second);
                    of << "\t";
                    if (method.ret != AllocatorDataTy::Unit) {
                        of << "return (i8*)";
                    }
                    of << TransMangleValue(methodPath) << "(&" << TransMangleValue(staticPath) << ".val";
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
                                of << "(u8*)a" << flatArg;
                                flatArg += 1;
                                break;
                            case AllocatorDataTy::Usize:
                                of << "a" << flatArg;
                                flatArg += 1;
                                break;
                            case AllocatorDataTy::Unit:
                            case AllocatorDataTy::ResultPtr:
                                UNREACHABLE();
                        }
                    }
                    of << ")";
                    of << ";\n";
                }
                of << "}\n";
            }

            of << "void __rust_no_alloc_shim_is_unstable_v2() {}\n";

            {
                auto oomMethod = crate.getLangItemPathOpt("trustme-alloc_error_handler");
                of << "u8 __rust_alloc_error_handler_should_panic = 0;\n";
                of << "u8 __rust_no_alloc_shim_is_unstable = 0;\n";

                auto layoutPath = HIRSimplePath("core", {"alloc", "Layout"});
                if (oomMethod != HIRSimplePath()) {
                    of << "struct s_" << TransMangle(layoutPath) << "_A { uintptr_t a, b; };\n";
                    of << "void oom_impl(s_" << TransMangle(layoutPath) << "_A l) {"
                       << " extern void " << TransMangleValue(oomMethod) << "(s_" << TransMangle(layoutPath) << "_A l);"
                       << " " << TransMangleValue(oomMethod) << "(l);"
                       << " }\n";
                }

                // Force abort on alloc error, rustc uses `-Zoom={panic,abort}` to select this
                of << "u8 __rust_alloc_error_handler_should_panic_v2() { return 0; }";
                of << "void __rust_alloc_error_handler(uintptr_t s, uintptr_t a) {\n";
                if (oomMethod == HIRSimplePath()) {
                    of << "\tvoid __rdl_oom(uintptr_t, uintptr_t);\n";
                    of << "\t__rdl_oom(s,a);\n";
                } else {
                    of << "\ts_" << TransMangle(layoutPath) << "_A v = { s, a };\n";
                    of << "\toom_impl(v);\n";
                }
                of << "}\n";
            }
        }

        {
            // Bind `panic_impl` only when this crate actually provides
            // a panic implementation. A no_core binary without one can
            // still be valid when no generated code uses it.
            const auto& panicImplPath = crate.getLangItemPathOpt("trustme-panic_implementation");
            if (panicImplPath != HIRSimplePath()) {
                of << "u32 panic_impl(uintptr_t payload) {";
                of << "extern u32 " << TransMangleValue(panicImplPath) << "(uintptr_t payload);";
                of << "return " << TransMangleValue(panicImplPath) << "(payload);";
                of << "}\n";
            } else if (!crate.isNoCore) {
                crate.getLangItemPath(Span(), "trustme-panic_implementation");
            }
        }
    }

    of << "}\n";
    emitCallerLocationDefinitions();
    of.flush();
    of.close();
    ASSERT_BUG(Span(), !of.bad(), "Error set on output stream for: " << outfilePathC);
    closeLiteralBlob();

    // Stop after emitting the C++ source, without invoking the C
    // compiler (used to profile the trustme front/middle-end alone).
    if (opt.emitCppOnly) {
        return;
    }

    struct LinkList: private StringList {
        enum class Ty {
            //Border,   // --{push,pop}-state
            Directory, // -L <value>
            Explicit,  // <value>
            Implicit,  // -l <value>
        };

        std::vector<Ty> ty_;

        void pushDir(const char* s) {
            // Don't de-dup since there's the push/pop rules
            auto it = ::std::find_if(StringList::begin(), StringList::end(), [&](const char* es) {
                return ::std::strcmp(es, s) == 0;
            });
            if (it != StringList::end()) {
                return;
            }
            ty_.push_back(Ty::Directory);
            this->push_back(s);
        }

        void pushExplicit(std::string s) {
            ty_.push_back(Ty::Explicit);
            this->push_back(std::move(s));
        }

        void pushLib(const char* s) {
            if (ty_.size() > 0 && ty_.back() == Ty::Implicit && std::strcmp(this->getVec().back(), s) == 0) {
                return;
            }
            ty_.push_back(Ty::Implicit);
            this->push_back(s);
        }

        void pushLib(std::string s) {
            if (ty_.size() > 0 && ty_.back() == Ty::Implicit && s == this->getVec().back()) {
                return;
            }
            ty_.push_back(Ty::Implicit);
            this->push_back(std::move(s));
        }

        void pushBorder() {
            // If the previous is also a marker, don't push
        }

        struct iterator {
            const LinkList& parent;
            size_t idx;

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
                return std::make_pair(parent.ty_[idx], parent.getVec()[idx]);
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
                auto isDylib = [](const HIRExternCrate& c) {
                    bool rv = false;
                    // TODO: Better rule than this
                    rv |= (c.path.compare(c.path.size() - 3, 3, ".so") == 0);
                    return rv;
                };
                // If this crate is included in a dylib crate, ignore it
                bool isInDylib = false;
                for (const auto& crate2 : crate.extCrates) {
                    if (isDylib(crate2.second)) {
                        for (const auto& subcrate : crate2.second.data->extCrates) {
                            if (subcrate.second.path == extCrate.path) {
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
                if (extCrate.data->langItems.count("trustme-panic_runtime")) {
                    // Check if this is the requested panic crate
                    if (strncmp(crateName.c_str(), opt.panicCrate.c_str(), opt.panicCrate.size()) != 0) {
                        continue;
                    } else {
                    }
                }

                if (extCrate.isProcMacro) {
                    // Host executables participate in expansion, not target linking.
                } else if (extCrate.objectPath != "") {
                    extCrates.push_back(extCrate.objectPath.c_str());
                } else if (extCrate.path.size() >= 5 && extCrate.path.compare(extCrate.path.size() - 5, 5, ".rlib") == 0) {
                    extCrates.push_back(extCrate.path + ".o");
                } else if (isDylib(extCrate)) {
                    extCratesDylib.push_back(extCrate.path.c_str());
                } else {
                    // Probably a procedural macro, ignore it
                }
            }

            struct H {
                static bool fileExists(const std::string& path) {
                    return std::ifstream(path).is_open();
                }

                static std::string findLibraryOne(const std::string& path, const std::string& name) {
                    std::string libPath;
                    libPath = FMT(path << "/lib" << name << ".so");
                    if (fileExists(libPath)) {
                        return libPath;
                    }
                    libPath = FMT(path << "/lib" << name << ".a");
                    if (fileExists(libPath)) {
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
                if (!extCrate.data->extLibs.empty() || !extCrate.data->linkPaths.empty()) {
                    librariesAndDirs.pushBorder();
                }
                for (const auto& path : extCrate.data->linkPaths) {
                    librariesAndDirs.pushDir(path.c_str());
                }
                // NOTE: Does explicit lookup, to provide scoped search directories
                // - Needed for 1.39 cargo on linux when libgit2 and libz exist on the system, butsystem libgit2 isn't new enough
                for (const auto& lib : extCrate.data->extLibs) {
                    ASSERT_BUG(Span(), lib.name != "", "Empty lib from " << crateName);
                    auto path = H::findLibrary(extCrate.data->linkPaths, opt.librarySearchDirs, lib.name);
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
        std::string varname = "CXX_" + TargetGetCurSpec(wb_).backendC.cCompiler;
        std::replace(varname.begin(), varname.end(), '-', '_');

        if (getenv(varname.c_str())) {
            args.push_back(getenv(varname.c_str()));
        } else if (getenv("CXX")) {
            args.push_back(getenv("CXX"));
        } else if (system(("command -v " + TargetGetCurSpec(wb_).backendC.cCompiler + "-g++" + " >/dev/null 2>&1").c_str()) == 0) {
            args.push_back(TargetGetCurSpec(wb_).backendC.cCompiler + "-g++");
        } else {
            args.push_back("g++");
        }
    }
    argFileStart = args.getVec().size();
    args.push_back("-std=gnu++20");
    args.push_back("-fexceptions");
    // Rust integer arithmetic wraps when overflow checks are disabled.
    // Preserve that contract for signed C++ operations even under
    // optimisation, where native signed overflow would otherwise be
    // undefined behaviour.
    args.push_back("-fwrapv");
    if (usesIntelCompilerAsmDialect()) {
        // Rust's default x86 asm dialect is Intel syntax. Keep the C++
        // compiler's operand printer in that dialect too: an inline
        // `.intel_syntax` directive does not change how `%0`, `%k0`,
        // and `%q0` are expanded by the compiler.
        args.push_back("-masm=intel");
    }
    for (const auto& a : TargetGetCurSpec(wb_).backendC.compilerOpts) {
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
            args.push_back("-O1"); // HACK: Reduce the optimisation level to work around a GCC miscompilation
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
            for (const auto& a : TargetGetCurSpec(wb_).backendC.linkerOptsPre) {
                args.push_back(a.c_str());
            }
            for (const auto& c : extCrates) {
                args.push_back(c);
            }
            for (const auto& c : extCratesDylib) {
                args.push_back(c);
            }
            for (const auto& path : opt.frameworkSearchDirs) {
                args.push_back("-F");
                args.push_back(path.c_str());
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
            for (const auto& a : TargetGetCurSpec(wb_).backendC.linkerOptsPost) {
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
    if (getenv("TRUSTME_CCACHE")) {
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
}

auto CodeGeneratorC::emitBoxDrop(unsigned indentLevel, const HIRTypeData* innerType, const HIRTypeData* boxType, const MIRLValue& slot, bool runDestructor) -> void {
    auto indent = RepeatLitStr{"\t", static_cast<int>(indentLevel)};
    if (runDestructor) {
        auto innerPtr = MIRLValue::newField(MIRLValue::newField(MIRLValue::newField(slot.clone(), 0), 0), 0);
        emitDestructorCall(MIRLValue::newDeref(mv$(innerPtr)), innerType, /*unsized_valid=*/true, indentLevel);
    }

    auto p = HIRPath(boxType, crate.getLangItemPath(Span(), "drop"), "drop");
    of << indent << TransMangleValue(p) << "(&";
    emitLvalue(slot);
    of << ");\n";

    // The pointee is a synthetic Box move-path, not a physical field. A shallow
    // drop skips that path, but still drops the real fields after Box::drop.
    const auto* repr = TargetGetTypeRepr(sp, resolve_, boxType);
    MIR_ASSERT(*mirRes, repr, "No repr for Box " << boxType);
    auto field = MIRLValue::newField(slot.clone(), 0);
    for (const auto& fieldRepr : repr->fields) {
        if (resolve_.typeNeedsDropGlue(sp, fieldRepr.ty)) {
            emitDestructorCall(field, fieldRepr.ty, /*unsized_valid=*/false, indentLevel);
        }
        field.incField();
    }
}

auto CodeGeneratorC::asmSymbol(const Span& span, const HIRPath& path) const -> std::string {
    MonomorphState params(crate.types);
    auto item = resolve_.getValue(span, path, params, false);
    const HIRLinkage* linkage = nullptr;
    if (const auto* function = item.opt_Function()) {
        linkage = &(*function)->linkage;
    } else if (const auto* stat = item.opt_Static()) {
        linkage = &(*stat)->linkage;
    } else {
        BUG(span, "asm sym operand does not name a function or static: " << path);
    }

    std::string symbol = linkage->name;
    if (symbol.empty()) {
        symbol = FMT(TransMangleValue(path));
    }
    if (!symbol.empty() && symbol[0] == '\1') {
        symbol.erase(symbol.begin());
    }
    if (TargetGetCurSpec(wb_).osName == "macos") {
        symbol.insert(symbol.begin(), '_');
    }
    return symbol;
}

auto CodeGeneratorC::inlineAsmConstant(const MIRConstant& operand) const -> std::string {
    if (const auto* value = operand.opt_Int()) {
        return FMT(value->v);
    }
    if (const auto* value = operand.opt_Uint()) {
        return FMT(value->v);
    }
    BUG(Span(), "asm const operand is not an integer: " << operand);
}

auto CodeGeneratorC::globalAsmConstant(const HIRGlobalAssembly& assembly, const HIRGlobalAsmOperand::Data_Const& operand) const -> std::string {
    ASSERT_BUG(assembly.span, operand.value.is_Evaluated(), "Unevaluated global_asm const operand");
    ASSERT_BUG(assembly.span, operand.type->is_Primitive() && isInteger(operand.type->as_Primitive()), "Non-integer global_asm const operand: " << operand.type);
    const auto& value = **operand.value.opt_Evaluated();
    ASSERT_BUG(assembly.span, value.relocations.empty(), "Relocated global_asm const operand");

    switch (operand.type->as_Primitive()) {
        case HIRCoreType::Isize:
        case HIRCoreType::I8:
        case HIRCoreType::I16:
        case HIRCoreType::I32:
        case HIRCoreType::I64:
        case HIRCoreType::I128:
            return FMT(EncodedLiteralSlice(value).readSint());
        default:
            return FMT(EncodedLiteralSlice(value).readUint());
    }
}

auto CodeGeneratorC::emitGlobalAsm(const HIRGlobalAssembly& se) -> void {
    of << "__asm__ (\"";
    if (usesIntelCompilerAsmDialect() && se.options.attSyntax) {
        of << ".att_syntax prefix; ";
    }
    for (const auto& l : se.lines) {
        for (const auto& f : l.frags) {
            of << FmtGccAsm(f.before, false);
            ASSERT_BUG(se.span, f.index < se.operands.size(), "Invalid argument reference in global assembly");
            const auto& operand = se.operands[f.index];
            switch (operand.tag()) {
                case HIRGlobalAsmOperand::TAG_Const: {
                    auto& value = operand.as_Const();
                    auto text = globalAsmConstant(se, value);
                    of << FmtGccAsm(text, false);
                    break;
                }
                case HIRGlobalAsmOperand::TAG_Sym: {
                    auto& path = operand.as_Sym();
                    auto text = asmSymbol(se.span, path);
                    of << FmtGccAsm(text, false);
                    break;
                }
            }
        }
        of << FmtGccAsm(l.trailing, false);
        of << ";\\n ";
    }
    if (usesIntelCompilerAsmDialect() && se.options.attSyntax) {
        of << ".intel_syntax noprefix; ";
    }
    of << "\");\n";
}

auto CodeGeneratorC::emitTypeId(const HIRTypeData* ty) -> void {
    of << "tTYPEID __typeid_" << TransMangleTypeId(ty) << " __attribute__((weak));\n";
}

auto CodeGeneratorC::compilerAbiAttribute(const RcString& abi) -> const char* {
    if (abi == "win64") {
        return "__attribute__((ms_abi)) ";
    }
    if (abi == "sysv64") {
        return "__attribute__((sysv_abi)) ";
    }
    return "";
}

auto CodeGeneratorC::emitTypeProto(const HIRTypeData* ty) -> void {
    switch ((*ty).tag()) {
        default:
            // No prototype required
            break;
        case HIRTypeData::TAG_Tuple: {
            auto& te = (*ty).as_Tuple();
            if (te.size() > 0) {
                of << "struct ";
                emitCtype(ty);
                of << ";\n";
            }
            break;
        }
        case HIRTypeData::TAG_Function: {
            emitTypeFn(ty);
            of << "\n";
            break;
        }
        case HIRTypeData::TAG_NamedFunction: {
            of << "struct ";
            emitCtype(ty);
            of << ";\n";
            break;
        }
        case HIRTypeData::TAG_Array: {
            of << "struct ";
            emitCtype(ty);
            of << ";\n";
            break;
        }
        case HIRTypeData::TAG_Path: {
            auto& te = (*ty).as_Path();
            switch (te.binding.tag()) {
                case HIRTypePathBinding::TAG_Unbound: {
                    UNREACHABLE();
                }
                case HIRTypePathBinding::TAG_Opaque: {
                    UNREACHABLE();
                }
                case HIRTypePathBinding::TAG_Struct: {
                    of << "struct s_" << TransMangle(te.path) << ";\n";
                    break;
                }
                case HIRTypePathBinding::TAG_ExternType: {
                    of << "struct x_" << TransMangle(te.path) << ";\n";
                    break;
                }
                case HIRTypePathBinding::TAG_Union: {
                    of << "union u_" << TransMangle(te.path) << ";\n";
                    break;
                }
                case HIRTypePathBinding::TAG_Enum: {
                    of << "struct e_" << TransMangle(te.path) << ";\n";
                    break;
                }
            }
            break;
        }
        case HIRTypeData::TAG_ErasedType: {
            // TODO: Is this actually a bug?
            return;
        }
    }
}

auto CodeGeneratorC::emitTypeFn(const HIRTypeData* ty) -> void {
    if (emittedFnTypes.count(ty)) {
        return;
    }
    emittedFnTypes.insert(ty);

    const auto& te = ty->as_Function();
    of << "typedef ";
    if (te.rettype == crate.types.unit()) {
        of << "void";
    } else {
        // TODO: Better emit_ctype call for return type?
        emitCtype(te.rettype);
    }
    of << " (" << compilerAbiAttribute(te.abi);
    of << "*";
    emitCtype(ty);
    of << ")(";
    if (te.argTypes.empty() && !te.trackCaller) {
        of << "void)";
    } else {
        for (unsigned int i = 0; i < te.argTypes.size(); i++) {
            if (i != 0) {
                of << ",";
            }
            of << " ";
            this->emitFunctionArgument(te.argTypes[i], FMT_CB(ss, ss << "arg" << i;));
        }
        if (te.isVariadic) {
            of << ", ...";
        }
        if (te.trackCaller) {
            MIR_ASSERT(*mirRes, !te.isVariadic, "#[track_caller] on a variadic function pointer");
            if (!te.argTypes.empty()) {
                of << ",";
            }
            of << " const trustme_caller_location* trustme_caller";
        }
        of << " )";
    }
    of << ";";
}

auto CodeGeneratorC::emitStructInner(const HIRTypeData* ty, const TypeRepr* repr, unsigned packingMaxAlign) -> void {
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
        TargetGetSizeAndAlignOf(sp, resolve_, ty, sz, al);
        if (sz == 0 && al == repr->align && al > 0) {
            hasManualAlign = true;
        }
        maxAlign = std::max(maxAlign, al);
        // Track what C will derive separately - under a capping ABI an interior over-aligned member doesn't raise it
        {
            size_t alC = al;
            if (TargetCapsMemberAlignment() && sz > 0 && ent.offset != 0 && alC > 4 && !TargetTypeHasUserAlignment(sp, resolve_, ty)) {
                alC = 4;
            }
            cMaxAlign = std::max(cMaxAlign, alC);
        }

        fields.push_back(fields.size());
        zsts.push_back(sz == 0);
    }
    const auto emittedAlignment = cTypeAlignment(repr->size, repr->align);
    if (packingMaxAlign == 0 && cMaxAlign != emittedAlignment /*&& repr->size > 0*/) {
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
    of << "struct ";
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
        TargetGetSizeAndAlignOf(sp, resolve_, ty, s, a);

        // Check offset/alignment
        if (s == SIZE_MAX) {
        } else if (s == 0) {
        } else {
            MIR_ASSERT(*mirRes, curOfs <= offset, "Current offset is already past expected (#" << fld << "): " << curOfs << " > " << offset);
            auto fieldAlign = a;
            // PowerPC 32-bit ABI alignment
            if (TargetGetCurSpec(wb_).arch.name == "powerpc") {
                if (s > 0) {
                    if (!isFirstField && fieldAlign >= 4 && fieldAlign <= 8) {
                        fieldAlign = 4;
                    }
                    isFirstField = false;
                }
            }
            a = packingMaxAlign > 0 ? std::min<size_t>(packingMaxAlign, fieldAlign) : fieldAlign;
            while (curOfs % a != 0) {
                curOfs++;
            }
        }

        // Inject padding
        if (curOfs < offset) {
            auto n = offset - curOfs;
            of << "\tu8 _padding" << fld << "[" << n << "];\n";
            curOfs += n;
        }
        MIR_ASSERT(*mirRes, curOfs == offset, "Current offset doesn't match expected (#" << fld << "): " << curOfs << " != " << offset);

        if ((*ty).is_Path() && (*ty).as_Path().binding.is_ExternType()) {
            hasUnsized = true;
        } else if (!(s == 0 && options.disallowEmptyStructs)) {
            of << "\t";
            if (const auto* te = ty->opt_Slice()) {
                emitCtype(te->inner, FMT_CB(ss, ss << "_" << fld << "[0]";));
                hasUnsized = true;
            } else if (ty->is_TraitObject()) {
                of << "unsigned char _" << fld << "[0]";
                hasUnsized = true;
            } else if (ty == HIRCoreType::Str) {
                of << "u8 _" << fld << "[0]";
                hasUnsized = true;
            } else {
                // TODO: Nested unsized?
                emitCtype(ty, FMT_CB(ss, ss << "_" << fld));
                sizedFields++;

                hasUnsized |= (s == SIZE_MAX);
            }
            of << ";\n";
        }

        curOfs += s;
    }
    if (repr->align > maxCTypeAlignment && repr->size != SIZE_MAX && curOfs < repr->size) {
        of << "\tu8 _trustme_tail[" << repr->size - curOfs << "];\n";
        curOfs = repr->size;
        sizedFields++;
    }
    if (sizedFields == 0 && !hasUnsized && options.disallowEmptyStructs) {
        of << "\tchar _d;\n";
    }
    of << "}";
    if (hasManualAlign) {
        of << " __attribute__((__aligned__(" << emittedAlignment << ")))";
    }
    of << ";\n";
    if (packingMaxAlign != 0) {
        of << "#pragma pack(pop)\n";
    }
}

auto CodeGeneratorC::emitType(const HIRTypeData* ty) -> void {
    MIRFunction emptyFcn;
    auto pathCallback = makeCallable<MIRPathCb>([&](auto& os) {
        os << "type " << ty;
    });
    MIRTypeResolve topMirRes{sp, resolve_, pathCallback, HIRTypeRef(), {}, emptyFcn};
    mirRes = &topMirRes;

    switch ((*ty).tag()) {
        default:
            // Nothing to emit
            break;
        case HIRTypeData::TAG_Tuple: {
            auto& te = (*ty).as_Tuple();
            if (te.size() > 0) {
                const auto* repr = TargetGetTypeRepr(sp, resolve_, ty);

                emitStructInner(ty, repr, /*packing_max_align=*/0);

                if (repr->size > 0 && repr->size != SIZE_MAX) {
                    of << "static_assert(sizeof(";
                    emitCtype(ty);
                    of << ")==" << repr->size << ");\n";
                }
            }
            break;
        }
        case HIRTypeData::TAG_Function: {
            emitTypeFn(ty);
            of << "\n";
            break;
        }
        case HIRTypeData::TAG_NamedFunction: {
            of << "struct ";
            emitCtype(ty);
            of << " {";
            if (options.disallowEmptyStructs) {
                of << " char _unused; ";
            }
            of << "};\n";
            break;
        }
        case HIRTypeData::TAG_Array: {
            auto& te = (*ty).as_Array();
            size_t rustSize;
            ASSERT_BUG(sp, TargetGetSizeOf(sp, resolve_, ty, rustSize), "Unable to determine array size for " << ty);
            const bool isZeroSized = rustSize == 0;

            size_t align;
            if (isZeroSized) {
                TargetGetAlignOf(sp, resolve_, ty, align);
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
            } else {
                emitCtype(te.inner);
                of << " DATA[" << te.size.as_Known() << "];";
            }
            of << " }";
            if (isZeroSized) {
                of << " __attribute__((";
                of << "__aligned__(" << cTypeAlignment(0, align) << "),";
                of << "))";
            }
            of << ";\n";
            break;
        }
        case HIRTypeData::TAG_ErasedType: {
            // TODO: Is this actually a bug?
            return;
        }
    }

    mirRes = nullptr;
}

auto CodeGeneratorC::emitStruct(const Span& sp, const HIRGenericPath& p, const HIRStruct& item) -> void {
    MIRFunction emptyFcn;
    auto pathCallback = makeCallable<MIRPathCb>([&](auto& os) {
        os << "struct " << p;
    });
    MIRTypeResolve topMirRes{sp, resolve_, pathCallback, HIRTypeRef(), {}, emptyFcn};
    mirRes = &topMirRes;
    // TODO: repr(transparent) and repr(align(foo))

    auto itemTy = crate.types.path(p.clone(), HIRTypePathBinding::make_Struct(&item));
    const auto* repr = TargetGetTypeRepr(sp, resolve_, itemTy);
    MIR_ASSERT(*mirRes, repr, "No repr for struct " << p);

    emitStructInner(itemTy, repr, item.maxFieldAlignment);

    if (repr->size > 0 && repr->size != SIZE_MAX) {
        // TODO: Handle unsized (should check the size of the fixed-size region)
        of << "static_assert(sizeof(s_" << TransMangle(p) << ")==" << repr->size << ");\n";
    }
    of << "static_assert(ALIGNOF(s_" << TransMangle(p) << ")==" << cTypeAlignment(repr->size, repr->align) << ");\n";

    mirRes = nullptr;
}

auto CodeGeneratorC::emitUnion(const Span& sp, const HIRGenericPath& p, const HIRUnion& item) -> void {
    MIRFunction emptyFcn;
    auto pathCallback = makeCallable<MIRPathCb>([&](auto& os) {
        os << "union " << p;
    });
    MIRTypeResolve topMirRes{sp, resolve_, pathCallback, HIRTypeRef(), {}, emptyFcn};
    mirRes = &topMirRes;

    auto itemTy = crate.types.path(p.clone(), HIRTypePathBinding::make_Union(&item));
    const auto* repr = TargetGetTypeRepr(sp, resolve_, itemTy);
    MIR_ASSERT(*mirRes, repr != nullptr, "No repr for union " << itemTy);

    of << "union u_" << TransMangle(p) << " {\n";
    for (unsigned int i = 0; i < repr->fields.size(); i++) {
        assert(repr->fields[i].offset == 0);
        of << "\t";
        emitCtype(repr->fields[i].ty, FMT_CB(ss, ss << "var_" << i;));
        of << ";\n";
    }
    if (repr->align > maxCTypeAlignment && repr->size > 0) {
        of << "\tu8 _trustme_size[" << repr->size << "];\n";
    }
    of << "}";
    // `#[repr(packed)]` on a union caps every member's alignment, so
    // the whole thing is as small and as loosely aligned as its bytes.
    if (item.maxFieldAlignment > 0) {
        of << " __attribute__((packed))";
    }
    // Pin union alignment - under the power ABI gcc takes a union's alignment from its *first* member
    if (repr->align > 0) {
        of << " __attribute__((__aligned__(" << cTypeAlignment(repr->size, repr->align) << ")))";
    }
    of << ";\n";
    if (true && repr->size > 0) {
        of << "static_assert(sizeof(u_" << TransMangle(p) << ")==" << repr->size << ");\n";
    }

    mirRes = nullptr;
}

auto CodeGeneratorC::isEnumTag(const TypeRepr* repr, size_t idx) -> bool {
    if (const auto* ve = repr->variants.opt_Values()) {
        return ve->isTag(idx);
    }
    if (const auto* ve = repr->variants.opt_Linear()) {
        return ve->isTag(idx);
    }
    return false;
}

auto CodeGeneratorC::emitEnumPath(const TypeRepr* repr, const TypeRepr::FieldPath& path) -> const HIRTypeData* {
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
        repr = TargetGetTypeRepr(sp, resolve_, *ty);
        if (isEnumTag(repr, fld)) {
            if (embeddedTags.count(repr)) {
                of << ".DATA";
            }
            of << ".TAG";
            assert(&fld == &path.subFields.back());
        } else if (/*!repr->variants.is_None() ||*/ ((**ty).is_Path() && ((**ty).as_Path().binding.is_Enum()))) {
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

auto CodeGeneratorC::emitEnum(const Span& sp, const HIRGenericPath& p, const HIREnum& item) -> void {
    MIRFunction emptyFcn;
    auto pathCallback = makeCallable<MIRPathCb>([&](auto& os) {
        os << "enum " << p;
    });
    MIRTypeResolve topMirRes{sp, resolve_, pathCallback, HIRTypeRef(), {}, emptyFcn};
    mirRes = &topMirRes;

    auto itemTy = crate.types.path(p.clone(), HIRTypePathBinding::make_Enum(&item));
    const auto* repr = TargetGetTypeRepr(sp, resolve_, itemTy);

    // 1. Enumerate fields with the same offset as the first (these go into a union)
    // TODO: What if all data variants are zero-sized?
    // A `repr(C)` (or `repr(iN)`) enum keeps its tag beside the data
    // rather than in it, so the data fields are all of them but the
    // last -- even when there is only one, which a single-variant
    // enum with a payload has.
    const bool hasSeparateTag = repr->fields.size() >= 2 && isEnumTag(repr, repr->fields.size() - 1) && repr->fields.back().offset != repr->fields[0].offset;
    const size_t dataFieldCount = repr->fields.size() - (hasSeparateTag ? 1 : 0);
    ::std::vector<unsigned> unionFields;
    for (size_t i = 1; i < dataFieldCount; i++) {
        if (repr->fields[i].offset == repr->fields[0].offset) {
            unionFields.push_back(i);
        }
    }
    if (unionFields.size() > 0 || (hasSeparateTag && dataFieldCount == 1)) {
        unionFields.insert(unionFields.begin(), 0);
    }

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
        } else {
            // Leading & external tag: repr(C)
            assert(unionFields.size() + 1 == repr->fields.size());
            assert(isEnumTag(repr, repr->fields.size() - 1));

            assert(repr->fields.back().offset == 0);

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
                const auto& ty = repr->fields[idx].ty;
                if (!this->typeIsBadZst(ty)) {
                    of << "\t\t";
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

    if (repr->align > maxCTypeAlignment && repr->size > 0) {
        size_t contentEnd = 0;
        for (const auto& field : repr->fields) {
            size_t fieldSize = 0;
            MIR_ASSERT(*mirRes, TargetGetSizeOf(sp, resolve_, field.ty, fieldSize), "Unknown enum field size");
            if (fieldSize != SIZE_MAX && contentEnd < field.offset + fieldSize) {
                contentEnd = field.offset + fieldSize;
            }
        }
        if (contentEnd < repr->size) {
            of << "\tu8 _trustme_tail[" << repr->size - contentEnd << "];\n";
        }
    }

    of << "}";
    // `#[repr(align(N))]` on the enum itself; C would otherwise derive the
    // alignment from the tag alone and make the type too small.
    if (item.forcedAlignment > 0) {
        of << " __attribute__((__aligned__(" << cTypeAlignment(repr->size, repr->align) << ")))";
    }
    of << ";\n";

    size_t expSize = (repr->size > 0 ? repr->size : (options.disallowEmptyStructs ? 1 : 0));
    of << "static_assert(sizeof(e_" << TransMangle(p) << ")==" << expSize << ");\n";

    mirRes = nullptr;
}

auto CodeGeneratorC::emitConstructorEnum(const Span& sp, const HIRGenericPath& path, const HIREnum& item, size_t varIdx) -> void {
    auto p = path.clone();
    p.path.popComponent();
    auto ty = crate.types.path(p.clone(), HIRTypePathBinding::make_Enum(&item));

    MonomorphStatePtr ms(crate.types, nullptr, &path.params, nullptr);
    HIRTypeRef tmp;
    auto monomorph = [&](const auto& x) {
        return resolve_.monomorphExpandOpt(sp, tmp, x, ms);
    };

    ASSERT_BUG(sp, item.data.is_Data(), "");
    const auto& var = item.data.as_Data().at(varIdx);
    ASSERT_BUG(sp, var.type->is_Path(), "");
    const auto& str = *var.type->as_Path().binding.as_Struct();
    ASSERT_BUG(sp, str.data.is_Tuple(), "");
    const auto& e = str.data.as_Tuple();

    HIRFunction::argsT args;
    for (unsigned int i = 0; i < e.size(); i++) {
        args.push_back(::std::make_pair(HIRPattern(), monomorph(e[i].ent)));
    }

    MIRFunction emptyFcn;
    auto pathCallback = makeCallable<MIRPathCb>([&](auto& os) {
        os << "enum cons " << path;
    });
    MIRTypeResolve topMirRes{sp, resolve_, pathCallback, ty, args, emptyFcn};
    mirRes = &topMirRes;

    of << "static e_" << TransMangle(p) << " " << TransMangleValue(path) << "(";
    for (unsigned int i = 0; i < e.size(); i++) {
        if (i != 0) {
            of << ", ";
        }
        const auto& ty = args[i].second; // already monomorphised
        emitCtype(ty, FMT_CB(ss, ss << "arg" << i;));
    }
    of << ") {\n";

    of << "\te_" << TransMangle(p) << " rv;\n";

    std::vector<MIRParam> vals;
    for (unsigned int i = 0; i < e.size(); i++) {
        vals.push_back(MIRLValue::newArgument(i));
    }

    // Create the variant
    // - Use `emit_statement` to avoid re-writing the enum tag handling
    emitStatement(*mirRes, MIRStatement::make_Assign({MIRLValue::newReturn(), MIRRValue::make_EnumVariant({p.clone(), static_cast<unsigned>(varIdx), mv$(vals)})}));
    of << "\treturn rv;\n";
    of << "}\n\n";
    mirRes = nullptr;
}

auto CodeGeneratorC::emitConstructorStruct(const Span& sp, const HIRGenericPath& p, const HIRStruct& item) -> void {
    HIRTypeRef tmp;
    MonomorphStatePtr ms(crate.types, nullptr, &p.params, nullptr);
    auto monomorph = [&](const auto& x) {
        return resolve_.monomorphExpandOpt(sp, tmp, x, ms);
    };

    // Crate constructor function
    const auto& e = item.data.as_Tuple();
    of << "static s_" << TransMangle(p) << " " << TransMangleValue(p) << "(";
    for (unsigned int i = 0; i < e.size(); i++) {
        if (i != 0) {
            of << ", ";
        }
        const auto& ty = monomorph(e[i].ent);
        emitCtype(ty, FMT_CB(ss, ss << "_" << i;));
    }
    of << ") {\n";
    // The emitted members are in layout order, which `repr(Rust)` may
    // have shuffled, so each one is named rather than positional.
    of << "\ts_" << TransMangle(p) << " rv = {};\n";
    for (unsigned int i = 0; i < e.size(); i++) {
        const auto& ty = monomorph(e[i].ent);
        if (this->typeIsBadZst(ty)) {
            continue;
        }
        of << "\trv._" << i << " = _" << i << ";\n";
    }
    of << "\treturn rv;\n";
    of << "}\n\n";
}

auto CodeGeneratorC::emitExternTypeDefinition(const HIRTypeData* type) -> void {
    if (type->is_Path() && type->as_Path().binding.is_ExternType()) {
        of << "struct x_" << TransMangle(type->as_Path().path) << " { };\n";
    }
}

auto CodeGeneratorC::emitStaticTy(const HIRTypeData* type, const HIRPath& p, bool isProto, size_t explicitAlignment) -> bool {
    size_t size = 0, align = 0;
    const bool sized = TargetGetSizeAndAlignOf(sp, resolve_, type, size, align);
    align = std::max(align, explicitAlignment);
    bool rv = (align * 8 >= TargetGetPointerBits());
    of << "union u_static_" << TransMangleValue(p);
    // An `extern type` has no size here, and a value of one cannot be
    // read: only its address is ever taken. The symbol still needs a
    // declaration that stands on its own, so give it one byte.
    if (!sized || size == SIZE_MAX) {
        if (isProto) {
            of << "{ ";
            emitCtype(type, FMT_CB(ss, ss << "val";));
            of << "; u8 raw[1]; }";
        }
        of << " " << TransMangleValue(p);
        return false;
    }
    if (isProto) {
        of << "{ ";
        emitCtype(type, FMT_CB(ss, ss << "val";));
        of << "; ";
        if (rv) {
            const auto pointerSize = TargetGetPointerBits() / 8;
            const auto words = size == 0 ? 0 : 1 + (size - 1) / pointerSize;
            of << "uintptr_t raw[" << words << "];";
        } else {
            of << "u8 raw[" << size << "];";
        }
        of << " }";
    }
    of << " " << TransMangleValue(p);
    return rv;
}

auto CodeGeneratorC::emitStaticExt(const HIRPath& p, const HIRStatic& item, const TransParams& params) -> void {
    MIRFunction emptyFcn;
    auto pathCallback = makeCallable<MIRPathCb>([&](auto& os) {
        os << "extern static " << p;
    });
    MIRTypeResolve topMirRes{sp, resolve_, pathCallback, HIRTypeRef(), {}, emptyFcn};
    mirRes = &topMirRes;
    auto type = params.monomorph(resolve_, item.type);

    // LLVM supports prepending a symbol name with \1 to prevent further mangling.
    // Since we're targeting C, not LLVM, strip off this prefix.
    std::string linkageName = item.linkage.name;
    if (!linkageName.empty() && linkageName[0] == '\1') {
        linkageName = linkageName.substr(1);
    }

    if (item.linkage.type == HIRLinkage::Type::ExternWeak) {
        ASSERT_BUG(sp, linkageName != "", "");
        of << "extern char ";
        of << "__attribute__((weak)) ";

        of << linkageName << "[0];\n";

        emitStaticTy(type, p, /*is_proto=*/true, item.explicitAlignment);
        of << " = { .raw = { (uintptr_t)" << linkageName << " } };";
        of << "\n";
        return;
    }

    if (linkageName != "") {
        // Handled with asm() later
    }

    emitExternTypeDefinition(type);
    of << "extern ";
    emitStaticTy(type, p, /*is_proto=*/true, item.explicitAlignment);
    if (linkageName != "") {
        if (TargetGetCurSpec(wb_).osName == "macos") { // Not macOS only, but all Apple platforms.
            of << " asm(\"_" << linkageName << "\")";
        } else {
            of << " asm(\"" << linkageName << "\")";
        }
    }
    of << ";\n";

    mirRes = nullptr;
}

auto CodeGeneratorC::emitStaticProto(const HIRPath& p, const HIRStatic& item, const TransParams& params) -> void {
    MIRFunction emptyFcn;
    auto pathCallback = makeCallable<MIRPathCb>([&](auto& os) {
        os << "static " << p;
    });
    MIRTypeResolve topMirRes{sp, resolve_, pathCallback, HIRTypeRef(), {}, emptyFcn};
    mirRes = &topMirRes;

    auto type = params.monomorph(resolve_, item.type);
    // Two promoted borrows of the same value are the same value: rustc
    // gives them one address, and library code compares those
    // addresses. Which of them holds it is settled here, before any
    // definition can name either.
    if (promotedIsShared(item) && promotedTypeIsSettled(type)) {
        if (const auto* value = promotedValue(p, item)) {
            takePromotedHolder(p, type, *value);
        }
    }
    switch (item.linkage.type) {
        case HIRLinkage::Type::External:
            break;
        case HIRLinkage::Type::Auto:
            break;
        case HIRLinkage::Type::Weak:
            of << "__attribute__((weak)) ";

            break;
        case HIRLinkage::Type::ExternWeak:
            of << "__attribute__((weak_import)) ";

            break;
    }
    if (item.linkage.section != "") {
        of << "__attribute__((section(\"" << item.linkage.section << "\"))) ";
    }
    if (item.params.isGeneric()) {
        of << "__attribute__((weak)) ";
    }
    emitExternTypeDefinition(type);
    of << "extern ";
    emitStaticTy(type, p, /*is_proto=*/true, item.explicitAlignment);
    if (item.explicitAlignment != 0) {
        of << " __attribute__((aligned(" << item.explicitAlignment << ")))";
    }
    of << ";\n";

    mirRes = nullptr;
}

auto CodeGeneratorC::promotedHash(const RcString& ctype, const EncodedLiteral& value) -> u64 {
    auto h = splitMix64(ctype.contentHash());
    for (auto b : value.bytes) {
        h = splitMix64(h ^ static_cast<u64>(b));
    }
    for (const auto& reloc : value.relocations) {
        h = splitMix64(h ^ reloc.ofs ^ (reloc.len << 8));
    }
    return h;
}

auto CodeGeneratorC::promotedHolder(const HIRTypeData* ty, const EncodedLiteral& value) const -> const HIRPath* {
    const auto ctype = TransMangle(ty);
    auto* head = promotedValues.find(promotedHash(ctype, value));
    for (auto* n = (head ? *head : nullptr); n; n = n->next) {
        if (n->ctype == ctype && *n->value == value) {
            return &n->path;
        }
    }
    return nullptr;
}

auto CodeGeneratorC::takePromotedHolder(const HIRPath& p, const HIRTypeData* ty, const EncodedLiteral& value) -> const HIRPath* {
    if (const auto* held = promotedHolder(ty, value)) {
        return held;
    }
    const auto ctype = TransMangle(ty);
    const auto h = promotedHash(ctype, value);
    auto* head = promotedValues.find(h);
    auto* node = crate.pool->make<PromotedNode>(PromotedNode{head ? *head : nullptr, p.clone(), ctype, &value});
    if (head) {
        *head = node;
    } else {
        promotedValues.insert(h, node);
    }
    return &node->path;
}

auto CodeGeneratorC::promotedIsShared(const HIRStatic& item) -> bool {
    return item.isPromoted && !item.noEmitValue;
}

auto CodeGeneratorC::promotedTypeIsSettled(const HIRTypeData* ty) -> bool {
    return !monomorphiseTypeNeeded(ty) && !ty->mayHaveAssociatedType();
}

auto CodeGeneratorC::promotedValue(const HIRPath& p, const HIRStatic& item) -> const EncodedLiteral* {
    if (!item.params.isGeneric()) {
        return item.valueGenerated ? &item.valueRes : nullptr;
    }
    auto it = item.monomorphCache.find(p);
    return it == item.monomorphCache.end() ? nullptr : &it->second;
}

auto CodeGeneratorC::emitStaticLocal(const HIRPath& p, const HIRStatic& item, const TransParams& params, const EncodedLiteral& encoded) -> void {
    MIRFunction emptyFcn;
    auto pathCallback = makeCallable<MIRPathCb>([&](auto& os) {
        os << "static " << p;
    });
    MIRTypeResolve topMirRes{sp, resolve_, pathCallback, HIRTypeRef(), {}, emptyFcn};
    mirRes = &topMirRes;

    auto type = params.monomorph(resolve_, item.type);
    const bool isZero = isZeroLiteral(type, encoded, params);

    const bool blobLinkage = item.linkage.type == HIRLinkage::Type::Auto || item.linkage.type == HIRLinkage::Type::Weak;
    // Feeding one C++ initializer per byte to the host compiler turns
    // large Rust constants into enormous source files. GNU assembly
    // can include the evaluated bytes directly and preserve the same
    // link-time value without asking the C++ parser to see them.
    if (!isZero && encoded.bytes.size() >= 64 * 1024 && encoded.relocations.empty() && TargetGetCurSpec(wb_).osName == "linux" && blobLinkage && item.linkage.name.empty() && item.linkage.section.empty() && literalBlobPathIsSafe()) {
        size_t size = 0;
        size_t align = 0;
        MIR_ASSERT(topMirRes, TargetGetSizeAndAlignOf(sp, resolve_, type, size, align), "Unsized static " << p);
        MIR_ASSERT(topMirRes, size == encoded.bytes.size(), "Static size differs from its encoded value: " << size << " != " << encoded.bytes.size());
        if (align < item.explicitAlignment) {
            align = item.explicitAlignment;
        }

        const size_t blobOffset = appendLiteralBlob(encoded);
        const bool weak = item.params.isGeneric() || item.linkage.type == HIRLinkage::Type::Weak;
        of << "__asm__(\n";
        of << "\".pushsection .data\\n\"\n";
        of << "\".balign " << align << "\\n\"\n";
        of << "\"." << (weak ? "weak " : "globl ") << TransMangleValue(p) << "\\n\"\n";
        of << "\".type " << TransMangleValue(p) << ",@object\\n\"\n";
        of << "\"" << TransMangleValue(p) << ":\\n\"\n";
        of << "\".incbin \\\"" << outfilePath << ".blob\\\", " << blobOffset << ", " << encoded.bytes.size() << "\\n\"\n";
        of << "\".size " << TransMangleValue(p) << "," << size << "\\n\"\n";
        of << "\".popsection\\n\");\n";
        mirRes = nullptr;
        return;
    }

    if (item.params.isGeneric()) {
        of << "__attribute__((weak)) ";
    }
    bool isPacked = emitStaticTy(type, p, /*is_proto=*/false, item.explicitAlignment);
    if (item.explicitAlignment != 0) {
        of << " __attribute__((aligned(" << item.explicitAlignment << ")))";
    }
    of << " = ";

    if (isZero) {
        of << "{}";
    } else {
        of << "{ .raw = {";
        if (isPacked) {
            auto relocIt = encoded.relocations.begin();
            auto ptrSize = TargetGetPointerBits() / 8;
            for (size_t i = 0; i < encoded.bytes.size(); i += ptrSize) {
                u64 v = 0;
                // little-endian only (big-endian targets are unsupported)
                for (size_t o = 0; o < ptrSize && i + o < encoded.bytes.size(); o++) {
                    v |= static_cast<u64>(encoded.bytes[i + o]) << (o * 8);
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
                        if (relocIt->p->data.is_UfcsInherent() && relocIt->p->data.as_UfcsInherent().item == "#type_id") {
                            const auto& ty = relocIt->p->data.as_UfcsInherent().type;
                            of << "&__typeid_" << TransMangleTypeId(ty);
                        } else {
                            of << "&";
                            emitReifiedFunctionName(*relocIt->p, relocIt->preserveTrackCaller);
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
    of << ";\n";
    mirRes = nullptr;
}

auto CodeGeneratorC::emitFloat(FloatValue v, HIRCoreType ty) -> void {
    if (ty == HIRCoreType::F16) {
        const F16 bits(v);
        of << "make_f16_bits(0x" << ::std::hex << bits.v << "u)" << ::std::dec;
    } else if (ty == HIRCoreType::F32) {
        const float value = static_cast<float>(v);
        u32 bits;
        ::std::memcpy(&bits, &value, sizeof(bits));
        of << "make_f32_bits(0x" << ::std::hex << bits << "u)" << ::std::dec;
    } else if (ty == HIRCoreType::F64) {
        const double value = static_cast<double>(v);
        u64 bits;
        ::std::memcpy(&bits, &value, sizeof(bits));
        of << "make_f64_bits(0x" << ::std::hex << bits << "ull)" << ::std::dec;
    } else if (ty == HIRCoreType::F128) {
        const F128 bits(v);
        of << "make_f128_bits(0x" << ::std::hex << bits.hi << "ull, 0x" << bits.lo << "ull)" << ::std::dec;
    } else {
        BUG(Span(), "Unexpected floating-point type " << ty);
    }
}

auto CodeGeneratorC::printEscapedString(const std::string& s) -> void {
    printEscapedStringInner(s.c_str(), s.c_str() + s.size());
}

auto CodeGeneratorC::printEscapedString(const std::vector<u8>& s) -> void {
    const char* start = reinterpret_cast<const char*>(s.data());
    printEscapedStringInner(start, start + s.size());
}

auto CodeGeneratorC::printEscapedStringInner(const char* start, const char* end) -> void {
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
                if (' ' <= v && static_cast<u8>(v) < 0x7F) {
                    of << v;
                } else {
                    if (static_cast<u8>(v) < 16) {
                        of << "\\x0" << (unsigned int)static_cast<u8>(v);
                    } else {
                        of << "\\x" << (unsigned int)static_cast<u8>(v);
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

auto CodeGeneratorC::emitFunctionExt(const HIRPath& p, const HIRFunction& item, const TransParams& params) -> void {
    MIRFunction emptyFcn;
    auto pathCallback = makeCallable<MIRPathCb>([&](auto& os) {
        os << "extern fn " << p;
    });
    MIRTypeResolve topMirRes{sp, resolve_, pathCallback, HIRTypeRef(), {}, emptyFcn};
    mirRes = &topMirRes;
    const bool tracksCaller = crate.functionTracksCaller(sp, p, item);
    if (tracksCaller) {
        trackedFunctions.insert(p.clone());
    }

    if (item.linkage.name.rfind("llvm.", 0) == 0) {
        of << "static ";
        emitFunctionHeader(p, item, params);
        of << " {\n";
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
            of << "\tconst u8* src = (const u8*)&arg0;\n"
               << "\tconst u8* mask = (const u8*)&arg1;\n"
               << "\tu8* dst = (u8*)&rv;\n"
               << "\tfor(int i = 0; i < " << 128 / 8 << "; i ++) dst[i] = (mask[i] < 0x80 ? src[mask[i] & 0xF] : 0);\n"
               << "\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.avx2.pshuf.b") {
            of << "\tconst u8* src = (const u8*)&arg0;\n"
               << "\tconst u8* mask = (const u8*)&arg1;\n"
               << "\tu8* dst = (u8*)&rv;\n"
               << "\tfor(int i = 0; i < " << 256 / 8 << "; i ++) dst[i] = (mask[i] < 0x80 ? src[(i & 16) | (mask[i] & 0xF)] : 0);\n"
               << "\treturn rv;\n";
        }
        // Multiply-add intrinsics used by simd-adler32 (via png's flate2)
        else if (item.linkage.name == "llvm.x86.ssse3.pmadd.ub.sw.128" || item.linkage.name == "llvm.x86.avx2.pmadd.ub.sw") {
            int n = (item.linkage.name == "llvm.x86.avx2.pmadd.ub.sw" ? 32 : 16);
            of << "\tconst u8* a = (const u8*)&arg0;\n"
               << "\tconst i8* b = (const i8*)&arg1;\n"
               << "\ti16* dst = (i16*)&rv;\n"
               << "\tfor(int i = 0; i < " << n / 2 << "; i ++) {\n"
               << "\t\ti32 v = (i32)a[2*i]*b[2*i] + (i32)a[2*i+1]*b[2*i+1];\n"
               << "\t\tdst[i] = (i16)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));\n"
               << "\t}\n"
               << "\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.sse2.pmadd.wd" || item.linkage.name == "llvm.x86.avx2.pmadd.wd") {
            int n = (item.linkage.name == "llvm.x86.avx2.pmadd.wd" ? 16 : 8);
            of << "\tconst i16* a = (const i16*)&arg0;\n"
               << "\tconst i16* b = (const i16*)&arg1;\n"
               << "\ti32* dst = (i32*)&rv;\n"
               << "\tfor(int i = 0; i < " << n / 2 << "; i ++) dst[i] = (i32)a[2*i]*b[2*i] + (i32)a[2*i+1]*b[2*i+1];\n"
               << "\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.sse2.psad.bw" || item.linkage.name == "llvm.x86.avx2.psad.bw") {
            int n = (item.linkage.name == "llvm.x86.avx2.psad.bw" ? 32 : 16);
            of << "\tconst u8* a = (const u8*)&arg0;\n"
               << "\tconst u8* b = (const u8*)&arg1;\n"
               << "\tu64* dst = (u64*)&rv;\n"
               << "\tfor(int k = 0; k < " << n / 8 << "; k ++) {\n"
               << "\t\tu64 sum = 0;\n"
               << "\t\tfor(int j = 0; j < 8; j ++) { int d = (int)a[k*8+j] - (int)b[k*8+j]; sum += (d < 0 ? -d : d); }\n"
               << "\t\tdst[k] = sum;\n"
               << "\t}\n"
               << "\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.sse.cmp.ps") {
            of << "\tfloat lhs[4], rhs[4]; u32 result[4];\n"
               << "\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(rhs, &arg1, sizeof(rhs));\n"
               << "\tfor(unsigned i = 0; i < 4; i++) result[i] = trustme_x86_cmp_f32(lhs[i], rhs[i], arg2) ? UINT32_MAX : 0;\n"
               << "\tmemcpy(&rv, result, sizeof(result));\n"
               << "\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.sse.cmp.ss") {
            of << "\tfloat lhs[4], rhs[4]; u32 result[4];\n"
               << "\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(rhs, &arg1, sizeof(rhs)); memcpy(result, &arg0, sizeof(result));\n"
               << "\tresult[0] = trustme_x86_cmp_f32(lhs[0], rhs[0], arg2) ? UINT32_MAX : 0;\n"
               << "\tmemcpy(&rv, result, sizeof(result));\n"
               << "\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.sse.comieq.ss" || item.linkage.name == "llvm.x86.sse.comige.ss" || item.linkage.name == "llvm.x86.sse.comile.ss" || item.linkage.name == "llvm.x86.sse.comilt.ss" || item.linkage.name == "llvm.x86.sse.comineq.ss" || item.linkage.name == "llvm.x86.sse.ucomieq.ss" || item.linkage.name == "llvm.x86.sse.ucomige.ss" || item.linkage.name == "llvm.x86.sse.ucomigt.ss" || item.linkage.name == "llvm.x86.sse.ucomile.ss" || item.linkage.name == "llvm.x86.sse.ucomilt.ss" || item.linkage.name == "llvm.x86.sse.ucomineq.ss") {
            const char* op = nullptr;
            if (item.linkage.name == "llvm.x86.sse.comieq.ss" || item.linkage.name == "llvm.x86.sse.ucomieq.ss") {
                op = "==";
            } else if (item.linkage.name == "llvm.x86.sse.comige.ss" || item.linkage.name == "llvm.x86.sse.ucomige.ss") {
                op = ">=";
            } else if (item.linkage.name == "llvm.x86.sse.ucomigt.ss") {
                op = ">";
            } else if (item.linkage.name == "llvm.x86.sse.comile.ss" || item.linkage.name == "llvm.x86.sse.ucomile.ss") {
                op = "<=";
            } else if (item.linkage.name == "llvm.x86.sse.comilt.ss" || item.linkage.name == "llvm.x86.sse.ucomilt.ss") {
                op = "<";
            } else {
                op = "!=";
            }
            of << "\tfloat lhs[4], rhs[4];\n"
               << "\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(rhs, &arg1, sizeof(rhs));\n"
               << "\treturn lhs[0] " << op << " rhs[0];\n";
        } else if (item.linkage.name == "llvm.x86.sse.cvtsi2ss" || item.linkage.name == "llvm.x86.sse.cvtsi642ss") {
            of << "\tfloat result[4];\n"
               << "\tmemcpy(result, &arg0, sizeof(result)); result[0] = (float)arg1;\n"
               << "\tmemcpy(&rv, result, sizeof(result));\n"
               << "\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.sse.cvtss2si" || item.linkage.name == "llvm.x86.sse.cvttss2si" || item.linkage.name == "llvm.x86.sse.cvtss2si64" || item.linkage.name == "llvm.x86.sse.cvttss2si64") {
            const bool truncate = item.linkage.name == "llvm.x86.sse.cvttss2si" || item.linkage.name == "llvm.x86.sse.cvttss2si64";
            const bool is64 = item.linkage.name == "llvm.x86.sse.cvtss2si64" || item.linkage.name == "llvm.x86.sse.cvttss2si64";
            of << "\tfloat input[4]; memcpy(input, &arg0, sizeof(input));\n"
               << "\treturn trustme_x86_f32_to_i" << (is64 ? 64 : 32) << "(input[0], " << truncate << ");\n";
        } else if (item.linkage.name == "llvm.x86.sse.min.ps" || item.linkage.name == "llvm.x86.sse.min.ss" || item.linkage.name == "llvm.x86.sse.max.ps" || item.linkage.name == "llvm.x86.sse.max.ss") {
            const bool isMin = item.linkage.name == "llvm.x86.sse.min.ps" || item.linkage.name == "llvm.x86.sse.min.ss";
            const bool scalar = item.linkage.name == "llvm.x86.sse.min.ss" || item.linkage.name == "llvm.x86.sse.max.ss";
            of << "\tfloat lhs[4], rhs[4], result[4];\n"
               << "\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(rhs, &arg1, sizeof(rhs)); memcpy(result, &arg0, sizeof(result));\n"
               << "\tfor(unsigned i = 0; i < " << (scalar ? 1 : 4) << "; i++) result[i] = lhs[i] " << (isMin ? "<" : ">") << " rhs[i] ? lhs[i] : rhs[i];\n"
               << "\tmemcpy(&rv, result, sizeof(result));\n"
               << "\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.sse.rcp.ps" || item.linkage.name == "llvm.x86.sse.rcp.ss" || item.linkage.name == "llvm.x86.sse.rsqrt.ps" || item.linkage.name == "llvm.x86.sse.rsqrt.ss") {
            const bool reciprocalSqrt = item.linkage.name == "llvm.x86.sse.rsqrt.ps" || item.linkage.name == "llvm.x86.sse.rsqrt.ss";
            const bool scalar = item.linkage.name == "llvm.x86.sse.rcp.ss" || item.linkage.name == "llvm.x86.sse.rsqrt.ss";
            of << "\tfloat result[4]; memcpy(result, &arg0, sizeof(result));\n"
               << "\tfor(unsigned i = 0; i < " << (scalar ? 1 : 4) << "; i++) result[i] = 1.0f / ";
            if (reciprocalSqrt) {
                of << "__builtin_sqrtf(result[i])";
            } else {
                of << "result[i]";
            }
            of << ";\n\tmemcpy(&rv, result, sizeof(result));\n\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.sse2.cmp.pd") {
            of << "\tdouble lhs[2], rhs[2]; u64 result[2];\n"
               << "\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(rhs, &arg1, sizeof(rhs));\n"
               << "\tfor(unsigned i = 0; i < 2; i++) result[i] = trustme_x86_cmp_f64(lhs[i], rhs[i], arg2) ? UINT64_MAX : 0;\n"
               << "\tmemcpy(&rv, result, sizeof(result));\n"
               << "\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.sse2.cmp.sd") {
            of << "\tdouble lhs[2], rhs[2]; u64 result[2];\n"
               << "\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(rhs, &arg1, sizeof(rhs)); memcpy(result, &arg0, sizeof(result));\n"
               << "\tresult[0] = trustme_x86_cmp_f64(lhs[0], rhs[0], arg2) ? UINT64_MAX : 0;\n"
               << "\tmemcpy(&rv, result, sizeof(result));\n"
               << "\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.sse2.comieq.sd" || item.linkage.name == "llvm.x86.sse2.comige.sd" || item.linkage.name == "llvm.x86.sse2.comigt.sd" || item.linkage.name == "llvm.x86.sse2.comile.sd" || item.linkage.name == "llvm.x86.sse2.comilt.sd" || item.linkage.name == "llvm.x86.sse2.comineq.sd" || item.linkage.name == "llvm.x86.sse2.ucomieq.sd" || item.linkage.name == "llvm.x86.sse2.ucomige.sd" || item.linkage.name == "llvm.x86.sse2.ucomigt.sd" || item.linkage.name == "llvm.x86.sse2.ucomile.sd" || item.linkage.name == "llvm.x86.sse2.ucomilt.sd" || item.linkage.name == "llvm.x86.sse2.ucomineq.sd") {
            const char* op = nullptr;
            if (item.linkage.name == "llvm.x86.sse2.comieq.sd" || item.linkage.name == "llvm.x86.sse2.ucomieq.sd") {
                op = "==";
            } else if (item.linkage.name == "llvm.x86.sse2.comige.sd" || item.linkage.name == "llvm.x86.sse2.ucomige.sd") {
                op = ">=";
            } else if (item.linkage.name == "llvm.x86.sse2.comigt.sd" || item.linkage.name == "llvm.x86.sse2.ucomigt.sd") {
                op = ">";
            } else if (item.linkage.name == "llvm.x86.sse2.comile.sd" || item.linkage.name == "llvm.x86.sse2.ucomile.sd") {
                op = "<=";
            } else if (item.linkage.name == "llvm.x86.sse2.comilt.sd" || item.linkage.name == "llvm.x86.sse2.ucomilt.sd") {
                op = "<";
            } else {
                op = "!=";
            }
            of << "\tdouble lhs[2], rhs[2];\n"
               << "\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(rhs, &arg1, sizeof(rhs));\n"
               << "\treturn lhs[0] " << op << " rhs[0];\n";
        } else if (item.linkage.name == "llvm.x86.sse2.cvtpd2dq" || item.linkage.name == "llvm.x86.sse2.cvttpd2dq" || item.linkage.name == "llvm.x86.sse2.cvtps2dq" || item.linkage.name == "llvm.x86.sse2.cvttps2dq") {
            const bool inputIsDouble = item.linkage.name == "llvm.x86.sse2.cvtpd2dq" || item.linkage.name == "llvm.x86.sse2.cvttpd2dq";
            const bool truncate = item.linkage.name == "llvm.x86.sse2.cvttpd2dq" || item.linkage.name == "llvm.x86.sse2.cvttps2dq";
            if (inputIsDouble) {
                of << "\tdouble input[2]; i32 result[4] = {0, 0, 0, 0}; memcpy(input, &arg0, sizeof(input));\n"
                   << "\tfor(unsigned i = 0; i < 2; i++) result[i] = trustme_x86_f64_to_i32(input[i], " << truncate << ");\n";
            } else {
                of << "\tfloat input[4]; i32 result[4]; memcpy(input, &arg0, sizeof(input));\n"
                   << "\tfor(unsigned i = 0; i < 4; i++) result[i] = trustme_x86_f32_to_i32(input[i], " << truncate << ");\n";
            }
            of << "\tmemcpy(&rv, result, sizeof(result));\n\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.sse2.cvtsd2si" || item.linkage.name == "llvm.x86.sse2.cvttsd2si" || item.linkage.name == "llvm.x86.sse2.cvtsd2si64" || item.linkage.name == "llvm.x86.sse2.cvttsd2si64") {
            const bool truncate = item.linkage.name == "llvm.x86.sse2.cvttsd2si" || item.linkage.name == "llvm.x86.sse2.cvttsd2si64";
            const bool is64 = item.linkage.name == "llvm.x86.sse2.cvtsd2si64" || item.linkage.name == "llvm.x86.sse2.cvttsd2si64";
            of << "\tdouble input[2]; memcpy(input, &arg0, sizeof(input));\n"
               << "\treturn trustme_x86_f64_to_i" << (is64 ? 64 : 32) << "(input[0], " << truncate << ");\n";
        } else if (item.linkage.name == "llvm.x86.sse2.cvtsd2ss") {
            of << "\tfloat result[4]; double input[2];\n"
               << "\tmemcpy(result, &arg0, sizeof(result)); memcpy(input, &arg1, sizeof(input)); result[0] = (float)input[0];\n"
               << "\tmemcpy(&rv, result, sizeof(result));\n\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.sse2.cvtss2sd") {
            of << "\tdouble result[2]; float input[4];\n"
               << "\tmemcpy(result, &arg0, sizeof(result)); memcpy(input, &arg1, sizeof(input)); result[0] = (double)input[0];\n"
               << "\tmemcpy(&rv, result, sizeof(result));\n\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.sse2.min.pd" || item.linkage.name == "llvm.x86.sse2.min.sd" || item.linkage.name == "llvm.x86.sse2.max.pd" || item.linkage.name == "llvm.x86.sse2.max.sd") {
            const bool isMin = item.linkage.name == "llvm.x86.sse2.min.pd" || item.linkage.name == "llvm.x86.sse2.min.sd";
            const bool scalar = item.linkage.name == "llvm.x86.sse2.min.sd" || item.linkage.name == "llvm.x86.sse2.max.sd";
            of << "\tdouble lhs[2], rhs[2], result[2];\n"
               << "\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(rhs, &arg1, sizeof(rhs)); memcpy(result, &arg0, sizeof(result));\n"
               << "\tfor(unsigned i = 0; i < " << (scalar ? 1 : 2) << "; i++) result[i] = lhs[i] " << (isMin ? "<" : ">") << " rhs[i] ? lhs[i] : rhs[i];\n"
               << "\tmemcpy(&rv, result, sizeof(result));\n\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.sse2.packssdw.128") {
            of << "\ti32 lhs[4], rhs[4]; i16 result[8];\n"
               << "\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(rhs, &arg1, sizeof(rhs));\n"
               << "\tfor(unsigned i = 0; i < 8; i++) { i32 value = i < 4 ? lhs[i] : rhs[i - 4]; result[i] = value > INT16_MAX ? INT16_MAX : (value < INT16_MIN ? INT16_MIN : (i16)value); }\n"
               << "\tmemcpy(&rv, result, sizeof(result));\n\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.sse2.packsswb.128" || item.linkage.name == "llvm.x86.sse2.packuswb.128") {
            const bool unsignedResult = item.linkage.name == "llvm.x86.sse2.packuswb.128";
            of << "\ti16 lhs[8], rhs[8]; " << (unsignedResult ? "u8" : "i8") << " result[16];\n"
               << "\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(rhs, &arg1, sizeof(rhs));\n"
               << "\tfor(unsigned i = 0; i < 16; i++) { i16 value = i < 8 ? lhs[i] : rhs[i - 8]; ";
            if (unsignedResult) {
                of << "result[i] = value > UINT8_MAX ? UINT8_MAX : (value < 0 ? 0 : (u8)value);";
            } else {
                of << "result[i] = value > INT8_MAX ? INT8_MAX : (value < INT8_MIN ? INT8_MIN : (i8)value);";
            }
            of << " }\n\tmemcpy(&rv, result, sizeof(result));\n\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.sse2.psll.w" || item.linkage.name == "llvm.x86.sse2.psll.d" || item.linkage.name == "llvm.x86.sse2.psll.q" || item.linkage.name == "llvm.x86.sse2.psrl.w" || item.linkage.name == "llvm.x86.sse2.psrl.d" || item.linkage.name == "llvm.x86.sse2.psrl.q" || item.linkage.name == "llvm.x86.sse2.psra.w" || item.linkage.name == "llvm.x86.sse2.psra.d") {
            const bool left = item.linkage.name.compare(14, 4, "psll") == 0;
            const bool arithmetic = item.linkage.name.compare(14, 4, "psra") == 0;
            const unsigned bits = item.linkage.name.back() == 'w' ? 16 : (item.linkage.name.back() == 'd' ? 32 : 64);
            of << "\tu64 count_words[2]; memcpy(count_words, &arg1, sizeof(count_words)); u64 count = count_words[0];\n"
               << "\tu" << bits << " input[" << 128 / bits << "], result[" << 128 / bits << "]; memcpy(input, &arg0, sizeof(input));\n"
               << "\tfor(unsigned i = 0; i < " << 128 / bits << "; i++) {\n";
            if (arithmetic) {
                of << "\t\tif(count >= " << bits << ") result[i] = input[i] >> " << bits - 1 << " ? UINT" << bits << "_MAX : 0;\n"
                   << "\t\telse if(count == 0) result[i] = input[i];\n"
                   << "\t\telse { result[i] = input[i] >> count; if(input[i] >> " << bits - 1 << ") result[i] |= UINT" << bits << "_MAX << (" << bits << " - count); }\n";
            } else {
                of << "\t\tresult[i] = count >= " << bits << " ? 0 : input[i] " << (left ? "<<" : ">>") << " count;\n";
            }
            of << "\t}\n\tmemcpy(&rv, result, sizeof(result));\n\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.sse41.dpps") {
            of << "\tfloat lhs[4], rhs[4], product[4] = {0, 0, 0, 0}, result[4];\n"
               << "\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(rhs, &arg1, sizeof(rhs));\n"
               << "\tfor(unsigned i = 0; i < 4; i++) if(arg2 & (1 << (i + 4))) product[i] = lhs[i] * rhs[i];\n"
               << "\tfloat sum = (product[0] + product[1]) + (product[2] + product[3]);\n"
               << "\tfor(unsigned i = 0; i < 4; i++) result[i] = arg2 & (1 << i) ? sum : 0.0f;\n"
               << "\tmemcpy(&rv, result, sizeof(result));\n\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.sse41.dppd") {
            of << "\tdouble lhs[2], rhs[2], product[2] = {0, 0}, result[2];\n"
               << "\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(rhs, &arg1, sizeof(rhs));\n"
               << "\tfor(unsigned i = 0; i < 2; i++) if(arg2 & (1 << (i + 4))) product[i] = lhs[i] * rhs[i];\n"
               << "\tdouble sum = product[0] + product[1];\n"
               << "\tfor(unsigned i = 0; i < 2; i++) result[i] = arg2 & (1 << i) ? sum : 0.0;\n"
               << "\tmemcpy(&rv, result, sizeof(result));\n\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.sse41.insertps") {
            of << "\tu32 lhs[4], rhs[4], result[4];\n"
               << "\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(rhs, &arg1, sizeof(rhs)); memcpy(result, lhs, sizeof(result));\n"
               << "\tresult[(arg2 >> 4) & 3] = rhs[(arg2 >> 6) & 3];\n"
               << "\tfor(unsigned i = 0; i < 4; i++) if(arg2 & (1 << i)) result[i] = 0;\n"
               << "\tmemcpy(&rv, result, sizeof(result));\n\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.sse41.mpsadbw") {
            of << "\tu8 lhs[16], rhs[16]; u16 result[8];\n"
               << "\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(rhs, &arg1, sizeof(rhs));\n"
               << "\tunsigned lhs_start = arg2 & 4 ? 4 : 0; unsigned rhs_start = (arg2 & 3) * 4;\n"
               << "\tfor(unsigned i = 0; i < 8; i++) {\n"
               << "\t\tresult[i] = 0;\n"
               << "\t\tfor(unsigned j = 0; j < 4; j++) { int d = (int)lhs[lhs_start + i + j] - (int)rhs[rhs_start + j]; result[i] += d < 0 ? -d : d; }\n"
               << "\t}\n\tmemcpy(&rv, result, sizeof(result));\n\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.sse41.packusdw") {
            of << "\ti32 lhs[4], rhs[4]; u16 result[8];\n"
               << "\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(rhs, &arg1, sizeof(rhs));\n"
               << "\tfor(unsigned i = 0; i < 8; i++) { i32 value = i < 4 ? lhs[i] : rhs[i - 4]; result[i] = value > UINT16_MAX ? UINT16_MAX : (value < 0 ? 0 : (u16)value); }\n"
               << "\tmemcpy(&rv, result, sizeof(result));\n\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.sse41.phminposuw") {
            of << "\tu16 input[8], result[8] = {0, 0, 0, 0, 0, 0, 0, 0}; memcpy(input, &arg0, sizeof(input));\n"
               << "\tresult[0] = input[0];\n"
               << "\tfor(unsigned i = 1; i < 8; i++) if(input[i] < result[0]) { result[0] = input[i]; result[1] = i; }\n"
               << "\tmemcpy(&rv, result, sizeof(result));\n\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.sse41.ptestz" || item.linkage.name == "llvm.x86.sse41.ptestc" || item.linkage.name == "llvm.x86.sse41.ptestnzc") {
            of << "\tu64 lhs[2], rhs[2]; memcpy(lhs, &arg0, sizeof(lhs)); memcpy(rhs, &arg1, sizeof(rhs));\n"
               << "\tbool intersection = (lhs[0] & rhs[0]) != 0 || (lhs[1] & rhs[1]) != 0;\n"
               << "\tbool outside = (~lhs[0] & rhs[0]) != 0 || (~lhs[1] & rhs[1]) != 0;\n";
            if (item.linkage.name == "llvm.x86.sse41.ptestz") {
                of << "\treturn !intersection;\n";
            } else if (item.linkage.name == "llvm.x86.sse41.ptestc") {
                of << "\treturn !outside;\n";
            } else {
                of << "\treturn intersection && outside;\n";
            }
        } else if (item.linkage.name == "llvm.x86.sse41.round.ps" || item.linkage.name == "llvm.x86.sse41.round.ss") {
            const bool scalar = item.linkage.name == "llvm.x86.sse41.round.ss";
            of << "\tfloat input[4], result[4]; memcpy(input, &arg" << (scalar ? 1 : 0) << ", sizeof(input)); memcpy(result, &arg0, sizeof(result));\n"
               << "\tfor(unsigned i = 0; i < " << (scalar ? 1 : 4) << "; i++) result[i] = trustme_x86_round_f32(input[i], arg" << (scalar ? 2 : 1) << ");\n"
               << "\tmemcpy(&rv, result, sizeof(result));\n\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.sse41.round.pd" || item.linkage.name == "llvm.x86.sse41.round.sd") {
            const bool scalar = item.linkage.name == "llvm.x86.sse41.round.sd";
            of << "\tdouble input[2], result[2]; memcpy(input, &arg" << (scalar ? 1 : 0) << ", sizeof(input)); memcpy(result, &arg0, sizeof(result));\n"
               << "\tfor(unsigned i = 0; i < " << (scalar ? 1 : 2) << "; i++) result[i] = trustme_x86_round_f64(input[i], arg" << (scalar ? 2 : 1) << ");\n"
               << "\tmemcpy(&rv, result, sizeof(result));\n\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.sse42.crc32.32.8" || item.linkage.name == "llvm.x86.sse42.crc32.32.16" || item.linkage.name == "llvm.x86.sse42.crc32.32.32" || item.linkage.name == "llvm.x86.sse42.crc32.64.64") {
            const unsigned bits = item.linkage.name == "llvm.x86.sse42.crc32.32.8" ? 8 : (item.linkage.name == "llvm.x86.sse42.crc32.32.16" ? 16 : (item.linkage.name == "llvm.x86.sse42.crc32.32.32" ? 32 : 64));
            of << "\treturn trustme_x86_crc32c((u32)arg0, arg1, " << bits << ");\n";
        } else if (item.linkage.name.rfind("llvm.x86.sse42.pcmp", 0) == 0) {
            const bool explicitLengths = item.linkage.name.find("pcmpestr") != ::std::string::npos;
            const char* control = explicitLengths ? "arg4" : "arg2";
            if (explicitLengths) {
                of << "\ttrustme_x86_pcmp_state state = trustme_x86_pcmp(&arg0, arg1, &arg2, arg3, arg4, true);\n";
            } else {
                of << "\ttrustme_x86_pcmp_state state = trustme_x86_pcmp(&arg0, 0, &arg1, 0, arg2, false);\n";
            }
            if (item.linkage.name.find("pcmpestrm128") != ::std::string::npos || item.linkage.name.find("pcmpistrm128") != ::std::string::npos) {
                of << "\ttrustme_x86_pcmp_mask(&rv, state, " << control << ");\n\treturn rv;\n";
            } else if (item.linkage.name.find("pcmpestri128") != ::std::string::npos || item.linkage.name.find("pcmpistri128") != ::std::string::npos) {
                of << "\treturn trustme_x86_pcmp_index(state, " << control << ");\n";
            } else if (item.linkage.name.find("pcmpestria128") != ::std::string::npos || item.linkage.name.find("pcmpistria128") != ::std::string::npos) {
                of << "\treturn state.mask == 0 && state.len2 == state.count;\n";
            } else if (item.linkage.name.find("pcmpestric128") != ::std::string::npos || item.linkage.name.find("pcmpistric128") != ::std::string::npos) {
                of << "\treturn state.mask != 0;\n";
            } else if (item.linkage.name.find("pcmpestrio128") != ::std::string::npos || item.linkage.name.find("pcmpistrio128") != ::std::string::npos) {
                of << "\treturn state.mask & 1;\n";
            } else if (item.linkage.name.find("pcmpestris128") != ::std::string::npos || item.linkage.name.find("pcmpistris128") != ::std::string::npos) {
                of << "\treturn state.len1 < state.count;\n";
            } else if (item.linkage.name.find("pcmpestriz128") != ::std::string::npos || item.linkage.name.find("pcmpistriz128") != ::std::string::npos) {
                of << "\treturn state.len2 < state.count;\n";
            } else {
                BUG(sp, "Unknown SSE4.2 string comparison intrinsic " << item.linkage.name);
            }
        } else if (item.linkage.name == "llvm.x86.sse3.hadd.ps" || item.linkage.name == "llvm.x86.sse3.hsub.ps") {
            const char op = item.linkage.name == "llvm.x86.sse3.hadd.ps" ? '+' : '-';
            of << "\tfloat lhs[4], rhs[4], result[4];\n"
               << "\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(rhs, &arg1, sizeof(rhs));\n"
               << "\tfor(unsigned i = 0; i < 2; i++) { result[i] = lhs[2*i] " << op << " lhs[2*i+1]; result[i+2] = rhs[2*i] " << op << " rhs[2*i+1]; }\n"
               << "\tmemcpy(&rv, result, sizeof(result));\n"
               << "\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.sse3.hadd.pd" || item.linkage.name == "llvm.x86.sse3.hsub.pd") {
            const char op = item.linkage.name == "llvm.x86.sse3.hadd.pd" ? '+' : '-';
            of << "\tdouble lhs[2], rhs[2], result[2];\n"
               << "\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(rhs, &arg1, sizeof(rhs));\n"
               << "\tresult[0] = lhs[0] " << op << " lhs[1]; result[1] = rhs[0] " << op << " rhs[1];\n"
               << "\tmemcpy(&rv, result, sizeof(result));\n"
               << "\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.sse3.ldu.dq") {
            of << "\tmemcpy(&rv, arg0, sizeof(rv));\n"
               << "\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.ssse3.phadd.d.128" || item.linkage.name == "llvm.x86.ssse3.phsub.d.128") {
            const char op = item.linkage.name == "llvm.x86.ssse3.phadd.d.128" ? '+' : '-';
            of << "\tu32 lhs[4], rhs[4], result[4];\n"
               << "\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(rhs, &arg1, sizeof(rhs));\n"
               << "\tfor(unsigned i = 0; i < 2; i++) { result[i] = lhs[2*i] " << op << " lhs[2*i+1]; result[i+2] = rhs[2*i] " << op << " rhs[2*i+1]; }\n"
               << "\tmemcpy(&rv, result, sizeof(result));\n"
               << "\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.ssse3.phadd.w.128" || item.linkage.name == "llvm.x86.ssse3.phsub.w.128") {
            const char op = item.linkage.name == "llvm.x86.ssse3.phadd.w.128" ? '+' : '-';
            of << "\tu16 lhs[8], rhs[8], result[8];\n"
               << "\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(rhs, &arg1, sizeof(rhs));\n"
               << "\tfor(unsigned i = 0; i < 4; i++) { result[i] = lhs[2*i] " << op << " lhs[2*i+1]; result[i+4] = rhs[2*i] " << op << " rhs[2*i+1]; }\n"
               << "\tmemcpy(&rv, result, sizeof(result));\n"
               << "\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.ssse3.phadd.sw.128" || item.linkage.name == "llvm.x86.ssse3.phsub.sw.128") {
            const char op = item.linkage.name == "llvm.x86.ssse3.phadd.sw.128" ? '+' : '-';
            of << "\ti16 lhs[8], rhs[8], result[8];\n"
               << "\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(rhs, &arg1, sizeof(rhs));\n"
               << "\tfor(unsigned i = 0; i < 4; i++) {\n"
               << "\t\ti32 a = (i32)lhs[2*i] " << op << " lhs[2*i+1]; i32 b = (i32)rhs[2*i] " << op << " rhs[2*i+1];\n"
               << "\t\tresult[i] = (i16)(a > INT16_MAX ? INT16_MAX : (a < INT16_MIN ? INT16_MIN : a));\n"
               << "\t\tresult[i+4] = (i16)(b > INT16_MAX ? INT16_MAX : (b < INT16_MIN ? INT16_MIN : b));\n"
               << "\t}\n"
               << "\tmemcpy(&rv, result, sizeof(result));\n"
               << "\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.ssse3.pmul.hr.sw.128") {
            of << "\ti16 lhs[8], rhs[8], result[8];\n"
               << "\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(rhs, &arg1, sizeof(rhs));\n"
               << "\tfor(unsigned i = 0; i < 8; i++) {\n"
               << "\t\ti32 value = ((i32)lhs[i] * rhs[i] + 0x4000) >> 15;\n"
               << "\t\tresult[i] = (i16)value;\n"
               << "\t}\n"
               << "\tmemcpy(&rv, result, sizeof(result));\n"
               << "\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.ssse3.psign.b.128") {
            of << "\tu8 lhs[16], result[16]; i8 signs[16];\n"
               << "\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(signs, &arg1, sizeof(signs));\n"
               << "\tfor(unsigned i = 0; i < 16; i++) result[i] = signs[i] == 0 ? 0 : (signs[i] < 0 ? 0 - lhs[i] : lhs[i]);\n"
               << "\tmemcpy(&rv, result, sizeof(result));\n"
               << "\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.ssse3.psign.w.128") {
            of << "\tu16 lhs[8], result[8]; i16 signs[8];\n"
               << "\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(signs, &arg1, sizeof(signs));\n"
               << "\tfor(unsigned i = 0; i < 8; i++) result[i] = signs[i] == 0 ? 0 : (signs[i] < 0 ? 0 - lhs[i] : lhs[i]);\n"
               << "\tmemcpy(&rv, result, sizeof(result));\n"
               << "\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.ssse3.psign.d.128") {
            of << "\tu32 lhs[4], result[4]; i32 signs[4];\n"
               << "\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(signs, &arg1, sizeof(signs));\n"
               << "\tfor(unsigned i = 0; i < 4; i++) result[i] = signs[i] == 0 ? 0 : (signs[i] < 0 ? 0 - lhs[i] : lhs[i]);\n"
               << "\tmemcpy(&rv, result, sizeof(result));\n"
               << "\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.sse2.psrli.d") {
            of << "\tconst u32* src = (const u32*)&arg0;\n"
               << "\tu32* dst = (u32*)&rv;\n"
               << "\tfor(int i = 0; i < " << 128 / 32 << "; i ++) dst[i] = src[i] >> arg1;\n"
               << "\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.sse2.pslli.d") {
            of << "\tconst u32* src = (const u32*)&arg0;\n"
               << "\tu32* dst = (u32*)&rv;\n"
               << "\tfor(int i = 0; i < " << 128 / 32 << "; i ++) dst[i] = src[i] << arg1;\n"
               << "\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.sse2.pmovmskb.128") {
            of << "\tconst u8* src = (const u8*)&arg0;\n"
               << "\tu8* dst = (u8*)&rv; *dst = 0;\n"
               << "\tfor(int i = 0; i < " << 128 / 8 << "; i ++) *dst |= (src[i] >> 7) << i;\n"
               << "\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.sse2.storeu.dq") {
            of << "\tmemcpy(arg0, &arg1, sizeof(arg1));\n";
        }
        // SHA-NI: the sha2 crate takes this path when runtime detection
        // reports hardware support; portable C keeps it correct.
        else if (item.linkage.name == "llvm.x86.sha256rnds2") {
            of << "\tconst u32* st_cdgh = (const u32*)&arg0;\n"
               << "\tconst u32* st_abef = (const u32*)&arg1;\n"
               << "\tconst u32* wk = (const u32*)&arg2;\n"
               << "\tu32* dst = (u32*)&rv;\n"
               << "\tu32 a = st_abef[3], b = st_abef[2], e = st_abef[1], f = st_abef[0];\n"
               << "\tu32 c = st_cdgh[3], d = st_cdgh[2], g = st_cdgh[1], h = st_cdgh[0];\n"
               << "\tfor(int i = 0; i < 2; i ++) {\n"
               << "\t\tu32 ch = (e & f) ^ (~e & g);\n"
               << "\t\tu32 maj = (a & b) ^ (a & c) ^ (b & c);\n"
               << "\t\tu32 s0 = (a >> 2 | a << 30) ^ (a >> 13 | a << 19) ^ (a >> 22 | a << 10);\n"
               << "\t\tu32 s1 = (e >> 6 | e << 26) ^ (e >> 11 | e << 21) ^ (e >> 25 | e << 7);\n"
               << "\t\tu32 t = ch + s1 + wk[i] + h;\n"
               << "\t\th = g; g = f; f = e; e = t + d; d = c; c = b; b = a; a = t + maj + s0;\n"
               << "\t}\n"
               << "\tdst[3] = a; dst[2] = b; dst[1] = e; dst[0] = f;\n"
               << "\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.sha256msg1") {
            of << "\tconst u32* w = (const u32*)&arg0;\n"
               << "\tconst u32* w2 = (const u32*)&arg1;\n"
               << "\tu32* dst = (u32*)&rv;\n"
               << "\tfor(int i = 0; i < 4; i ++) {\n"
               << "\t\tu32 x = (i < 3 ? w[i+1] : w2[0]);\n"
               << "\t\tdst[i] = w[i] + ((x >> 7 | x << 25) ^ (x >> 18 | x << 14) ^ (x >> 3));\n"
               << "\t}\n"
               << "\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.sha256msg2") {
            of << "\tconst u32* w = (const u32*)&arg0;\n"
               << "\tconst u32* prev = (const u32*)&arg1;\n"
               << "\tu32* dst = (u32*)&rv;\n"
               << "\tu32 w14 = prev[2], w15 = prev[3];\n"
               << "\tu32 w16 = w[0] + ((w14 >> 17 | w14 << 15) ^ (w14 >> 19 | w14 << 13) ^ (w14 >> 10));\n"
               << "\tu32 w17 = w[1] + ((w15 >> 17 | w15 << 15) ^ (w15 >> 19 | w15 << 13) ^ (w15 >> 10));\n"
               << "\tu32 w18 = w[2] + ((w16 >> 17 | w16 << 15) ^ (w16 >> 19 | w16 << 13) ^ (w16 >> 10));\n"
               << "\tu32 w19 = w[3] + ((w17 >> 17 | w17 << 15) ^ (w17 >> 19 | w17 << 13) ^ (w17 >> 10));\n"
               << "\tdst[0] = w16; dst[1] = w17; dst[2] = w18; dst[3] = w19;\n"
               << "\treturn rv;\n";
        }
        // Bit-manipulation intrinsics. Keep these portable: callers
        // may use runtime feature detection, but the generated C++
        // itself must not require BMI instructions.
        else if (item.linkage.name == "llvm.x86.bmi.bextr.32") {
            of << "\tu32 start = arg1 & 0xff;\n"
               << "\tu32 length = (arg1 >> 8) & 0xff;\n"
               << "\tif(start >= 32 || length == 0) return 0;\n"
               << "\tif(length > 32 - start) length = 32 - start;\n"
               << "\treturn (arg0 >> start) & (UINT32_MAX >> (32 - length));\n";
        } else if (item.linkage.name == "llvm.x86.bmi.bextr.64") {
            of << "\tu64 start = arg1 & 0xff;\n"
               << "\tu64 length = (arg1 >> 8) & 0xff;\n"
               << "\tif(start >= 64 || length == 0) return 0;\n"
               << "\tif(length > 64 - start) length = 64 - start;\n"
               << "\treturn (arg0 >> start) & (UINT64_MAX >> (64 - length));\n";
        } else if (item.linkage.name == "llvm.x86.bmi.bzhi.32") {
            of << "\tu32 index = arg1 & 0xff;\n"
               << "\tif(index >= 32) return arg0;\n"
               << "\treturn index == 0 ? 0 : arg0 & (UINT32_MAX >> (32 - index));\n";
        } else if (item.linkage.name == "llvm.x86.bmi.bzhi.64") {
            of << "\tu64 index = arg1 & 0xff;\n"
               << "\tif(index >= 64) return arg0;\n"
               << "\treturn index == 0 ? 0 : arg0 & (UINT64_MAX >> (64 - index));\n";
        } else if (item.linkage.name == "llvm.x86.bmi.pext.32") {
            of << "\trv = 0;\n"
               << "\tu32 output_bit = 1;\n"
               << "\twhile(arg1) {\n"
               << "\t\tu32 mask_bit = arg1 & -arg1;\n"
               << "\t\tif(arg0 & mask_bit) rv |= output_bit;\n"
               << "\t\targ1 &= arg1 - 1; output_bit <<= 1;\n"
               << "\t}\n"
               << "\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.bmi.pext.64") {
            of << "\trv = 0;\n"
               << "\tu64 output_bit = 1;\n"
               << "\twhile(arg1) {\n"
               << "\t\tu64 mask_bit = arg1 & -arg1;\n"
               << "\t\tif(arg0 & mask_bit) rv |= output_bit;\n"
               << "\t\targ1 &= arg1 - 1; output_bit <<= 1;\n"
               << "\t}\n"
               << "\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.bmi.pdep.32") {
            of << "\trv = 0;\n"
               << "\tu32 input_bit = 1;\n"
               << "\twhile(arg1) {\n"
               << "\t\tu32 mask_bit = arg1 & -arg1;\n"
               << "\t\tif(arg0 & input_bit) rv |= mask_bit;\n"
               << "\t\targ1 &= arg1 - 1; input_bit <<= 1;\n"
               << "\t}\n"
               << "\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.bmi.pdep.64") {
            of << "\trv = 0;\n"
               << "\tu64 input_bit = 1;\n"
               << "\twhile(arg1) {\n"
               << "\t\tu64 mask_bit = arg1 & -arg1;\n"
               << "\t\tif(arg0 & input_bit) rv |= mask_bit;\n"
               << "\t\targ1 &= arg1 - 1; input_bit <<= 1;\n"
               << "\t}\n"
               << "\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.pclmulqdq") {
            of << "\tu64 a_words[2], b_words[2], result[2] = {0, 0};\n"
               << "\tmemcpy(a_words, &arg0, sizeof(a_words));\n"
               << "\tmemcpy(b_words, &arg1, sizeof(b_words));\n"
               << "\tu64 a = a_words[arg2 & 1];\n"
               << "\tu64 b = b_words[(arg2 >> 4) & 1];\n"
               << "\tfor(unsigned i = 0; i < 64; i++) {\n"
               << "\t\tif((b >> i) & 1) {\n"
               << "\t\t\tresult[0] ^= a << i;\n"
               << "\t\t\tif(i != 0) result[1] ^= a >> (64 - i);\n"
               << "\t\t}\n"
               << "\t}\n"
               << "\tmemcpy(&rv, result, sizeof(result));\n"
               << "\treturn rv;\n";
        }
        // Add with carry
        // `fn llvm_addcarry_u32(a: u8, b: u32, c: u32) -> (u8, u32)`
        else if (item.linkage.name == "llvm.x86.addcarry.32") {
            of << "\trv._0 = __builtin_add_overflow(arg1, arg2, &rv._1);\n";
            of << "\tif(arg0) rv._0 |= __builtin_add_overflow(rv._1, 1, &rv._1);\n";
            of << "\treturn rv;\n";
        }
        // `fn llvm_addcarry_u64(a: u8, b: u64, c: u64) -> (u8, u64)`
        else if (item.linkage.name == "llvm.x86.addcarry.64") {
            of << "\trv._0 = __builtin_add_overflow(arg1, arg2, &rv._1);\n";
            of << "\tif(arg0) rv._0 |= __builtin_add_overflow(rv._1, 1, &rv._1);\n";
            of << "\treturn rv;\n";
        }
        // `fn llvm_addcarryx_u32(a: u8, b: u32, c: u32, d: *mut u8) -> u8`
        else if (item.linkage.name == "llvm.x86.addcarryx.u32") {
            of << "\trv = __builtin_add_overflow(arg1, arg2, (u32*)arg3);\n";
            of << "\tif(arg0) rv |= __builtin_add_overflow(*arg3, 1, (u32*)arg3);\n";
            of << "\treturn rv;\n";
        }
        // `fn llvm_addcarryx_u64(a: u8, b: u64, c: u64, d: *mut u64) -> u8`
        else if (item.linkage.name == "llvm.x86.addcarryx.u64") {
            of << "\trv = __builtin_add_overflow(arg1, arg2, (u64*)arg3);\n";
            of << "\tif(arg0) rv |= __builtin_add_overflow(*arg3, 1, (u64*)arg3);\n";
            of << "\treturn rv;\n";
        }
        // `fn llvm_subborrow(a: u8, b: u32, c: u32) -> (u8, u32);`
        else if (item.linkage.name == "llvm.x86.subborrow.32") {
            of << "\trv._0 = __builtin_sub_overflow(arg1, arg2, &rv._1);\n";
            of << "\tif(arg0) rv._0 |= __builtin_sub_overflow(rv._1, 1, &rv._1);\n";
            of << "\treturn rv;\n";
        }
        // `fn llvm_subborrow(a: u8, b: u64, c: u64) -> (u8, u64);`
        else if (item.linkage.name == "llvm.x86.subborrow.64") {
            of << "\trv._0 = __builtin_sub_overflow(arg1, arg2, &rv._1);\n";
            of << "\tif(arg0) rv._0 |= __builtin_sub_overflow(rv._1, 1, &rv._1);\n";
            of << "\treturn rv;\n";
        } else if (item.linkage.name == "llvm.x86.xgetbv") {
            of << "\tu32 lo, hi;\n";
            of << "\t__asm__ __volatile__ (\"xgetbv\" : \"=a\" (lo), \"=d\" (hi) : \"c\" (arg0) );\n";
            of << "\treturn lo | ((u64)hi << 32);\n";

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
        of << "}\n\n";
        mirRes = nullptr;
        return;
    } else if (item.linkage.name == "_Unwind_RaiseException") {
        of << "static ";
        emitFunctionHeader(p, item, params);
        of << " {\n";
        of << "\tthrow trustme_panic{arg0};\n";
        of << "}\n\n";
        return;
    } else {
        of << "extern ";
    }
    emitFunctionHeader(p, item, params);
    if (item.linkage.name != "") {
        if (TargetGetCurSpec(wb_).osName == "macos") { // Not macOS only, but all Apple platforms.
            of << " asm(\"_" << item.linkage.name << "\")";
        } else {
            of << " asm(\"" << item.linkage.name << "\")";
        }
    }
    of << ";\n\n";

    if (tracksCaller) {
        emitTrackCallerReifyWrapper(p, item, params);
    }

    mirRes = nullptr;
}

auto CodeGeneratorC::emitFunctionLinkageAlias(const HIRPath& p, const HIRFunction& item) -> void {
    // `main` is not renameable: C++ fixes its signature, and a Rust
    // `extern "C" fn main(c_int, *const *const c_char)` does not match.
    // It gets a shim after its body instead.
    if (item.linkage.name != "" && item.linkage.name != "main") {
        of << "#define " << TransMangleValue(p) << " " << item.linkage.name << "\n";
    }
}

auto CodeGeneratorC::emitFunctionDefinitionPrefix(const HIRFunction& item, bool isExternDef) -> void {
    if (isExternDef) {
        of << "static ";
    }
    switch (item.linkage.type) {
        case HIRLinkage::Type::External:
        case HIRLinkage::Type::Auto:
            break;
        case HIRLinkage::Type::Weak:
            of << "__attribute__((weak)) ";
            break;
        case HIRLinkage::Type::ExternWeak:
            BUG(Span(), "unexpected ExternWeak on function");
    }
}

auto CodeGeneratorC::emitFunctionProto(const HIRPath& p, const HIRFunction& item, const TransParams& params, bool isExternDef) -> void {
    MIRFunction emptyFcn;
    auto pathCallback = makeCallable<MIRPathCb>([&](auto& os) {
        os << "/*proto*/ fn " << p;
    });
    MIRTypeResolve topMirRes{sp, resolve_, pathCallback, HIRTypeRef(), {}, emptyFcn};
    mirRes = &topMirRes;

    emitFunctionLinkageAlias(p, item);
    emitFunctionDefinitionPrefix(item, isExternDef);
    emitFunctionHeader(p, item, params);
    of << ";\n\n";

    if (crate.functionTracksCaller(sp, p, item)) {
        trackedFunctions.insert(p.clone());
        emitTrackCallerReifyWrapper(p, item, params);
    }

    mirRes = nullptr;
}

auto CodeGeneratorC::emitFunctionCode(const HIRPath& p, const HIRFunction& item, const TransParams& params, bool isExternDef, const MIRFunctionPointer& code, bool hasPrototype) -> void {
    const bool tracksCaller = crate.functionTracksCaller(sp, p, item);
    if (tracksCaller) {
        trackedFunctions.insert(p.clone());
    }

    MIRTypeResolve::argsT argTypes;
    for (const auto& ent : item.args) {
        argTypes.push_back(::std::make_pair(HIRPattern{}, params.monomorph(resolve_, ent.second)));
    }

    HIRTypeRef retTypeTmp;
    const auto& retType = monomorphiseFcnReturn(retTypeTmp, item, params);

    auto pathCallback = makeCallable<MIRPathCb>([&](auto& os) {
        os << p;
    });
    MIRTypeResolve localMirRes{sp, resolve_, pathCallback, retType, argTypes, *code};
    mirRes = &localMirRes;
    currentFunctionTracksCaller = tracksCaller;

    if (!hasPrototype) {
        emitFunctionLinkageAlias(p, item);
    }
    emitFunctionDefinitionPrefix(item, isExternDef);
    if (exceedsBackendOptimizationBudget(item, *code)) {
        of << "TRUSTME_BACKEND_OPTNONE ";
    }
    emitFunctionHeader(p, item, params);
    of << " {\n";
    if (item.hasNamedVariadic) {
        const auto index = item.fixedArgCount();
        of << "\t";
        emitCtype(argTypes[index].second, FMT_CB(os, os << "arg" << index;));
        of << ";\n\tva_start(*(va_list*)&arg" << index << ", ";
        size_t lastPassed = SIZE_MAX;
        for (size_t i = 0; i < item.fixedArgCount(); i++) {
            if (argumentIsPassed(item.abi, argTypes[i].second)) {
                lastPassed = i;
            }
        }
        if (lastPassed == SIZE_MAX) {
            of << '0';
        } else {
            of << "arg" << lastPassed;
        }
        of << ");\n";
    }
    for (unsigned int i = 0; i < item.fixedArgCount(); i++) {
        const auto& argTy = argTypes[i].second;
        if (!argumentIsPassed(item.abi, argTy)) {
            of << "\t";
            emitCtype(argTy, FMT_CB(os, os << "arg" << i;));
            of << " = {};\n";
        }
    }

    if (item.markings.isNaked) {
        MIR_ASSERT(localMirRes, code->locals.empty(), "Naked function has MIR locals");
        MIR_ASSERT(localMirRes, code->dropFlags.empty(), "Naked function has drop flags");
        MIR_ASSERT(localMirRes, code->blocks.size() == 1, "Naked function does not have exactly one basic block");
        const auto& block = code->blocks.front();
        const MIRStatement* nakedAsm = nullptr;
        unsigned nakedAsmIndex = 0;
        for (unsigned i = 0; i < block.statements.size(); i++) {
            const auto& statement = block.statements[i];
            if (const auto* assembly = statement.opt_Asm2()) {
                MIR_ASSERT(localMirRes, assembly->options.naked && nakedAsm == nullptr, "Naked function body is not a single naked_asm statement");
                nakedAsm = &statement;
                nakedAsmIndex = i;
            } else if (const auto* assignment = statement.opt_Assign()) {
                MIR_ASSERT(localMirRes, assignment->dst.root.is_Return() && assignment->dst.wrappers.empty() && assignment->src.is_Tuple() && assignment->src.as_Tuple().vals.empty(), "Naked function contains a non-unit assignment");
            } else {
                MIR_BUG(localMirRes, "Naked function contains a non-assembly statement: " << statement);
            }
        }
        MIR_ASSERT(localMirRes, nakedAsm != nullptr, "Naked function body does not contain naked_asm");
        MIR_ASSERT(localMirRes, block.terminator.is_Return() || block.terminator.is_Unreachable(), "Naked function has a non-trivial MIR terminator");
        localMirRes.setCurStmt(0, nakedAsmIndex);
        emitStatement(localMirRes, *nakedAsm, 1);
        of << "}\n\n";
        of.flush();
        currentFunctionTracksCaller = false;
        if (tracksCaller && !hasPrototype) {
            emitTrackCallerReifyWrapper(p, item, params);
        }
        mirRes = nullptr;
        return;
    }

    for (unsigned int i = 0; i < argTypes.size(); i++) {
        emitUnsizedArgumentLocal(argTypes[i].second, i);
    }

    for (unsigned int i = 0; i < item.fixedArgCount(); i++) {
        const auto& argTy = argTypes[i].second;
        size_t argSize = 0;
        size_t argAlignment = 0;
        if (!argumentIsPassed(item.abi, argTy) || !TargetGetSizeAndAlignOf(sp, resolve_, argTy, argSize, argAlignment) || argSize == 0 || argAlignment <= maxCTypeAlignment) {
            continue;
        }
        of << "\tu8 arg" << i << "_storage[" << argSize + argAlignment - 1 << "];\n\t";
        emitCtype(argTy, FMT_CB(ss, ss << "&arg" << i << "_aligned";));
        of << " = *(";
        emitCtype(argTy);
        of << "*)trustme_align_storage(arg" << i << "_storage, " << argAlignment << ");\n";
        of << "\targ" << i << "_aligned = arg" << i << ";\n";
    }
    currentFunctionRealignsArguments = true;

    // Variables
    size_t returnSize = 0;
    size_t returnAlignment = 0;
    if (TargetGetSizeAndAlignOf(sp, resolve_, retType, returnSize, returnAlignment) && returnSize > 0 && returnAlignment > maxCTypeAlignment) {
        of << "\tu8 rv_storage[" << returnSize + returnAlignment - 1 << "];\n\t";
        emitCtype(retType, FMT_CB(ss, ss << "&rv";));
        of << " = *(";
        emitCtype(retType);
        of << "*)trustme_align_storage(rv_storage, " << returnAlignment << ");\n";
    } else {
        of << "\t";
        emitCtype(retType, FMT_CB(ss, ss << "rv";));
        of << ";\n";
    }
    // Native C/C++ compilers place separate stack locals opposite to
    // declaration order.  Reverse the declarations so consecutive
    // MIR locals keep their order in memory.
    for (size_t i = code->locals.size(); i-- > 0;) {
        // If the type is a ZST, initialise it (to avoid warnings)
        if (this->typeIsBadZst(code->locals[i])) {
            continue;
        }
        size_t localSize = 0;
        size_t localAlignment = 0;
        if (TargetGetSizeAndAlignOf(sp, resolve_, code->locals[i], localSize, localAlignment) && localSize > 0 && localAlignment > maxCTypeAlignment) {
            of << "\tu8 var" << i << "_storage[" << localSize + localAlignment - 1 << "];\n\t";
            emitCtype(code->locals[i], FMT_CB(ss, ss << "&var" << i;));
            of << " = *(";
            emitCtype(code->locals[i]);
            of << "*)trustme_align_storage(var" << i << "_storage, " << localAlignment << ");\n";
        } else {
            of << "\t";
            emitCtype(code->locals[i], FMT_CB(ss, ss << "var" << i;));
            of << ";\n";
        }
    }
    for (unsigned int i = 0; i < code->dropFlags.size(); i++) {
        of << "\tbool df" << i << " = " << code->dropFlags[i] << ";\n";
    }

    Vector<MIRBasicBlockId> pendingCleanupBlocks;
    for (const auto& block : code->blocks) {
        switch (block.terminator.tag()) {
            case MIRTerminator::TAG_Drop: {
                const auto* target = block.terminator.as_Drop().unwind.opt_Cleanup();
                if (target) {
                    pendingCleanupBlocks.pushBack(*target);
                }
                break;
            }
            case MIRTerminator::TAG_Call: {
                const auto* target = block.terminator.as_Call().unwind.opt_Cleanup();
                if (target) {
                    pendingCleanupBlocks.pushBack(*target);
                }
                break;
            }
            default:
                break;
        }
    }
    findNoOpCleanupBlocks(localMirRes, *code, pendingCleanupBlocks);
    ::std::set<unsigned> cleanupBlocks;
    while (!pendingCleanupBlocks.empty()) {
        const auto blockIndex = pendingCleanupBlocks.popBack();
        MIR_ASSERT(localMirRes, blockIndex < code->blocks.size(), "Cleanup target BB" << blockIndex << " is out of range");
        if (cleanupBlockIsNoOp(blockIndex) || !cleanupBlocks.insert(blockIndex).second) {
            continue;
        }

        struct QueueTargets final: public MIRTargetVisitor {
            Vector<MIRBasicBlockId>& pending;
            const CodeGeneratorC& codegen;

            QueueTargets(Vector<MIRBasicBlockId>& pending, const CodeGeneratorC& codegen)
                : pending(pending)
                , codegen(codegen)
            {
            }

            void visitTarget(const MIRBasicBlockId& target) override {
                if (!codegen.cleanupBlockIsNoOp(target)) {
                    pending.pushBack(target);
                }
            }
        } queueTargets{pendingCleanupBlocks, *this};

        visitTerminatorTarget(code->blocks[blockIndex].terminator, queueTargets);
    }
    findForwardedBlocks(localMirRes, *code);
    findBlockLabels(*code, cleanupBlocks);
    if (!cleanupBlocks.empty()) {
        emitCleanupRunner(localMirRes, cleanupBlocks);
    }

    for (unsigned i = 0; i < code->blocks.size(); i++) {
        const auto& block = code->blocks[i];
        if (cleanupBlocks.count(i) != 0 || cleanupBlockIsNoOp(i) || blockIsForwarded(i) || blockIsInlinedReturn(i)) {
            continue;
        }
        fallthroughBlock = ~0u;
        for (MIRBasicBlockId next = i + 1; next < code->blocks.size(); next++) {
            if (cleanupBlocks.count(next) == 0 && !cleanupBlockIsNoOp(next) && !blockIsForwarded(next) && !blockIsInlinedReturn(next)) {
                fallthroughBlock = next;
                break;
            }
        }
        if (blockLabels[i]) {
            of << "bb" << i << ":\n";
        }
        for (const auto& stmt : block.statements) {
            localMirRes.setCurStmt(i, &stmt - block.statements.data());
            emitStatement(localMirRes, stmt, 1);
        }
        localMirRes.setCurStmtTerm(i);
        emitBlockTerminator(localMirRes, block.terminator, i, false, 1);
    }
    fallthroughBlock = ~0u;
    of << "}\n\n";
    if (item.linkage.name == "main") {
        emitCMainShim(p, item, params, retType);
    }
    of.flush();
    currentFunctionTracksCaller = false;
    currentFunctionRealignsArguments = false;
    if (tracksCaller && !hasPrototype) {
        emitTrackCallerReifyWrapper(p, item, params);
    }
    mirRes = nullptr;
}

auto CodeGeneratorC::cleanupBlockIsNoOp(MIRBasicBlockId block) const -> bool {
    return block < noOpCleanupBlocks.length() && noOpCleanupBlocks[block] != 0;
}

auto CodeGeneratorC::dropOperationIsNoOp(const MIRTypeResolve& localMirRes, const MIRTerminator::Data_Drop& drop) const -> bool {
    if (drop.kind != MIRDropKind::DEEP) {
        return false;
    }
    HIRTypeRef tmp;
    const auto& ty = localMirRes.getLvalueType(tmp, drop.slot);
    return !resolve_.typeNeedsDropGlue(localMirRes.sp, ty);
}

auto CodeGeneratorC::forwardedBlockTarget(MIRBasicBlockId block) const -> MIRBasicBlockId {
    if (block < forwardedBlockTargets.length()) {
        return forwardedBlockTargets[block];
    }
    return block;
}

auto CodeGeneratorC::blockIsForwarded(MIRBasicBlockId block) const -> bool {
    return forwardedBlockTarget(block) != block;
}

auto CodeGeneratorC::blockIsInlinedReturn(MIRBasicBlockId block) const -> bool {
    return block < inlinedReturnBlocks.length() && inlinedReturnBlocks[block] != 0;
}

auto CodeGeneratorC::markBlockLabel(MIRBasicBlockId target, MIRBasicBlockId fallthrough, bool allowFallthrough) -> void {
    target = forwardedBlockTarget(target);
    if (target >= blockLabels.length() || cleanupCandidateBlocks[target] || cleanupBlockIsNoOp(target) || blockIsInlinedReturn(target) || (allowFallthrough && target == fallthrough)) {
        return;
    }
    blockLabels.mut(target) = 1;
}

template <typename CleanupBlocks>
auto CodeGeneratorC::findBlockLabels(const MIRFunction& code, const CleanupBlocks& cleanupBlocks) -> void {
    blockLabels.clear();
    blockLabels.zero(code.blocks.size());
    for (MIRBasicBlockId source = 0; source < code.blocks.size(); source++) {
        if (cleanupBlocks.count(source) != 0 || cleanupBlockIsNoOp(source) || blockIsForwarded(source) || blockIsInlinedReturn(source)) {
            continue;
        }
        MIRBasicBlockId fallthrough = ~0u;
        for (MIRBasicBlockId next = source + 1; next < code.blocks.size(); next++) {
            if (cleanupBlocks.count(next) == 0 && !cleanupBlockIsNoOp(next) && !blockIsForwarded(next) && !blockIsInlinedReturn(next)) {
                fallthrough = next;
                break;
            }
        }

        struct MarkTargets final: public MIRTargetVisitor {
            CodeGeneratorC& codegen;
            MIRBasicBlockId fallthrough;

            MarkTargets(CodeGeneratorC& codegen, MIRBasicBlockId fallthrough)
                : codegen(codegen)
                , fallthrough(fallthrough)
            {
            }

            void visitTarget(const MIRBasicBlockId& target) override {
                codegen.markBlockLabel(target, fallthrough, true);
            }
        } markTargets{*this, fallthrough};

        const auto& terminator = code.blocks[source].terminator;
        visitTerminatorTarget(terminator, markTargets);
        if (const auto* branch = terminator.opt_Switch()) {
            if (branch->validFlag != ~0u) {
                markBlockLabel(branch->invalidTarget, fallthrough, false);
            }
        }
        if (const auto* assembly = terminator.opt_Asm2()) {
            for (const auto& param : assembly->params) {
                if (const auto* label = param.opt_Label()) {
                    markBlockLabel(*label, fallthrough, false);
                }
            }
        }
    }
}

auto CodeGeneratorC::findForwardedBlocks(const MIRTypeResolve& localMirRes, const MIRFunction& code) -> void {
    forwardedBlockTargets.clear();
    forwardedBlockTargets.zero(code.blocks.size());
    for (MIRBasicBlockId i = 0; i < code.blocks.size(); i++) {
        forwardedBlockTargets.mut(i) = i;
    }

    // BB0 is the implicit function entry and must remain physically
    // first. Cleanup blocks have their own runner and labels, so leave
    // that graph to findNoOpCleanupBlocks.
    for (MIRBasicBlockId i = 1; i < code.blocks.size(); i++) {
        const auto& block = code.blocks[i];
        if (cleanupCandidateBlocks[i] || !block.statements.empty()) {
            continue;
        }
        MIRBasicBlockId target = i;
        if (block.terminator.is_Goto()) {
            target = block.terminator.as_Goto();
        } else if (const auto* drop = block.terminator.opt_Drop()) {
            if (dropOperationIsNoOp(localMirRes, *drop)) {
                target = drop->target;
            }
        }
        if (target < code.blocks.size() && !cleanupCandidateBlocks[target]) {
            forwardedBlockTargets.mut(i) = target;
        }
    }

    // Resolve every chain once. A goto cycle keeps one representative
    // block, preserving the infinite loop while removing the rest.
    forwardingState.clear();
    forwardingState.zero(code.blocks.size());
    for (MIRBasicBlockId root = 0; root < code.blocks.size(); root++) {
        if (forwardingState[root] != 0) {
            continue;
        }
        forwardingPath.clear();
        auto block = root;
        while (forwardingState[block] == 0) {
            forwardingState.mut(block) = 1;
            forwardingPath.pushBack(block);
            block = forwardedBlockTargets[block];
        }
        if (forwardingState[block] == 1) {
            forwardedBlockTargets.mut(block) = block;
        }
        while (!forwardingPath.empty()) {
            const auto item = forwardingPath.popBack();
            const auto target = forwardedBlockTargets[item];
            forwardedBlockTargets.mut(item) = forwardedBlockTargets[target];
            forwardingState.mut(item) = 2;
        }
    }

    blockIncoming.clear();
    blockIncoming.zero(code.blocks.size());
    asmLabelBlocks.clear();
    asmLabelBlocks.zero(code.blocks.size());
    for (MIRBasicBlockId source = 0; source < code.blocks.size(); source++) {
        if (cleanupCandidateBlocks[source] || cleanupBlockIsNoOp(source) || blockIsForwarded(source)) {
            continue;
        }

        struct CountIncoming final: public MIRTargetVisitor {
            const CodeGeneratorC& codegen;
            Vector<u32>& incoming;

            CountIncoming(const CodeGeneratorC& codegen, Vector<u32>& incoming)
                : codegen(codegen)
                , incoming(incoming)
            {
            }

            void visitTarget(const MIRBasicBlockId& target) override {
                const auto resolved = codegen.forwardedBlockTarget(target);
                if (resolved < incoming.length()) {
                    incoming.mut(resolved)++;
                }
            }
        } countIncoming{*this, blockIncoming};

        const auto& terminator = code.blocks[source].terminator;
        visitTerminatorTarget(terminator, countIncoming);
        if (const auto* assembly = terminator.opt_Asm2()) {
            for (const auto& param : assembly->params) {
                if (const auto* label = param.opt_Label()) {
                    asmLabelBlocks.mut(forwardedBlockTarget(*label)) = 1;
                }
            }
        }
    }

    inlinedReturnBlocks.clear();
    inlinedReturnBlocks.zero(code.blocks.size());
    for (MIRBasicBlockId i = 1; i < code.blocks.size(); i++) {
        const auto& block = code.blocks[i];
        if (!cleanupCandidateBlocks[i] && !blockIsForwarded(i) && !asmLabelBlocks[i] && blockIncoming[i] == 1 && block.statements.empty() && block.terminator.is_Return()) {
            inlinedReturnBlocks.mut(i) = 1;
        }
    }
}

auto CodeGeneratorC::findNoOpCleanupBlocks(const MIRTypeResolve& localMirRes, const MIRFunction& code, const Vector<MIRBasicBlockId>& cleanupEntries) -> void {
    noOpCleanupBlocks.clear();
    noOpCleanupBlocks.zero(code.blocks.size());
    cleanupCandidateBlocks.clear();
    cleanupCandidateBlocks.zero(code.blocks.size());
    cleanupReachabilityWorklist.clear();
    cleanupReachabilityWorklist.append(cleanupEntries.begin(), cleanupEntries.end());
    while (!cleanupReachabilityWorklist.empty()) {
        const auto blockIndex = cleanupReachabilityWorklist.popBack();
        MIR_ASSERT(localMirRes, blockIndex < code.blocks.size(), "Cleanup target BB" << blockIndex << " is out of range");
        if (cleanupCandidateBlocks[blockIndex]) {
            continue;
        }
        cleanupCandidateBlocks.mut(blockIndex) = 1;

        struct QueueTargets final: public MIRTargetVisitor {
            Vector<MIRBasicBlockId>& pending;

            explicit QueueTargets(Vector<MIRBasicBlockId>& pending)
                : pending(pending)
            {
            }

            void visitTarget(const MIRBasicBlockId& target) override {
                pending.pushBack(target);
            }
        } queueTargets{cleanupReachabilityWorklist};

        visitTerminatorTarget(code.blocks[blockIndex].terminator, queueTargets);
    }

    bool changed;
    do {
        changed = false;
        for (MIRBasicBlockId blockIndex = 0; blockIndex < code.blocks.size(); blockIndex++) {
            const auto& block = code.blocks[blockIndex];
            if (!cleanupCandidateBlocks[blockIndex] || cleanupBlockIsNoOp(blockIndex) || !block.statements.empty()) {
                continue;
            }

            bool noOp = false;
            switch (block.terminator.tag()) {
                case MIRTerminator::TAG_UnwindResume:
                    noOp = true;
                    break;
                case MIRTerminator::TAG_Goto:
                    noOp = cleanupBlockIsNoOp(block.terminator.as_Goto());
                    break;
                case MIRTerminator::TAG_Drop: {
                    const auto& drop = block.terminator.as_Drop();
                    noOp = dropOperationIsNoOp(localMirRes, drop) && cleanupBlockIsNoOp(drop.target);
                    break;
                }
                case MIRTerminator::TAG_If: {
                    const auto& e = block.terminator.as_If();
                    noOp = cleanupBlockIsNoOp(e.bbTrue) && cleanupBlockIsNoOp(e.bbFalse);
                    break;
                }
                case MIRTerminator::TAG_Switch: {
                    const auto& e = block.terminator.as_Switch();
                    noOp = e.validFlag == ~0u || cleanupBlockIsNoOp(e.invalidTarget);
                    for (const auto target : e.targets) {
                        noOp = noOp && cleanupBlockIsNoOp(target);
                    }
                    break;
                }
                case MIRTerminator::TAG_SwitchValue: {
                    const auto& e = block.terminator.as_SwitchValue();
                    noOp = cleanupBlockIsNoOp(e.defTarget);
                    for (const auto target : e.targets) {
                        noOp = noOp && cleanupBlockIsNoOp(target);
                    }
                    break;
                }
                default:
                    break;
            }
            if (noOp) {
                noOpCleanupBlocks.mut(blockIndex) = 1;
                changed = true;
            }
        }
    } while (changed);
}

auto CodeGeneratorC::emitCMainShim(const HIRPath& p, const HIRFunction& item, const TransParams& params, const HIRTypeData* retType) -> void {
    MIR_ASSERT(*mirRes, item.args.size() == 0 || item.args.size() == 2, "`main` takes no arguments or (argc, argv), got " << item.args.size());
    of << "int main(int argc, char** argv) {\n\t";
    const bool returnsValue = retType != crate.types.unit();
    if (returnsValue) {
        of << "return (int)";
    }
    of << TransMangleValue(p) << "(";
    if (item.args.size() == 2) {
        of << "(";
        emitCtype(params.monomorph(resolve_, item.args[0].second));
        of << ")argc, (";
        emitCtype(params.monomorph(resolve_, item.args[1].second));
        of << ")argv";
    }
    of << ");\n";
    if (!returnsValue) {
        of << "\treturn 0;\n";
    }
    of << "}\n\n";
}

auto CodeGeneratorC::emitOperationWithUnwindCb(const MIRUnwindAction& action, unsigned indentLevel, CUnwindOperationCallback& emitOperation) -> void {
    auto indent = RepeatLitStr{"\t", static_cast<int>(indentLevel)};
    switch (action.tag()) {
        case MIRUnwindAction::TAG_Continue: {
            auto& _ = action.as_Continue();
            emitOperation.emit(indentLevel);
            break;
        }
        case MIRUnwindAction::TAG_Cleanup: {
            auto& target = action.as_Cleanup();
            if (cleanupBlockIsNoOp(target)) {
                emitOperation.emit(indentLevel);
                break;
            }
            of << indent << "try {\n";
            emitOperation.emit(indentLevel + 1);
            of << indent << "} catch (...) {\n";
            of << indent << "\ttrustme_run_cleanup(" << target << ");\n";
            of << indent << "\tthrow;\n";
            of << indent << "}\n";
            break;
        }
        case MIRUnwindAction::TAG_Terminate: {
            auto& _ = action.as_Terminate();
            of << indent << "try {\n";
            emitOperation.emit(indentLevel + 1);
            of << indent << "} catch (...) { abort(); }\n";
            break;
        }
        case MIRUnwindAction::TAG_Unreachable: {
            auto& _ = action.as_Unreachable();
            of << indent << "try {\n";
            emitOperation.emit(indentLevel + 1);
            of << indent << "} catch (...) { abort(); }\n";
            break;
        }
    }
}

template <typename F>
auto CodeGeneratorC::emitOperationWithUnwind(const MIRUnwindAction& action, unsigned indentLevel, F f) -> void {
    CUnwindOperationCb<F> cb(f);
    emitOperationWithUnwindCb(action, indentLevel, cb);
}

auto CodeGeneratorC::emitBlockTerminator(MIRTypeResolve& localMirRes, const MIRTerminator& term, unsigned blockIndex, bool cleanup, unsigned indentLevel) -> void {
    auto indent = RepeatLitStr{"\t", static_cast<int>(indentLevel)};
    auto emitReturnBody = [&]() {
        if (localMirRes.retType == crate.types.unit()) {
            of << "return";
        } else {
            of << "return rv";
        }
    };
    auto targetFallsThrough = [&](unsigned target) {
        return !cleanup && forwardedBlockTarget(target) == fallthroughBlock && !blockIsInlinedReturn(fallthroughBlock);
    };
    auto emitTargetBodyImpl = [&](unsigned target, bool allowFallthrough) {
        if (cleanup && cleanupBlockIsNoOp(target)) {
            of << "return";
        } else {
            if (!cleanup) {
                target = forwardedBlockTarget(target);
                if (blockIsInlinedReturn(target)) {
                    emitReturnBody();
                    return;
                }
                if (allowFallthrough && target == fallthroughBlock) {
                    return;
                }
            }
            of << "goto " << (cleanup ? "cleanup_bb" : "bb") << target;
        }
    };
    auto emitTargetBody = [&](unsigned target) {
        emitTargetBodyImpl(target, true);
    };
    auto emitTarget = [&](unsigned target) {
        if (targetFallsThrough(target)) {
            return;
        }
        of << indent;
        emitTargetBodyImpl(target, false);
        of << ";\n";
    };
    switch (term.tag()) {
        case MIRTerminator::TAG_Incomplete: {
            auto& _ = term.as_Incomplete();
            of << indent << "abort();\n";
            break;
        }
        case MIRTerminator::TAG_Return: {
            auto& _ = term.as_Return();
            if (cleanup) {
                of << indent << "abort();\n";
            } else {
                of << indent;
                emitReturnBody();
                of << ";\n";
            }
            break;
        }
        case MIRTerminator::TAG_UnwindResume: {
            auto& _ = term.as_UnwindResume();
            if (cleanup) {
                of << indent << "return;\n";
            } else {
                of << indent << "abort();\n";
            }
            break;
        }
        case MIRTerminator::TAG_UnwindTerminate: {
            auto& _ = term.as_UnwindTerminate();
            of << indent << "abort();\n";
            break;
        }
        case MIRTerminator::TAG_Unreachable: {
            auto& _ = term.as_Unreachable();
            of << indent << "abort();\n";
            break;
        }
        case MIRTerminator::TAG_Goto: {
            auto& target = term.as_Goto();
            emitTarget(target);
            break;
        }
        case MIRTerminator::TAG_If: {
            auto& e = term.as_If();
            const auto trueTarget = forwardedBlockTarget(e.bbTrue);
            const auto falseTarget = forwardedBlockTarget(e.bbFalse);
            if (!cleanup && trueTarget == falseTarget) {
                emitTarget(trueTarget);
            } else if (!cleanup && targetFallsThrough(e.bbTrue)) {
                of << indent << "if(!(";
                emitLvalue(e.cond);
                of << ")) ";
                emitTargetBodyImpl(e.bbFalse, false);
                of << ";\n";
            } else if (!cleanup && targetFallsThrough(e.bbFalse)) {
                of << indent << "if(";
                emitLvalue(e.cond);
                of << ") ";
                emitTargetBodyImpl(e.bbTrue, false);
                of << ";\n";
            } else {
                of << indent << "if(";
                emitLvalue(e.cond);
                of << ") ";
                emitTargetBody(e.bbTrue);
                of << "; else ";
                emitTargetBody(e.bbFalse);
                of << ";\n";
            }
            break;
        }
        case MIRTerminator::TAG_Switch: {
            auto& e = term.as_Switch();
            if (e.validFlag != ~0u) {
                of << indent << "if(!df" << e.validFlag << ") ";
                emitTargetBodyImpl(e.invalidTarget, false);
                of << ";\n";
            }

            MIR_ASSERT(localMirRes, !e.targets.empty(), "Enum switch without variants");
            const auto firstTarget = e.targets[0];
            auto secondTarget = firstTarget;
            size_t firstTargetCount = 0;
            size_t secondTargetCount = 0;
            size_t secondTargetIndex = SIZE_MAX;
            bool hasSecondTarget = false;
            bool hasThirdTarget = false;
            for (size_t idx = 0; idx < e.targets.size(); idx++) {
                const auto target = e.targets[idx];
                if (target == firstTarget) {
                    firstTargetCount++;
                } else if (!hasSecondTarget) {
                    secondTarget = target;
                    secondTargetCount = 1;
                    secondTargetIndex = idx;
                    hasSecondTarget = true;
                } else if (target == secondTarget) {
                    secondTargetCount++;
                } else {
                    hasThirdTarget = true;
                }
            }

            if (!hasSecondTarget) {
                emitTarget(firstTarget);
                break;
            }

            size_t oddArm = SIZE_MAX;
            if (!hasThirdTarget) {
                if (firstTargetCount == 1) {
                    oddArm = 0;
                } else if (secondTargetCount == 1) {
                    oddArm = secondTargetIndex;
                }
            }
            emitTermSwitch(localMirRes, e.val, e.targets.size(), indentLevel, [&](size_t idx) {
                emitTargetBody(e.targets[idx]);
                of << ";";
            }, oddArm);
            break;
        }
        case MIRTerminator::TAG_SwitchValue: {
            auto& e = term.as_SwitchValue();
            emitTermSwitchvalue(localMirRes, e.val, e.values, indentLevel, [&](size_t idx) {
                const auto target = idx == SIZE_MAX ? e.defTarget : e.targets[idx];
                emitTargetBody(target);
                of << ";";
            });
            break;
        }
        case MIRTerminator::TAG_Drop: {
            auto& e = term.as_Drop();
            if (dropOperationIsNoOp(localMirRes, e)) {
                emitTarget(e.target);
                break;
            }
            if (cleanup) {
                emitDropOperation(localMirRes, e, indentLevel);
            } else {
                emitOperationWithUnwind(e.unwind, indentLevel, [&](unsigned operationIndent) {
                    emitDropOperation(localMirRes, e, operationIndent);
                });
            }
            emitTarget(e.target);
            break;
        }
        case MIRTerminator::TAG_Call: {
            auto& e = term.as_Call();
            if (cleanup) {
                emitTermCall(localMirRes, e, indentLevel);
            } else {
                emitOperationWithUnwind(e.unwind, indentLevel, [&](unsigned operationIndent) {
                    emitTermCall(localMirRes, e, operationIndent);
                });
            }
            emitTarget(e.retBlock);
            break;
        }
        case MIRTerminator::TAG_TailCall: {
            auto& e = term.as_TailCall();
            if (cleanup) {
                MIR_BUG(localMirRes, "Tail call in a cleanup block");
            }
            emitTermTailCall(localMirRes, e, indentLevel);
            break;
        }
        case MIRTerminator::TAG_Asm2: {
            auto& e = term.as_Asm2();
            if (cleanup) {
                MIR_BUG(localMirRes, "asm goto in a cleanup block");
            }
            emitAsm2Gcc(localMirRes, e.options, e.lines, e.params, true, e.retBlock, indentLevel);
            break;
        }
    }
}

auto CodeGeneratorC::emitCleanupRunner(MIRTypeResolve& localMirRes, const ::std::set<unsigned>& cleanupBlocks) -> void {
    of << "\tauto trustme_run_cleanup = [&](unsigned trustme_cleanup_entry) noexcept {\n";
    of << "\t\tswitch(trustme_cleanup_entry) {\n";
    for (auto block : cleanupBlocks) {
        of << "\t\tcase " << block << ": goto cleanup_bb" << block << ";\n";
    }
    of << "\t\tdefault: abort();\n";
    of << "\t\t}\n";
    for (auto blockIndex : cleanupBlocks) {
        const auto& block = localMirRes.fcn.blocks.at(blockIndex);
        of << "\tcleanup_bb" << blockIndex << ":\n";
        for (const auto& stmt : block.statements) {
            localMirRes.setCurStmt(blockIndex, &stmt - block.statements.data());
            emitStatement(localMirRes, stmt, 2);
        }
        localMirRes.setCurStmtTerm(blockIndex);
        emitBlockTerminator(localMirRes, block.terminator, blockIndex, true, 2);
    }
    of << "\t};\n";
}

auto CodeGeneratorC::typeIsEmulatedI128(const HIRTypeData* ty) const -> bool {
    return options.emulatedI128 && (ty == HIRCoreType::I128 || ty == HIRCoreType::U128);
}

auto CodeGeneratorC::typeIsCScalar(const HIRTypeData* ty) const -> bool {
    if (ty->is_Primitive()) {
        return ty->as_Primitive() != HIRCoreType::Str && !typeIsEmulatedI128(ty);
    }
    if (ty->is_Function()) {
        return true;
    }
    if (const auto* te = ty->opt_Pointer()) {
        return metadataType(te->inner) == MetadataType::None;
    }
    if (const auto* te = ty->opt_Borrow()) {
        return metadataType(te->inner) == MetadataType::None;
    }
    return false;
}

auto CodeGeneratorC::typeIsBadZst(const HIRTypeData* ty) const -> bool {
    if (options.disallowEmptyStructs) {
        // TODO: Extern types are also ZSTs?
        size_t size, align;
        // NOTE: Uses the Size+Align version because that doesn't panic on unsized
        MIR_ASSERT(*mirRes, TargetGetSizeAndAlignOf(sp, resolve_, ty, size, align), "Unexpected generic? " << ty);
        return size == 0;
    } else {
        return false;
    }
}

auto CodeGeneratorC::lvalueIsBadZst(const MIRLValue& lv) const -> bool {
    if (options.disallowEmptyStructs) {
        HIRTypeRef tmp;
        return typeIsBadZst(mirRes->getLvalueType(tmp, lv));
    } else {
        return false;
    }
}

auto CodeGeneratorC::lvalueRootIsBadZst(const MIRLValue& lv) const -> bool {
    if (options.disallowEmptyStructs) {
        HIRTypeRef tmp;
        return typeIsBadZst(mirRes->getLvalueType(tmp, lv, lv.wrappers.size()));
    } else {
        return false;
    }
}

auto CodeGeneratorC::lvalueZstIndexBacking(const MIRLValue& lv) const -> MIRLValue {
    auto rv = lv.clone();
    while (MIRLValue::CRef(rv).is_Index()) {
        auto inner = MIRLValue::CRef(rv).innerRef();
        HIRTypeRef tmp;
        if (!this->typeIsBadZst(mirRes->getLvalueType(tmp, inner))) {
            break;
        }
        rv.wrappers.pop_back();
    }
    return rv;
}

auto CodeGeneratorC::emitBorrow(const MIRTypeResolve& localMirRes, HIRBorrowType bt, const MIRLValue& val) -> void {
    HIRTypeRef tmp;
    const auto& ty = localMirRes.getLvalueType(tmp, val);

    if (this->typeIsBadZst(ty) && this->lvalueRootIsBadZst(val)) {
        size_t alignment = 0;
        MIR_ASSERT(localMirRes, TargetGetAlignOf(sp, resolve_, ty, alignment), "Unknown ZST alignment");
        of << "(void*)" << alignment;
        return;
    }

    if (this->typeIsBadZst(ty) && !this->lvalueRootIsBadZst(val)) {
        auto backing = this->lvalueZstIndexBacking(val);
        if (backing.wrappers.size() != val.wrappers.size()) {
            emitBorrow(localMirRes, bt, backing);
            return;
        }
    }

    bool special = false;
    // A by-value DST argument is represented by the same data/metadata pair
    // as a wide pointer. Borrowing it reuses that indirect place, just as a
    // dereference of an ordinary wide pointer does.
    if (this->isDst(ty)) {
        emitDstLvaluePointer(MIRLValue::CRef(val));
        special = true;
    }
    // If the inner value was a deref, just copy the pointer verbatim
    else if (val.is_Deref()) {
        emitLvalue(MIRLValue::CRef(val).innerRef());
        special = true;
    }

    // NOTE: If disallow_empty_structs is set, structs don't include ZST fields
    // In this case, we need to avoid mentioning the removed fields
    auto valRef = MIRLValue::CRef(val);
    if (!special && options.disallowEmptyStructs && valRef.is_Index() && this->typeIsBadZst(ty)) {
        auto inner = valRef.innerRef();
        HIRTypeRef tmp;
        const auto& parentTy = localMirRes.getLvalueType(tmp, inner);
        const HIRTypeData* elementTy = nullptr;
        if (const auto* array = parentTy->opt_Array()) {
            elementTy = array->inner;
        } else if (const auto* slice = parentTy->opt_Slice()) {
            elementTy = slice->inner;
        }
        MIR_ASSERT(localMirRes, elementTy, "Index of non-array type in ZST borrow path: " << parentTy);
        size_t elementSize = 0;
        MIR_ASSERT(localMirRes, TargetGetSizeOf(sp, resolve_, elementTy, elementSize), "Unknown array element size for " << parentTy);
        MIR_ASSERT(localMirRes, elementSize == 0, "Non-ZST element in ZST borrow path: " << elementTy);
        if (parentTy->is_Slice()) {
            of << "(void*)";
            emitDstLvaluePointer(inner);
            of << ".PTR";
        } else {
            of << "(void*)& ";
            emitLvalue(inner);
        }
        special = true;
    }

    auto zstField = MIRLValue::CRef(val);
    while (zstField.is_Downcast()) {
        zstField.tryUnwrap();
    }
    if (!special && options.disallowEmptyStructs && zstField.is_Field() && this->typeIsBadZst(ty)) {
        // Work backwards to the first non-ZST field
        auto valFp = zstField;
        assert(valFp.is_Field());
        while (valFp.innerRef().is_Field()) {
            HIRTypeRef tmp;
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
            HIRTypeRef tmp;
            const auto& parentTy = localMirRes.getLvalueType(tmp, fieldInner);
            if (parentTy->is_Slice()) {
                of << "(void*)";
                emitDstLvaluePointer(fieldInner);
                of << ".PTR";
            } else {
                of << "(void*)& ";
                emitLvalue(fieldInner);
            }
        } else {
            HIRTypeRef tmp;
            const auto& parentTy = localMirRes.getLvalueType(tmp, fieldInner);
            const HIRTypeData* elementTy = nullptr;
            if (const auto* array = parentTy->opt_Array()) {
                elementTy = array->inner;
            } else if (const auto* slice = parentTy->opt_Slice()) {
                elementTy = slice->inner;
            }

            if (elementTy) {
                size_t elementSize = 0;
                MIR_ASSERT(localMirRes, TargetGetSizeOf(sp, resolve_, elementTy, elementSize), "Unknown array element size for " << parentTy);
                MIR_ASSERT(localMirRes, elementSize == 0, "Non-ZST element in ZST borrow path: " << elementTy);
                of << "(void*)( (u8*)";
                if (parentTy->is_Slice()) {
                    emitDstLvaluePointer(fieldInner);
                    of << ".PTR";
                } else {
                    of << "& ";
                    emitLvalue(fieldInner);
                }
                of << " + " << elementSize * valFp.as_Field() << ")";
            } else {
                // Get the number of fields in parent
                auto* repr = TargetGetTypeRepr(sp, resolve_, parentTy);
                assert(repr);
                size_t nParentFields = repr->fields.size();
                // Find next non-zero field
                auto tmpLv = MIRLValue::newField(fieldInner.clone(), valFp.as_Field() + 1);
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
                    tmpLv.wrappers.back() = MIRLValue::Wrapper::newField(idx + 1);
                }

                // If no non-zero fields were found before the end, then do pointer manipulation using the repr
                if (!found) {
                    of << "(void*)( (u8*)& ";
                    emitLvalue(fieldInner);
                    of << " + " << repr->fields[valFp.as_Field()].offset << ")";
                }
                // Otherwise, use the next non-zero field
                else {
                    of << "(void*)( &";
                    emitLvalue(tmpLv);
                    of << ")";
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

auto CodeGeneratorC::emitCompositeAssignCb(const MIRTypeResolve& localMirRes, CSlotCallback& emitSlot, const MIRParamList& vals, unsigned indentLevel, bool prependNewline) -> void {
    auto indent = RepeatLitStr{"\t", static_cast<int>(indentLevel)};
    bool hasEmitted = prependNewline;
    for (unsigned int j = 0; j < vals.size(); j++) {
        if (options.disallowEmptyStructs) {
            HIRTypeRef tmp;
            const auto& ty = localMirRes.getParamType(tmp, vals[j]);

            // Don't emit assignment of PhantomData
            if (vals[j].is_LValue() && resolve_.isTypePhantomData(ty)) {
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

        emitSlot.emit();
        of << "._" << j << " = ";
        emitParam(vals[j]);
    }
}

template <typename F>
auto CodeGeneratorC::emitCompositeAssign(const MIRTypeResolve& localMirRes, F f, const MIRParamList& vals, unsigned indentLevel, bool prependNewline) -> void {
    CSlotCb<F> cb(f);
    emitCompositeAssignCb(localMirRes, cb, vals, indentLevel, prependNewline);
}

auto CodeGeneratorC::emitDropOperation(const MIRTypeResolve& localMirRes, const MIRTerminator::Data_Drop& e, unsigned indentLevel) -> void {
    auto indent = RepeatLitStr{"\t", static_cast<int>(indentLevel)};
    HIRTypeRef tmp;
    const auto& ty = localMirRes.getLvalueType(tmp, e.slot);
    if (e.flagIdx != ~0u) {
        of << indent << "if( df" << e.flagIdx << " ) {\n";
    }
    switch (e.kind) {
        case MIRDropKind::SHALLOW:
            if (const auto* ity = resolve_.isTypeOwnedBox(ty)) {
                emitBoxDrop(indentLevel + (e.flagIdx != ~0u ? 1 : 0), ity, ty, e.slot, false);
            } else {
                MIR_BUG(localMirRes, "Shallow drop on non-Box - " << ty);
            }
            break;
        case MIRDropKind::DEEP:
            emitDestructorCall(e.slot, ty, true, indentLevel + (e.flagIdx != ~0u ? 1 : 0));
            break;
    }
    if (e.flagIdx != ~0u) {
        of << indent << "}\n";
    }
}

auto CodeGeneratorC::emitStatement(const MIRTypeResolve& localMirRes, const MIRStatement& stmt, unsigned indentLevel) -> void {
    auto indent = RepeatLitStr{"\t", static_cast<int>(indentLevel)};
    switch (stmt.tag()) {
        case MIRStatement::TAG_ScopeEnd:
            break;
        case MIRStatement::TAG_SetDropFlag: {
            const auto& e = stmt.as_SetDropFlag();
            of << indent << "df" << e.idx << " = ";
            if (e.other == ~0u) {
                of << e.newVal;
            } else {
                of << (e.newVal ? "!" : "") << "df" << e.other;
            }
            of << ";\n";
            break;
        } break;
        case MIRStatement::TAG_SaveDropFlag: {
            auto& e = stmt.as_SaveDropFlag();
            of << indent << "if(df" << e.idx << ") { ";
            emitLvalue(e.slot);
            of << ".DATA[" << (e.bitIndex / 8) << "] |= (1 << " << (e.bitIndex % 8) << ");";
            of << " } else { ";
            emitLvalue(e.slot);
            of << ".DATA[" << (e.bitIndex / 8) << "] &= ~(1 << " << (e.bitIndex % 8) << ");";
            of << " }\n";

        } break;
            break;
        case MIRStatement::TAG_LoadDropFlag: {
            auto& e = stmt.as_LoadDropFlag();
            of << indent << "df" << e.idx << " = ((";
            emitLvalue(e.slot);
            of << ".DATA[" << (e.bitIndex / 8) << "] & (1 << " << (e.bitIndex % 8) << ")) != 0)";
            of << ";\n";

        } break;
        case MIRStatement::TAG_Asm:
            this->emitAsmGcc(localMirRes, stmt.as_Asm(), indentLevel);
            break;
        case MIRStatement::TAG_Asm2:
            this->emitAsm2Gcc(localMirRes, stmt, indentLevel);
            break;
        case MIRStatement::TAG_Assign: {
            const auto& e = stmt.as_Assign();

            HIRTypeRef tmp;
            const auto& ty = localMirRes.getLvalueType(tmp, e.dst);
            if (/*(e.dst.is_Deref() || e.dst.is_Field()) &&*/ this->typeIsBadZst(ty)) {
                break;
            }
            of << indent;

            switch (e.src.tag()) {
                case MIRRValue::TAG_Use: {
                    auto& ve = e.src.as_Use();
                    HIRTypeRef tmp;
                    const auto& ty = localMirRes.getLvalueType(tmp, ve);
                    if (ty == crate.types.diverge()) {
                        of << "abort()";
                        break;
                    }

                    if (ve.is_Field() && this->typeIsBadZst(ty)) {
                        break;
                    }

                    emitLvalue(e.dst);
                    of << " = ";
                    emitLvalue(ve);
                    break;
                }
                case MIRRValue::TAG_Constant: {
                    auto& ve = e.src.as_Constant();
                    emitLvalue(e.dst);
                    of << " = (";
                    emitCtype(ty);
                    of << ")";
                    emitConstant(ve, &e.dst);
                    break;
                }
                case MIRRValue::TAG_SizedArray: {
                    auto& ve = e.src.as_SizedArray();
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
                    break;
                }
                case MIRRValue::TAG_Borrow: {
                    auto& ve = e.src.as_Borrow();
                    emitLvalue(e.dst);
                    MIR_ASSERT(localMirRes, ty->is_Borrow() || ty->is_Pointer(), "Borrow rvalue has non-pointer result type " << ty);
                    of << " = (";
                    emitCtype(ty);
                    of << ")";
                    emitBorrow(localMirRes, ve.type, ve.val);
                    break;
                }
                case MIRRValue::TAG_Cast: {
                    auto& ve = e.src.as_Cast();
                    emitRvalueCast(localMirRes, e.dst, ve);
                    break;
                }
                case MIRRValue::TAG_BinOp: {
                    auto& ve = e.src.as_BinOp();
                    emitLvalue(e.dst);
                    of << " = ";
                    HIRTypeRef tmp, tmpR;
                    const auto& ty = localMirRes.getParamType(tmp, ve.valL);
                    const auto& tyR = localMirRes.getParamType(tmpR, ve.valR);
                    if (ty->is_Borrow()) {
                        of << "(slice_cmp(";
                        emitParam(ve.valL);
                        of << ", ";
                        emitParam(ve.valR);
                        of << ")";
                        switch (ve.op) {
                            case MIRBinOp::EQ:
                                of << " == 0";
                                break;
                            case MIRBinOp::NE:
                                of << " != 0";
                                break;
                            case MIRBinOp::GT:
                                of << " >  0";
                                break;
                            case MIRBinOp::GE:
                                of << " >= 0";
                                break;
                            case MIRBinOp::LT:
                                of << " <  0";
                                break;
                            case MIRBinOp::LE:
                                of << " <= 0";
                                break;
                            default:
                                MIR_BUG(localMirRes, "Unknown comparison of a &-ptr - " << e.src << " with " << ty);
                        }
                        of << ")";
                        break;
                    } else if (const auto* te = ty->opt_Pointer()) {
                        if (isDst(te->inner)) {
                            of << "(raw_fat_ptr_cmp((uintptr_t)";
                            emitParam(ve.valL);
                            of << ".PTR, (uintptr_t)";
                            emitParam(ve.valL);
                            of << ".META, (uintptr_t)";
                            emitParam(ve.valR);
                            of << ".PTR, (uintptr_t)";
                            emitParam(ve.valR);
                            of << ".META)";
                            switch (ve.op) {
                                case MIRBinOp::EQ:
                                    of << " == 0";
                                    break;
                                case MIRBinOp::NE:
                                    of << " != 0";
                                    break;
                                case MIRBinOp::GT:
                                    of << " > 0";
                                    break;
                                case MIRBinOp::GE:
                                    of << " >= 0";
                                    break;
                                case MIRBinOp::LT:
                                    of << " < 0";
                                    break;
                                case MIRBinOp::LE:
                                    of << " <= 0";
                                    break;
                                default:
                                    MIR_BUG(localMirRes, "Unknown comparison of a *-ptr - " << e.src << " with " << ty);
                            }
                            of << ")";
                        } else {
                            const bool ordering = ve.op == MIRBinOp::GT || ve.op == MIRBinOp::GE || ve.op == MIRBinOp::LT || ve.op == MIRBinOp::LE;
                            if (ordering) {
                                of << "(uintptr_t)";
                            }
                            emitParam(ve.valL);
                            switch (ve.op) {
                                case MIRBinOp::EQ:
                                    of << " == ";
                                    break;
                                case MIRBinOp::NE:
                                    of << " != ";
                                    break;
                                case MIRBinOp::GT:
                                    of << " > ";
                                    break;
                                case MIRBinOp::GE:
                                    of << " >= ";
                                    break;
                                case MIRBinOp::LT:
                                    of << " < ";
                                    break;
                                case MIRBinOp::LE:
                                    of << " <= ";
                                    break;
                                default:
                                    MIR_BUG(localMirRes, "Unknown comparison of a *-ptr - " << e.src << " with " << ty);
                            }
                            if (ordering) {
                                of << "(uintptr_t)";
                            }
                            emitParam(ve.valR);
                        }
                        break;
                    } else if (ve.op == MIRBinOp::MOD && (ty == HIRCoreType::F16 || ty == HIRCoreType::F32 || ty == HIRCoreType::F64)) {
                        // Rust's `%` on floats truncates the quotient and
                        // keeps the dividend's sign, which is `fmod`.
                        // `remainder` rounds the quotient to nearest, so
                        // `7.0 % 4.0` came out as -1.0 rather than 3.0.
                        of << "__builtin_";
                        if (ty == HIRCoreType::F64) {
                            of << "fmod";
                        } else {
                            of << "fmodf";
                        }
                        of << "(";
                        emitParam(ve.valL);
                        of << ", ";
                        emitParam(ve.valR);
                        of << ")";
                        break;
                    } else if (ty == HIRCoreType::F128) {
                        switch (ve.op) {
                            case MIRBinOp::ADD:
                                of << "f128_add";
                                break;
                            case MIRBinOp::SUB:
                                of << "f128_sub";
                                break;
                            case MIRBinOp::MUL:
                                of << "f128_mul";
                                break;
                            case MIRBinOp::DIV:
                                of << "f128_div";
                                break;
                            case MIRBinOp::MOD:
                                of << "f128_mod";
                                break;
                            case MIRBinOp::EQ:
                                of << "f128_eq";
                                break;
                            case MIRBinOp::NE:
                                of << "f128_ne";
                                break;
                            case MIRBinOp::GT:
                                of << "f128_gt";
                                break;
                            case MIRBinOp::GE:
                                of << "f128_ge";
                                break;
                            case MIRBinOp::LT:
                                of << "f128_lt";
                                break;
                            case MIRBinOp::LE:
                                of << "f128_le";
                                break;
                            default:
                                MIR_TODO(localMirRes, "unsupported f128 binop");
                        }
                        of << "(";
                        emitParam(ve.valL);
                        of << ", ";
                        emitParam(ve.valR);
                        of << ")";
                        break;
                    } else if (typeIsEmulatedI128(ty)) {
                        switch (ve.op) {
                            case MIRBinOp::ADD:
                                of << "add128";
                                if (0) {
                                    case MIRBinOp::SUB:
                                        of << "sub128";
                                }
                                if (0) {
                                    case MIRBinOp::MUL:
                                        of << "mul128";
                                }
                                if (0) {
                                    case MIRBinOp::DIV:
                                        of << "div128";
                                }
                                if (0) {
                                    case MIRBinOp::MOD:
                                        of << "mod128";
                                }
                                if (0) {
                                    case MIRBinOp::BIT_OR:
                                        of << "or128";
                                }
                                if (0) {
                                    case MIRBinOp::BIT_AND:
                                        of << "and128";
                                }
                                if (0) {
                                    case MIRBinOp::BIT_XOR:
                                        of << "xor128";
                                }
                                if (ty == HIRCoreType::I128) {
                                    of << "s";
                                }
                                of << "(";
                                emitParam(ve.valL);
                                of << ", ";
                                emitParam(ve.valR);
                                of << ")";
                                break;
                            case MIRBinOp::BIT_SHR:
                                of << "shr128";
                                if (0) {
                                    case MIRBinOp::BIT_SHL:
                                        of << "shl128";
                                }
                                if (ty == HIRCoreType::I128) {
                                    of << "s";
                                }
                                of << "(";
                                emitParam(ve.valL);
                                of << ", ";
                                emitParam(ve.valR);
                                if ((tyR == HIRCoreType::I128 || tyR == HIRCoreType::U128)) {
                                    of << ".lo";
                                }
                                of << ")";
                                break;

                            case MIRBinOp::EQ:
                                of << "0 == ";
                                if (0) {
                                    case MIRBinOp::NE:
                                        of << "0 != ";
                                }
                                if (0) {
                                    case MIRBinOp::GT:
                                        of << "0 > ";
                                }
                                if (0) {
                                    case MIRBinOp::GE:
                                        of << "0 >= ";
                                }
                                if (0) {
                                    case MIRBinOp::LT:
                                        of << "0 < ";
                                }
                                if (0) {
                                    case MIRBinOp::LE:
                                        of << "0 <= ";
                                }
                                // NOTE: Reversed order due to reversed logic above
                                of << "cmp128";
                                if (ty == HIRCoreType::I128) {
                                    of << "s";
                                }
                                of << "(";
                                emitParam(ve.valR);
                                of << ", ";
                                emitParam(ve.valL);
                                of << ")";
                                break;

                            case MIRBinOp::ADD_OV:
                            case MIRBinOp::SUB_OV:
                            case MIRBinOp::MUL_OV:
                            case MIRBinOp::DIV_OV:
                                MIR_TODO(localMirRes, "Overflowing binops for emulated i128");
                                break;
                        }
                        break;
                    } else {
                    }

                    emitParam(ve.valL);
                    switch (ve.op) {
                        case MIRBinOp::ADD:
                            of << " + ";
                            break;
                        case MIRBinOp::SUB:
                            of << " - ";
                            break;
                        case MIRBinOp::MUL:
                            of << " * ";
                            break;
                        case MIRBinOp::DIV:
                            of << " / ";
                            break;
                        case MIRBinOp::MOD:
                            of << " % ";
                            break;

                        case MIRBinOp::BIT_OR:
                            of << " | ";
                            break;
                        case MIRBinOp::BIT_AND:
                            of << " & ";
                            break;
                        case MIRBinOp::BIT_XOR:
                            of << " ^ ";
                            break;
                        case MIRBinOp::BIT_SHR:
                            of << " >> ";
                            break;
                        case MIRBinOp::BIT_SHL:
                            of << " << ";
                            break;
                        case MIRBinOp::EQ:
                            of << " == ";
                            break;
                        case MIRBinOp::NE:
                            of << " != ";
                            break;
                        case MIRBinOp::GT:
                            of << " > ";
                            break;
                        case MIRBinOp::GE:
                            of << " >= ";
                            break;
                        case MIRBinOp::LT:
                            of << " < ";
                            break;
                        case MIRBinOp::LE:
                            of << " <= ";
                            break;

                        case MIRBinOp::ADD_OV:
                        case MIRBinOp::SUB_OV:
                        case MIRBinOp::MUL_OV:
                        case MIRBinOp::DIV_OV:
                            MIR_TODO(localMirRes, "Overflow");
                            break;
                    }
                    emitParam(ve.valR);
                    if (typeIsEmulatedI128(tyR)) {
                        of << ".lo";
                    }
                    break;
                }
                case MIRRValue::TAG_UniOp: {
                    auto& ve = e.src.as_UniOp();
                    HIRTypeRef tmp;
                    const auto& ty = localMirRes.getLvalueType(tmp, e.dst);

                    if (typeIsEmulatedI128(ty)) {
                        switch (ve.op) {
                            case MIRUniOp::NEG:
                                emitLvalue(e.dst);
                                of << " = neg128s(";
                                emitLvalue(ve.val);
                                of << ")";
                                break;
                            case MIRUniOp::INV:
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
                    } else if (ty == HIRCoreType::F128) {
                        switch (ve.op) {
                            case MIRUniOp::NEG:
                                emitLvalue(e.dst);
                                of << " = f128_neg(";
                                emitLvalue(ve.val);
                                of << ")";
                                break;
                            case MIRUniOp::INV:
                                MIR_TODO(*mirRes, "f128 INV");
                                break;
                        }
                        break;
                    }

                    emitLvalue(e.dst);
                    of << " = ";
                    switch (ve.op) {
                        case MIRUniOp::NEG:
                            of << "-";
                            break;
                        case MIRUniOp::INV:
                            if (ty == HIRCoreType::Bool) {
                                of << "!";
                            } else {
                                of << "~";
                            }
                            break;
                    }
                    emitLvalue(ve.val);
                    break;
                }
                case MIRRValue::TAG_DstMeta: {
                    auto& ve = e.src.as_DstMeta();
                    emitLvalue(e.dst);
                    // TODO: Why? Probably for getting `VTable`
                    if (ty->is_Primitive() || ty->is_Pointer() || ty->is_Borrow()) {
                    } else {
                        of << "._0._0";
                    }
                    of << " = (decltype(";
                    emitLvalue(e.dst);
                    if (ty->is_Primitive() || ty->is_Pointer() || ty->is_Borrow()) {
                    } else {
                        of << "._0._0";
                    }
                    of << "))";
                    emitLvalue(ve.val);
                    of << ".META";
                    break;
                }
                case MIRRValue::TAG_DstPtr: {
                    auto& ve = e.src.as_DstPtr();
                    emitLvalue(e.dst);
                    of << " = (";
                    emitCtype(ty);
                    of << ")";
                    emitLvalue(ve.val);
                    of << ".PTR";
                    break;
                }
                case MIRRValue::TAG_MakeDst: {
                    auto& ve = e.src.as_MakeDst();
                    emitLvalue(e.dst);
                    of << " = (";
                    emitCtype(ty);
                    of << ")";
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
                    break;
                }
                case MIRRValue::TAG_Tuple: {
                    auto& ve = e.src.as_Tuple();
                    emitCompositeAssign(localMirRes, [&]() {
                        emitLvalue(e.dst);
                    }, ve.vals, indentLevel);
                    break;
                }
                case MIRRValue::TAG_Array: {
                    auto& ve = e.src.as_Array();
                    for (unsigned int j = 0; j < ve.vals.size(); j++) {
                        if (j != 0) {
                            of << ";\n" << indent;
                        }
                        emitLvalue(e.dst);
                        of << ".DATA[" << j << "] = ";
                        emitParam(ve.vals[j]);
                    }
                    break;
                }
                case MIRRValue::TAG_UnionVariant: {
                    auto& ve = e.src.as_UnionVariant();
                    MIR_ASSERT(localMirRes, crate.getTypeitemByPath(sp, ve.path.path).is_Union(), "");
                    if (!this->typeIsBadZst(mirRes->getParamType(tmp, ve.val))) {
                        emitLvalue(e.dst);
                        of << ".var_" << ve.index << " = ";
                        emitParam(ve.val);
                    }
                    break;
                }
                case MIRRValue::TAG_EnumVariant: {
                    auto& ve = e.src.as_EnumVariant();
                    const auto& tyi = crate.getTypeitemByPath(sp, ve.path.path);
                    MIR_ASSERT(localMirRes, tyi.is_Enum(), "");
                    const auto* enmP = &tyi.as_Enum();

                    HIRTypeRef tmp;
                    const auto& ty = localMirRes.getLvalueType(tmp, e.dst);
                    auto* repr = TargetGetTypeRepr(sp, resolve_, ty);

                    switch (repr->variants.tag()) {
                        case TypeReprVariantMode::TAG_None: {
                            // One variant and nothing in it: there is no
                            // storage to write.
                            if (enumIsTagless(repr)) {
                                break;
                            }
                            emitCompositeAssign(localMirRes, [&]() {
                                emitLvalue(e.dst);
                                of << ".DATA.var_0";
                            }, /*repr->fields[0].ty,*/ ve.vals, indentLevel);
                            break;
                        }
                        case TypeReprVariantMode::TAG_NonZero: {
                            auto& re = repr->variants.as_NonZero();
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
                            break;
                        }
                        case TypeReprVariantMode::TAG_Linear: {
                            auto& re = repr->variants.as_Linear();
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
                                    of << re.tagValue(ve.index);
                                } else {
                                    auto vr = TargetGetTypeRepr(sp, resolve_, repr->fields[ve.index].ty);
                                    emitLvalue(e.dst);
                                    of << ".DATA.var_" << ve.index << "._" << (vr->fields.size() - 1) << " = ";
                                    const auto& slotTy = vr->fields.back().ty;
                                    if (slotTy->is_Pointer() || slotTy->is_Borrow() || slotTy->is_Function()) {
                                        of << "(";
                                        emitCtype(slotTy);
                                        of << ")(uintptr_t)";
                                    }
                                    of << re.tagValue(ve.index);
                                }
                                emitNewline = true;
                            }
                            if (enmP->isValue()) {
                                // Value enums have no data fields
                            } else {
                                emitCompositeAssign(localMirRes, [&]() {
                                    emitLvalue(e.dst);
                                    of << ".DATA.var_" << ve.index;
                                }, ve.vals, indentLevel, emitNewline);
                            }
                            break;
                        }
                        case TypeReprVariantMode::TAG_Values: {
                            auto& re = repr->variants.as_Values();
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
                            break;
                        }
                    }
                    break;
                }
                case MIRRValue::TAG_Struct: {
                    auto& ve = e.src.as_Struct();
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
                    break;
                }
            }
            of << ";\n";
            break;
        }
    }
}

auto CodeGeneratorC::emitRvalueCast(const MIRTypeResolve& localMirRes, const MIRLValue& dst, const MIRRValue::Data_Cast& ve) -> void {
    if (resolve_.isTypePhantomData(ve.type)) {
        return;
    }

    HIRTypeRef tmp;
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
        of << " = ";
        emitReifiedFunctionName(ty->as_NamedFunction().path);
        return;
    }

    // Emulated i128/u128 support
    if (options.emulatedI128 && (ve.type == HIRCoreType::U128 || ve.type == HIRCoreType::I128 || ty == HIRCoreType::U128 || ty == HIRCoreType::I128)) {
        // Destination
        MIR_ASSERT(localMirRes, ve.type->is_Primitive(), "i128/u128 cast to non-primitive - " << ve.type);
        MIR_ASSERT(localMirRes, ty->is_Primitive() || (ty->is_Path() && ty->as_Path().binding.is_Enum()), "i128/u128 cast from non-primitive - " << ty);
        switch (ve.type->as_Primitive()) {
            case HIRCoreType::I128:
            case HIRCoreType::U128:
                if (ty == HIRCoreType::I128 || ty == HIRCoreType::U128) {
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
                    if (enumIsTagless(TargetGetTypeRepr(sp, resolve_, ty))) {
                        emitLvalue(dst);
                        of << ".lo = ";
                        emitTaglessEnumDiscriminant(ty);
                        of << "; ";
                        emitLvalue(dst);
                        of << ".hi = (";
                        emitTaglessEnumDiscriminant(ty);
                        of << ") < 0 ? -1 : 0";
                        break;
                    }
                    emitLvalue(dst);
                    of << ".lo = ";
                    emitLvalue(ve.val);
                    of << ".TAG; ";
                    emitLvalue(dst);
                    of << ".hi = ";
                    emitLvalue(ve.val);
                    of << ".TAG < 0 ? -1 : 0";
                } else if (ty == HIRCoreType::F32 || ty == HIRCoreType::F64) {
                    // A float does not fit the sign-extension the
                    // integer path below does: its value can need both
                    // halves, and out of range it saturates.
                    emitLvalue(dst);
                    of << " = ";
                    of << (ve.type == HIRCoreType::I128 ? "cast_float_to_i128(" : "cast_float_to_u128(");
                    emitLvalue(ve.val);
                    of << ")";
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
            case HIRCoreType::I8:
            case HIRCoreType::I16:
            case HIRCoreType::I32:
            case HIRCoreType::I64:
            case HIRCoreType::Isize:
            case HIRCoreType::U8:
            case HIRCoreType::U16:
            case HIRCoreType::U32:
            case HIRCoreType::U64:
            case HIRCoreType::Usize:
                emitLvalue(dst);
                of << " = ";
                switch (ty->as_Primitive()) {
                    case HIRCoreType::U128:
                    case HIRCoreType::I128:
                        emitLvalue(ve.val);
                        of << ".lo";
                        break;
                    default:
                        MIR_BUG(localMirRes, "Unreachable");
                }
                break;
            case HIRCoreType::F16:
                MIR_TODO(localMirRes, "f16 from i128/u128");
            case HIRCoreType::F32:
                emitLvalue(dst);
                of << " = ";
                switch (ty->as_Primitive()) {
                    case HIRCoreType::U128:
                        of << "cast128_float(";
                        emitLvalue(ve.val);
                        of << ")";
                        break;
                    case HIRCoreType::I128:
                        of << "cast128s_float(";
                        emitLvalue(ve.val);
                        of << ")";
                        break;
                    default:
                        MIR_BUG(localMirRes, "Unreachable");
                }
                break;
            case HIRCoreType::F64:
                emitLvalue(dst);
                of << " = ";
                switch (ty->as_Primitive()) {
                    case HIRCoreType::U128:
                        of << "cast128_double(";
                        emitLvalue(ve.val);
                        of << ")";
                        break;
                    case HIRCoreType::I128:
                        of << "cast128s_double(";
                        emitLvalue(ve.val);
                        of << ")";
                        break;
                    default:
                        MIR_BUG(localMirRes, "Unreachable");
                }
                break;
            case HIRCoreType::F128:
                MIR_TODO(localMirRes, "f128 from i128/u128");
            default:
                MIR_BUG(localMirRes, "Bad i128/u128 cast - " << ty << " to " << ve.type);
        }
        return;
    }
    if (ve.type == HIRCoreType::F128) {
        emitLvalue(dst);
        of << " = f128_encode((f128_native)";
        emitLvalue(ve.val);
        of << ")";
        return;
    }
    if (ty == HIRCoreType::F128) {
        emitLvalue(dst);
        of << " = (";
        emitCtype(ve.type);
        of << ")f128_decode(";
        emitLvalue(ve.val);
        of << ")";
        return;
    }

    HIRTypeRef dstTmp;
    const auto& dstTy = localMirRes.getLvalueType(dstTmp, dst);
    const auto* dstPrimitive = ve.type->opt_Primitive();
    if (dstPrimitive && isInteger(*dstPrimitive) && (ty->is_NamedFunction() || ty->is_Function() || ty->is_Pointer())) {
        emitLvalue(dst);
        of << " = (";
        emitCtype(dstTy);
        of << ")(uintptr_t)";
        if (ty->is_NamedFunction()) {
            emitReifiedFunctionName(ty->as_NamedFunction().path);
        } else {
            emitLvalue(ve.val);
        }
        return;
    }

    // Standard cast
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
        emitReifiedFunctionName(ty->as_NamedFunction().path);
        special = true;
    }
    if (ve.type->is_Primitive() && ty->is_Path() && ty->as_Path().binding.is_Enum()) {
        if (enumIsTagless(TargetGetTypeRepr(sp, resolve_, ty))) {
            emitTaglessEnumDiscriminant(ty);
        } else {
            emitLvalue(ve.val);
            // NOTE: Embedded tag enums can't be cast
            of << ".TAG";
        }
        special = true;
    }
    if (!special) {
        emitLvalue(ve.val);
    }
}

auto CodeGeneratorC::emitTermSwitchCb(const MIRTypeResolve& localMirRes, const MIRLValue& val, size_t nArms, unsigned indentLevel, CSwitchArmCallback& cb, size_t oddArm) -> void {
    auto indent = RepeatLitStr{"\t", static_cast<int>(indentLevel)};

    HIRTypeRef tmp;
    const auto& ty = localMirRes.getLvalueType(tmp, val);
    MIR_ASSERT(localMirRes, ty->is_Path(), "Switch over non-Path type");
    MIR_ASSERT(localMirRes, ty->as_Path().binding.is_Enum(), "Switch over non-enum");
    const auto* repr = TargetGetTypeRepr(localMirRes.sp, resolve_, ty);
    MIR_ASSERT(localMirRes, repr, "No repr for " << ty);

    struct MaybeSigned64 {
        bool is_signed;
        u64 v;

        MaybeSigned64(bool is_signed, u64 v)
            : is_signed(is_signed)
            , v(v)
        {
        }

        void fmt(std::ostream& os) const {
            if (is_signed) {
                os << static_cast<i64>(v);
            } else {
                os << v;
            }
        }

        //}
    };

    switch (repr->variants.tag()) {
        case TypeReprVariantMode::TAG_NonZero: {
            auto& e = repr->variants.as_NonZero();
            MIR_ASSERT(localMirRes, nArms == 2, "NonZero optimised switch without two arms");
            // If this is an emulated i128, check both fields
            of << indent << "if( ";
            emitLvalue(val);
            const auto& slotTy = emitEnumPath(repr, e.field);
            MIR_ASSERT(localMirRes, slotTy->is_Pointer() || slotTy->is_Function() || slotTy->is_Borrow() || slotTy->is_Primitive(), "Invalid niche type: " << slotTy << " in " << ty);
            if (typeIsEmulatedI128(slotTy)) {
                // The niche is "the whole value is zero", so both
                // halves have to be tested, either one being set makes
                // it the non-zero variant.
                of << ".lo != 0 || ";
                emitLvalue(val);
                emitEnumPath(repr, e.field);
                of << ".hi";
            }
            of << " != 0 )\n";
            of << indent << "\t";
            cb.emit(1 - e.zeroVariant);
            of << "\n";
            of << indent << "else\n";
            of << indent << "\t";
            cb.emit(e.zeroVariant);
            of << "\n";
            break;
        }
        case TypeReprVariantMode::TAG_Linear: {
            auto& e = repr->variants.as_Linear();
            const auto& tagTy = TargetGetInnerType(sp, resolve_, *repr, e.field.index, e.field.subFields);
            const bool pointerTag = tagTy->is_Pointer() || tagTy->is_Borrow() || tagTy->is_Function();
            if (!pointerTag) {
                switch (tagTy->as_Primitive()) {
                    case HIRCoreType::Bool:
                    case HIRCoreType::U8:
                    case HIRCoreType::I8:
                    case HIRCoreType::U16:
                    case HIRCoreType::I16:
                    case HIRCoreType::U32:
                    case HIRCoreType::I32:
                    case HIRCoreType::U64:
                    case HIRCoreType::I64:
                    case HIRCoreType::Usize:
                    case HIRCoreType::Isize:
                    case HIRCoreType::Char:
                        break;
                    default:
                        MIR_BUG(localMirRes, "Invalid tag type?! " << tagTy);
                }
            }

            auto emitVariant = [&]() {
                if (pointerTag) {
                    of << "(uintptr_t)";
                } else {
                    of << "(" << tagUnsignedType(e.field.size) << ")";
                }
                emitLvalue(val);
                emitEnumPath(repr, e.field);
            };
            auto tagOf = [&](size_t varIdx) {
                return tagBits(e.field.size, e.tagValue(varIdx));
            };

            // Optimisation: If there's only one arm with a different value, then emit an `if` isntead of a `switch`
            if (oddArm != static_cast<size_t>(-1)) {
                of << indent << "if( ";
                if (e.isNiche(oddArm)) {
                    bool firstComparison = true;
                    for (size_t j = 0; j < nArms; j++) {
                        if (j == oddArm || e.isNiche(j)) {
                            continue;
                        }
                        if (!firstComparison) {
                            of << " && ";
                        }
                        emitVariant();
                        of << " != " << tagOf(j) << "ull";
                        firstComparison = false;
                    }
                    MIR_ASSERT(localMirRes, !firstComparison, "Niche switch without explicit tag values");
                } else {
                    emitVariant();
                    of << " == " << tagOf(oddArm) << "ull";
                }
                of << ") {";
                cb.emit(oddArm);
                of << "} else {";
                cb.emit(oddArm == 0 ? 1 : 0);
                of << "}\n";
            } else {
                of << indent << "switch(";
                emitVariant();
                of << ") {\n";
                for (size_t j = 0; j < nArms; j++) {
                    if (e.isNiche(j)) {
                        continue;
                    }
                    of << indent << "case " << tagOf(j) << "ull: ";
                    cb.emit(j);
                    of << "break;\n";
                }
                of << indent << "default: ";
                if (e.usesNiche()) {
                    cb.emit(e.field.index);
                    of << "break;";
                } else {
                    of << "abort();";
                }
                of << "\n";
                of << indent << "}\n";
            }
            break;
        }
        case TypeReprVariantMode::TAG_Values: {
            auto& e = repr->variants.as_Values();
            const auto& tagTy = TargetGetInnerType(sp, resolve_, *repr, e.field.index, e.field.subFields);
            bool is_signed = false;
            switch (tagTy->as_Primitive()) {
                case HIRCoreType::I8:
                case HIRCoreType::I16:
                case HIRCoreType::I32:
                case HIRCoreType::I64:
                case HIRCoreType::Isize:
                case HIRCoreType::I128:
                    is_signed = true;
                    break;
                case HIRCoreType::Bool:
                case HIRCoreType::U8:
                case HIRCoreType::U16:
                case HIRCoreType::U32:
                case HIRCoreType::U64:
                case HIRCoreType::Usize:
                case HIRCoreType::Char:
                case HIRCoreType::U128:
                    is_signed = false;
                    break;
                case HIRCoreType::F16:
                case HIRCoreType::F32:
                case HIRCoreType::F64:
                case HIRCoreType::F128:
                    MIR_TODO(localMirRes, "Floating point enum tag.");
                    break;
                case HIRCoreType::Str:
                    MIR_BUG(localMirRes, "Unsized tag?!");
            }

            const bool is128 = tagTy == HIRCoreType::I128 || tagTy == HIRCoreType::U128;
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
                cb.emit(oddArm);
                of << "} else {";
                cb.emit(oddArm == 0 ? 1 : 0);
                of << "}\n";
                return;
            }

            if (is128) {
                for (size_t j = 0; j < nArms; j++) {
                    of << indent << (j == 0 ? "if(" : "else if(");
                    emitEqual(j);
                    of << ") {";
                    cb.emit(j);
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
                    const auto value = S128(e.values[j]).truncateI64();
                    // `-9223372036854775808ll` is a negated positive literal
                    // that does not fit in `long long`; write it as a
                    // subtraction so the constant stays in range.
                    if (value == INT64_MIN) {
                        of << indent << "case (-9223372036854775807ll - 1): ";
                    } else {
                        of << indent << "case " << value << "ll: ";
                    }
                } else {
                    of << indent << "case " << e.values[j].truncateU64() << "ull: ";
                }
                cb.emit(j);
                of << "break;\n";
            }
            of << indent << "default: abort();\n";
            of << indent << "}\n";
            break;
        }
        case TypeReprVariantMode::TAG_None: {
            of << indent;
            cb.emit(0);
            of << "\n";
            break;
        }
    }
}

template <typename F>
auto CodeGeneratorC::emitTermSwitch(const MIRTypeResolve& localMirRes, const MIRLValue& val, size_t nArms, unsigned indentLevel, F f, size_t oddArm) -> void {
    CSwitchArmCb<F> cb(f);
    emitTermSwitchCb(localMirRes, val, nArms, indentLevel, cb, oddArm);
}

auto CodeGeneratorC::emitTermSwitchvalueCb(const MIRTypeResolve& localMirRes, const MIRLValue& val, const MIRSwitchValues& values, unsigned indentLevel, CSwitchArmCallback& cb) -> void {
    auto indent = RepeatLitStr{"\t", static_cast<int>(indentLevel)};

    HIRTypeRef tmp;
    const auto& ty = localMirRes.getLvalueType(tmp, val);
    if (const auto* ve = values.opt_String()) {
        of << indent << "{ static SLICE_PTR switch_strings[] = {";
        for (const auto& v : *ve) {
            of << " {(void*)";
            this->printEscapedString(v);
            of << "," << v.size() << "},";
        }
        of << " {0,0} };\n";
        of << indent << "switch( trustme_string_search_linear(";
        emitLvalue(val);
        of << ", " << ve->size() << ", switch_strings) ) {\n";
        for (size_t i = 0; i < ve->size(); i++) {
            of << indent << "case " << i << ": ";
            cb.emit(i);
            of << " break;\n";
        }
        of << indent << "default: ";
        cb.emit(SIZE_MAX);
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
        HIRTypeRef tmp;
        const auto& ty = localMirRes.getLvalueType(tmp, val);
        of << indent << "switch( trustme_string_search_linear(";
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
            cb.emit(i);
            of << " break;\n";
        }
        of << indent << "default: ";
        cb.emit(SIZE_MAX);
        of << "\n";
        of << indent << "} }\n";
    } else if (const auto* ve = values.opt_Unsigned()) {
        const bool emulatedU128 = options.emulatedI128 && ty == HIRCoreType::U128;
        if (emulatedU128) {
            of << indent << "if(";
            emitLvalue(val);
            of << ".hi != 0) { ";
            cb.emit(SIZE_MAX);
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
            cb.emit(i);
            of << " break;\n";
        }
        of << indent << "\tdefault: ";
        cb.emit(SIZE_MAX);
        of << "\n";
        of << indent << "}\n";
    } else if (const auto* ve = values.opt_Signed()) {
        const bool emulatedI128 = options.emulatedI128 && ty == HIRCoreType::I128;
        if (emulatedI128) {
            of << indent << "if(";
            emitLvalue(val);
            of << ".hi != ((i64)";
            emitLvalue(val);
            of << ".lo < 0 ? UINT64_MAX : 0)) { ";
            cb.emit(SIZE_MAX);
            of << " }\n";
        }
        of << indent << (emulatedI128 ? "else " : "") << "switch(";
        if (emulatedI128) {
            of << "(i64)";
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
            cb.emit(i);
            of << " break;\n";
        }
        of << indent << "\tdefault: ";
        cb.emit(SIZE_MAX);
        of << "\n";
        of << indent << "}\n";
    } else {
        MIR_BUG(localMirRes, "SwitchValue with unknown value type - " << values.tagStr());
    }
}

auto CodeGeneratorC::calleeAbi(const MIRTypeResolve& localMirRes, const MIRCallTarget& fcn) -> RcString {
    if (const auto* pathP = fcn.opt_Path()) {
        MonomorphState msTmp(crate.types);
        auto v = resolve_.getValue(sp, *pathP, msTmp, /*signature_only=*/true);
        if (const auto* f = v.opt_Function()) {
            return (**f).abi;
        }
    } else if (const auto* valP = fcn.opt_Value()) {
        HIRTypeRef tmp;
        const auto& ty = localMirRes.getLvalueType(tmp, *valP);
        if (const auto* ft = ty->opt_Function()) {
            return ft->abi;
        }
    }
    return RcString::newInterned(ABI_RUST);
}

auto CodeGeneratorC::mangleResolvedValuePath(const HIRPath& path) const -> RcString {
    if (!path.data.is_UfcsKnown()) {
        return TransMangleValue(path);
    }

    // A concrete trait impl is emitted under its declaration's Self
    // and trait parameters. An equivalent caller path can retain
    // lifetime identity that is absent from that published symbol.
    MonomorphState params(crate.types);
    StaticTraitResolve::ResolvedTraitImplPath implPath;
    resolve_.getValue(sp, path, params, /*signatureOnly=*/false, nullptr, &implPath);
    if (!implPath.type) {
        return TransMangleValue(path);
    }

    auto canonical = path.clone();
    auto& pe = canonical.data.as_UfcsKnown();
    pe.type = implPath.type;
    pe.trait.params = mv$(implPath.traitParams);
    return TransMangleValue(canonical);
}

auto CodeGeneratorC::emitTermCall(const MIRTypeResolve& localMirRes, const MIRTerminator::Data_Call& e, unsigned indentLevel, bool tailCall) -> void {
    auto indent = RepeatLitStr{"\t", static_cast<int>(indentLevel)};
    const auto* targetPath = e.fcn.opt_Path();
    const bool targetTracksCaller = e.tracksCaller || (targetPath && trackedFunctions.count(*targetPath) != 0);
    if (tailCall && e.fcn.is_Intrinsic()) {
        MIR_BUG(localMirRes, "Intrinsic used as an explicit tail-call target");
    }
    of << indent;

    const auto calleeAbi = this->calleeAbi(localMirRes, e.fcn);
    bool hasZst = false;
    for (unsigned int j = 0; j < e.args.size(); j++) {
        HIRTypeRef tmp;
        const auto& ty = mirRes->getParamType(tmp, e.args[j]);
        if (!argumentIsPassed(calleeAbi, ty)) {
            continue;
        }
        if (options.disallowEmptyStructs /*&& (e.args[j].is_LValue() && (e.args[j].as_LValue().is_Field()))*/) {
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

    bool omitAssign = tailCall;

    // If the return type is `()`, omit the assignment (all `()` returning functions are marked as returning
    // void)
    {
        HIRTypeRef tmp;
        if (mirRes->getLvalueType(tmp, e.retVal) == crate.types.unit()) {
            omitAssign = true;
        }

        if (this->typeIsBadZst(mirRes->getLvalueType(tmp, e.retVal))) {
            omitAssign = true;
        }
    }

    if (tailCall) {
        if (targetTracksCaller == currentFunctionTracksCaller) {
            of << "TRUSTME_MUSTTAIL ";
        }
        of << "return ";
    }

    switch (e.fcn.tag()) {
        case MIRCallTarget::TAG_Value: {
            auto& e2 = e.fcn.as_Value();
            {
                HIRTypeRef tmp;
                const auto& ty = localMirRes.getLvalueType(tmp, e2);
                MIR_ASSERT(localMirRes, ty->is_Function(), "Call::Value on non-function - " << ty);

                const auto& retTy = ty->as_Function().rettype;
                omitAssign |= retTy->is_Diverge();
                if (!omitAssign) {
                    emitLvalue(e.retVal);
                    of << " = ";
                }
            }
            of << "(";
            emitLvalue(e2);
            of << ")";
            break;
        }
        case MIRCallTarget::TAG_Path: {
            auto& e2 = e.fcn.as_Path();
            {
                switch (e2.data.tag()) {
                    case HIRPathData::TAG_Generic: {
                        auto& pe = e2.data.as_Generic();
                        const auto& fcn = crate.getFunctionByPath(sp, pe.path);
                        omitAssign |= fcn.returnType->is_Diverge();
                        // TODO: Monomorph.
                        break;
                    }
                    case HIRPathData::TAG_UfcsUnknown: {
                        break;
                    }
                    case HIRPathData::TAG_UfcsInherent: {
                        auto& pe = e2.data.as_UfcsInherent();
                        // Check if the return type is !
                        omitAssign |= resolve_.hirCrate().findTypeImpls(pe.type, HIRResolvePlaceholdersNop(), [&](const auto& impl) {
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
                        break;
                    }
                    case HIRPathData::TAG_UfcsKnown: {
                        auto& pe = e2.data.as_UfcsKnown();
                        // Check if the return type is !
                        const auto& tr = resolve_.hirCrate().getTraitByPath(sp, pe.trait.path);
                        const auto& fcn = tr.values.find(pe.item)->second.as_Function();
                        const auto& rvTpl = fcn.returnType;
                        if (rvTpl->is_Diverge() || rvTpl == crate.types.unit()) {
                            omitAssign |= true;
                        } else if (const auto* te = rvTpl->opt_Generic()) {
                            // TODO: Generic lookup
                        } else if (const auto* te = rvTpl->opt_Path()) {
                            if (te->binding.is_Opaque()) {
                                // TODO: Associated type lookup
                            }
                        } else {
                            // Not a ! type
                        }
                        break;
                    }
                }
                if (!omitAssign) {
                    emitLvalue(e.retVal);
                    of << " = ";
                }
            }
            of << mangleResolvedValuePath(e2);
            break;
        }
        case MIRCallTarget::TAG_Intrinsic: {
            auto& e2 = e.fcn.as_Intrinsic();
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
    bool firstCallArgument = true;
    for (unsigned int j = 0; j < e.args.size(); j++) {
        HIRTypeRef tmp;
        const auto& ty = mirRes->getParamType(tmp, e.args[j]);
        if (!argumentIsPassed(calleeAbi, ty)) {
            continue;
        }
        if (!firstCallArgument) {
            of << ", ";
        }
        firstCallArgument = false;

        if (this->typeIsBadZst(ty)) {
            of << "zarg" << j;
            continue;
        }
        if (this->isDst(ty)) {
            emitDstParamPointer(e.args[j]);
            of << ".PTR, ";
            emitDstParamPointer(e.args[j]);
            of << ".META";
            continue;
        }
        emitParam(e.args[j]);
    }
    if (targetTracksCaller) {
        if (!firstCallArgument) {
            of << ", ";
        }
        if (currentFunctionTracksCaller) {
            of << "trustme_caller";
        } else {
            emitCallerLocationPointer(e.source);
        }
    }
    of << ");\n";

    if (hasZst) {
        indent.n--;
        of << indent << "}\n";
    }
}

auto CodeGeneratorC::emitTermTailCall(const MIRTypeResolve& localMirRes, const MIRTerminator::Data_TailCall& e, unsigned indentLevel) -> void {
    MIRCallTarget target;
    switch (e.fcn.tag()) {
        case MIRCallTarget::TAG_Value: {
            auto& value = e.fcn.as_Value();
            target = MIRCallTarget::make_Value(value.clone());
            break;
        }
        case MIRCallTarget::TAG_Path: {
            auto& path = e.fcn.as_Path();
            target = MIRCallTarget::make_Path(path.clone());
            break;
        }
        case MIRCallTarget::TAG_Intrinsic: {
            auto& intrinsic = e.fcn.as_Intrinsic();
            target = MIRCallTarget::make_Intrinsic({intrinsic.name, intrinsic.params.clone()});
            break;
        }
    }
    ::std::vector<MIRParam> args;
    args.reserve(e.args.size());
    for (const auto& arg : e.args) {
        args.push_back(arg.clone());
    }
    MIRTerminator::Data_Call call{
        0,
        MIRUnwindAction::make_Continue({}),
        MIRLValue::newReturn(),
        mv$(target),
        mv$(args),
        e.source,
        e.tracksCaller,
    };
    emitTermCall(localMirRes, call, indentLevel, true);
}

auto CodeGeneratorC::asmMatchesTemplate(const MIRStatement::Data_Asm& e, const char* tpl, ::std::initializer_list<const char*> inputs, ::std::initializer_list<const char*> outputs) -> bool {
    struct H {
        static bool checkList(const std::vector<std::pair<std::string, MIRLValue>>& have, const ::std::initializer_list<const char*>& exp) {
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

auto CodeGeneratorC::emitAsmGcc(const MIRTypeResolve& localMirRes, const MIRStatement::Data_Asm& e, unsigned indentLevel) -> void {
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

    bool isVolatile = H::hasFlag(e.flags, "volatile");
    bool isIntel = H::hasFlag(e.flags, "intel");

    // The following clobber overlaps with an output
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
        of << indent << "__asm__ __volatile__ (\".att_syntax prefix; pushfl; popl %%%0; .intel_syntax noprefix\" : \"=r\" (";
        emitLvalue(e.outputs[0].second);
        of << ") : : );\n";
        return;
    }
    if (asmMatchesTemplate(e, "pushl $0; popfd", {"r"}, {})) {
        of << indent << "__asm__ __volatile__ (\".att_syntax prefix; pushl %%%0; popfl; .intel_syntax noprefix\" : : \"r\" (";
        emitLvalue(e.inputs[0].second);
        of << ") : );\n";
        return;
    }

    of << indent << "__asm__ ";
    if (isVolatile) {
        of << "__volatile__";
    }
    const bool emitAttSyntax = usesIntelCompilerAsmDialect() && !isIntel;
    of << "(\"" << (emitAttSyntax ? ".att_syntax prefix; " : "");
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
        } else if (*it == '%' && isdigit(*(it + 1)) && emitAttSyntax) {
            of << "%%%";
        } else if (*it == '%' && !isdigit(*(it + 1))) {
            of << "%%";
        } else if (*it == '$' && isdigit(*(it + 1)) && *(it + 2) != 'x') {
            of << (emitAttSyntax ? "%%%" : "%");
        }
        // Hack for `${0:b}` seen with `setc`, just emit as `%0`
        else if (*it == '$' && *(it + 1) == '{') {
            of << (emitAttSyntax ? "%%%" : "%") << *(it + 2);
            while (it != e.tpl.end() && *it != '}') {
                it++;
            }
        } else {
            of << *it;
        }
    }
    of << (emitAttSyntax ? ".intel_syntax noprefix; " : "") << "\"";
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

auto CodeGeneratorC::emitAsm2Gcc(const MIRTypeResolve& localMirRes, const MIRStatement& stmt, unsigned indentLevel) -> void {
    const auto& e = stmt.as_Asm2();
    emitAsm2Gcc(localMirRes, e.options, e.lines, e.params, false, ~0u, indentLevel);
}

auto CodeGeneratorC::emitAsm2Gcc(const MIRTypeResolve& localMirRes, const AsmOptions& asmOptions, const std::vector<AsmLine>& asmLines, const std::vector<MIRAsmParam>& asmParams, bool asmGoto, MIRBasicBlockId retBlock, unsigned indentLevel) -> void {
    auto indent = RepeatLitStr{"\t", static_cast<int>(indentLevel)};
    Asm2TplMatch m{localMirRes, asmLines, asmParams};

    // The following clobber overlaps with an output
    if (m.matchesTemplate({"movq %rbx, {0:r}", "cpuid", "xchgq %rbx, {0:r}"}, {"lateout:reg", "inlateout=eax", "inlateout=ecx", "lateout=edx"})) {
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
        of << indent << "__asm__(\".att_syntax prefix; bt %%%1, (%%%2); setc %%%0; .intel_syntax noprefix\"";
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
        of << indent << "__asm__(\".att_syntax prefix; btc %%%1, (%%%2); setc %%%0; .intel_syntax noprefix\"";
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
        of << indent << "__asm__(\".att_syntax prefix; btr %%%1, (%%%2); setc %%%0; .intel_syntax noprefix\"";
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
        of << indent << "__asm__(\".att_syntax prefix; bts %%%1, (%%%2); setc %%%0; .intel_syntax noprefix\"";
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
    else if (asmLines[0].frags.size() > 0 && (false || asmLines[0].frags[0].before.find("vmov") == 0 || asmLines[0].frags[0].before.find("vexpand") == 0 || asmLines[0].frags[0].before.find("vpexpand") == 0)) {
        of << "abort();\n";
        return;
    } else {
        std::vector<unsigned> argMappings(asmParams.size(), UINT_MAX);
        // If there is an explicit register, create a block and add `register uintptr_t asm_REGNAME asm("REGNAME");`
        // - Requires updating the arg mappings, as doing so would remove the argument from the list.
        bool blockOpen = false;
        for (size_t i = 0; i < asmParams.size(); i++) {
            if (const auto* pe = asmParams[i].opt_Reg()) {
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
        // A vector register class takes a vector, and the C type a SIMD
        // value is emitted as is a struct -- which the compiler will not
        // put in one. Copy such an operand through a vector of the same
        // width, which it will.
        Vector<size_t> vectorShim;
        vectorShim.zero(asmParams.size());
        auto paramIndexOf = [&](const MIRAsmParam::Data_Reg* reg) {
            for (size_t i = 0; i < asmParams.size(); i++) {
                if (asmParams[i].opt_Reg() == reg) {
                    return i;
                }
            }
            return asmParams.size();
        };
        for (size_t i = 0; i < asmParams.size(); i++) {
            const auto* pe = asmParams[i].opt_Reg();
            if (!pe || (!pe->input && !pe->output)) {
                continue;
            }
            const auto* regClass = pe->spec.opt_Class();
            if (!regClass || (*regClass != AsmRegisterClass::x86Xmm && *regClass != AsmRegisterClass::x86Ymm && *regClass != AsmRegisterClass::x86Zmm)) {
                continue;
            }
            HIRTypeRef tmp;
            const auto* opTy = pe->input ? mirRes->getParamType(tmp, *pe->input) : mirRes->getLvalueType(tmp, *pe->output);
            if (opTy->is_Primitive() || opTy->is_Pointer() || opTy->is_Borrow()) {
                continue;
            }
            size_t opSize = 0;
            if (!TargetGetSizeOf(sp, resolve_, opTy, opSize) || opSize == 0) {
                continue;
            }
            if (!blockOpen) {
                blockOpen = true;
                of << indent << "{\n";
            }
            vectorShim.mut(i) = opSize;
            of << indent << "typedef long long asm_vec_ty_" << i << " __attribute__((vector_size(" << opSize << ")));\n";
            of << indent << "asm_vec_ty_" << i << " asm_vec_" << i << ";\n";
            if (pe->input) {
                of << indent << "memcpy(&asm_vec_" << i << ", &";
                emitParam(*pe->input);
                of << ", " << opSize << ");\n";
            }
        }

        std::vector<const MIRAsmParam::Data_Reg*> outputs;
        // Outputs
        for (size_t i = 0; i < asmParams.size(); i++) {
            if (const auto* pe = asmParams[i].opt_Reg()) {
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
        std::vector<const MIRAsmParam*> inputs;
        for (size_t i = 0; i < asmParams.size(); i++) {
            if (const auto* pe = asmParams[i].opt_Reg()) {
                if (pe->spec.opt_Explicit()) {
                    // Ignore, handled explicitly above
                    // An in+out explicit register is fully covered by its
                    // read-write output constraint; emitting a matching
                    // input for it too is rejected by clang.
                    if (pe->input && !pe->output) {
                        inputs.push_back(&asmParams[i]);
                    }
                } else if (pe->input) {
                    if (!pe->output) {
                        argMappings[i] = outputs.size() + inputs.size();
                    }
                    inputs.push_back(&asmParams[i]);
                }
            }
        }
        // Clobbers
        std::vector<const char*> clobbers;
        for (size_t i = 0; i < asmParams.size(); i++) {
            // An explicit register, not "In" and output parameter
            if (const auto* pe = asmParams[i].opt_Reg()) {
                if (!pe->input && !pe->output && pe->spec.is_Explicit()) {
                    const auto& regname = pe->spec.as_Explicit();
                    clobbers.push_back(regname.c_str());
                }
            }
        }

        const bool emitAttSyntax = usesIntelCompilerAsmDialect() && asmOptions.attSyntax;
        of << indent << "__asm__ ";
        of << "__volatile__"; // Default everything to volatile
        if (asmGoto) {
            of << " goto";
        }
        of << "(\"";
        if (emitAttSyntax) {
            of << ".att_syntax prefix; ";
        }
        bool escapePercent = true || !inputs.empty() || !outputs.empty();
        for (const auto& l : asmLines) {
            for (const auto& f : l.frags) {
                of << FmtGccAsm(f.before, escapePercent);
                const auto& param = asmParams.at(f.index);
                if (const auto* constant = param.opt_Const()) {
                    MIR_ASSERT(localMirRes, f.modifier == '\0', "Modifier on asm const operand");
                    auto text = inlineAsmConstant(*constant);
                    of << FmtGccAsm(text, escapePercent);
                    continue;
                }
                if (const auto* path = param.opt_Sym()) {
                    MIR_ASSERT(localMirRes, f.modifier == '\0', "Modifier on asm sym operand");
                    auto text = asmSymbol(localMirRes.sp, *path);
                    of << FmtGccAsm(text, escapePercent);
                    continue;
                }
                if (param.is_Label()) {
                    MIR_ASSERT(localMirRes, asmGoto && f.modifier == '\0', "Invalid asm label operand");
                    of << "%l[bb" << param.as_Label() << "]";
                    continue;
                }
                MIR_ASSERT(localMirRes, argMappings.at(f.index) != UINT_MAX, "Invalid asm operand mapping");
                if (emitAttSyntax) {
                    of << "%%";
                }
                of << "%";
                if (argMappings.at(f.index) == UINT_MAX - 1) {
                    of << asmParams[f.index].as_Reg().spec.as_Explicit();
                    continue;
                }
                switch (f.modifier) {
                    case '\0': {
                        if (const auto* reg = asmParams[f.index].opt_Reg()) {
                            if (const auto* regClass = reg->spec.opt_Class()) {
                                switch (*regClass) {
                                    case AsmRegisterClass::x86Reg:
                                    case AsmRegisterClass::x86RegAbcd:
                                        of << (TargetGetCurSpec(wb_).arch.name == "x86_64" ? 'q' : 'k');
                                        break;
                                    case AsmRegisterClass::x86RegByte:
                                        of << 'b';
                                        break;
                                    default:
                                        break;
                                }
                            }
                        }
                        break;
                    }
                    // Rust names the part of a register it wants by
                    // width; gcc names the same parts by other letters.
                    case 'l':
                        of << 'b'; // x86: the low byte, `al`
                        break;
                    case 'h':
                        of << 'h'; // x86: the second byte, `ah`
                        break;
                    case 'x': {
                        // On a general register this is the low half,
                        // `ax`; on a vector register it is the whole
                        // 128-bit one, which gcc spells the same way.
                        const auto* opClass = asmParams[f.index].as_Reg().spec.opt_Class();
                        const bool vector = opClass && (*opClass == AsmRegisterClass::x86Xmm || *opClass == AsmRegisterClass::x86Ymm || *opClass == AsmRegisterClass::x86Zmm);
                        of << (vector ? 'x' : 'w');
                        break;
                    }
                    case 'y':
                        of << 't'; // x86: the 256-bit `ymm`
                        break;
                    case 'z':
                        of << 'g'; // x86: the 512-bit `zmm`
                        break;
                    case 'e':
                        of << 'k'; // x86: `k` selects eax instead of rax
                        break;
                    case 'r':
                        of << 'q'; // x86: `q` selects rax explicitly
                        break;
                    default:
                        MIR_TODO(localMirRes, "Asm2 GCC: modifier " << f.modifier);
                }
                of << argMappings.at(f.index);
            }
            of << FmtGccAsm(l.trailing, escapePercent);
            of << ";\\n ";
        }
        if (emitAttSyntax) {
            of << ".intel_syntax noprefix; ";
        }
        of << "\"";
        if (asmOptions.naked) {
            MIR_ASSERT(localMirRes, outputs.empty() && inputs.empty() && clobbers.empty() && !blockOpen, "naked_asm contains register operands");
            of << ");\n";
            return;
        }
        of << " :";
        for (size_t i = 0; i < outputs.size(); i++) {
            const auto& p = *outputs[i];
            if (i != 0) {
                of << ",";
            }
            of << " ";
            of << "\"";
            if (!p.output && !p.input) {
                of << "+";
            } else if (p.input && p.spec.is_Explicit()) {
                of << (p.dir == AsmDirection::InOut ? "+&" : "+");
            } else {
                switch (p.dir) {
                    case AsmDirection::Out:
                    case AsmDirection::InOut:
                        of << "=&";
                        break;
                    case AsmDirection::LateOut:
                    case AsmDirection::InLateOut:
                        of << "=";
                        break;
                    case AsmDirection::In:
                        MIR_BUG(localMirRes, "Input-only asm parameter listed as an output");
                }
            }
            switch (p.spec.tag()) {
                case AsmRegisterSpec::TAG_Class: {
                    auto& c = p.spec.as_Class();
                    // https://gcc.gnu.org/onlinedocs/gcc/Machine-Constraints.html
                    switch (c) {
                        // x86
                        case AsmRegisterClass::x86Reg:
                            of << "r";
                            break;
                        case AsmRegisterClass::x86RegAbcd:
                            of << "Q";
                            break;
                        case AsmRegisterClass::x86RegByte:
                            of << "q";
                            break;
                        case AsmRegisterClass::x86Xmm:
                            of << "x";
                            break;
                        case AsmRegisterClass::x86Ymm:
                            of << "x";
                            break;
                        case AsmRegisterClass::x86Zmm:
                            of << "v";
                            break;
                        case AsmRegisterClass::x86Kreg:
                            of << "Yk";
                            break;
                        // riscv
                        case AsmRegisterClass::riscvReg:
                            of << "r";
                            break;
                        case AsmRegisterClass::riscvFreg:
                            of << "f";
                            break;
                    }
                    break;
                }
                case AsmRegisterSpec::TAG_Explicit: {
                    of << "r";
                    break;
                }
            }
            of << "\" (";
            if (!p.output) {
                of << "asm_anon_" << i;
            } else if (const auto* regnameP = p.spec.opt_Explicit()) {
                of << "asm_" << *regnameP;
            } else if (const auto shimIdx = paramIndexOf(&p); shimIdx != asmParams.size() && vectorShim[shimIdx] != 0) {
                of << "asm_vec_" << shimIdx;
            } else {
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
            switch (p.tag()) {
                case MIRAsmParam::TAG_Reg: {
                    auto& r = p.as_Reg();
                    of << "\"";
                    if (r.output && !r.spec.is_Explicit()) {
                        const auto it = ::std::find(outputs.begin(), outputs.end(), &r);
                        MIR_ASSERT(localMirRes, it != outputs.end(), "Missing asm output");
                        of << (it - outputs.begin());
                    } else {
                        switch (r.spec.tag()) {
                            case AsmRegisterSpec::TAG_Class: {
                                auto& c = r.spec.as_Class();
                                switch (c) {
                                    // x86
                                    case AsmRegisterClass::x86Reg:
                                        of << "r";
                                        break;
                                    case AsmRegisterClass::x86RegAbcd:
                                        of << "Q";
                                        break;
                                    case AsmRegisterClass::x86RegByte:
                                        of << "q";
                                        break;
                                    case AsmRegisterClass::x86Xmm:
                                        of << "x";
                                        break;
                                    case AsmRegisterClass::x86Ymm:
                                        of << "x";
                                        break;
                                    case AsmRegisterClass::x86Zmm:
                                        of << "v";
                                        break;
                                    case AsmRegisterClass::x86Kreg:
                                        of << "Yk";
                                        break;
                                    // riscv
                                    case AsmRegisterClass::riscvReg:
                                        of << "r";
                                        break;
                                    case AsmRegisterClass::riscvFreg:
                                        of << "f";
                                        break;
                                }
                                break;
                            }
                            case AsmRegisterSpec::TAG_Explicit: {
                                of << "r";
                                break;
                            }
                        }
                    }
                    assert(r.input);
                    of << "\" (";
                    const auto shimIdx = paramIndexOf(&r);
                    if (const auto* regnameP = p.as_Reg().spec.opt_Explicit()) {
                        of << "asm_" << *regnameP;
                    } else if (shimIdx != asmParams.size() && vectorShim[shimIdx] != 0) {
                        of << "asm_vec_" << shimIdx;
                    } else {
                        emitParam(*r.input);
                    }
                    of << ")";
                    break;
                }
                case MIRAsmParam::TAG_Const: {
                    MIR_TODO(localMirRes, "Asm2 GCC - Const");
                    break;
                }
                case MIRAsmParam::TAG_Sym: {
                    MIR_TODO(localMirRes, "Asm2 GCC - Sym");
                    break;
                }
                case MIRAsmParam::TAG_Label: {
                    MIR_BUG(localMirRes, "Asm label listed as an input");
                    break;
                }
            }
        }
        of << " :";
        for (size_t i = 0; i < clobbers.size(); i++) {
            if (i > 0) {
                of << ",";
            }
            // GCC spells the top of the x87 stack `st`, where Rust
            // spells it `st(0)`; the rest of the stack agrees.
            of << " \"" << (::std::strcmp(clobbers[i], "st(0)") == 0 ? "st" : clobbers[i]) << "\"";
        }
        if (asmGoto) {
            of << " :";
            bool firstLabel = true;
            for (size_t i = 0; i < asmParams.size(); ++i) {
                if (const auto* label = asmParams[i].opt_Label()) {
                    if (!firstLabel) {
                        of << ",";
                    }
                    firstLabel = false;
                    of << " bb" << forwardedBlockTarget(*label);
                }
            }
        }
        of << ");\n";
        for (size_t i = 0; i < asmParams.size(); i++) {
            if (vectorShim[i] != 0) {
                const auto* pe = asmParams[i].opt_Reg();
                if (pe->output) {
                    of << indent << "memcpy(&";
                    emitLvalue(*pe->output);
                    of << ", &asm_vec_" << i << ", " << vectorShim[i] << ");\n";
                }
            }
        }
        for (size_t i = 0; i < asmParams.size(); i++) {
            if (const auto* pe = asmParams[i].opt_Reg()) {
                if (const auto* regnameP = pe->spec.opt_Explicit()) {
                    if (pe->output) {
                        of << indent;
                        emitLvalue(*pe->output);
                        of << " = ";
                        HIRTypeRef tmp;
                        of << "(";
                        emitCtype(mirRes->getLvalueType(tmp, *pe->output));
                        of << ")";
                        of << "asm_" << *regnameP << ";\n";
                    }
                }
            }
        }
        if (asmGoto) {
            if (retBlock == ~0u) {
                of << indent << "__builtin_unreachable();\n";
            } else {
                const auto target = forwardedBlockTarget(retBlock);
                if (blockIsInlinedReturn(target)) {
                    of << indent << "return";
                    if (localMirRes.retType != crate.types.unit()) {
                        of << " rv";
                    }
                    of << ";\n";
                } else if (target != fallthroughBlock) {
                    of << indent << "goto bb" << target << ";\n";
                }
            }
        }
        if (blockOpen) {
            of << indent << "}\n";
        }
    }
}

auto CodeGeneratorC::resolveFunction(const HIRPath& path) -> const HIRFunction* {
    MonomorphState ms(crate.types);
    auto value = resolve_.getValue(sp, path, ms, /*signatureOnly=*/true);
    if (const auto* function = value.opt_Function()) {
        return *function;
    }
    return nullptr;
}

auto CodeGeneratorC::pathTracksCaller(const HIRPath& path) -> bool {
    if (trackedFunctions.count(path) != 0) {
        return true;
    }
    const auto* function = resolveFunction(path);
    return function && crate.functionTracksCaller(sp, path, *function);
}

auto CodeGeneratorC::emitSourceLocationInitializer(const SourceLocation& source) -> void {
    of << "{{(void*)";
    printEscapedStringInner(source.filename.c_str(), source.filename.c_str() + source.filename.size());
    of << "," << source.filename.size() << "}," << source.line << "," << source.column << "}";
}

auto CodeGeneratorC::callerLocationHash(const SourceLocation& source) const -> u64 {
    return source.filename.contentHash() ^ splitMix64(source.line + 0x9e3779b97f4a7c15ULL) ^ splitMix64(source.column + 0xd6e8feb86659fd93ULL);
}

auto CodeGeneratorC::internCallerLocation(const SourceLocation& source) -> CallerLocationNode* {
    const auto hash = callerLocationHash(source);
    auto* head = callerLocations.find(hash);
    for (auto* node = head ? *head : nullptr; node; node = node->hashNext) {
        if (node->source == source) {
            return node;
        }
    }

    auto* node = callerLocationPool->make<CallerLocationNode>(source, callerLocationCount++, head ? *head : nullptr);
    if (head) {
        *head = node;
    } else {
        callerLocations.insert(hash, node);
    }
    if (lastCallerLocation) {
        lastCallerLocation->orderNext = node;
    } else {
        firstCallerLocation = node;
    }
    lastCallerLocation = node;
    return node;
}

auto CodeGeneratorC::emitCallerLocationPointer(const SourceLocation& source) -> void {
    const auto* location = internCallerLocation(source);
    of << "&trustme_caller_locations[" << location->index << "]";
}

auto CodeGeneratorC::emitCallerLocationDefinitions() -> void {
    of << "namespace {\n"
       << "const trustme_caller_location trustme_caller_locations[] = {\n";
    if (!firstCallerLocation) {
        of << "\t{},\n";
    } else {
        for (const auto* location = firstCallerLocation; location; location = location->orderNext) {
            of << "\t";
            emitSourceLocationInitializer(location->source);
            of << ",\n";
        }
    }
    of << "};\n}\n";
}

auto CodeGeneratorC::promotedName(const HIRPath& path) -> const HIRPath& {
    const auto* gp = path.data.opt_Generic();
    if (!gp || gp->path.components().empty()) {
        return path;
    }
    const auto& last = gp->path.components().back();
    if (strncmp(last.c_str(), "const#", 6) != 0 && strncmp(last.c_str(), "lifted#", 7) != 0) {
        return path;
    }
    MonomorphState msTmp(crate.types);
    auto v = resolve_.getValue(sp, path, msTmp, /*signature_only=*/true);
    const auto* stat = v.opt_Static();
    if (!stat || !promotedIsShared(**stat)) {
        return path;
    }
    const auto* value = promotedValue(path, **stat);
    auto statTy = msTmp.monomorphType(sp, (**stat).type);
    if (!value || !promotedTypeIsSettled(statTy)) {
        return path;
    }
    const auto* held = promotedHolder(statTy, *value);
    return held ? *held : path;
}

auto CodeGeneratorC::emitReifiedFunctionName(const HIRPath& path, bool preserveTrackCaller) -> void {
    of << mangleResolvedValuePath(promotedName(path));
    if (!preserveTrackCaller && pathTracksCaller(path)) {
        of << "__trustme_reify";
    }
}

auto CodeGeneratorC::monomorphiseFcnReturn(HIRTypeRef& tmp, const HIRFunction& item, const TransParams& params) -> const HIRTypeData* {
    bool hasErased = visitTyWith(item.returnType, [&](const auto& x) {
        return x->is_ErasedType();
    });

    if (hasErased || monomorphiseTypeNeeded(item.returnType)) {
        // If there's an erased type, make a copy with the erased type expanded
        if (hasErased) {
            tmp = cloneTyWith(crate.types, sp, item.returnType, [&](const auto& x, auto& out) {
                if (const auto* te = x->opt_ErasedType()) {
                    if (const auto* e = te->inner.opt_Fcn()) {
                        out = item.code.erasedTypes.at(e->index);
                        return true;
                    }
                }
                return false;
            });
            tmp = params.monomorphType(Span(), tmp);
        } else {
            tmp = params.monomorphType(Span(), item.returnType);
        }
        resolve_.expandAssociatedTypes(Span(), tmp);
        return tmp;
    } else {
        return item.returnType;
    }
}

auto CodeGeneratorC::argumentIsPassed(const RcString& abi, const HIRTypeData* ty) -> bool {
    // `rust-call`, `rust-intrinsic` and friends are Rust's own ABI
    // under another name, and pass what the emitted code passes.
    if (abi == ABI_RUST || abi == "unadjusted" || strncmp(abi.c_str(), "rust-", 5) == 0) {
        return true;
    }
    size_t size = 0;
    return !(TargetGetSizeOf(sp, resolve_, ty, size) && size == 0);
}

auto CodeGeneratorC::emitFunctionHeader(const HIRPath& p, const HIRFunction& item, const TransParams& params, bool includeCallerLocation, const char* nameSuffix) -> void {
    HIRTypeRef tmp;
    const auto& retTy = monomorphiseFcnReturn(tmp, item, params);
    const bool hasCallerLocation = includeCallerLocation && crate.functionTracksCaller(sp, p, item);
    unsigned int passedCount = 0;
    unsigned int parameterCount = item.variadic + hasCallerLocation;
    for (unsigned int i = 0; i < item.fixedArgCount(); i++) {
        auto ty = params.monomorph(resolve_, item.args[i].second);
        if (!argumentIsPassed(item.abi, ty)) {
            continue;
        }
        passedCount++;
        const auto metadata = metadataType(ty);
        parameterCount += (metadata == MetadataType::Slice || metadata == MetadataType::TraitObject) ? 2 : 1;
    }
    const bool compact = parameterCount <= 5;
    if (item.markings.isNaked) {
        of << "__attribute__((naked)) ";
    }
    if (item.markings.inlineType == HIRFunction::Markings::Inline::Always) {
        of << "__attribute__((always_inline)) ";
    }
    if (item.markings.alignment != 0) {
        of << "__attribute__((aligned(" << item.markings.alignment << "))) ";
    }
    auto cb = FMT_CB(ss, ss << " " << compilerAbiAttribute(item.abi) << TransMangleValue(p) << nameSuffix << "("; if (passedCount == 0 && !hasCallerLocation && !item.variadic) { ss << "void)"; } else {
        unsigned int emitted = 0;
        for (unsigned int i = 0; i < item.fixedArgCount(); i++) {
            auto ty = params.monomorph(resolve_, item.args[i].second);
            if (!argumentIsPassed(item.abi, ty)) {
                continue;
            }
            if (compact) {
                if (emitted != 0) {
                    ss << ", ";
                }
            } else {
                ss << "\n\t\t";
            }
            this->emitFunctionArgument(ty, FMT_CB(os, os << "arg" << i;));
            emitted++;
            if (!compact && (item.variadic || emitted < passedCount || hasCallerLocation)) {
                of << ",";
            }
        }

        if (item.variadic) {
            if (compact) {
                of << (emitted != 0 ? ", ..." : "...");
            } else {
                of << "\n\t\t...";
            }
            emitted++;
        }

        if (hasCallerLocation) {
            MIR_ASSERT(*mirRes, !item.variadic, "#[track_caller] on a variadic function");
            if (compact) {
                if (emitted != 0) {
                    of << ", ";
                }
            } else {
                of << "\n\t\t";
            }
            of << "const trustme_caller_location* trustme_caller";
        }

        ss << (compact ? ")" : "\n\t\t)");
    });
    if (retTy != crate.types.unit()) {
        emitCtype(retTy, cb);
    } else {
        of << "void " << cb;
    }
}

auto CodeGeneratorC::emitTrackCallerReifyWrapper(const HIRPath& p, const HIRFunction& item, const TransParams& params) -> void {
    MIR_ASSERT(*mirRes, !item.variadic, "Cannot reify a variadic #[track_caller] function");
    of << "static ";
    emitFunctionHeader(p, item, params, /*includeCallerLocation=*/false, "__trustme_reify");
    of << " {\n";
    of << "\t";

    HIRTypeRef returnTypeTmp;
    const auto& returnType = monomorphiseFcnReturn(returnTypeTmp, item, params);
    if (returnType != crate.types.unit()) {
        of << "return ";
    }
    of << TransMangleValue(p) << "(";
    bool first = true;
    auto emitArgument = [&](const char* prefix, unsigned index, const char* suffix) {
        if (!first) {
            of << ", ";
        }
        first = false;
        of << prefix << index << suffix;
    };
    for (unsigned int i = 0; i < item.args.size(); i++) {
        auto type = params.monomorph(resolve_, item.args[i].second);
        switch (metadataType(type)) {
            case MetadataType::Unknown:
                MIR_BUG(*mirRes, type << " has unknown function-argument metadata");
            case MetadataType::None:
            case MetadataType::Zero:
                emitArgument("arg", i, "");
                break;
            case MetadataType::Slice:
            case MetadataType::TraitObject:
                emitArgument("arg", i, "_ptr");
                emitArgument("arg", i, "_meta");
                break;
        }
    }
    if (!first) {
        of << ", ";
    }
    emitCallerLocationPointer(item.source);
    of << ");\n";
    if (returnType == crate.types.unit()) {
        of << "\treturn;\n";
    }
    of << "}\n\n";
}

auto CodeGeneratorC::tagUnsignedType(size_t size) -> const char* {
    switch (size) {
        case 1:
            return "u8";
        case 2:
            return "u16";
        case 4:
            return "u32";
        default:
            return "u64";
    }
}

auto CodeGeneratorC::tagBits(size_t size, size_t value) -> uint64_t {
    const auto v = static_cast<uint64_t>(value);
    return (size == 0 || size >= 8) ? v : (v & ((uint64_t(1) << (size * 8)) - 1));
}

auto CodeGeneratorC::emitIntrinsicCall(const RcString& name, const HIRPathParams& params, const MIRTerminator::Data_Call& e) -> void {
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
        UNREACHABLE();
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
        UNREACHABLE();
    };
    auto getPrimSize = [&localMirRes](const HIRTypeData* ty) -> unsigned {
        if (ty->is_Pointer()) {
            return TargetGetPointerBits();
        }
        if (!ty->is_Primitive()) {
            MIR_BUG(localMirRes, "Unknown type for getting primitive size - " << ty);
        }
        switch (ty->as_Primitive()) {
            case HIRCoreType::U8:
            case HIRCoreType::I8:
                return 8;
            case HIRCoreType::U16:
            case HIRCoreType::I16:
                return 16;
            case HIRCoreType::U32:
            case HIRCoreType::I32:
                return 32;
            case HIRCoreType::U64:
            case HIRCoreType::I64:
                return 64;
            case HIRCoreType::U128:
            case HIRCoreType::I128:
                return 128;
            case HIRCoreType::Usize:
            case HIRCoreType::Isize:
                // TODO: Is this a good idea?
                return TargetGetPointerBits();
            default:
                MIR_BUG(localMirRes, "Unknown primitive for getting size- " << ty);
        }
    };
    auto getRealPrimTy = [](HIRCoreType ct) -> HIRCoreType {
        switch (ct) {
            case HIRCoreType::Usize:
                if (TargetGetPointerBits() == 64) {
                    return HIRCoreType::U64;
                }
                if (TargetGetPointerBits() == 32) {
                    return HIRCoreType::U32;
                }
                BUG(Span(), "");
            case HIRCoreType::Isize:
                if (TargetGetPointerBits() == 64) {
                    return HIRCoreType::I64;
                }
                if (TargetGetPointerBits() == 32) {
                    return HIRCoreType::I32;
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
    const bool atomicTypeIsPointer = params.types.size() > 0 && params.types.at(0)->is_Pointer();
    auto emitAtomicRmwCast = [&]() {
        if (atomicTypeIsPointer) {
            of << "(";
            emitCtype(params.types.at(0));
            of << ")";
        }
    };
    auto emitAtomicRmwOperand = [&](const MIRParam& param) {
        if (atomicTypeIsPointer) {
            of << "(uintptr_t)";
        }
        emitParam(param);
    };
    auto emitAtomicCxchg = [&](const auto& e, Ordering oSucc, Ordering oFail, bool isWeak) {
        const bool emulatedI128 = typeIsEmulatedI128(params.types.at(0));
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
        if (emulatedI128) {
            of << "{ ";
            emitCtype(params.types.at(0), FMT_CB(ss, ss << " trustme_atomic_desired";));
            of << " = ";
            emitParam(e.args.at(2));
            of << "; ";
        }
        emitLvalue(e.retVal);
        of << "._0 = ";
        emitParam(e.args.at(1));
        of << ";\n\t";
        emitLvalue(e.retVal);
        of << "._1 = " << (emulatedI128 ? "__atomic_compare_exchange(" : "__atomic_compare_exchange_n(");
        emitAtomicCast();
        emitParam(e.args.at(0));
        of << ", &";
        emitLvalue(e.retVal);
        of << "._0"; // Expected (i.e. the check value)
        of << ", ";
        if (emulatedI128) {
            of << "&trustme_atomic_desired";
        } else {
            emitParam(e.args.at(2)); // `desired` (the new value for the slot if equal)
        }
        of << ", " << (isWeak ? "true" : "false");
        of << ", " << getAtomicTyGcc(oSucc) << ", " << getAtomicTyGcc(oFail) << ")";
        if (emulatedI128) {
            of << "; }";
        }
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
        MIR_ASSERT(localMirRes, TargetGetSizeOf(sp, resolve_, params.types.at(0), size), "Can't get size of " << params.types.at(0));
        emitLvalue(e.retVal);
        of << " = " << size;
    } else if (name == "offset_of") {
        size_t val = localMirRes.intrinsicOffsetOf(params.types.at(0), e.args);
        emitLvalue(e.retVal);
        of << " = " << val;
    } else if (name == "min_align_of" || name == "align_of") {
        size_t align = 0;
        MIR_ASSERT(localMirRes, TargetGetAlignOf(sp, resolve_, params.types.at(0), align), "Can't get alignment of " << params.types.at(0));
        emitLvalue(e.retVal);
        of << " = " << align;
    } else if (name == "vtable_size" || name == "vtable_align") {
        emitLvalue(e.retVal);
        of << " = ((VTABLE_HDR*)";
        emitParam(e.args.at(0));
        of << ")->" << (name == "vtable_size" ? "size" : "align");
    } else if (name == "size_of_val") {
        const auto& ty = params.types.at(0);
        // Get the unsized type and use that in place of MetadataType
        auto innerTy = getInnerUnsizedType(ty);
        if (isExternUnsizedType(innerTy)) {
            emitExternTypeLayoutPanic(innerTy);
        } else {
            emitLvalue(e.retVal);
            of << " = ";
            if (innerTy == HIRTypeRef()) {
                size_t size = 0;
                MIR_ASSERT(localMirRes, TargetGetSizeOf(sp, resolve_, ty, size), "Can't get size of " << ty);
                of << size;
            }
            // slice metadata (`[T]` and `str`)
            else if (innerTy->is_Slice() || innerTy == HIRCoreType::Str) {
                bool alignNeeded = false;
                size_t itemSize = 0;
                size_t itemAlign = 0;
                if (const auto* te = innerTy->opt_Slice()) {
                    MIR_ASSERT(localMirRes, TargetGetSizeAndAlignOf(sp, resolve_, te->inner, itemSize, itemAlign), "Can't get size of " << te->inner);
                } else {
                    assert(innerTy == HIRCoreType::Str);
                    itemSize = 1;
                    itemAlign = 1;
                }
                if (!ty->is_Slice() && !ty->is_Primitive()) {
                    // A wrapper's own prefix is only part of the size:
                    // the tail may be another wrapper, whose prefix
                    // counts too.
                    emitDstSize(ty, e.args.at(0));
                } else {
                    emitParam(e.args.at(0));
                    of << ".META * " << itemSize;
                }
            }
            // Trait object metadata.
            else if (innerTy->is_TraitObject()) {
                emitDstSize(ty, e.args.at(0));
            } else {
                MIR_BUG(localMirRes, "Unknown inner unsized type " << innerTy << " for " << ty);
            }
        }
        // TODO: Align up
    } else if (name == "min_align_of_val" || name == "align_of_val") {
        const auto& ty = params.types.at(0);
        auto innerTy = getInnerUnsizedType(ty);
        if (isExternUnsizedType(innerTy)) {
            emitExternTypeLayoutPanic(innerTy);
        } else {
            emitLvalue(e.retVal);
            of << " = ";
            if (innerTy == HIRTypeRef()) {
                size_t alignment = 0;
                MIR_ASSERT(localMirRes, TargetGetAlignOf(sp, resolve_, ty, alignment), "Can't get alignment of " << ty);
                of << alignment;
            } else if (const auto* te = innerTy->opt_Slice()) {
                of << "ALIGNOF(";
                if (ty->is_Slice()) {
                    emitCtype(te->inner);
                } else {
                    emitCtype(ty);
                }
                of << ")";
            } else if (innerTy == HIRCoreType::Str) {
                if (!ty->is_Primitive()) {
                    of << "ALIGNOF(";
                    emitCtype(ty);
                    of << ")";
                } else {
                    of << "1";
                }
            } else if (innerTy->is_TraitObject()) {
                emitDstAlign(ty, e.args.at(0));
            } else {
                MIR_BUG(localMirRes, "Unknown inner unsized type " << innerTy << " for " << ty);
            }
        }
    }
    // --- Type assertions ---
    else if (name == "panic_if_uninhabited" || name == "assert_inhabited") {
        // TODO: Detect uninhabited (empty enum or `!` - potentially via nested types)
    } else if (name == "assert_zero_valid") {
        // TODO: Detect nonzero within
    } else if (name == "assert_mem_uninitialized_valid") {
        // TODO: Detect nonzero or enum within
    } else if (name == "const_eval_select") {
        const auto& argTyTuple = params.types.at(0)->as_Tuple();
        const auto& arg = e.args.at(0).as_LValue();
        // Note: arg 1 is the constant function
        const auto& fcnPath = *e.args.at(2).as_Constant().as_Function().p;

        // Reuse ordinary call emission for the runtime branch of const_eval_select.
        ::std::vector<MIRParam> args;
        args.reserve(argTyTuple.size());
        for (size_t i = 0; i < argTyTuple.size(); i++) {
            args.push_back(MIRLValue::newField(arg.clone(), i));
        }
        auto pseudoTerm = MIRTerminator::Data_Call{e.retBlock, MIRUnwindAction::make_Continue({}), e.retVal.clone(), MIRCallTarget::make_Path(fcnPath.clone()), std::move(args)};
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
        of << "(uintptr_t)&__typeid_" << TransMangleTypeId(ty);
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
        auto isPtr = [](const HIRTypeData* ty) {
            return ty->is_Borrow() || ty->is_Pointer();
        };
        if (this->typeIsBadZst(tyDst)) {
            return;
        }
        // A transmute keeps the size, so a zero-sized source has a
        // zero-sized destination and no bytes to copy. The source has
        // no storage to copy them from either, but C++ gives an empty
        // struct a byte, so clear the destination rather than leave it
        // holding whatever was in that byte.
        if (this->typeIsBadZst(tySrc)) {
            of << "memset(&";
            emitLvalue(e.retVal);
            of << ", 0, sizeof(";
            emitCtype(tyDst);
            of << "))";
        } else if (e.args.at(0).is_Constant()) {
            of << "{ ";
            emitCtype(tySrc, FMT_CB(s, s << "v";));
            of << " = ";
            emitParam(e.args.at(0));
            of << "; ";
            of << "memcpy(&";
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
            of << "memcpy(&";
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
        } else if (srcTy == HIRCoreType::F128) {
            emitLvalue(e.retVal);
            of << " = (";
            emitCtype(dstTy);
            of << ")f128_decode(";
            emitParam(e.args.at(0));
            of << ")";
        } else {
            emitLvalue(e.retVal);
            of << " = (";
            emitCtype(dstTy);
            of << ")";
            emitParam(e.args.at(0));
        }
    } else if (name == "copy_nonoverlapping" || name == "copy") {
        if (this->typeIsBadZst(params.types.at(0))) {
            return;
        }
        if (name == "copy") {
            of << "memmove";
        } else {
            of << "memcpy";
        }
        // 0: Source, 1: Destination, 2: Count
        of << "(";
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
            return;
        }
        // 0: Destination, 1: Value, 2: Count
        of << "if( ";
        emitParam(e.args.at(2));
        of << " > 0) memset(";
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
        of << " = memcmp(";
        emitParam(e.args.at(0));
        of << ", ";
        emitParam(e.args.at(1));
        of << ", ";
        emitParam(e.args.at(2));
        of << ")";
    } else if (name == "raw_eq") {
        size_t size = 0;
        MIR_ASSERT(localMirRes, TargetGetSizeOf(sp, resolve_, params.types.at(0), size), "Can't get size of " << params.types.at(0));

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
            of << (t == HIRCoreType::U128 ? "cmp128" : "cmp128s");
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
    } else if (name == "async_drop_state") {
        MIR_ASSERT(localMirRes, params.types.size() == 1, "async_drop_state expects its outer future type");
        const auto* repr = TargetGetTypeRepr(sp, resolve_, params.types[0]);
        MIR_ASSERT(localMirRes, repr && !repr->fields.empty(), "async-drop future has no state field");
        emitLvalue(e.retVal);
        of << " = (u8*)((u8*)";
        emitParam(e.args.at(0));
        of << " + " << repr->fields[0].offset << ")";
    } else if (name == "async_drop_storage") {
        MIR_ASSERT(localMirRes, params.types.size() == 2, "async_drop_storage expects outer and stored future types");
        const auto* repr = TargetGetTypeRepr(sp, resolve_, params.types[0]);
        MIR_ASSERT(localMirRes, repr && repr->fields.size() >= 3, "async-drop future has no suspension storage");
        emitLvalue(e.retVal);
        of << " = (";
        emitCtype(params.types[1]);
        of << "*)((u8*)";
        emitParam(e.args.at(0));
        of << " + " << repr->fields[2].offset << ")";
    } else if (name == "drop_in_place") {
        emitDestructorCall(MIRLValue::newDeref(e.args.at(0).as_LValue().clone()), params.types.at(0), true, /*indent_level=*/1 /* TODO: get from caller */);
    }
    // --- Type traits
    else if (name == "needs_drop") {
        // Returns `true` if the actual type given as `T` requires drop glue;
        // returns `false` if the actual type provided for `T` implements `Copy`. (Either otherwise)
        // NOTE: libarena assumes that this returns `true` iff T doesn't require drop glue.
        const auto& ty = params.types.at(0);
        emitLvalue(e.retVal);
        of << " = ";
        if (resolve_.typeNeedsDropGlue(localMirRes.sp, ty)) {
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
        of << "memset(&";
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
        of << " = 0; } catch (trustme_panic& panic) { (";
        emitParam(e.args.at(2));
        of << ")(";
        emitParam(e.args.at(1));
        of << ", (u8*)panic.rust_exception); ";
        emitLvalue(e.retVal);
        of << " = 1; } }";
    }
    // --- #[track_caller]
    else if (name == "caller_location") {
        MIR_ASSERT(localMirRes, currentFunctionTracksCaller, "`caller_location` used outside a #[track_caller] function");
        emitLvalue(e.retVal);
        of << " = (";
        HIRTypeRef callerTypeTmp;
        emitCtype(localMirRes.getLvalueType(callerTypeTmp, e.retVal));
        of << ")trustme_caller";
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
    } else if (name == "ptr_mask") {
        HIRTypeRef tmp;
        const auto& returnType = localMirRes.getLvalueType(tmp, e.retVal);
        MIR_ASSERT(localMirRes, returnType->is_Pointer(), "ptr_mask returned " << returnType);
        emitLvalue(e.retVal);
        of << " = (";
        emitCtype(returnType);
        of << ")((uintptr_t)";
        emitParam(e.args.at(0));
        of << " & (uintptr_t)";
        emitParam(e.args.at(1));
        of << ")";
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
        if (ty == HIRCoreType::U8 || ty == HIRCoreType::I8) {
            // Nop.
            emitLvalue(e.retVal);
            of << " = ";
            emitParam(e.args.at(0));
        } else if (getPrimSize(ty) == 128) {
            // There is no `__builtin_bswap128`, and a signed 128-bit
            // value has its own type here.
            emitLvalue(e.retVal);
            of << " = ";
            emitWide128Call(ty, "__trustme_bswap128", e.args.at(0));
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
        if (getPrimSize(ty) == 128) {
            emitWide128Call(ty, "__trustme_bitrev128", e.args.at(0));
        } else {
            switch (getPrimSize(ty)) {
                case 8:
                    of << "__trustme_bitrev8";
                    break;
                case 16:
                    of << "__trustme_bitrev16";
                    break;
                case 32:
                    of << "__trustme_bitrev32";
                    break;
                case 64:
                    of << "__trustme_bitrev64";
                    break;
                default:
                    MIR_TODO(localMirRes, "bitreverse<" << ty << ">");
            }
            of << "(";
            emitParam(e.args.at(0));
            of << ")";
        }
    }
    // > Obtain the discriminane of a &T as u64
    else if (name == "discriminant_value") {
        const auto& ty = params.types.at(0);
        emitLvalue(e.retVal);
        of << " = ";
        if (ty->is_Path() && (ty->as_Path().isGenerator() || ty->as_Path().isFuture())) {
            auto state = [&]() -> MIRLValue {
                if (const auto* value = e.args.at(0).opt_LValue()) {
                    return MIRLValue::newDeref(value->clone());
                }
                if (const auto* value = e.args.at(0).opt_Borrow()) {
                    return value->val.clone();
                }
                MIR_BUG(localMirRes, "Generator passed to `discriminant_value` by constant: " << e.args.at(0));
            }();
            state = MIRLValue::newField(mv$(state), 0);    // MaybeUninit<state>
            state = MIRLValue::newDowncast(mv$(state), 1); // MaybeUninit::value
            state = MIRLValue::newField(mv$(state), 0);    // ManuallyDrop::value
            state = MIRLValue::newField(mv$(state), 0);    // state discriminant
            emitLvalue(state);
            of << ".TAG";
        } else if (!(ty->is_Path() && ty->as_Path().binding.is_Enum())) {
            of << "0";
        } else {
            const auto* repr = TargetGetTypeRepr(sp, resolve_, ty);
            MIR_ASSERT(localMirRes, repr, "No repr for enum " << ty);
            switch (repr->variants.tag()) {
                break;
                case TypeReprVariantMode::TAG_None: {
                    of << "0";
                } break;
                    break;
                case TypeReprVariantMode::TAG_Values: {
                    auto& ve = repr->variants.as_Values();
                    of << "(*";
                    emitParam(e.args.at(0));
                    of << ")";
                    emitEnumPath(repr, ve.field);

                } break;
                    break;
                case TypeReprVariantMode::TAG_Linear: {
                    auto& ve = repr->variants.as_Linear();
                    const auto& tagTy = TargetGetInnerType(sp, resolve_, *repr, ve.field.index, ve.field.subFields);
                    const bool pointerTag = tagTy->is_Pointer() || tagTy->is_Borrow() || tagTy->is_Function();
                    auto emitTag = [&]() {
                        if (pointerTag) {
                            of << "(uintptr_t)";
                        } else {
                            of << "(" << tagUnsignedType(ve.field.size) << ")";
                        }
                        of << "(*";
                        emitParam(e.args.at(0));
                        of << ")";
                        emitEnumPath(repr, ve.field);
                    };
                    if (ve.usesNiche()) {
                        const auto start = tagBits(ve.field.size, ve.offset);
                        of << "( ";
                        emitTag();
                        of << " >= " << start << "ull && ";
                        emitTag();
                        of << " < " << (start + ve.nicheVariantCount()) << "ull";
                        of << " ? " << ve.nicheVariantStart() << " + ";
                        emitTag();
                        of << " - " << start << "ull";
                        of << " : ";
                        of << ve.field.index;
                        of << " )";
                    } else {
                        emitTag();
                    }

                } break;
                    break;
                case TypeReprVariantMode::TAG_NonZero: {
                    auto& ve = repr->variants.as_NonZero();
                    of << "(*";
                    emitParam(e.args.at(0));
                    of << ")";
                    emitEnumPath(repr, ve.field);
                    of << " ";
                    of << (ve.zeroVariant ? "==" : "!=");
                    of << " 0";

                } break;
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
        if (options.emulatedI128 && params.types.at(0) == HIRCoreType::U128) {
            emitLvalue(e.retVal);
            of << "._1 = add128_o";
            of << "(";
            emitParam(e.args.at(0));
            of << ", ";
            emitParam(e.args.at(1));
            of << ", &";
            emitLvalue(e.retVal);
            of << "._0)";
        } else if (options.emulatedI128 && params.types.at(0) == HIRCoreType::I128) {
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
        if (options.emulatedI128 && params.types.at(0) == HIRCoreType::U128) {
            emitLvalue(e.retVal);
            of << "._1 = sub128_o";
            of << "(";
            emitParam(e.args.at(0));
            of << ", ";
            emitParam(e.args.at(1));
            of << ", &";
            emitLvalue(e.retVal);
            of << "._0)";
        } else if (options.emulatedI128 && params.types.at(0) == HIRCoreType::I128) {
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
        if (options.emulatedI128 && params.types.at(0) == HIRCoreType::U128) {
            emitLvalue(e.retVal);
            of << "._1 = mul128_o";
            of << "(";
            emitParam(e.args.at(0));
            of << ", ";
            emitParam(e.args.at(1));
            of << ", &";
            emitLvalue(e.retVal);
            of << "._0)";
        } else if (options.emulatedI128 && params.types.at(0) == HIRCoreType::I128) {
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

        if (options.emulatedI128 && ty == HIRCoreType::U128) {
            of << "add128_o";
            of << "(";
            emitParam(e.args.at(0));
            of << ", ";
            emitParam(e.args.at(1));
            of << ", &";
            emitLvalue(e.retVal);
            of << ")";
        } else if (options.emulatedI128 && ty == HIRCoreType::I128) {
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
                case HIRCoreType::U8:
                case HIRCoreType::U16:
                case HIRCoreType::U32:
                case HIRCoreType::U64:
                    of << "-1"; // -1 should extend to MAX
                    break;
                case HIRCoreType::U128:
                    if (options.emulatedI128) {
                        of << "make128_raw(-1, -1)";
                    } else {
                        of << "-1";
                    }
                    break;
                // If the LHS is negative, then the only way overflow can happen is if the RHS is also negative, so saturate at negative.
                case HIRCoreType::I8:
                    of << "(";
                    emitParam(e.args.at(0));
                    of << " < 0 ? -0x80 : 0x7F)";
                    break;
                case HIRCoreType::I16:
                    of << "(";
                    emitParam(e.args.at(0));
                    of << " < 0 ? -0x8000 : 0x7FFF)";
                    break;
                case HIRCoreType::I32:
                    of << "(";
                    emitParam(e.args.at(0));
                    of << " < 0 ? (-0x7FFFFFFFl - 1) : 0x7FFFFFFFl)";
                    break;
                case HIRCoreType::I64:
                    of << "(";
                    emitParam(e.args.at(0));
                    of << " < 0 ? (-0x7FFFFFFF"
                          "FFFFFFFFll - 1) : 0x7FFFFFFF"
                          "FFFFFFFFll)";
                    break;
                case HIRCoreType::I128:
                    if (options.emulatedI128) {
                        of << "( (i64)(";
                        emitParam(e.args.at(0));
                        of << ".hi) < 0 ? make128s_raw(-0x7FFFFFFF"
                              "FFFFFFFFll - 1, 0) : make128s_raw(0x7FFFFFFF"
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
        if (options.emulatedI128 && ty == HIRCoreType::U128) {
            of << "sub128_o";
            of << "(";
            emitParam(e.args.at(0));
            of << ", ";
            emitParam(e.args.at(1));
            of << ", &";
            emitLvalue(e.retVal);
            of << ")";
        } else if (options.emulatedI128 && ty == HIRCoreType::I128) {
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
                case HIRCoreType::U8:
                case HIRCoreType::U16:
                case HIRCoreType::U32:
                case HIRCoreType::U64:
                    of << "0";
                    break;
                case HIRCoreType::U128:
                    if (options.emulatedI128) {
                        of << "make128(0)";
                    } else {
                        of << "0";
                    }
                    break;
                case HIRCoreType::I8:
                    of << "(";
                    emitParam(e.args.at(0));
                    of << " < 0 ? -0x80 : 0x7F)";
                    break;
                case HIRCoreType::I16:
                    of << "(";
                    emitParam(e.args.at(0));
                    of << " < 0 ? -0x8000 : 0x7FFF)";
                    break;
                case HIRCoreType::I32:
                    of << "(";
                    emitParam(e.args.at(0));
                    of << " < 0 ? (-0x7FFFFFFFl - 1) : 0x7FFFFFFFl)";
                    break;
                case HIRCoreType::I64:
                    of << "(";
                    emitParam(e.args.at(0));
                    of << " < 0 ? (-0x7FFFFFFF"
                          "FFFFFFFFll - 1) : 0x7FFFFFFF"
                          "FFFFFFFFll)";
                    break;
                case HIRCoreType::I128:
                    if (options.emulatedI128) {
                        of << "( (i64)(";
                        emitParam(e.args.at(0));
                        of << ".hi) < 0 ? make128s_raw(-0x7FFFFFFF"
                              "FFFFFFFFll - 1, 0) : make128s_raw(0x7FFFFFFF"
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
        if (options.emulatedI128 && params.types.at(0) == HIRCoreType::U128) {
            of << "mul128_o";
            of << "(";
            emitParam(e.args.at(0));
            of << ", ";
            emitParam(e.args.at(1));
            of << ", &";
            emitLvalue(e.retVal);
            of << ")";
        } else if (options.emulatedI128 && params.types.at(0) == HIRCoreType::I128) {
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
            if (params.types.at(0) == HIRCoreType::I128) {
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
            if (params.types.at(0) == HIRCoreType::I128) {
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
            if (params.types.at(0) == HIRCoreType::I128) {
                of << "s";
            }
            of << "(";
            emitParam(e.args.at(0));
            of << ", ";
            emitParam(e.args.at(1));
            // If the shift type is a u128/i128, get the inner
            HIRTypeRef tmp;
            const auto& shiftTy = localMirRes.getParamType(tmp, e.args.at(1));
            if (shiftTy == HIRCoreType::I128 || shiftTy == HIRCoreType::U128) {
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
            if (params.types.at(0) == HIRCoreType::I128) {
                of << "s";
            }
            of << "(";
            emitParam(e.args.at(0));
            of << ", ";
            emitParam(e.args.at(1));
            // If the shift type is a u128/i128, get the inner
            HIRTypeRef tmp;
            const auto& shiftTy = localMirRes.getParamType(tmp, e.args.at(1));
            if (shiftTy == HIRCoreType::I128 || shiftTy == HIRCoreType::U128) {
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
            case HIRCoreType::I8:
            case HIRCoreType::U8:
                of << "{";
                of << " u8 v = ";
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
            case HIRCoreType::I16:
            case HIRCoreType::U16:
                of << "{";
                of << " u16 v = ";
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
            case HIRCoreType::I32:
            case HIRCoreType::U32:
                of << "{";
                of << " u32 v = ";
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
            case HIRCoreType::I64:
            case HIRCoreType::U64:
                of << "{";
                of << " u64 v = ";
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
            case HIRCoreType::I128:
            case HIRCoreType::U128:
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
            case HIRCoreType::I8:
            case HIRCoreType::U8:
                of << "{";
                of << " u8 v = ";
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
            case HIRCoreType::I16:
            case HIRCoreType::U16:
                of << "{";
                of << " u16 v = ";
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
            case HIRCoreType::I32:
            case HIRCoreType::U32:
                of << "{";
                of << " u32 v = ";
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
            case HIRCoreType::I64:
            case HIRCoreType::U64:
                of << "{";
                of << " u64 v = ";
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
            case HIRCoreType::I128:
            case HIRCoreType::U128:
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
        if (ty == HIRCoreType::U128 || ty == HIRCoreType::I128) {
            if (ty == HIRCoreType::I128) {
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
            if (ty == HIRCoreType::I128) {
                if (options.emulatedI128) {
                    of << "int128_to_uint128(";
                } else {
                    of << "(uint128_t)";
                }
            }
            emitParam(e.args.at(0));
            of << ")";
            if (ty == HIRCoreType::I128 && options.emulatedI128) {
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
        } else if (ty == HIRCoreType::U64 || ty == HIRCoreType::I64 || ((ty == HIRCoreType::Usize || ty == HIRCoreType::Isize) && TargetGetPointerBits() > 32)) {
            // Counting bits does not care about the sign, but the width
            // does: a signed 64-bit value used to fall through to the
            // 32-bit builtin.
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
                if (ty == HIRCoreType::U8 || ty == HIRCoreType::I8) {
                    of << "(u8)(";
                } else if (ty == HIRCoreType::U16 || ty == HIRCoreType::I16) {
                    of << "(u16)(";
                }
                emitParam(e.args.at(0));
                if (ty == HIRCoreType::U8 || ty == HIRCoreType::I8 || ty == HIRCoreType::U16 || ty == HIRCoreType::I16) {
                    of << ")";
                }
                of << ")";
                if (ty == HIRCoreType::U8 || ty == HIRCoreType::I8) {
                    of << " - 24";
                } else if (ty == HIRCoreType::U16 || ty == HIRCoreType::I16) {
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
        const auto& ty = params.types.at(0);
        emitLvalue(e.retVal);
        of << " = ";

        if (ty == HIRCoreType::I128 || ty == HIRCoreType::U128) {
            of << "popcount128(";
            if (ty == HIRCoreType::I128) {
                if (options.emulatedI128) {
                    of << "int128_to_uint128(";
                } else {
                    of << "(uint128_t)(";
                }
            }
            emitParam(e.args.at(0));
            if (ty == HIRCoreType::I128) {
                of << ")";
            }
            of << ")";
            if (options.emulatedI128) {
                of << ".lo";
            }
        } else {
            of << "__builtin_popcountll(";
            of << "(u" << getPrimSize(ty) << ")(";
            emitParam(e.args.at(0));
            of << "))";
        }
    }
    // --- Floating Point
    else if (name == "fadd_fast" || name == "fsub_fast" || name == "fmul_fast" || name == "fdiv_fast" || name == "frem_fast") {
        const auto& ty = params.types.at(0);
        MIR_ASSERT(localMirRes, ty->is_Primitive(), "Fast float intrinsic instantiated with " << ty);
        const auto coreTy = ty->as_Primitive();
        MIR_ASSERT(localMirRes, coreTy == HIRCoreType::F16 || coreTy == HIRCoreType::F32 || coreTy == HIRCoreType::F64 || coreTy == HIRCoreType::F128, "Fast float intrinsic instantiated with " << ty);

        emitLvalue(e.retVal);
        of << " = ";
        if (coreTy == HIRCoreType::F128) {
            of << "f128_";
            if (name == "fadd_fast") {
                of << "add";
            } else if (name == "fsub_fast") {
                of << "sub";
            } else if (name == "fmul_fast") {
                of << "mul";
            } else if (name == "fdiv_fast") {
                of << "div";
            } else {
                of << "mod";
            }
            of << "(";
            emitParam(e.args.at(0));
            of << ", ";
            emitParam(e.args.at(1));
            of << ")";
        } else if (name == "frem_fast") {
            of << (coreTy == HIRCoreType::F64 ? "__builtin_fmod" : "__builtin_fmodf") << "(";
            emitParam(e.args.at(0));
            of << ", ";
            emitParam(e.args.at(1));
            of << ")";
        } else {
            of << "(";
            emitParam(e.args.at(0));
            if (name == "fadd_fast") {
                of << " + ";
            } else if (name == "fsub_fast") {
                of << " - ";
            } else if (name == "fmul_fast") {
                of << " * ";
            } else {
                of << " / ";
            }
            emitParam(e.args.at(1));
            of << ")";
        }
    } else if ((name.size() > 3 && name.compare(name.size() - 3, 3, "f16") == 0) || (name.size() > 3 && name.compare(name.size() - 3, 3, "f32") == 0) || (name.size() > 3 && name.compare(name.size() - 3, 3, "f64") == 0) || (name.size() > 4 && name.compare(name.size() - 4, 4, "f128") == 0)) {
        const bool isF16 = name.compare(name.size() - 3, 3, "f16") == 0;
        const bool isF128 = name.size() > 4 && name.compare(name.size() - 4, 4, "f128") == 0;
        auto emitMathName = [&](const char* op) {
            of << "__builtin_";
            of << op << (isF16 || name.back() == '2' ? "f" : "");
        };
        auto emit1 = [&](const char* op) {
            emitLvalue(e.retVal);
            of << " = ";
            if (isF128) {
                of << "f128_" << op;
            } else {
                emitMathName(op);
            }
            of << "(";
            emitParam(e.args.at(0));
            of << ")";
        };
        // > Round to nearest integer, half-way rounds away from zero
        if (name == "rintf16" || name == "rintf32" || name == "rintf64" || name == "rintf128") {
            emit1("round");
        }
        // > Round to nearest integer, half-way rounds to even
        else if (name == "round_ties_even_f16" || name == "round_ties_even_f32" || name == "round_ties_even_f64" || name == "round_ties_even_f128") {
            emit1(isF128 ? "round_even" : "roundeven");
        } else if (name == "fabsf16" || name == "fabsf32" || name == "fabsf64" || name == "fabsf128") {
            emit1(isF128 ? "abs" : "fabs");
        } else if (name == "copysignf16" || name == "copysignf32" || name == "copysignf64" || name == "copysignf128") {
            emitLvalue(e.retVal);
            of << " = ";
            if (isF128) {
                of << "f128_copysign";
            } else {
                emitMathName("copysign");
            }
            of << "(";
            emitParam(e.args.at(0));
            of << ", ";
            emitParam(e.args.at(1));
            of << ")";
        }
        // > Returns the integer part of an `f32`.
        else if (name == "truncf16" || name == "truncf32" || name == "truncf64" || name == "truncf128") {
            emit1("trunc");
        } else if (name == "powif16" || name == "powif32" || name == "powif64" || name == "powif128") {
            emitLvalue(e.retVal);
            of << " = ";
            if (isF128) {
                of << "f128_powi";
            } else {
                emitMathName("pow");
            }
            of << "(";
            emitParam(e.args.at(0));
            of << ", ";
            emitParam(e.args.at(1));
            of << ")";
        } else if (name == "powf16" || name == "powf32" || name == "powf64" || name == "powf128") {
            emitLvalue(e.retVal);
            of << " = ";
            if (isF128) {
                of << "f128_pow";
            } else {
                emitMathName("pow");
            }
            of << "(";
            emitParam(e.args.at(0));
            of << ", ";
            emitParam(e.args.at(1));
            of << ")";
        } else if (name == "expf16" || name == "expf32" || name == "expf64" || name == "expf128") {
            emit1("exp");
        } else if (name == "exp2f16" || name == "exp2f32" || name == "exp2f64" || name == "exp2f128") {
            emit1("exp2");
        } else if (name == "logf16" || name == "logf32" || name == "logf64" || name == "logf128") {
            emit1("log");
        } else if (name == "log10f16" || name == "log10f32" || name == "log10f64" || name == "log10f128") {
            emit1("log10");
        } else if (name == "log2f16" || name == "log2f32" || name == "log2f64" || name == "log2f128") {
            emit1("log2");
        } else if (name == "sqrtf16" || name == "sqrtf32" || name == "sqrtf64" || name == "sqrtf128") {
            emit1("sqrt");
        } else if (name == "ceilf16" || name == "ceilf32" || name == "ceilf64" || name == "ceilf128") {
            emit1("ceil");
        } else if (name == "floorf16" || name == "floorf32" || name == "floorf64" || name == "floorf128") {
            emit1("floor");
        } else if (name == "roundf16" || name == "roundf32" || name == "roundf64" || name == "roundf128") {
            emit1("round");
        } else if (name == "cosf16" || name == "cosf32" || name == "cosf64" || name == "cosf128") {
            emit1("cos");
        } else if (name == "sinf16" || name == "sinf32" || name == "sinf64" || name == "sinf128") {
            emit1("sin");
        } else if (name == "fmaf16" || name == "fmaf32" || name == "fmaf64" || name == "fmaf128" || name == "fmuladdf16" || name == "fmuladdf32" || name == "fmuladdf64" || name == "fmuladdf128") {
            emitLvalue(e.retVal);
            of << " = ";
            if (isF128) {
                of << "f128_fma";
            } else {
                emitMathName("fma");
            }
            of << "(";
            emitParam(e.args.at(0));
            of << ", ";
            emitParam(e.args.at(1));
            of << ", ";
            emitParam(e.args.at(2));
            of << ")";
        } else if (name == "maxnumf16" || name == "maxnumf32" || name == "maxnumf64" || name == "maxnumf128") {
            emitLvalue(e.retVal);
            of << " = ";
            if (isF128) {
                of << "f128_max";
            } else {
                emitMathName("fmax");
            }
            of << "(";
            emitParam(e.args.at(0));
            of << ", ";
            emitParam(e.args.at(1));
            of << ")";
        } else if (name == "minnumf16" || name == "minnumf32" || name == "minnumf64" || name == "minnumf128") {
            emitLvalue(e.retVal);
            of << " = ";
            if (isF128) {
                of << "f128_min";
            } else {
                emitMathName("fmin");
            }
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
            if (this->typeIsCScalar(params.types.at(0))) {
                emitLvalue(e.retVal);
                of << " = *(volatile ";
                emitCtype(params.types.at(0));
                of << "*)";
                emitParam(e.args.at(0));
            } else {
                size_t valueSize = 0;
                MIR_ASSERT(localMirRes, TargetGetSizeOf(sp, resolve_, params.types.at(0), valueSize), "Can't get size of " << params.types.at(0));
                of << "__trustme_unaligned_volatile_load((void*)&";
                emitLvalue(e.retVal);
                of << ", (const void*)";
                emitParam(e.args.at(0));
                of << ", " << valueSize << ")";
            }
        }
    } else if (name == "unaligned_volatile_load") {
        size_t valueSize = 0;
        MIR_ASSERT(localMirRes, TargetGetSizeOf(sp, resolve_, params.types.at(0), valueSize), "Can't get size of " << params.types.at(0));
        if (valueSize == 0) {
            return;
        }
        of << "__trustme_unaligned_volatile_load((void*)&";
        emitLvalue(e.retVal);
        of << ", (const void*)";
        emitParam(e.args.at(0));
        of << ", " << valueSize << ")";
    } else if (name == "volatile_store") {
        if (!this->typeIsBadZst(params.types.at(0))) {
            if (this->typeIsCScalar(params.types.at(0))) {
                of << "*(volatile ";
                emitCtype(params.types.at(0));
                of << "*)";
                emitParam(e.args.at(0));
                of << " = ";
                emitParam(e.args.at(1));
            } else {
                size_t valueSize = 0;
                MIR_ASSERT(localMirRes, TargetGetSizeOf(sp, resolve_, params.types.at(0), valueSize), "Can't get size of " << params.types.at(0));
                of << "{ ";
                emitCtype(params.types.at(0));
                of << " trustme_value = ";
                emitParam(e.args.at(1));
                of << "; __trustme_unaligned_volatile_store((void*)";
                emitParam(e.args.at(0));
                of << ", (const void*)&trustme_value, " << valueSize << "); }";
            }
        }
    } else if (name == "unaligned_volatile_store") {
        size_t valueSize = 0;
        MIR_ASSERT(localMirRes, TargetGetSizeOf(sp, resolve_, params.types.at(0), valueSize), "Can't get size of " << params.types.at(0));
        if (valueSize == 0) {
            return;
        }
        of << "{ ";
        emitCtype(params.types.at(0));
        of << " trustme_value = ";
        emitParam(e.args.at(1));
        of << "; __trustme_unaligned_volatile_store((void*)";
        emitParam(e.args.at(0));
        of << ", (const void*)&trustme_value, " << valueSize << "); }";
    } else if (name == "volatile_copy_memory" || name == "volatile_copy_nonoverlapping_memory") {
        size_t elementSize = 0;
        MIR_ASSERT(localMirRes, TargetGetSizeOf(sp, resolve_, params.types.at(0), elementSize), "Can't get size of " << params.types.at(0));
        if (elementSize == 0) {
            return;
        }
        of << (name == "volatile_copy_memory" ? "__trustme_volatile_memmove" : "__trustme_volatile_memcpy");
        of << "((void*)";
        emitParam(e.args.at(0));
        of << ", (const void*)";
        emitParam(e.args.at(1));
        of << ", (size_t)";
        emitParam(e.args.at(2));
        of << " * " << elementSize << ")";
    } else if (name == "volatile_set_memory") {
        size_t elementSize = 0;
        MIR_ASSERT(localMirRes, TargetGetSizeOf(sp, resolve_, params.types.at(0), elementSize), "Can't get size of " << params.types.at(0));
        if (elementSize == 0) {
            return;
        }
        of << "__trustme_volatile_memset((void*)";
        emitParam(e.args.at(0));
        of << ", (u8)";
        emitParam(e.args.at(1));
        of << ", (size_t)";
        emitParam(e.args.at(2));
        of << " * " << elementSize << ")";
    } else if (name == "nontemporal_store") {
        // TODO: Actually do a non-temporal store
        // GCC: _mm_stream_* (depending on input type, which must be `repr(simd)`)
        if (!this->typeIsBadZst(params.types.at(0))) {
            if (this->typeIsCScalar(params.types.at(0))) {
                of << "*(volatile ";
                emitCtype(params.types.at(0));
                of << "*)";
                emitParam(e.args.at(0));
                of << " = ";
                emitParam(e.args.at(1));
            } else {
                size_t valueSize = 0;
                MIR_ASSERT(localMirRes, TargetGetSizeOf(sp, resolve_, params.types.at(0), valueSize), "Can't get size of " << params.types.at(0));
                of << "{ ";
                emitCtype(params.types.at(0));
                of << " trustme_value = ";
                emitParam(e.args.at(1));
                of << "; __trustme_unaligned_volatile_store((void*)";
                emitParam(e.args.at(0));
                of << ", (const void*)&trustme_value, " << valueSize << "); }";
            }
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
            of << "__trustme_atomicloop" << getPrimSize(ty) << "(";
            of << "(volatile u" << getPrimSize(ty) << "*)";
            emitParam(e.args.at(0));
            of << ", ";
            emitAtomicRmwOperand(e.args.at(1));
            of << ", " << getAtomicTyGcc(ordering);
            of << ", __trustme_op_and_not" << getPrimSize(ty);
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
            of << "__trustme_atomicloop" << getPrimSize(ty) << "(";
            of << "(volatile u" << getPrimSize(ty) << "*)";
            emitParam(e.args.at(0));
            of << ", ";
            emitAtomicRmwOperand(e.args.at(1));
            of << ", " << getAtomicTyGcc(ordering);
            of << ", __trustme_op_" << op << getPrimSize(ty);
            of << ")";
        } else if (name == "atomic_umax" || name.compare(0, 7 + 4 + 1, "atomic_umax_") == 0 || name == "atomic_umin" || name.compare(0, 7 + 4 + 1, "atomic_umin_") == 0) {
            auto ordering = getAtomicOrdering(name, 7 + 4 + 1);
            const auto& ty = params.types.at(0);
            const char* op = (name.c_str()[7 + 2] == 'a' ? "umax" : "umin"); // m'a'x vs m'i'n
            emitLvalue(e.retVal);
            of << " = ";
            emitAtomicRmwCast();
            of << "__trustme_atomicloop" << getPrimSize(ty) << "(";
            of << "(volatile u" << getPrimSize(ty) << "*)";
            emitParam(e.args.at(0));
            of << ", ";
            emitAtomicRmwOperand(e.args.at(1));
            of << ", " << getAtomicTyGcc(ordering);
            of << ", __trustme_op_" << op << getPrimSize(ty);
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
    else if (name == "va_arg") {
        emitLvalue(e.retVal);
        of << " = va_arg(*(va_list*)";
        emitParam(e.args.at(0));
        of << ", ";
        emitCtype(params.types.at(0));
        of << ")";
    } else if (name == "va_copy") {
        of << "va_copy(*(va_list*)";
        emitParam(e.args.at(0));
        of << ", *(va_list*)";
        emitParam(e.args.at(1));
        of << ")";
    } else if (name == "va_end") {
        of << "va_end(*(va_list*)";
        emitParam(e.args.at(0));
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

            static SimdInfo forTy(const CodeGeneratorC& self, const HIRTypeData* ty) {
                const auto* tyRepr = TargetGetTypeRepr(self.sp, self.mirRes->resolve, ty);
                MIR_ASSERT(*self.mirRes, tyRepr, "No repr for " << ty);
                size_t sizeSlot = tyRepr->size;
                const auto& ity = tyRepr->fields[0].ty;
                const auto& tyVal = ity->is_Primitive() ? ity : tyRepr->fields[0].ty->as_Array().inner;
                size_t sizeVal = 0;
                MIR_ASSERT(*self.mirRes, TargetGetSizeOf(self.sp, self.resolve_, tyVal, sizeVal), tyVal);

                MIR_ASSERT(*self.mirRes, sizeSlot >= sizeVal, sizeSlot << " < " << sizeVal);
                MIR_ASSERT(*self.mirRes, sizeVal > 0, "SimdInfo::for_ty - Value type " << tyVal << " was a ZST");
                MIR_ASSERT(*self.mirRes, sizeSlot / sizeVal * sizeVal == sizeSlot, sizeSlot << " not a multiple of " << sizeVal);

                SimdInfo rv;
                rv.itemSize = sizeVal;
                rv.count = sizeSlot == 0 ? 0 : sizeSlot / sizeVal;
                switch (tyVal->as_Primitive()) {
                    case HIRCoreType::I8:
                        rv.ty = Signed;
                        break;
                    case HIRCoreType::I16:
                        rv.ty = Signed;
                        break;
                    case HIRCoreType::I32:
                        rv.ty = Signed;
                        break;
                    case HIRCoreType::I64:
                        rv.ty = Signed;
                        break;
                    case HIRCoreType::Isize:
                        rv.ty = Signed;
                        break;
                    case HIRCoreType::U8:
                        rv.ty = Unsigned;
                        break;
                    case HIRCoreType::U16:
                        rv.ty = Unsigned;
                        break;
                    case HIRCoreType::U32:
                        rv.ty = Unsigned;
                        break;
                    case HIRCoreType::U64:
                        rv.ty = Unsigned;
                        break;
                    case HIRCoreType::Usize:
                        rv.ty = Unsigned;
                        break;
                    case HIRCoreType::F16:
                        rv.ty = Float;
                        break;
                    case HIRCoreType::F32:
                        rv.ty = Float;
                        break;
                    case HIRCoreType::F64:
                        rv.ty = Float;
                        break;
                    case HIRCoreType::F128:
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
                        self.of << "i" << (itemSize * 8);
                        break;
                    case Unsigned:
                        self.of << "u" << (itemSize * 8);
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
        // A one-argument reduction folds the lanes left to right with a
        // single operator, starting from the first lane.
        auto simdReduceFold = [&](const char* op) {
            auto info = SimdInfo::forTy(*this, params.types.at(0));
            MIR_ASSERT(localMirRes, e.args.size() == 1, name << " requires a vector");
            emitLvalue(e.retVal);
            of << " = ((";
            info.emitValTy(*this);
            of << "*)&";
            emitParam(e.args.at(0));
            of << ")[0]; ";
            of << "for(int i = 1; i < " << info.count << "; i++) ";
            emitLvalue(e.retVal);
            of << " " << op << "= ((";
            info.emitValTy(*this);
            of << "*)&";
            emitParam(e.args.at(0));
            of << ")[i]";
        };
        // `min` and `max` keep the lane that compares smaller/larger.
        auto simdReduceMinMax = [&](const char* cmp) {
            auto info = SimdInfo::forTy(*this, params.types.at(0));
            MIR_ASSERT(localMirRes, e.args.size() == 1, name << " requires a vector");
            emitLvalue(e.retVal);
            of << " = ((";
            info.emitValTy(*this);
            of << "*)&";
            emitParam(e.args.at(0));
            of << ")[0]; ";
            of << "for(int i = 1; i < " << info.count << "; i++) if( ((";
            info.emitValTy(*this);
            of << "*)&";
            emitParam(e.args.at(0));
            of << ")[i] " << cmp << " ";
            emitLvalue(e.retVal);
            of << " ) ";
            emitLvalue(e.retVal);
            of << " = ((";
            info.emitValTy(*this);
            of << "*)&";
            emitParam(e.args.at(0));
            of << ")[i]";
        };
        // `all` and `any` reduce a mask, whose lanes are all-ones or zero.
        auto simdReduceMask = [&](bool isAll) {
            auto info = SimdInfo::forTy(*this, params.types.at(0));
            MIR_ASSERT(localMirRes, e.args.size() == 1, name << " requires a mask vector");
            emitLvalue(e.retVal);
            of << " = " << (isAll ? "true" : "false") << "; ";
            of << "for(int i = 0; i < " << info.count << "; i++) ";
            emitLvalue(e.retVal);
            of << " = ";
            emitLvalue(e.retVal);
            of << (isAll ? " && " : " || ") << "( ((";
            info.emitValTy(*this);
            of << "*)&";
            emitParam(e.args.at(0));
            of << ")[i] != 0 )";
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
            TargetGetSizeOf(sp, resolve_, params.types.at(0), sizeSlot);
            TargetGetSizeOf(sp, resolve_, params.types.at(1), sizeVal);
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
            TargetGetSizeOf(sp, resolve_, params.types.at(0), sizeSlot);
            TargetGetSizeOf(sp, resolve_, params.types.at(1), sizeVal);
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
            TargetGetSizeOf(sp, resolve_, params.types.at(1), sizeOut);
            of << "{ u8* out = (u8*)&(";
            emitLvalue(e.retVal);
            of << "); memset(out, 0, " << sizeOut << "); ";
            for (size_t i = 0; i < srcInfo.count; i++) {
                of << "out[" << (i / 8) << "] |= ((((const u8*)&";
                emitParam(e.args.at(0));
                of << ")[" << (i * srcInfo.itemSize + srcInfo.itemSize - 1) << "] >> 7) & 1) << " << (i % 8) << "; ";
            }
            of << "}";
        } else if (nameStrip == "simd_shuffle128" || nameStrip == "simd_shuffle64" || nameStrip == "simd_shuffle32" || nameStrip == "simd_shuffle16" || nameStrip == "simd_shuffle8" || nameStrip == "simd_shuffle4" || nameStrip == "simd_shuffle2") {
            // Shuffle in 8 entries
            size_t sizeSlot = 0;
            TargetGetSizeOf(sp, resolve_, params.types.at(1), sizeSlot);
            size_t div = nameStrip == "simd_shuffle128" ? 128 : nameStrip == "simd_shuffle64" ? 64 : nameStrip == "simd_shuffle32" ? 32 : nameStrip == "simd_shuffle16" ? 16 : nameStrip == "simd_shuffle8" ? 8 : nameStrip == "simd_shuffle4" ? 4 : nameStrip == "simd_shuffle2" ? 2 : (UNREACHABLE(), 0);
            size_t sizeVal = sizeSlot / div;
            MIR_ASSERT(localMirRes, sizeVal > 0, sizeSlot << " / " << div << " == 0?");
            MIR_ASSERT(localMirRes, sizeSlot >= sizeVal, sizeSlot << " < " << sizeVal);
            MIR_ASSERT(localMirRes, sizeSlot / sizeVal * sizeVal == sizeSlot, sizeSlot << " not a multiple of " << sizeVal);
            // Indices address the concatenation of both input vectors, so the split
            // point is the INPUT element count, not the index count.
            size_t sizeIn = 0;
            TargetGetSizeOf(sp, resolve_, params.types.at(0), sizeIn);
            size_t nIn = sizeIn / sizeVal;
            MIR_ASSERT(localMirRes, nIn > 0, "Zero-sized shuffle input");
            of << "for(int i = 0; i < " << div << "; i++) { int j = ";
            emitParam(e.args.at(2));
            of << ".DATA[i];";
            of << "((u" << (sizeVal * 8) << "*)&";
            emitLvalue(e.retVal);
            of << ")[i]";
            of << " = ((u" << (sizeVal * 8) << "*)(j < " << nIn << " ? &";
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
            TargetGetSizeOf(sp, resolve_, vecTy, sizeVec);
            TargetGetSizeOf(sp, resolve_, mapTy, sizeMap);
            TargetGetSizeOf(sp, resolve_, retTy, sizeRet);
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
            of << " ((u" << (sizeVal * 8) << "*)&";
            emitLvalue(e.retVal);
            of << ")[i]";
            of << " = ((u" << (sizeVal * 8) << "*)(j < " << nIn << " ? &";
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
        else if (nameStrip == "simd_neg") {
            auto info = SimdInfo::forTy(*this, params.types.at(0));
            emitLvalue(e.retVal);
            of << " = ";
            emitParam(e.args.at(0));
            of << "; for(int i = 0; i < " << info.count << "; i++) ";
            if (info.ty == SimdInfo::Float) {
                of << "((";
                info.emitValTy(*this);
                of << "*)&";
                emitLvalue(e.retVal);
                of << ")[i] = -((";
                info.emitValTy(*this);
                of << "*)&";
                emitParam(e.args.at(0));
                of << ")[i]";
            } else {
                of << "((u" << (info.itemSize * 8) << "*)&";
                emitLvalue(e.retVal);
                of << ")[i] = 0 - ((u" << (info.itemSize * 8) << "*)&";
                emitParam(e.args.at(0));
                of << ")[i]";
            }
        } else if (nameStrip == "simd_add") {
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
        // Ordered reductions preserve their left-to-right operation
        // order and include the explicit accumulator argument.
        else if (nameStrip == "simd_reduce_add_ordered" || nameStrip == "simd_reduce_mul_ordered") {
            auto info = SimdInfo::forTy(*this, params.types.at(0));
            MIR_ASSERT(localMirRes, e.args.size() == 2, name << " requires a vector and accumulator");
            emitLvalue(e.retVal);
            of << " = ";
            emitParam(e.args.at(1));
            of << "; ";
            of << "for(int i = 0; i < " << info.count << "; i++) ";
            if (info.ty == SimdInfo::Float) {
                emitLvalue(e.retVal);
                of << (nameStrip == "simd_reduce_add_ordered" ? " += " : " *= ");
                of << "((";
                info.emitValTy(*this);
                of << "*)&";
                emitParam(e.args.at(0));
                of << ")[i]";
            } else {
                of << (nameStrip == "simd_reduce_add_ordered" ? "__builtin_add_overflow(" : "__builtin_mul_overflow(");
                emitLvalue(e.retVal);
                of << ", ((";
                info.emitValTy(*this);
                of << "*)&";
                emitParam(e.args.at(0));
                of << ")[i], &";
                emitLvalue(e.retVal);
                of << ")";
            }
        }
        // platform:simd_saturating_add
        // platform:simd_saturating_sub
        else if (nameStrip == "simd_reduce_add_unordered") {
            simdReduceFold("+");
        } else if (nameStrip == "simd_reduce_mul_unordered") {
            simdReduceFold("*");
        } else if (nameStrip == "simd_reduce_and") {
            simdReduceFold("&");
        } else if (nameStrip == "simd_reduce_or") {
            simdReduceFold("|");
        } else if (nameStrip == "simd_reduce_xor") {
            simdReduceFold("^");
        } else if (nameStrip == "simd_reduce_min") {
            simdReduceMinMax("<");
        } else if (nameStrip == "simd_reduce_max") {
            simdReduceMinMax(">");
        } else if (nameStrip == "simd_reduce_all") {
            simdReduceMask(true);
        } else if (nameStrip == "simd_reduce_any") {
            simdReduceMask(false);
        } else if (nameStrip == "simd_ceil") {
            simdCall("ceil");
        } else if (nameStrip == "simd_floor") {
            simdCall("floor");
        } else if (nameStrip == "simd_round") {
            simdCall("round");
        } else if (nameStrip == "simd_trunc") {
            simdCall("trunc");
        } else if (nameStrip == "simd_fabs") {
            simdCall("fabs");
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

template <typename F>
auto CodeGeneratorC::emitTermSwitchvalue(const MIRTypeResolve& localMirRes, const MIRLValue& val, const MIRSwitchValues& values, unsigned indentLevel, F f) -> void {
    CSwitchArmCb<F> cb(f);
    emitTermSwitchvalueCb(localMirRes, val, values, indentLevel, cb);
}

auto CodeGeneratorC::emitDestructorLoopCb(const MIRLValue& slot, const HIRTypeData* elementTy, CDestructorCountCallback& emitCount, unsigned indentLevel) -> void {
    auto indent = RepeatLitStr{"\t", static_cast<int>(indentLevel)};
    auto element = MIRLValue::newIndex(slot.clone(), MIRLValue::Storage::MAX_ARG);

    of << indent << "for(unsigned i = 0; i < ";
    emitCount.emit();
    of << "; i++) {\n";
    of << indent << "\ttry {\n";
    emitDestructorCall(element, elementTy, false, indentLevel + 2);
    of << "\n" << indent << "\t} catch (...) {\n";
    of << indent << "\t\tfor(i++; i < ";
    emitCount.emit();
    of << "; i++) {\n";
    of << indent << "\t\t\ttry {\n";
    emitDestructorCall(element, elementTy, false, indentLevel + 4);
    of << "\n" << indent << "\t\t\t} catch (...) { abort(); }\n";
    of << indent << "\t\t}\n";
    of << indent << "\t\tthrow;\n";
    of << indent << "\t}\n";
    of << indent << "}";
}

template <typename F>
auto CodeGeneratorC::emitDestructorLoop(const MIRLValue& slot, const HIRTypeData* elementTy, F f, unsigned indentLevel) -> void {
    CDestructorCountCb<F> cb(f);
    emitDestructorLoopCb(slot, elementTy, cb, indentLevel);
}

auto CodeGeneratorC::emitTupleDestructor(const MIRLValue& slot, const HIRTypeData::Data_Tuple& tuple, bool unsizedValid, unsigned indentLevel) -> void {
    ::std::vector<MIRLValue> fields;
    ::std::vector<const HIRTypeData*> fieldTypes;
    ::std::vector<bool> fieldUnsized;
    auto field = MIRLValue::newField(slot.clone(), 0);
    for (size_t i = 0; i < tuple.size(); i++) {
        if (resolve_.typeNeedsDropGlue(sp, tuple[i])) {
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
    of << indent << "{ unsigned trustme_drop_progress = 0;\n";
    of << indent << "\ttry {\n";
    for (size_t i = 0; i < fields.size(); i++) {
        emitDestructorCall(fields[i], fieldTypes[i], fieldUnsized[i], indentLevel + 2);
        of << indent << "\t\ttrustme_drop_progress = " << i + 1 << ";\n";
    }
    of << indent << "\t} catch (...) {\n";
    for (size_t i = 1; i < fields.size(); i++) {
        of << indent << "\t\tif(trustme_drop_progress < " << i << ") {\n";
        of << indent << "\t\t\ttry {\n";
        emitDestructorCall(fields[i], fieldTypes[i], fieldUnsized[i], indentLevel + 4);
        of << indent << "\t\t\t} catch (...) { abort(); }\n";
        of << indent << "\t\t}\n";
    }
    of << indent << "\t\tthrow;\n";
    of << indent << "\t}\n";
    of << indent << "}";
}

auto CodeGeneratorC::fieldIsUnderaligned(const MIRLValue& slot, const HIRTypeData* ty) -> bool {
    auto ref = MIRLValue::CRef(slot);
    if (!ref.is_Field()) {
        return false;
    }
    size_t align = 0;
    if (!TargetGetAlignOf(sp, resolve_, ty, align) || align <= 1) {
        return false;
    }
    // Only `#[repr(packed)]` places a field below its own alignment,
    // and it does so for everything nested inside that field too, so
    // the whole chain of owners is what decides.
    while (ref.is_Field() || ref.is_Downcast()) {
        auto inner = ref.innerRef();
        if (ref.is_Field()) {
            HIRTypeRef tmp;
            const auto* outerTy = mirRes->getLvalueType(tmp, inner);
            if (const auto* te = outerTy->opt_Path()) {
                if (const auto* str = te->binding.opt_Struct()) {
                    const unsigned packedTo = (**str).maxFieldAlignment;
                    if (packedTo != 0 && packedTo < align) {
                        return true;
                    }
                }
            }
        }
        ref = inner;
    }
    return false;
}

auto CodeGeneratorC::emitDestructorCall(const MIRLValue& slot, const HIRTypeData* ty, bool unsizedValid, unsigned indentLevel) -> void {
    // If the type doesn't need dropping, don't try.
    if (!resolve_.typeNeedsDropGlue(sp, ty)) {
        return;
    }
    auto indent = RepeatLitStr{"\t", static_cast<int>(indentLevel)};
    switch ((*ty).tag()) {
        case HIRTypeData::TAG_Diverge: {
            break;
        }
        case HIRTypeData::TAG_Infer: {
            break;
        }
        case HIRTypeData::TAG_ErasedType: {
            break;
        }
        case HIRTypeData::TAG_NodeType: {
            break;
        }
        case HIRTypeData::TAG_Generic: {
            break;
        }
        case HIRTypeData::TAG_Primitive: {
            break;
        }
        case HIRTypeData::TAG_Pointer: {
            break;
        }
        case HIRTypeData::TAG_NamedFunction: {
            break;
        }
        case HIRTypeData::TAG_Function: {
            break;
        }
        case HIRTypeData::TAG_Borrow: {
            auto& te = (*ty).as_Borrow();
            if (te.type == HIRBorrowType::Owned) {
                // Call drop glue on inner.
                emitDestructorCall(MIRLValue::newDeref(slot.clone()), te.inner, true, indentLevel);
            }
            break;
        }
        case HIRTypeData::TAG_Path: {
            // Call drop glue
            // - TODO: If the destructor is known to do nothing, don't call it.
            auto p = HIRPath(ty, "#drop_glue");
            switch (metadataType(ty)) {
                case MetadataType::Unknown:
                    MIR_BUG(*mirRes, ty << " unknown metadata");
                case MetadataType::None:
                case MetadataType::Zero:
                    if (this->typeIsBadZst(ty) && this->lvalueRootIsBadZst(slot)) {
                        size_t alignment = 0;
                        MIR_ASSERT(*mirRes, TargetGetAlignOf(sp, resolve_, ty, alignment), "Unknown ZST alignment");
                        of << indent << TransMangleValue(p) << "((";
                        emitCtype(ty);
                        of << "*)" << alignment << ");\n";
                    } else if (this->typeIsBadZst(ty) && MIRLValue::CRef(slot).is_Index()) {
                        of << indent << TransMangleValue(p) << "((";
                        emitCtype(ty);
                        of << "*)";
                        emitBorrow(*mirRes, HIRBorrowType::Unique, slot);
                        of << ");\n";
                    } else if (this->typeIsBadZst(ty) && (slot.is_Field() || slot.is_Downcast())) {
                        // Elided ZST projections can be nested through
                        // tuples, generated coroutine storage unions,
                        // and their downcasts. Back out to the first
                        // materialized ancestor before taking an
                        // address for drop glue.
                        auto v = MIRLValue::CRef(slot).innerRef();
                        HIRTypeRef tmp;
                        while (this->typeIsBadZst(mirRes->getLvalueType(tmp, v)) && (v.is_Field() || v.is_Downcast())) {
                            v = v.innerRef();
                        }
                        of << indent << TransMangleValue(p) << "((";
                        emitCtype(ty);
                        of << "*)&";
                        emitLvalue(v);
                        of << ");\n";
                    } else if (this->typeIsBadZst(ty) && slot.wrappers.empty()) {
                        of << indent << TransMangleValue(p) << "((";
                        emitCtype(ty);
                        of << "*)&rv);\n";
                    } else if (this->fieldIsUnderaligned(slot, ty)) {
                        // A field of a packed struct can sit at less
                        // alignment than its own type asks for, and
                        // `Drop::drop` takes a `&mut Self` that may
                        // not. Drop a properly aligned copy instead,
                        // which is what the field's owner does with it.
                        of << indent << "{ ";
                        emitCtype(ty, FMT_CB(ss, ss << "trustme_unaligned"));
                        of << "; memcpy(&trustme_unaligned, &";
                        emitLvalue(slot);
                        of << ", sizeof(trustme_unaligned)); " << TransMangleValue(p) << "(&trustme_unaligned); }\n";
                    } else {
                        of << indent << TransMangleValue(p) << "(&";
                        emitLvalue(slot);
                        of << ");\n";
                    }
                    break;
                case MetadataType::Slice:
                case MetadataType::TraitObject:
                    of << indent << TransMangleValue(p) << "(";
                    emitDstLvaluePointer(MIRLValue::CRef(slot));
                    of << ");\n";
                    break;
            }
            break;
        }
        case HIRTypeData::TAG_Array: {
            auto& te = (*ty).as_Array();
            // Emit destructors for all entries
            if (te.size.as_Known() > 0) {
                emitDestructorLoop(slot, te.inner, [&] {
                    of << te.size.as_Known();
                }, indentLevel);
            }
            break;
        }
        case HIRTypeData::TAG_Tuple: {
            auto& te = (*ty).as_Tuple();
            emitTupleDestructor(slot, te, unsizedValid, indentLevel);
            break;
        }
        case HIRTypeData::TAG_TraitObject: {
            MIR_ASSERT(*mirRes, unsizedValid, "Dropping TraitObject without an owned pointer");
            // Call destructor in vtable
            of << indent << "((VTABLE_HDR*)";
            emitDstLvaluePointer(MIRLValue::CRef(slot));
            of << ".META)->drop(";
            emitDstLvaluePointer(MIRLValue::CRef(slot));
            of << ".PTR";
            of << ");";
            break;
        }
        case HIRTypeData::TAG_Slice: {
            auto& te = (*ty).as_Slice();
            MIR_ASSERT(*mirRes, unsizedValid, "Dropping Slice without an owned pointer");
            // If one element destructor unwinds, Rust still drops the
            // unvisited tail.  A second exception during that cleanup
            // is a double panic and must terminate.
            emitDestructorLoop(slot, te.inner, [&] {
                emitDstLvaluePointer(MIRLValue::CRef(slot));
                of << ".META";
            }, indentLevel);
            break;
        }
        case HIRTypeData::TAG_Pattern: {
            auto& te = (*ty).as_Pattern();
            emitDestructorCall(slot, te.inner, unsizedValid, indentLevel);
            break;
        }
    }
}

auto CodeGeneratorC::enumIsTagless(const TypeRepr* repr) -> bool {
    return repr && repr->fields.empty() && repr->variants.is_None();
}

auto CodeGeneratorC::emitTaglessEnumDiscriminant(const HIRTypeData* ty) -> void {
    const auto& enm = *ty->as_Path().binding.as_Enum();
    auto v = enm.getDiscriminant(0);
    of << S128(U128(v)).truncateI64() << "ll";
}

auto CodeGeneratorC::emitEnumVariantVal(const TypeRepr* repr, unsigned idx) -> void {
    const auto& ve = repr->variants.as_Values();
    const auto& tagTy = TargetGetInnerType(sp, resolve_, *repr, ve.field.index, ve.field.subFields);
    switch (tagTy->as_Primitive()) {
        case HIRCoreType::I8:
        case HIRCoreType::I16:
        case HIRCoreType::I32:
        case HIRCoreType::I64:
        case HIRCoreType::Isize:
            of << S128(ve.values[idx]).truncateI64() << "ll";
            break;
        case HIRCoreType::Bool:
        case HIRCoreType::U8:
        case HIRCoreType::U16:
        case HIRCoreType::U32:
        case HIRCoreType::U64:
        case HIRCoreType::Usize:
        case HIRCoreType::Char:
            of << ve.values[idx].truncateU64() << "ull";
            break;
        case HIRCoreType::I128:
            if (options.emulatedI128) {
                of << "make128s_raw(" << ve.values[idx].getHi() << "ull, " << ve.values[idx].getLo() << "ull)";
            } else {
                of << "((int128_t)(((uint128_t)" << ve.values[idx].getHi() << "ull << 64) | (uint128_t)" << ve.values[idx].getLo() << "ull))";
            }
            break;
        case HIRCoreType::U128:
            if (options.emulatedI128) {
                of << "make128_raw(" << ve.values[idx].getHi() << "ull, " << ve.values[idx].getLo() << "ull)";
            } else {
                of << "(((uint128_t)" << ve.values[idx].getHi() << "ull << 64) | (uint128_t)" << ve.values[idx].getLo() << "ull)";
            }
            break;
        case HIRCoreType::F16:
        case HIRCoreType::F32:
        case HIRCoreType::F64:
        case HIRCoreType::F128:
            MIR_TODO(*mirRes, "Floating point enum tag.");
            break;
        case HIRCoreType::Str:
            MIR_BUG(*mirRes, "Unsized tag?!");
    }
}

auto CodeGeneratorC::isZeroLiteral(const HIRTypeData* ty, const EncodedLiteral& lit, const TransParams& params) -> bool {
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

auto CodeGeneratorC::emitLvalue(const MIRLValue::CRef& val) -> void {
    switch (val.tag()) {
        case MIRLValue::RefCommon::TAG_Return: {
            of << "rv";
            break;
        }
        case MIRLValue::RefCommon::TAG_Argument: {
            decltype(val.as_Argument()) e = val.as_Argument();
            if (currentFunctionRealignsArguments) {
                HIRTypeRef tmp;
                size_t size = 0;
                size_t alignment = 0;
                const auto& ty = mirRes->getLvalueType(tmp, val);
                if (TargetGetSizeAndAlignOf(sp, resolve_, ty, size, alignment) && size > 0 && alignment > maxCTypeAlignment) {
                    of << "arg" << e << "_aligned";
                    break;
                }
            }
            of << "arg" << e;
            break;
        }
        case MIRLValue::RefCommon::TAG_Local: {
            decltype(val.as_Local()) e = val.as_Local();
            if (e == MIRLValue::Storage::MAX_ARG) {
                of << "i";
            } else {
                of << "var" << e;
            }
            break;
        }
        case MIRLValue::RefCommon::TAG_Static: {
            decltype(val.as_Static()) e = val.as_Static();
            of << TransMangleValue(e);
            of << ".val";
            break;
        }
        case MIRLValue::RefCommon::TAG_Field: {
            decltype(val.as_Field()) fieldIndex = val.as_Field();
            HIRTypeRef tmp;
            auto inner = val.innerRef();
            const auto& ty = mirRes->getLvalueType(tmp, inner);
            if (ty->is_Slice()) {
                if (inner.is_Deref() || isIndirectDstLvalue(inner)) {
                    of << "((";
                    emitCtype(ty->as_Slice().inner);
                    of << "*)";
                    if (inner.is_Deref()) {
                        emitLvalue(inner.innerRef());
                    } else {
                        emitDstLvaluePointer(inner);
                    }
                    of << ".PTR)";
                } else {
                    emitLvalue(inner);
                }
                of << "[" << fieldIndex << "]";
            } else if (ty->is_Array()) {
                emitLvalue(inner);
                of << ".DATA[" << fieldIndex << "]";
            } else if (inner.is_Deref() || isIndirectDstLvalue(inner)) {
                auto dstType = metadataType(ty);
                if (dstType != MetadataType::None) {
                    of << "((";
                    emitCtype(ty);
                    of << "*)";
                    if (inner.is_Deref()) {
                        emitLvalue(inner.innerRef());
                    } else {
                        emitDstLvaluePointer(inner);
                    }
                    of << ".PTR)->_" << fieldIndex;
                } else {
                    emitLvalue(inner.innerRef());
                    of << "->_" << fieldIndex;
                }
            } else {
                emitLvalue(inner);
                of << "._" << fieldIndex;
            }
            break;
        }
        case MIRLValue::RefCommon::TAG_Deref: {
            auto inner = val.innerRef();
            HIRTypeRef tmp;
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
            break;
        }
        case MIRLValue::RefCommon::TAG_Index: {
            decltype(val.as_Index()) indexLocal = val.as_Index();
            auto inner = val.innerRef();
            HIRTypeRef tmp;
            const auto& ty = mirRes->getLvalueType(tmp, inner);
            of << "(";
            if (ty->is_Slice()) {
                if (inner.is_Deref() || isIndirectDstLvalue(inner)) {
                    of << "(";
                    emitCtype(ty->as_Slice().inner);
                    of << "*)";
                    if (inner.is_Deref()) {
                        emitLvalue(inner.innerRef());
                    } else {
                        emitDstLvaluePointer(inner);
                    }
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
            emitLvalue(MIRLValue::newLocal(indexLocal));
            of << "]";
            break;
        }
        case MIRLValue::RefCommon::TAG_Downcast: {
            decltype(val.as_Downcast()) variantIndex = val.as_Downcast();
            auto inner = val.innerRef();
            HIRTypeRef tmp;
            const auto& ty = mirRes->getLvalueType(tmp, inner);
            emitLvalue(inner);
            MIR_ASSERT(*mirRes, ty->is_Path(), "Downcast on non-Path type - " << ty);
            if (ty->as_Path().binding.is_Enum()) {
                of << ".DATA";
            }
            of << ".var_" << variantIndex;
            break;
        }
    }
}

auto CodeGeneratorC::emitLvalue(const MIRLValue& val) -> void {
    emitLvalue(MIRLValue::CRef(val));
}

auto CodeGeneratorC::emitEncodedConstant(const HIRTypeData* type, const EncodedLiteral& encoded) -> void {
    size_t size = 0;
    size_t align = 0;
    TargetGetSizeAndAlignOf(sp, resolve_, type, size, align);
    const bool pointerAligned = align * 8 >= TargetGetPointerBits();

    of << "([]() { union { ";
    emitCtype(type, FMT_CB(ss, ss << "val";));
    of << "; ";
    if (pointerAligned) {
        const auto pointerSize = TargetGetPointerBits() / 8;
        const auto words = size == 0 ? 0 : 1 + (size - 1) / pointerSize;
        of << "uintptr_t raw[" << words << "]";
    } else {
        of << "u8 raw[" << size << "]";
    }
    of << "; } value = { .raw = {";

    if (pointerAligned) {
        const auto pointerSize = TargetGetPointerBits() / 8;
        auto relocation = encoded.relocations.begin();
        for (size_t i = 0; i < encoded.bytes.size(); i += pointerSize) {
            u64 word = 0;
            for (size_t byte = 0; byte < pointerSize && i + byte < encoded.bytes.size(); byte++) {
                word |= static_cast<u64>(encoded.bytes[i + byte]) << (byte * 8);
            }
            if (i > 0) {
                of << ",";
            }
            if (relocation != encoded.relocations.end() && relocation->ofs <= i) {
                MIR_ASSERT(*mirRes, relocation->ofs == i, "Relocation not aligned to a pointer - " << relocation->ofs << " != " << i);
                MIR_ASSERT(*mirRes, relocation->len == pointerSize, "Relocation size not pointer size - " << relocation->len << " != " << pointerSize);
                word -= EncodedLiteral::PTR_BASE;
                of << "(uintptr_t)";
                if (relocation->p) {
                    if (relocation->p->data.is_UfcsInherent() && relocation->p->data.as_UfcsInherent().item == "#type_id") {
                        of << "&__typeid_" << TransMangleTypeId(relocation->p->data.as_UfcsInherent().type);
                    } else {
                        of << "&";
                        emitReifiedFunctionName(*relocation->p, relocation->preserveTrackCaller);
                    }
                } else {
                    printEscapedString(relocation->bytes);
                }
                if (word > 0) {
                    of << "+" << word;
                }
                ++relocation;
            } else {
                of << "0x" << std::hex << word << "ull" << std::dec;
            }
        }
        MIR_ASSERT(*mirRes, relocation == encoded.relocations.end(), "Relocation outside encoded constant");
    } else {
        MIR_ASSERT(*mirRes, encoded.relocations.empty(), "Non-pointer-aligned encoded constant has relocations");
        for (size_t i = 0; i < encoded.bytes.size(); i++) {
            if (i > 0) {
                of << ",";
            }
            of << static_cast<unsigned>(encoded.bytes[i]);
        }
    }
    of << "} }; return value.val; }())";
}

auto CodeGeneratorC::emitConstant(const MIRConstant& ve, const MIRLValue* dstPtr) -> void {
    switch (ve.tag()) {
        case MIRConstant::TAG_Int: {
            auto& c = ve.as_Int();
            switch (c.t) {
                // TODO: These should already have been truncated/reinterpreted, but just in case.
                case HIRCoreType::I8:
                    of << static_cast<int>(static_cast<i8>(c.v.truncateI64())); // cast to int, because `i8` is printed as a `char`
                    break;
                case HIRCoreType::I16:
                    of << static_cast<i16>(c.v.truncateI64());
                    break;
                case HIRCoreType::I32:
                    of << static_cast<i32>(c.v.truncateI64());
                    break;
                case HIRCoreType::I64:
                case HIRCoreType::Isize:
                    if (c.v.truncateI64() == INT64_MIN) {
                        of << "INT64_MIN";
                    } else if (c.v.truncateI64() == INT64_MAX) {
                        of << "INT64_MAX";
                    } else {
                        of << c.v.truncateI64();
                        of << "ll";
                    }
                    break;
                case HIRCoreType::I128:
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
            break;
        }
        case MIRConstant::TAG_Uint: {
            auto& c = ve.as_Uint();
            switch (c.t) {
                case HIRCoreType::U8:
                    of << ::std::hex << "0x" << (c.v.truncateU64() & 0xFF) << ::std::dec;
                    break;
                case HIRCoreType::U16:
                    of << ::std::hex << "0x" << (c.v.truncateU64() & 0xFFFF) << ::std::dec;
                    break;
                case HIRCoreType::U32:
                    of << ::std::hex << "0x" << (c.v.truncateU64() & 0xFFFFFFFF) << ::std::dec;
                    break;
                case HIRCoreType::U64:
                case HIRCoreType::Usize:
                    of << ::std::hex << "0x" << c.v.truncateU64() << "ull" << ::std::dec;
                    break;
                case HIRCoreType::U128:
                    if (options.emulatedI128) {
                        of << "make128_raw(" << c.v.getHi() << "ull, " << c.v.getLo() << "ull)";
                    } else if (c.v.isU64()) {
                        of << "(uint128_t)";
                        of << ::std::hex << "0x" << c.v << "ull" << ::std::dec;
                    } else {
                        of << std::hex << "( ((uint128_t)0x" << c.v.getHi() << "ull << 64) | (uint128_t)0x" << c.v.getLo() << "ull)" << std::dec;
                    }
                    break;
                case HIRCoreType::Char:
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
            break;
        }
        case MIRConstant::TAG_Float: {
            auto& c = ve.as_Float();
            this->emitFloat(c.v, c.t);
            break;
        }
        case MIRConstant::TAG_Bool: {
            auto& c = ve.as_Bool();
            of << (c.v ? "true" : "false");
            break;
        }
        case MIRConstant::TAG_Bytes: {
            auto& c = ve.as_Bytes();
            // Array borrow : Cast the C string to the array
            // - Laziness
            of << "(void*)";
            this->printEscapedString(c);
            break;
        }
        case MIRConstant::TAG_StaticString: {
            auto& c = ve.as_StaticString();
            of << "make_sliceptr(";
            this->printEscapedString(c);
            of << ", " << ::std::dec << c.size() << ")";
            break;
        }
        case MIRConstant::TAG_Encoded: {
            auto& c = ve.as_Encoded();
            emitEncodedConstant(c.type, c.value);
            break;
        }
        case MIRConstant::TAG_Const: {
            MIR_BUG(*mirRes, "Unexpected Constant::Const - " << ve);
            break;
        }
        case MIRConstant::TAG_Generic: {
            MIR_BUG(*mirRes, "Generic value present at codegen");
            break;
        }
        case MIRConstant::TAG_Function: {
            MIR_TODO(*mirRes, "Constant::Function");
            break;
        }
        case MIRConstant::TAG_ItemAddr: {
            auto& c = ve.as_ItemAddr();
            const bool hasOffset = c.offset != U128(0);
            if (hasOffset) {
                MIR_ASSERT(*mirRes, c.offset.isU64(), "Item address offset is too large: " << c.offset);
                of << "((void*)((u8*)";
            }
            if (c->data.is_UfcsInherent() && c->data.as_UfcsInherent().item == "#type_id") {
                of << "(void*)&__typeid_" << TransMangleTypeId(c->data.as_UfcsInherent().type);
            } else {
                MonomorphState msTmp(crate.types);
                auto v = resolve_.getValue(sp, *c, msTmp, /*signature_only=*/true);
                // Storage the compiler made for a promoted borrow of a
                // zero-sized value holds nothing, so it needs none: the
                // address is the alignment, which is what rustc gives
                // `&()` and an empty slice.
                if (const auto* stat = v.opt_Static(); stat && (**stat).isPromoted && !hasOffset) {
                    auto statTy = msTmp.monomorphType(sp, (**stat).type);
                    size_t size = 0;
                    size_t align = 0;
                    if (!monomorphiseTypeNeeded(statTy) && !statTy->mayHaveAssociatedType() && TargetGetSizeOf(sp, resolve_, statTy, size) && size == 0 && TargetGetAlignOf(sp, resolve_, statTy, align)) {
                        of << "((";
                        emitCtype(statTy);
                        of << "*)(uintptr_t)" << (align == 0 ? 1 : align) << ")";
                        break;
                    }
                }
                const bool isFcn = v.is_Function() || v.is_EnumConstructor() || v.is_StructConstructor();
                MIR_ASSERT(*mirRes, !isFcn || !hasOffset, "Function address has a non-zero offset: " << c.offset);
                if (!isFcn) {
                    of << "&";
                }
                emitReifiedFunctionName(*c);
                if (!isFcn) {
                    of << ".val";
                }
            }
            if (hasOffset) {
                of << " + 0x" << ::std::hex << c.offset.truncateU64() << ::std::dec << "))";
            }
            break;
        }
    }
}

auto CodeGeneratorC::emitWide128Call(const HIRTypeData* ty, const char* helper, const MIRParam& arg) -> void {
    const bool isSigned = (ty == HIRCoreType::I128);
    if (isSigned) {
        of << (options.emulatedI128 ? "uint128_to_int128(" : "(int128_t)");
    }
    of << helper << "(";
    if (isSigned) {
        of << (options.emulatedI128 ? "int128_to_uint128(" : "(uint128_t)");
    }
    emitParam(arg);
    if (isSigned && options.emulatedI128) {
        of << ")";
    }
    of << ")";
    if (isSigned && options.emulatedI128) {
        of << ")";
    }
}

auto CodeGeneratorC::emitParam(const MIRParam& p, bool typeBytes) -> void {
    switch (p.tag()) {
        case MIRParam::TAG_LValue: {
            auto& e = p.as_LValue();
            emitLvalue(e);
            break;
        }
        case MIRParam::TAG_Borrow: {
            auto& e = p.as_Borrow();
            emitBorrow(*mirRes, e.type, e.val);
            break;
        }
        case MIRParam::TAG_Constant: {
            auto& e = p.as_Constant();
            if (typeBytes && e.is_Bytes()) {
                HIRTypeRef tmp;
                of << "(";
                emitCtype(mirRes->getParamType(tmp, p));
                of << ")";
                emitConstant(e);
            } else {
                emitConstant(e);
            }
            break;
        }
    }
}

auto CodeGeneratorC::emitTraitMetadataParam(const MIRTypeResolve& localMirRes, const MIRParam& param) -> void {
    HIRTypeRef tmp;
    const auto& ty = localMirRes.getParamType(tmp, param);
    emitParam(param);
    if (const auto* te = ty->opt_Path()) {
        if (te->path.data.is_Generic() && te->path.data.as_Generic().path == resolve_.langDynMetadata()) {
            of << "._0._0";
        }
    }
}

auto CodeGeneratorC::emitCtype(const HIRTypeData* ty) -> void {
    EmptyCTypeCb callback;
    emitCtypeCb(ty, callback);
}

template <typename F>
auto CodeGeneratorC::emitCtype(const HIRTypeData* ty, F inner, bool isExternC) -> void {
    auto callback = makeCallable<CTypeCb>(inner);
    emitCtypeCb(ty, callback, isExternC);
}

auto CodeGeneratorC::emitCtypeCb(const HIRTypeData* ty, CTypeCallback& inner, bool isExternC) -> void {
    auto normalizedIt = normalizedCtypes.find(ty);
    if (normalizedIt == normalizedCtypes.end()) {
        HIRTypeRef normalized = ty;
        resolve_.expandAssociatedTypes(sp, normalized);
        normalizedIt = normalizedCtypes.emplace(ty, mv$(normalized)).first;
    }
    if (normalizedIt->second != ty) {
        emitCtypeCb(normalizedIt->second, inner, isExternC);
        return;
    }

    switch ((*ty).tag()) {
        case HIRTypeData::TAG_Infer: {
            of << "@" << ty << "@" << inner;
            break;
        }
        case HIRTypeData::TAG_Diverge: {
            of << "tBANG";
            if (!inner.empty()) {
                of << " " << inner;
            }
            break;
        }
        case HIRTypeData::TAG_Primitive: {
            auto& te = (*ty).as_Primitive();
            switch (te) {
                case HIRCoreType::Usize:
                    of << "uintptr_t";
                    break;
                case HIRCoreType::Isize:
                    of << "intptr_t";
                    break;
                case HIRCoreType::U8:
                    of << "u8";
                    break;
                case HIRCoreType::I8:
                    of << "i8";
                    break;
                case HIRCoreType::U16:
                    of << "u16";
                    break;
                case HIRCoreType::I16:
                    of << "i16";
                    break;
                case HIRCoreType::U32:
                    of << "u32";
                    break;
                case HIRCoreType::I32:
                    of << "i32";
                    break;
                case HIRCoreType::U64:
                    of << "u64";
                    break;
                case HIRCoreType::I64:
                    of << "i64";
                    break;
                case HIRCoreType::U128:
                    of << "uint128_t";
                    break;
                case HIRCoreType::I128:
                    of << "int128_t";
                    break;

                case HIRCoreType::F16:
                    of << "f16";
                    break;
                case HIRCoreType::F32:
                    of << "float";
                    break;
                case HIRCoreType::F64:
                    of << "double";
                    break;
                case HIRCoreType::F128:
                    of << "f128";
                    break;

                case HIRCoreType::Bool:
                    of << "RUST_BOOL";
                    break;
                case HIRCoreType::Char:
                    of << "RUST_CHAR";
                    break;
                case HIRCoreType::Str:
                    MIR_BUG(*mirRes, "Raw str");
            }
            if (!inner.empty()) {
                of << " " << inner;
            }
            break;
        }
        case HIRTypeData::TAG_Path: {
            auto& te = (*ty).as_Path();
            //}
            switch (te.binding.tag()) {
                case HIRTypePathBinding::TAG_Struct: {
                    of << "s_" << TransMangle(te.path);
                    break;
                }
                case HIRTypePathBinding::TAG_Union: {
                    of << "u_" << TransMangle(te.path);
                    break;
                }
                case HIRTypePathBinding::TAG_Enum: {
                    of << "e_" << TransMangle(te.path);
                    break;
                }
                case HIRTypePathBinding::TAG_ExternType: {
                    of << "x_" << TransMangle(te.path);
                    break;
                }
                case HIRTypePathBinding::TAG_Unbound: {
                    MIR_BUG(*mirRes, "Unbound type path in trans - " << ty);
                    break;
                }
                case HIRTypePathBinding::TAG_Opaque: {
                    MIR_BUG(*mirRes, "Opaque path in trans - " << ty);
                    break;
                }
            }
            if (!inner.empty()) {
                of << " " << inner;
            }
            break;
        }
        case HIRTypeData::TAG_Generic: {
            MIR_BUG(*mirRes, "Generic in trans - " << ty);
            break;
        }
        case HIRTypeData::TAG_TraitObject: {
            MIR_BUG(*mirRes, "Raw trait object - " << ty);
            break;
        }
        case HIRTypeData::TAG_ErasedType: {
            MIR_BUG(*mirRes, "ErasedType in trans - " << ty);
            break;
        }
        case HIRTypeData::TAG_Array: {
            of << "t_" << TransMangle(ty);
            if (!inner.empty()) {
                of << " " << inner;
            }
            break;
        }
        case HIRTypeData::TAG_Slice: {
            MIR_BUG(*mirRes, "Raw slice object - " << ty);
            break;
        }
        case HIRTypeData::TAG_Tuple: {
            auto& te = (*ty).as_Tuple();
            if (te.size() == 0) {
                of << "tUNIT";
            } else {
                of << "TUP_" << te.size();
                for (const auto& t : te) {
                    of << "_" << TransMangle(t);
                }
            }
            if (!inner.empty()) {
                of << " " << inner;
            }
            break;
        }
        case HIRTypeData::TAG_Borrow: {
            auto& te = (*ty).as_Borrow();
            emitCtypePtr(te.inner, inner);
            break;
        }
        case HIRTypeData::TAG_Pointer: {
            auto& te = (*ty).as_Pointer();
            emitCtypePtr(te.inner, inner);
            break;
        }
        case HIRTypeData::TAG_NamedFunction: {
            of << "t_" << TransMangle(ty);
            if (!inner.empty()) {
                of << " " << inner;
            }
            break;
        }
        case HIRTypeData::TAG_Function: {
            of << "t_" << TransMangle(ty);
            if (!inner.empty()) {
                of << " " << inner;
            }
            break;
        }
        case HIRTypeData::TAG_Pattern: {
            auto& te = (*ty).as_Pattern();
            emitCtypeCb(te.inner, inner, isExternC);
            break;
        } break;
        case HIRTypeData::TAG_NodeType:
            MIR_BUG(*mirRes, "NodeType during trans - " << ty);
            break;
    }
}

auto CodeGeneratorC::getInnerUnsizedType(const HIRTypeData* ty) -> HIRTypeRef {
    if (ty == HIRCoreType::Str || ty->is_Slice()) {
        return ty;
    } else if (ty->is_TraitObject()) {
        return ty;
    } else if (ty->is_Path()) {
        {
            auto& tuMatch = ty->as_Path().binding;
            switch (tuMatch.tag()) {
                default:
                    MIR_BUG(*mirRes, "Unbound/opaque path in trans - " << ty);
                    UNREACHABLE();
                case HIRTypePathBinding::TAG_ExternType: {
                    return ty;
                }
                case HIRTypePathBinding::TAG_Struct: {
                    auto& tpb = tuMatch.as_Struct();
                    switch (tpb->structMarkings.dstType) {
                        case HIRStructMarkings::DstType::None:
                            return HIRTypeRef();
                        case HIRStructMarkings::DstType::Slice:
                        case HIRStructMarkings::DstType::TraitObject:
                        case HIRStructMarkings::DstType::Possible:
                        case HIRStructMarkings::DstType::Projection: {
                            // TODO: How to figure out? Lazy way is to check the monomorpised type of the last field (structs only)
                            const auto& path = ty->as_Path().path.data.as_Generic();
                            const auto& str = *ty->as_Path().binding.as_Struct();
                            auto monomorph = [&](const auto& tpl) {
                                return resolve_.monomorphExpand(sp, tpl, MonomorphStatePtr(crate.types, ty, &path.params, nullptr));
                            };
                            switch (str.data.tag()) {
                                case HIRStructData::TAG_Unit: {
                                    MIR_BUG(*mirRes, "Unit-like struct with DstType::Possible");
                                    break;
                                }
                                case HIRStructData::TAG_Tuple: {
                                    auto& se = str.data.as_Tuple();
                                    return getInnerUnsizedType(monomorph(se.back().ent));
                                }
                                case HIRStructData::TAG_Named: {
                                    auto& se = str.data.as_Named();
                                    return getInnerUnsizedType(monomorph(se.back().ty));
                                }
                            }
                            UNREACHABLE();
                        }
                    }
                    break;
                }
                case HIRTypePathBinding::TAG_Union: {
                    return HIRTypeRef();
                }
                case HIRTypePathBinding::TAG_Enum: {
                    return HIRTypeRef();
                }
            }
        }
        UNREACHABLE();
    } else {
        return HIRTypeRef();
    }
}

auto CodeGeneratorC::isExternUnsizedType(const HIRTypeData* ty) const -> bool {
    return ty && ty->is_Path() && ty->as_Path().binding.is_ExternType();
}

auto CodeGeneratorC::emitExternTypeLayoutPanic(const HIRTypeData* ty) -> void {
    const auto message = FMT("attempted to compute the size or alignment of extern type `" << ty << "`");
    const auto& panicPath = crate.getLangItemPath(sp, "panic_nounwind");
    const auto panicName = TransMangleValue(panicPath);
    of << "{ extern tBANG " << panicName << "(SLICE_PTR); ";
    of << panicName << "(SLICE_PTR{(void*)\"" << FmtEscaped(message) << "\", " << message.size() << "}); abort(); }";
}

auto CodeGeneratorC::getPackingMaxAlign(const HIRTypeData* ty) const -> unsigned {
    if (ty->is_Path() && ty->as_Path().binding.is_Struct()) {
        return ty->as_Path().binding.as_Struct()->maxFieldAlignment;
    }
    return 0;
}

auto CodeGeneratorC::emitTraitObjectVtableSize(const MIRParam& value) -> void {
    of << "((VTABLE_HDR*)";
    emitParam(value);
    of << ".META)->size";
}

auto CodeGeneratorC::emitTraitObjectVtableAlign(const MIRParam& value) -> void {
    of << "((VTABLE_HDR*)";
    emitParam(value);
    of << ".META)->align";
}

auto CodeGeneratorC::emitDstTailAlign(const HIRTypeData* outerTy, const HIRTypeData* tailTy, const MIRParam& value) -> void {
    const auto maxAlign = getPackingMaxAlign(outerTy);
    if (maxAlign != 0) {
        of << "trustme_min(";
    }
    emitDstAlign(tailTy, value);
    if (maxAlign != 0) {
        of << ", " << maxAlign << ")";
    }
}

auto CodeGeneratorC::emitDstAlign(const HIRTypeData* ty, const MIRParam& value) -> void {
    if (ty->is_TraitObject()) {
        emitTraitObjectVtableAlign(value);
        return;
    }
    if (const auto* te = ty->opt_Slice()) {
        of << "ALIGNOF(";
        emitCtype(te->inner);
        of << ")";
        return;
    }
    if (ty == HIRCoreType::Str) {
        of << "1";
        return;
    }

    const auto* repr = TargetGetTypeRepr(sp, resolve_, ty);
    MIR_ASSERT(*mirRes, repr && repr->size == SIZE_MAX && !repr->fields.empty(), "Expected a DST wrapper - " << ty);
    of << "trustme_max(" << repr->align << ", ";
    emitDstTailAlign(ty, repr->fields.back().ty, value);
    of << ")";
}

auto CodeGeneratorC::emitDstSize(const HIRTypeData* ty, const MIRParam& value) -> void {
    if (ty->is_TraitObject()) {
        emitTraitObjectVtableSize(value);
        return;
    }
    if (const auto* te = ty->opt_Slice()) {
        size_t itemSize = 0, itemAlign = 0;
        MIR_ASSERT(*mirRes, TargetGetSizeAndAlignOf(sp, resolve_, te->inner, itemSize, itemAlign), "Can't get size of " << te->inner);
        emitParam(value);
        of << ".META * " << itemSize;
        return;
    }
    if (ty == HIRCoreType::Str) {
        emitParam(value);
        of << ".META";
        return;
    }

    const auto* repr = TargetGetTypeRepr(sp, resolve_, ty);
    MIR_ASSERT(*mirRes, repr && repr->size == SIZE_MAX && !repr->fields.empty(), "Expected a DST wrapper - " << ty);
    const auto& tail = repr->fields.back();
    of << "ALIGN_TO(ALIGN_TO(" << tail.offset << ", ";
    emitDstTailAlign(ty, tail.ty, value);
    of << ") + ";
    emitDstSize(tail.ty, value);
    of << ", ";
    emitDstAlign(ty, value);
    of << ")";
}

auto CodeGeneratorC::emitDstFieldOffset(const HIRTypeData* ty, size_t fieldIdx, const MIRParam& value) -> void {
    const auto* repr = TargetGetTypeRepr(sp, resolve_, ty);
    MIR_ASSERT(*mirRes, repr && fieldIdx < repr->fields.size(), "Invalid DST field " << fieldIdx << " on " << ty);
    const auto& field = repr->fields[fieldIdx];
    auto innerTy = getInnerUnsizedType(field.ty);
    MIR_ASSERT(*mirRes, fieldIdx + 1 == repr->fields.size() && innerTy->is_TraitObject(), "Expected final trait object field on " << ty);
    of << "ALIGN_TO(" << field.offset << ", ";
    emitDstTailAlign(ty, field.ty, value);
    of << ")";
}

auto CodeGeneratorC::metadataType(const HIRTypeData* ty) const -> MetadataType {
    return resolve_.metadataType(mirRes ? mirRes->sp : sp, ty);
}

template <typename F>
auto CodeGeneratorC::emitFunctionArgument(const HIRTypeData* ty, F inner) -> void {
    auto callback = makeCallable<CTypeCb>(inner);
    emitFunctionArgumentCb(ty, callback);
}

auto CodeGeneratorC::emitFunctionArgumentCb(const HIRTypeData* ty, CTypeCallback& inner) -> void {
    switch (this->metadataType(ty)) {
        case MetadataType::Unknown:
            MIR_BUG(*mirRes, ty << " has unknown function-argument metadata");
        case MetadataType::None:
        case MetadataType::Zero:
            emitCtypeCb(ty, inner);
            break;
        case MetadataType::Slice:
            of << "void* " << inner << "_ptr, uintptr_t " << inner << "_meta";
            break;
        case MetadataType::TraitObject:
            of << "void* " << inner << "_ptr, void* " << inner << "_meta";
            break;
    }
}

auto CodeGeneratorC::emitUnsizedArgumentLocal(const HIRTypeData* ty, unsigned index) -> void {
    switch (this->metadataType(ty)) {
        case MetadataType::Unknown:
            MIR_BUG(*mirRes, ty << " has unknown function-argument metadata");
        case MetadataType::None:
        case MetadataType::Zero:
            return;
        case MetadataType::Slice:
            of << "\tSLICE_PTR arg" << index << " = make_sliceptr(arg" << index << "_ptr, arg" << index << "_meta);\n";
            return;
        case MetadataType::TraitObject:
            of << "\tTRAITOBJ_PTR arg" << index << " = make_traitobjptr(arg" << index << "_ptr, arg" << index << "_meta);\n";
            return;
    }
}

auto CodeGeneratorC::isIndirectDstLvalue(const MIRLValue::CRef& value) -> bool {
    HIRTypeRef tmp;
    if (!this->isDst(mirRes->getLvalueType(tmp, value))) {
        return false;
    }
    auto base = value;
    while (base.is_Field()) {
        base.tryUnwrap();
    }
    return base.is_Deref() || base.is_Argument();
}

auto CodeGeneratorC::emitDstLvaluePointer(const MIRLValue::CRef& value) -> void {
    HIRTypeRef valueTmp;
    const auto& valueTy = mirRes->getLvalueType(valueTmp, value);
    const auto valueMeta = this->metadataType(valueTy);
    MIR_ASSERT(*mirRes, valueMeta == MetadataType::Slice || valueMeta == MetadataType::TraitObject, "Expected an indirect DST lvalue - " << value);

    auto base = value;
    while (base.is_Field()) {
        base.tryUnwrap();
    }

    MIRLValue::CRef basePointer = base;
    if (base.is_Deref()) {
        basePointer = base.innerRef();
    } else {
        HIRTypeRef baseTmp;
        const auto& baseTy = mirRes->getLvalueType(baseTmp, base);
        MIR_ASSERT(*mirRes, base.is_Argument() && this->isDst(baseTy), "DST access must be through a pointer or an unsized argument - " << value);
    }

    if (base.wrapperCount() == value.wrapperCount()) {
        emitLvalue(basePointer);
        return;
    }

    of << (valueMeta == MetadataType::Slice ? "make_sliceptr(" : "make_traitobjptr(");
    of << "(u8*)";
    emitLvalue(basePointer);
    of << ".PTR";

    const auto baseParam = MIRParam::make_LValue(basePointer.clone());
    for (size_t i = base.wrapperCount(); i < value.wrapperCount(); i++) {
        const auto& wrapper = value.lv().wrappers[i];
        MIR_ASSERT(*mirRes, wrapper.is_Field(), "Unexpected DST projection in " << value);

        HIRTypeRef parentTmp;
        const auto& parentTy = mirRes->getLvalueType(parentTmp, MIRLValue::CRef(value.lv(), i));
        const auto* repr = TargetGetTypeRepr(sp, resolve_, parentTy);
        MIR_ASSERT(*mirRes, repr && wrapper.as_Field() < repr->fields.size(), "Invalid DST field " << wrapper.as_Field() << " on " << parentTy);
        const auto& field = repr->fields[wrapper.as_Field()];

        of << " + ";
        if (this->metadataType(field.ty) == MetadataType::TraitObject) {
            emitDstFieldOffset(parentTy, wrapper.as_Field(), baseParam);
        } else {
            of << field.offset;
        }
    }

    of << ", ";
    emitLvalue(basePointer);
    of << ".META)";
}

auto CodeGeneratorC::emitDstParamPointer(const MIRParam& param) -> void {
    if (const auto* value = param.opt_LValue()) {
        emitDstLvaluePointer(MIRLValue::CRef(*value));
        return;
    }
    MIR_BUG(*mirRes, "Unsized function argument isn't an lvalue - " << param);
}

auto CodeGeneratorC::emitCtypePtr(const HIRTypeData* innerTy, CTypeCallback& inner) -> void {
    //}
    //else
    {
        switch (this->metadataType(innerTy)) {
            case MetadataType::Unknown:
                BUG(sp, innerTy << " unknown metadata type");
            case MetadataType::None:
            case MetadataType::Zero: {
                auto callback = makeCallable<CTypeCb>([&](auto& os) {
                    os << "*" << inner;
                });
                emitCtypeCb(innerTy, callback);
                break;
            }
            case MetadataType::Slice:
                of << "SLICE_PTR";
                if (!inner.empty()) {
                    of << " " << inner;
                }
                break;
            case MetadataType::TraitObject:
                of << "TRAITOBJ_PTR";
                if (!inner.empty()) {
                    of << " " << inner;
                }
                break;
        }
    }
}

auto CodeGeneratorC::isDst(const HIRTypeData* ty) const -> bool {
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

CodeGeneratorC::CallerLocationNode::CallerLocationNode(const SourceLocation& source, u32 index, CallerLocationNode* hashNext)
    : hashNext(hashNext)
    , orderNext(nullptr)
    , source(source)
    , index(index)
{
}

CodeGeneratorC::Asm2TplMatch::Asm2TplMatch(const MIRTypeResolve& localMirRes, const std::vector<AsmLine>& lines, const std::vector<MIRAsmParam>& params)
    : mirRes(localMirRes)
    , lines(lines)
    , params(params)
{
    for (const auto& v : lines) {
        fmtLines.push_back(FMT(FMT_CB(os, v.fmt(os))));
        fmtLines.back().erase(fmtLines.back().begin());
        fmtLines.back().pop_back();
    }

    for (const auto& p : params) {
        fmtParams.push_back(getParamText(p));
    }
}

auto CodeGeneratorC::Asm2TplMatch::matchesTemplate(::std::initializer_list<const char*> lines, ::std::initializer_list<const char*> params) const -> bool {
    if (!checkList(fmtLines, lines)) {
        return false;
    }

    if (!checkList(fmtParams, params)) {
        MIR_BUG(mirRes, "Hard-coded asm translation doesn't apply\n" << "[" << fmtParams << "] != \n[" << FMT_CB(os, for (auto it = params.begin(); it != params.end(); ++it) os << *it << ", ") << "]");
    }

    return true;
}

auto CodeGeneratorC::Asm2TplMatch::p(size_t i) const -> const MIRAsmParam& {
    return params.at(i);
}

auto CodeGeneratorC::Asm2TplMatch::input(size_t i) const -> const MIRParam& {
    MIR_ASSERT(mirRes, params.at(i).as_Reg().input, "Parameter " << i << " isn't a register input");
    return *params.at(i).as_Reg().input;
}

auto CodeGeneratorC::Asm2TplMatch::output(size_t i) const -> const MIRLValue& {
    MIR_ASSERT(mirRes, params.at(i).as_Reg().output, "Parameter " << i << " isn't a register output");
    return *params.at(i).as_Reg().output;
}

auto CodeGeneratorC::Asm2TplMatch::getParamText(const MIRAsmParam& p) -> std::string {
    switch (p.tag()) {
        case MIRAsmParam::TAG_Reg: {
            auto& e = p.as_Reg();
            switch (e.spec.tag()) {
                case AsmRegisterSpec::TAG_Explicit: {
                    auto& n = e.spec.as_Explicit();
                    return FMT(getDirText(e.dir) << "=" << n);
                }
                case AsmRegisterSpec::TAG_Class: {
                    auto& c = e.spec.as_Class();
                    return FMT(getDirText(e.dir) << ":" << to_string(c));
                }
            }
            break;
        }
        case MIRAsmParam::TAG_Const: {
            return "const";
        }
        case MIRAsmParam::TAG_Sym: {
            return "sym";
        }
        case MIRAsmParam::TAG_Label: {
            return "label";
        }
    }
    UNREACHABLE();
}

auto CodeGeneratorC::Asm2TplMatch::getDirText(const AsmDirection& d) -> const char* {
    switch (d) {
        case AsmDirection::In:
            return "in";
        case AsmDirection::Out:
            return "out";
        case AsmDirection::InOut:
            return "inout";
        case AsmDirection::LateOut:
            return "lateout";
        case AsmDirection::InLateOut:
            return "inlateout";
    }
    UNREACHABLE();
}

auto CodeGeneratorC::Asm2TplMatch::checkList(const std::vector<std::string>& have, const ::std::initializer_list<const char*>& exp) -> bool {
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

template <typename F>
CodeGeneratorC::CTypeCb<F>::CTypeCb(F f)
    : f(f)
{
}

template <typename F>
auto CodeGeneratorC::CTypeCb<F>::write(::std::ostream& os) const -> void {
    f(os);
}

template <typename F>
auto CodeGeneratorC::CTypeCb<F>::empty() const -> bool {
    return false;
}

auto CodeGeneratorC::EmptyCTypeCb::write(::std::ostream&) const -> void {
}

auto CodeGeneratorC::EmptyCTypeCb::empty() const -> bool {
    return true;
}
