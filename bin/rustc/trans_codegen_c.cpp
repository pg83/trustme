#include "trans_codegen_c.h"
#include "output.h"
#include "output_file.h"

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
#include <algorithm>
#include <string_view>
#include <codegen_c_prelude.h>

using namespace stl;

namespace {
    struct FmtShell {
        const std::string& s;

        FmtShell(const std::string& s);
    };

    struct FmtGccAsm {
        const std::string& s;
        bool escapePercent;

        FmtGccAsm(const std::string& s, bool escapePercent);
    };

    struct StringList {
        std::vector<std::string> cached;
        std::vector<const char*> strings;

        StringList();

        StringList(const StringList&) = delete;
        StringList(StringList&&) = default;

        const std::vector<const char*>& getVec() const;

        std::vector<const char*>::const_iterator begin() const;

        std::vector<const char*>::const_iterator end() const;

        void push_back(std::string s);

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

    using MIRParamList = std::vector<MIRParam>;

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

        std::string outfilePath;
        std::string outfilePathC;

        OutputFile of;
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
        std::set<HIRPath> trackedFunctions;
        std::set<const TypeRepr*> embeddedTags;
        HIRTypeRefMap<HIRTypeRef> normalizedCtypes;

        struct PromotedNode {
            PromotedNode* next;
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

        CodeGeneratorC(const WireBoard& wb, const HIRCrate& crate, const std::string& outfile);

        ~CodeGeneratorC();

        void finalise(const TransOptions& opt, CodegenOutput outTy, const std::string& hirFile) override;

        void emitBoxDrop(unsigned indentLevel, const HIRTypeData* innerType, const HIRTypeData* boxType, const MIRLValue& slot, bool runDestructor);

        std::string asmSymbol(const Span& span, const HIRPath& path) const;

        std::string inlineAsmConstant(const MIRConstant& operand) const;

        std::string globalAsmConstant(const HIRGlobalAssembly& assembly, const HIRGlobalAsmOperand::Data_Const& operand) const;

        void emitGlobalAsm(const HIRGlobalAssembly& se) override;

        void emitTypeId(const HIRTypeData* ty) override;

        static const char* compilerAbiAttribute(const RcString& abi);

        void emitTypeProto(const HIRTypeData* ty) override;

        void emitTypeFn(const HIRTypeData* ty);

        void emitStructInner(const HIRTypeData* ty, const TypeRepr* repr, unsigned packingMaxAlign);

        void emitType(const HIRTypeData* ty) override;

        void emitStruct(const Span& sp, const HIRGenericPath& p, const HIRStruct& item) override;

        void emitUnion(const Span& sp, const HIRGenericPath& p, const HIRUnion& item) override;

        bool isEnumTag(const TypeRepr* repr, size_t idx);

        const HIRTypeData* emitEnumPath(const TypeRepr* repr, const TypeRepr::FieldPath& path);

        void emitEnum(const Span& sp, const HIRGenericPath& p, const HIREnum& item) override;

        void emitConstructorEnum(const Span& sp, const HIRGenericPath& path, const HIREnum& item, size_t varIdx) override;

        void emitConstructorStruct(const Span& sp, const HIRGenericPath& p, const HIRStruct& item) override;

        void emitExternTypeDefinition(const HIRTypeData* type);

        bool emitStaticTy(const HIRTypeData* type, const HIRPath& p, bool isProto, size_t explicitAlignment);

        void emitStaticExt(const HIRPath& p, const HIRStatic& item, const TransParams& params) override;

        void emitStaticProto(const HIRPath& p, const HIRStatic& item, const TransParams& params) override;

        static u64 promotedHash(const RcString& ctype, const EncodedLiteral& value);

        const HIRPath* promotedHolder(const HIRTypeData* ty, const EncodedLiteral& value) const;

        const HIRPath* takePromotedHolder(const HIRPath& p, const HIRTypeData* ty, const EncodedLiteral& value);

        static bool promotedIsShared(const HIRStatic& item);

        static bool promotedTypeIsSettled(const HIRTypeData* ty);

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

        void emitCMainShim(const HIRPath& p, const HIRFunction& item, const TransParams& params, const HIRTypeData* retType);

        void emitOperationWithUnwindCb(const MIRUnwindAction& action, unsigned indentLevel, CUnwindOperationCallback& emitOperation);

        template <typename F>
        void emitOperationWithUnwind(const MIRUnwindAction& action, unsigned indentLevel, F f);

        void emitBlockTerminator(MIRTypeResolve& localMirRes, const MIRTerminator& term, unsigned blockIndex, bool cleanup, unsigned indentLevel);

        void emitCleanupRunner(MIRTypeResolve& localMirRes, const std::set<unsigned>& cleanupBlocks);

        bool typeIsEmulatedI128(const HIRTypeData* ty) const;

        bool typeIsCScalar(const HIRTypeData* ty) const;

        bool typeIsBadZst(const HIRTypeData* ty) const;

        bool lvalueIsBadZst(const MIRLValue& lv) const;

        bool lvalueRootIsBadZst(const MIRLValue& lv) const;

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

        RcString calleeAbi(const MIRTypeResolve& localMirRes, const MIRCallTarget& fcn);

        RcString mangleResolvedValuePath(const HIRPath& path) const;

        void emitTermCall(const MIRTypeResolve& localMirRes, const MIRTerminator::Data_Call& e, unsigned indentLevel, bool tailCall = false);

        void emitTermTailCall(const MIRTypeResolve& localMirRes, const MIRTerminator::Data_TailCall& e, unsigned indentLevel);

        bool asmMatchesTemplate(const MIRStatement::Data_Asm& e, const char* tpl, std::initializer_list<const char*> inputs, std::initializer_list<const char*> outputs);

        void emitAsmGcc(const MIRTypeResolve& localMirRes, const MIRStatement::Data_Asm& e, unsigned indentLevel);

        struct Asm2TplMatch {
            const MIRTypeResolve& mirRes;
            const std::vector<AsmLine>& lines;
            const std::vector<MIRAsmParam>& params;
            std::vector<std::string> fmtLines;
            std::vector<std::string> fmtParams;

            Asm2TplMatch(const MIRTypeResolve& localMirRes, const std::vector<AsmLine>& lines, const std::vector<MIRAsmParam>& params);

            bool matchesTemplate(std::initializer_list<const char*> lines, std::initializer_list<const char*> params) const;

            const MIRAsmParam& p(size_t i) const;

            const MIRParam& input(size_t i) const;

            const MIRLValue& output(size_t i) const;

            static std::string getParamText(const MIRAsmParam& p);

            static const char* getDirText(const AsmDirection& d);

            static bool checkList(const std::vector<std::string>& have, const std::initializer_list<const char*>& exp);
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

        const HIRPath& promotedName(const HIRPath& path);

        void emitReifiedFunctionName(const HIRPath& path, bool preserveTrackCaller = false);

        const HIRTypeData* monomorphiseFcnReturn(HIRTypeRef& tmp, const HIRFunction& item, const TransParams& params);

        bool argumentIsPassed(const RcString& abi, const HIRTypeData* ty);

        void emitFunctionHeader(const HIRPath& p, const HIRFunction& item, const TransParams& params, bool includeCallerLocation = true, const char* nameSuffix = "");

        void emitTrackCallerReifyWrapper(const HIRPath& p, const HIRFunction& item, const TransParams& params);

        static const char* tagUnsignedType(size_t size);

        static uint64_t tagBits(size_t size, size_t value);

        void emitIntrinsicCall(const RcString& name, const HIRPathParams& params, const MIRTerminator::Data_Call& e);

        template <typename F>
        void emitTermSwitchvalue(const MIRTypeResolve& localMirRes, const MIRLValue& val, const MIRSwitchValues& values, unsigned indentLevel, F f);

        void emitDestructorLoopCb(const MIRLValue& slot, const HIRTypeData* elementTy, CDestructorCountCallback& emitCount, unsigned indentLevel);

        template <typename F>
        void emitDestructorLoop(const MIRLValue& slot, const HIRTypeData* elementTy, F f, unsigned indentLevel);

        void emitTupleDestructor(const MIRLValue& slot, const HIRTypeData::Data_Tuple& tuple, bool unsizedValid, unsigned indentLevel);

        bool fieldIsUnderaligned(const MIRLValue& slot, const HIRTypeData* ty);

        void emitDestructorCall(const MIRLValue& slot, const HIRTypeData* ty, bool unsizedValid, unsigned indentLevel);

        static bool enumIsTagless(const TypeRepr* repr);

        void emitTaglessEnumDiscriminant(const HIRTypeData* ty);

        void emitEnumVariantVal(const TypeRepr* repr, unsigned idx);

        bool isZeroLiteral(const HIRTypeData* ty, const EncodedLiteral& lit, const TransParams& params);

        void emitLvalue(const MIRLValue::CRef& val);

        void emitLvalue(const MIRLValue& val);

        void emitEncodedConstant(const HIRTypeData* type, const EncodedLiteral& encoded);

        void emitConstant(const MIRConstant& ve, const MIRLValue* dstPtr = nullptr);

        void emitWide128Call(const HIRTypeData* ty, const char* helper, const MIRParam& arg);

        void emitParam(const MIRParam& p, bool typeBytes = true);

        void emitTraitMetadataParam(const MIRTypeResolve& localMirRes, const MIRParam& param);

        struct CTypeCallback {
            virtual void write(ZeroCopyOutput& os) const = 0;
            virtual bool empty() const = 0;

            friend ZeroCopyOutput& operator<<(ZeroCopyOutput& os, const CTypeCallback& callback) {
                callback.write(os);
                return os;
            }
        };

        template <typename F>
        struct CTypeCb final: CTypeCallback {
            F f;

            explicit CTypeCb(F f);

            void write(ZeroCopyOutput& os) const override;

            bool empty() const override;
        };

        struct EmptyCTypeCb final: CTypeCallback {
            void write(ZeroCopyOutput&) const override;

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

        void emitDstAlign(const HIRTypeData* ty, const MIRParam& value);

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





    enum class AtomicOp {
        Add,
        Sub,
        And,
        Or,
        Xor
    };
}

std::unique_ptr<CodeGenerator> TransCodegenGetGeneratorC(const WireBoard& wb, const HIRCrate& crate, const std::string& outfile) { // escape: existing factory interface exposed by declaration reordering
    return std::unique_ptr<CodeGenerator>(new CodeGeneratorC(wb, crate, outfile));
}

FmtShell::FmtShell(const std::string& s) // escape: existing formatting interface exposed by declaration reordering
    : s(s) {
}

FmtGccAsm::FmtGccAsm(const std::string& s, bool escapePercent) // escape: existing formatting interface exposed by declaration reordering
    : s(s)
    , escapePercent(escapePercent) {
}

StringList::StringList() {
}

auto StringList::getVec() const -> const std::vector<const char*>& { // escape: existing container interface exposed by declaration reordering
    return strings;
}

auto StringList::begin() const -> std::vector<const char*>::const_iterator { // escape: existing container interface exposed by declaration reordering
    return strings.begin();
}

auto StringList::end() const -> std::vector<const char*>::const_iterator { // escape: existing container interface exposed by declaration reordering
    return strings.end();
}

auto StringList::push_back(std::string s) -> void { // escape: existing container interface exposed by declaration reordering
    if (cached.capacity() == cached.size()) {
        std::vector<bool> b; // escape: existing temporary exposed by declaration reordering
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

        cached.push_back(std::move(s));
        j = 0;
        for (size_t i = 0; i < b.size(); i++) {
            if (b[i]) {
                strings[i] = cached.at(j++).c_str();
            }
        }
    } else {
        cached.push_back(std::move(s));
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
        ASSERT_BUG(Span(), literalBlob, StringView("Failed to open `") << path << StringView("` for writing"));
    }
    const size_t offset = literalBlobSize;
    const size_t written = fwrite(encoded.bytes.data(), 1, encoded.bytes.size(), literalBlob);
    ASSERT_BUG(Span(), written == encoded.bytes.size(), StringView("Failed to write literal blob for `") << outfilePath << StringView("`"));
    literalBlobSize += written;
    return offset;
}

auto CodeGeneratorC::closeLiteralBlob() -> void {
    if (literalBlob) {
        ASSERT_BUG(Span(), fclose(literalBlob) == 0, StringView("Failed to close literal blob for `") << outfilePath << StringView("`"));
        literalBlob = nullptr;
    }
}

CodeGeneratorC::CodeGeneratorC(const WireBoard& wb, const HIRCrate& crate, const std::string& outfile) // escape: existing constructor interface exposed by declaration reordering
    : wb_(wb)
    , crate(crate)
    , resolve_(wb, OpaqueReveal::All)
    , outfilePath(outfile)
    , outfilePathC(outfile + ".cpp")
    , of(outfilePathC)
    , promotedValues(crate.pool) {
    options.emulatedI128 = TargetGetCurSpec(wb_).backendC.emulatedI128;
    if (TargetGetPointerBits() < 64 && !options.emulatedI128) {
        WARNING(Span(), W0000, StringView("Potentially misconfigured target, 32-bit targets require i128 emulation"));
    }
    options.disallowEmptyStructs = true;

    const auto& targetSpec = TargetGetCurSpec(wb_);
    of << StringView("#define TRUSTME_CODEGEN_DISALLOW_EMPTY_STRUCTS ") << options.disallowEmptyStructs << StringView("\n")
       << StringView("#define TRUSTME_TARGET_EMULATED_I128 ") << options.emulatedI128 << StringView("\n")
       << StringView("#define TRUSTME_TARGET_U128_ALIGN ") << static_cast<unsigned>(targetSpec.arch.alignments.u128) << StringView("\n")
       << StringView("#define TRUSTME_TARGET_HAS_NATIVE_F128 ") << usesIntelCompilerAsmDialect() << StringView("\n")
       << StringView(CODEGEN_C_PRELUDE);
    of << StringView("}\nnamespace {\n")
       << StringView("extern const trustme_caller_location trustme_caller_locations[];\n")
       << StringView("}\nextern \"C\" {\n");
}

CodeGeneratorC::~CodeGeneratorC() {
}

auto CodeGeneratorC::finalise(const TransOptions& opt, CodegenOutput outTy, const std::string& hirFile) -> void {
    const bool createShims = (outTy == CodegenOutput::Executable);

    // TODO: Support dynamic libraries too

    if (outTy == CodegenOutput::Executable && !crate.noMain) {
        // TODO: Define this function in MIR?
        of << StringView("}\n\n");
        of << StringView("int main(int argc, const char* argv[]) {\n");
        auto cStartPath = resolve_.hirCrate().getLangItemPathOpt("trustme-start");
        if (cStartPath == HIRSimplePath()) {
            auto mainPath = crate.getLangItemPath(Span(), "trustme-main");
            const auto& mainFcn = crate.getFunctionByPath(sp, mainPath);

            const auto& startPath = resolve_.hirCrate().getLangItemPathOpt("start");
            if (crate.isNoCore && startPath == HIRSimplePath()) {
                of << StringView("\t") << TransMangleValue(HIRGenericPath(mainPath)) << StringView("();\n");
                of << StringView("\treturn 0;\n");
            } else {
                auto startGpath = HIRGenericPath(resolve_.hirCrate().getLangItemPath(Span(), "start"));
                startGpath.params.types.push_back(mainFcn.returnType);
                of << StringView("\treturn ") << TransMangleValue(startGpath) << StringView("(") << TransMangleValue(HIRGenericPath(mainPath)) << StringView(", argc, (u8**)argv");
                of << StringView(", 0");
                of << StringView(");\n");
            }
        } else {
            of << StringView("\treturn ") << TransMangleValue(HIRGenericPath(cStartPath)) << StringView("(argc, (u8**)argv);\n");
        }
        of << StringView("}\n\n");
        of << StringView("extern \"C\" {\n");
    }

    if (createShims) {
        {
            const auto allocatorIt = crate.langItems.find(GLOBAL_ALLOCATOR_LANG_ITEM);
            const bool hasGlobalAllocator = allocatorIt != crate.langItems.end();
            const HIRStatic* globalAllocator = hasGlobalAllocator ? &crate.getStaticByPath(Span(), allocatorIt->second) : nullptr;
            for (size_t i = 0; i < NUM_ALLOCATOR_METHODS; i++) {
                struct H {
                    static void tyArgs(std::vector<const char*>& out, AllocatorDataTy t) {
                        switch (t) {
                            case AllocatorDataTy::Unit:
                            case AllocatorDataTy::ResultPtr:
                                UNREACHABLE();
                            case AllocatorDataTy::Layout:
                                out.push_back("uintptr_t");
                                out.push_back("uintptr_t");
                                break;
                            case AllocatorDataTy::Ptr:
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
                            case AllocatorDataTy::ResultPtr:
                                return "i8*";
                            case AllocatorDataTy::Layout:
                            case AllocatorDataTy::Ptr:
                            case AllocatorDataTy::Usize:
                                UNREACHABLE();
                        }
                        UNREACHABLE();
                    }

                    static void emitProto(ZeroCopyOutput& os, const AllocatorMethod& method, const char* namePrefix, const std::vector<const char*>& args) {
                        os << H::tyRet(method.ret) << StringView(" ") << namePrefix << method.name << StringView("(");
                        for (size_t j = 0; j < args.size(); j++) {
                            if (j != 0) {
                                os << StringView(", ");
                            }
                            os << args[j] << StringView(" a") << j;
                        }
                        os << StringView(")");
                    }
                };

                const auto& method = ALLOCATOR_METHODS[i];
                std::vector<const char*> args;
                for (size_t j = 0; j < method.nArgs; j++) {
                    H::tyArgs(args, method.args[j]);
                }
                H::emitProto(of, method, "__rust_", args);
                of << StringView(" {\n");
                if (!hasGlobalAllocator) {
                    const char* allocPrefix = "__rdl_";
                    of << StringView("\textern ");
                    H::emitProto(of, method, allocPrefix, args);
                    of << StringView(";\n");
                    of << StringView("\t");
                    if (method.ret != AllocatorDataTy::Unit) {
                        of << StringView("return ");
                    }
                    of << allocPrefix << method.name << StringView("(");
                    for (size_t j = 0; j < args.size(); j++) {
                        if (j != 0) {
                            of << StringView(", ");
                        }
                        of << StringView("a") << j;
                    }
                    of << StringView(");\n");
                } else {
                    size_t flatArg = 0;
                    size_t layoutArg = 0;
                    for (size_t j = 0; j < method.nArgs; j++) {
                        switch (method.args[j]) {
                            case AllocatorDataTy::Layout:
                                of << StringView("\tauto layout") << layoutArg << StringView(" = ");
                                emitReifiedFunctionName(TransAllocatorLayoutCtorPath(crate));
                                of << StringView("(a") << flatArg << StringView(", a") << flatArg + 1 << StringView(");\n");
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
                    of << StringView("\t");
                    if (method.ret != AllocatorDataTy::Unit) {
                        of << StringView("return (i8*)");
                    }
                    of << TransMangleValue(methodPath) << StringView("(&") << TransMangleValue(staticPath) << StringView(".val");
                    flatArg = 0;
                    layoutArg = 0;
                    for (size_t j = 0; j < method.nArgs; j++) {
                        of << StringView(", ");
                        switch (method.args[j]) {
                            case AllocatorDataTy::Layout:
                                of << StringView("layout") << layoutArg;
                                flatArg += 2;
                                layoutArg += 1;
                                break;
                            case AllocatorDataTy::Ptr:
                                of << StringView("(u8*)a") << flatArg;
                                flatArg += 1;
                                break;
                            case AllocatorDataTy::Usize:
                                of << StringView("a") << flatArg;
                                flatArg += 1;
                                break;
                            case AllocatorDataTy::Unit:
                            case AllocatorDataTy::ResultPtr:
                                UNREACHABLE();
                        }
                    }
                    of << StringView(")");
                    of << StringView(";\n");
                }
                of << StringView("}\n");
            }

            of << StringView("void __rust_no_alloc_shim_is_unstable_v2() {}\n");

            {
                auto oomMethod = crate.getLangItemPathOpt("trustme-alloc_error_handler");
                of << StringView("u8 __rust_alloc_error_handler_should_panic = 0;\n");
                of << StringView("u8 __rust_no_alloc_shim_is_unstable = 0;\n");

                auto layoutPath = HIRSimplePath("core", {"alloc", "Layout"});
                if (oomMethod != HIRSimplePath()) {
                    of << StringView("struct s_") << TransMangle(layoutPath) << StringView("_A { uintptr_t a, b; };\n");
                    of << StringView("void oom_impl(s_") << TransMangle(layoutPath) << StringView("_A l) {")
                       << StringView(" extern void ") << TransMangleValue(oomMethod) << StringView("(s_") << TransMangle(layoutPath) << StringView("_A l);")
                       << StringView(" ") << TransMangleValue(oomMethod) << StringView("(l);")
                       << StringView(" }\n");
                }

                of << StringView("u8 __rust_alloc_error_handler_should_panic_v2() { return 0; }");
                of << StringView("void __rust_alloc_error_handler(uintptr_t s, uintptr_t a) {\n");
                if (oomMethod == HIRSimplePath()) {
                    of << StringView("\tvoid __rdl_oom(uintptr_t, uintptr_t);\n");
                    of << StringView("\t__rdl_oom(s,a);\n");
                } else {
                    of << StringView("\ts_") << TransMangle(layoutPath) << StringView("_A v = { s, a };\n");
                    of << StringView("\toom_impl(v);\n");
                }
                of << StringView("}\n");
            }
        }

        {
            const auto& panicImplPath = crate.getLangItemPathOpt("trustme-panic_implementation");
            if (panicImplPath != HIRSimplePath()) {
                of << StringView("u32 panic_impl(uintptr_t payload) {");
                of << StringView("extern u32 ") << TransMangleValue(panicImplPath) << StringView("(uintptr_t payload);");
                of << StringView("return ") << TransMangleValue(panicImplPath) << StringView("(payload);");
                of << StringView("}\n");
            } else if (!crate.isNoCore) {
                crate.getLangItemPath(Span(), "trustme-panic_implementation");
            }
        }
    }

    of << StringView("}\n");
    emitCallerLocationDefinitions();
    of.close();
    closeLiteralBlob();

    if (opt.emitCppOnly) {
        return;
    }

    struct LinkList: private StringList {
        enum class Ty {
            Directory,
            Explicit,
            Implicit,
        };

        std::vector<Ty> ty_;

        void pushDir(const char* s) {
            auto it = std::find_if(StringList::begin(), StringList::end(), [&](const char* es) {
                return std::strcmp(es, s) == 0;
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
                bool isInDylib = false;
                for (const auto& crate2 : crate.extCrates) {
                    if (isDylib(crate2.second)) {
                        for (const auto& subcrate : crate2.second.data->extCrates) {
                            if (subcrate.second.path == extCrate.path) {
                                DEBUG(crateName << StringView(" referenced by dylib ") << crate2.first);
                                isInDylib = true;
                            }
                        }
                    }
                    if (isInDylib) {
                        break;
                    }
                }
                if (isInDylib && !isDylib(extCrate)) {
                    continue;
                }

                if (extCrate.data->langItems.count("trustme-panic_runtime")) {
                    if (strncmp(crateName.c_str(), opt.panicCrate.c_str(), opt.panicCrate.size()) != 0) {
                        DEBUG(StringView("Ignore not-selected panic crate: ") << crateName);
                        continue;
                    } else {
                        DEBUG(StringView("Keep panic crate: ") << crateName);
                    }
                }

                if (extCrate.isProcMacro) {
                } else if (extCrate.objectPath != "") {
                    extCrates.push_back(extCrate.objectPath.c_str());
                } else if (extCrate.path.size() >= 5 && extCrate.path.compare(extCrate.path.size() - 5, 5, ".rlib") == 0) {
                    extCrates.push_back(extCrate.path + ".o");
                } else if (isDylib(extCrate)) {
                    extCratesDylib.push_back(extCrate.path.c_str());
                } else {
                }
            }

            struct H {
                static bool fileExists(const std::string& path) {
                    return std::ifstream(path).is_open();
                }

                static std::string findLibraryOne(const std::string& path, const std::string& name) {
                    std::string libPath;
                    libPath = FMT(path << StringView("/lib") << name << StringView(".so"));
                    if (fileExists(libPath)) {
                        return libPath;
                    }
                    libPath = FMT(path << StringView("/lib") << name << StringView(".a"));
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
                ASSERT_BUG(Span(), lib.name != "", StringView(""));
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
                for (const auto& lib : extCrate.data->extLibs) {
                    ASSERT_BUG(Span(), lib.name != "", StringView("Empty lib from ") << crateName);
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

    StringList args;
    size_t argFileStart = 0;
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
    args.push_back("-fwrapv");
    if (usesIntelCompilerAsmDialect()) {
        args.push_back("-masm=intel");
    }
    for (const auto& a : TargetGetCurSpec(wb_).backendC.compilerOpts) {
        args.push_back(a.c_str());
    }
    switch (opt.optLevel) {
        case OptimizationLevel::None:
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

    StringBuilder cmdSs;
    std::string commandFile = outfilePath + "_cmd.txt";
    std::unique_ptr<OutputFile> commandFileStream;
    if (getenv("TRUSTME_CCACHE")) {
        cmdSs << StringView("ccache ");
    }
    bool useArgFile = argFileStart > 0;
    if (useArgFile) {
        commandFileStream = std::make_unique<OutputFile>(commandFile);
    }
    size_t i = -1;
    for (const auto& arg : args.getVec()) {
        i++;
        ZeroCopyOutput& outSs = useArgFile && i >= argFileStart ? static_cast<ZeroCopyOutput&>(*commandFileStream) : cmdSs;
        outSs << StringView("\"") << FmtShell(arg) << StringView("\" ");
    }
    if (useArgFile) {
        cmdSs << StringView("@\"") << FmtShell(commandFile) << StringView("\"");
        commandFileStream->close();
    }
    const std::string commandText(static_cast<const char*>(cmdSs.data()), cmdSs.length());
    sysO << StringView("Running command - ") << commandText << endL;
    if (opt.buildCommandFile != "") {
        sysE << StringView("INVOKE CC: ") << commandText << endL;
        OutputFile buildCommand(opt.buildCommandFile);
        buildCommand << commandText << endL;
    } else {
        int ec = system(commandText.c_str());
        if (ec == -1) {
            sysE << StringView("C Compiler failed to execute (system returned -1)") << endL;
            perror("system");
            exit(1);
        } else if (ec != 0) {
            sysE << StringView("C Compiler failed to execute - error code ") << ec << endL;
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
    of << indent << TransMangleValue(p) << StringView("(&");
    emitLvalue(slot);
    of << StringView(");\n");

    const auto* repr = TargetGetTypeRepr(sp, resolve_, boxType);
    MIR_ASSERT(*mirRes, repr, StringView("No repr for Box ") << boxType);
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
        BUG(span, StringView("asm sym operand does not name a function or static: ") << path);
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
    BUG(Span(), StringView("asm const operand is not an integer: ") << operand);
}

auto CodeGeneratorC::globalAsmConstant(const HIRGlobalAssembly& assembly, const HIRGlobalAsmOperand::Data_Const& operand) const -> std::string {
    ASSERT_BUG(assembly.span, operand.value.is_Evaluated(), StringView("Unevaluated global_asm const operand"));
    ASSERT_BUG(assembly.span, operand.type->is_Primitive() && isInteger(operand.type->as_Primitive()), StringView("Non-integer global_asm const operand: ") << operand.type);
    const auto& value = **operand.value.opt_Evaluated();
    ASSERT_BUG(assembly.span, value.relocations.empty(), StringView("Relocated global_asm const operand"));

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
    of << StringView("__asm__ (\"");
    if (usesIntelCompilerAsmDialect() && se.options.attSyntax) {
        of << StringView(".att_syntax prefix; ");
    }
    for (const auto& l : se.lines) {
        for (const auto& f : l.frags) {
            of << FmtGccAsm(f.before, false);
            ASSERT_BUG(se.span, f.index < se.operands.size(), StringView("Invalid argument reference in global assembly"));
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
        of << StringView(";\\n ");
    }
    if (usesIntelCompilerAsmDialect() && se.options.attSyntax) {
        of << StringView(".intel_syntax noprefix; ");
    }
    of << StringView("\");\n");
}

auto CodeGeneratorC::emitTypeId(const HIRTypeData* ty) -> void {
    of << StringView("tTYPEID __typeid_") << TransMangleTypeId(ty) << StringView(" __attribute__((weak));\n");
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
            break;
        case HIRTypeData::TAG_Tuple: {
            auto& te = (*ty).as_Tuple();
            if (te.size() > 0) {
                of << StringView("struct ");
                emitCtype(ty);
                of << StringView(";\n");
            }
            break;
        }
        case HIRTypeData::TAG_Function: {
            emitTypeFn(ty);
            of << StringView("\n");
            break;
        }
        case HIRTypeData::TAG_NamedFunction: {
            of << StringView("struct ");
            emitCtype(ty);
            of << StringView(";\n");
            break;
        }
        case HIRTypeData::TAG_Array: {
            of << StringView("struct ");
            emitCtype(ty);
            of << StringView(";\n");
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
                    of << StringView("struct s_") << TransMangle(te.path) << StringView(";\n");
                    break;
                }
                case HIRTypePathBinding::TAG_ExternType: {
                    of << StringView("struct x_") << TransMangle(te.path) << StringView(";\n");
                    break;
                }
                case HIRTypePathBinding::TAG_Union: {
                    of << StringView("union u_") << TransMangle(te.path) << StringView(";\n");
                    break;
                }
                case HIRTypePathBinding::TAG_Enum: {
                    of << StringView("struct e_") << TransMangle(te.path) << StringView(";\n");
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
    of << StringView("typedef ");
    if (te.rettype == crate.types.unit()) {
        of << StringView("void");
    } else {
        // TODO: Better emit_ctype call for return type?
        emitCtype(te.rettype);
    }
    of << StringView(" (") << compilerAbiAttribute(te.abi);
    of << StringView("*");
    emitCtype(ty);
    of << StringView(")(");
    if (te.argTypes.empty() && !te.trackCaller) {
        of << StringView("void)");
    } else {
        for (unsigned int i = 0; i < te.argTypes.size(); i++) {
            if (i != 0) {
                of << StringView(",");
            }
            of << StringView(" ");
            this->emitFunctionArgument(te.argTypes[i], FMT_CB(ss, ss << StringView("arg") << i;));
        }
        if (te.isVariadic) {
            of << StringView(", ...");
        }
        if (te.trackCaller) {
            MIR_ASSERT(*mirRes, !te.isVariadic, StringView("#[track_caller] on a variadic function pointer"));
            if (!te.argTypes.empty()) {
                of << StringView(",");
            }
            of << StringView(" const trustme_caller_location* trustme_caller");
        }
        of << StringView(" )");
    }
    of << StringView(";");
}

auto CodeGeneratorC::emitStructInner(const HIRTypeData* ty, const TypeRepr* repr, unsigned packingMaxAlign) -> void {
    std::vector<unsigned> fields;
    fields.reserve(repr->fields.size());
    std::vector<bool> zsts;
    zsts.reserve(repr->fields.size());
    size_t maxAlign = 0;
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
    if (packingMaxAlign == 0 && !hasManualAlign && repr->align == 1 && repr->size > 1) {
        packingMaxAlign = 1;
    }
    std::sort(fields.begin(), fields.end(), [&](auto a, auto b) {
        if (repr->fields[a].offset == repr->fields[b].offset) {
            return !zsts[a] < !zsts[b];
        }
        return repr->fields[a].offset < repr->fields[b].offset;
    });

    if (packingMaxAlign) {
        of << StringView("#pragma pack(push, ") << packingMaxAlign << StringView(")\n");
    }
    of << StringView("struct ");
    emitCtype(ty);
    of << StringView(" {\n");

    bool hasUnsized = false;
    size_t sizedFields = 0;
    size_t curOfs = 0;
    bool isFirstField = true;
    for (unsigned fld : fields) {
        const auto& ty = repr->fields[fld].ty;
        const auto offset = repr->fields[fld].offset;
        size_t s = 0, a;
        TargetGetSizeAndAlignOf(sp, resolve_, ty, s, a);

        DEBUG(StringView("@") << offset << StringView(": ") << ty << StringView(" ") << s << StringView(",") << a);
        if (s == SIZE_MAX) {
        } else if (s == 0) {
        } else {
            MIR_ASSERT(*mirRes, curOfs <= offset, StringView("Current offset is already past expected (#") << fld << StringView("): ") << curOfs << StringView(" > ") << offset);
            auto fieldAlign = a;
            if (TargetGetCurSpec(wb_).arch.name == "powerpc") {
                if (s > 0) {
                    if (!isFirstField && fieldAlign >= 4 && fieldAlign <= 8) {
                        fieldAlign = 4;
                    }
                    isFirstField = false;
                }
            }
            a = packingMaxAlign > 0 ? std::min<size_t>(packingMaxAlign, fieldAlign) : fieldAlign;
            DEBUG(StringView("a = ") << a);
            while (curOfs % a != 0) {
                curOfs++;
            }
        }

        if (curOfs < offset) {
            auto n = offset - curOfs;
            of << StringView("\tu8 _padding") << fld << StringView("[") << n << StringView("];\n");
            curOfs += n;
        }
        MIR_ASSERT(*mirRes, curOfs == offset, StringView("Current offset doesn't match expected (#") << fld << StringView("): ") << curOfs << StringView(" != ") << offset);

        if ((*ty).is_Path() && (*ty).as_Path().binding.is_ExternType()) {
            hasUnsized = true;
        } else if (!(s == 0 && options.disallowEmptyStructs)) {
            of << StringView("\t");
            if (const auto* te = ty->opt_Slice()) {
                emitCtype(te->inner, FMT_CB(ss, ss << StringView("_") << fld << StringView("[0]");));
                hasUnsized = true;
            } else if (ty->is_TraitObject()) {
                of << StringView("unsigned char _") << fld << StringView("[0]");
                hasUnsized = true;
            } else if (ty == HIRCoreType::Str) {
                of << StringView("u8 _") << fld << StringView("[0]");
                hasUnsized = true;
            } else {
                // TODO: Nested unsized?
                emitCtype(ty, FMT_CB(ss, ss << StringView("_") << fld));
                sizedFields++;

                hasUnsized |= (s == SIZE_MAX);
            }
            of << StringView(";\n");
        }

        curOfs += s;
    }
    if (repr->align > maxCTypeAlignment && repr->size != SIZE_MAX && curOfs < repr->size) {
        of << StringView("\tu8 _trustme_tail[") << repr->size - curOfs << StringView("];\n");
        curOfs = repr->size;
        sizedFields++;
    }
    if (sizedFields == 0 && !hasUnsized && options.disallowEmptyStructs) {
        of << StringView("\tchar _d;\n");
    }
    of << StringView("}");
    if (hasManualAlign) {
        of << StringView(" __attribute__((__aligned__(") << emittedAlignment << StringView(")))");
    }
    of << StringView(";\n");
    if (packingMaxAlign != 0) {
        of << StringView("#pragma pack(pop)\n");
    }
}

auto CodeGeneratorC::emitType(const HIRTypeData* ty) -> void {
    MIRFunction emptyFcn;
    auto pathCallback = makeCallable<MIRPathCb>([&](auto& os) {
        os << StringView("type ") << ty;
    });
    MIRTypeResolve topMirRes{sp, resolve_, pathCallback, HIRTypeRef(), {}, emptyFcn};
    mirRes = &topMirRes;

    TRACE_FUNCTION_F(ty);
    switch ((*ty).tag()) {
        default:
            break;
        case HIRTypeData::TAG_Tuple: {
            auto& te = (*ty).as_Tuple();
            if (te.size() > 0) {
                const auto* repr = TargetGetTypeRepr(sp, resolve_, ty);

                emitStructInner(ty, repr, /*packing_max_align=*/0);

                if (repr->size > 0 && repr->size != SIZE_MAX) {
                    of << StringView("static_assert(sizeof(");
                    emitCtype(ty);
                    of << StringView(")==") << repr->size << StringView(");\n");
                }
            }
            break;
        }
        case HIRTypeData::TAG_Function: {
            emitTypeFn(ty);
            of << StringView("\n");
            break;
        }
        case HIRTypeData::TAG_NamedFunction: {
            of << StringView("struct ");
            emitCtype(ty);
            of << StringView(" {");
            if (options.disallowEmptyStructs) {
                of << StringView(" char _unused; ");
            }
            of << StringView("};\n");
            break;
        }
        case HIRTypeData::TAG_Array: {
            auto& te = (*ty).as_Array();
            size_t rustSize;
            ASSERT_BUG(sp, TargetGetSizeOf(sp, resolve_, ty, rustSize), StringView("Unable to determine array size for ") << ty);
            const bool isZeroSized = rustSize == 0;

            size_t align;
            if (isZeroSized) {
                TargetGetAlignOf(sp, resolve_, ty, align);
            }
            of << StringView("struct ");
            emitCtype(ty);
            of << StringView(" { ");
            if (isZeroSized && options.disallowEmptyStructs) {
                of << StringView("char _d;");
            } else if (isZeroSized) {
                if (te.size.as_Known() > 0) {
                    emitCtype(te.inner);
                    of << StringView(" DATA[1];");
                }
            } else {
                emitCtype(te.inner);
                of << StringView(" DATA[") << te.size.as_Known() << StringView("];");
            }
            of << StringView(" }");
            if (isZeroSized) {
                of << StringView(" __attribute__((");
                of << StringView("__aligned__(") << cTypeAlignment(0, align) << StringView("),");
                of << StringView("))");
            }
            of << StringView(";\n");
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
        os << StringView("struct ") << p;
    });
    MIRTypeResolve topMirRes{sp, resolve_, pathCallback, HIRTypeRef(), {}, emptyFcn};
    mirRes = &topMirRes;
    // TODO: repr(transparent) and repr(align(foo))

    TRACE_FUNCTION_F(p);
    auto itemTy = crate.types.path(p.clone(), HIRTypePathBinding::make_Struct(&item));
    const auto* repr = TargetGetTypeRepr(sp, resolve_, itemTy);
    MIR_ASSERT(*mirRes, repr, StringView("No repr for struct ") << p);

    emitStructInner(itemTy, repr, item.maxFieldAlignment);

    if (repr->size > 0 && repr->size != SIZE_MAX) {
        // TODO: Handle unsized (should check the size of the fixed-size region)
        of << StringView("static_assert(sizeof(s_") << TransMangle(p) << StringView(")==") << repr->size << StringView(");\n");
    }
    of << StringView("static_assert(ALIGNOF(s_") << TransMangle(p) << StringView(")==") << cTypeAlignment(repr->size, repr->align) << StringView(");\n");

    mirRes = nullptr;
}

auto CodeGeneratorC::emitUnion(const Span& sp, const HIRGenericPath& p, const HIRUnion& item) -> void {
    MIRFunction emptyFcn;
    auto pathCallback = makeCallable<MIRPathCb>([&](auto& os) {
        os << StringView("union ") << p;
    });
    MIRTypeResolve topMirRes{sp, resolve_, pathCallback, HIRTypeRef(), {}, emptyFcn};
    mirRes = &topMirRes;

    TRACE_FUNCTION_F(p);
    auto itemTy = crate.types.path(p.clone(), HIRTypePathBinding::make_Union(&item));
    const auto* repr = TargetGetTypeRepr(sp, resolve_, itemTy);
    MIR_ASSERT(*mirRes, repr != nullptr, StringView("No repr for union ") << itemTy);

    of << StringView("union u_") << TransMangle(p) << StringView(" {\n");
    for (unsigned int i = 0; i < repr->fields.size(); i++) {
        BUG_ASSERT(repr->fields[i].offset == 0);
        of << StringView("\t");
        emitCtype(repr->fields[i].ty, FMT_CB(ss, ss << StringView("var_") << i;));
        of << StringView(";\n");
    }
    if (repr->align > maxCTypeAlignment && repr->size > 0) {
        of << StringView("\tu8 _trustme_size[") << repr->size << StringView("];\n");
    }
    of << StringView("}");
    if (item.maxFieldAlignment > 0) {
        of << StringView(" __attribute__((packed))");
    }
    if (repr->align > 0) {
        of << StringView(" __attribute__((__aligned__(") << cTypeAlignment(repr->size, repr->align) << StringView(")))");
    }
    of << StringView(";\n");
    if (true && repr->size > 0) {
        of << StringView("static_assert(sizeof(u_") << TransMangle(p) << StringView(")==") << repr->size << StringView(");\n");
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
        if (embeddedTags.count(repr)) {
            of << StringView(".DATA");
        }
        of << StringView(".TAG");
        BUG_ASSERT(path.subFields.empty());
    } else {
        of << StringView(".DATA.var_") << path.index;
    }
    const auto* ty = &repr->fields[path.index].ty;
    for (const auto& fld : path.subFields) {
        if (fld == TypeRepr::FieldPath::ARRAY_ELEMENT) {
            const auto* array = (*ty)->opt_Array();
            BUG_ASSERT(array && array->size.is_Known() && array->size.as_Known() > 0);
            of << StringView(".DATA[0]");
            ty = &array->inner;
            continue;
        }
        repr = TargetGetTypeRepr(sp, resolve_, *ty);
        if (isEnumTag(repr, fld)) {
            if (embeddedTags.count(repr)) {
                of << StringView(".DATA");
            }
            of << StringView(".TAG");
            BUG_ASSERT(&fld == &path.subFields.back());
        } else if (/*!repr->variants.is_None() ||*/ ((**ty).is_Path() && ((**ty).as_Path().binding.is_Enum()))) {
            of << StringView(".DATA.var_") << fld;
        } else {
            of << StringView("._") << fld;
        }

        ty = &repr->fields[fld].ty;
    }
    if (const auto* te = (*ty)->opt_Borrow()) {
        if (isDst(te->inner)) {
            of << StringView(".PTR");
        }
    } else if (const auto* te = (*ty)->opt_Pointer()) {
        if (isDst(te->inner)) {
            of << StringView(".PTR");
        }
    }
    return *ty;
}

auto CodeGeneratorC::emitEnum(const Span& sp, const HIRGenericPath& p, const HIREnum& item) -> void {
    MIRFunction emptyFcn;
    auto pathCallback = makeCallable<MIRPathCb>([&](auto& os) {
        os << StringView("enum ") << p;
    });
    MIRTypeResolve topMirRes{sp, resolve_, pathCallback, HIRTypeRef(), {}, emptyFcn};
    mirRes = &topMirRes;

    TRACE_FUNCTION_F(p);
    auto itemTy = crate.types.path(p.clone(), HIRTypePathBinding::make_Enum(&item));
    const auto* repr = TargetGetTypeRepr(sp, resolve_, itemTy);

    // TODO: What if all data variants are zero-sized?

    const bool hasSeparateTag = repr->fields.size() >= 2 && isEnumTag(repr, repr->fields.size() - 1) && repr->fields.back().offset != repr->fields[0].offset;
    const size_t dataFieldCount = repr->fields.size() - (hasSeparateTag ? 1 : 0);
    std::vector<unsigned> unionFields;
    for (size_t i = 1; i < dataFieldCount; i++) {
        if (repr->fields[i].offset == repr->fields[0].offset) {
            unionFields.push_back(i);
        }
    }
    if (unionFields.size() > 0 || (hasSeparateTag && dataFieldCount == 1)) {
        unionFields.insert(unionFields.begin(), 0);
    }

    of << StringView("struct e_") << TransMangle(p) << StringView(" {\n");

    // HACK: For NonZero optimised enums, emit a struct with a single field
    // - This avoids a bug in GCC5 where it would generate incorrect code if there's a union here.
    if (const auto* ve = repr->variants.opt_NonZero()) {
        of << StringView("\tstruct {\n");
        of << StringView("\t\t");
        unsigned idx = 1 - ve->zeroVariant;
        emitCtype(repr->fields.at(idx).ty, FMT_CB(os, os << StringView("var_") << idx));
        of << StringView(";\n");
        of << StringView("\t} DATA;\n");
    } else if (repr->fields.size() == 1) {
        if (repr->variants.is_Values()) {
            of << StringView("\t");
            emitCtype(repr->fields.back().ty, FMT_CB(os, os << StringView("TAG")));
            of << StringView(";\n");
        } else {
            of << StringView("\tunion {\n");
            of << StringView("\t\t");
            emitCtype(repr->fields.back().ty, FMT_CB(os, os << StringView("var_0")));
            of << StringView(";\n");
            of << StringView("\t} DATA;\n");
        }
    } else if (unionFields.size() > 0) {
        if (unionFields.size() == repr->fields.size()) {
        } else {
            BUG_ASSERT(unionFields.size() + 1 == repr->fields.size());
            BUG_ASSERT(isEnumTag(repr, repr->fields.size() - 1));

            BUG_ASSERT(repr->fields.back().offset == 0);

            DEBUG(StringView("Tag present at offset ") << repr->fields.back().offset << StringView(" - ") << repr->fields.back().ty);
            of << StringView("\t");
            emitCtype(repr->fields.back().ty, FMT_CB(os, os << StringView("TAG")));
            of << StringView(";\n");
        }

        if (std::any_of(unionFields.begin(), unionFields.end(), [this, repr](auto x) {
            return !this->typeIsBadZst(repr->fields[x].ty);
        })) {
            of << StringView("\tunion {\n");
            for (auto idx : unionFields) {
                const auto& ty = repr->fields[idx].ty;
                if (!this->typeIsBadZst(ty)) {
                    of << StringView("\t\t");
                    if (isEnumTag(repr, idx)) {
                        emitCtype(ty, FMT_CB(ss, ss << StringView("TAG")));
                        embeddedTags.insert(repr);
                    } else {
                        emitCtype(ty, FMT_CB(ss, ss << StringView("var_") << idx));
                    }
                    of << StringView(";\n");
                }
            }
            of << StringView("\t} DATA;\n");
        }
    } else if (repr->fields.size() == 0) {
        if (options.disallowEmptyStructs) {
            of << StringView("\tchar _d;\n");
        }
    } else {
        TODO(sp, StringView("No common offsets and more than one field, is this possible? - ") << itemTy);
    }

    if (repr->align > maxCTypeAlignment && repr->size > 0) {
        size_t contentEnd = 0;
        for (const auto& field : repr->fields) {
            size_t fieldSize = 0;
            MIR_ASSERT(*mirRes, TargetGetSizeOf(sp, resolve_, field.ty, fieldSize), StringView("Unknown enum field size"));
            if (fieldSize != SIZE_MAX && contentEnd < field.offset + fieldSize) {
                contentEnd = field.offset + fieldSize;
            }
        }
        if (contentEnd < repr->size) {
            of << StringView("\tu8 _trustme_tail[") << repr->size - contentEnd << StringView("];\n");
        }
    }

    of << StringView("}");
    if (item.forcedAlignment > 0) {
        of << StringView(" __attribute__((__aligned__(") << cTypeAlignment(repr->size, repr->align) << StringView(")))");
    }
    of << StringView(";\n");

    size_t expSize = (repr->size > 0 ? repr->size : (options.disallowEmptyStructs ? 1 : 0));
    of << StringView("static_assert(sizeof(e_") << TransMangle(p) << StringView(")==") << expSize << StringView(");\n");

    mirRes = nullptr;
}

auto CodeGeneratorC::emitConstructorEnum(const Span& sp, const HIRGenericPath& path, const HIREnum& item, size_t varIdx) -> void {
    TRACE_FUNCTION_F(path << StringView(" var_idx=") << varIdx);
    auto p = path.clone();
    p.path.popComponent();
    auto ty = crate.types.path(p.clone(), HIRTypePathBinding::make_Enum(&item));

    MonomorphStatePtr ms(crate.types, nullptr, &path.params, nullptr);
    HIRTypeRef tmp;
    auto monomorph = [&](const auto& x) {
        return resolve_.monomorphExpandOpt(sp, tmp, x, ms);
    };

    ASSERT_BUG(sp, item.data.is_Data(), StringView(""));
    const auto& var = item.data.as_Data().at(varIdx);
    ASSERT_BUG(sp, var.type->is_Path(), StringView(""));
    const auto& str = *var.type->as_Path().binding.as_Struct();
    ASSERT_BUG(sp, str.data.is_Tuple(), StringView(""));
    const auto& e = str.data.as_Tuple();

    HIRFunction::argsT args;
    for (unsigned int i = 0; i < e.size(); i++) {
        args.push_back(std::make_pair(HIRPattern(), monomorph(e[i].ent)));
    }

    MIRFunction emptyFcn;
    auto pathCallback = makeCallable<MIRPathCb>([&](auto& os) {
        os << StringView("enum cons ") << path;
    });
    MIRTypeResolve topMirRes{sp, resolve_, pathCallback, ty, args, emptyFcn};
    mirRes = &topMirRes;

    of << StringView("static e_") << TransMangle(p) << StringView(" ") << TransMangleValue(path) << StringView("(");
    for (unsigned int i = 0; i < e.size(); i++) {
        if (i != 0) {
            of << StringView(", ");
        }
        const auto& ty = args[i].second;
        emitCtype(ty, FMT_CB(ss, ss << StringView("arg") << i;));
    }
    of << StringView(") {\n");

    of << StringView("\te_") << TransMangle(p) << StringView(" rv;\n");

    std::vector<MIRParam> vals;
    for (unsigned int i = 0; i < e.size(); i++) {
        vals.push_back(MIRLValue::newArgument(i));
    }

    emitStatement(*mirRes, MIRStatement::make_Assign({MIRLValue::newReturn(), MIRRValue::make_EnumVariant({p.clone(), static_cast<unsigned>(varIdx), mv$(vals)})}));
    of << StringView("\treturn rv;\n");
    of << StringView("}\n\n");
    mirRes = nullptr;
}

auto CodeGeneratorC::emitConstructorStruct(const Span& sp, const HIRGenericPath& p, const HIRStruct& item) -> void {
    TRACE_FUNCTION_F(p);
    HIRTypeRef tmp;
    MonomorphStatePtr ms(crate.types, nullptr, &p.params, nullptr);
    auto monomorph = [&](const auto& x) {
        return resolve_.monomorphExpandOpt(sp, tmp, x, ms);
    };

    const auto& e = item.data.as_Tuple();
    of << StringView("static s_") << TransMangle(p) << StringView(" ") << TransMangleValue(p) << StringView("(");
    for (unsigned int i = 0; i < e.size(); i++) {
        if (i != 0) {
            of << StringView(", ");
        }
        const auto& ty = monomorph(e[i].ent);
        emitCtype(ty, FMT_CB(ss, ss << StringView("_") << i;));
    }
    of << StringView(") {\n");
    of << StringView("\ts_") << TransMangle(p) << StringView(" rv = {};\n");
    for (unsigned int i = 0; i < e.size(); i++) {
        const auto& ty = monomorph(e[i].ent);
        if (this->typeIsBadZst(ty)) {
            continue;
        }
        of << StringView("\trv._") << i << StringView(" = _") << i << StringView(";\n");
    }
    of << StringView("\treturn rv;\n");
    of << StringView("}\n\n");
}

auto CodeGeneratorC::emitExternTypeDefinition(const HIRTypeData* type) -> void {
    if (type->is_Path() && type->as_Path().binding.is_ExternType()) {
        of << StringView("struct x_") << TransMangle(type->as_Path().path) << StringView(" { };\n");
    }
}

auto CodeGeneratorC::emitStaticTy(const HIRTypeData* type, const HIRPath& p, bool isProto, size_t explicitAlignment) -> bool {
    size_t size = 0, align = 0;
    const bool sized = TargetGetSizeAndAlignOf(sp, resolve_, type, size, align);
    align = std::max(align, explicitAlignment);
    bool rv = (align * 8 >= TargetGetPointerBits());
    of << StringView("union u_static_") << TransMangleValue(p);
    if (!sized || size == SIZE_MAX) {
        if (isProto) {
            of << StringView("{ ");
            emitCtype(type, FMT_CB(ss, ss << StringView("val");));
            of << StringView("; u8 raw[1]; }");
        }
        of << StringView(" ") << TransMangleValue(p);
        return false;
    }
    if (isProto) {
        of << StringView("{ ");
        emitCtype(type, FMT_CB(ss, ss << StringView("val");));
        of << StringView("; ");
        if (rv) {
            const auto pointerSize = TargetGetPointerBits() / 8;
            const auto words = size == 0 ? 0 : 1 + (size - 1) / pointerSize;
            of << StringView("uintptr_t raw[") << words << StringView("];");
        } else {
            of << StringView("u8 raw[") << size << StringView("];");
        }
        of << StringView(" }");
    }
    of << StringView(" ") << TransMangleValue(p);
    return rv;
}

auto CodeGeneratorC::emitStaticExt(const HIRPath& p, const HIRStatic& item, const TransParams& params) -> void {
    MIRFunction emptyFcn;
    auto pathCallback = makeCallable<MIRPathCb>([&](auto& os) {
        os << StringView("extern static ") << p;
    });
    MIRTypeResolve topMirRes{sp, resolve_, pathCallback, HIRTypeRef(), {}, emptyFcn};
    mirRes = &topMirRes;
    auto type = params.monomorph(resolve_, item.type);

    std::string linkageName = item.linkage.name;
    if (!linkageName.empty() && linkageName[0] == '\1') {
        linkageName = linkageName.substr(1);
    }

    if (item.linkage.type == HIRLinkage::Type::ExternWeak) {
        ASSERT_BUG(sp, linkageName != "", StringView(""));
        of << StringView("extern char ");
        of << StringView("__attribute__((weak)) ");

        of << linkageName << StringView("[0];\n");

        emitStaticTy(type, p, /*is_proto=*/true, item.explicitAlignment);
        of << StringView(" = { .raw = { (uintptr_t)") << linkageName << StringView(" } };");
        of << StringView("\n");
        return;
    }

    if (linkageName != "") {
    }

    emitExternTypeDefinition(type);
    of << StringView("extern ");
    emitStaticTy(type, p, /*is_proto=*/true, item.explicitAlignment);
    if (linkageName != "") {
        if (TargetGetCurSpec(wb_).osName == "macos") {
            of << StringView(" asm(\"_") << linkageName << StringView("\")");
        } else {
            of << StringView(" asm(\"") << linkageName << StringView("\")");
        }
    }
    of << StringView(";\n");

    mirRes = nullptr;
}

auto CodeGeneratorC::emitStaticProto(const HIRPath& p, const HIRStatic& item, const TransParams& params) -> void {
    MIRFunction emptyFcn;
    auto pathCallback = makeCallable<MIRPathCb>([&](auto& os) {
        os << StringView("static ") << p;
    });
    MIRTypeResolve topMirRes{sp, resolve_, pathCallback, HIRTypeRef(), {}, emptyFcn};
    mirRes = &topMirRes;

    auto type = params.monomorph(resolve_, item.type);
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
            of << StringView("__attribute__((weak)) ");

            break;
        case HIRLinkage::Type::ExternWeak:
            of << StringView("__attribute__((weak_import)) ");

            break;
    }
    if (item.linkage.section != "") {
        of << StringView("__attribute__((section(\"") << item.linkage.section << StringView("\"))) ");
    }
    if (item.params.isGeneric()) {
        of << StringView("__attribute__((weak)) ");
    }
    emitExternTypeDefinition(type);
    of << StringView("extern ");
    emitStaticTy(type, p, /*is_proto=*/true, item.explicitAlignment);
    if (item.explicitAlignment != 0) {
        of << StringView(" __attribute__((aligned(") << item.explicitAlignment << StringView(")))");
    }
    of << StringView(";\n");

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
        os << StringView("static ") << p;
    });
    MIRTypeResolve topMirRes{sp, resolve_, pathCallback, HIRTypeRef(), {}, emptyFcn};
    mirRes = &topMirRes;

    TRACE_FUNCTION_F(p);
    auto type = params.monomorph(resolve_, item.type);
    const bool isZero = isZeroLiteral(type, encoded, params);

    const bool blobLinkage = item.linkage.type == HIRLinkage::Type::Auto || item.linkage.type == HIRLinkage::Type::Weak;
    if (!isZero && encoded.bytes.size() >= 64 * 1024 && encoded.relocations.empty() && TargetGetCurSpec(wb_).osName == "linux" && blobLinkage && item.linkage.name.empty() && item.linkage.section.empty() && literalBlobPathIsSafe()) {
        size_t size = 0;
        size_t align = 0;
        MIR_ASSERT(topMirRes, TargetGetSizeAndAlignOf(sp, resolve_, type, size, align), StringView("Unsized static ") << p);
        MIR_ASSERT(topMirRes, size == encoded.bytes.size(), StringView("Static size differs from its encoded value: ") << size << StringView(" != ") << encoded.bytes.size());
        if (align < item.explicitAlignment) {
            align = item.explicitAlignment;
        }

        const size_t blobOffset = appendLiteralBlob(encoded);
        const bool weak = item.params.isGeneric() || item.linkage.type == HIRLinkage::Type::Weak;
        of << StringView("__asm__(\n");
        of << StringView("\".pushsection .data\\n\"\n");
        of << StringView("\".balign ") << align << StringView("\\n\"\n");
        of << StringView("\".") << (weak ? "weak " : "globl ") << TransMangleValue(p) << StringView("\\n\"\n");
        of << StringView("\".type ") << TransMangleValue(p) << StringView(",@object\\n\"\n");
        of << StringView("\"") << TransMangleValue(p) << StringView(":\\n\"\n");
        of << StringView("\".incbin \\\"") << outfilePath << StringView(".blob\\\", ") << blobOffset << StringView(", ") << encoded.bytes.size() << StringView("\\n\"\n");
        of << StringView("\".size ") << TransMangleValue(p) << StringView(",") << size << StringView("\\n\"\n");
        of << StringView("\".popsection\\n\");\n");
        mirRes = nullptr;
        return;
    }

    if (item.params.isGeneric()) {
        of << StringView("__attribute__((weak)) ");
    }
    bool isPacked = emitStaticTy(type, p, /*is_proto=*/false, item.explicitAlignment);
    if (item.explicitAlignment != 0) {
        of << StringView(" __attribute__((aligned(") << item.explicitAlignment << StringView(")))");
    }
    of << StringView(" = ");

    if (isZero) {
        of << StringView("{}");
    } else {
        of << StringView("{ .raw = {");
        if (isPacked) {
            DEBUG(StringView("encoded.bytes = `") << FMT_CB(ss, for (auto& b : encoded.bytes) ss << formatHex(unsigned(b), 2) << StringView(int(&b - encoded.bytes.data()) % 8 == 7 ? " " : "");) << StringView("`"));
            DEBUG(StringView("encoded.relocations = ") << encoded.relocations);
            auto relocIt = encoded.relocations.begin();
            auto ptrSize = TargetGetPointerBits() / 8;
            for (size_t i = 0; i < encoded.bytes.size(); i += ptrSize) {
                u64 v = 0;
                for (size_t o = 0; o < ptrSize && i + o < encoded.bytes.size(); o++) {
                    v |= static_cast<u64>(encoded.bytes[i + o]) << (o * 8);
                }

                if (i > 0) {
                    of << StringView(",");
                }

                if (relocIt != encoded.relocations.end() && relocIt->ofs <= i) {
                    MIR_ASSERT(*mirRes, relocIt->ofs == i, StringView("Relocation not aligned to a pointer - ") << relocIt->ofs << StringView(" != ") << i);
                    MIR_ASSERT(*mirRes, relocIt->len == ptrSize, StringView("Relocation size not pointer size - ") << relocIt->len << StringView(" != ") << ptrSize);
                    v -= EncodedLiteral::PTR_BASE;
                    //MIR_ASSERT(*m_mir_res, v == 0, StringView("TODO: Relocation with non-zero offset ") << i << ": v=0x" << std::hex << v << std::dec << " Reloc=" << *reloc_it << " Literal=" << encoded);

                    of << StringView("(uintptr_t)");
                    if (relocIt->p) {
                        if (relocIt->p->data.is_UfcsInherent() && relocIt->p->data.as_UfcsInherent().item == "#type_id") {
                            const auto& ty = relocIt->p->data.as_UfcsInherent().type;
                            of << StringView("&__typeid_") << TransMangleTypeId(ty);
                        } else {
                            of << StringView("&");
                            emitReifiedFunctionName(*relocIt->p, relocIt->preserveTrackCaller);
                        }
                    } else {
                        this->printEscapedString(relocIt->bytes);
                    }
                    if (v > 0) {
                        of << StringView("+") << v;
                    }

                    ++relocIt;
                } else {
                    of << StringView("0x") << formatHex(v) << StringView("ull");
                }
            }
        } else {
            MIR_ASSERT(*mirRes, encoded.relocations.empty(), StringView("Non-pointer-aligned data with relocations"));
            bool e = false;
            for (auto b : encoded.bytes) {
                if (e) {
                    of << StringView(",");
                }
                of << int(b);
                e = true;
            }
        }
        of << StringView("} }");
    }
    of << StringView(";\n");
    mirRes = nullptr;
}

auto CodeGeneratorC::emitFloat(FloatValue v, HIRCoreType ty) -> void {
    if (ty == HIRCoreType::F16) {
        const F16 bits(v);
        of << StringView("make_f16_bits(0x") << formatHex(bits.v) << StringView("u)");
    } else if (ty == HIRCoreType::F32) {
        const float value = static_cast<float>(v);
        u32 bits;
        std::memcpy(&bits, &value, sizeof(bits));
        of << StringView("make_f32_bits(0x") << formatHex(bits) << StringView("u)");
    } else if (ty == HIRCoreType::F64) {
        const double value = static_cast<double>(v);
        u64 bits;
        std::memcpy(&bits, &value, sizeof(bits));
        of << StringView("make_f64_bits(0x") << formatHex(bits) << StringView("ull)");
    } else if (ty == HIRCoreType::F128) {
        const F128 bits(v);
        of << StringView("make_f128_bits(0x") << formatHex(bits.hi) << StringView("ull, 0x") << formatHex(bits.lo) << StringView("ull)");
    } else {
        BUG(Span(), StringView("Unexpected floating-point type ") << ty);
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
    of << StringView("\"");
    unsigned nCh = 0;
    while (start != end) {
        const char v = *start++;
        switch (v) {
            case '"':
                of << StringView("\\\"");
                break;
            case '\\':
                of << StringView("\\\\");
                break;
            case '\n':
                of << StringView("\\n");
                break;
            case '?':
                if (end - start >= 2 && start[0] == '?') {
                    if (start[1] == '!') {
                        of << v;
                        of << StringView("\"\"");
                        nCh = 0;
                        break;
                    }
                }
            default:
                if (' ' <= v && static_cast<u8>(v) < 0x7F) {
                    of << v;
                } else {
                    if (static_cast<u8>(v) < 16) {
                        of << StringView("\\x0") << formatHex(static_cast<u8>(v));
                    } else {
                        of << StringView("\\x") << formatHex(static_cast<u8>(v));
                    }
                    if (start != end && isxdigit(static_cast<unsigned char>(*start))) {
                        of << StringView("\"\"");
                        nCh = 0;
                    }
                }
        }
        nCh++;
        if (nCh == MAX_STRING_LEN) {
            of << StringView("\"\"");
            nCh = 0;
        }
    }
    of << StringView("\"");
}

auto CodeGeneratorC::emitFunctionExt(const HIRPath& p, const HIRFunction& item, const TransParams& params) -> void {
    MIRFunction emptyFcn;
    auto pathCallback = makeCallable<MIRPathCb>([&](auto& os) {
        os << StringView("extern fn ") << p;
    });
    MIRTypeResolve topMirRes{sp, resolve_, pathCallback, HIRTypeRef(), {}, emptyFcn};
    mirRes = &topMirRes;
    TRACE_FUNCTION_F(p);
    const bool tracksCaller = crate.functionTracksCaller(sp, p, item);
    if (tracksCaller) {
        trackedFunctions.insert(p.clone());
    }

    if (item.linkage.name.rfind("llvm.", 0) == 0) {
        of << StringView("static ");
        emitFunctionHeader(p, item, params);
        of << StringView(" {\n");
        of << StringView("\t");
        emitCtype(item.returnType);
        of << StringView(" rv;\n");

        if (item.linkage.name == "llvm.prefetch") {
            of << StringView("\tif(arg1) {\n")
               << StringView("\t\tswitch(arg2) {\n")
               << StringView("\t\tcase 0: __builtin_prefetch(arg0, 1, 0); break;\n")
               << StringView("\t\tcase 1: __builtin_prefetch(arg0, 1, 1); break;\n")
               << StringView("\t\tcase 2: __builtin_prefetch(arg0, 1, 2); break;\n")
               << StringView("\t\tdefault: __builtin_prefetch(arg0, 1, 3); break;\n")
               << StringView("\t\t}\n")
               << StringView("\t} else {\n")
               << StringView("\t\tswitch(arg2) {\n")
               << StringView("\t\tcase 0: __builtin_prefetch(arg0, 0, 0); break;\n")
               << StringView("\t\tcase 1: __builtin_prefetch(arg0, 0, 1); break;\n")
               << StringView("\t\tcase 2: __builtin_prefetch(arg0, 0, 2); break;\n")
               << StringView("\t\tdefault: __builtin_prefetch(arg0, 0, 3); break;\n")
               << StringView("\t\t}\n")
               << StringView("\t}\n")
               << StringView("\treturn;\n");
        } else if (item.linkage.name == "llvm.x86.ssse3.pshuf.b.128") {
            of << StringView("\tconst u8* src = (const u8*)&arg0;\n")
               << StringView("\tconst u8* mask = (const u8*)&arg1;\n")
               << StringView("\tu8* dst = (u8*)&rv;\n")
               << StringView("\tfor(int i = 0; i < ") << 128 / 8 << StringView("; i ++) dst[i] = (mask[i] < 0x80 ? src[mask[i] & 0xF] : 0);\n")
               << StringView("\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.avx2.pshuf.b") {
            of << StringView("\tconst u8* src = (const u8*)&arg0;\n")
               << StringView("\tconst u8* mask = (const u8*)&arg1;\n")
               << StringView("\tu8* dst = (u8*)&rv;\n")
               << StringView("\tfor(int i = 0; i < ") << 256 / 8 << StringView("; i ++) dst[i] = (mask[i] < 0x80 ? src[(i & 16) | (mask[i] & 0xF)] : 0);\n")
               << StringView("\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.ssse3.pmadd.ub.sw.128" || item.linkage.name == "llvm.x86.avx2.pmadd.ub.sw") {
            int n = (item.linkage.name == "llvm.x86.avx2.pmadd.ub.sw" ? 32 : 16);
            of << StringView("\tconst u8* a = (const u8*)&arg0;\n")
               << StringView("\tconst i8* b = (const i8*)&arg1;\n")
               << StringView("\ti16* dst = (i16*)&rv;\n")
               << StringView("\tfor(int i = 0; i < ") << n / 2 << StringView("; i ++) {\n")
               << StringView("\t\ti32 v = (i32)a[2*i]*b[2*i] + (i32)a[2*i+1]*b[2*i+1];\n")
               << StringView("\t\tdst[i] = (i16)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));\n")
               << StringView("\t}\n")
               << StringView("\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.sse2.pmadd.wd" || item.linkage.name == "llvm.x86.avx2.pmadd.wd") {
            int n = (item.linkage.name == "llvm.x86.avx2.pmadd.wd" ? 16 : 8);
            of << StringView("\tconst i16* a = (const i16*)&arg0;\n")
               << StringView("\tconst i16* b = (const i16*)&arg1;\n")
               << StringView("\ti32* dst = (i32*)&rv;\n")
               << StringView("\tfor(int i = 0; i < ") << n / 2 << StringView("; i ++) dst[i] = (i32)a[2*i]*b[2*i] + (i32)a[2*i+1]*b[2*i+1];\n")
               << StringView("\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.sse2.psad.bw" || item.linkage.name == "llvm.x86.avx2.psad.bw") {
            int n = (item.linkage.name == "llvm.x86.avx2.psad.bw" ? 32 : 16);
            of << StringView("\tconst u8* a = (const u8*)&arg0;\n")
               << StringView("\tconst u8* b = (const u8*)&arg1;\n")
               << StringView("\tu64* dst = (u64*)&rv;\n")
               << StringView("\tfor(int k = 0; k < ") << n / 8 << StringView("; k ++) {\n")
               << StringView("\t\tu64 sum = 0;\n")
               << StringView("\t\tfor(int j = 0; j < 8; j ++) { int d = (int)a[k*8+j] - (int)b[k*8+j]; sum += (d < 0 ? -d : d); }\n")
               << StringView("\t\tdst[k] = sum;\n")
               << StringView("\t}\n")
               << StringView("\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.sse.cmp.ps") {
            of << StringView("\tfloat lhs[4], rhs[4]; u32 result[4];\n")
               << StringView("\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(rhs, &arg1, sizeof(rhs));\n")
               << StringView("\tfor(unsigned i = 0; i < 4; i++) result[i] = trustme_x86_cmp_f32(lhs[i], rhs[i], arg2) ? UINT32_MAX : 0;\n")
               << StringView("\tmemcpy(&rv, result, sizeof(result));\n")
               << StringView("\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.sse.cmp.ss") {
            of << StringView("\tfloat lhs[4], rhs[4]; u32 result[4];\n")
               << StringView("\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(rhs, &arg1, sizeof(rhs)); memcpy(result, &arg0, sizeof(result));\n")
               << StringView("\tresult[0] = trustme_x86_cmp_f32(lhs[0], rhs[0], arg2) ? UINT32_MAX : 0;\n")
               << StringView("\tmemcpy(&rv, result, sizeof(result));\n")
               << StringView("\treturn rv;\n");
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
            of << StringView("\tfloat lhs[4], rhs[4];\n")
               << StringView("\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(rhs, &arg1, sizeof(rhs));\n")
               << StringView("\treturn lhs[0] ") << op << StringView(" rhs[0];\n");
        } else if (item.linkage.name == "llvm.x86.sse.cvtsi2ss" || item.linkage.name == "llvm.x86.sse.cvtsi642ss") {
            of << StringView("\tfloat result[4];\n")
               << StringView("\tmemcpy(result, &arg0, sizeof(result)); result[0] = (float)arg1;\n")
               << StringView("\tmemcpy(&rv, result, sizeof(result));\n")
               << StringView("\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.sse.cvtss2si" || item.linkage.name == "llvm.x86.sse.cvttss2si" || item.linkage.name == "llvm.x86.sse.cvtss2si64" || item.linkage.name == "llvm.x86.sse.cvttss2si64") {
            const bool truncate = item.linkage.name == "llvm.x86.sse.cvttss2si" || item.linkage.name == "llvm.x86.sse.cvttss2si64";
            const bool is64 = item.linkage.name == "llvm.x86.sse.cvtss2si64" || item.linkage.name == "llvm.x86.sse.cvttss2si64";
            of << StringView("\tfloat input[4]; memcpy(input, &arg0, sizeof(input));\n")
               << StringView("\treturn trustme_x86_f32_to_i") << (is64 ? 64 : 32) << StringView("(input[0], ") << truncate << StringView(");\n");
        } else if (item.linkage.name == "llvm.x86.sse.min.ps" || item.linkage.name == "llvm.x86.sse.min.ss" || item.linkage.name == "llvm.x86.sse.max.ps" || item.linkage.name == "llvm.x86.sse.max.ss") {
            const bool isMin = item.linkage.name == "llvm.x86.sse.min.ps" || item.linkage.name == "llvm.x86.sse.min.ss";
            const bool scalar = item.linkage.name == "llvm.x86.sse.min.ss" || item.linkage.name == "llvm.x86.sse.max.ss";
            of << StringView("\tfloat lhs[4], rhs[4], result[4];\n")
               << StringView("\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(rhs, &arg1, sizeof(rhs)); memcpy(result, &arg0, sizeof(result));\n")
               << StringView("\tfor(unsigned i = 0; i < ") << (scalar ? 1 : 4) << StringView("; i++) result[i] = lhs[i] ") << StringView(isMin ? "<" : ">") << StringView(" rhs[i] ? lhs[i] : rhs[i];\n")
               << StringView("\tmemcpy(&rv, result, sizeof(result));\n")
               << StringView("\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.sse.rcp.ps" || item.linkage.name == "llvm.x86.sse.rcp.ss" || item.linkage.name == "llvm.x86.sse.rsqrt.ps" || item.linkage.name == "llvm.x86.sse.rsqrt.ss") {
            const bool reciprocalSqrt = item.linkage.name == "llvm.x86.sse.rsqrt.ps" || item.linkage.name == "llvm.x86.sse.rsqrt.ss";
            const bool scalar = item.linkage.name == "llvm.x86.sse.rcp.ss" || item.linkage.name == "llvm.x86.sse.rsqrt.ss";
            of << StringView("\tfloat result[4]; memcpy(result, &arg0, sizeof(result));\n")
               << StringView("\tfor(unsigned i = 0; i < ") << (scalar ? 1 : 4) << StringView("; i++) result[i] = 1.0f / ");
            if (reciprocalSqrt) {
                of << StringView("__builtin_sqrtf(result[i])");
            } else {
                of << StringView("result[i]");
            }
            of << StringView(";\n\tmemcpy(&rv, result, sizeof(result));\n\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.sse2.cmp.pd") {
            of << StringView("\tdouble lhs[2], rhs[2]; u64 result[2];\n")
               << StringView("\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(rhs, &arg1, sizeof(rhs));\n")
               << StringView("\tfor(unsigned i = 0; i < 2; i++) result[i] = trustme_x86_cmp_f64(lhs[i], rhs[i], arg2) ? UINT64_MAX : 0;\n")
               << StringView("\tmemcpy(&rv, result, sizeof(result));\n")
               << StringView("\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.sse2.cmp.sd") {
            of << StringView("\tdouble lhs[2], rhs[2]; u64 result[2];\n")
               << StringView("\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(rhs, &arg1, sizeof(rhs)); memcpy(result, &arg0, sizeof(result));\n")
               << StringView("\tresult[0] = trustme_x86_cmp_f64(lhs[0], rhs[0], arg2) ? UINT64_MAX : 0;\n")
               << StringView("\tmemcpy(&rv, result, sizeof(result));\n")
               << StringView("\treturn rv;\n");
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
            of << StringView("\tdouble lhs[2], rhs[2];\n")
               << StringView("\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(rhs, &arg1, sizeof(rhs));\n")
               << StringView("\treturn lhs[0] ") << op << StringView(" rhs[0];\n");
        } else if (item.linkage.name == "llvm.x86.sse2.cvtpd2dq" || item.linkage.name == "llvm.x86.sse2.cvttpd2dq" || item.linkage.name == "llvm.x86.sse2.cvtps2dq" || item.linkage.name == "llvm.x86.sse2.cvttps2dq") {
            const bool inputIsDouble = item.linkage.name == "llvm.x86.sse2.cvtpd2dq" || item.linkage.name == "llvm.x86.sse2.cvttpd2dq";
            const bool truncate = item.linkage.name == "llvm.x86.sse2.cvttpd2dq" || item.linkage.name == "llvm.x86.sse2.cvttps2dq";
            if (inputIsDouble) {
                of << StringView("\tdouble input[2]; i32 result[4] = {0, 0, 0, 0}; memcpy(input, &arg0, sizeof(input));\n")
                   << StringView("\tfor(unsigned i = 0; i < 2; i++) result[i] = trustme_x86_f64_to_i32(input[i], ") << truncate << StringView(");\n");
            } else {
                of << StringView("\tfloat input[4]; i32 result[4]; memcpy(input, &arg0, sizeof(input));\n")
                   << StringView("\tfor(unsigned i = 0; i < 4; i++) result[i] = trustme_x86_f32_to_i32(input[i], ") << truncate << StringView(");\n");
            }
            of << StringView("\tmemcpy(&rv, result, sizeof(result));\n\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.sse2.cvtsd2si" || item.linkage.name == "llvm.x86.sse2.cvttsd2si" || item.linkage.name == "llvm.x86.sse2.cvtsd2si64" || item.linkage.name == "llvm.x86.sse2.cvttsd2si64") {
            const bool truncate = item.linkage.name == "llvm.x86.sse2.cvttsd2si" || item.linkage.name == "llvm.x86.sse2.cvttsd2si64";
            const bool is64 = item.linkage.name == "llvm.x86.sse2.cvtsd2si64" || item.linkage.name == "llvm.x86.sse2.cvttsd2si64";
            of << StringView("\tdouble input[2]; memcpy(input, &arg0, sizeof(input));\n")
               << StringView("\treturn trustme_x86_f64_to_i") << (is64 ? 64 : 32) << StringView("(input[0], ") << truncate << StringView(");\n");
        } else if (item.linkage.name == "llvm.x86.sse2.cvtsd2ss") {
            of << StringView("\tfloat result[4]; double input[2];\n")
               << StringView("\tmemcpy(result, &arg0, sizeof(result)); memcpy(input, &arg1, sizeof(input)); result[0] = (float)input[0];\n")
               << StringView("\tmemcpy(&rv, result, sizeof(result));\n\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.sse2.cvtss2sd") {
            of << StringView("\tdouble result[2]; float input[4];\n")
               << StringView("\tmemcpy(result, &arg0, sizeof(result)); memcpy(input, &arg1, sizeof(input)); result[0] = (double)input[0];\n")
               << StringView("\tmemcpy(&rv, result, sizeof(result));\n\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.sse2.min.pd" || item.linkage.name == "llvm.x86.sse2.min.sd" || item.linkage.name == "llvm.x86.sse2.max.pd" || item.linkage.name == "llvm.x86.sse2.max.sd") {
            const bool isMin = item.linkage.name == "llvm.x86.sse2.min.pd" || item.linkage.name == "llvm.x86.sse2.min.sd";
            const bool scalar = item.linkage.name == "llvm.x86.sse2.min.sd" || item.linkage.name == "llvm.x86.sse2.max.sd";
            of << StringView("\tdouble lhs[2], rhs[2], result[2];\n")
               << StringView("\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(rhs, &arg1, sizeof(rhs)); memcpy(result, &arg0, sizeof(result));\n")
               << StringView("\tfor(unsigned i = 0; i < ") << (scalar ? 1 : 2) << StringView("; i++) result[i] = lhs[i] ") << StringView(isMin ? "<" : ">") << StringView(" rhs[i] ? lhs[i] : rhs[i];\n")
               << StringView("\tmemcpy(&rv, result, sizeof(result));\n\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.sse2.packssdw.128") {
            of << StringView("\ti32 lhs[4], rhs[4]; i16 result[8];\n")
               << StringView("\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(rhs, &arg1, sizeof(rhs));\n")
               << StringView("\tfor(unsigned i = 0; i < 8; i++) { i32 value = i < 4 ? lhs[i] : rhs[i - 4]; result[i] = value > INT16_MAX ? INT16_MAX : (value < INT16_MIN ? INT16_MIN : (i16)value); }\n")
               << StringView("\tmemcpy(&rv, result, sizeof(result));\n\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.sse2.packsswb.128" || item.linkage.name == "llvm.x86.sse2.packuswb.128") {
            const bool unsignedResult = item.linkage.name == "llvm.x86.sse2.packuswb.128";
            of << StringView("\ti16 lhs[8], rhs[8]; ") << StringView(unsignedResult ? "u8" : "i8") << StringView(" result[16];\n")
               << StringView("\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(rhs, &arg1, sizeof(rhs));\n")
               << StringView("\tfor(unsigned i = 0; i < 16; i++) { i16 value = i < 8 ? lhs[i] : rhs[i - 8]; ");
            if (unsignedResult) {
                of << StringView("result[i] = value > UINT8_MAX ? UINT8_MAX : (value < 0 ? 0 : (u8)value);");
            } else {
                of << StringView("result[i] = value > INT8_MAX ? INT8_MAX : (value < INT8_MIN ? INT8_MIN : (i8)value);");
            }
            of << StringView(" }\n\tmemcpy(&rv, result, sizeof(result));\n\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.sse2.psll.w" || item.linkage.name == "llvm.x86.sse2.psll.d" || item.linkage.name == "llvm.x86.sse2.psll.q" || item.linkage.name == "llvm.x86.sse2.psrl.w" || item.linkage.name == "llvm.x86.sse2.psrl.d" || item.linkage.name == "llvm.x86.sse2.psrl.q" || item.linkage.name == "llvm.x86.sse2.psra.w" || item.linkage.name == "llvm.x86.sse2.psra.d") {
            const bool left = item.linkage.name.compare(14, 4, "psll") == 0;
            const bool arithmetic = item.linkage.name.compare(14, 4, "psra") == 0;
            const unsigned bits = item.linkage.name.back() == 'w' ? 16 : (item.linkage.name.back() == 'd' ? 32 : 64);
            of << StringView("\tu64 count_words[2]; memcpy(count_words, &arg1, sizeof(count_words)); u64 count = count_words[0];\n")
               << StringView("\tu") << bits << StringView(" input[") << 128 / bits << StringView("], result[") << 128 / bits << StringView("]; memcpy(input, &arg0, sizeof(input));\n")
               << StringView("\tfor(unsigned i = 0; i < ") << 128 / bits << StringView("; i++) {\n");
            if (arithmetic) {
                of << StringView("\t\tif(count >= ") << bits << StringView(") result[i] = input[i] >> ") << bits - 1 << StringView(" ? UINT") << bits << StringView("_MAX : 0;\n")
                   << StringView("\t\telse if(count == 0) result[i] = input[i];\n")
                   << StringView("\t\telse { result[i] = input[i] >> count; if(input[i] >> ") << bits - 1 << StringView(") result[i] |= UINT") << bits << StringView("_MAX << (") << bits << StringView(" - count); }\n");
            } else {
                of << StringView("\t\tresult[i] = count >= ") << bits << StringView(" ? 0 : input[i] ") << StringView(left ? "<<" : ">>") << StringView(" count;\n");
            }
            of << StringView("\t}\n\tmemcpy(&rv, result, sizeof(result));\n\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.sse41.dpps") {
            of << StringView("\tfloat lhs[4], rhs[4], product[4] = {0, 0, 0, 0}, result[4];\n")
               << StringView("\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(rhs, &arg1, sizeof(rhs));\n")
               << StringView("\tfor(unsigned i = 0; i < 4; i++) if(arg2 & (1 << (i + 4))) product[i] = lhs[i] * rhs[i];\n")
               << StringView("\tfloat sum = (product[0] + product[1]) + (product[2] + product[3]);\n")
               << StringView("\tfor(unsigned i = 0; i < 4; i++) result[i] = arg2 & (1 << i) ? sum : 0.0f;\n")
               << StringView("\tmemcpy(&rv, result, sizeof(result));\n\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.sse41.dppd") {
            of << StringView("\tdouble lhs[2], rhs[2], product[2] = {0, 0}, result[2];\n")
               << StringView("\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(rhs, &arg1, sizeof(rhs));\n")
               << StringView("\tfor(unsigned i = 0; i < 2; i++) if(arg2 & (1 << (i + 4))) product[i] = lhs[i] * rhs[i];\n")
               << StringView("\tdouble sum = product[0] + product[1];\n")
               << StringView("\tfor(unsigned i = 0; i < 2; i++) result[i] = arg2 & (1 << i) ? sum : 0.0;\n")
               << StringView("\tmemcpy(&rv, result, sizeof(result));\n\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.sse41.insertps") {
            of << StringView("\tu32 lhs[4], rhs[4], result[4];\n")
               << StringView("\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(rhs, &arg1, sizeof(rhs)); memcpy(result, lhs, sizeof(result));\n")
               << StringView("\tresult[(arg2 >> 4) & 3] = rhs[(arg2 >> 6) & 3];\n")
               << StringView("\tfor(unsigned i = 0; i < 4; i++) if(arg2 & (1 << i)) result[i] = 0;\n")
               << StringView("\tmemcpy(&rv, result, sizeof(result));\n\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.sse41.mpsadbw") {
            of << StringView("\tu8 lhs[16], rhs[16]; u16 result[8];\n")
               << StringView("\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(rhs, &arg1, sizeof(rhs));\n")
               << StringView("\tunsigned lhs_start = arg2 & 4 ? 4 : 0; unsigned rhs_start = (arg2 & 3) * 4;\n")
               << StringView("\tfor(unsigned i = 0; i < 8; i++) {\n")
               << StringView("\t\tresult[i] = 0;\n")
               << StringView("\t\tfor(unsigned j = 0; j < 4; j++) { int d = (int)lhs[lhs_start + i + j] - (int)rhs[rhs_start + j]; result[i] += d < 0 ? -d : d; }\n")
               << StringView("\t}\n\tmemcpy(&rv, result, sizeof(result));\n\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.sse41.packusdw") {
            of << StringView("\ti32 lhs[4], rhs[4]; u16 result[8];\n")
               << StringView("\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(rhs, &arg1, sizeof(rhs));\n")
               << StringView("\tfor(unsigned i = 0; i < 8; i++) { i32 value = i < 4 ? lhs[i] : rhs[i - 4]; result[i] = value > UINT16_MAX ? UINT16_MAX : (value < 0 ? 0 : (u16)value); }\n")
               << StringView("\tmemcpy(&rv, result, sizeof(result));\n\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.sse41.phminposuw") {
            of << StringView("\tu16 input[8], result[8] = {0, 0, 0, 0, 0, 0, 0, 0}; memcpy(input, &arg0, sizeof(input));\n")
               << StringView("\tresult[0] = input[0];\n")
               << StringView("\tfor(unsigned i = 1; i < 8; i++) if(input[i] < result[0]) { result[0] = input[i]; result[1] = i; }\n")
               << StringView("\tmemcpy(&rv, result, sizeof(result));\n\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.sse41.ptestz" || item.linkage.name == "llvm.x86.sse41.ptestc" || item.linkage.name == "llvm.x86.sse41.ptestnzc") {
            of << StringView("\tu64 lhs[2], rhs[2]; memcpy(lhs, &arg0, sizeof(lhs)); memcpy(rhs, &arg1, sizeof(rhs));\n")
               << StringView("\tbool intersection = (lhs[0] & rhs[0]) != 0 || (lhs[1] & rhs[1]) != 0;\n")
               << StringView("\tbool outside = (~lhs[0] & rhs[0]) != 0 || (~lhs[1] & rhs[1]) != 0;\n");
            if (item.linkage.name == "llvm.x86.sse41.ptestz") {
                of << StringView("\treturn !intersection;\n");
            } else if (item.linkage.name == "llvm.x86.sse41.ptestc") {
                of << StringView("\treturn !outside;\n");
            } else {
                of << StringView("\treturn intersection && outside;\n");
            }
        } else if (item.linkage.name == "llvm.x86.sse41.round.ps" || item.linkage.name == "llvm.x86.sse41.round.ss") {
            const bool scalar = item.linkage.name == "llvm.x86.sse41.round.ss";
            of << StringView("\tfloat input[4], result[4]; memcpy(input, &arg") << (scalar ? 1 : 0) << StringView(", sizeof(input)); memcpy(result, &arg0, sizeof(result));\n")
               << StringView("\tfor(unsigned i = 0; i < ") << (scalar ? 1 : 4) << StringView("; i++) result[i] = trustme_x86_round_f32(input[i], arg") << (scalar ? 2 : 1) << StringView(");\n")
               << StringView("\tmemcpy(&rv, result, sizeof(result));\n\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.sse41.round.pd" || item.linkage.name == "llvm.x86.sse41.round.sd") {
            const bool scalar = item.linkage.name == "llvm.x86.sse41.round.sd";
            of << StringView("\tdouble input[2], result[2]; memcpy(input, &arg") << (scalar ? 1 : 0) << StringView(", sizeof(input)); memcpy(result, &arg0, sizeof(result));\n")
               << StringView("\tfor(unsigned i = 0; i < ") << (scalar ? 1 : 2) << StringView("; i++) result[i] = trustme_x86_round_f64(input[i], arg") << (scalar ? 2 : 1) << StringView(");\n")
               << StringView("\tmemcpy(&rv, result, sizeof(result));\n\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.sse42.crc32.32.8" || item.linkage.name == "llvm.x86.sse42.crc32.32.16" || item.linkage.name == "llvm.x86.sse42.crc32.32.32" || item.linkage.name == "llvm.x86.sse42.crc32.64.64") {
            const unsigned bits = item.linkage.name == "llvm.x86.sse42.crc32.32.8" ? 8 : (item.linkage.name == "llvm.x86.sse42.crc32.32.16" ? 16 : (item.linkage.name == "llvm.x86.sse42.crc32.32.32" ? 32 : 64));
            of << StringView("\treturn trustme_x86_crc32c((u32)arg0, arg1, ") << bits << StringView(");\n");
        } else if (item.linkage.name.rfind("llvm.x86.sse42.pcmp", 0) == 0) {
            const bool explicitLengths = item.linkage.name.find("pcmpestr") != std::string::npos;
            const char* control = explicitLengths ? "arg4" : "arg2";
            if (explicitLengths) {
                of << StringView("\ttrustme_x86_pcmp_state state = trustme_x86_pcmp(&arg0, arg1, &arg2, arg3, arg4, true);\n");
            } else {
                of << StringView("\ttrustme_x86_pcmp_state state = trustme_x86_pcmp(&arg0, 0, &arg1, 0, arg2, false);\n");
            }
            if (item.linkage.name.find("pcmpestrm128") != std::string::npos || item.linkage.name.find("pcmpistrm128") != std::string::npos) {
                of << StringView("\ttrustme_x86_pcmp_mask(&rv, state, ") << control << StringView(");\n\treturn rv;\n");
            } else if (item.linkage.name.find("pcmpestri128") != std::string::npos || item.linkage.name.find("pcmpistri128") != std::string::npos) {
                of << StringView("\treturn trustme_x86_pcmp_index(state, ") << control << StringView(");\n");
            } else if (item.linkage.name.find("pcmpestria128") != std::string::npos || item.linkage.name.find("pcmpistria128") != std::string::npos) {
                of << StringView("\treturn state.mask == 0 && state.len2 == state.count;\n");
            } else if (item.linkage.name.find("pcmpestric128") != std::string::npos || item.linkage.name.find("pcmpistric128") != std::string::npos) {
                of << StringView("\treturn state.mask != 0;\n");
            } else if (item.linkage.name.find("pcmpestrio128") != std::string::npos || item.linkage.name.find("pcmpistrio128") != std::string::npos) {
                of << StringView("\treturn state.mask & 1;\n");
            } else if (item.linkage.name.find("pcmpestris128") != std::string::npos || item.linkage.name.find("pcmpistris128") != std::string::npos) {
                of << StringView("\treturn state.len1 < state.count;\n");
            } else if (item.linkage.name.find("pcmpestriz128") != std::string::npos || item.linkage.name.find("pcmpistriz128") != std::string::npos) {
                of << StringView("\treturn state.len2 < state.count;\n");
            } else {
                BUG(sp, StringView("Unknown SSE4.2 string comparison intrinsic ") << item.linkage.name);
            }
        } else if (item.linkage.name == "llvm.x86.sse3.hadd.ps" || item.linkage.name == "llvm.x86.sse3.hsub.ps") {
            const char op = item.linkage.name == "llvm.x86.sse3.hadd.ps" ? '+' : '-';
            of << StringView("\tfloat lhs[4], rhs[4], result[4];\n")
               << StringView("\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(rhs, &arg1, sizeof(rhs));\n")
               << StringView("\tfor(unsigned i = 0; i < 2; i++) { result[i] = lhs[2*i] ") << op << StringView(" lhs[2*i+1]; result[i+2] = rhs[2*i] ") << op << StringView(" rhs[2*i+1]; }\n")
               << StringView("\tmemcpy(&rv, result, sizeof(result));\n")
               << StringView("\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.sse3.hadd.pd" || item.linkage.name == "llvm.x86.sse3.hsub.pd") {
            const char op = item.linkage.name == "llvm.x86.sse3.hadd.pd" ? '+' : '-';
            of << StringView("\tdouble lhs[2], rhs[2], result[2];\n")
               << StringView("\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(rhs, &arg1, sizeof(rhs));\n")
               << StringView("\tresult[0] = lhs[0] ") << op << StringView(" lhs[1]; result[1] = rhs[0] ") << op << StringView(" rhs[1];\n")
               << StringView("\tmemcpy(&rv, result, sizeof(result));\n")
               << StringView("\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.sse3.ldu.dq") {
            of << StringView("\tmemcpy(&rv, arg0, sizeof(rv));\n")
               << StringView("\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.ssse3.phadd.d.128" || item.linkage.name == "llvm.x86.ssse3.phsub.d.128") {
            const char op = item.linkage.name == "llvm.x86.ssse3.phadd.d.128" ? '+' : '-';
            of << StringView("\tu32 lhs[4], rhs[4], result[4];\n")
               << StringView("\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(rhs, &arg1, sizeof(rhs));\n")
               << StringView("\tfor(unsigned i = 0; i < 2; i++) { result[i] = lhs[2*i] ") << op << StringView(" lhs[2*i+1]; result[i+2] = rhs[2*i] ") << op << StringView(" rhs[2*i+1]; }\n")
               << StringView("\tmemcpy(&rv, result, sizeof(result));\n")
               << StringView("\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.ssse3.phadd.w.128" || item.linkage.name == "llvm.x86.ssse3.phsub.w.128") {
            const char op = item.linkage.name == "llvm.x86.ssse3.phadd.w.128" ? '+' : '-';
            of << StringView("\tu16 lhs[8], rhs[8], result[8];\n")
               << StringView("\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(rhs, &arg1, sizeof(rhs));\n")
               << StringView("\tfor(unsigned i = 0; i < 4; i++) { result[i] = lhs[2*i] ") << op << StringView(" lhs[2*i+1]; result[i+4] = rhs[2*i] ") << op << StringView(" rhs[2*i+1]; }\n")
               << StringView("\tmemcpy(&rv, result, sizeof(result));\n")
               << StringView("\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.ssse3.phadd.sw.128" || item.linkage.name == "llvm.x86.ssse3.phsub.sw.128") {
            const char op = item.linkage.name == "llvm.x86.ssse3.phadd.sw.128" ? '+' : '-';
            of << StringView("\ti16 lhs[8], rhs[8], result[8];\n")
               << StringView("\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(rhs, &arg1, sizeof(rhs));\n")
               << StringView("\tfor(unsigned i = 0; i < 4; i++) {\n")
               << StringView("\t\ti32 a = (i32)lhs[2*i] ") << op << StringView(" lhs[2*i+1]; i32 b = (i32)rhs[2*i] ") << op << StringView(" rhs[2*i+1];\n")
               << StringView("\t\tresult[i] = (i16)(a > INT16_MAX ? INT16_MAX : (a < INT16_MIN ? INT16_MIN : a));\n")
               << StringView("\t\tresult[i+4] = (i16)(b > INT16_MAX ? INT16_MAX : (b < INT16_MIN ? INT16_MIN : b));\n")
               << StringView("\t}\n")
               << StringView("\tmemcpy(&rv, result, sizeof(result));\n")
               << StringView("\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.ssse3.pmul.hr.sw.128") {
            of << StringView("\ti16 lhs[8], rhs[8], result[8];\n")
               << StringView("\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(rhs, &arg1, sizeof(rhs));\n")
               << StringView("\tfor(unsigned i = 0; i < 8; i++) {\n")
               << StringView("\t\ti32 value = ((i32)lhs[i] * rhs[i] + 0x4000) >> 15;\n")
               << StringView("\t\tresult[i] = (i16)value;\n")
               << StringView("\t}\n")
               << StringView("\tmemcpy(&rv, result, sizeof(result));\n")
               << StringView("\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.ssse3.psign.b.128") {
            of << StringView("\tu8 lhs[16], result[16]; i8 signs[16];\n")
               << StringView("\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(signs, &arg1, sizeof(signs));\n")
               << StringView("\tfor(unsigned i = 0; i < 16; i++) result[i] = signs[i] == 0 ? 0 : (signs[i] < 0 ? 0 - lhs[i] : lhs[i]);\n")
               << StringView("\tmemcpy(&rv, result, sizeof(result));\n")
               << StringView("\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.ssse3.psign.w.128") {
            of << StringView("\tu16 lhs[8], result[8]; i16 signs[8];\n")
               << StringView("\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(signs, &arg1, sizeof(signs));\n")
               << StringView("\tfor(unsigned i = 0; i < 8; i++) result[i] = signs[i] == 0 ? 0 : (signs[i] < 0 ? 0 - lhs[i] : lhs[i]);\n")
               << StringView("\tmemcpy(&rv, result, sizeof(result));\n")
               << StringView("\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.ssse3.psign.d.128") {
            of << StringView("\tu32 lhs[4], result[4]; i32 signs[4];\n")
               << StringView("\tmemcpy(lhs, &arg0, sizeof(lhs)); memcpy(signs, &arg1, sizeof(signs));\n")
               << StringView("\tfor(unsigned i = 0; i < 4; i++) result[i] = signs[i] == 0 ? 0 : (signs[i] < 0 ? 0 - lhs[i] : lhs[i]);\n")
               << StringView("\tmemcpy(&rv, result, sizeof(result));\n")
               << StringView("\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.sse2.psrli.d") {
            of << StringView("\tconst u32* src = (const u32*)&arg0;\n")
               << StringView("\tu32* dst = (u32*)&rv;\n")
               << StringView("\tfor(int i = 0; i < ") << 128 / 32 << StringView("; i ++) dst[i] = src[i] >> arg1;\n")
               << StringView("\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.sse2.pslli.d") {
            of << StringView("\tconst u32* src = (const u32*)&arg0;\n")
               << StringView("\tu32* dst = (u32*)&rv;\n")
               << StringView("\tfor(int i = 0; i < ") << 128 / 32 << StringView("; i ++) dst[i] = src[i] << arg1;\n")
               << StringView("\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.sse2.pmovmskb.128") {
            of << StringView("\tconst u8* src = (const u8*)&arg0;\n")
               << StringView("\tu8* dst = (u8*)&rv; *dst = 0;\n")
               << StringView("\tfor(int i = 0; i < ") << 128 / 8 << StringView("; i ++) *dst |= (src[i] >> 7) << i;\n")
               << StringView("\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.sse2.storeu.dq") {
            of << StringView("\tmemcpy(arg0, &arg1, sizeof(arg1));\n");
        } else if (item.linkage.name == "llvm.x86.sha256rnds2") {
            of << StringView("\tconst u32* st_cdgh = (const u32*)&arg0;\n")
               << StringView("\tconst u32* st_abef = (const u32*)&arg1;\n")
               << StringView("\tconst u32* wk = (const u32*)&arg2;\n")
               << StringView("\tu32* dst = (u32*)&rv;\n")
               << StringView("\tu32 a = st_abef[3], b = st_abef[2], e = st_abef[1], f = st_abef[0];\n")
               << StringView("\tu32 c = st_cdgh[3], d = st_cdgh[2], g = st_cdgh[1], h = st_cdgh[0];\n")
               << StringView("\tfor(int i = 0; i < 2; i ++) {\n")
               << StringView("\t\tu32 ch = (e & f) ^ (~e & g);\n")
               << StringView("\t\tu32 maj = (a & b) ^ (a & c) ^ (b & c);\n")
               << StringView("\t\tu32 s0 = (a >> 2 | a << 30) ^ (a >> 13 | a << 19) ^ (a >> 22 | a << 10);\n")
               << StringView("\t\tu32 s1 = (e >> 6 | e << 26) ^ (e >> 11 | e << 21) ^ (e >> 25 | e << 7);\n")
               << StringView("\t\tu32 t = ch + s1 + wk[i] + h;\n")
               << StringView("\t\th = g; g = f; f = e; e = t + d; d = c; c = b; b = a; a = t + maj + s0;\n")
               << StringView("\t}\n")
               << StringView("\tdst[3] = a; dst[2] = b; dst[1] = e; dst[0] = f;\n")
               << StringView("\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.sha256msg1") {
            of << StringView("\tconst u32* w = (const u32*)&arg0;\n")
               << StringView("\tconst u32* w2 = (const u32*)&arg1;\n")
               << StringView("\tu32* dst = (u32*)&rv;\n")
               << StringView("\tfor(int i = 0; i < 4; i ++) {\n")
               << StringView("\t\tu32 x = (i < 3 ? w[i+1] : w2[0]);\n")
               << StringView("\t\tdst[i] = w[i] + ((x >> 7 | x << 25) ^ (x >> 18 | x << 14) ^ (x >> 3));\n")
               << StringView("\t}\n")
               << StringView("\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.sha256msg2") {
            of << StringView("\tconst u32* w = (const u32*)&arg0;\n")
               << StringView("\tconst u32* prev = (const u32*)&arg1;\n")
               << StringView("\tu32* dst = (u32*)&rv;\n")
               << StringView("\tu32 w14 = prev[2], w15 = prev[3];\n")
               << StringView("\tu32 w16 = w[0] + ((w14 >> 17 | w14 << 15) ^ (w14 >> 19 | w14 << 13) ^ (w14 >> 10));\n")
               << StringView("\tu32 w17 = w[1] + ((w15 >> 17 | w15 << 15) ^ (w15 >> 19 | w15 << 13) ^ (w15 >> 10));\n")
               << StringView("\tu32 w18 = w[2] + ((w16 >> 17 | w16 << 15) ^ (w16 >> 19 | w16 << 13) ^ (w16 >> 10));\n")
               << StringView("\tu32 w19 = w[3] + ((w17 >> 17 | w17 << 15) ^ (w17 >> 19 | w17 << 13) ^ (w17 >> 10));\n")
               << StringView("\tdst[0] = w16; dst[1] = w17; dst[2] = w18; dst[3] = w19;\n")
               << StringView("\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.bmi.bextr.32") {
            of << StringView("\tu32 start = arg1 & 0xff;\n")
               << StringView("\tu32 length = (arg1 >> 8) & 0xff;\n")
               << StringView("\tif(start >= 32 || length == 0) return 0;\n")
               << StringView("\tif(length > 32 - start) length = 32 - start;\n")
               << StringView("\treturn (arg0 >> start) & (UINT32_MAX >> (32 - length));\n");
        } else if (item.linkage.name == "llvm.x86.bmi.bextr.64") {
            of << StringView("\tu64 start = arg1 & 0xff;\n")
               << StringView("\tu64 length = (arg1 >> 8) & 0xff;\n")
               << StringView("\tif(start >= 64 || length == 0) return 0;\n")
               << StringView("\tif(length > 64 - start) length = 64 - start;\n")
               << StringView("\treturn (arg0 >> start) & (UINT64_MAX >> (64 - length));\n");
        } else if (item.linkage.name == "llvm.x86.bmi.bzhi.32") {
            of << StringView("\tu32 index = arg1 & 0xff;\n")
               << StringView("\tif(index >= 32) return arg0;\n")
               << StringView("\treturn index == 0 ? 0 : arg0 & (UINT32_MAX >> (32 - index));\n");
        } else if (item.linkage.name == "llvm.x86.bmi.bzhi.64") {
            of << StringView("\tu64 index = arg1 & 0xff;\n")
               << StringView("\tif(index >= 64) return arg0;\n")
               << StringView("\treturn index == 0 ? 0 : arg0 & (UINT64_MAX >> (64 - index));\n");
        } else if (item.linkage.name == "llvm.x86.bmi.pext.32") {
            of << StringView("\trv = 0;\n")
               << StringView("\tu32 output_bit = 1;\n")
               << StringView("\twhile(arg1) {\n")
               << StringView("\t\tu32 mask_bit = arg1 & -arg1;\n")
               << StringView("\t\tif(arg0 & mask_bit) rv |= output_bit;\n")
               << StringView("\t\targ1 &= arg1 - 1; output_bit <<= 1;\n")
               << StringView("\t}\n")
               << StringView("\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.bmi.pext.64") {
            of << StringView("\trv = 0;\n")
               << StringView("\tu64 output_bit = 1;\n")
               << StringView("\twhile(arg1) {\n")
               << StringView("\t\tu64 mask_bit = arg1 & -arg1;\n")
               << StringView("\t\tif(arg0 & mask_bit) rv |= output_bit;\n")
               << StringView("\t\targ1 &= arg1 - 1; output_bit <<= 1;\n")
               << StringView("\t}\n")
               << StringView("\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.bmi.pdep.32") {
            of << StringView("\trv = 0;\n")
               << StringView("\tu32 input_bit = 1;\n")
               << StringView("\twhile(arg1) {\n")
               << StringView("\t\tu32 mask_bit = arg1 & -arg1;\n")
               << StringView("\t\tif(arg0 & input_bit) rv |= mask_bit;\n")
               << StringView("\t\targ1 &= arg1 - 1; input_bit <<= 1;\n")
               << StringView("\t}\n")
               << StringView("\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.bmi.pdep.64") {
            of << StringView("\trv = 0;\n")
               << StringView("\tu64 input_bit = 1;\n")
               << StringView("\twhile(arg1) {\n")
               << StringView("\t\tu64 mask_bit = arg1 & -arg1;\n")
               << StringView("\t\tif(arg0 & input_bit) rv |= mask_bit;\n")
               << StringView("\t\targ1 &= arg1 - 1; input_bit <<= 1;\n")
               << StringView("\t}\n")
               << StringView("\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.pclmulqdq") {
            of << StringView("\tu64 a_words[2], b_words[2], result[2] = {0, 0};\n")
               << StringView("\tmemcpy(a_words, &arg0, sizeof(a_words));\n")
               << StringView("\tmemcpy(b_words, &arg1, sizeof(b_words));\n")
               << StringView("\tu64 a = a_words[arg2 & 1];\n")
               << StringView("\tu64 b = b_words[(arg2 >> 4) & 1];\n")
               << StringView("\tfor(unsigned i = 0; i < 64; i++) {\n")
               << StringView("\t\tif((b >> i) & 1) {\n")
               << StringView("\t\t\tresult[0] ^= a << i;\n")
               << StringView("\t\t\tif(i != 0) result[1] ^= a >> (64 - i);\n")
               << StringView("\t\t}\n")
               << StringView("\t}\n")
               << StringView("\tmemcpy(&rv, result, sizeof(result));\n")
               << StringView("\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.addcarry.32") {
            of << StringView("\trv._0 = __builtin_add_overflow(arg1, arg2, &rv._1);\n");
            of << StringView("\tif(arg0) rv._0 |= __builtin_add_overflow(rv._1, 1, &rv._1);\n");
            of << StringView("\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.addcarry.64") {
            of << StringView("\trv._0 = __builtin_add_overflow(arg1, arg2, &rv._1);\n");
            of << StringView("\tif(arg0) rv._0 |= __builtin_add_overflow(rv._1, 1, &rv._1);\n");
            of << StringView("\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.addcarryx.u32") {
            of << StringView("\trv = __builtin_add_overflow(arg1, arg2, (u32*)arg3);\n");
            of << StringView("\tif(arg0) rv |= __builtin_add_overflow(*arg3, 1, (u32*)arg3);\n");
            of << StringView("\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.addcarryx.u64") {
            of << StringView("\trv = __builtin_add_overflow(arg1, arg2, (u64*)arg3);\n");
            of << StringView("\tif(arg0) rv |= __builtin_add_overflow(*arg3, 1, (u64*)arg3);\n");
            of << StringView("\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.subborrow.32") {
            of << StringView("\trv._0 = __builtin_sub_overflow(arg1, arg2, &rv._1);\n");
            of << StringView("\tif(arg0) rv._0 |= __builtin_sub_overflow(rv._1, 1, &rv._1);\n");
            of << StringView("\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.subborrow.64") {
            of << StringView("\trv._0 = __builtin_sub_overflow(arg1, arg2, &rv._1);\n");
            of << StringView("\tif(arg0) rv._0 |= __builtin_sub_overflow(rv._1, 1, &rv._1);\n");
            of << StringView("\treturn rv;\n");
        } else if (item.linkage.name == "llvm.x86.xgetbv") {
            of << StringView("\tu32 lo, hi;\n");
            of << StringView("\t__asm__ __volatile__ (\"xgetbv\" : \"=a\" (lo), \"=d\" (hi) : \"c\" (arg0) );\n");
            of << StringView("\treturn lo | ((u64)hi << 32);\n");
        } else if (item.linkage.name == "llvm.x86.sse2.pause") {
            of << StringView("\t__asm__ __volatile__ (\"pause\");\n");

            of << StringView("\treturn ;\n");
        } else if (item.linkage.name.rfind("llvm.x86.aesni.", 0) == 0) {
            of << StringView("\tassert(!\"Unsupprorted LLVM x86 intrinsic: ") << item.linkage.name << StringView("\"); abort();\n");
        } else {
            // TODO: Hand off to compiler-specific intrinsics

            of << StringView("\tassert(!\"Extern LLVM: ") << item.linkage.name << StringView("\"); abort();\n");
        }
        of << StringView("}\n\n");
        mirRes = nullptr;
        return;
    } else if (item.linkage.name == "_Unwind_RaiseException") {
        of << StringView("static ");
        emitFunctionHeader(p, item, params);
        of << StringView(" {\n");
        of << StringView("\tthrow trustme_panic{arg0};\n");
        of << StringView("}\n\n");
        return;
    } else {
        of << StringView("extern ");
    }
    emitFunctionHeader(p, item, params);
    if (item.linkage.name != "") {
        if (TargetGetCurSpec(wb_).osName == "macos") {
            of << StringView(" asm(\"_") << item.linkage.name << StringView("\")");
        } else {
            of << StringView(" asm(\"") << item.linkage.name << StringView("\")");
        }
    }
    of << StringView(";\n\n");

    if (tracksCaller) {
        emitTrackCallerReifyWrapper(p, item, params);
    }

    mirRes = nullptr;
}

auto CodeGeneratorC::emitFunctionLinkageAlias(const HIRPath& p, const HIRFunction& item) -> void {
    if (item.linkage.name != "" && item.linkage.name != "main") {
        of << StringView("#define ") << TransMangleValue(p) << StringView(" ") << item.linkage.name << StringView("\n");
    }
}

auto CodeGeneratorC::emitFunctionDefinitionPrefix(const HIRFunction& item, bool isExternDef) -> void {
    if (isExternDef) {
        of << StringView("static ");
    }
    switch (item.linkage.type) {
        case HIRLinkage::Type::External:
        case HIRLinkage::Type::Auto:
            break;
        case HIRLinkage::Type::Weak:
            of << StringView("__attribute__((weak)) ");
            break;
        case HIRLinkage::Type::ExternWeak:
            BUG(Span(), StringView("unexpected ExternWeak on function"));
    }
}

auto CodeGeneratorC::emitFunctionProto(const HIRPath& p, const HIRFunction& item, const TransParams& params, bool isExternDef) -> void {
    MIRFunction emptyFcn;
    auto pathCallback = makeCallable<MIRPathCb>([&](auto& os) {
        os << StringView("/*proto*/ fn ") << p;
    });
    MIRTypeResolve topMirRes{sp, resolve_, pathCallback, HIRTypeRef(), {}, emptyFcn};
    mirRes = &topMirRes;

    TRACE_FUNCTION_F(p);
    emitFunctionLinkageAlias(p, item);
    emitFunctionDefinitionPrefix(item, isExternDef);
    emitFunctionHeader(p, item, params);
    of << StringView(";\n\n");

    if (crate.functionTracksCaller(sp, p, item)) {
        trackedFunctions.insert(p.clone());
        emitTrackCallerReifyWrapper(p, item, params);
    }

    mirRes = nullptr;
}

auto CodeGeneratorC::emitFunctionCode(const HIRPath& p, const HIRFunction& item, const TransParams& params, bool isExternDef, const MIRFunctionPointer& code, bool hasPrototype) -> void {
    TRACE_FUNCTION_F(p);
    const bool tracksCaller = crate.functionTracksCaller(sp, p, item);
    if (tracksCaller) {
        trackedFunctions.insert(p.clone());
    }

    MIRTypeResolve::argsT argTypes;
    for (const auto& ent : item.args) {
        argTypes.push_back(std::make_pair(HIRPattern{}, params.monomorph(resolve_, ent.second)));
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
        of << StringView("TRUSTME_BACKEND_OPTNONE ");
    }
    emitFunctionHeader(p, item, params);
    of << StringView(" {\n");
    if (item.hasNamedVariadic) {
        const auto index = item.fixedArgCount();
        of << StringView("\t");
        emitCtype(argTypes[index].second, FMT_CB(os, os << StringView("arg") << index;));
        of << StringView(";\n\tva_start(*(va_list*)&arg") << index << StringView(", ");
        size_t lastPassed = SIZE_MAX;
        for (size_t i = 0; i < item.fixedArgCount(); i++) {
            if (argumentIsPassed(item.abi, argTypes[i].second)) {
                lastPassed = i;
            }
        }
        if (lastPassed == SIZE_MAX) {
            of << '0';
        } else {
            of << StringView("arg") << lastPassed;
        }
        of << StringView(");\n");
    }
    for (unsigned int i = 0; i < item.fixedArgCount(); i++) {
        const auto& argTy = argTypes[i].second;
        if (!argumentIsPassed(item.abi, argTy)) {
            of << StringView("\t");
            emitCtype(argTy, FMT_CB(os, os << StringView("arg") << i;));
            of << StringView(" = {};\n");
        }
    }

    if (item.markings.isNaked) {
        MIR_ASSERT(localMirRes, code->locals.empty(), StringView("Naked function has MIR locals"));
        MIR_ASSERT(localMirRes, code->dropFlags.empty(), StringView("Naked function has drop flags"));
        MIR_ASSERT(localMirRes, code->blocks.size() == 1, StringView("Naked function does not have exactly one basic block"));
        const auto& block = code->blocks.front();
        const MIRStatement* nakedAsm = nullptr;
        unsigned nakedAsmIndex = 0;
        for (unsigned i = 0; i < block.statements.size(); i++) {
            const auto& statement = block.statements[i];
            if (const auto* assembly = statement.opt_Asm2()) {
                MIR_ASSERT(localMirRes, assembly->options.naked && nakedAsm == nullptr, StringView("Naked function body is not a single naked_asm statement"));
                nakedAsm = &statement;
                nakedAsmIndex = i;
            } else if (const auto* assignment = statement.opt_Assign()) {
                MIR_ASSERT(localMirRes, assignment->dst.root.is_Return() && assignment->dst.wrappers.empty() && assignment->src.is_Tuple() && assignment->src.as_Tuple().vals.empty(), StringView("Naked function contains a non-unit assignment"));
            } else {
                MIR_BUG(localMirRes, StringView("Naked function contains a non-assembly statement: ") << statement);
            }
        }
        MIR_ASSERT(localMirRes, nakedAsm != nullptr, StringView("Naked function body does not contain naked_asm"));
        MIR_ASSERT(localMirRes, block.terminator.is_Return() || block.terminator.is_Unreachable(), StringView("Naked function has a non-trivial MIR terminator"));
        localMirRes.setCurStmt(0, nakedAsmIndex);
        emitStatement(localMirRes, *nakedAsm, 1);
        of << StringView("}\n\n");
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
        of << StringView("\tu8 arg") << i << StringView("_storage[") << argSize + argAlignment - 1 << StringView("];\n\t");
        emitCtype(argTy, FMT_CB(ss, ss << StringView("&arg") << i << StringView("_aligned");));
        of << StringView(" = *(");
        emitCtype(argTy);
        of << StringView("*)trustme_align_storage(arg") << i << StringView("_storage, ") << argAlignment << StringView(");\n");
        of << StringView("\targ") << i << StringView("_aligned = arg") << i << StringView(";\n");
    }
    currentFunctionRealignsArguments = true;

    size_t returnSize = 0;
    size_t returnAlignment = 0;
    if (TargetGetSizeAndAlignOf(sp, resolve_, retType, returnSize, returnAlignment) && returnSize > 0 && returnAlignment > maxCTypeAlignment) {
        of << StringView("\tu8 rv_storage[") << returnSize + returnAlignment - 1 << StringView("];\n\t");
        emitCtype(retType, FMT_CB(ss, ss << StringView("&rv");));
        of << StringView(" = *(");
        emitCtype(retType);
        of << StringView("*)trustme_align_storage(rv_storage, ") << returnAlignment << StringView(");\n");
    } else {
        of << StringView("\t");
        emitCtype(retType, FMT_CB(ss, ss << StringView("rv");));
        of << StringView(";\n");
    }
    for (size_t i = code->locals.size(); i-- > 0;) {
        if (this->typeIsBadZst(code->locals[i])) {
            continue;
        }
        DEBUG(StringView("var") << i << StringView(" : ") << code->locals[i]);
        size_t localSize = 0;
        size_t localAlignment = 0;
        if (TargetGetSizeAndAlignOf(sp, resolve_, code->locals[i], localSize, localAlignment) && localSize > 0 && localAlignment > maxCTypeAlignment) {
            of << StringView("\tu8 var") << i << StringView("_storage[") << localSize + localAlignment - 1 << StringView("];\n\t");
            emitCtype(code->locals[i], FMT_CB(ss, ss << StringView("&var") << i;));
            of << StringView(" = *(");
            emitCtype(code->locals[i]);
            of << StringView("*)trustme_align_storage(var") << i << StringView("_storage, ") << localAlignment << StringView(");\n");
        } else {
            of << StringView("\t");
            emitCtype(code->locals[i], FMT_CB(ss, ss << StringView("var") << i;));
            of << StringView(";\n");
        }
    }
    for (unsigned int i = 0; i < code->dropFlags.size(); i++) {
        of << StringView("\tbool df") << i << StringView(" = ") << code->dropFlags[i] << StringView(";\n");
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
    std::set<unsigned> cleanupBlocks;
    while (!pendingCleanupBlocks.empty()) {
        const auto blockIndex = pendingCleanupBlocks.popBack();
        MIR_ASSERT(localMirRes, blockIndex < code->blocks.size(), StringView("Cleanup target BB") << blockIndex << StringView(" is out of range"));
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
            of << StringView("bb") << i << StringView(":\n");
        }
        for (const auto& stmt : block.statements) {
            localMirRes.setCurStmt(i, &stmt - block.statements.data());
            emitStatement(localMirRes, stmt, 1);
        }
        localMirRes.setCurStmtTerm(i);
        emitBlockTerminator(localMirRes, block.terminator, i, false, 1);
    }
    fallthroughBlock = ~0u;
    of << StringView("}\n\n");
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
        MIR_ASSERT(localMirRes, blockIndex < code.blocks.size(), StringView("Cleanup target BB") << blockIndex << StringView(" is out of range"));
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
    MIR_ASSERT(*mirRes, item.args.size() == 0 || item.args.size() == 2, StringView("`main` takes no arguments or (argc, argv), got ") << item.args.size());
    of << StringView("int main(int argc, char** argv) {\n\t");
    const bool returnsValue = retType != crate.types.unit();
    if (returnsValue) {
        of << StringView("return (int)");
    }
    of << TransMangleValue(p) << StringView("(");
    if (item.args.size() == 2) {
        of << StringView("(");
        emitCtype(params.monomorph(resolve_, item.args[0].second));
        of << StringView(")argc, (");
        emitCtype(params.monomorph(resolve_, item.args[1].second));
        of << StringView(")argv");
    }
    of << StringView(");\n");
    if (!returnsValue) {
        of << StringView("\treturn 0;\n");
    }
    of << StringView("}\n\n");
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
            of << indent << StringView("try {\n");
            emitOperation.emit(indentLevel + 1);
            of << indent << StringView("} catch (...) {\n");
            of << indent << StringView("\ttrustme_run_cleanup(") << target << StringView(");\n");
            of << indent << StringView("\tthrow;\n");
            of << indent << StringView("}\n");
            break;
        }
        case MIRUnwindAction::TAG_Terminate: {
            auto& _ = action.as_Terminate();
            of << indent << StringView("try {\n");
            emitOperation.emit(indentLevel + 1);
            of << indent << StringView("} catch (...) { abort(); }\n");
            break;
        }
        case MIRUnwindAction::TAG_Unreachable: {
            auto& _ = action.as_Unreachable();
            of << indent << StringView("try {\n");
            emitOperation.emit(indentLevel + 1);
            of << indent << StringView("} catch (...) { abort(); }\n");
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
            of << StringView("return");
        } else {
            of << StringView("return rv");
        }
    };
    auto targetFallsThrough = [&](unsigned target) {
        return !cleanup && forwardedBlockTarget(target) == fallthroughBlock && !blockIsInlinedReturn(fallthroughBlock);
    };
    auto emitTargetBodyImpl = [&](unsigned target, bool allowFallthrough) {
        if (cleanup && cleanupBlockIsNoOp(target)) {
            of << StringView("return");
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
            of << StringView("goto ") << (cleanup ? "cleanup_bb" : "bb") << target;
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
        of << StringView(";\n");
    };
    switch (term.tag()) {
        case MIRTerminator::TAG_Incomplete: {
            auto& _ = term.as_Incomplete();
            of << indent << StringView("abort();\n");
            break;
        }
        case MIRTerminator::TAG_Return: {
            auto& _ = term.as_Return();
            if (cleanup) {
                of << indent << StringView("abort();\n");
            } else {
                of << indent;
                emitReturnBody();
                of << StringView(";\n");
            }
            break;
        }
        case MIRTerminator::TAG_UnwindResume: {
            auto& _ = term.as_UnwindResume();
            if (cleanup) {
                of << indent << StringView("return;\n");
            } else {
                of << indent << StringView("abort();\n");
            }
            break;
        }
        case MIRTerminator::TAG_UnwindTerminate: {
            auto& _ = term.as_UnwindTerminate();
            of << indent << StringView("abort();\n");
            break;
        }
        case MIRTerminator::TAG_Unreachable: {
            auto& _ = term.as_Unreachable();
            of << indent << StringView("abort();\n");
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
                of << indent << StringView("if(!(");
                emitLvalue(e.cond);
                of << StringView(")) ");
                emitTargetBodyImpl(e.bbFalse, false);
                of << StringView(";\n");
            } else if (!cleanup && targetFallsThrough(e.bbFalse)) {
                of << indent << StringView("if(");
                emitLvalue(e.cond);
                of << StringView(") ");
                emitTargetBodyImpl(e.bbTrue, false);
                of << StringView(";\n");
            } else {
                of << indent << StringView("if(");
                emitLvalue(e.cond);
                of << StringView(") ");
                emitTargetBody(e.bbTrue);
                of << StringView("; else ");
                emitTargetBody(e.bbFalse);
                of << StringView(";\n");
            }
            break;
        }
        case MIRTerminator::TAG_Switch: {
            auto& e = term.as_Switch();
            if (e.validFlag != ~0u) {
                of << indent << StringView("if(!df") << e.validFlag << StringView(") ");
                emitTargetBodyImpl(e.invalidTarget, false);
                of << StringView(";\n");
            }

            MIR_ASSERT(localMirRes, !e.targets.empty(), StringView("Enum switch without variants"));
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
                of << StringView(";");
            }, oddArm);
            break;
        }
        case MIRTerminator::TAG_SwitchValue: {
            auto& e = term.as_SwitchValue();
            emitTermSwitchvalue(localMirRes, e.val, e.values, indentLevel, [&](size_t idx) {
                const auto target = idx == SIZE_MAX ? e.defTarget : e.targets[idx];
                emitTargetBody(target);
                of << StringView(";");
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
                MIR_BUG(localMirRes, StringView("Tail call in a cleanup block"));
            }
            emitTermTailCall(localMirRes, e, indentLevel);
            break;
        }
        case MIRTerminator::TAG_Asm2: {
            auto& e = term.as_Asm2();
            if (cleanup) {
                MIR_BUG(localMirRes, StringView("asm goto in a cleanup block"));
            }
            emitAsm2Gcc(localMirRes, e.options, e.lines, e.params, true, e.retBlock, indentLevel);
            break;
        }
    }
}

auto CodeGeneratorC::emitCleanupRunner(MIRTypeResolve& localMirRes, const std::set<unsigned>& cleanupBlocks) -> void {
    of << StringView("\tauto trustme_run_cleanup = [&](unsigned trustme_cleanup_entry) noexcept {\n");
    of << StringView("\t\tswitch(trustme_cleanup_entry) {\n");
    for (auto block : cleanupBlocks) {
        of << StringView("\t\tcase ") << block << StringView(": goto cleanup_bb") << block << StringView(";\n");
    }
    of << StringView("\t\tdefault: abort();\n");
    of << StringView("\t\t}\n");
    for (auto blockIndex : cleanupBlocks) {
        const auto& block = localMirRes.fcn.blocks.at(blockIndex);
        of << StringView("\tcleanup_bb") << blockIndex << StringView(":\n");
        for (const auto& stmt : block.statements) {
            localMirRes.setCurStmt(blockIndex, &stmt - block.statements.data());
            emitStatement(localMirRes, stmt, 2);
        }
        localMirRes.setCurStmtTerm(blockIndex);
        emitBlockTerminator(localMirRes, block.terminator, blockIndex, true, 2);
    }
    of << StringView("\t};\n");
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
        MIR_ASSERT(*mirRes, TargetGetSizeAndAlignOf(sp, resolve_, ty, size, align), StringView("Unexpected generic? ") << ty);
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
        MIR_ASSERT(localMirRes, TargetGetAlignOf(sp, resolve_, ty, alignment), StringView("Unknown ZST alignment"));
        of << StringView("(void*)") << alignment;
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
    if (this->isDst(ty)) {
        emitDstLvaluePointer(MIRLValue::CRef(val));
        special = true;
    } else if (val.is_Deref()) {
        emitLvalue(MIRLValue::CRef(val).innerRef());
        special = true;
    }

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
        MIR_ASSERT(localMirRes, elementTy, StringView("Index of non-array type in ZST borrow path: ") << parentTy);
        size_t elementSize = 0;
        MIR_ASSERT(localMirRes, TargetGetSizeOf(sp, resolve_, elementTy, elementSize), StringView("Unknown array element size for ") << parentTy);
        MIR_ASSERT(localMirRes, elementSize == 0, StringView("Non-ZST element in ZST borrow path: ") << elementTy);
        if (parentTy->is_Slice()) {
            of << StringView("(void*)");
            emitDstLvaluePointer(inner);
            of << StringView(".PTR");
        } else {
            of << StringView("(void*)& ");
            emitLvalue(inner);
        }
        special = true;
    }

    auto zstField = MIRLValue::CRef(val);
    while (zstField.is_Downcast()) {
        zstField.tryUnwrap();
    }
    if (!special && options.disallowEmptyStructs && zstField.is_Field() && this->typeIsBadZst(ty)) {
        auto valFp = zstField;
        BUG_ASSERT(valFp.is_Field());
        while (valFp.innerRef().is_Field()) {
            HIRTypeRef tmp;
            const auto& ty = localMirRes.getLvalueType(tmp, valFp.innerRef());
            if (!this->typeIsBadZst(ty)) {
                break;
            }
            valFp.tryUnwrap();
        }
        BUG_ASSERT(valFp.is_Field());

        auto fieldInner = valFp.innerRef();
        if (fieldInner.is_Downcast()) {
            of << StringView("(void*)& ");
            emitLvalue(fieldInner.innerRef());
        } else if (valFp.as_Field() == 0) {
            HIRTypeRef tmp;
            const auto& parentTy = localMirRes.getLvalueType(tmp, fieldInner);
            if (parentTy->is_Slice()) {
                of << StringView("(void*)");
                emitDstLvaluePointer(fieldInner);
                of << StringView(".PTR");
            } else {
                of << StringView("(void*)& ");
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
                MIR_ASSERT(localMirRes, TargetGetSizeOf(sp, resolve_, elementTy, elementSize), StringView("Unknown array element size for ") << parentTy);
                MIR_ASSERT(localMirRes, elementSize == 0, StringView("Non-ZST element in ZST borrow path: ") << elementTy);
                of << StringView("(void*)( (u8*)");
                if (parentTy->is_Slice()) {
                    emitDstLvaluePointer(fieldInner);
                    of << StringView(".PTR");
                } else {
                    of << StringView("& ");
                    emitLvalue(fieldInner);
                }
                of << StringView(" + ") << elementSize * valFp.as_Field() << StringView(")");
            } else {
                auto* repr = TargetGetTypeRepr(sp, resolve_, parentTy);
                BUG_ASSERT(repr);
                size_t nParentFields = repr->fields.size();
                auto tmpLv = MIRLValue::newField(fieldInner.clone(), valFp.as_Field() + 1);
                bool found = false;
                while (tmpLv.as_Field() < nParentFields) {
                    auto idx = tmpLv.as_Field();
                    const auto& ty = repr->fields[idx].ty;
                    if (ty->is_Path() && ty->as_Path().binding.is_ExternType()) {
                    } else if (this->typeIsBadZst(ty)) {
                    } else {
                        found = true;
                        break;
                    }
                    tmpLv.wrappers.back() = MIRLValue::Wrapper::newField(idx + 1);
                }

                if (!found) {
                    of << StringView("(void*)( (u8*)& ");
                    emitLvalue(fieldInner);
                    of << StringView(" + ") << repr->fields[valFp.as_Field()].offset << StringView(")");
                } else {
                    of << StringView("(void*)( &");
                    emitLvalue(tmpLv);
                    of << StringView(")");
                }
            }
        }
        special = true;
    }

    if (!special) {
        of << StringView("& ");
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

            if (vals[j].is_LValue() && resolve_.isTypePhantomData(ty)) {
                continue;
            }

            if (this->typeIsBadZst(ty)) {
                continue;
            }
        }

        if (hasEmitted) {
            of << StringView(";\n") << indent;
        }
        hasEmitted = true;

        emitSlot.emit();
        of << StringView("._") << j << StringView(" = ");
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
        of << indent << StringView("if( df") << e.flagIdx << StringView(" ) {\n");
    }
    switch (e.kind) {
        case MIRDropKind::SHALLOW:
            if (const auto* ity = resolve_.isTypeOwnedBox(ty)) {
                emitBoxDrop(indentLevel + (e.flagIdx != ~0u ? 1 : 0), ity, ty, e.slot, false);
            } else {
                MIR_BUG(localMirRes, StringView("Shallow drop on non-Box - ") << ty);
            }
            break;
        case MIRDropKind::DEEP:
            emitDestructorCall(e.slot, ty, true, indentLevel + (e.flagIdx != ~0u ? 1 : 0));
            break;
    }
    if (e.flagIdx != ~0u) {
        of << indent << StringView("}\n");
    }
}

auto CodeGeneratorC::emitStatement(const MIRTypeResolve& localMirRes, const MIRStatement& stmt, unsigned indentLevel) -> void {
    DEBUG(stmt);
    auto indent = RepeatLitStr{"\t", static_cast<int>(indentLevel)};
    switch (stmt.tag()) {
        case MIRStatement::TAG_ScopeEnd:
            break;
        case MIRStatement::TAG_SetDropFlag: {
            const auto& e = stmt.as_SetDropFlag();
            of << indent << StringView("df") << e.idx << StringView(" = ");
            if (e.other == ~0u) {
                of << e.newVal;
            } else {
                of << (e.newVal ? "!" : "") << StringView("df") << e.other;
            }
            of << StringView(";\n");
            break;
        } break;
        case MIRStatement::TAG_SaveDropFlag: {
            auto& e = stmt.as_SaveDropFlag();
            of << indent << StringView("if(df") << e.idx << StringView(") { ");
            emitLvalue(e.slot);
            of << StringView(".DATA[") << (e.bitIndex / 8) << StringView("] |= (1 << ") << (e.bitIndex % 8) << StringView(");");
            of << StringView(" } else { ");
            emitLvalue(e.slot);
            of << StringView(".DATA[") << (e.bitIndex / 8) << StringView("] &= ~(1 << ") << (e.bitIndex % 8) << StringView(");");
            of << StringView(" }\n");
        } break;
            break;
        case MIRStatement::TAG_LoadDropFlag: {
            auto& e = stmt.as_LoadDropFlag();
            of << indent << StringView("df") << e.idx << StringView(" = ((");
            emitLvalue(e.slot);
            of << StringView(".DATA[") << (e.bitIndex / 8) << StringView("] & (1 << ") << (e.bitIndex % 8) << StringView(")) != 0)");
            of << StringView(";\n");
        } break;
        case MIRStatement::TAG_Asm:
            this->emitAsmGcc(localMirRes, stmt.as_Asm(), indentLevel);
            break;
        case MIRStatement::TAG_Asm2:
            this->emitAsm2Gcc(localMirRes, stmt, indentLevel);
            break;
        case MIRStatement::TAG_Assign: {
            const auto& e = stmt.as_Assign();

            DEBUG(StringView("- ") << e.dst << StringView(" = ") << e.src);
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
                        of << StringView("abort()");
                        break;
                    }

                    if (ve.is_Field() && this->typeIsBadZst(ty)) {
                        break;
                    }

                    emitLvalue(e.dst);
                    of << StringView(" = ");
                    emitLvalue(ve);
                    break;
                }
                case MIRRValue::TAG_Constant: {
                    auto& ve = e.src.as_Constant();
                    emitLvalue(e.dst);
                    of << StringView(" = (");
                    emitCtype(ty);
                    of << StringView(")");
                    emitConstant(ve, &e.dst);
                    break;
                }
                case MIRRValue::TAG_SizedArray: {
                    auto& ve = e.src.as_SizedArray();
                    if (ve.count == 0) {
                    } else if (ve.count == 1) {
                        emitLvalue(e.dst);
                        of << StringView(".DATA[0] = ");
                        emitParam(ve.val);
                    } else if (ve.count == 2) {
                        emitLvalue(e.dst);
                        of << StringView(".DATA[0] = ");
                        emitParam(ve.val);
                        of << StringView(";\n") << indent;
                        emitLvalue(e.dst);
                        of << StringView(".DATA[1] = ");
                        emitParam(ve.val);
                    } else if (ve.count == 3) {
                        emitLvalue(e.dst);
                        of << StringView(".DATA[0] = ");
                        emitParam(ve.val);
                        of << StringView(";\n") << indent;
                        emitLvalue(e.dst);
                        of << StringView(".DATA[1] = ");
                        emitParam(ve.val);
                        of << StringView(";\n") << indent;
                        emitLvalue(e.dst);
                        of << StringView(".DATA[2] = ");
                        emitParam(ve.val);
                    } else {
                        of << StringView("for(unsigned int i = 0; i < ") << ve.count << StringView("; i ++)\n");
                        of << indent << StringView("\t");
                        emitLvalue(e.dst);
                        of << StringView(".DATA[i] = ");
                        emitParam(ve.val);
                    }
                    break;
                }
                case MIRRValue::TAG_Borrow: {
                    auto& ve = e.src.as_Borrow();
                    emitLvalue(e.dst);
                    MIR_ASSERT(localMirRes, ty->is_Borrow() || ty->is_Pointer(), StringView("Borrow rvalue has non-pointer result type ") << ty);
                    of << StringView(" = (");
                    emitCtype(ty);
                    of << StringView(")");
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
                    of << StringView(" = ");
                    HIRTypeRef tmp, tmpR;
                    const auto& ty = localMirRes.getParamType(tmp, ve.valL);
                    const auto& tyR = localMirRes.getParamType(tmpR, ve.valR);
                    if (ty->is_Borrow()) {
                        of << StringView("(slice_cmp(");
                        emitParam(ve.valL);
                        of << StringView(", ");
                        emitParam(ve.valR);
                        of << StringView(")");
                        switch (ve.op) {
                            case MIRBinOp::EQ:
                                of << StringView(" == 0");
                                break;
                            case MIRBinOp::NE:
                                of << StringView(" != 0");
                                break;
                            case MIRBinOp::GT:
                                of << StringView(" >  0");
                                break;
                            case MIRBinOp::GE:
                                of << StringView(" >= 0");
                                break;
                            case MIRBinOp::LT:
                                of << StringView(" <  0");
                                break;
                            case MIRBinOp::LE:
                                of << StringView(" <= 0");
                                break;
                            default:
                                MIR_BUG(localMirRes, StringView("Unknown comparison of a &-ptr - ") << e.src << StringView(" with ") << ty);
                        }
                        of << StringView(")");
                        break;
                    } else if (const auto* te = ty->opt_Pointer()) {
                        if (isDst(te->inner)) {
                            of << StringView("(raw_fat_ptr_cmp((uintptr_t)");
                            emitParam(ve.valL);
                            of << StringView(".PTR, (uintptr_t)");
                            emitParam(ve.valL);
                            of << StringView(".META, (uintptr_t)");
                            emitParam(ve.valR);
                            of << StringView(".PTR, (uintptr_t)");
                            emitParam(ve.valR);
                            of << StringView(".META)");
                            switch (ve.op) {
                                case MIRBinOp::EQ:
                                    of << StringView(" == 0");
                                    break;
                                case MIRBinOp::NE:
                                    of << StringView(" != 0");
                                    break;
                                case MIRBinOp::GT:
                                    of << StringView(" > 0");
                                    break;
                                case MIRBinOp::GE:
                                    of << StringView(" >= 0");
                                    break;
                                case MIRBinOp::LT:
                                    of << StringView(" < 0");
                                    break;
                                case MIRBinOp::LE:
                                    of << StringView(" <= 0");
                                    break;
                                default:
                                    MIR_BUG(localMirRes, StringView("Unknown comparison of a *-ptr - ") << e.src << StringView(" with ") << ty);
                            }
                            of << StringView(")");
                        } else {
                            const bool ordering = ve.op == MIRBinOp::GT || ve.op == MIRBinOp::GE || ve.op == MIRBinOp::LT || ve.op == MIRBinOp::LE;
                            if (ordering) {
                                of << StringView("(uintptr_t)");
                            }
                            emitParam(ve.valL);
                            switch (ve.op) {
                                case MIRBinOp::EQ:
                                    of << StringView(" == ");
                                    break;
                                case MIRBinOp::NE:
                                    of << StringView(" != ");
                                    break;
                                case MIRBinOp::GT:
                                    of << StringView(" > ");
                                    break;
                                case MIRBinOp::GE:
                                    of << StringView(" >= ");
                                    break;
                                case MIRBinOp::LT:
                                    of << StringView(" < ");
                                    break;
                                case MIRBinOp::LE:
                                    of << StringView(" <= ");
                                    break;
                                default:
                                    MIR_BUG(localMirRes, StringView("Unknown comparison of a *-ptr - ") << e.src << StringView(" with ") << ty);
                            }
                            if (ordering) {
                                of << StringView("(uintptr_t)");
                            }
                            emitParam(ve.valR);
                        }
                        break;
                    } else if (ve.op == MIRBinOp::MOD && (ty == HIRCoreType::F16 || ty == HIRCoreType::F32 || ty == HIRCoreType::F64)) {
                        of << StringView("__builtin_");
                        if (ty == HIRCoreType::F64) {
                            of << StringView("fmod");
                        } else {
                            of << StringView("fmodf");
                        }
                        of << StringView("(");
                        emitParam(ve.valL);
                        of << StringView(", ");
                        emitParam(ve.valR);
                        of << StringView(")");
                        break;
                    } else if (ty == HIRCoreType::F128) {
                        switch (ve.op) {
                            case MIRBinOp::ADD:
                                of << StringView("f128_add");
                                break;
                            case MIRBinOp::SUB:
                                of << StringView("f128_sub");
                                break;
                            case MIRBinOp::MUL:
                                of << StringView("f128_mul");
                                break;
                            case MIRBinOp::DIV:
                                of << StringView("f128_div");
                                break;
                            case MIRBinOp::MOD:
                                of << StringView("f128_mod");
                                break;
                            case MIRBinOp::EQ:
                                of << StringView("f128_eq");
                                break;
                            case MIRBinOp::NE:
                                of << StringView("f128_ne");
                                break;
                            case MIRBinOp::GT:
                                of << StringView("f128_gt");
                                break;
                            case MIRBinOp::GE:
                                of << StringView("f128_ge");
                                break;
                            case MIRBinOp::LT:
                                of << StringView("f128_lt");
                                break;
                            case MIRBinOp::LE:
                                of << StringView("f128_le");
                                break;
                            default:
                                MIR_TODO(localMirRes, StringView("unsupported f128 binop"));
                        }
                        of << StringView("(");
                        emitParam(ve.valL);
                        of << StringView(", ");
                        emitParam(ve.valR);
                        of << StringView(")");
                        break;
                    } else if (typeIsEmulatedI128(ty)) {
                        switch (ve.op) {
                            case MIRBinOp::ADD:
                                of << StringView("add128");
                                if (0) {
                                    case MIRBinOp::SUB:
                                        of << StringView("sub128");
                                }
                                if (0) {
                                    case MIRBinOp::MUL:
                                        of << StringView("mul128");
                                }
                                if (0) {
                                    case MIRBinOp::DIV:
                                        of << StringView("div128");
                                }
                                if (0) {
                                    case MIRBinOp::MOD:
                                        of << StringView("mod128");
                                }
                                if (0) {
                                    case MIRBinOp::BIT_OR:
                                        of << StringView("or128");
                                }
                                if (0) {
                                    case MIRBinOp::BIT_AND:
                                        of << StringView("and128");
                                }
                                if (0) {
                                    case MIRBinOp::BIT_XOR:
                                        of << StringView("xor128");
                                }
                                if (ty == HIRCoreType::I128) {
                                    of << StringView("s");
                                }
                                of << StringView("(");
                                emitParam(ve.valL);
                                of << StringView(", ");
                                emitParam(ve.valR);
                                of << StringView(")");
                                break;
                            case MIRBinOp::BIT_SHR:
                                of << StringView("shr128");
                                if (0) {
                                    case MIRBinOp::BIT_SHL:
                                        of << StringView("shl128");
                                }
                                if (ty == HIRCoreType::I128) {
                                    of << StringView("s");
                                }
                                of << StringView("(");
                                emitParam(ve.valL);
                                of << StringView(", ");
                                emitParam(ve.valR);
                                if ((tyR == HIRCoreType::I128 || tyR == HIRCoreType::U128)) {
                                    of << StringView(".lo");
                                }
                                of << StringView(")");
                                break;

                            case MIRBinOp::EQ:
                                of << StringView("0 == ");
                                if (0) {
                                    case MIRBinOp::NE:
                                        of << StringView("0 != ");
                                }
                                if (0) {
                                    case MIRBinOp::GT:
                                        of << StringView("0 > ");
                                }
                                if (0) {
                                    case MIRBinOp::GE:
                                        of << StringView("0 >= ");
                                }
                                if (0) {
                                    case MIRBinOp::LT:
                                        of << StringView("0 < ");
                                }
                                if (0) {
                                    case MIRBinOp::LE:
                                        of << StringView("0 <= ");
                                }
                                of << StringView("cmp128");
                                if (ty == HIRCoreType::I128) {
                                    of << StringView("s");
                                }
                                of << StringView("(");
                                emitParam(ve.valR);
                                of << StringView(", ");
                                emitParam(ve.valL);
                                of << StringView(")");
                                break;

                            case MIRBinOp::ADD_OV:
                            case MIRBinOp::SUB_OV:
                            case MIRBinOp::MUL_OV:
                            case MIRBinOp::DIV_OV:
                                MIR_TODO(localMirRes, StringView("Overflowing binops for emulated i128"));
                                break;
                        }
                        break;
                    } else {
                    }

                    emitParam(ve.valL);
                    switch (ve.op) {
                        case MIRBinOp::ADD:
                            of << StringView(" + ");
                            break;
                        case MIRBinOp::SUB:
                            of << StringView(" - ");
                            break;
                        case MIRBinOp::MUL:
                            of << StringView(" * ");
                            break;
                        case MIRBinOp::DIV:
                            of << StringView(" / ");
                            break;
                        case MIRBinOp::MOD:
                            of << StringView(" % ");
                            break;

                        case MIRBinOp::BIT_OR:
                            of << StringView(" | ");
                            break;
                        case MIRBinOp::BIT_AND:
                            of << StringView(" & ");
                            break;
                        case MIRBinOp::BIT_XOR:
                            of << StringView(" ^ ");
                            break;
                        case MIRBinOp::BIT_SHR:
                            of << StringView(" >> ");
                            break;
                        case MIRBinOp::BIT_SHL:
                            of << StringView(" << ");
                            break;
                        case MIRBinOp::EQ:
                            of << StringView(" == ");
                            break;
                        case MIRBinOp::NE:
                            of << StringView(" != ");
                            break;
                        case MIRBinOp::GT:
                            of << StringView(" > ");
                            break;
                        case MIRBinOp::GE:
                            of << StringView(" >= ");
                            break;
                        case MIRBinOp::LT:
                            of << StringView(" < ");
                            break;
                        case MIRBinOp::LE:
                            of << StringView(" <= ");
                            break;

                        case MIRBinOp::ADD_OV:
                        case MIRBinOp::SUB_OV:
                        case MIRBinOp::MUL_OV:
                        case MIRBinOp::DIV_OV:
                            MIR_TODO(localMirRes, StringView("Overflow"));
                            break;
                    }
                    emitParam(ve.valR);
                    if (typeIsEmulatedI128(tyR)) {
                        of << StringView(".lo");
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
                                of << StringView(" = neg128s(");
                                emitLvalue(ve.val);
                                of << StringView(")");
                                break;
                            case MIRUniOp::INV:
                                emitLvalue(e.dst);
                                of << StringView(".lo = ~");
                                emitLvalue(ve.val);
                                of << StringView(".lo; ");
                                emitLvalue(e.dst);
                                of << StringView(".hi = ~");
                                emitLvalue(ve.val);
                                of << StringView(".hi");
                                break;
                        }
                        break;
                    } else if (ty == HIRCoreType::F128) {
                        switch (ve.op) {
                            case MIRUniOp::NEG:
                                emitLvalue(e.dst);
                                of << StringView(" = f128_neg(");
                                emitLvalue(ve.val);
                                of << StringView(")");
                                break;
                            case MIRUniOp::INV:
                                MIR_TODO(*mirRes, StringView("f128 INV"));
                                break;
                        }
                        break;
                    }

                    emitLvalue(e.dst);
                    of << StringView(" = ");
                    switch (ve.op) {
                        case MIRUniOp::NEG:
                            of << StringView("-");
                            break;
                        case MIRUniOp::INV:
                            if (ty == HIRCoreType::Bool) {
                                of << StringView("!");
                            } else {
                                of << StringView("~");
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
                        of << StringView("._0._0");
                    }
                    of << StringView(" = (decltype(");
                    emitLvalue(e.dst);
                    if (ty->is_Primitive() || ty->is_Pointer() || ty->is_Borrow()) {
                    } else {
                        of << StringView("._0._0");
                    }
                    of << StringView("))");
                    emitLvalue(ve.val);
                    of << StringView(".META");
                    break;
                }
                case MIRRValue::TAG_DstPtr: {
                    auto& ve = e.src.as_DstPtr();
                    emitLvalue(e.dst);
                    of << StringView(" = (");
                    emitCtype(ty);
                    of << StringView(")");
                    emitLvalue(ve.val);
                    of << StringView(".PTR");
                    break;
                }
                case MIRRValue::TAG_MakeDst: {
                    auto& ve = e.src.as_MakeDst();
                    emitLvalue(e.dst);
                    of << StringView(" = (");
                    emitCtype(ty);
                    of << StringView(")");
                    auto meta = metadataType(ty->is_Pointer() ? ty->as_Pointer().inner : ty->as_Borrow().inner);
                    switch (meta) {
                        case MetadataType::Slice:
                            of << StringView("make_sliceptr");
                            of << StringView("(");
                            emitParam(ve.ptrVal, false);
                            of << StringView(", ");
                            emitParam(ve.metaVal);
                            of << StringView(")");
                            break;
                        case MetadataType::TraitObject:
                            of << StringView("make_traitobjptr");
                            of << StringView("(");
                            emitParam(ve.ptrVal);
                            of << StringView(", ");
                            emitTraitMetadataParam(localMirRes, ve.metaVal);
                            of << StringView(")");
                            break;
                        case MetadataType::Zero:
                        case MetadataType::Unknown:
                        case MetadataType::None:
                            of << StringView("(void*)");
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
                            of << StringView(";\n") << indent;
                        }
                        emitLvalue(e.dst);
                        of << StringView(".DATA[") << j << StringView("] = ");
                        emitParam(ve.vals[j]);
                    }
                    break;
                }
                case MIRRValue::TAG_UnionVariant: {
                    auto& ve = e.src.as_UnionVariant();
                    MIR_ASSERT(localMirRes, crate.getTypeitemByPath(sp, ve.path.path).is_Union(), StringView(""));
                    if (!this->typeIsBadZst(mirRes->getParamType(tmp, ve.val))) {
                        emitLvalue(e.dst);
                        of << StringView(".var_") << ve.index << StringView(" = ");
                        emitParam(ve.val);
                    }
                    break;
                }
                case MIRRValue::TAG_EnumVariant: {
                    auto& ve = e.src.as_EnumVariant();
                    const auto& tyi = crate.getTypeitemByPath(sp, ve.path.path);
                    MIR_ASSERT(localMirRes, tyi.is_Enum(), StringView(""));
                    const auto* enmP = &tyi.as_Enum();

                    HIRTypeRef tmp;
                    const auto& ty = localMirRes.getLvalueType(tmp, e.dst);
                    auto* repr = TargetGetTypeRepr(sp, resolve_, ty);

                    switch (repr->variants.tag()) {
                        case TypeReprVariantMode::TAG_None: {
                            if (enumIsTagless(repr)) {
                                break;
                            }
                            emitCompositeAssign(localMirRes, [&]() {
                                emitLvalue(e.dst);
                                of << StringView(".DATA.var_0");
                            }, /*repr->fields[0].ty,*/ ve.vals, indentLevel);
                            break;
                        }
                        case TypeReprVariantMode::TAG_NonZero: {
                            auto& re = repr->variants.as_NonZero();
                            MIR_ASSERT(*mirRes, ve.index < 2, StringView(""));
                            if (ve.index == re.zeroVariant) {
                                // TODO: Use nonzero_path
                                of << StringView("memset(&");
                                emitLvalue(e.dst);
                                of << StringView(", 0, sizeof(");
                                emitCtype(ty);
                                of << StringView("))");
                            } else {
                                emitCompositeAssign(localMirRes, [&]() {
                                    emitLvalue(e.dst);
                                    of << StringView(".DATA.var_") << ve.index;
                                }, /*repr->fields[0].ty,*/ ve.vals, indentLevel, /*prepend_newline=*/false);
                            }
                            break;
                        }
                        case TypeReprVariantMode::TAG_Linear: {
                            auto& re = repr->variants.as_Linear();
                            bool emitNewline = false;
                            if (!re.isNiche(ve.index)) {
                                if (re.field.subFields.empty() || typeIsBadZst(repr->fields[ve.index].ty)) {
                                    emitLvalue(e.dst);
                                    const auto& slotTy = emitEnumPath(repr, re.field);
                                    of << StringView(" = ");
                                    if (slotTy->is_Pointer() || slotTy->is_Borrow() || slotTy->is_Function()) {
                                        of << StringView("(");
                                        emitCtype(slotTy);
                                        of << StringView(")(uintptr_t)");
                                    }
                                    of << re.tagValue(ve.index);
                                } else {
                                    auto vr = TargetGetTypeRepr(sp, resolve_, repr->fields[ve.index].ty);
                                    emitLvalue(e.dst);
                                    of << StringView(".DATA.var_") << ve.index << StringView("._") << (vr->fields.size() - 1) << StringView(" = ");
                                    const auto& slotTy = vr->fields.back().ty;
                                    if (slotTy->is_Pointer() || slotTy->is_Borrow() || slotTy->is_Function()) {
                                        of << StringView("(");
                                        emitCtype(slotTy);
                                        of << StringView(")(uintptr_t)");
                                    }
                                    of << re.tagValue(ve.index);
                                }
                                emitNewline = true;
                            }
                            if (enmP->isValue()) {
                            } else {
                                emitCompositeAssign(localMirRes, [&]() {
                                    emitLvalue(e.dst);
                                    of << StringView(".DATA.var_") << ve.index;
                                }, ve.vals, indentLevel, emitNewline);
                            }
                            break;
                        }
                        case TypeReprVariantMode::TAG_Values: {
                            auto& re = repr->variants.as_Values();
                            if (re.field.index == 0) {
                                emitLvalue(e.dst);
                                of << StringView(".TAG = ");
                                emitEnumVariantVal(repr, ve.index);
                            } else {
                                emitLvalue(e.dst);
                                of << StringView(".DATA.TAG = ");
                                emitEnumVariantVal(repr, ve.index);
                            }
                            if (!enmP->isValue()) {
                                emitCompositeAssign(localMirRes, [&]() {
                                    emitLvalue(e.dst);
                                    of << StringView(".DATA.var_") << ve.index;
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
                            of << StringView("._d = 0");
                        }
                    } else {
                        emitCompositeAssign(localMirRes, [&]() {
                            emitLvalue(e.dst);
                        }, ve.vals, indentLevel, /*emit_newline=*/false);
                    }
                    break;
                }
            }
            of << StringView(";\n");
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

    if ((ve.type->is_Pointer() && isDst(ve.type->as_Pointer().inner)) || (ve.type->is_Borrow() && isDst(ve.type->as_Borrow().inner)) || ve.type == ty) {
        emitLvalue(dst);
        of << StringView(" = ");
        emitLvalue(ve.val);
        return;
    }

    if (ve.type->is_Function() && ty->is_NamedFunction()) {
        emitLvalue(dst);
        of << StringView(" = ");
        emitReifiedFunctionName(ty->as_NamedFunction().path);
        return;
    }

    if (options.emulatedI128 && (ve.type == HIRCoreType::U128 || ve.type == HIRCoreType::I128 || ty == HIRCoreType::U128 || ty == HIRCoreType::I128)) {
        MIR_ASSERT(localMirRes, ve.type->is_Primitive(), StringView("i128/u128 cast to non-primitive - ") << ve.type);
        MIR_ASSERT(localMirRes, ty->is_Primitive() || (ty->is_Path() && ty->as_Path().binding.is_Enum()), StringView("i128/u128 cast from non-primitive - ") << ty);
        switch (ve.type->as_Primitive()) {
            case HIRCoreType::I128:
            case HIRCoreType::U128:
                if (ty == HIRCoreType::I128 || ty == HIRCoreType::U128) {
                    emitLvalue(dst);
                    of << StringView(".lo = ");
                    emitLvalue(ve.val);
                    of << StringView(".lo; ");
                    emitLvalue(dst);
                    of << StringView(".hi = ");
                    emitLvalue(ve.val);
                    of << StringView(".hi");
                } else if (ty->is_Path() && ty->as_Path().binding.is_Enum()) {
                    if (enumIsTagless(TargetGetTypeRepr(sp, resolve_, ty))) {
                        emitLvalue(dst);
                        of << StringView(".lo = ");
                        emitTaglessEnumDiscriminant(ty);
                        of << StringView("; ");
                        emitLvalue(dst);
                        of << StringView(".hi = (");
                        emitTaglessEnumDiscriminant(ty);
                        of << StringView(") < 0 ? -1 : 0");
                        break;
                    }
                    emitLvalue(dst);
                    of << StringView(".lo = ");
                    emitLvalue(ve.val);
                    of << StringView(".TAG; ");
                    emitLvalue(dst);
                    of << StringView(".hi = ");
                    emitLvalue(ve.val);
                    of << StringView(".TAG < 0 ? -1 : 0");
                } else if (ty == HIRCoreType::F32 || ty == HIRCoreType::F64) {
                    emitLvalue(dst);
                    of << StringView(" = ");
                    of << StringView(ve.type == HIRCoreType::I128 ? "cast_float_to_i128(" : "cast_float_to_u128(");
                    emitLvalue(ve.val);
                    of << StringView(")");
                } else {
                    emitLvalue(dst);
                    of << StringView(".lo = ");
                    emitLvalue(ve.val);
                    of << StringView("; ");
                    emitLvalue(dst);
                    of << StringView(".hi = ");
                    emitLvalue(ve.val);
                    of << StringView(" < 0 ? -1 : 0");
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
                of << StringView(" = ");
                switch (ty->as_Primitive()) {
                    case HIRCoreType::U128:
                    case HIRCoreType::I128:
                        emitLvalue(ve.val);
                        of << StringView(".lo");
                        break;
                    default:
                        MIR_BUG(localMirRes, StringView("Unreachable"));
                }
                break;
            case HIRCoreType::F16:
                MIR_TODO(localMirRes, StringView("f16 from i128/u128"));
            case HIRCoreType::F32:
                emitLvalue(dst);
                of << StringView(" = ");
                switch (ty->as_Primitive()) {
                    case HIRCoreType::U128:
                        of << StringView("cast128_float(");
                        emitLvalue(ve.val);
                        of << StringView(")");
                        break;
                    case HIRCoreType::I128:
                        of << StringView("cast128s_float(");
                        emitLvalue(ve.val);
                        of << StringView(")");
                        break;
                    default:
                        MIR_BUG(localMirRes, StringView("Unreachable"));
                }
                break;
            case HIRCoreType::F64:
                emitLvalue(dst);
                of << StringView(" = ");
                switch (ty->as_Primitive()) {
                    case HIRCoreType::U128:
                        of << StringView("cast128_double(");
                        emitLvalue(ve.val);
                        of << StringView(")");
                        break;
                    case HIRCoreType::I128:
                        of << StringView("cast128s_double(");
                        emitLvalue(ve.val);
                        of << StringView(")");
                        break;
                    default:
                        MIR_BUG(localMirRes, StringView("Unreachable"));
                }
                break;
            case HIRCoreType::F128:
                MIR_TODO(localMirRes, StringView("f128 from i128/u128"));
            default:
                MIR_BUG(localMirRes, StringView("Bad i128/u128 cast - ") << ty << StringView(" to ") << ve.type);
        }
        return;
    }
    if (ve.type == HIRCoreType::F128) {
        emitLvalue(dst);
        of << StringView(" = f128_encode((f128_native)");
        emitLvalue(ve.val);
        of << StringView(")");
        return;
    }
    if (ty == HIRCoreType::F128) {
        emitLvalue(dst);
        of << StringView(" = (");
        emitCtype(ve.type);
        of << StringView(")f128_decode(");
        emitLvalue(ve.val);
        of << StringView(")");
        return;
    }

    HIRTypeRef dstTmp;
    const auto& dstTy = localMirRes.getLvalueType(dstTmp, dst);
    const auto* dstPrimitive = ve.type->opt_Primitive();
    if (dstPrimitive && isInteger(*dstPrimitive) && (ty->is_NamedFunction() || ty->is_Function() || ty->is_Pointer())) {
        emitLvalue(dst);
        of << StringView(" = (");
        emitCtype(dstTy);
        of << StringView(")(uintptr_t)");
        if (ty->is_NamedFunction()) {
            emitReifiedFunctionName(ty->as_NamedFunction().path);
        } else {
            emitLvalue(ve.val);
        }
        return;
    }

    emitLvalue(dst);
    of << StringView(" = ");
    of << StringView("(");
    emitCtype(dstTy);
    of << StringView(")");
    // TODO: If the source is an unsized borrow, then extract the pointer
    bool special = false;
    if (ve.type->is_Pointer() && !isDst(ve.type->as_Pointer().inner)) {
        if ((ty->is_Borrow() && isDst(ty->as_Borrow().inner)) || (ty->is_Pointer() && isDst(ty->as_Pointer().inner))) {
            emitLvalue(ve.val);
            of << StringView(".PTR");
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
            of << StringView(".TAG");
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
    MIR_ASSERT(localMirRes, ty->is_Path(), StringView("Switch over non-Path type"));
    MIR_ASSERT(localMirRes, ty->as_Path().binding.is_Enum(), StringView("Switch over non-enum"));
    const auto* repr = TargetGetTypeRepr(localMirRes.sp, resolve_, ty);
    MIR_ASSERT(localMirRes, repr, StringView("No repr for ") << ty);

    struct MaybeSigned64 {
        bool is_signed;
        u64 v;

        MaybeSigned64(bool is_signed, u64 v)
            : is_signed(is_signed)
            , v(v)
        {
        }

        void fmt(ZeroCopyOutput& os) const {
            if (is_signed) {
                os << static_cast<i64>(v);
            } else {
                os << v;
            }
        }
    };

    switch (repr->variants.tag()) {
        case TypeReprVariantMode::TAG_NonZero: {
            auto& e = repr->variants.as_NonZero();
            MIR_ASSERT(localMirRes, nArms == 2, StringView("NonZero optimised switch without two arms"));
            of << indent << StringView("if( ");
            emitLvalue(val);
            const auto& slotTy = emitEnumPath(repr, e.field);
            MIR_ASSERT(localMirRes, slotTy->is_Pointer() || slotTy->is_Function() || slotTy->is_Borrow() || slotTy->is_Primitive(), StringView("Invalid niche type: ") << slotTy << StringView(" in ") << ty);
            if (typeIsEmulatedI128(slotTy)) {
                of << StringView(".lo != 0 || ");
                emitLvalue(val);
                emitEnumPath(repr, e.field);
                of << StringView(".hi");
            }
            of << StringView(" != 0 )\n");
            of << indent << StringView("\t");
            cb.emit(1 - e.zeroVariant);
            of << StringView("\n");
            of << indent << StringView("else\n");
            of << indent << StringView("\t");
            cb.emit(e.zeroVariant);
            of << StringView("\n");
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
                        MIR_BUG(localMirRes, StringView("Invalid tag type?! ") << tagTy);
                }
            }

            auto emitVariant = [&]() {
                if (pointerTag) {
                    of << StringView("(uintptr_t)");
                } else {
                    of << StringView("(") << tagUnsignedType(e.field.size) << StringView(")");
                }
                emitLvalue(val);
                emitEnumPath(repr, e.field);
            };
            auto tagOf = [&](size_t varIdx) {
                return tagBits(e.field.size, e.tagValue(varIdx));
            };

            if (oddArm != static_cast<size_t>(-1)) {
                of << indent << StringView("if( ");
                if (e.isNiche(oddArm)) {
                    bool firstComparison = true;
                    for (size_t j = 0; j < nArms; j++) {
                        if (j == oddArm || e.isNiche(j)) {
                            continue;
                        }
                        if (!firstComparison) {
                            of << StringView(" && ");
                        }
                        emitVariant();
                        of << StringView(" != ") << tagOf(j) << StringView("ull");
                        firstComparison = false;
                    }
                    MIR_ASSERT(localMirRes, !firstComparison, StringView("Niche switch without explicit tag values"));
                } else {
                    emitVariant();
                    of << StringView(" == ") << tagOf(oddArm) << StringView("ull");
                }
                of << StringView(") {");
                cb.emit(oddArm);
                of << StringView("} else {");
                cb.emit(oddArm == 0 ? 1 : 0);
                of << StringView("}\n");
            } else {
                of << indent << StringView("switch(");
                emitVariant();
                of << StringView(") {\n");
                for (size_t j = 0; j < nArms; j++) {
                    if (e.isNiche(j)) {
                        continue;
                    }
                    of << indent << StringView("case ") << tagOf(j) << StringView("ull: ");
                    cb.emit(j);
                    of << StringView("break;\n");
                }
                of << indent << StringView("default: ");
                if (e.usesNiche()) {
                    cb.emit(e.field.index);
                    of << StringView("break;");
                } else {
                    of << StringView("abort();");
                }
                of << StringView("\n");
                of << indent << StringView("}\n");
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
                    MIR_TODO(localMirRes, StringView("Floating point enum tag."));
                    break;
                case HIRCoreType::Str:
                    MIR_BUG(localMirRes, StringView("Unsized tag?!"));
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
                    of << StringView(", ");
                    emitEnumVariantVal(repr, variant);
                    of << StringView(") == 0");
                } else {
                    emitTag();
                    of << StringView(" == ");
                    emitEnumVariantVal(repr, variant);
                }
            };

            if (oddArm != static_cast<size_t>(-1)) {
                of << indent << StringView("if(");
                emitEqual(oddArm);
                of << StringView(") {");
                cb.emit(oddArm);
                of << StringView("} else {");
                cb.emit(oddArm == 0 ? 1 : 0);
                of << StringView("}\n");
                return;
            }

            if (is128) {
                for (size_t j = 0; j < nArms; j++) {
                    of << indent << (j == 0 ? "if(" : "else if(");
                    emitEqual(j);
                    of << StringView(") {");
                    cb.emit(j);
                    of << StringView("}\n");
                }
                of << indent << StringView("else { abort(); }\n");
                return;
            }

            of << indent << StringView("switch(");
            emitTag();
            of << StringView(") {\n");
            for (size_t j = 0; j < nArms; j++) {
                if (is_signed) {
                    const auto value = S128(e.values[j]).truncateI64();
                    if (value == INT64_MIN) {
                        of << indent << StringView("case (-9223372036854775807ll - 1): ");
                    } else {
                        of << indent << StringView("case ") << value << StringView("ll: ");
                    }
                } else {
                    of << indent << StringView("case ") << e.values[j].truncateU64() << StringView("ull: ");
                }
                cb.emit(j);
                of << StringView("break;\n");
            }
            of << indent << StringView("default: abort();\n");
            of << indent << StringView("}\n");
            break;
        }
        case TypeReprVariantMode::TAG_None: {
            of << indent;
            cb.emit(0);
            of << StringView("\n");
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
        of << indent << StringView("{ static SLICE_PTR switch_strings[] = {");
        for (const auto& v : *ve) {
            of << StringView(" {(void*)");
            this->printEscapedString(v);
            of << StringView(",") << v.size() << StringView("},");
        }
        of << StringView(" {0,0} };\n");
        of << indent << StringView("switch( trustme_string_search_linear(");
        emitLvalue(val);
        of << StringView(", ") << ve->size() << StringView(", switch_strings) ) {\n");
        for (size_t i = 0; i < ve->size(); i++) {
            of << indent << StringView("case ") << i << StringView(": ");
            cb.emit(i);
            of << StringView(" break;\n");
        }
        of << indent << StringView("default: ");
        cb.emit(SIZE_MAX);
        of << StringView("\n");
        of << indent << StringView("} }\n");
    } else if (const auto* ve = values.opt_ByteString()) {
        of << indent << StringView("{ static SLICE_PTR switch_strings[] = {");
        for (const auto& v : *ve) {
            of << StringView(" {(void*)");
            this->printEscapedString(v);
            of << StringView(",") << v.size() << StringView("},");
        }
        of << StringView(" {0,0} };\n");
        HIRTypeRef tmp;
        const auto& ty = localMirRes.getLvalueType(tmp, val);
        of << indent << StringView("switch( trustme_string_search_linear(");
        if (const auto* a = ty->as_Borrow().inner->opt_Array()) {
            auto len = a->size.as_Known();
            of << StringView("make_sliceptr(");
            emitLvalue(val);
            of << StringView("->DATA, ") << len << StringView(")");
        } else {
            emitLvalue(val);
        }
        of << StringView(", ") << ve->size() << StringView(", switch_strings) ) {\n");
        for (size_t i = 0; i < ve->size(); i++) {
            of << indent << StringView("case ") << i << StringView(": ");
            cb.emit(i);
            of << StringView(" break;\n");
        }
        of << indent << StringView("default: ");
        cb.emit(SIZE_MAX);
        of << StringView("\n");
        of << indent << StringView("} }\n");
    } else if (const auto* ve = values.opt_Unsigned()) {
        const bool emulatedU128 = options.emulatedI128 && ty == HIRCoreType::U128;
        if (emulatedU128) {
            of << indent << StringView("if(");
            emitLvalue(val);
            of << StringView(".hi != 0) { ");
            cb.emit(SIZE_MAX);
            of << StringView(" }\n");
        }
        of << indent << (emulatedU128 ? "else " : "") << StringView("switch(");
        emitLvalue(val);
        if (emulatedU128) {
            of << StringView(".lo");
        }
        of << StringView(") {\n");
        for (size_t i = 0; i < ve->size(); i++) {
            of << indent << StringView("\tcase ") << (*ve)[i] << StringView("ull: ");
            cb.emit(i);
            of << StringView(" break;\n");
        }
        of << indent << StringView("\tdefault: ");
        cb.emit(SIZE_MAX);
        of << StringView("\n");
        of << indent << StringView("}\n");
    } else if (const auto* ve = values.opt_Signed()) {
        const bool emulatedI128 = options.emulatedI128 && ty == HIRCoreType::I128;
        if (emulatedI128) {
            of << indent << StringView("if(");
            emitLvalue(val);
            of << StringView(".hi != ((i64)");
            emitLvalue(val);
            of << StringView(".lo < 0 ? UINT64_MAX : 0)) { ");
            cb.emit(SIZE_MAX);
            of << StringView(" }\n");
        }
        of << indent << (emulatedI128 ? "else " : "") << StringView("switch(");
        if (emulatedI128) {
            of << StringView("(i64)");
        }
        emitLvalue(val);
        if (emulatedI128) {
            of << StringView(".lo");
        }
        of << StringView(") {\n");
        for (size_t i = 0; i < ve->size(); i++) {
            of << indent << StringView("\tcase ");
            if ((*ve)[i] == INT64_MIN) {
                of << StringView("INT64_MIN");
            } else {
                of << (*ve)[i] << StringView("ll");
            }
            of << StringView(": ");
            cb.emit(i);
            of << StringView(" break;\n");
        }
        of << indent << StringView("\tdefault: ");
        cb.emit(SIZE_MAX);
        of << StringView("\n");
        of << indent << StringView("}\n");
    } else {
        MIR_BUG(localMirRes, StringView("SwitchValue with unknown value type - ") << values.tagStr());
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
        MIR_BUG(localMirRes, StringView("Intrinsic used as an explicit tail-call target"));
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
                    of << StringView("{\n");
                    indent.n++;
                    of << indent;
                    hasZst = true;
                }
                emitCtype(ty, FMT_CB(ss, ss << StringView("zarg") << j;));
                of << StringView(" = {0};\n");
                of << indent;
                continue;
            }
        }
    }

    bool omitAssign = tailCall;

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
            of << StringView("TRUSTME_MUSTTAIL ");
        }
        of << StringView("return ");
    }

    switch (e.fcn.tag()) {
        case MIRCallTarget::TAG_Value: {
            auto& e2 = e.fcn.as_Value();
            {
                HIRTypeRef tmp;
                const auto& ty = localMirRes.getLvalueType(tmp, e2);
                MIR_ASSERT(localMirRes, ty->is_Function(), StringView("Call::Value on non-function - ") << ty);

                const auto& retTy = ty->as_Function().rettype;
                omitAssign |= retTy->is_Diverge();
                if (!omitAssign) {
                    emitLvalue(e.retVal);
                    of << StringView(" = ");
                }
            }
            of << StringView("(");
            emitLvalue(e2);
            of << StringView(")");
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
                        omitAssign |= resolve_.hirCrate().findTypeImpls(pe.type, HIRResolvePlaceholdersNop(), [&](const auto& impl) {
                            {
                                auto it = impl.methods.find(pe.item);
                                if (it != impl.methods.end()) {
                                    return it->second.data.returnType->is_Diverge();
                                }
                            }
                            return false;
                        });
                        break;
                    }
                    case HIRPathData::TAG_UfcsKnown: {
                        auto& pe = e2.data.as_UfcsKnown();
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
                        }
                        break;
                    }
                }
                if (!omitAssign) {
                    emitLvalue(e.retVal);
                    of << StringView(" = ");
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
                of << indent << StringView("}\n");
            }
            return;
        }
    }
    of << StringView("(");
    bool firstCallArgument = true;
    for (unsigned int j = 0; j < e.args.size(); j++) {
        HIRTypeRef tmp;
        const auto& ty = mirRes->getParamType(tmp, e.args[j]);
        if (!argumentIsPassed(calleeAbi, ty)) {
            continue;
        }
        if (!firstCallArgument) {
            of << StringView(", ");
        }
        firstCallArgument = false;

        if (this->typeIsBadZst(ty)) {
            of << StringView("zarg") << j;
            continue;
        }
        if (this->isDst(ty)) {
            emitDstParamPointer(e.args[j]);
            of << StringView(".PTR, ");
            emitDstParamPointer(e.args[j]);
            of << StringView(".META");
            continue;
        }
        emitParam(e.args[j]);
    }
    if (targetTracksCaller) {
        if (!firstCallArgument) {
            of << StringView(", ");
        }
        if (currentFunctionTracksCaller) {
            of << StringView("trustme_caller");
        } else {
            emitCallerLocationPointer(e.source);
        }
    }
    of << StringView(");\n");

    if (hasZst) {
        indent.n--;
        of << indent << StringView("}\n");
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
    std::vector<MIRParam> args;
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

auto CodeGeneratorC::asmMatchesTemplate(const MIRStatement::Data_Asm& e, const char* tpl, std::initializer_list<const char*> inputs, std::initializer_list<const char*> outputs) -> bool {
    struct H {
        static bool checkList(const std::vector<std::pair<std::string, MIRLValue>>& have, const std::initializer_list<const char*>& exp) {
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
            MIR_BUG(*mirRes, StringView("Hard-coded asm translation doesn't apply - `") << e.tpl << StringView("` inputs=") << e.inputs << StringView(" outputs=") << e.outputs);
        }
        return true;
    }
    return false;
}

auto CodeGeneratorC::emitAsmGcc(const MIRTypeResolve& localMirRes, const MIRStatement::Data_Asm& e, unsigned indentLevel) -> void {
    auto indent = RepeatLitStr{"\t", static_cast<int>(indentLevel)};

    struct H {
        static bool hasFlag(const std::vector<std::string>& flags, const char* des) {
            return std::find_if(flags.begin(), flags.end(), [des](const auto& x) {
                return x == des;
            }) != flags.end();
        }

        static const char* convertReg(const char* r) {
            if (std::strcmp(r, "{eax}") == 0 || std::strcmp(r, "{rax}") == 0) {
                return "a";
            } else if (std::strcmp(r, "{ebx}") == 0 || std::strcmp(r, "{rbx}") == 0) {
                return "b";
            } else if (std::strcmp(r, "{ecx}") == 0 || std::strcmp(r, "{rcx}") == 0) {
                return "c";
            } else if (std::strcmp(r, "{edx}") == 0 || std::strcmp(r, "{rdx}") == 0) {
                return "d";
            } else {
                return r;
            }
        }
    };

    bool isVolatile = H::hasFlag(e.flags, "volatile");
    bool isIntel = H::hasFlag(e.flags, "intel");

    if (asmMatchesTemplate(e, "cpuid", {"{eax}", "{ecx}"}, {"={eax}", "={ebx}", "={ecx}", "={edx}"})) {
        if (e.clobbers.size() == 1 && e.clobbers[0] == "rbx") {
            of << indent << StringView("__asm__(\"cpuid\"");
            of << StringView(" : ");
            of << StringView("\"=a\" (");
            emitLvalue(e.outputs[0].second);
            of << StringView("), ");
            of << StringView("\"=b\" (");
            emitLvalue(e.outputs[1].second);
            of << StringView("), ");
            of << StringView("\"=c\" (");
            emitLvalue(e.outputs[2].second);
            of << StringView("), ");
            of << StringView("\"=d\" (");
            emitLvalue(e.outputs[3].second);
            of << StringView(")");
            of << StringView(" : ");
            of << StringView("\"a\" (");
            emitLvalue(e.inputs[0].second);
            of << StringView("), ");
            of << StringView("\"c\" (");
            emitLvalue(e.inputs[1].second);
            of << StringView(")");
            of << StringView(" );\n");
            return;
        }
    }
    if (asmMatchesTemplate(e, "pushfd; popl $0", {}, {"=r"})) {
        of << indent << StringView("__asm__ __volatile__ (\".att_syntax prefix; pushfl; popl %%%0; .intel_syntax noprefix\" : \"=r\" (");
        emitLvalue(e.outputs[0].second);
        of << StringView(") : : );\n");
        return;
    }
    if (asmMatchesTemplate(e, "pushl $0; popfd", {"r"}, {})) {
        of << indent << StringView("__asm__ __volatile__ (\".att_syntax prefix; pushl %%%0; popfl; .intel_syntax noprefix\" : : \"r\" (");
        emitLvalue(e.inputs[0].second);
        of << StringView(") : );\n");
        return;
    }

    of << indent << StringView("__asm__ ");
    if (isVolatile) {
        of << StringView("__volatile__");
    }
    const bool emitAttSyntax = usesIntelCompilerAsmDialect() && !isIntel;
    of << StringView("(\"") << (emitAttSyntax ? ".att_syntax prefix; " : "");
    // TODO: Use a more powerful parser that can properly handle the differences between rustc/llvm and GCC
    for (auto it = e.tpl.begin(); it != e.tpl.end(); ++it) {
        if (*it == '\n') {
            of << StringView(";\\n");
        } else if (*it == '"') {
            of << StringView("\\\"");
        } else if (*it == '\\') {
            of << StringView("\\\\");
        } else if (*it == '/' && *(it + 1) == '/') {
            while (it != e.tpl.end() || *it == '\n') {
                ++it;
            }
            --it;
        } else if (*it == '%' && *(it + 1) == '%') {
            of << StringView("%");
        } else if (*it == '%' && isdigit(*(it + 1)) && emitAttSyntax) {
            of << StringView("%%%");
        } else if (*it == '%' && !isdigit(*(it + 1))) {
            of << StringView("%%");
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
    of << (emitAttSyntax ? ".intel_syntax noprefix; " : "") << StringView("\"");
    of << StringView(": ");
    for (unsigned int i = 0; i < e.outputs.size(); i++) {
        const auto& v = e.outputs[i];
        if (i != 0) {
            of << StringView(", ");
        }
        of << StringView("\"");
        switch (v.first[0]) {
            case '=':
                of << StringView("=");
                break;
            case '+':
                of << StringView("+");
                break;
            default:
                MIR_TODO(localMirRes, StringView("Handle asm! output leader '") << v.first[0] << StringView("'"));
        }
        of << H::convertReg(v.first.c_str() + 1);
        of << StringView("\" (");
        emitLvalue(v.second);
        of << StringView(")");
    }
    of << StringView(": ");
    for (unsigned int i = 0; i < e.inputs.size(); i++) {
        const auto& v = e.inputs[i];
        if (i != 0) {
            of << StringView(", ");
        }
        // TODO: If this is the same reg as an output, use the output index
        of << StringView("\"") << H::convertReg(v.first.c_str()) << StringView("\" (");
        emitLvalue(v.second);
        of << StringView(")");
    }
    of << StringView(": ");
    for (unsigned int i = 0; i < e.clobbers.size(); i++) {
        if (i != 0) {
            of << StringView(", ");
        }
        if (e.tpl == "cpuid\n" && e.clobbers[i] == "rbx") {
            continue;
        }
        of << StringView("\"") << e.clobbers[i] << StringView("\"");
    }
    of << StringView(");\n");
}

auto CodeGeneratorC::emitAsm2Gcc(const MIRTypeResolve& localMirRes, const MIRStatement& stmt, unsigned indentLevel) -> void {
    const auto& e = stmt.as_Asm2();
    emitAsm2Gcc(localMirRes, e.options, e.lines, e.params, false, ~0u, indentLevel);
}

auto CodeGeneratorC::emitAsm2Gcc(const MIRTypeResolve& localMirRes, const AsmOptions& asmOptions, const std::vector<AsmLine>& asmLines, const std::vector<MIRAsmParam>& asmParams, bool asmGoto, MIRBasicBlockId retBlock, unsigned indentLevel) -> void {
    auto indent = RepeatLitStr{"\t", static_cast<int>(indentLevel)};
    Asm2TplMatch m{localMirRes, asmLines, asmParams};

    if (m.matchesTemplate({"movq %rbx, {0:r}", "cpuid", "xchgq %rbx, {0:r}"}, {"lateout:reg", "inlateout=eax", "inlateout=ecx", "lateout=edx"})) {
        of << indent << StringView("__asm__(\"cpuid\"");
        of << StringView(" : ");
        of << StringView("\"=a\" (");
        emitLvalue(m.output(1));
        of << StringView("), ");
        of << StringView("\"=b\" (");
        emitLvalue(m.output(0));
        of << StringView("), ");
        of << StringView("\"=c\" (");
        emitLvalue(m.output(2));
        of << StringView("), ");
        of << StringView("\"=d\" (");
        emitLvalue(m.output(3));
        of << StringView(")");
        of << StringView(" : ");
        of << StringView("\"a\" (");
        emitParam(m.input(1));
        of << StringView("), ");
        of << StringView("\"c\" (");
        emitParam(m.input(2));
        of << StringView(")");
        of << StringView(" );\n");
        return;
    } else if (m.matchesTemplate({"mov {0:r}, rbx", "cpuid", "xchg {0:r}, rbx"}, {"out:reg", "inout=eax", "inout=ecx", "out=edx"})) {
        of << indent << StringView("__asm__(\"cpuid\"");
        of << StringView(" : ");
        of << StringView("\"=a\" (");
        emitLvalue(m.output(1));
        of << StringView("), ");
        of << StringView("\"=b\" (");
        emitLvalue(m.output(0));
        of << StringView("), ");
        of << StringView("\"=c\" (");
        emitLvalue(m.output(2));
        of << StringView("), ");
        of << StringView("\"=d\" (");
        emitLvalue(m.output(3));
        of << StringView(")");
        of << StringView(" : ");
        of << StringView("\"a\" (");
        emitParam(m.input(1));
        of << StringView("), ");
        of << StringView("\"c\" (");
        emitParam(m.input(2));
        of << StringView(")");
        of << StringView(" );\n");
        return;
    } else if (m.matchesTemplate({"btl {1:e}, ({0})", "setc {2}"}, {"in:reg", "in:reg", "out:reg_byte"})) {
        of << indent << StringView("__asm__(\".att_syntax prefix; bt %%%1, (%%%2); setc %%%0; .intel_syntax noprefix\"");
        of << StringView(" : \"=r\"(");
        emitLvalue(m.output(2));
        of << StringView(")");
        of << StringView(" : \"r\"(");
        emitParam(m.input(0));
        of << StringView("), \"r\"(");
        emitParam(m.input(1));
        of << StringView(")");
        of << StringView(");\n");
        return;
    } else if (m.matchesTemplate({"btcl {1:e}, ({0})", "setc {2}"}, {"in:reg", "in:reg", "out:reg_byte"})) {
        of << indent << StringView("__asm__(\".att_syntax prefix; btc %%%1, (%%%2); setc %%%0; .intel_syntax noprefix\"");
        of << StringView(" : \"=r\"(");
        emitLvalue(m.output(2));
        of << StringView(")");
        of << StringView(" : \"r\"(");
        emitParam(m.input(0));
        of << StringView("), \"r\"(");
        emitParam(m.input(1));
        of << StringView(")");
        of << StringView(");\n");
        return;
    } else if (m.matchesTemplate({"btrl {1:e}, ({0})", "setc {2}"}, {"in:reg", "in:reg", "out:reg_byte"})) {
        of << indent << StringView("__asm__(\".att_syntax prefix; btr %%%1, (%%%2); setc %%%0; .intel_syntax noprefix\"");
        of << StringView(" : \"=r\"(");
        emitLvalue(m.output(2));
        of << StringView(")");
        of << StringView(" : \"r\"(");
        emitParam(m.input(0));
        of << StringView("), \"r\"(");
        emitParam(m.input(1));
        of << StringView(")");
        of << StringView(");\n");
        return;
    } else if (m.matchesTemplate({"btsl {1:e}, ({0})", "setc {2}"}, {"in:reg", "in:reg", "out:reg_byte"})) {
        of << indent << StringView("__asm__(\".att_syntax prefix; bts %%%1, (%%%2); setc %%%0; .intel_syntax noprefix\"");
        of << StringView(" : \"=r\"(");
        emitLvalue(m.output(2));
        of << StringView(")");
        of << StringView(" : \"r\"(");
        emitParam(m.input(0));
        of << StringView("), \"r\"(");
        emitParam(m.input(1));
        of << StringView(")");
        of << StringView(");\n");
        return;
    }
    // HACK: Abort on various `v*` operations, as they have overly complex register specs that gcc doesn't like
    else if (asmLines[0].frags.size() > 0 && (false || asmLines[0].frags[0].before.find("vmov") == 0 || asmLines[0].frags[0].before.find("vexpand") == 0 || asmLines[0].frags[0].before.find("vpexpand") == 0)) {
        of << StringView("abort();\n");
        return;
    } else {
        std::vector<unsigned> argMappings(asmParams.size(), UINT_MAX);
        bool blockOpen = false;
        for (size_t i = 0; i < asmParams.size(); i++) {
            if (const auto* pe = asmParams[i].opt_Reg()) {
                if (!pe->input && !pe->output) {
                } else if (const auto* regnameP = pe->spec.opt_Explicit()) {
                    argMappings[i] = UINT_MAX - 1;
                    if (!blockOpen) {
                        blockOpen = true;
                        of << indent << StringView("{\n");
                    }
                    of << indent << StringView("register uintptr_t asm_") << *regnameP << StringView(" asm(\"") << *regnameP << StringView("\")");
                    if (pe->input) {
                        of << StringView(" = (uintptr_t)");
                        emitParam(*pe->input);
                    }
                    of << StringView(";\n");
                }
            }
        }
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
                of << indent << StringView("{\n");
            }
            vectorShim.mut(i) = opSize;
            of << indent << StringView("typedef long long asm_vec_ty_") << i << StringView(" __attribute__((vector_size(") << opSize << StringView(")));\n");
            of << indent << StringView("asm_vec_ty_") << i << StringView(" asm_vec_") << i << StringView(";\n");
            if (pe->input) {
                of << indent << StringView("memcpy(&asm_vec_") << i << StringView(", &");
                emitParam(*pe->input);
                of << StringView(", ") << opSize << StringView(");\n");
            }
        }

        std::vector<const MIRAsmParam::Data_Reg*> outputs;
        for (size_t i = 0; i < asmParams.size(); i++) {
            if (const auto* pe = asmParams[i].opt_Reg()) {
                if (pe->spec.is_Explicit()) {
                    if (pe->output) {
                        outputs.push_back(pe);
                    }
                } else if (!pe->output && !pe->input) {
                    if (!blockOpen) {
                        blockOpen = true;
                        of << indent << StringView("{\n");
                    }
                    of << indent << StringView("uintptr_t asm_anon_") << outputs.size() << StringView(" = 0;\n");

                    argMappings[i] = outputs.size();
                    outputs.push_back(pe);
                } else if (pe->output) {
                    argMappings[i] = outputs.size();
                    outputs.push_back(pe);
                }
            }
        }
        std::vector<const MIRAsmParam*> inputs;
        for (size_t i = 0; i < asmParams.size(); i++) {
            if (const auto* pe = asmParams[i].opt_Reg()) {
                if (pe->spec.opt_Explicit()) {
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
        std::vector<const char*> clobbers;
        for (size_t i = 0; i < asmParams.size(); i++) {
            if (const auto* pe = asmParams[i].opt_Reg()) {
                if (!pe->input && !pe->output && pe->spec.is_Explicit()) {
                    const auto& regname = pe->spec.as_Explicit();
                    clobbers.push_back(regname.c_str());
                }
            }
        }

        const bool emitAttSyntax = usesIntelCompilerAsmDialect() && asmOptions.attSyntax;
        of << indent << StringView("__asm__ ");
        of << StringView("__volatile__");
        if (asmGoto) {
            of << StringView(" goto");
        }
        of << StringView("(\"");
        if (emitAttSyntax) {
            of << StringView(".att_syntax prefix; ");
        }
        bool escapePercent = true || !inputs.empty() || !outputs.empty();
        for (const auto& l : asmLines) {
            for (const auto& f : l.frags) {
                of << FmtGccAsm(f.before, escapePercent);
                const auto& param = asmParams.at(f.index);
                if (const auto* constant = param.opt_Const()) {
                    MIR_ASSERT(localMirRes, f.modifier == '\0', StringView("Modifier on asm const operand"));
                    auto text = inlineAsmConstant(*constant);
                    of << FmtGccAsm(text, escapePercent);
                    continue;
                }
                if (const auto* path = param.opt_Sym()) {
                    MIR_ASSERT(localMirRes, f.modifier == '\0', StringView("Modifier on asm sym operand"));
                    auto text = asmSymbol(localMirRes.sp, *path);
                    of << FmtGccAsm(text, escapePercent);
                    continue;
                }
                if (param.is_Label()) {
                    MIR_ASSERT(localMirRes, asmGoto && f.modifier == '\0', StringView("Invalid asm label operand"));
                    of << StringView("%l[bb") << param.as_Label() << StringView("]");
                    continue;
                }
                MIR_ASSERT(localMirRes, argMappings.at(f.index) != UINT_MAX, StringView("Invalid asm operand mapping"));
                if (emitAttSyntax) {
                    of << StringView("%%");
                }
                of << StringView("%");
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
                    case 'l':
                        of << 'b';
                        break;
                    case 'h':
                        of << 'h';
                        break;
                    case 'x': {
                        const auto* opClass = asmParams[f.index].as_Reg().spec.opt_Class();
                        const bool vector = opClass && (*opClass == AsmRegisterClass::x86Xmm || *opClass == AsmRegisterClass::x86Ymm || *opClass == AsmRegisterClass::x86Zmm);
                        of << (vector ? 'x' : 'w');
                        break;
                    }
                    case 'y':
                        of << 't';
                        break;
                    case 'z':
                        of << 'g';
                        break;
                    case 'e':
                        of << 'k';
                        break;
                    case 'r':
                        of << 'q';
                        break;
                    default:
                        MIR_TODO(localMirRes, StringView("Asm2 GCC: modifier ") << f.modifier);
                }
                of << argMappings.at(f.index);
            }
            of << FmtGccAsm(l.trailing, escapePercent);
            of << StringView(";\\n ");
        }
        if (emitAttSyntax) {
            of << StringView(".intel_syntax noprefix; ");
        }
        of << StringView("\"");
        if (asmOptions.naked) {
            MIR_ASSERT(localMirRes, outputs.empty() && inputs.empty() && clobbers.empty() && !blockOpen, StringView("naked_asm contains register operands"));
            of << StringView(");\n");
            return;
        }
        of << StringView(" :");
        for (size_t i = 0; i < outputs.size(); i++) {
            const auto& p = *outputs[i];
            if (i != 0) {
                of << StringView(",");
            }
            of << StringView(" ");
            of << StringView("\"");
            if (!p.output && !p.input) {
                of << StringView("+");
            } else if (p.input && p.spec.is_Explicit()) {
                of << (p.dir == AsmDirection::InOut ? "+&" : "+");
            } else {
                switch (p.dir) {
                    case AsmDirection::Out:
                    case AsmDirection::InOut:
                        of << StringView("=&");
                        break;
                    case AsmDirection::LateOut:
                    case AsmDirection::InLateOut:
                        of << StringView("=");
                        break;
                    case AsmDirection::In:
                        MIR_BUG(localMirRes, StringView("Input-only asm parameter listed as an output"));
                }
            }
            switch (p.spec.tag()) {
                case AsmRegisterSpec::TAG_Class: {
                    auto& c = p.spec.as_Class();
                    switch (c) {
                        case AsmRegisterClass::x86Reg:
                            of << StringView("r");
                            break;
                        case AsmRegisterClass::x86RegAbcd:
                            of << StringView("Q");
                            break;
                        case AsmRegisterClass::x86RegByte:
                            of << StringView("q");
                            break;
                        case AsmRegisterClass::x86Xmm:
                            of << StringView("x");
                            break;
                        case AsmRegisterClass::x86Ymm:
                            of << StringView("x");
                            break;
                        case AsmRegisterClass::x86Zmm:
                            of << StringView("v");
                            break;
                        case AsmRegisterClass::x86Kreg:
                            of << StringView("Yk");
                            break;
                        case AsmRegisterClass::riscvReg:
                            of << StringView("r");
                            break;
                        case AsmRegisterClass::riscvFreg:
                            of << StringView("f");
                            break;
                    }
                    break;
                }
                case AsmRegisterSpec::TAG_Explicit: {
                    of << StringView("r");
                    break;
                }
            }
            of << StringView("\" (");
            if (!p.output) {
                of << StringView("asm_anon_") << i;
            } else if (const auto* regnameP = p.spec.opt_Explicit()) {
                of << StringView("asm_") << *regnameP;
            } else if (const auto shimIdx = paramIndexOf(&p); shimIdx != asmParams.size() && vectorShim[shimIdx] != 0) {
                of << StringView("asm_vec_") << shimIdx;
            } else {
                emitLvalue(*p.output);
            }
            of << StringView(")");
        }
        of << StringView(" :");
        for (size_t i = 0; i < inputs.size(); i++) {
            const auto& p = *inputs[i];
            if (i != 0) {
                of << StringView(",");
            }
            of << StringView(" ");
            switch (p.tag()) {
                case MIRAsmParam::TAG_Reg: {
                    auto& r = p.as_Reg();
                    of << StringView("\"");
                    if (r.output && !r.spec.is_Explicit()) {
                        const auto it = std::find(outputs.begin(), outputs.end(), &r);
                        MIR_ASSERT(localMirRes, it != outputs.end(), StringView("Missing asm output"));
                        of << (it - outputs.begin());
                    } else {
                        switch (r.spec.tag()) {
                            case AsmRegisterSpec::TAG_Class: {
                                auto& c = r.spec.as_Class();
                                switch (c) {
                                    case AsmRegisterClass::x86Reg:
                                        of << StringView("r");
                                        break;
                                    case AsmRegisterClass::x86RegAbcd:
                                        of << StringView("Q");
                                        break;
                                    case AsmRegisterClass::x86RegByte:
                                        of << StringView("q");
                                        break;
                                    case AsmRegisterClass::x86Xmm:
                                        of << StringView("x");
                                        break;
                                    case AsmRegisterClass::x86Ymm:
                                        of << StringView("x");
                                        break;
                                    case AsmRegisterClass::x86Zmm:
                                        of << StringView("v");
                                        break;
                                    case AsmRegisterClass::x86Kreg:
                                        of << StringView("Yk");
                                        break;
                                    case AsmRegisterClass::riscvReg:
                                        of << StringView("r");
                                        break;
                                    case AsmRegisterClass::riscvFreg:
                                        of << StringView("f");
                                        break;
                                }
                                break;
                            }
                            case AsmRegisterSpec::TAG_Explicit: {
                                of << StringView("r");
                                break;
                            }
                        }
                    }
                    BUG_ASSERT(r.input);
                    of << StringView("\" (");
                    const auto shimIdx = paramIndexOf(&r);
                    if (const auto* regnameP = p.as_Reg().spec.opt_Explicit()) {
                        of << StringView("asm_") << *regnameP;
                    } else if (shimIdx != asmParams.size() && vectorShim[shimIdx] != 0) {
                        of << StringView("asm_vec_") << shimIdx;
                    } else {
                        emitParam(*r.input);
                    }
                    of << StringView(")");
                    break;
                }
                case MIRAsmParam::TAG_Const: {
                    MIR_TODO(localMirRes, StringView("Asm2 GCC - Const"));
                    break;
                }
                case MIRAsmParam::TAG_Sym: {
                    MIR_TODO(localMirRes, StringView("Asm2 GCC - Sym"));
                    break;
                }
                case MIRAsmParam::TAG_Label: {
                    MIR_BUG(localMirRes, StringView("Asm label listed as an input"));
                    break;
                }
            }
        }
        of << StringView(" :");
        for (size_t i = 0; i < clobbers.size(); i++) {
            if (i > 0) {
                of << StringView(",");
            }
            of << StringView(" \"") << (std::strcmp(clobbers[i], "st(0)") == 0 ? "st" : clobbers[i]) << StringView("\"");
        }
        if (asmGoto) {
            of << StringView(" :");
            bool firstLabel = true;
            for (size_t i = 0; i < asmParams.size(); ++i) {
                if (const auto* label = asmParams[i].opt_Label()) {
                    if (!firstLabel) {
                        of << StringView(",");
                    }
                    firstLabel = false;
                    of << StringView(" bb") << forwardedBlockTarget(*label);
                }
            }
        }
        of << StringView(");\n");
        for (size_t i = 0; i < asmParams.size(); i++) {
            if (vectorShim[i] != 0) {
                const auto* pe = asmParams[i].opt_Reg();
                if (pe->output) {
                    of << indent << StringView("memcpy(&");
                    emitLvalue(*pe->output);
                    of << StringView(", &asm_vec_") << i << StringView(", ") << vectorShim[i] << StringView(");\n");
                }
            }
        }
        for (size_t i = 0; i < asmParams.size(); i++) {
            if (const auto* pe = asmParams[i].opt_Reg()) {
                if (const auto* regnameP = pe->spec.opt_Explicit()) {
                    if (pe->output) {
                        of << indent;
                        emitLvalue(*pe->output);
                        of << StringView(" = ");
                        HIRTypeRef tmp;
                        of << StringView("(");
                        emitCtype(mirRes->getLvalueType(tmp, *pe->output));
                        of << StringView(")");
                        of << StringView("asm_") << *regnameP << StringView(";\n");
                    }
                }
            }
        }
        if (asmGoto) {
            if (retBlock == ~0u) {
                of << indent << StringView("__builtin_unreachable();\n");
            } else {
                const auto target = forwardedBlockTarget(retBlock);
                if (blockIsInlinedReturn(target)) {
                    of << indent << StringView("return");
                    if (localMirRes.retType != crate.types.unit()) {
                        of << StringView(" rv");
                    }
                    of << StringView(";\n");
                } else if (target != fallthroughBlock) {
                    of << indent << StringView("goto bb") << target << StringView(";\n");
                }
            }
        }
        if (blockOpen) {
            of << indent << StringView("}\n");
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
    of << StringView("{{(void*)");
    printEscapedStringInner(source.filename.c_str(), source.filename.c_str() + source.filename.size());
    of << StringView(",") << source.filename.size() << StringView("},") << source.line << StringView(",") << source.column << StringView("}");
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
    of << StringView("&trustme_caller_locations[") << location->index << StringView("]");
}

auto CodeGeneratorC::emitCallerLocationDefinitions() -> void {
    of << StringView("namespace {\n")
       << StringView("const trustme_caller_location trustme_caller_locations[] = {\n");
    if (!firstCallerLocation) {
        of << StringView("\t{},\n");
    } else {
        for (const auto* location = firstCallerLocation; location; location = location->orderNext) {
            of << StringView("\t");
            emitSourceLocationInitializer(location->source);
            of << StringView(",\n");
        }
    }
    of << StringView("};\n}\n");
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
        of << StringView("__trustme_reify");
    }
}

auto CodeGeneratorC::monomorphiseFcnReturn(HIRTypeRef& tmp, const HIRFunction& item, const TransParams& params) -> const HIRTypeData* {
    bool hasErased = visitTyWith(item.returnType, [&](const auto& x) {
        return x->is_ErasedType();
    });

    if (hasErased || monomorphiseTypeNeeded(item.returnType)) {
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
        of << StringView("__attribute__((naked)) ");
    }
    if (item.markings.inlineType == HIRFunction::Markings::Inline::Always) {
        of << StringView("__attribute__((always_inline)) ");
    }
    if (item.markings.alignment != 0) {
        of << StringView("__attribute__((aligned(") << item.markings.alignment << StringView("))) ");
    }
    auto cb = FMT_CB(ss, ss << StringView(" ") << compilerAbiAttribute(item.abi) << TransMangleValue(p) << nameSuffix << StringView("("); if (passedCount == 0 && !hasCallerLocation && !item.variadic) { ss << StringView("void)"); } else {
        unsigned int emitted = 0;
        for (unsigned int i = 0; i < item.fixedArgCount(); i++) {
            auto ty = params.monomorph(resolve_, item.args[i].second);
            if (!argumentIsPassed(item.abi, ty)) {
                continue;
            }
            if (compact) {
                if (emitted != 0) {
                    ss << StringView(", ");
                }
            } else {
                ss << StringView("\n\t\t");
            }
            this->emitFunctionArgument(ty, FMT_CB(os, os << StringView("arg") << i;));
            emitted++;
            if (!compact && (item.variadic || emitted < passedCount || hasCallerLocation)) {
                of << StringView(",");
            }
        }

        if (item.variadic) {
            if (compact) {
                of << (emitted != 0 ? ", ..." : "...");
            } else {
                of << StringView("\n\t\t...");
            }
            emitted++;
        }

        if (hasCallerLocation) {
            MIR_ASSERT(*mirRes, !item.variadic, StringView("#[track_caller] on a variadic function"));
            if (compact) {
                if (emitted != 0) {
                    of << StringView(", ");
                }
            } else {
                of << StringView("\n\t\t");
            }
            of << StringView("const trustme_caller_location* trustme_caller");
        }

        ss << (compact ? ")" : "\n\t\t)");
    });
    if (retTy != crate.types.unit()) {
        emitCtype(retTy, cb);
    } else {
        of << StringView("void ") << cb;
    }
}

auto CodeGeneratorC::emitTrackCallerReifyWrapper(const HIRPath& p, const HIRFunction& item, const TransParams& params) -> void {
    MIR_ASSERT(*mirRes, !item.variadic, StringView("Cannot reify a variadic #[track_caller] function"));
    of << StringView("static ");
    emitFunctionHeader(p, item, params, /*includeCallerLocation=*/false, "__trustme_reify");
    of << StringView(" {\n");
    of << StringView("\t");

    HIRTypeRef returnTypeTmp;
    const auto& returnType = monomorphiseFcnReturn(returnTypeTmp, item, params);
    if (returnType != crate.types.unit()) {
        of << StringView("return ");
    }
    of << TransMangleValue(p) << StringView("(");
    bool first = true;
    auto emitArgument = [&](const char* prefix, unsigned index, const char* suffix) {
        if (!first) {
            of << StringView(", ");
        }
        first = false;
        of << prefix << index << suffix;
    };
    for (unsigned int i = 0; i < item.args.size(); i++) {
        auto type = params.monomorph(resolve_, item.args[i].second);
        switch (metadataType(type)) {
            case MetadataType::Unknown:
                MIR_BUG(*mirRes, type << StringView(" has unknown function-argument metadata"));
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
        of << StringView(", ");
    }
    emitCallerLocationPointer(item.source);
    of << StringView(");\n");
    if (returnType == crate.types.unit()) {
        of << StringView("\treturn;\n");
    }
    of << StringView("}\n\n");
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
        if (std::strcmp(suffix, "acq") == 0 || std::strcmp(suffix, "acquire") == 0 || std::strcmp(suffix, "relaxed_acquire") == 0 || std::strcmp(suffix, "acquire_acquire") == 0 || std::strcmp(suffix, "acquire_relaxed") == 0) {
            return Ordering::Acquire;
        } else if (std::strcmp(suffix, "rel") == 0 || std::strcmp(suffix, "release") == 0 || std::strcmp(suffix, "release_relaxed") == 0) {
            return Ordering::Release;
        } else if (std::strcmp(suffix, "relaxed") == 0 || std::strcmp(suffix, "relaxed_relaxed") == 0) {
            return Ordering::Relaxed;
        } else if (std::strcmp(suffix, "acqrel") == 0 || std::strcmp(suffix, "acqrel_relaxed") == 0) {
            return Ordering::AcqRel;
        }
        // TODO: Is this correct?
        else if (std::strcmp(suffix, "unordered") == 0) {
            return Ordering::Relaxed;
        } else if (std::strcmp(suffix, "seqcst") == 0 || std::strcmp(suffix, "relaxed_seqcst") == 0 || std::strcmp(suffix, "release_seqcst") == 0 || std::strcmp(suffix, "acquire_seqcst") == 0 || std::strcmp(suffix, "acqrel_seqcst") == 0 || std::strcmp(suffix, "seqcst_seqcst") == 0 || std::strcmp(suffix, "release_acquire") == 0 || std::strcmp(suffix, "acqrel_acquire") == 0 || std::strcmp(suffix, "seqcst_acquire") == 0 || std::strcmp(suffix, "seqcst_relaxed") == 0) {
            return Ordering::SeqCst;
        } else {
            MIR_BUG(localMirRes, StringView("Unknown atomic ordering suffix - '") << suffix << StringView("'"));
        }
        UNREACHABLE();
    };
    auto getPrimSize = [&localMirRes](const HIRTypeData* ty) -> unsigned {
        if (ty->is_Pointer()) {
            return TargetGetPointerBits();
        }
        if (!ty->is_Primitive()) {
            MIR_BUG(localMirRes, StringView("Unknown type for getting primitive size - ") << ty);
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
                MIR_BUG(localMirRes, StringView("Unknown primitive for getting size- ") << ty);
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
                BUG(Span(), StringView(""));
            case HIRCoreType::Isize:
                if (TargetGetPointerBits() == 64) {
                    return HIRCoreType::I64;
                }
                if (TargetGetPointerBits() == 32) {
                    return HIRCoreType::I32;
                }
                BUG(Span(), StringView(""));
            default:
                return ct;
        }
    };
    auto emitAtomicCast = [&]() {
        of << StringView("(");
        emitCtype(params.types.at(0));
        of << StringView("*)");
    };
    const bool atomicTypeIsPointer = params.types.size() > 0 && params.types.at(0)->is_Pointer();
    auto emitAtomicRmwCast = [&]() {
        if (atomicTypeIsPointer) {
            of << StringView("(");
            emitCtype(params.types.at(0));
            of << StringView(")");
        }
    };
    auto emitAtomicRmwOperand = [&](const MIRParam& param) {
        if (atomicTypeIsPointer) {
            of << StringView("(uintptr_t)");
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
            of << StringView("{ ");
            emitCtype(params.types.at(0), FMT_CB(ss, ss << StringView(" trustme_atomic_desired");));
            of << StringView(" = ");
            emitParam(e.args.at(2));
            of << StringView("; ");
        }
        emitLvalue(e.retVal);
        of << StringView("._0 = ");
        emitParam(e.args.at(1));
        of << StringView(";\n\t");
        emitLvalue(e.retVal);
        of << StringView("._1 = ") << (emulatedI128 ? "__atomic_compare_exchange(" : "__atomic_compare_exchange_n(");
        emitAtomicCast();
        emitParam(e.args.at(0));
        of << StringView(", &");
        emitLvalue(e.retVal);
        of << StringView("._0");
        of << StringView(", ");
        if (emulatedI128) {
            of << StringView("&trustme_atomic_desired");
        } else {
            emitParam(e.args.at(2));
        }
        of << StringView(", ") << (isWeak ? "true" : "false");
        of << StringView(", ") << getAtomicTyGcc(oSucc) << StringView(", ") << getAtomicTyGcc(oFail) << StringView(")");
        if (emulatedI128) {
            of << StringView("; }");
        }
    };
    auto emitAtomicArith = [&](AtomicOp op, Ordering ordering) {
        emitLvalue(e.retVal);
        of << StringView(" = ");
        emitAtomicRmwCast();
        switch (op) {
            case AtomicOp::Add:
                of << StringView("__atomic_fetch_add");
                break;
            case AtomicOp::Sub:
                of << StringView("__atomic_fetch_sub");
                break;
            case AtomicOp::And:
                of << StringView("__atomic_fetch_and");
                break;
            case AtomicOp::Or:
                of << StringView("__atomic_fetch_or");
                break;
            case AtomicOp::Xor:
                of << StringView("__atomic_fetch_xor");
                break;
        }
        of << StringView("(");
        if (atomicTypeIsPointer) {
            of << StringView("(uintptr_t *)");
        } else {
            emitAtomicCast();
        }
        emitParam(e.args.at(0));
        of << StringView(", ");
        emitAtomicRmwOperand(e.args.at(1));
        of << StringView(", ") << getAtomicTyGcc(ordering) << StringView(")");
    };
    if (name == "size_of") {
        size_t size = 0;
        MIR_ASSERT(localMirRes, TargetGetSizeOf(sp, resolve_, params.types.at(0), size), StringView("Can't get size of ") << params.types.at(0));
        emitLvalue(e.retVal);
        of << StringView(" = ") << size;
    } else if (name == "offset_of") {
        size_t val = localMirRes.intrinsicOffsetOf(params.types.at(0), e.args);
        emitLvalue(e.retVal);
        of << StringView(" = ") << val;
    } else if (name == "min_align_of" || name == "align_of") {
        size_t align = 0;
        MIR_ASSERT(localMirRes, TargetGetAlignOf(sp, resolve_, params.types.at(0), align), StringView("Can't get alignment of ") << params.types.at(0));
        emitLvalue(e.retVal);
        of << StringView(" = ") << align;
    } else if (name == "vtable_size" || name == "vtable_align") {
        emitLvalue(e.retVal);
        of << StringView(" = ((VTABLE_HDR*)");
        emitParam(e.args.at(0));
        of << StringView(")->") << (name == "vtable_size" ? "size" : "align");
    } else if (name == "size_of_val") {
        const auto& ty = params.types.at(0);
        auto innerTy = getInnerUnsizedType(ty);
        if (isExternUnsizedType(innerTy)) {
            emitExternTypeLayoutPanic(innerTy);
        } else {
            emitLvalue(e.retVal);
            of << StringView(" = ");
            if (innerTy == HIRTypeRef()) {
                size_t size = 0;
                MIR_ASSERT(localMirRes, TargetGetSizeOf(sp, resolve_, ty, size), StringView("Can't get size of ") << ty);
                of << size;
            } else if (innerTy->is_Slice() || innerTy == HIRCoreType::Str) {
                bool alignNeeded = false;
                size_t itemSize = 0;
                size_t itemAlign = 0;
                if (const auto* te = innerTy->opt_Slice()) {
                    MIR_ASSERT(localMirRes, TargetGetSizeAndAlignOf(sp, resolve_, te->inner, itemSize, itemAlign), StringView("Can't get size of ") << te->inner);
                } else {
                    BUG_ASSERT(innerTy == HIRCoreType::Str);
                    itemSize = 1;
                    itemAlign = 1;
                }
                if (!ty->is_Slice() && !ty->is_Primitive()) {
                    emitDstSize(ty, e.args.at(0));
                } else {
                    emitParam(e.args.at(0));
                    of << StringView(".META * ") << itemSize;
                }
            } else if (innerTy->is_TraitObject()) {
                emitDstSize(ty, e.args.at(0));
            } else {
                MIR_BUG(localMirRes, StringView("Unknown inner unsized type ") << innerTy << StringView(" for ") << ty);
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
            of << StringView(" = ");
            if (innerTy == HIRTypeRef()) {
                size_t alignment = 0;
                MIR_ASSERT(localMirRes, TargetGetAlignOf(sp, resolve_, ty, alignment), StringView("Can't get alignment of ") << ty);
                of << alignment;
            } else if (const auto* te = innerTy->opt_Slice()) {
                of << StringView("ALIGNOF(");
                if (ty->is_Slice()) {
                    emitCtype(te->inner);
                } else {
                    emitCtype(ty);
                }
                of << StringView(")");
            } else if (innerTy == HIRCoreType::Str) {
                if (!ty->is_Primitive()) {
                    of << StringView("ALIGNOF(");
                    emitCtype(ty);
                    of << StringView(")");
                } else {
                    of << StringView("1");
                }
            } else if (innerTy->is_TraitObject()) {
                emitDstAlign(ty, e.args.at(0));
            } else {
                MIR_BUG(localMirRes, StringView("Unknown inner unsized type ") << innerTy << StringView(" for ") << ty);
            }
        }
    } else if (name == "panic_if_uninhabited" || name == "assert_inhabited") {
        // TODO: Detect uninhabited (empty enum or `!` - potentially via nested types)
    } else if (name == "assert_zero_valid") {
        // TODO: Detect nonzero within
    } else if (name == "assert_mem_uninitialized_valid") {
        // TODO: Detect nonzero or enum within
    } else if (name == "const_eval_select") {
        const auto& argTyTuple = params.types.at(0)->as_Tuple();
        const auto& arg = e.args.at(0).as_LValue();
        const auto& fcnPath = *e.args.at(2).as_Constant().as_Function().p;

        std::vector<MIRParam> args;
        args.reserve(argTyTuple.size());
        for (size_t i = 0; i < argTyTuple.size(); i++) {
            args.push_back(MIRLValue::newField(arg.clone(), i));
        }
        auto pseudoTerm = MIRTerminator::Data_Call{e.retBlock, MIRUnwindAction::make_Continue({}), e.retVal.clone(), MIRCallTarget::make_Path(fcnPath.clone()), std::move(args)};
        emitTermCall(localMirRes, pseudoTerm, 1);
    } else if (name == "type_id") {
        const auto& ty = params.types.at(0);
        emitLvalue(e.retVal);
        of << StringView(" = ");
        if (options.emulatedI128) {
            of << StringView("make128(");
        }
        of << StringView("(uintptr_t)&__typeid_") << TransMangleTypeId(ty);
        if (options.emulatedI128) {
            of << StringView(")");
        }
    } else if (name == "type_name") {
        auto name = localMirRes.intrinsicTypeName(params.types.at(0));
        emitLvalue(e.retVal);
        of << StringView(".PTR = \"") << FmtEscaped(name) << StringView("\";\n\t");
        emitLvalue(e.retVal);
        of << StringView(".META = ") << name.size() << StringView("");
    } else if (name == "transmute" || name == "transmute_unchecked") {
        const auto& tySrc = params.types.at(0);
        const auto& tyDst = params.types.at(1);
        auto isPtr = [](const HIRTypeData* ty) {
            return ty->is_Borrow() || ty->is_Pointer();
        };
        if (this->typeIsBadZst(tyDst)) {
            return;
        }
        if (this->typeIsBadZst(tySrc)) {
            of << StringView("memset(&");
            emitLvalue(e.retVal);
            of << StringView(", 0, sizeof(");
            emitCtype(tyDst);
            of << StringView("))");
        } else if (e.args.at(0).is_Constant()) {
            of << StringView("{ ");
            emitCtype(tySrc, FMT_CB(s, s << StringView("v");));
            of << StringView(" = ");
            emitParam(e.args.at(0));
            of << StringView("; ");
            of << StringView("memcpy(&");
            emitLvalue(e.retVal);
            of << StringView(", &v, sizeof(");
            emitCtype(tyDst);
            of << StringView(")); ");
            of << StringView("}");
        } else if (isPtr(tyDst) && isPtr(tySrc)) {
            auto srcMeta = metadataType(tySrc->is_Pointer() ? tySrc->as_Pointer().inner : tySrc->as_Borrow().inner);
            auto dstMeta = metadataType(tyDst->is_Pointer() ? tyDst->as_Pointer().inner : tyDst->as_Borrow().inner);
            if (srcMeta == MetadataType::None || srcMeta == MetadataType::Zero) {
                MIR_ASSERT(*mirRes, dstMeta == MetadataType::None || dstMeta == MetadataType::Zero, StringView("Transmuting to fat pointer from thin: ") << tySrc << StringView(" -> ") << tyDst);
                emitLvalue(e.retVal);
                of << StringView(" = (");
                emitCtype(tyDst);
                of << StringView(")");
                emitParam(e.args.at(0));
            } else if (dstMeta == MetadataType::None || dstMeta == MetadataType::Zero) {
                MIR_BUG(*mirRes, StringView("Transmuting from fat pointer to thin: (") << srcMeta << StringView("->") << dstMeta << StringView(") ") << tySrc << StringView(" -> ") << tyDst);
            } else if (srcMeta != dstMeta) {
                emitLvalue(e.retVal);
                of << StringView(".PTR = ");
                emitParam(e.args.at(0));
                of << StringView(".PTR; ");
                emitLvalue(e.retVal);
                of << StringView(".META = ");
                switch (dstMeta) {
                    case MetadataType::Unknown:
                        BUG_ASSERT(!"Impossible");
                    case MetadataType::None:
                        BUG_ASSERT(!"Impossible");
                    case MetadataType::Zero:
                        BUG_ASSERT(!"Impossible");
                    case MetadataType::Slice:
                        of << StringView("(size_t)");
                        break;
                    case MetadataType::TraitObject:
                        of << StringView("(const void*)");
                        break;
                }
                emitParam(e.args.at(0));
                of << StringView(".META");
            } else {
                emitLvalue(e.retVal);
                of << StringView(" = ");
                emitParam(e.args.at(0));
            }
        } else {
            of << StringView("memcpy(&");
            emitLvalue(e.retVal);
            of << StringView(", &");
            emitParam(e.args.at(0));
            of << StringView(", sizeof(");
            emitCtype(tySrc);
            of << StringView("))");
        }
    } else if (name == "float_to_int_unchecked") {
        const auto& srcTy = params.types.at(0);
        const auto& dstTy = params.types.at(1);
        if (this->typeIsEmulatedI128(dstTy)) {
            of << StringView("abort()");
        } else if (srcTy == HIRCoreType::F128) {
            emitLvalue(e.retVal);
            of << StringView(" = (");
            emitCtype(dstTy);
            of << StringView(")f128_decode(");
            emitParam(e.args.at(0));
            of << StringView(")");
        } else {
            emitLvalue(e.retVal);
            of << StringView(" = (");
            emitCtype(dstTy);
            of << StringView(")");
            emitParam(e.args.at(0));
        }
    } else if (name == "copy_nonoverlapping" || name == "copy") {
        if (this->typeIsBadZst(params.types.at(0))) {
            return;
        }
        if (name == "copy") {
            of << StringView("memmove");
        } else {
            of << StringView("memcpy");
        }
        of << StringView("(");
        emitParam(e.args.at(1));
        of << StringView(", ");
        emitParam(e.args.at(0));
        of << StringView(", ");
        emitParam(e.args.at(2));
        of << StringView(" * sizeof(");
        emitCtype(params.types.at(0));
        of << StringView(")");
        of << StringView(")");
    } else if (name == "write_bytes") {
        if (this->typeIsBadZst(params.types.at(0))) {
            return;
        }
        of << StringView("if( ");
        emitParam(e.args.at(2));
        of << StringView(" > 0) memset(");
        emitParam(e.args.at(0));
        of << StringView(", ");
        emitParam(e.args.at(1));
        of << StringView(", ");
        emitParam(e.args.at(2));
        of << StringView(" * sizeof(");
        emitCtype(params.types.at(0));
        of << StringView(")");
        of << StringView(")");
    } else if (name == "compare_bytes") {
        emitLvalue(e.retVal);
        of << StringView(" = memcmp(");
        emitParam(e.args.at(0));
        of << StringView(", ");
        emitParam(e.args.at(1));
        of << StringView(", ");
        emitParam(e.args.at(2));
        of << StringView(")");
    } else if (name == "raw_eq") {
        size_t size = 0;
        MIR_ASSERT(localMirRes, TargetGetSizeOf(sp, resolve_, params.types.at(0), size), StringView("Can't get size of ") << params.types.at(0));

        emitLvalue(e.retVal);
        of << StringView(" = (0 == memcmp(");
        emitParam(e.args.at(0));
        of << StringView(", ");
        emitParam(e.args.at(1));
        of << StringView(", ");
        of << size;
        of << StringView("))");
    } else if (name == "three_way_compare") {
        const auto& t = params.types.at(0);
        if (typeIsEmulatedI128(t)) {
            emitLvalue(e.retVal);
            of << StringView(".TAG = ");
            of << (t == HIRCoreType::U128 ? "cmp128" : "cmp128s");
            of << StringView("(");
            emitParam(e.args.at(0));
            of << StringView(", ");
            emitParam(e.args.at(1));
            of << StringView(");\n");
        } else {
            emitLvalue(e.retVal);
            of << StringView(".TAG = (");
            emitParam(e.args.at(0));
            of << StringView(" == ");
            emitParam(e.args.at(1));
            of << StringView(" ? 0 : (");
            emitParam(e.args.at(0));
            of << StringView(" < ");
            emitParam(e.args.at(1));
            of << StringView(" ? -1 : 1));\n");
        }
        return;
    } else if (name == "forget") {
    } else if (name == "async_drop_state") {
        MIR_ASSERT(localMirRes, params.types.size() == 1, StringView("async_drop_state expects its outer future type"));
        const auto* repr = TargetGetTypeRepr(sp, resolve_, params.types[0]);
        MIR_ASSERT(localMirRes, repr && !repr->fields.empty(), StringView("async-drop future has no state field"));
        emitLvalue(e.retVal);
        of << StringView(" = (u8*)((u8*)");
        emitParam(e.args.at(0));
        of << StringView(" + ") << repr->fields[0].offset << StringView(")");
    } else if (name == "async_drop_storage") {
        MIR_ASSERT(localMirRes, params.types.size() == 2, StringView("async_drop_storage expects outer and stored future types"));
        const auto* repr = TargetGetTypeRepr(sp, resolve_, params.types[0]);
        MIR_ASSERT(localMirRes, repr && repr->fields.size() >= 3, StringView("async-drop future has no suspension storage"));
        emitLvalue(e.retVal);
        of << StringView(" = (");
        emitCtype(params.types[1]);
        of << StringView("*)((u8*)");
        emitParam(e.args.at(0));
        of << StringView(" + ") << repr->fields[2].offset << StringView(")");
    } else if (name == "drop_in_place") {
        emitDestructorCall(MIRLValue::newDeref(e.args.at(0).as_LValue().clone()), params.types.at(0), true, /*indent_level=*/1 /* TODO: get from caller */);
    } else if (name == "needs_drop") {
        const auto& ty = params.types.at(0);
        emitLvalue(e.retVal);
        of << StringView(" = ");
        if (resolve_.typeNeedsDropGlue(localMirRes.sp, ty)) {
            of << StringView("true");
        } else {
            of << StringView("false");
        }
    } else if (name == "uninit") {
        // TODO: This makes the C compiler warn
    } else if (name == "init") {
        of << StringView("memset(&");
        emitLvalue(e.retVal);
        of << StringView(", 0, sizeof(");
        emitCtype(params.types.at(0));
        of << StringView("))");
    } else if (name == "move_val_init") {
        if (!this->typeIsBadZst(params.types.at(0))) {
            of << StringView("*");
            emitParam(e.args.at(0));
            of << StringView(" = ");
            emitParam(e.args.at(1));
        }
    } else if (name == "abort") {
        of << StringView("abort()");
    } else if (name == "try" || name == "catch_unwind") {
        of << StringView("{ try { ");
        emitParam(e.args.at(0));
        of << StringView("(");
        emitParam(e.args.at(1));
        of << StringView("); ");
        emitLvalue(e.retVal);
        of << StringView(" = 0; } catch (trustme_panic& panic) { (");
        emitParam(e.args.at(2));
        of << StringView(")(");
        emitParam(e.args.at(1));
        of << StringView(", (u8*)panic.rust_exception); ");
        emitLvalue(e.retVal);
        of << StringView(" = 1; } }");
    } else if (name == "caller_location") {
        MIR_ASSERT(localMirRes, currentFunctionTracksCaller, StringView("`caller_location` used outside a #[track_caller] function"));
        emitLvalue(e.retVal);
        of << StringView(" = (");
        HIRTypeRef callerTypeTmp;
        emitCtype(localMirRes.getLvalueType(callerTypeTmp, e.retVal));
        of << StringView(")trustme_caller");
    } else if (name == "offset") {
        emitLvalue(e.retVal);
        of << StringView(" = ");
        emitParam(e.args.at(0));
        of << StringView(" + ");
        emitParam(e.args.at(1));
    } else if (name == "arith_offset") {
        emitLvalue(e.retVal);
        of << StringView(" = ");
        emitParam(e.args.at(0));
        of << StringView(" + ");
        emitParam(e.args.at(1));
    } else if (name == "ptr_mask") {
        HIRTypeRef tmp;
        const auto& returnType = localMirRes.getLvalueType(tmp, e.retVal);
        MIR_ASSERT(localMirRes, returnType->is_Pointer(), StringView("ptr_mask returned ") << returnType);
        emitLvalue(e.retVal);
        of << StringView(" = (");
        emitCtype(returnType);
        of << StringView(")((uintptr_t)");
        emitParam(e.args.at(0));
        of << StringView(" & (uintptr_t)");
        emitParam(e.args.at(1));
        of << StringView(")");
    } else if (name == "ptr_offset_from") {
        emitLvalue(e.retVal);
        of << StringView(" = ");
        emitParam(e.args.at(0));
        of << StringView(" - ");
        emitParam(e.args.at(1));
    } else if (name == "ptr_guaranteed_eq") {
        emitLvalue(e.retVal);
        of << StringView(" = (");
        emitParam(e.args.at(0));
        of << StringView(" == ");
        emitParam(e.args.at(1));
        of << StringView(")");
    } else if (name == "ptr_guaranteed_ne") {
        emitLvalue(e.retVal);
        of << StringView(" = (");
        emitParam(e.args.at(0));
        of << StringView(" != ");
        emitParam(e.args.at(1));
        of << StringView(")");
    } else if (name == "ptr_guaranteed_cmp") {
        emitLvalue(e.retVal);
        of << StringView("= ( (");
        emitParam(e.args.at(0));
        of << StringView(") == (");
        emitParam(e.args.at(1));
        of << StringView("))");
    } else if (name == "ptr_offset_from_unsigned") {
        emitLvalue(e.retVal);
        of << StringView("= ( (");
        emitParam(e.args.at(0));
        of << StringView(") - (");
        emitParam(e.args.at(1));
        of << StringView("))");
    } else if (name == "bswap") {
        const auto& ty = params.types.at(0);
        MIR_ASSERT(localMirRes, ty->is_Primitive(), StringView("Invalid type passed to bwsap, must be a primitive, got ") << ty);
        if (ty == HIRCoreType::U8 || ty == HIRCoreType::I8) {
            emitLvalue(e.retVal);
            of << StringView(" = ");
            emitParam(e.args.at(0));
        } else if (getPrimSize(ty) == 128) {
            emitLvalue(e.retVal);
            of << StringView(" = ");
            emitWide128Call(ty, "__trustme_bswap128", e.args.at(0));
        } else {
            emitLvalue(e.retVal);
            of << StringView(" = ");
            switch (getPrimSize(ty)) {
                case 16:
                    of << StringView("__builtin_bswap16");
                    break;
                case 32:
                    of << StringView("__builtin_bswap32");
                    break;
                case 64:
                    of << StringView("__builtin_bswap64");
                    break;
                default:
                    MIR_TODO(localMirRes, StringView("bswap<") << ty << StringView(">"));
            }

            of << StringView("(");
            emitParam(e.args.at(0));
            of << StringView(")");
        }
    } else if (name == "bitreverse") {
        const auto& ty = params.types.at(0);
        MIR_ASSERT(localMirRes, ty->is_Primitive(), StringView("Invalid type passed to bitreverse. Must be a primitive, got ") << ty);
        emitLvalue(e.retVal);
        of << StringView(" = ");
        if (getPrimSize(ty) == 128) {
            emitWide128Call(ty, "__trustme_bitrev128", e.args.at(0));
        } else {
            switch (getPrimSize(ty)) {
                case 8:
                    of << StringView("__trustme_bitrev8");
                    break;
                case 16:
                    of << StringView("__trustme_bitrev16");
                    break;
                case 32:
                    of << StringView("__trustme_bitrev32");
                    break;
                case 64:
                    of << StringView("__trustme_bitrev64");
                    break;
                default:
                    MIR_TODO(localMirRes, StringView("bitreverse<") << ty << StringView(">"));
            }
            of << StringView("(");
            emitParam(e.args.at(0));
            of << StringView(")");
        }
    } else if (name == "discriminant_value") {
        const auto& ty = params.types.at(0);
        emitLvalue(e.retVal);
        of << StringView(" = ");
        if (ty->is_Path() && (ty->as_Path().isGenerator() || ty->as_Path().isFuture())) {
            auto state = [&]() -> MIRLValue {
                if (const auto* value = e.args.at(0).opt_LValue()) {
                    return MIRLValue::newDeref(value->clone());
                }
                if (const auto* value = e.args.at(0).opt_Borrow()) {
                    return value->val.clone();
                }
                MIR_BUG(localMirRes, StringView("Generator passed to `discriminant_value` by constant: ") << e.args.at(0));
            }();
            state = MIRLValue::newField(mv$(state), 0);
            state = MIRLValue::newDowncast(mv$(state), 1);
            state = MIRLValue::newField(mv$(state), 0);
            state = MIRLValue::newField(mv$(state), 0);
            emitLvalue(state);
            of << StringView(".TAG");
        } else if (!(ty->is_Path() && ty->as_Path().binding.is_Enum())) {
            of << StringView("0");
        } else {
            const auto* repr = TargetGetTypeRepr(sp, resolve_, ty);
            MIR_ASSERT(localMirRes, repr, StringView("No repr for enum ") << ty);
            switch (repr->variants.tag()) {
                break;
                case TypeReprVariantMode::TAG_None: {
                    of << StringView("0");
                } break;
                    break;
                case TypeReprVariantMode::TAG_Values: {
                    auto& ve = repr->variants.as_Values();
                    of << StringView("(*");
                    emitParam(e.args.at(0));
                    of << StringView(")");
                    emitEnumPath(repr, ve.field);
                } break;
                    break;
                case TypeReprVariantMode::TAG_Linear: {
                    auto& ve = repr->variants.as_Linear();
                    const auto& tagTy = TargetGetInnerType(sp, resolve_, *repr, ve.field.index, ve.field.subFields);
                    const bool pointerTag = tagTy->is_Pointer() || tagTy->is_Borrow() || tagTy->is_Function();
                    auto emitTag = [&]() {
                        if (pointerTag) {
                            of << StringView("(uintptr_t)");
                        } else {
                            of << StringView("(") << tagUnsignedType(ve.field.size) << StringView(")");
                        }
                        of << StringView("(*");
                        emitParam(e.args.at(0));
                        of << StringView(")");
                        emitEnumPath(repr, ve.field);
                    };
                    if (ve.usesNiche()) {
                        const auto start = tagBits(ve.field.size, ve.offset);
                        of << StringView("( ");
                        emitTag();
                        of << StringView(" >= ") << start << StringView("ull && ");
                        emitTag();
                        of << StringView(" < ") << (start + ve.nicheVariantCount()) << StringView("ull");
                        of << StringView(" ? ") << ve.nicheVariantStart() << StringView(" + ");
                        emitTag();
                        of << StringView(" - ") << start << StringView("ull");
                        of << StringView(" : ");
                        of << ve.field.index;
                        of << StringView(" )");
                    } else {
                        emitTag();
                    }
                } break;
                    break;
                case TypeReprVariantMode::TAG_NonZero: {
                    auto& ve = repr->variants.as_NonZero();
                    of << StringView("(*");
                    emitParam(e.args.at(0));
                    of << StringView(")");
                    emitEnumPath(repr, ve.field);
                    of << StringView(" ");
                    of << StringView(ve.zeroVariant ? "==" : "!=");
                    of << StringView(" 0");
                } break;
            }
        }
    } else if (name == "unreachable") {
        of << StringView("__builtin_unreachable()");
    } else if (name == "assume") {
    } else if (name == "likely" || name == "unlikely") {
        emitLvalue(e.retVal);
        of << StringView("= (");
        emitParam(e.args.at(0));
        of << StringView(")");
    } else if (name == "black_box") {
        if (!lvalueIsBadZst(e.retVal)) {
            emitLvalue(e.retVal);
            of << StringView("= (");
            emitParam(e.args.at(0));
            of << StringView(")");
        }
    } else if (name == "add_with_overflow") {
        if (options.emulatedI128 && params.types.at(0) == HIRCoreType::U128) {
            emitLvalue(e.retVal);
            of << StringView("._1 = add128_o");
            of << StringView("(");
            emitParam(e.args.at(0));
            of << StringView(", ");
            emitParam(e.args.at(1));
            of << StringView(", &");
            emitLvalue(e.retVal);
            of << StringView("._0)");
        } else if (options.emulatedI128 && params.types.at(0) == HIRCoreType::I128) {
            emitLvalue(e.retVal);
            of << StringView("._1 = add128s_o");
            of << StringView("(");
            emitParam(e.args.at(0));
            of << StringView(", ");
            emitParam(e.args.at(1));
            of << StringView(", &");
            emitLvalue(e.retVal);
            of << StringView("._0)");
        } else {
            emitLvalue(e.retVal);
            of << StringView("._1 = __builtin_add_overflow");
            of << StringView("(");
            emitParam(e.args.at(0));
            of << StringView(", ");
            emitParam(e.args.at(1));
            of << StringView(", &");
            emitLvalue(e.retVal);
            of << StringView("._0)");
        }
    } else if (name == "sub_with_overflow") {
        if (options.emulatedI128 && params.types.at(0) == HIRCoreType::U128) {
            emitLvalue(e.retVal);
            of << StringView("._1 = sub128_o");
            of << StringView("(");
            emitParam(e.args.at(0));
            of << StringView(", ");
            emitParam(e.args.at(1));
            of << StringView(", &");
            emitLvalue(e.retVal);
            of << StringView("._0)");
        } else if (options.emulatedI128 && params.types.at(0) == HIRCoreType::I128) {
            emitLvalue(e.retVal);
            of << StringView("._1 = sub128s_o");
            of << StringView("(");
            emitParam(e.args.at(0));
            of << StringView(", ");
            emitParam(e.args.at(1));
            of << StringView(", &");
            emitLvalue(e.retVal);
            of << StringView("._0)");
        } else {
            emitLvalue(e.retVal);
            of << StringView("._1 = __builtin_sub_overflow");
            of << StringView("(");
            emitParam(e.args.at(0));
            of << StringView(", ");
            emitParam(e.args.at(1));
            of << StringView(", &");
            emitLvalue(e.retVal);
            of << StringView("._0)");
        }
    } else if (name == "mul_with_overflow") {
        if (options.emulatedI128 && params.types.at(0) == HIRCoreType::U128) {
            emitLvalue(e.retVal);
            of << StringView("._1 = mul128_o");
            of << StringView("(");
            emitParam(e.args.at(0));
            of << StringView(", ");
            emitParam(e.args.at(1));
            of << StringView(", &");
            emitLvalue(e.retVal);
            of << StringView("._0)");
        } else if (options.emulatedI128 && params.types.at(0) == HIRCoreType::I128) {
            emitLvalue(e.retVal);
            of << StringView("._1 = mul128s_o");
            of << StringView("(");
            emitParam(e.args.at(0));
            of << StringView(", ");
            emitParam(e.args.at(1));
            of << StringView(", &");
            emitLvalue(e.retVal);
            of << StringView("._0)");
        } else {
            emitLvalue(e.retVal);
            of << StringView("._1 = __builtin_mul_overflow(");
            emitParam(e.args.at(0));
            of << StringView(", ");
            emitParam(e.args.at(1));
            of << StringView(", &");
            emitLvalue(e.retVal);
            of << StringView("._0)");
        }
    } else if (name == "overflowing_add" || name == "wrapping_add" || name == "saturating_add" || name == "unchecked_add") {
        const auto& ty = params.types.at(0);
        if (name == "saturating_add") {
            of << StringView("if( ");
        }

        if (options.emulatedI128 && ty == HIRCoreType::U128) {
            of << StringView("add128_o");
            of << StringView("(");
            emitParam(e.args.at(0));
            of << StringView(", ");
            emitParam(e.args.at(1));
            of << StringView(", &");
            emitLvalue(e.retVal);
            of << StringView(")");
        } else if (options.emulatedI128 && ty == HIRCoreType::I128) {
            of << StringView("add128s_o");
            of << StringView("(");
            emitParam(e.args.at(0));
            of << StringView(", ");
            emitParam(e.args.at(1));
            of << StringView(", &");
            emitLvalue(e.retVal);
            of << StringView(")");
        } else {
            of << StringView("__builtin_add_overflow");
            of << StringView("(");
            emitParam(e.args.at(0));
            of << StringView(", ");
            emitParam(e.args.at(1));
            of << StringView(", &");
            emitLvalue(e.retVal);
            of << StringView(")");
        }

        if (name == "saturating_add") {
            of << StringView(") { ");
            emitLvalue(e.retVal);
            of << StringView(" = ");
            switch (getRealPrimTy(ty->as_Primitive())) {
                case HIRCoreType::U8:
                case HIRCoreType::U16:
                case HIRCoreType::U32:
                case HIRCoreType::U64:
                    of << StringView("-1");
                    break;
                case HIRCoreType::U128:
                    if (options.emulatedI128) {
                        of << StringView("make128_raw(-1, -1)");
                    } else {
                        of << StringView("-1");
                    }
                    break;
                case HIRCoreType::I8:
                    of << StringView("(");
                    emitParam(e.args.at(0));
                    of << StringView(" < 0 ? -0x80 : 0x7F)");
                    break;
                case HIRCoreType::I16:
                    of << StringView("(");
                    emitParam(e.args.at(0));
                    of << StringView(" < 0 ? -0x8000 : 0x7FFF)");
                    break;
                case HIRCoreType::I32:
                    of << StringView("(");
                    emitParam(e.args.at(0));
                    of << StringView(" < 0 ? (-0x7FFFFFFFl - 1) : 0x7FFFFFFFl)");
                    break;
                case HIRCoreType::I64:
                    of << StringView("(");
                    emitParam(e.args.at(0));
                    of << StringView(" < 0 ? (-0x7FFFFFFF"
                          "FFFFFFFFll - 1) : 0x7FFFFFFF"
                          "FFFFFFFFll)");
                    break;
                case HIRCoreType::I128:
                    if (options.emulatedI128) {
                        of << StringView("( (i64)(");
                        emitParam(e.args.at(0));
                        of << StringView(".hi) < 0 ? make128s_raw(-0x7FFFFFFF"
                              "FFFFFFFFll - 1, 0) : make128s_raw(0x7FFFFFFF"
                              "FFFFFFFFll, -1))");
                    } else {
                        of << StringView("(");
                        emitParam(e.args.at(0));
                        of << StringView(" < 0 ? ((uint128_t)1 << 127) : (((uint128_t)1 << 127) - 1))");
                    }
                    break;
                default:
                    MIR_TODO(localMirRes, StringView("saturating_add - ") << ty);
            }
            of << StringView("; }");
        }
    } else if (name == "overflowing_sub" || name == "wrapping_sub" || name == "saturating_sub" || name == "unchecked_sub") {
        const auto& ty = params.types.at(0);
        if (name == "saturating_sub") {
            of << StringView("if( ");
        }
        if (options.emulatedI128 && ty == HIRCoreType::U128) {
            of << StringView("sub128_o");
            of << StringView("(");
            emitParam(e.args.at(0));
            of << StringView(", ");
            emitParam(e.args.at(1));
            of << StringView(", &");
            emitLvalue(e.retVal);
            of << StringView(")");
        } else if (options.emulatedI128 && ty == HIRCoreType::I128) {
            of << StringView("sub128s_o");
            of << StringView("(");
            emitParam(e.args.at(0));
            of << StringView(", ");
            emitParam(e.args.at(1));
            of << StringView(", &");
            emitLvalue(e.retVal);
            of << StringView(")");
        } else {
            of << StringView("__builtin_sub_overflow");
            of << StringView("(");
            emitParam(e.args.at(0));
            of << StringView(", ");
            emitParam(e.args.at(1));
            of << StringView(", &");
            emitLvalue(e.retVal);
            of << StringView(")");
        }

        if (name == "saturating_sub") {
            of << StringView(") { ");
            emitLvalue(e.retVal);
            of << StringView(" = ");
            switch (getRealPrimTy(ty->as_Primitive())) {
                case HIRCoreType::U8:
                case HIRCoreType::U16:
                case HIRCoreType::U32:
                case HIRCoreType::U64:
                    of << StringView("0");
                    break;
                case HIRCoreType::U128:
                    if (options.emulatedI128) {
                        of << StringView("make128(0)");
                    } else {
                        of << StringView("0");
                    }
                    break;
                case HIRCoreType::I8:
                    of << StringView("(");
                    emitParam(e.args.at(0));
                    of << StringView(" < 0 ? -0x80 : 0x7F)");
                    break;
                case HIRCoreType::I16:
                    of << StringView("(");
                    emitParam(e.args.at(0));
                    of << StringView(" < 0 ? -0x8000 : 0x7FFF)");
                    break;
                case HIRCoreType::I32:
                    of << StringView("(");
                    emitParam(e.args.at(0));
                    of << StringView(" < 0 ? (-0x7FFFFFFFl - 1) : 0x7FFFFFFFl)");
                    break;
                case HIRCoreType::I64:
                    of << StringView("(");
                    emitParam(e.args.at(0));
                    of << StringView(" < 0 ? (-0x7FFFFFFF"
                          "FFFFFFFFll - 1) : 0x7FFFFFFF"
                          "FFFFFFFFll)");
                    break;
                case HIRCoreType::I128:
                    if (options.emulatedI128) {
                        of << StringView("( (i64)(");
                        emitParam(e.args.at(0));
                        of << StringView(".hi) < 0 ? make128s_raw(-0x7FFFFFFF"
                              "FFFFFFFFll - 1, 0) : make128s_raw(0x7FFFFFFF"
                              "FFFFFFFFll, -1))");
                    } else {
                        of << StringView("(");
                        emitParam(e.args.at(0));
                        of << StringView(" < 0 ? ((uint128_t)1 << 127) : (((uint128_t)1 << 127) - 1))");
                    }
                    break;
                default:
                    MIR_TODO(localMirRes, StringView("saturating_sub - ") << ty);
            }
            of << StringView("; }");
        }
    } else if (name == "overflowing_mul" || name == "wrapping_mul" || name == "unchecked_mul") {
        if (options.emulatedI128 && params.types.at(0) == HIRCoreType::U128) {
            of << StringView("mul128_o");
            of << StringView("(");
            emitParam(e.args.at(0));
            of << StringView(", ");
            emitParam(e.args.at(1));
            of << StringView(", &");
            emitLvalue(e.retVal);
            of << StringView(")");
        } else if (options.emulatedI128 && params.types.at(0) == HIRCoreType::I128) {
            of << StringView("mul128s_o");
            of << StringView("(");
            emitParam(e.args.at(0));
            of << StringView(", ");
            emitParam(e.args.at(1));
            of << StringView(", &");
            emitLvalue(e.retVal);
            of << StringView(")");
        } else {
            of << StringView("__builtin_mul_overflow");
            of << StringView("(");
            emitParam(e.args.at(0));
            of << StringView(", ");
            emitParam(e.args.at(1));
            of << StringView(", &");
            emitLvalue(e.retVal);
            of << StringView(")");
        }
    } else if (name == "unchecked_div" || name == "exact_div") {
        emitLvalue(e.retVal);
        of << StringView(" = ");
        if (typeIsEmulatedI128(params.types.at(0))) {
            of << StringView("div128");
            if (params.types.at(0) == HIRCoreType::I128) {
                of << StringView("s");
            }
            of << StringView("(");
            emitParam(e.args.at(0));
            of << StringView(", ");
            emitParam(e.args.at(1));
            of << StringView(")");
        } else {
            emitParam(e.args.at(0));
            of << StringView(" / ");
            emitParam(e.args.at(1));
        }
    } else if (name == "unchecked_rem") {
        emitLvalue(e.retVal);
        of << StringView(" = ");
        if (typeIsEmulatedI128(params.types.at(0))) {
            of << StringView("mod128");
            if (params.types.at(0) == HIRCoreType::I128) {
                of << StringView("s");
            }
            of << StringView("(");
            emitParam(e.args.at(0));
            of << StringView(", ");
            emitParam(e.args.at(1));
            of << StringView(")");
        } else {
            emitParam(e.args.at(0));
            of << StringView(" % ");
            emitParam(e.args.at(1));
        }
    } else if (name == "unchecked_shl") {
        emitLvalue(e.retVal);
        of << StringView(" = ");
        if (typeIsEmulatedI128(params.types.at(0))) {
            of << StringView("shl128");
            if (params.types.at(0) == HIRCoreType::I128) {
                of << StringView("s");
            }
            of << StringView("(");
            emitParam(e.args.at(0));
            of << StringView(", ");
            emitParam(e.args.at(1));
            HIRTypeRef tmp;
            const auto& shiftTy = localMirRes.getParamType(tmp, e.args.at(1));
            if (shiftTy == HIRCoreType::I128 || shiftTy == HIRCoreType::U128) {
                of << StringView(".lo");
            }
            of << StringView(")");
        } else {
            emitParam(e.args.at(0));
            of << StringView(" << ");
            emitParam(e.args.at(1));
        }
    } else if (name == "unchecked_shr") {
        emitLvalue(e.retVal);
        of << StringView(" = ");
        if (typeIsEmulatedI128(params.types.at(0))) {
            of << StringView("shr128");
            if (params.types.at(0) == HIRCoreType::I128) {
                of << StringView("s");
            }
            of << StringView("(");
            emitParam(e.args.at(0));
            of << StringView(", ");
            emitParam(e.args.at(1));
            HIRTypeRef tmp;
            const auto& shiftTy = localMirRes.getParamType(tmp, e.args.at(1));
            if (shiftTy == HIRCoreType::I128 || shiftTy == HIRCoreType::U128) {
                of << StringView(".lo");
            }
            of << StringView(")");
        } else {
            emitParam(e.args.at(0));
            of << StringView(" >> ");
            emitParam(e.args.at(1));
        }
    } else if (name == "rotate_left") {
        const auto& ty = params.types.at(0);
        switch (getRealPrimTy(ty->as_Primitive())) {
            case HIRCoreType::I8:
            case HIRCoreType::U8:
                of << StringView("{");
                of << StringView(" u8 v = ");
                emitParam(e.args.at(0));
                of << StringView(";");
                of << StringView(" unsigned shift = ");
                emitParam(e.args.at(1));
                of << StringView(" % 8;");
                of << StringView(" ");
                emitLvalue(e.retVal);
                of << StringView(" = shift == 0 ? v : (v << shift) | (v >> (8 - shift));");
                of << StringView("}");
                break;
            case HIRCoreType::I16:
            case HIRCoreType::U16:
                of << StringView("{");
                of << StringView(" u16 v = ");
                emitParam(e.args.at(0));
                of << StringView(";");
                of << StringView(" unsigned shift = ");
                emitParam(e.args.at(1));
                of << StringView(" % 16;");
                of << StringView(" ");
                emitLvalue(e.retVal);
                of << StringView(" = shift == 0 ? v : (v << shift) | (v >> (16 - shift));");
                of << StringView("}");
                break;
            case HIRCoreType::I32:
            case HIRCoreType::U32:
                of << StringView("{");
                of << StringView(" u32 v = ");
                emitParam(e.args.at(0));
                of << StringView(";");
                of << StringView(" unsigned shift = ");
                emitParam(e.args.at(1));
                of << StringView(" % 32;");
                of << StringView(" ");
                emitLvalue(e.retVal);
                of << StringView(" = shift == 0 ? v : (v << shift) | (v >> (32 - shift));");
                of << StringView("}");
                break;
            case HIRCoreType::I64:
            case HIRCoreType::U64:
                of << StringView("{");
                of << StringView(" u64 v = ");
                emitParam(e.args.at(0));
                of << StringView(";");
                of << StringView(" unsigned shift = ");
                emitParam(e.args.at(1));
                of << StringView(" % 64;");
                of << StringView(" ");
                emitLvalue(e.retVal);
                of << StringView(" = shift == 0 ? v : (v << shift) | (v >> (64 - shift));");
                of << StringView("}");
                break;
            case HIRCoreType::I128:
            case HIRCoreType::U128:
                of << StringView("{");
                of << StringView(" uint128_t v = ");
                emitParam(e.args.at(0));
                of << StringView(";");
                of << StringView(" unsigned shift = ");
                emitParam(e.args.at(1));
                of << StringView(" % 128;");
                if (options.emulatedI128) {
                    of << StringView(" if(shift == 0) {");
                    of << StringView(" ");
                    emitLvalue(e.retVal);
                    of << StringView(" = v;");
                    of << StringView(" } else if(shift < 64) {");
                    of << StringView(" ");
                    emitLvalue(e.retVal);
                    of << StringView(".lo = (v.lo << shift) | (v.hi >> (64 - shift));");
                    of << StringView(" ");
                    emitLvalue(e.retVal);
                    of << StringView(".hi = (v.hi << shift) | (v.lo >> (64 - shift));");
                    of << StringView(" } else if(shift == 64) {");
                    of << StringView(" ");
                    emitLvalue(e.retVal);
                    of << StringView(".lo = v.hi;");
                    of << StringView(" ");
                    emitLvalue(e.retVal);
                    of << StringView(".hi = v.lo;");
                    of << StringView(" } else {");
                    of << StringView(" shift -= 64;");
                    of << StringView(" ");
                    emitLvalue(e.retVal);
                    of << StringView(".lo = (v.hi << shift) | (v.lo >> (64 - shift));");
                    of << StringView(" ");
                    emitLvalue(e.retVal);
                    of << StringView(".hi = (v.lo << shift) | (v.hi >> (64 - shift));");
                    of << StringView(" }");
                } else {
                    of << StringView(" ");
                    emitLvalue(e.retVal);
                    of << StringView(" = shift == 0 ? v : (v << shift) | (v >> (128 - shift));");
                }
                of << StringView("}");
                break;
            default:
                MIR_TODO(localMirRes, StringView("rotate_left - ") << ty);
        }
    } else if (name == "rotate_right") {
        const auto& ty = params.types.at(0);
        switch (getRealPrimTy(ty->as_Primitive())) {
            case HIRCoreType::I8:
            case HIRCoreType::U8:
                of << StringView("{");
                of << StringView(" u8 v = ");
                emitParam(e.args.at(0));
                of << StringView(";");
                of << StringView(" unsigned shift = ");
                emitParam(e.args.at(1));
                of << StringView(" % 8;");
                of << StringView(" ");
                emitLvalue(e.retVal);
                of << StringView(" = shift == 0 ? v : (v >> shift) | (v << (8 - shift));");
                of << StringView("}");
                break;
            case HIRCoreType::I16:
            case HIRCoreType::U16:
                of << StringView("{");
                of << StringView(" u16 v = ");
                emitParam(e.args.at(0));
                of << StringView(";");
                of << StringView(" unsigned shift = ");
                emitParam(e.args.at(1));
                of << StringView(" % 16;");
                of << StringView(" ");
                emitLvalue(e.retVal);
                of << StringView(" = shift == 0 ? v : (v >> shift) | (v << (16 - shift));");
                of << StringView("}");
                break;
            case HIRCoreType::I32:
            case HIRCoreType::U32:
                of << StringView("{");
                of << StringView(" u32 v = ");
                emitParam(e.args.at(0));
                of << StringView(";");
                of << StringView(" unsigned shift = ");
                emitParam(e.args.at(1));
                of << StringView(" % 32;");
                of << StringView(" ");
                emitLvalue(e.retVal);
                of << StringView(" = shift == 0 ? v : (v >> shift) | (v << (32 - shift));");
                of << StringView("}");
                break;
            case HIRCoreType::I64:
            case HIRCoreType::U64:
                of << StringView("{");
                of << StringView(" u64 v = ");
                emitParam(e.args.at(0));
                of << StringView(";");
                of << StringView(" unsigned shift = ");
                emitParam(e.args.at(1));
                of << StringView(" % 64;");
                of << StringView(" ");
                emitLvalue(e.retVal);
                of << StringView(" = shift == 0 ? v : (v >> shift) | (v << (64 - shift));");
                of << StringView("}");
                break;
            case HIRCoreType::I128:
            case HIRCoreType::U128:
                of << StringView("{");
                of << StringView(" uint128_t v = ");
                emitParam(e.args.at(0));
                of << StringView(";");
                of << StringView(" unsigned shift = ");
                emitParam(e.args.at(1));
                of << StringView(" % 128;");
                if (options.emulatedI128) {
                    of << StringView(" if(shift == 0) {");
                    of << StringView(" ");
                    emitLvalue(e.retVal);
                    of << StringView(" = v;");
                    of << StringView(" } else if(shift < 64) {");
                    of << StringView(" ");
                    emitLvalue(e.retVal);
                    of << StringView(".lo = (v.lo >> shift) | (v.hi << (64 - shift));");
                    of << StringView(" ");
                    emitLvalue(e.retVal);
                    of << StringView(".hi = (v.hi >> shift) | (v.lo << (64 - shift));");
                    of << StringView(" } else if(shift == 64) {");
                    of << StringView(" ");
                    emitLvalue(e.retVal);
                    of << StringView(".lo = v.hi;");
                    of << StringView(" ");
                    emitLvalue(e.retVal);
                    of << StringView(".hi = v.lo;");
                    of << StringView(" } else {");
                    of << StringView(" shift -= 64;");
                    of << StringView(" ");
                    emitLvalue(e.retVal);
                    of << StringView(".lo = (v.hi >> shift) | (v.lo << (64 - shift));");
                    of << StringView(" ");
                    emitLvalue(e.retVal);
                    of << StringView(".hi = (v.lo >> shift) | (v.hi << (64 - shift));");
                    of << StringView(" }");
                } else {
                    of << StringView(" ");
                    emitLvalue(e.retVal);
                    of << StringView(" = shift == 0 ? v : (v >> shift) | (v << (128 - shift));");
                }
                of << StringView("}");
                break;
            default:
                MIR_TODO(localMirRes, StringView("rotate_right - ") << ty);
        }
    } else if (name == "ctlz" || name == "ctlz_nonzero" || name == "cttz" || name == "cttz_nonzero") {
        auto emitArg0 = [&]() {
            emitParam(e.args.at(0));
        };
        const auto& ty = params.types.at(0);
        emitLvalue(e.retVal);
        of << StringView(" = (");
        if (ty == HIRCoreType::U128 || ty == HIRCoreType::I128) {
            if (ty == HIRCoreType::I128) {
                if (options.emulatedI128) {
                    of << StringView("uint128_to_int128(");
                } else {
                    of << StringView("(int128_t)");
                }
            }
            if (name == "ctlz" || name == "ctlz_nonzero") {
                of << StringView("intrinsic_ctlz_u128(");
            } else {
                of << StringView("intrinsic_cttz_u128(");
            }
            if (ty == HIRCoreType::I128) {
                if (options.emulatedI128) {
                    of << StringView("int128_to_uint128(");
                } else {
                    of << StringView("(uint128_t)");
                }
            }
            emitParam(e.args.at(0));
            of << StringView(")");
            if (ty == HIRCoreType::I128 && options.emulatedI128) {
                of << StringView(")");
                of << StringView(")");
            } else {
            }
            of << StringView(")");
            if (options.emulatedI128) {
                of << StringView(".lo");
            }
            of << StringView(";");
            return;
        } else if (ty == HIRCoreType::U64 || ty == HIRCoreType::I64 || ((ty == HIRCoreType::Usize || ty == HIRCoreType::Isize) && TargetGetPointerBits() > 32)) {
            emitParam(e.args.at(0));
            of << StringView(" != 0 ? ");
            if (name == "ctlz" || name == "ctlz_nonzero") {
                of << StringView("__builtin_clz64(");
                emitArg0();
                of << StringView(")");
            } else {
                of << StringView("__builtin_ctz64(");
                emitArg0();
                of << StringView(")");
            }
        } else {
            emitParam(e.args.at(0));
            of << StringView(" != 0 ? ");
            if (name == "ctlz" || name == "ctlz_nonzero") {
                of << StringView("__builtin_clz(");
                if (ty == HIRCoreType::U8 || ty == HIRCoreType::I8) {
                    of << StringView("(u8)(");
                } else if (ty == HIRCoreType::U16 || ty == HIRCoreType::I16) {
                    of << StringView("(u16)(");
                }
                emitParam(e.args.at(0));
                if (ty == HIRCoreType::U8 || ty == HIRCoreType::I8 || ty == HIRCoreType::U16 || ty == HIRCoreType::I16) {
                    of << StringView(")");
                }
                of << StringView(")");
                if (ty == HIRCoreType::U8 || ty == HIRCoreType::I8) {
                    of << StringView(" - 24");
                } else if (ty == HIRCoreType::U16 || ty == HIRCoreType::I16) {
                    of << StringView(" - 16");
                }
            } else {
                of << StringView("__builtin_ctz(");
                emitParam(e.args.at(0));
                of << StringView(")");
            }
        }
        of << StringView(" : sizeof(");
        emitCtype(ty);
        of << StringView(")*8)");
    } else if (name == "ctpop") {
        const auto& ty = params.types.at(0);
        emitLvalue(e.retVal);
        of << StringView(" = ");

        if (ty == HIRCoreType::I128 || ty == HIRCoreType::U128) {
            of << StringView("popcount128(");
            if (ty == HIRCoreType::I128) {
                if (options.emulatedI128) {
                    of << StringView("int128_to_uint128(");
                } else {
                    of << StringView("(uint128_t)(");
                }
            }
            emitParam(e.args.at(0));
            if (ty == HIRCoreType::I128) {
                of << StringView(")");
            }
            of << StringView(")");
            if (options.emulatedI128) {
                of << StringView(".lo");
            }
        } else {
            of << StringView("__builtin_popcountll(");
            of << StringView("(u") << getPrimSize(ty) << StringView(")(");
            emitParam(e.args.at(0));
            of << StringView("))");
        }
    } else if (name == "fadd_fast" || name == "fsub_fast" || name == "fmul_fast" || name == "fdiv_fast" || name == "frem_fast") {
        const auto& ty = params.types.at(0);
        MIR_ASSERT(localMirRes, ty->is_Primitive(), StringView("Fast float intrinsic instantiated with ") << ty);
        const auto coreTy = ty->as_Primitive();
        MIR_ASSERT(localMirRes, coreTy == HIRCoreType::F16 || coreTy == HIRCoreType::F32 || coreTy == HIRCoreType::F64 || coreTy == HIRCoreType::F128, StringView("Fast float intrinsic instantiated with ") << ty);

        emitLvalue(e.retVal);
        of << StringView(" = ");
        if (coreTy == HIRCoreType::F128) {
            of << StringView("f128_");
            if (name == "fadd_fast") {
                of << StringView("add");
            } else if (name == "fsub_fast") {
                of << StringView("sub");
            } else if (name == "fmul_fast") {
                of << StringView("mul");
            } else if (name == "fdiv_fast") {
                of << StringView("div");
            } else {
                of << StringView("mod");
            }
            of << StringView("(");
            emitParam(e.args.at(0));
            of << StringView(", ");
            emitParam(e.args.at(1));
            of << StringView(")");
        } else if (name == "frem_fast") {
            of << (coreTy == HIRCoreType::F64 ? "__builtin_fmod" : "__builtin_fmodf") << StringView("(");
            emitParam(e.args.at(0));
            of << StringView(", ");
            emitParam(e.args.at(1));
            of << StringView(")");
        } else {
            of << StringView("(");
            emitParam(e.args.at(0));
            if (name == "fadd_fast") {
                of << StringView(" + ");
            } else if (name == "fsub_fast") {
                of << StringView(" - ");
            } else if (name == "fmul_fast") {
                of << StringView(" * ");
            } else {
                of << StringView(" / ");
            }
            emitParam(e.args.at(1));
            of << StringView(")");
        }
    } else if ((name.size() > 3 && name.compare(name.size() - 3, 3, "f16") == 0) || (name.size() > 3 && name.compare(name.size() - 3, 3, "f32") == 0) || (name.size() > 3 && name.compare(name.size() - 3, 3, "f64") == 0) || (name.size() > 4 && name.compare(name.size() - 4, 4, "f128") == 0)) {
        const bool isF16 = name.compare(name.size() - 3, 3, "f16") == 0;
        const bool isF128 = name.size() > 4 && name.compare(name.size() - 4, 4, "f128") == 0;
        auto emitMathName = [&](const char* op) {
            of << StringView("__builtin_");
            of << op << (isF16 || name.back() == '2' ? "f" : "");
        };
        auto emit1 = [&](const char* op) {
            emitLvalue(e.retVal);
            of << StringView(" = ");
            if (isF128) {
                of << StringView("f128_") << op;
            } else {
                emitMathName(op);
            }
            of << StringView("(");
            emitParam(e.args.at(0));
            of << StringView(")");
        };
        if (name == "rintf16" || name == "rintf32" || name == "rintf64" || name == "rintf128") {
            emit1("round");
        } else if (name == "round_ties_even_f16" || name == "round_ties_even_f32" || name == "round_ties_even_f64" || name == "round_ties_even_f128") {
            emit1(isF128 ? "round_even" : "roundeven");
        } else if (name == "fabsf16" || name == "fabsf32" || name == "fabsf64" || name == "fabsf128") {
            emit1(isF128 ? "abs" : "fabs");
        } else if (name == "copysignf16" || name == "copysignf32" || name == "copysignf64" || name == "copysignf128") {
            emitLvalue(e.retVal);
            of << StringView(" = ");
            if (isF128) {
                of << StringView("f128_copysign");
            } else {
                emitMathName("copysign");
            }
            of << StringView("(");
            emitParam(e.args.at(0));
            of << StringView(", ");
            emitParam(e.args.at(1));
            of << StringView(")");
        } else if (name == "truncf16" || name == "truncf32" || name == "truncf64" || name == "truncf128") {
            emit1("trunc");
        } else if (name == "powif16" || name == "powif32" || name == "powif64" || name == "powif128") {
            emitLvalue(e.retVal);
            of << StringView(" = ");
            if (isF128) {
                of << StringView("f128_powi");
            } else {
                emitMathName("pow");
            }
            of << StringView("(");
            emitParam(e.args.at(0));
            of << StringView(", ");
            emitParam(e.args.at(1));
            of << StringView(")");
        } else if (name == "powf16" || name == "powf32" || name == "powf64" || name == "powf128") {
            emitLvalue(e.retVal);
            of << StringView(" = ");
            if (isF128) {
                of << StringView("f128_pow");
            } else {
                emitMathName("pow");
            }
            of << StringView("(");
            emitParam(e.args.at(0));
            of << StringView(", ");
            emitParam(e.args.at(1));
            of << StringView(")");
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
            of << StringView(" = ");
            if (isF128) {
                of << StringView("f128_fma");
            } else {
                emitMathName("fma");
            }
            of << StringView("(");
            emitParam(e.args.at(0));
            of << StringView(", ");
            emitParam(e.args.at(1));
            of << StringView(", ");
            emitParam(e.args.at(2));
            of << StringView(")");
        } else if (name == "maxnumf16" || name == "maxnumf32" || name == "maxnumf64" || name == "maxnumf128") {
            emitLvalue(e.retVal);
            of << StringView(" = ");
            if (isF128) {
                of << StringView("f128_max");
            } else {
                emitMathName("fmax");
            }
            of << StringView("(");
            emitParam(e.args.at(0));
            of << StringView(", ");
            emitParam(e.args.at(1));
            of << StringView(")");
        } else if (name == "minnumf16" || name == "minnumf32" || name == "minnumf64" || name == "minnumf128") {
            emitLvalue(e.retVal);
            of << StringView(" = ");
            if (isF128) {
                of << StringView("f128_min");
            } else {
                emitMathName("fmin");
            }
            of << StringView("(");
            emitParam(e.args.at(0));
            of << StringView(", ");
            emitParam(e.args.at(1));
            of << StringView(")");
        } else {
            MIR_BUG(localMirRes, StringView("Unknown float intrinsic '") << name << StringView("'"));
        }
    } else if (name == "volatile_load") {
        if (!this->typeIsBadZst(params.types.at(0))) {
            if (this->typeIsCScalar(params.types.at(0))) {
                emitLvalue(e.retVal);
                of << StringView(" = *(volatile ");
                emitCtype(params.types.at(0));
                of << StringView("*)");
                emitParam(e.args.at(0));
            } else {
                size_t valueSize = 0;
                MIR_ASSERT(localMirRes, TargetGetSizeOf(sp, resolve_, params.types.at(0), valueSize), StringView("Can't get size of ") << params.types.at(0));
                of << StringView("__trustme_unaligned_volatile_load((void*)&");
                emitLvalue(e.retVal);
                of << StringView(", (const void*)");
                emitParam(e.args.at(0));
                of << StringView(", ") << valueSize << StringView(")");
            }
        }
    } else if (name == "unaligned_volatile_load") {
        size_t valueSize = 0;
        MIR_ASSERT(localMirRes, TargetGetSizeOf(sp, resolve_, params.types.at(0), valueSize), StringView("Can't get size of ") << params.types.at(0));
        if (valueSize == 0) {
            return;
        }
        of << StringView("__trustme_unaligned_volatile_load((void*)&");
        emitLvalue(e.retVal);
        of << StringView(", (const void*)");
        emitParam(e.args.at(0));
        of << StringView(", ") << valueSize << StringView(")");
    } else if (name == "volatile_store") {
        if (!this->typeIsBadZst(params.types.at(0))) {
            if (this->typeIsCScalar(params.types.at(0))) {
                of << StringView("*(volatile ");
                emitCtype(params.types.at(0));
                of << StringView("*)");
                emitParam(e.args.at(0));
                of << StringView(" = ");
                emitParam(e.args.at(1));
            } else {
                size_t valueSize = 0;
                MIR_ASSERT(localMirRes, TargetGetSizeOf(sp, resolve_, params.types.at(0), valueSize), StringView("Can't get size of ") << params.types.at(0));
                of << StringView("{ ");
                emitCtype(params.types.at(0));
                of << StringView(" trustme_value = ");
                emitParam(e.args.at(1));
                of << StringView("; __trustme_unaligned_volatile_store((void*)");
                emitParam(e.args.at(0));
                of << StringView(", (const void*)&trustme_value, ") << valueSize << StringView("); }");
            }
        }
    } else if (name == "unaligned_volatile_store") {
        size_t valueSize = 0;
        MIR_ASSERT(localMirRes, TargetGetSizeOf(sp, resolve_, params.types.at(0), valueSize), StringView("Can't get size of ") << params.types.at(0));
        if (valueSize == 0) {
            return;
        }
        of << StringView("{ ");
        emitCtype(params.types.at(0));
        of << StringView(" trustme_value = ");
        emitParam(e.args.at(1));
        of << StringView("; __trustme_unaligned_volatile_store((void*)");
        emitParam(e.args.at(0));
        of << StringView(", (const void*)&trustme_value, ") << valueSize << StringView("); }");
    } else if (name == "volatile_copy_memory" || name == "volatile_copy_nonoverlapping_memory") {
        size_t elementSize = 0;
        MIR_ASSERT(localMirRes, TargetGetSizeOf(sp, resolve_, params.types.at(0), elementSize), StringView("Can't get size of ") << params.types.at(0));
        if (elementSize == 0) {
            return;
        }
        of << (name == "volatile_copy_memory" ? "__trustme_volatile_memmove" : "__trustme_volatile_memcpy");
        of << StringView("((void*)");
        emitParam(e.args.at(0));
        of << StringView(", (const void*)");
        emitParam(e.args.at(1));
        of << StringView(", (size_t)");
        emitParam(e.args.at(2));
        of << StringView(" * ") << elementSize << StringView(")");
    } else if (name == "volatile_set_memory") {
        size_t elementSize = 0;
        MIR_ASSERT(localMirRes, TargetGetSizeOf(sp, resolve_, params.types.at(0), elementSize), StringView("Can't get size of ") << params.types.at(0));
        if (elementSize == 0) {
            return;
        }
        of << StringView("__trustme_volatile_memset((void*)");
        emitParam(e.args.at(0));
        of << StringView(", (u8)");
        emitParam(e.args.at(1));
        of << StringView(", (size_t)");
        emitParam(e.args.at(2));
        of << StringView(" * ") << elementSize << StringView(")");
    } else if (name == "nontemporal_store") {
        // TODO: Actually do a non-temporal store

        if (!this->typeIsBadZst(params.types.at(0))) {
            if (this->typeIsCScalar(params.types.at(0))) {
                of << StringView("*(volatile ");
                emitCtype(params.types.at(0));
                of << StringView("*)");
                emitParam(e.args.at(0));
                of << StringView(" = ");
                emitParam(e.args.at(1));
            } else {
                size_t valueSize = 0;
                MIR_ASSERT(localMirRes, TargetGetSizeOf(sp, resolve_, params.types.at(0), valueSize), StringView("Can't get size of ") << params.types.at(0));
                of << StringView("{ ");
                emitCtype(params.types.at(0));
                of << StringView(" trustme_value = ");
                emitParam(e.args.at(1));
                of << StringView("; __trustme_unaligned_volatile_store((void*)");
                emitParam(e.args.at(0));
                of << StringView(", (const void*)&trustme_value, ") << valueSize << StringView("); }");
            }
        }
    } else if (name.compare(0, 7, "atomic_") == 0) {
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
            of << StringView(" = ");
            emitAtomicRmwCast();
            of << StringView("__trustme_atomicloop") << getPrimSize(ty) << StringView("(");
            of << StringView("(volatile u") << getPrimSize(ty) << StringView("*)");
            emitParam(e.args.at(0));
            of << StringView(", ");
            emitAtomicRmwOperand(e.args.at(1));
            of << StringView(", ") << getAtomicTyGcc(ordering);
            of << StringView(", __trustme_op_and_not") << getPrimSize(ty);
            of << StringView(")");
        } else if (name == "atomic_or" || name.compare(0, 7 + 2 + 1, "atomic_or_") == 0) {
            auto ordering = getAtomicOrdering(name, 7 + 2 + 1);
            emitAtomicArith(AtomicOp::Or, ordering);
        } else if (name == "atomic_xor" || name.compare(0, 7 + 3 + 1, "atomic_xor_") == 0) {
            auto ordering = getAtomicOrdering(name, 7 + 3 + 1);
            emitAtomicArith(AtomicOp::Xor, ordering);
        } else if (name == "atomic_max" || name.compare(0, 7 + 3 + 1, "atomic_max_") == 0 || name == "atomic_min" || name.compare(0, 7 + 3 + 1, "atomic_min_") == 0) {
            auto ordering = getAtomicOrdering(name, 7 + 3 + 1);
            const auto& ty = params.types.at(0);
            const char* op = (name.c_str()[7 + 1] == 'a' ? "imax" : "imin");
            emitLvalue(e.retVal);
            of << StringView(" = ");
            emitAtomicRmwCast();
            of << StringView("__trustme_atomicloop") << getPrimSize(ty) << StringView("(");
            of << StringView("(volatile u") << getPrimSize(ty) << StringView("*)");
            emitParam(e.args.at(0));
            of << StringView(", ");
            emitAtomicRmwOperand(e.args.at(1));
            of << StringView(", ") << getAtomicTyGcc(ordering);
            of << StringView(", __trustme_op_") << op << getPrimSize(ty);
            of << StringView(")");
        } else if (name == "atomic_umax" || name.compare(0, 7 + 4 + 1, "atomic_umax_") == 0 || name == "atomic_umin" || name.compare(0, 7 + 4 + 1, "atomic_umin_") == 0) {
            auto ordering = getAtomicOrdering(name, 7 + 4 + 1);
            const auto& ty = params.types.at(0);
            const char* op = (name.c_str()[7 + 2] == 'a' ? "umax" : "umin");
            emitLvalue(e.retVal);
            of << StringView(" = ");
            emitAtomicRmwCast();
            of << StringView("__trustme_atomicloop") << getPrimSize(ty) << StringView("(");
            of << StringView("(volatile u") << getPrimSize(ty) << StringView("*)");
            emitParam(e.args.at(0));
            of << StringView(", ");
            emitAtomicRmwOperand(e.args.at(1));
            of << StringView(", ") << getAtomicTyGcc(ordering);
            of << StringView(", __trustme_op_") << op << getPrimSize(ty);
            of << StringView(")");
        } else if (name == "atomic_load" || name.compare(0, 7 + 4 + 1, "atomic_load_") == 0) {
            auto ordering = getAtomicOrdering(name, 7 + 4 + 1);
            emitLvalue(e.retVal);
            of << StringView(" = ");
            of << StringView("__atomic_load_n(");
            emitAtomicCast();
            emitParam(e.args.at(0));
            of << StringView(", ") << getAtomicTyGcc(ordering) << StringView(")");
        } else if (name == "atomic_store" || name.compare(0, 7 + 5 + 1, "atomic_store_") == 0) {
            auto ordering = getAtomicOrdering(name, 7 + 5 + 1);
            of << StringView("__atomic_store_n(");
            emitAtomicCast();
            emitParam(e.args.at(0));
            of << StringView(", ");
            emitParam(e.args.at(1));
            of << StringView(", ") << getAtomicTyGcc(ordering) << StringView(")");
        } else if (name == "atomic_cxchg_acq_failrelaxed") {
            emitAtomicCxchg(e, Ordering::Acquire, Ordering::Relaxed, false);
        } else if (name == "atomic_cxchg_acqrel_failrelaxed") {
            emitAtomicCxchg(e, Ordering::AcqRel, Ordering::Relaxed, false);
        } else if (name == "atomic_cxchg_rel") {
            emitAtomicCxchg(e, Ordering::Release, Ordering::Relaxed, false);
        } else if (name == "atomic_cxchg_acqrel") {
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
            of << StringView(" = ");
            of << StringView("__atomic_exchange_n(");
            emitAtomicCast();
            emitParam(e.args.at(0));
            of << StringView(", ");
            emitParam(e.args.at(1));
            of << StringView(", ") << getAtomicTyGcc(ordering) << StringView(")");
        } else if (name == "atomic_fence" || name.compare(0, 7 + 6, "atomic_fence_") == 0) {
            auto ordering = getAtomicOrdering(name, 7 + 6);
            of << StringView("__atomic_thread_fence(") << getAtomicTyGcc(ordering) << StringView(")");
        } else if (name == "atomic_singlethreadfence" || name.compare(0, 7 + 18, "atomic_singlethreadfence_") == 0) {
            // TODO: Does this matter?
        } else {
            MIR_BUG(localMirRes, StringView("Unknown atomic intrinsic '") << name << StringView("'"));
        }
    } else if (name == "option_payload_ptr") {
        emitLvalue(e.retVal);
        of << StringView(" = &(");
        emitParam(e.args.at(0));
        of << StringView(")->DATA.var_1. _0");
    } else if (name == "va_arg") {
        emitLvalue(e.retVal);
        of << StringView(" = va_arg(*(va_list*)");
        emitParam(e.args.at(0));
        of << StringView(", ");
        emitCtype(params.types.at(0));
        of << StringView(")");
    } else if (name == "va_copy") {
        of << StringView("va_copy(*(va_list*)");
        emitParam(e.args.at(0));
        of << StringView(", *(va_list*)");
        emitParam(e.args.at(1));
        of << StringView(")");
    } else if (name == "va_end") {
        of << StringView("va_end(*(va_list*)");
        emitParam(e.args.at(0));
        of << StringView(")");
    } else if (name.compare(0, 9, "platform:") == 0 || name.compare(0, 5, "simd_") == 0) {
        auto nameStrip = std::string_view(name.c_str() + (name.compare(0, 9, "platform:") == 0 ? 9 : 0));

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
                MIR_ASSERT(*self.mirRes, tyRepr, StringView("No repr for ") << ty);
                size_t sizeSlot = tyRepr->size;
                const auto& ity = tyRepr->fields[0].ty;
                DEBUG(StringView("SimdInfo Type: ") << ity);
                const auto& tyVal = ity->is_Primitive() ? ity : tyRepr->fields[0].ty->as_Array().inner;
                DEBUG(StringView("ty_val = ") << tyVal);
                size_t sizeVal = 0;
                MIR_ASSERT(*self.mirRes, TargetGetSizeOf(self.sp, self.resolve_, tyVal, sizeVal), tyVal);

                MIR_ASSERT(*self.mirRes, sizeSlot >= sizeVal, sizeSlot << StringView(" < ") << sizeVal);
                MIR_ASSERT(*self.mirRes, sizeVal > 0, StringView("SimdInfo::for_ty - Value type ") << tyVal << StringView(" was a ZST"));
                MIR_ASSERT(*self.mirRes, sizeSlot / sizeVal * sizeVal == sizeSlot, sizeSlot << StringView(" not a multiple of ") << sizeVal);

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
                        MIR_BUG(*self.mirRes, StringView("Invalid SIMD type inner - ") << tyVal);
                }
                return rv;
            }

            void emitValTy(CodeGeneratorC& self) {
                switch (ty) {
                    case Float:
                        self.of << (itemSize == 4 ? "float" : "double");
                        break;
                    case Signed:
                        self.of << StringView("i") << (itemSize * 8);
                        break;
                    case Unsigned:
                        self.of << StringView("u") << (itemSize * 8);
                        break;
                }
            }
        };

        auto simdCmp = [&](const char* op) {
            auto srcInfo = SimdInfo::forTy(*this, params.types.at(0));
            auto dstInfo = SimdInfo::forTy(*this, params.types.at(1));
            MIR_ASSERT(localMirRes, srcInfo.count == dstInfo.count, StringView("Element counts must match for ") << name);
            of << StringView("for(int i = 0; i < ") << dstInfo.count << StringView("; i++)");
            of << StringView("((");
            dstInfo.emitValTy(*this);
            of << StringView("*)&");
            emitLvalue(e.retVal);
            of << StringView(")[i] ");
            of << StringView("= (");
            of << StringView(" ((");
            srcInfo.emitValTy(*this);
            of << StringView("*)&");
            emitParam(e.args.at(0));
            of << StringView(")[i]");
            of << StringView(" ") << op;
            of << StringView(" ((");
            srcInfo.emitValTy(*this);
            of << StringView("*)&");
            emitParam(e.args.at(1));
            of << StringView(")[i]");
            of << StringView(" ? -1 : 0)");
        };
        auto simdArith = [&](const char* op) {
            auto info = SimdInfo::forTy(*this, params.types.at(0));
            emitLvalue(e.retVal);
            of << StringView(" = ");
            emitParam(e.args.at(0));
            of << StringView("; ");
            of << StringView("for(int i = 0; i < ") << info.count << StringView("; i++)");
            of << StringView("((");
            info.emitValTy(*this);
            of << StringView("*)&");
            emitLvalue(e.retVal);
            of << StringView(")[i] ");
            of << op << StringView("=");
            of << StringView(" ((");
            info.emitValTy(*this);
            of << StringView("*)&");
            emitParam(e.args.at(1));
            of << StringView(")[i]");
        };
        auto simdReduceFold = [&](const char* op) {
            auto info = SimdInfo::forTy(*this, params.types.at(0));
            MIR_ASSERT(localMirRes, e.args.size() == 1, name << StringView(" requires a vector"));
            emitLvalue(e.retVal);
            of << StringView(" = ((");
            info.emitValTy(*this);
            of << StringView("*)&");
            emitParam(e.args.at(0));
            of << StringView(")[0]; ");
            of << StringView("for(int i = 1; i < ") << info.count << StringView("; i++) ");
            emitLvalue(e.retVal);
            of << StringView(" ") << op << StringView("= ((");
            info.emitValTy(*this);
            of << StringView("*)&");
            emitParam(e.args.at(0));
            of << StringView(")[i]");
        };
        auto simdReduceMinMax = [&](const char* cmp) {
            auto info = SimdInfo::forTy(*this, params.types.at(0));
            MIR_ASSERT(localMirRes, e.args.size() == 1, name << StringView(" requires a vector"));
            emitLvalue(e.retVal);
            of << StringView(" = ((");
            info.emitValTy(*this);
            of << StringView("*)&");
            emitParam(e.args.at(0));
            of << StringView(")[0]; ");
            of << StringView("for(int i = 1; i < ") << info.count << StringView("; i++) if( ((");
            info.emitValTy(*this);
            of << StringView("*)&");
            emitParam(e.args.at(0));
            of << StringView(")[i] ") << cmp << StringView(" ");
            emitLvalue(e.retVal);
            of << StringView(" ) ");
            emitLvalue(e.retVal);
            of << StringView(" = ((");
            info.emitValTy(*this);
            of << StringView("*)&");
            emitParam(e.args.at(0));
            of << StringView(")[i]");
        };
        auto simdReduceMask = [&](bool isAll) {
            auto info = SimdInfo::forTy(*this, params.types.at(0));
            MIR_ASSERT(localMirRes, e.args.size() == 1, name << StringView(" requires a mask vector"));
            emitLvalue(e.retVal);
            of << StringView(" = ") << (isAll ? "true" : "false") << StringView("; ");
            of << StringView("for(int i = 0; i < ") << info.count << StringView("; i++) ");
            emitLvalue(e.retVal);
            of << StringView(" = ");
            emitLvalue(e.retVal);
            of << StringView(isAll ? " && " : " || ") << StringView("( ((");
            info.emitValTy(*this);
            of << StringView("*)&");
            emitParam(e.args.at(0));
            of << StringView(")[i] != 0 )");
        };
        auto simdCall = [&](const char* op) {
            auto info = SimdInfo::forTy(*this, params.types.at(0));
            of << StringView("for(int i = 0; i < ") << info.count << StringView("; i++)");
            of << StringView("((");
            info.emitValTy(*this);
            of << StringView("*)&");
            emitLvalue(e.retVal);
            of << StringView(")[i] ");
            of << StringView("= ");
            of << StringView("__builtin_");
            of << op << StringView("( ((");
            info.emitValTy(*this);
            of << StringView("*)&");
            emitParam(e.args.at(0));
            of << StringView(")[i] )");
        };

        if (nameStrip == "simd_insert") {
            size_t sizeSlot = 0, sizeVal = 0;
            TargetGetSizeOf(sp, resolve_, params.types.at(0), sizeSlot);
            TargetGetSizeOf(sp, resolve_, params.types.at(1), sizeVal);
            MIR_ASSERT(localMirRes, sizeSlot >= sizeVal, sizeSlot << StringView(" < ") << sizeVal);
            MIR_ASSERT(localMirRes, sizeSlot / sizeVal * sizeVal == sizeSlot, sizeSlot << StringView(" not a multiple of ") << sizeVal);

            emitLvalue(e.retVal);
            of << StringView(" = ");
            emitParam(e.args.at(0));
            of << StringView("; ");
            of << StringView("(( ");
            emitCtype(params.types.at(1));
            of << StringView("*)&");
            emitLvalue(e.retVal);
            of << StringView(")[");
            emitParam(e.args.at(1));
            of << StringView("] = ");
            emitParam(e.args.at(2));
        } else if (nameStrip == "simd_extract") {
            size_t sizeSlot = 0, sizeVal = 0;
            TargetGetSizeOf(sp, resolve_, params.types.at(0), sizeSlot);
            TargetGetSizeOf(sp, resolve_, params.types.at(1), sizeVal);
            MIR_ASSERT(localMirRes, sizeSlot >= sizeVal, sizeSlot << StringView(" < ") << sizeVal);
            MIR_ASSERT(localMirRes, sizeSlot / sizeVal * sizeVal == sizeSlot, sizeSlot << StringView(" not a multiple of ") << sizeVal);

            emitLvalue(e.retVal);
            of << StringView(" = (( ");
            emitCtype(params.types.at(1));
            of << StringView("*)&");
            emitParam(e.args.at(0));
            of << StringView(")[");
            emitParam(e.args.at(1));
            of << StringView("]");
        } else if (nameStrip == "simd_bitmask") {
            auto srcInfo = SimdInfo::forTy(*this, params.types.at(0));
            size_t sizeOut = 0;
            TargetGetSizeOf(sp, resolve_, params.types.at(1), sizeOut);
            of << StringView("{ u8* out = (u8*)&(");
            emitLvalue(e.retVal);
            of << StringView("); memset(out, 0, ") << sizeOut << StringView("); ");
            for (size_t i = 0; i < srcInfo.count; i++) {
                of << StringView("out[") << (i / 8) << StringView("] |= ((((const u8*)&");
                emitParam(e.args.at(0));
                of << StringView(")[") << (i * srcInfo.itemSize + srcInfo.itemSize - 1) << StringView("] >> 7) & 1) << ") << (i % 8) << StringView("; ");
            }
            of << StringView("}");
        } else if (nameStrip == "simd_shuffle128" || nameStrip == "simd_shuffle64" || nameStrip == "simd_shuffle32" || nameStrip == "simd_shuffle16" || nameStrip == "simd_shuffle8" || nameStrip == "simd_shuffle4" || nameStrip == "simd_shuffle2") {
            size_t sizeSlot = 0;
            TargetGetSizeOf(sp, resolve_, params.types.at(1), sizeSlot);
            size_t div = nameStrip == "simd_shuffle128" ? 128 : nameStrip == "simd_shuffle64" ? 64 : nameStrip == "simd_shuffle32" ? 32 : nameStrip == "simd_shuffle16" ? 16 : nameStrip == "simd_shuffle8" ? 8 : nameStrip == "simd_shuffle4" ? 4 : nameStrip == "simd_shuffle2" ? 2 : (UNREACHABLE(), 0);
            size_t sizeVal = sizeSlot / div;
            MIR_ASSERT(localMirRes, sizeVal > 0, sizeSlot << StringView(" / ") << div << StringView(" == 0?"));
            MIR_ASSERT(localMirRes, sizeSlot >= sizeVal, sizeSlot << StringView(" < ") << sizeVal);
            MIR_ASSERT(localMirRes, sizeSlot / sizeVal * sizeVal == sizeSlot, sizeSlot << StringView(" not a multiple of ") << sizeVal);
            size_t sizeIn = 0;
            TargetGetSizeOf(sp, resolve_, params.types.at(0), sizeIn);
            size_t nIn = sizeIn / sizeVal;
            MIR_ASSERT(localMirRes, nIn > 0, StringView("Zero-sized shuffle input"));
            of << StringView("for(int i = 0; i < ") << div << StringView("; i++) { int j = ");
            emitParam(e.args.at(2));
            of << StringView(".DATA[i];");
            of << StringView("((u") << (sizeVal * 8) << StringView("*)&");
            emitLvalue(e.retVal);
            of << StringView(")[i]");
            of << StringView(" = ((u") << (sizeVal * 8) << StringView("*)(j < ") << nIn << StringView(" ? &");
            emitParam(e.args.at(0));
            of << StringView(" : &");
            emitParam(e.args.at(1));
            of << StringView("))[j < ") << nIn << StringView(" ? j : j - ") << nIn << StringView("];");
            of << StringView("}");
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
            size_t div = sizeMap / 4;
            size_t sizeVal = sizeRet / div;
            size_t nIn = sizeVec / sizeVal;
            MIR_ASSERT(localMirRes, nIn > 0, StringView("Zero-sized shuffle input"));
            of << StringView("for(int i = 0; i < ") << div << StringView("; i++) {");
            of << StringView(" int j = ");
            emitParam(e.args.at(2));
            of << StringView("._0");
            of << StringView(".DATA[i];");
            of << StringView(" ((u") << (sizeVal * 8) << StringView("*)&");
            emitLvalue(e.retVal);
            of << StringView(")[i]");
            of << StringView(" = ((u") << (sizeVal * 8) << StringView("*)(j < ") << nIn << StringView(" ? &");
            emitParam(e.args.at(0));
            of << StringView(" : &");
            emitParam(e.args.at(1));
            of << StringView("))[j < ") << nIn << StringView(" ? j : j - ") << nIn << StringView("];");
            of << StringView("}");
        } else if (nameStrip == "simd_cast") {
            auto srcInfo = SimdInfo::forTy(*this, params.types.at(0));
            auto dstInfo = SimdInfo::forTy(*this, params.types.at(1));
            MIR_ASSERT(localMirRes, srcInfo.count == dstInfo.count, StringView("Element counts must match for ") << name);
            of << StringView("for(int i = 0; i < ") << dstInfo.count << StringView("; i++) ");
            of << StringView("((");
            dstInfo.emitValTy(*this);
            of << StringView("*)&");
            emitLvalue(e.retVal);
            of << StringView(")[i] ");
            of << StringView("= ((");
            srcInfo.emitValTy(*this);
            of << StringView("*)&");
            emitParam(e.args.at(0));
            of << StringView(")[i];");
        } else if (nameStrip == "simd_select") {
            auto maskInfo = SimdInfo::forTy(*this, params.types.at(0));
            auto valInfo = SimdInfo::forTy(*this, params.types.at(1));
            MIR_ASSERT(localMirRes, maskInfo.count == valInfo.count, StringView("Element counts must match for ") << name);
            of << StringView("for(int i = 0; i < ") << valInfo.count << StringView("; i++) ");
            of << StringView("((");
            valInfo.emitValTy(*this);
            of << StringView("*)&");
            emitLvalue(e.retVal);
            of << StringView(")[i] ");
            of << StringView("= ((");
            maskInfo.emitValTy(*this);
            of << StringView("*)&");
            emitParam(e.args.at(0));
            of << StringView(")[i]");
            of << StringView("? ((");
            valInfo.emitValTy(*this);
            of << StringView("*)&");
            emitParam(e.args.at(1));
            of << StringView(")[i]");
            of << StringView(": ((");
            valInfo.emitValTy(*this);
            of << StringView("*)&");
            emitParam(e.args.at(2));
            of << StringView(")[i]");
            of << StringView(";");
        } else if (nameStrip == "simd_select_bitmask") {
            auto valInfo = SimdInfo::forTy(*this, params.types.at(1));
            of << StringView("for(int i = 0; i < ") << valInfo.count << StringView("; i++) ");
            of << StringView("((");
            valInfo.emitValTy(*this);
            of << StringView("*)&");
            emitLvalue(e.retVal);
            of << StringView(")[i] ");
            of << StringView("= ((");
            emitParam(e.args.at(0));
            of << StringView(") >> i) != 0");
            of << StringView("? ((");
            valInfo.emitValTy(*this);
            of << StringView("*)&");
            emitParam(e.args.at(1));
            of << StringView(")[i]");
            of << StringView(": ((");
            valInfo.emitValTy(*this);
            of << StringView("*)&");
            emitParam(e.args.at(2));
            of << StringView(")[i]");
            of << StringView(";");
        } else if (nameStrip == "simd_eq") {
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
        } else if (nameStrip == "simd_neg") {
            auto info = SimdInfo::forTy(*this, params.types.at(0));
            emitLvalue(e.retVal);
            of << StringView(" = ");
            emitParam(e.args.at(0));
            of << StringView("; for(int i = 0; i < ") << info.count << StringView("; i++) ");
            if (info.ty == SimdInfo::Float) {
                of << StringView("((");
                info.emitValTy(*this);
                of << StringView("*)&");
                emitLvalue(e.retVal);
                of << StringView(")[i] = -((");
                info.emitValTy(*this);
                of << StringView("*)&");
                emitParam(e.args.at(0));
                of << StringView(")[i]");
            } else {
                of << StringView("((u") << (info.itemSize * 8) << StringView("*)&");
                emitLvalue(e.retVal);
                of << StringView(")[i] = 0 - ((u") << (info.itemSize * 8) << StringView("*)&");
                emitParam(e.args.at(0));
                of << StringView(")[i]");
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
        } else if (nameStrip == "simd_reduce_add_ordered" || nameStrip == "simd_reduce_mul_ordered") {
            auto info = SimdInfo::forTy(*this, params.types.at(0));
            MIR_ASSERT(localMirRes, e.args.size() == 2, name << StringView(" requires a vector and accumulator"));
            emitLvalue(e.retVal);
            of << StringView(" = ");
            emitParam(e.args.at(1));
            of << StringView("; ");
            of << StringView("for(int i = 0; i < ") << info.count << StringView("; i++) ");
            if (info.ty == SimdInfo::Float) {
                emitLvalue(e.retVal);
                of << StringView(nameStrip == "simd_reduce_add_ordered" ? " += " : " *= ");
                of << StringView("((");
                info.emitValTy(*this);
                of << StringView("*)&");
                emitParam(e.args.at(0));
                of << StringView(")[i]");
            } else {
                of << StringView(nameStrip == "simd_reduce_add_ordered" ? "__builtin_add_overflow(" : "__builtin_mul_overflow(");
                emitLvalue(e.retVal);
                of << StringView(", ((");
                info.emitValTy(*this);
                of << StringView("*)&");
                emitParam(e.args.at(0));
                of << StringView(")[i], &");
                emitLvalue(e.retVal);
                of << StringView(")");
            }
        } else if (nameStrip == "simd_reduce_add_unordered") {
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
        } else if (nameStrip == "simd_fma") {
            auto info = SimdInfo::forTy(*this, params.types.at(0));
            of << StringView("for(int i = 0; i < ") << info.count << StringView("; i++)");
            of << StringView("((");
            info.emitValTy(*this);
            of << StringView("*)&");
            emitLvalue(e.retVal);
            of << StringView(")[i] ");
            of << StringView("= ");
            of << StringView("__builtin_");
            of << StringView("fma(");
            of << StringView(" ((");
            info.emitValTy(*this);
            of << StringView("*)&");
            emitParam(e.args.at(0));
            of << StringView(")[i],");
            of << StringView(" ((");
            info.emitValTy(*this);
            of << StringView("*)&");
            emitParam(e.args.at(1));
            of << StringView(")[i],");
            of << StringView(" ((");
            info.emitValTy(*this);
            of << StringView("*)&");
            emitParam(e.args.at(2));
            of << StringView(")[i]");
            of << StringView(")");
        } else {
            // TODO: Platform intrinsics
            of << StringView("assert(!\"TODO: Platform intrinsic \\\"") << name << StringView("\\\"\")");
        }
    } else {
        MIR_BUG(localMirRes, StringView("Unknown intrinsic '") << name << StringView("'"));
    }
    of << StringView(";\n");
}

template <typename F>
auto CodeGeneratorC::emitTermSwitchvalue(const MIRTypeResolve& localMirRes, const MIRLValue& val, const MIRSwitchValues& values, unsigned indentLevel, F f) -> void {
    CSwitchArmCb<F> cb(f);
    emitTermSwitchvalueCb(localMirRes, val, values, indentLevel, cb);
}

auto CodeGeneratorC::emitDestructorLoopCb(const MIRLValue& slot, const HIRTypeData* elementTy, CDestructorCountCallback& emitCount, unsigned indentLevel) -> void {
    auto indent = RepeatLitStr{"\t", static_cast<int>(indentLevel)};
    auto element = MIRLValue::newIndex(slot.clone(), MIRLValue::Storage::MAX_ARG);

    of << indent << StringView("for(unsigned i = 0; i < ");
    emitCount.emit();
    of << StringView("; i++) {\n");
    of << indent << StringView("\ttry {\n");
    emitDestructorCall(element, elementTy, false, indentLevel + 2);
    of << StringView("\n") << indent << StringView("\t} catch (...) {\n");
    of << indent << StringView("\t\tfor(i++; i < ");
    emitCount.emit();
    of << StringView("; i++) {\n");
    of << indent << StringView("\t\t\ttry {\n");
    emitDestructorCall(element, elementTy, false, indentLevel + 4);
    of << StringView("\n") << indent << StringView("\t\t\t} catch (...) { abort(); }\n");
    of << indent << StringView("\t\t}\n");
    of << indent << StringView("\t\tthrow;\n");
    of << indent << StringView("\t}\n");
    of << indent << StringView("}");
}

template <typename F>
auto CodeGeneratorC::emitDestructorLoop(const MIRLValue& slot, const HIRTypeData* elementTy, F f, unsigned indentLevel) -> void {
    CDestructorCountCb<F> cb(f);
    emitDestructorLoopCb(slot, elementTy, cb, indentLevel);
}

auto CodeGeneratorC::emitTupleDestructor(const MIRLValue& slot, const HIRTypeData::Data_Tuple& tuple, bool unsizedValid, unsigned indentLevel) -> void {
    std::vector<MIRLValue> fields;
    std::vector<const HIRTypeData*> fieldTypes;
    std::vector<bool> fieldUnsized;
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
    of << indent << StringView("{ unsigned trustme_drop_progress = 0;\n");
    of << indent << StringView("\ttry {\n");
    for (size_t i = 0; i < fields.size(); i++) {
        emitDestructorCall(fields[i], fieldTypes[i], fieldUnsized[i], indentLevel + 2);
        of << indent << StringView("\t\ttrustme_drop_progress = ") << i + 1 << StringView(";\n");
    }
    of << indent << StringView("\t} catch (...) {\n");
    for (size_t i = 1; i < fields.size(); i++) {
        of << indent << StringView("\t\tif(trustme_drop_progress < ") << i << StringView(") {\n");
        of << indent << StringView("\t\t\ttry {\n");
        emitDestructorCall(fields[i], fieldTypes[i], fieldUnsized[i], indentLevel + 4);
        of << indent << StringView("\t\t\t} catch (...) { abort(); }\n");
        of << indent << StringView("\t\t}\n");
    }
    of << indent << StringView("\t\tthrow;\n");
    of << indent << StringView("\t}\n");
    of << indent << StringView("}");
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
                emitDestructorCall(MIRLValue::newDeref(slot.clone()), te.inner, true, indentLevel);
            }
            break;
        }
        case HIRTypeData::TAG_Path: {
            // - TODO: If the destructor is known to do nothing, don't call it.
            auto p = HIRPath(ty, "#drop_glue");
            switch (metadataType(ty)) {
                case MetadataType::Unknown:
                    MIR_BUG(*mirRes, ty << StringView(" unknown metadata"));
                case MetadataType::None:
                case MetadataType::Zero:
                    if (this->typeIsBadZst(ty) && this->lvalueRootIsBadZst(slot)) {
                        size_t alignment = 0;
                        MIR_ASSERT(*mirRes, TargetGetAlignOf(sp, resolve_, ty, alignment), StringView("Unknown ZST alignment"));
                        of << indent << TransMangleValue(p) << StringView("((");
                        emitCtype(ty);
                        of << StringView("*)") << alignment << StringView(");\n");
                    } else if (this->typeIsBadZst(ty) && MIRLValue::CRef(slot).is_Index()) {
                        of << indent << TransMangleValue(p) << StringView("((");
                        emitCtype(ty);
                        of << StringView("*)");
                        emitBorrow(*mirRes, HIRBorrowType::Unique, slot);
                        of << StringView(");\n");
                    } else if (this->typeIsBadZst(ty) && (slot.is_Field() || slot.is_Downcast())) {
                        auto v = MIRLValue::CRef(slot).innerRef();
                        HIRTypeRef tmp;
                        while (this->typeIsBadZst(mirRes->getLvalueType(tmp, v)) && (v.is_Field() || v.is_Downcast())) {
                            v = v.innerRef();
                        }
                        of << indent << TransMangleValue(p) << StringView("((");
                        emitCtype(ty);
                        of << StringView("*)&");
                        emitLvalue(v);
                        of << StringView(");\n");
                    } else if (this->typeIsBadZst(ty) && slot.wrappers.empty()) {
                        of << indent << TransMangleValue(p) << StringView("((");
                        emitCtype(ty);
                        of << StringView("*)&rv);\n");
                    } else if (this->fieldIsUnderaligned(slot, ty)) {
                        of << indent << StringView("{ ");
                        emitCtype(ty, FMT_CB(ss, ss << StringView("trustme_unaligned")));
                        of << StringView("; memcpy(&trustme_unaligned, &");
                        emitLvalue(slot);
                        of << StringView(", sizeof(trustme_unaligned)); ") << TransMangleValue(p) << StringView("(&trustme_unaligned); }\n");
                    } else {
                        of << indent << TransMangleValue(p) << StringView("(&");
                        emitLvalue(slot);
                        of << StringView(");\n");
                    }
                    break;
                case MetadataType::Slice:
                case MetadataType::TraitObject:
                    of << indent << TransMangleValue(p) << StringView("(");
                    emitDstLvaluePointer(MIRLValue::CRef(slot));
                    of << StringView(");\n");
                    break;
            }
            break;
        }
        case HIRTypeData::TAG_Array: {
            auto& te = (*ty).as_Array();
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
            MIR_ASSERT(*mirRes, unsizedValid, StringView("Dropping TraitObject without an owned pointer"));
            of << indent << StringView("((VTABLE_HDR*)");
            emitDstLvaluePointer(MIRLValue::CRef(slot));
            of << StringView(".META)->drop(");
            emitDstLvaluePointer(MIRLValue::CRef(slot));
            of << StringView(".PTR");
            of << StringView(");");
            break;
        }
        case HIRTypeData::TAG_Slice: {
            auto& te = (*ty).as_Slice();
            MIR_ASSERT(*mirRes, unsizedValid, StringView("Dropping Slice without an owned pointer"));
            emitDestructorLoop(slot, te.inner, [&] {
                emitDstLvaluePointer(MIRLValue::CRef(slot));
                of << StringView(".META");
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
    of << S128(U128(v)).truncateI64() << StringView("ll");
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
            of << S128(ve.values[idx]).truncateI64() << StringView("ll");
            break;
        case HIRCoreType::Bool:
        case HIRCoreType::U8:
        case HIRCoreType::U16:
        case HIRCoreType::U32:
        case HIRCoreType::U64:
        case HIRCoreType::Usize:
        case HIRCoreType::Char:
            of << ve.values[idx].truncateU64() << StringView("ull");
            break;
        case HIRCoreType::I128:
            if (options.emulatedI128) {
                of << StringView("make128s_raw(") << ve.values[idx].getHi() << StringView("ull, ") << ve.values[idx].getLo() << StringView("ull)");
            } else {
                of << StringView("((int128_t)(((uint128_t)") << ve.values[idx].getHi() << StringView("ull << 64) | (uint128_t)") << ve.values[idx].getLo() << StringView("ull))");
            }
            break;
        case HIRCoreType::U128:
            if (options.emulatedI128) {
                of << StringView("make128_raw(") << ve.values[idx].getHi() << StringView("ull, ") << ve.values[idx].getLo() << StringView("ull)");
            } else {
                of << StringView("(((uint128_t)") << ve.values[idx].getHi() << StringView("ull << 64) | (uint128_t)") << ve.values[idx].getLo() << StringView("ull)");
            }
            break;
        case HIRCoreType::F16:
        case HIRCoreType::F32:
        case HIRCoreType::F64:
        case HIRCoreType::F128:
            MIR_TODO(*mirRes, StringView("Floating point enum tag."));
            break;
        case HIRCoreType::Str:
            MIR_BUG(*mirRes, StringView("Unsized tag?!"));
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
            of << StringView("rv");
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
                    of << StringView("arg") << e << StringView("_aligned");
                    break;
                }
            }
            of << StringView("arg") << e;
            break;
        }
        case MIRLValue::RefCommon::TAG_Local: {
            decltype(val.as_Local()) e = val.as_Local();
            if (e == MIRLValue::Storage::MAX_ARG) {
                of << StringView("i");
            } else {
                of << StringView("var") << e;
            }
            break;
        }
        case MIRLValue::RefCommon::TAG_Static: {
            decltype(val.as_Static()) e = val.as_Static();
            of << TransMangleValue(e);
            of << StringView(".val");
            break;
        }
        case MIRLValue::RefCommon::TAG_Field: {
            decltype(val.as_Field()) fieldIndex = val.as_Field();
            HIRTypeRef tmp;
            auto inner = val.innerRef();
            const auto& ty = mirRes->getLvalueType(tmp, inner);
            if (ty->is_Slice()) {
                if (inner.is_Deref() || isIndirectDstLvalue(inner)) {
                    of << StringView("((");
                    emitCtype(ty->as_Slice().inner);
                    of << StringView("*)");
                    if (inner.is_Deref()) {
                        emitLvalue(inner.innerRef());
                    } else {
                        emitDstLvaluePointer(inner);
                    }
                    of << StringView(".PTR)");
                } else {
                    emitLvalue(inner);
                }
                of << StringView("[") << fieldIndex << StringView("]");
            } else if (ty->is_Array()) {
                emitLvalue(inner);
                of << StringView(".DATA[") << fieldIndex << StringView("]");
            } else if (inner.is_Deref() || isIndirectDstLvalue(inner)) {
                auto dstType = metadataType(ty);
                if (dstType != MetadataType::None) {
                    of << StringView("((");
                    emitCtype(ty);
                    of << StringView("*)");
                    if (inner.is_Deref()) {
                        emitLvalue(inner.innerRef());
                    } else {
                        emitDstLvaluePointer(inner);
                    }
                    of << StringView(".PTR)->_") << fieldIndex;
                } else {
                    emitLvalue(inner.innerRef());
                    of << StringView("->_") << fieldIndex;
                }
            } else {
                emitLvalue(inner);
                of << StringView("._") << fieldIndex;
            }
            break;
        }
        case MIRLValue::RefCommon::TAG_Deref: {
            auto inner = val.innerRef();
            HIRTypeRef tmp;
            const auto& ty = mirRes->getLvalueType(tmp, val);
            auto dstType = metadataType(ty);
            if (dstType != MetadataType::None) {
                of << StringView("(*(");
                emitCtype(ty);
                of << StringView("*)");
                emitLvalue(inner);
                of << StringView(".PTR)");
            } else {
                of << StringView("(*");
                emitLvalue(inner);
                of << StringView(")");
            }
            break;
        }
        case MIRLValue::RefCommon::TAG_Index: {
            decltype(val.as_Index()) indexLocal = val.as_Index();
            auto inner = val.innerRef();
            HIRTypeRef tmp;
            const auto& ty = mirRes->getLvalueType(tmp, inner);
            of << StringView("(");
            if (ty->is_Slice()) {
                if (inner.is_Deref() || isIndirectDstLvalue(inner)) {
                    of << StringView("(");
                    emitCtype(ty->as_Slice().inner);
                    of << StringView("*)");
                    if (inner.is_Deref()) {
                        emitLvalue(inner.innerRef());
                    } else {
                        emitDstLvaluePointer(inner);
                    }
                    of << StringView(".PTR");
                } else {
                    emitLvalue(inner);
                }
            } else if (ty->is_Array()) {
                emitLvalue(inner);
                of << StringView(".DATA");
            } else {
                emitLvalue(inner);
            }
            of << StringView(")[");
            emitLvalue(MIRLValue::newLocal(indexLocal));
            of << StringView("]");
            break;
        }
        case MIRLValue::RefCommon::TAG_Downcast: {
            decltype(val.as_Downcast()) variantIndex = val.as_Downcast();
            auto inner = val.innerRef();
            HIRTypeRef tmp;
            const auto& ty = mirRes->getLvalueType(tmp, inner);
            emitLvalue(inner);
            MIR_ASSERT(*mirRes, ty->is_Path(), StringView("Downcast on non-Path type - ") << ty);
            if (ty->as_Path().binding.is_Enum()) {
                of << StringView(".DATA");
            }
            of << StringView(".var_") << variantIndex;
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

    of << StringView("([]() { union { ");
    emitCtype(type, FMT_CB(ss, ss << StringView("val");));
    of << StringView("; ");
    if (pointerAligned) {
        const auto pointerSize = TargetGetPointerBits() / 8;
        const auto words = size == 0 ? 0 : 1 + (size - 1) / pointerSize;
        of << StringView("uintptr_t raw[") << words << StringView("]");
    } else {
        of << StringView("u8 raw[") << size << StringView("]");
    }
    of << StringView("; } value = { .raw = {");

    if (pointerAligned) {
        const auto pointerSize = TargetGetPointerBits() / 8;
        auto relocation = encoded.relocations.begin();
        for (size_t i = 0; i < encoded.bytes.size(); i += pointerSize) {
            u64 word = 0;
            for (size_t byte = 0; byte < pointerSize && i + byte < encoded.bytes.size(); byte++) {
                word |= static_cast<u64>(encoded.bytes[i + byte]) << (byte * 8);
            }
            if (i > 0) {
                of << StringView(",");
            }
            if (relocation != encoded.relocations.end() && relocation->ofs <= i) {
                MIR_ASSERT(*mirRes, relocation->ofs == i, StringView("Relocation not aligned to a pointer - ") << relocation->ofs << StringView(" != ") << i);
                MIR_ASSERT(*mirRes, relocation->len == pointerSize, StringView("Relocation size not pointer size - ") << relocation->len << StringView(" != ") << pointerSize);
                word -= EncodedLiteral::PTR_BASE;
                of << StringView("(uintptr_t)");
                if (relocation->p) {
                    if (relocation->p->data.is_UfcsInherent() && relocation->p->data.as_UfcsInherent().item == "#type_id") {
                        of << StringView("&__typeid_") << TransMangleTypeId(relocation->p->data.as_UfcsInherent().type);
                    } else {
                        of << StringView("&");
                        emitReifiedFunctionName(*relocation->p, relocation->preserveTrackCaller);
                    }
                } else {
                    printEscapedString(relocation->bytes);
                }
                if (word > 0) {
                    of << StringView("+") << word;
                }
                ++relocation;
            } else {
                of << StringView("0x") << formatHex(word) << StringView("ull");
            }
        }
        MIR_ASSERT(*mirRes, relocation == encoded.relocations.end(), StringView("Relocation outside encoded constant"));
    } else {
        MIR_ASSERT(*mirRes, encoded.relocations.empty(), StringView("Non-pointer-aligned encoded constant has relocations"));
        for (size_t i = 0; i < encoded.bytes.size(); i++) {
            if (i > 0) {
                of << StringView(",");
            }
            of << static_cast<unsigned>(encoded.bytes[i]);
        }
    }
    of << StringView("} }; return value.val; }())");
}

auto CodeGeneratorC::emitConstant(const MIRConstant& ve, const MIRLValue* dstPtr) -> void {
    switch (ve.tag()) {
        case MIRConstant::TAG_Int: {
            auto& c = ve.as_Int();
            switch (c.t) {
                // TODO: These should already have been truncated/reinterpreted, but just in case.
                case HIRCoreType::I8:
                    of << static_cast<int>(static_cast<i8>(c.v.truncateI64()));
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
                        of << StringView("INT64_MIN");
                    } else if (c.v.truncateI64() == INT64_MAX) {
                        of << StringView("INT64_MAX");
                    } else {
                        of << c.v.truncateI64();
                        of << StringView("ll");
                    }
                    break;
                case HIRCoreType::I128:
                    if (options.emulatedI128) {
                        of << StringView("make128s_raw(") << c.v.getInner().getHi() << StringView("ull, ") << c.v.getInner().getLo() << StringView("ull)");
                    } else if (c.v.isI64() && c.v.truncateI64() != INT64_MIN) {
                        of << StringView("(int128_t)");
                        of << c.v;
                        of << StringView("ll");
                    } else {
                        of << StringView("(int128_t)( ((uint128_t)") << c.v.getInner().getHi() << StringView("ull << 64) | (uint128_t)") << c.v.getInner().getLo() << StringView("ull)");
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
                    of << StringView("0x") << formatHex(c.v.truncateU64() & 0xFF);
                    break;
                case HIRCoreType::U16:
                    of << StringView("0x") << formatHex(c.v.truncateU64() & 0xFFFF);
                    break;
                case HIRCoreType::U32:
                    of << StringView("0x") << formatHex(c.v.truncateU64() & 0xFFFFFFFF);
                    break;
                case HIRCoreType::U64:
                case HIRCoreType::Usize:
                    of << StringView("0x") << formatHex(c.v.truncateU64()) << StringView("ull");
                    break;
                case HIRCoreType::U128:
                    if (options.emulatedI128) {
                        of << StringView("make128_raw(") << c.v.getHi() << StringView("ull, ") << c.v.getLo() << StringView("ull)");
                    } else if (c.v.isU64()) {
                        of << StringView("(uint128_t)");
                        of << StringView("0x") << formatHex(c.v) << StringView("ull");
                    } else {
                        of << StringView("( ((uint128_t)0x") << formatHex(c.v.getHi()) << StringView("ull << 64) | (uint128_t)0x") << formatHex(c.v.getLo()) << StringView("ull)");
                    }
                    break;
                case HIRCoreType::Char:
                    BUG_ASSERT(c.v <= 0x10FFFF);
                    if (c.v < 256) {
                        of << c.v;
                    } else {
                        of << StringView("0x") << formatHex(c.v);
                    }
                    break;
                default:
                    MIR_BUG(*mirRes, StringView("Invalid type for UInt literal - ") << c.t);
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
            of << StringView("(void*)");
            this->printEscapedString(c);
            break;
        }
        case MIRConstant::TAG_StaticString: {
            auto& c = ve.as_StaticString();
            of << StringView("make_sliceptr(");
            this->printEscapedString(c);
            of << StringView(", ") << c.size() << StringView(")");
            break;
        }
        case MIRConstant::TAG_Encoded: {
            auto& c = ve.as_Encoded();
            emitEncodedConstant(c.type, c.value);
            break;
        }
        case MIRConstant::TAG_Const: {
            MIR_BUG(*mirRes, StringView("Unexpected Constant::Const - ") << ve);
            break;
        }
        case MIRConstant::TAG_Generic: {
            MIR_BUG(*mirRes, StringView("Generic value present at codegen"));
            break;
        }
        case MIRConstant::TAG_Function: {
            MIR_TODO(*mirRes, StringView("Constant::Function"));
            break;
        }
        case MIRConstant::TAG_ItemAddr: {
            auto& c = ve.as_ItemAddr();
            const bool hasOffset = c.offset != U128(0);
            if (hasOffset) {
                MIR_ASSERT(*mirRes, c.offset.isU64(), StringView("Item address offset is too large: ") << c.offset);
                of << StringView("((void*)((u8*)");
            }
            if (c->data.is_UfcsInherent() && c->data.as_UfcsInherent().item == "#type_id") {
                of << StringView("(void*)&__typeid_") << TransMangleTypeId(c->data.as_UfcsInherent().type);
            } else {
                MonomorphState msTmp(crate.types);
                auto v = resolve_.getValue(sp, *c, msTmp, /*signature_only=*/true);
                if (const auto* stat = v.opt_Static(); stat && (**stat).isPromoted && !hasOffset) {
                    auto statTy = msTmp.monomorphType(sp, (**stat).type);
                    size_t size = 0;
                    size_t align = 0;
                    if (!monomorphiseTypeNeeded(statTy) && !statTy->mayHaveAssociatedType() && TargetGetSizeOf(sp, resolve_, statTy, size) && size == 0 && TargetGetAlignOf(sp, resolve_, statTy, align)) {
                        of << StringView("((");
                        emitCtype(statTy);
                        of << StringView("*)(uintptr_t)") << (align == 0 ? 1 : align) << StringView(")");
                        break;
                    }
                }
                const bool isFcn = v.is_Function() || v.is_EnumConstructor() || v.is_StructConstructor();
                MIR_ASSERT(*mirRes, !isFcn || !hasOffset, StringView("Function address has a non-zero offset: ") << c.offset);
                if (!isFcn) {
                    of << StringView("&");
                }
                emitReifiedFunctionName(*c);
                if (!isFcn) {
                    of << StringView(".val");
                }
            }
            if (hasOffset) {
                of << StringView(" + 0x") << formatHex(c.offset.truncateU64()) << StringView("))");
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
    of << helper << StringView("(");
    if (isSigned) {
        of << (options.emulatedI128 ? "int128_to_uint128(" : "(uint128_t)");
    }
    emitParam(arg);
    if (isSigned && options.emulatedI128) {
        of << StringView(")");
    }
    of << StringView(")");
    if (isSigned && options.emulatedI128) {
        of << StringView(")");
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
                of << StringView("(");
                emitCtype(mirRes->getParamType(tmp, p));
                of << StringView(")");
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
            of << StringView("._0._0");
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
            of << StringView("@") << ty << StringView("@") << inner;
            break;
        }
        case HIRTypeData::TAG_Diverge: {
            of << StringView("tBANG");
            if (!inner.empty()) {
                of << StringView(" ") << inner;
            }
            break;
        }
        case HIRTypeData::TAG_Primitive: {
            auto& te = (*ty).as_Primitive();
            switch (te) {
                case HIRCoreType::Usize:
                    of << StringView("uintptr_t");
                    break;
                case HIRCoreType::Isize:
                    of << StringView("intptr_t");
                    break;
                case HIRCoreType::U8:
                    of << StringView("u8");
                    break;
                case HIRCoreType::I8:
                    of << StringView("i8");
                    break;
                case HIRCoreType::U16:
                    of << StringView("u16");
                    break;
                case HIRCoreType::I16:
                    of << StringView("i16");
                    break;
                case HIRCoreType::U32:
                    of << StringView("u32");
                    break;
                case HIRCoreType::I32:
                    of << StringView("i32");
                    break;
                case HIRCoreType::U64:
                    of << StringView("u64");
                    break;
                case HIRCoreType::I64:
                    of << StringView("i64");
                    break;
                case HIRCoreType::U128:
                    of << StringView("uint128_t");
                    break;
                case HIRCoreType::I128:
                    of << StringView("int128_t");
                    break;

                case HIRCoreType::F16:
                    of << StringView("f16");
                    break;
                case HIRCoreType::F32:
                    of << StringView("float");
                    break;
                case HIRCoreType::F64:
                    of << StringView("double");
                    break;
                case HIRCoreType::F128:
                    of << StringView("f128");
                    break;

                case HIRCoreType::Bool:
                    of << StringView("RUST_BOOL");
                    break;
                case HIRCoreType::Char:
                    of << StringView("RUST_CHAR");
                    break;
                case HIRCoreType::Str:
                    MIR_BUG(*mirRes, StringView("Raw str"));
            }
            if (!inner.empty()) {
                of << StringView(" ") << inner;
            }
            break;
        }
        case HIRTypeData::TAG_Path: {
            auto& te = (*ty).as_Path();
            switch (te.binding.tag()) {
                case HIRTypePathBinding::TAG_Struct: {
                    of << StringView("s_") << TransMangle(te.path);
                    break;
                }
                case HIRTypePathBinding::TAG_Union: {
                    of << StringView("u_") << TransMangle(te.path);
                    break;
                }
                case HIRTypePathBinding::TAG_Enum: {
                    of << StringView("e_") << TransMangle(te.path);
                    break;
                }
                case HIRTypePathBinding::TAG_ExternType: {
                    of << StringView("x_") << TransMangle(te.path);
                    break;
                }
                case HIRTypePathBinding::TAG_Unbound: {
                    MIR_BUG(*mirRes, StringView("Unbound type path in trans - ") << ty);
                    break;
                }
                case HIRTypePathBinding::TAG_Opaque: {
                    MIR_BUG(*mirRes, StringView("Opaque path in trans - ") << ty);
                    break;
                }
            }
            if (!inner.empty()) {
                of << StringView(" ") << inner;
            }
            break;
        }
        case HIRTypeData::TAG_Generic: {
            MIR_BUG(*mirRes, StringView("Generic in trans - ") << ty);
            break;
        }
        case HIRTypeData::TAG_TraitObject: {
            MIR_BUG(*mirRes, StringView("Raw trait object - ") << ty);
            break;
        }
        case HIRTypeData::TAG_ErasedType: {
            MIR_BUG(*mirRes, StringView("ErasedType in trans - ") << ty);
            break;
        }
        case HIRTypeData::TAG_Array: {
            of << StringView("t_") << TransMangle(ty);
            if (!inner.empty()) {
                of << StringView(" ") << inner;
            }
            break;
        }
        case HIRTypeData::TAG_Slice: {
            MIR_BUG(*mirRes, StringView("Raw slice object - ") << ty);
            break;
        }
        case HIRTypeData::TAG_Tuple: {
            auto& te = (*ty).as_Tuple();
            if (te.size() == 0) {
                of << StringView("tUNIT");
            } else {
                of << StringView("TUP_") << te.size();
                for (const auto& t : te) {
                    of << StringView("_") << TransMangle(t);
                }
            }
            if (!inner.empty()) {
                of << StringView(" ") << inner;
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
            of << StringView("t_") << TransMangle(ty);
            if (!inner.empty()) {
                of << StringView(" ") << inner;
            }
            break;
        }
        case HIRTypeData::TAG_Function: {
            of << StringView("t_") << TransMangle(ty);
            if (!inner.empty()) {
                of << StringView(" ") << inner;
            }
            break;
        }
        case HIRTypeData::TAG_Pattern: {
            auto& te = (*ty).as_Pattern();
            emitCtypeCb(te.inner, inner, isExternC);
            break;
        } break;
        case HIRTypeData::TAG_NodeType:
            MIR_BUG(*mirRes, StringView("NodeType during trans - ") << ty);
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
                    MIR_BUG(*mirRes, StringView("Unbound/opaque path in trans - ") << ty);
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
                                    MIR_BUG(*mirRes, StringView("Unit-like struct with DstType::Possible"));
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
    const auto message = FMT(StringView("attempted to compute the size or alignment of extern type `") << ty << StringView("`"));
    const auto& panicPath = crate.getLangItemPath(sp, "panic_nounwind");
    const auto panicName = TransMangleValue(panicPath);
    of << StringView("{ extern tBANG ") << panicName << StringView("(SLICE_PTR); ");
    of << panicName << StringView("(SLICE_PTR{(void*)\"") << FmtEscaped(message) << StringView("\", ") << message.size() << StringView("}); abort(); }");
}

auto CodeGeneratorC::getPackingMaxAlign(const HIRTypeData* ty) const -> unsigned {
    if (ty->is_Path() && ty->as_Path().binding.is_Struct()) {
        return ty->as_Path().binding.as_Struct()->maxFieldAlignment;
    }
    return 0;
}

auto CodeGeneratorC::emitTraitObjectVtableSize(const MIRParam& value) -> void {
    of << StringView("((VTABLE_HDR*)");
    emitParam(value);
    of << StringView(".META)->size");
}

auto CodeGeneratorC::emitTraitObjectVtableAlign(const MIRParam& value) -> void {
    of << StringView("((VTABLE_HDR*)");
    emitParam(value);
    of << StringView(".META)->align");
}

auto CodeGeneratorC::emitDstTailAlign(const HIRTypeData* outerTy, const HIRTypeData* tailTy, const MIRParam& value) -> void {
    const auto maxAlign = getPackingMaxAlign(outerTy);
    if (maxAlign != 0) {
        of << StringView("trustme_min(");
    }
    emitDstAlign(tailTy, value);
    if (maxAlign != 0) {
        of << StringView(", ") << maxAlign << StringView(")");
    }
}

auto CodeGeneratorC::emitDstAlign(const HIRTypeData* ty, const MIRParam& value) -> void {
    if (ty->is_TraitObject()) {
        emitTraitObjectVtableAlign(value);
        return;
    }
    if (const auto* te = ty->opt_Slice()) {
        of << StringView("ALIGNOF(");
        emitCtype(te->inner);
        of << StringView(")");
        return;
    }
    if (ty == HIRCoreType::Str) {
        of << StringView("1");
        return;
    }

    const auto* repr = TargetGetTypeRepr(sp, resolve_, ty);
    MIR_ASSERT(*mirRes, repr && repr->size == SIZE_MAX && !repr->fields.empty(), StringView("Expected a DST wrapper - ") << ty);
    of << StringView("trustme_max(") << repr->align << StringView(", ");
    emitDstTailAlign(ty, repr->fields.back().ty, value);
    of << StringView(")");
}

auto CodeGeneratorC::emitDstSize(const HIRTypeData* ty, const MIRParam& value) -> void {
    if (ty->is_TraitObject()) {
        emitTraitObjectVtableSize(value);
        return;
    }
    if (const auto* te = ty->opt_Slice()) {
        size_t itemSize = 0, itemAlign = 0;
        MIR_ASSERT(*mirRes, TargetGetSizeAndAlignOf(sp, resolve_, te->inner, itemSize, itemAlign), StringView("Can't get size of ") << te->inner);
        emitParam(value);
        of << StringView(".META * ") << itemSize;
        return;
    }
    if (ty == HIRCoreType::Str) {
        emitParam(value);
        of << StringView(".META");
        return;
    }

    const auto* repr = TargetGetTypeRepr(sp, resolve_, ty);
    MIR_ASSERT(*mirRes, repr && repr->size == SIZE_MAX && !repr->fields.empty(), StringView("Expected a DST wrapper - ") << ty);
    const auto& tail = repr->fields.back();
    of << StringView("ALIGN_TO(ALIGN_TO(") << tail.offset << StringView(", ");
    emitDstTailAlign(ty, tail.ty, value);
    of << StringView(") + ");
    emitDstSize(tail.ty, value);
    of << StringView(", ");
    emitDstAlign(ty, value);
    of << StringView(")");
}

auto CodeGeneratorC::emitDstFieldOffset(const HIRTypeData* ty, size_t fieldIdx, const MIRParam& value) -> void {
    const auto* repr = TargetGetTypeRepr(sp, resolve_, ty);
    MIR_ASSERT(*mirRes, repr && fieldIdx < repr->fields.size(), StringView("Invalid DST field ") << fieldIdx << StringView(" on ") << ty);
    const auto& field = repr->fields[fieldIdx];
    auto innerTy = getInnerUnsizedType(field.ty);
    MIR_ASSERT(*mirRes, fieldIdx + 1 == repr->fields.size() && innerTy->is_TraitObject(), StringView("Expected final trait object field on ") << ty);
    of << StringView("ALIGN_TO(") << field.offset << StringView(", ");
    emitDstTailAlign(ty, field.ty, value);
    of << StringView(")");
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
            MIR_BUG(*mirRes, ty << StringView(" has unknown function-argument metadata"));
        case MetadataType::None:
        case MetadataType::Zero:
            emitCtypeCb(ty, inner);
            break;
        case MetadataType::Slice:
            of << StringView("void* ") << inner << StringView("_ptr, uintptr_t ") << inner << StringView("_meta");
            break;
        case MetadataType::TraitObject:
            of << StringView("void* ") << inner << StringView("_ptr, void* ") << inner << StringView("_meta");
            break;
    }
}

auto CodeGeneratorC::emitUnsizedArgumentLocal(const HIRTypeData* ty, unsigned index) -> void {
    switch (this->metadataType(ty)) {
        case MetadataType::Unknown:
            MIR_BUG(*mirRes, ty << StringView(" has unknown function-argument metadata"));
        case MetadataType::None:
        case MetadataType::Zero:
            return;
        case MetadataType::Slice:
            of << StringView("\tSLICE_PTR arg") << index << StringView(" = make_sliceptr(arg") << index << StringView("_ptr, arg") << index << StringView("_meta);\n");
            return;
        case MetadataType::TraitObject:
            of << StringView("\tTRAITOBJ_PTR arg") << index << StringView(" = make_traitobjptr(arg") << index << StringView("_ptr, arg") << index << StringView("_meta);\n");
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
    MIR_ASSERT(*mirRes, valueMeta == MetadataType::Slice || valueMeta == MetadataType::TraitObject, StringView("Expected an indirect DST lvalue - ") << value);

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
        MIR_ASSERT(*mirRes, base.is_Argument() && this->isDst(baseTy), StringView("DST access must be through a pointer or an unsized argument - ") << value);
    }

    if (base.wrapperCount() == value.wrapperCount()) {
        emitLvalue(basePointer);
        return;
    }

    of << (valueMeta == MetadataType::Slice ? "make_sliceptr(" : "make_traitobjptr(");
    of << StringView("(u8*)");
    emitLvalue(basePointer);
    of << StringView(".PTR");

    const auto baseParam = MIRParam::make_LValue(basePointer.clone());
    for (size_t i = base.wrapperCount(); i < value.wrapperCount(); i++) {
        const auto& wrapper = value.lv().wrappers[i];
        MIR_ASSERT(*mirRes, wrapper.is_Field(), StringView("Unexpected DST projection in ") << value);

        HIRTypeRef parentTmp;
        const auto& parentTy = mirRes->getLvalueType(parentTmp, MIRLValue::CRef(value.lv(), i));
        const auto* repr = TargetGetTypeRepr(sp, resolve_, parentTy);
        MIR_ASSERT(*mirRes, repr && wrapper.as_Field() < repr->fields.size(), StringView("Invalid DST field ") << wrapper.as_Field() << StringView(" on ") << parentTy);
        const auto& field = repr->fields[wrapper.as_Field()];

        of << StringView(" + ");
        if (this->metadataType(field.ty) == MetadataType::TraitObject) {
            emitDstFieldOffset(parentTy, wrapper.as_Field(), baseParam);
        } else {
            of << field.offset;
        }
    }

    of << StringView(", ");
    emitLvalue(basePointer);
    of << StringView(".META)");
}

auto CodeGeneratorC::emitDstParamPointer(const MIRParam& param) -> void {
    if (const auto* value = param.opt_LValue()) {
        emitDstLvaluePointer(MIRLValue::CRef(*value));
        return;
    }
    MIR_BUG(*mirRes, StringView("Unsized function argument isn't an lvalue - ") << param);
}

auto CodeGeneratorC::emitCtypePtr(const HIRTypeData* innerTy, CTypeCallback& inner) -> void {
    {
        switch (this->metadataType(innerTy)) {
            case MetadataType::Unknown:
                BUG(sp, innerTy << StringView(" unknown metadata type"));
            case MetadataType::None:
            case MetadataType::Zero: {
                auto callback = makeCallable<CTypeCb>([&](auto& os) {
                    os << StringView("*") << inner;
                });
                emitCtypeCb(innerTy, callback);
                break;
            }
            case MetadataType::Slice:
                of << StringView("SLICE_PTR");
                if (!inner.empty()) {
                    of << StringView(" ") << inner;
                }
                break;
            case MetadataType::TraitObject:
                of << StringView("TRAITOBJ_PTR");
                if (!inner.empty()) {
                    of << StringView(" ") << inner;
                }
                break;
        }
    }
}

auto CodeGeneratorC::isDst(const HIRTypeData* ty) const -> bool {
    switch (this->metadataType(ty)) {
        case MetadataType::Unknown:
            BUG(sp, ty << StringView(" unknown metadata type"));
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
        DEBUG(fmtLines.back());
    }

    for (const auto& p : params) {
        fmtParams.push_back(getParamText(p));
    }
}

auto CodeGeneratorC::Asm2TplMatch::matchesTemplate(std::initializer_list<const char*> lines, std::initializer_list<const char*> params) const -> bool {
    if (!checkList(fmtLines, lines)) {
        return false;
    }

    if (!checkList(fmtParams, params)) {
        MIR_BUG(mirRes, StringView("Hard-coded asm translation doesn't apply\n") << StringView("[") << fmtParams << StringView("] != \n[") << FMT_CB(os, for (auto it = params.begin(); it != params.end(); ++it) os << *it << StringView(", ")) << StringView("]"));
    }

    return true;
}

auto CodeGeneratorC::Asm2TplMatch::p(size_t i) const -> const MIRAsmParam& {
    return params.at(i);
}

auto CodeGeneratorC::Asm2TplMatch::input(size_t i) const -> const MIRParam& {
    MIR_ASSERT(mirRes, params.at(i).as_Reg().input, StringView("Parameter ") << i << StringView(" isn't a register input"));
    return *params.at(i).as_Reg().input;
}

auto CodeGeneratorC::Asm2TplMatch::output(size_t i) const -> const MIRLValue& {
    MIR_ASSERT(mirRes, params.at(i).as_Reg().output, StringView("Parameter ") << i << StringView(" isn't a register output"));
    return *params.at(i).as_Reg().output;
}

auto CodeGeneratorC::Asm2TplMatch::getParamText(const MIRAsmParam& p) -> std::string {
    switch (p.tag()) {
        case MIRAsmParam::TAG_Reg: {
            auto& e = p.as_Reg();
            switch (e.spec.tag()) {
                case AsmRegisterSpec::TAG_Explicit: {
                    auto& n = e.spec.as_Explicit();
                    return FMT(getDirText(e.dir) << StringView("=") << n);
                }
                case AsmRegisterSpec::TAG_Class: {
                    auto& c = e.spec.as_Class();
                    return FMT(getDirText(e.dir) << StringView(":") << to_string(c));
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

auto CodeGeneratorC::Asm2TplMatch::checkList(const std::vector<std::string>& have, const std::initializer_list<const char*>& exp) -> bool {
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
auto CodeGeneratorC::CTypeCb<F>::write(ZeroCopyOutput& os) const -> void {
    if constexpr (std::is_invocable_v<const F&, ZeroCopyOutput&>) {
        f(os);
    } else {
        os << f;
    }
}

template <typename F>
auto CodeGeneratorC::CTypeCb<F>::empty() const -> bool {
    return false;
}

auto CodeGeneratorC::EmptyCTypeCb::write(ZeroCopyOutput&) const -> void {
}

auto CodeGeneratorC::EmptyCTypeCb::empty() const -> bool {
    return true;
}

namespace stl {
template <>
void output<ZeroCopyOutput, CodeGeneratorC::CTypeCallback>(ZeroCopyOutput& os, const CodeGeneratorC::CTypeCallback& callback) {
    callback.write(os);
}

template <>
void output<ZeroCopyOutput, FmtShell>(ZeroCopyOutput& os, FmtShell x) {
        for (char c : x.s) {
            switch (c) {
                case '\\':
                case '\"':
                case ' ':
                    os << StringView("\\");
                default:
                    os << c;
            }
        }
        return;
    }

template <>
void output<ZeroCopyOutput, FmtGccAsm>(ZeroCopyOutput& os, FmtGccAsm x) {
        bool inComment = false;
        for (const char& ch : x.s) {
            if (ch == '/' && (&ch)[1] == '/') {
                if (!inComment) {
                    os << StringView("\" ");
                }
                inComment = true;
            } else {
                inComment = false;
            }
            switch (ch) {
                case '\n':
                    os << StringView("\\n\"\n\"");
                    break;
                case '\"':
                    os << StringView("\\\"");
                    break;
                case '%':
                    if (x.escapePercent) {
                        os << StringView("%%");
                    } else {
                        os << StringView("%");
                    }
                    break;
                case '{':
                    os << StringView("%{");
                    break;
                case '}':
                    os << StringView("%}");
                    break;
                case '|':
                    os << StringView("%|");
                    break;
                default:
                    os << ch;
                    break;
            }
        }
        return;
    }
}
