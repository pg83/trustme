#pragma once

#include "mir_mir.h"
#include "hir_expr.h"
#include "hir_type.h"
#include "hir_typeck_static.h"

#include <map>

class MirBuilder;

void HIRGenerateMIR(const WireBoard& wb, HIRCrate& crate);
void HIRGenerateMIRExpr(const WireBoard& wb, const HIRCrate& crate, const HIRItemPath& path, HIRExprPtr& exprPtr, const HIRFunction::argsT& args, const HIRTypeData* resTy);

struct MIRDropEmitter {
    virtual bool emitDeepDrop(const Span& sp, const MIRLValue& value, unsigned int flag) = 0;
    virtual bool emitShallowDrop(const Span& sp, const MIRLValue& value, unsigned int flag) = 0;
};

class ScopeHandle {
    friend class MirBuilder;

    const MirBuilder& builder;
    unsigned int idx;

    ScopeHandle(const MirBuilder& builder, unsigned int idx);

public:
    unsigned index() const {
        return idx;
    }

    ScopeHandle(const ScopeHandle& x) = delete;

    ScopeHandle(ScopeHandle&& x);

    ScopeHandle& operator=(const ScopeHandle& x) = delete;
    ScopeHandle& operator=(ScopeHandle&& x) = delete;
    ~ScopeHandle();
};

enum class InvalidType {
    Uninit,
    Moved,
    Descoped,
};

#include "mir_from_hir_tu.h"

struct SplitArm {
    bool hasEarlyTerminated = false;
    bool alwaysEarlyTerminated = false;

    std::map<unsigned int, VarState> states;
    std::map<unsigned int, VarState> argStates;
};

struct SplitEnd {
    std::map<unsigned int, VarState> states;
    std::map<unsigned int, VarState> argStates;
};

struct ScopeDropSlot {
    bool isArgument;
    unsigned int index;
};

#include "mir_from_hir_scope_tu.h"

#define FIELD_DEREF 0xFFFF
#define FIELD_INDEX_MAX 0x8000

struct fieldPathT {
    std::vector<u16> data;

    size_t size() const {
        return data.size();
    }

    void push_back(u16 v) {
        data.push_back(v);
    }

    void pop_back() {
        data.pop_back();
    }

    u16& back() {
        return data.back();
    }

    bool operator==(const fieldPathT& x) const {
        return data == x.data;
    }

    Ordering ord(const fieldPathT& x) const {
        return ::ord(data, x.data);
    }
};

struct PatternBinding {
    fieldPathT field;
    unsigned rootIndex;
    const HIRPatternBinding* binding;
    std::pair<size_t, size_t> splitSlice;

    PatternBinding(fieldPathT field, const HIRPatternBinding& binding, unsigned rootIndex = 0);

    bool isSplitSlice() const {
        return splitSlice.first != SIZE_MAX;
    }

    bool operator==(const PatternBinding& x) const {
        return rootIndex == x.rootIndex && field == x.field && binding == x.binding && splitSlice == x.splitSlice;
    }
};

class MirBuilder {
    friend class ScopeHandle;

    const Span& rootSpan;
    const StaticTraitResolve& resolve_;
    const HIRTypeData* retTy;
    const HIRFunction::argsT& args_;
    MIRFunction& output;

    const HIRSimplePath* langBox_;

    unsigned int currentBlock;
    bool blockActive_;
    bool buildingCleanup = false;
    const MIRLValue* unwindConsumedValue = nullptr;

    MIRRValue result;
    bool resultValid;

    // TODO: Extra information (e.g. mutability)
    VarState returnState;
    std::vector<VarState> argStates;
    std::vector<VarState> slotStates;
    size_t firstTempIdx;

    bool frozenExitStateActive = false;
    std::map<unsigned int, VarState> frozenExitSlotStates;
    std::map<unsigned int, VarState> frozenExitArgStates;

    std::map<unsigned, unsigned> varArgMappings;

    std::map<unsigned, std::vector<unsigned>> dropFlagAliases;

    struct ScopeDef {
        const Span& span;
        bool complete = false;
        ScopeType data;

        ScopeDef(const Span& span);

        ScopeDef(const Span& span, ScopeType data);
    };

    std::vector<ScopeDef> scopes;
    std::vector<unsigned int> scopeStack;
    ScopeHandle fcnScope_;

    typedef std::pair<HIRPatternBinding::Type, MIRLValue> varAliasT;
    std::vector<varAliasT> variableAliases;

    MIRLValue ifCondLval;

    MIRDropEmitter* dropEmitter = nullptr;

public:
    MirBuilder(const Span& sp, const StaticTraitResolve& resolve, const HIRTypeData* retTy, const HIRFunction::argsT& args, MIRFunction& output);

    void finalCleanup();

    const HIRSimplePath* langBox() const {
        return langBox_;
    }

    const HIRCrate& crate() const {
        return resolve_.hirCrate();
    }

    const StaticTraitResolve& resolve() const {
        return resolve_;
    }

    const HIRTypeData* isTypeOwnedBox(const HIRTypeData* ty) const;

    class SavedAliases {
        friend class MirBuilder;

        std::vector<bool> setAliases;
    };

    SavedAliases saveAliases() const;

    void restoreAliases(SavedAliases a);

    void addVariableAlias(const Span& sp, unsigned idx, HIRPatternBinding::Type ty, MIRLValue lv);

    const varAliasT* getVariableAlias(const Span& sp, unsigned idx) const;

    MIRLValue getVariable(const Span& sp, unsigned idx) const;

    MIRLValue newTemporary(const HIRTypeData* ty);

    void emitArrayElementDropLoop(const Span& sp, const MIRLValue& arrLv, size_t start, size_t end, unsigned int dropFlag);
    MIRLValue lvalueOrTemp(const Span& sp, const HIRTypeData* ty, MIRRValue val);

    size_t localCount() const {
        return output.locals.size();
    }

    bool hasResult() const {
        return resultValid;
    }

    void setResult(const Span& sp, MIRRValue val);
    MIRRValue getResult(const Span& sp);

    MIRLValue getResultUnwrapLvalue(const Span& sp);

    MIRLValue getResultInLvalue(const Span& sp, const HIRTypeData* ty, bool allowMissingValue = false);

    MIRParam getResultInParam(const Span& sp, const HIRTypeData* ty, bool allowMissingValue = false);

    MIRLValue getIfCond() const {
        return ifCondLval.clone();
    }

    MIRLValue getRvalInIfCond(const Span& sp, MIRRValue val);

    MIRLValue getResultInIfCond(const Span& sp) {
        return getRvalInIfCond(sp, getResult(sp));
    }

    void pushStmtAssign(const Span& sp, MIRLValue dst, MIRRValue val, bool updateDestState = true);

    void pushStmtDrop(const Span& sp, MIRLValue val, unsigned int dropFlag = ~0u);

    void pushStmtDropRaw(const Span& sp, MIRLValue val, unsigned int dropFlag = ~0u);

    void pushStmtDropShallow(const Span& sp, MIRLValue val, unsigned int dropFlag = ~0u);

    void pushStmtAsm(const Span& sp, MIRStatement::Data_Asm data);

    void pushStmtSetDropflagVal(const Span& sp, unsigned int index, bool value);
    void pushStmtSetDropflagOther(const Span& sp, unsigned int index, unsigned int other);
    void pushStmtSetDropflagDefault(const Span& sp, unsigned int index);

    void pushStmt(const Span& sp, MIRStatement stmt);

    void setDropEmitter(MIRDropEmitter* emitter) {
        dropEmitter = emitter;
    }

    bool blockActive() const {
        return blockActive_;
    }

    MIRBasicBlockId activeBlock() const {
        BUG_ASSERT(blockActive_);
        return currentBlock;
    }

    void markValueAssigned(const Span& sp, const MIRLValue& val);

    void raiseTemporaries(const Span& sp, const MIRLValue& val, const ScopeHandle& scope, bool toAbove = false);
    void raiseTemporaries(const Span& sp, const MIRRValue& rval, const ScopeHandle& scope, bool toAbove = false);

    class SaveCodeProto {
        friend class MirBuilder;
        size_t index;
    };

    SaveCodeProto codeSaveStart();

    class SavedCode {
        friend class MirBuilder;
        std::vector<unsigned> blocks;
    };

    SavedCode codeSaveEnd(SaveCodeProto h);

    class CloneMapper {
    public:
        virtual MIRBasicBlockId updateBbRef(MIRBasicBlockId bbIdx) = 0;
    };

    void insertCloned(const Span& sp, const SavedCode& c, CloneMapper& mapper);

private:
    void markValueAssignedState(const Span& sp, const MIRLValue& val, VarState newState);
    void markValueAssignedVariant(const Span& sp, const MIRLValue& val, unsigned int variantIndex);

    struct CodeSaveStackEnt {
        size_t index;

        std::vector<unsigned> blocks;
    };

    std::vector<CodeSaveStackEnt> codeSaveStack;
    size_t nextCodeSaveIndex = 0;

public:
    void setCurBlock(unsigned int newBlock);
    MIRBasicBlockId pauseCurBlock();

    void endBlock(MIRTerminator term);

    MIRBasicBlockId newBbLinked();
    MIRBasicBlockId newBbUnlinked();

    unsigned int newDropFlag(bool defaultState);
    unsigned int newDropFlagAndSet(const Span& sp, bool setState);
    bool getDropFlagDefault(const Span& sp, unsigned int index);

    void dropFlagAlias(unsigned int oldIdx, unsigned int newIdx);

    ScopeHandle newScopeVar(const Span& sp);

    ScopeHandle newScopeTemp(const Span& sp);

    ScopeHandle newScopeSplit(const Span& sp);

    ScopeHandle newScopeLoop(const Span& sp);

    ScopeHandle newScopeFreeze(const Span& sp);

    void raiseAll(const Span& sp, ScopeHandle src, const ScopeHandle& target);

    void terminateScope(const Span& sp, ScopeHandle, bool cleanup = true);

    void terminateScopeEarly(const Span& sp, const ScopeHandle&, bool loopExit = false);

    void endSplitArm(const Span& sp, const ScopeHandle&, bool reachable, bool early = false);
    /// Terminates the current split early (TODO: What does this mean?)
    void endSplitArmEarly(const Span& sp);

    void endSplitCondition(const Span& sp, const ScopeHandle& condition, const ScopeHandle& outer);

    void unfreezeScope(const Span& sp, const ScopeHandle&);

    const ScopeHandle& fcnScope() const {
        return fcnScope_;
    }

    void scheduleVariableDrop(unsigned int idx);

    void registerVariableState(unsigned int idx);

    void scheduleRegisteredVariableDrop(unsigned int idx);

    void scheduleArgumentDrop(unsigned int idx);

    void moveTemporaryDropToVariableScope(const Span& sp, const MIRLValue& value, const ScopeHandle& source);

    void moveVariableToScope(const Span& sp, unsigned int idx, const ScopeHandle& target);

    void dropLvalue(const Span& sp, const MIRLValue& value);

    void movedLvalue(const Span& sp, const MIRLValue& lv);

private:
    enum class SlotType {
        Local,

        Argument
    };
    const VarState& getSlotState(const Span& sp, unsigned int idx, SlotType type, const ScopeHandle* aboveScope = nullptr) const;
    VarState& getSlotStateMut(const Span& sp, unsigned int idx, SlotType type);

    VarState* getValStateMutP(const Span& sp, const MIRLValue& lv, bool expectValid = false);

    void mergeSplitLists(const Span& sp, const ScopeHandle& handle, const std::map<unsigned int, VarState>& states, std::map<unsigned int, VarState>& endStates, MirBuilder::SlotType type);

    void terminateLoopEarly(const Span& sp, ScopeType::Data_Loop& sdLoop);

    void dropValueFromState(const Span& sp, VarState& vs, MIRLValue lv);

    void dropScopeValues(ScopeDef& sd, bool preserveStates = false);
    MIRUnwindAction makeUnwindAction(const Span& sp, const MIRLValue* consumedValue = nullptr);
    void pushDropTerminator(const Span& sp, MIRDropKind kind, MIRLValue val, unsigned int dropFlag);

    void completeScope(ScopeDef& sd);

public:
    HIRTypeRef valType(const Span& sp, const MIRLValue& val, const MIRLValue::Wrapper* stopWrapper = nullptr) const;
    bool lvalueIsCopy(const Span& sp, const MIRLValue& lv) const;

    MIRLValue getPtrToDst(const Span& sp, const MIRLValue& lv) const;

    class SavedActiveLocal {
        friend class MirBuilder;
        VarState state;

        SavedActiveLocal(VarState vs);

    public:
        const VarState& getState() const {
            return state;
        }
    };

    std::map<unsigned, SavedActiveLocal> getActiveLocals(const Span& sp, std::set<unsigned>& savedDropFlags) const;

    void dropActveLocal(const Span& sp, MIRLValue lv, const SavedActiveLocal& loc);

    void emitUnwindCleanup(const Span& sp);
};

template <typename T>
struct SaveAndEditVal {
    T& dst;
    T saved;

    SaveAndEditVal(T& dst, T newval)
        : dst(dst)
        , saved(dst)
    {
        dst = mv$(newval);
    }

    ~SaveAndEditVal() {
        this->dst = this->saved;
    }
};

template <typename T>
SaveAndEditVal<T> saveAndEdit(T& dst, typename std::remove_reference<T&>::type newval) {
    return SaveAndEditVal<T>{dst, mv$(newval)};
}

using PatternDropOrder = HIRPatternBindingOrder;

class MirConverter: public HIRExprVisitor {
public:
    virtual void schedulePatternDrops(const Span& sp, const HIRPattern& pat, PatternDropOrder order) = 0;
    virtual void registerPatternVariables(const Span& sp, const HIRPattern& pat, PatternDropOrder order) = 0;
    virtual void scheduleRegisteredPatternDrops(const Span& sp, const HIRPattern& pat, PatternDropOrder order) = 0;

    virtual void destructureFromList(const Span& sp, const HIRTypeData* ty, MIRLValue lval, const std::vector<PatternBinding>& bindings, bool updateStates = true) = 0;
    virtual MIRLValue getValueForBindingPath(const Span& sp, const HIRTypeData* outerTy, const MIRLValue& outerLval, const PatternBinding& b) = 0;
    virtual const HIRTypeData* getBindingType(const Span& sp, unsigned index) const = 0;

    virtual SaveAndEditVal<const ScopeHandle*> disableBorrowExtension() = 0;
};

void MIRLowerHIRMatch(MirBuilder& builder, MirConverter& conv, HIRExprNodeMatch& node, MIRLValue matchVal, const std::vector<unsigned>& letElseInitializerTemps);
void MIRLowerHIRLet(MirBuilder& builder, MirConverter& conv, const Span& sp, const HIRPattern& pat, MIRLValue val, const HIRExprNode* elseNode);

void MIRLowerHIRGetTypeValueForPath(
    const Span& sp,
    MirBuilder& builder,
    const HIRTypeData* topTy,
    const MIRLValue& topVal,
    const fieldPathT& fieldPath,
    /*Out ->*/ HIRTypeRef& outTy,
    MIRLValue& outVal
);
