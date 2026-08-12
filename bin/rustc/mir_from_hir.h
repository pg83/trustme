#pragma once

#include "mir_mir.h"
#include "hir_expr.h" // for ExprNodeMatch
#include "hir_type.h"
#include "hir_typeck_static.h" // StaticTraitResolve for Copy

class MirBuilder;

class ScopeHandle {
    friend class MirBuilder;

    const MirBuilder& builder;
    unsigned int idx;

    ScopeHandle(const MirBuilder& builder, unsigned int idx);

public:
    ScopeHandle(const ScopeHandle& x) = delete;

    ScopeHandle(ScopeHandle&& x);

    ScopeHandle& operator=(const ScopeHandle& x) = delete;
    ScopeHandle& operator=(ScopeHandle&& x) = delete;
    ~ScopeHandle();

    friend ::std::ostream& operator<<(::std::ostream& os, const ScopeHandle& x);
};

// Rust 1.90 only permits a Box dereference to form a move path. A future
// DerefMove feature will need a corresponding state variant here.
enum class InvalidType {
    Uninit,
    Moved,
    Descoped,
};
// NOTE: If there's a optional move and a partial merging, it becomes a partial?
TAGGED_UNION_EX(
    VarState,
    (),
    Invalid,
    (
        // Currently invalid
        (Invalid, InvalidType),
        // Partially valid (Map of field states)
        (Partial,
         struct {
             ::std::vector<VarState> innerStates;
             unsigned int outerFlag; // If ~0u, the outer discriminant is always valid.
         }),
        (MovedOut,
         struct {
             ::std::unique_ptr<VarState> innerState;
             unsigned int outerFlag; // If ~0u, the outer is always valid. If set, then the outer may have been moved (but inner state still maybe valid)
         }),
        // Optionally valid (integer indicates the drop flag index)
        (Optional, unsigned int),
        // Fully valid
        (Valid, struct {})
    ),
    (),
    (),
    (VarState clone() const; bool operator==(const VarState & x) const; bool operator!=(const VarState & x) const { return !(*this == x); }
     /// Returns `true` if any drop flags were present (i.e. this is possibly optional)
     bool getUsedDropFlags(std::set<unsigned>* out) const;)
);
extern ::std::ostream& operator<<(::std::ostream& os, const VarState& x);

struct SplitArm {
    bool hasEarlyTerminated = false;
    bool alwaysEarlyTerminated = false; // Populated on completion
    //BasicBlockId  source_block;
    ::std::map<unsigned int, VarState> states;
    ::std::map<unsigned int, VarState> argStates;
};

struct SplitEnd {
    ::std::map<unsigned int, VarState> states;
    ::std::map<unsigned int, VarState> argStates;
};

struct ScopeDropSlot {
    bool isArgument;
    unsigned int index;
};

TAGGED_UNION(
    ScopeType,
    Owning,
    (Owning,
     struct {
         bool isTemporary;
         ::std::vector<unsigned int> slots;      // Locals whose state is owned by this scope
         ::std::vector<ScopeDropSlot> dropSlots; // Locals and arguments in scheduled drop order
     }),
    (Split,
     struct {
         bool endStateValid = false;
         SplitEnd condState;
         SplitEnd endState;
         ::std::vector<SplitArm> arms;
     }),
    (Loop,
     struct {
         // NOTE: This contains the original state for variables changed after `exit_state_valid` is true
         ::std::map<unsigned int, VarState> changedSlots;
         ::std::map<unsigned int, VarState> changedArgs;
         bool exitStateValid;
         SplitEnd exitState;
         // TODO: Any drop flags allocated in the loop must be re-initialised at the start of the loop (or before a loopback)
         MIRBasicBlockId entryBb;
         ::std::vector<unsigned> dropFlags;
     }),
    (Freeze, struct {
        /// Has `unfreeze_scope` been called on this entry?
        bool unfrozen = false;
    })
);

#define FIELD_DEREF 0xFFFF
#define FIELD_INDEX_MAX 0x8000 // Above this is a negative field offset

struct fieldPathT {
    ::std::vector<uint16_t> data;

    size_t size() const {
        return data.size();
    }

    void push_back(uint16_t v) {
        data.push_back(v);
    }

    void pop_back() {
        data.pop_back();
    }

    uint16_t& back() {
        return data.back();
    }

    bool operator==(const fieldPathT& x) const {
        return data == x.data;
    }

    Ordering ord(const fieldPathT& x) const {
        return ::ord(data, x.data);
    }

    friend ::std::ostream& operator<<(::std::ostream& os, const fieldPathT& x);
};

/// Binding from an expanded pattern
struct PatternBinding {
    fieldPathT field;
    const ::HIR::PatternBinding* binding;
    std::pair<size_t, size_t> splitSlice;

    PatternBinding(fieldPathT field, const ::HIR::PatternBinding& binding);

    bool isSplitSlice() const {
        return splitSlice.first != SIZE_MAX;
    }

    bool operator==(const PatternBinding& x) const {
        return field == x.field && binding == x.binding && splitSlice == x.splitSlice;
    }

    friend ::std::ostream& operator<<(::std::ostream& os, const PatternBinding& x);
};

/// Helper class to construct MIR
class MirBuilder {
    friend class ScopeHandle;

    const Span& rootSpan;
    const StaticTraitResolve& mResolve;
    const ::HIR::TypeData* retTy;
    const ::HIR::Function::argsT& mArgs;
    MIRFunction& output;

    const ::HIR::SimplePath* mLangBox;

    unsigned int currentBlock;
    bool mBlockActive;
    bool buildingCleanup = false;
    const MIRLValue* unwindConsumedValue = nullptr;

    MIRRValue result;
    bool resultValid;

    // TODO: Extra information (e.g. mutability)
    VarState returnState;
    ::std::vector<VarState> argStates;
    ::std::vector<VarState> slotStates;
    size_t firstTempIdx;

    // A diverging match guard is generated once and then cloned for each
    // alternative. Drops on that terminal path must update state for nested
    // unwind cleanups without mutating the canonical state being cloned.
    bool frozenExitStateActive = false;
    ::std::map<unsigned int, VarState> frozenExitSlotStates;
    ::std::map<unsigned int, VarState> frozenExitArgStates;

    /// Mapping between variable slots and MIR arguments (for when the argument is not destructuring)
    ::std::map<unsigned, unsigned> varArgMappings;

    /// Re-mapped dropped flags (all flags in the vector are to be set when the key flag is set)
    ::std::map<unsigned, std::vector<unsigned>> dropFlagAliases;

    struct ScopeDef {
        const Span& span;
        bool complete = false;
        ScopeType data;

        ScopeDef(const Span& span);

        ScopeDef(const Span& span, ScopeType data);
    };

    ::std::vector<ScopeDef> scopes;
    ::std::vector<unsigned int> scopeStack;
    ScopeHandle mFcnScope;

    typedef std::pair<HIR::PatternBinding::Type, MIRLValue> varAliasT;
    ::std::vector<varAliasT> variableAliases;

    // LValue used only for the condition of `if`
    // - Using a fixed temporary simplifies parts of lowering (scope related) and reduces load on
    //   the optimiser.
    MIRLValue ifCondLval;

public:
    MirBuilder(const Span& sp, const StaticTraitResolve& resolve, const ::HIR::TypeData* retTy, const ::HIR::Function::argsT& args, MIRFunction& output);

    void finalCleanup();

    const ::HIR::SimplePath* langBox() const {
        return mLangBox;
    }

    const ::HIR::Crate& crate() const {
        return mResolve.crate;
    }

    const StaticTraitResolve& resolve() const {
        return mResolve;
    }

    /// Check if the passed type is Box<T> and returns a pointer to the T type if so, otherwise nullptr
    const ::HIR::TypeData* isTypeOwnedBox(const ::HIR::TypeData* ty) const;

    class SavedAliases {
        friend class MirBuilder;
        // Just remember which variables had aliases on them, as we want to clear anything added while saving.
        ::std::vector<bool> setAliases;
    };

    /// Save the current state of aliases (see add_variable_alias)
    SavedAliases saveAliases() const;

    void restoreAliases(SavedAliases a);

    // Variable aliases (used for match guards)
    void addVariableAlias(const Span& sp, unsigned idx, HIR::PatternBinding::Type ty, MIRLValue lv);

    const varAliasT* getVariableAlias(const Span& sp, unsigned idx) const;

    // - Values
    MIRLValue getVariable(const Span& sp, unsigned idx) const;

    MIRLValue newTemporary(const ::HIR::TypeData* ty);
    MIRLValue lvalueOrTemp(const Span& sp, const ::HIR::TypeData* ty, MIRRValue val);

    size_t localCount() const {
        return output.locals.size();
    }

    bool hasResult() const {
        return resultValid;
    }

    void setResult(const Span& sp, MIRRValue val);
    MIRRValue getResult(const Span& sp);
    /// Obtains the result, unwrapping into a LValue (and erroring if not)
    MIRLValue getResultUnwrapLvalue(const Span& sp);
    /// Obtains the result, copying into a temporary if required
    MIRLValue getResultInLvalue(const Span& sp, const ::HIR::TypeData* ty, bool allowMissingValue = false);
    /// Obtains a result in a param (or a lvalue)
    MIRParam getResultInParam(const Span& sp, const ::HIR::TypeData* ty, bool allowMissingValue = false);

    MIRLValue getIfCond() const {
        return ifCondLval.clone();
    }

    MIRLValue getRvalInIfCond(const Span& sp, MIRRValue val);

    MIRLValue getResultInIfCond(const Span& sp) {
        return getRvalInIfCond(sp, getResult(sp));
    }

    // - Statements
    // Push an assignment. NOTE: This also marks the rvalue as moved
    void pushStmtAssign(const Span& sp, MIRLValue dst, MIRRValue val, bool updateDestState = true);
    // Push a drop (likely only used by scope cleanup)
    void pushStmtDrop(const Span& sp, MIRLValue val, unsigned int dropFlag = ~0u);
    // Push a shallow drop (for Box)
    void pushStmtDropShallow(const Span& sp, MIRLValue val, unsigned int dropFlag = ~0u);
    // Push an inline assembly statement (NOTE: inputs aren't marked as moved)
    void pushStmtAsm(const Span& sp, MIRStatement::Data_Asm data);
    // Push a setting/clearing of a drop flag
    void pushStmtSetDropflagVal(const Span& sp, unsigned int index, bool value);
    void pushStmtSetDropflagOther(const Span& sp, unsigned int index, unsigned int other);
    void pushStmtSetDropflagDefault(const Span& sp, unsigned int index);

    void pushStmt(const Span& sp, MIRStatement stmt);

    // - Block management
    bool blockActive() const {
        return mBlockActive;
    }

    // Mark a value as initialised (used for Call, because it has to be done after the panic block is populated)
    void markValueAssigned(const Span& sp, const MIRLValue& val);

    // Moves control of temporaries up to the specified scope (or to above it)
    void raiseTemporaries(const Span& sp, const MIRLValue& val, const ScopeHandle& scope, bool toAbove = false);
    void raiseTemporaries(const Span& sp, const MIRRValue& rval, const ScopeHandle& scope, bool toAbove = false);

    class SaveCodeProto {
        friend class MirBuilder;
        size_t index;
    };

    /// @brief Start saving code for later duplication (match guards)
    /// @return Handle to the current save stack entry
    SaveCodeProto codeSaveStart();

    class SavedCode {
        friend class MirBuilder;
        std::vector<unsigned> blocks;
    };

    /// @brief Complete and finalise saved code
    SavedCode codeSaveEnd(SaveCodeProto h);

    class CloneMapper {
    public:
        virtual MIRBasicBlockId updateBbRef(MIRBasicBlockId bbIdx) = 0;
    };

    /// @brief Insert saved code, applying the supplied mapper
    void insertCloned(const Span& sp, const SavedCode& c, CloneMapper& mapper);

private:
    struct CodeSaveStackEnt {
        /// Unique index to catch stack violations
        size_t index;
        /// Basic blocks in the copied region
        std::vector<unsigned> blocks;
    };

    std::vector<CodeSaveStackEnt> codeSaveStack;

public:
    void setCurBlock(unsigned int newBlock);
    MIRBasicBlockId pauseCurBlock();

    void endBlock(MIRTerminator term);

    MIRBasicBlockId newBbLinked();
    MIRBasicBlockId newBbUnlinked();

    unsigned int newDropFlag(bool defaultState);
    unsigned int newDropFlagAndSet(const Span& sp, bool setState);
    bool getDropFlagDefault(const Span& sp, unsigned int index);
    /// Add a drop flag to be set when another is also set (used to rewrite drop flags after the fact)
    void dropFlagAlias(unsigned int oldIdx, unsigned int newIdx);

    // --- Scopes ---
    /// Scope controlling the state of defined variables
    ScopeHandle newScopeVar(const Span& sp);
    /// Scope controlling the state of temporaries created within it
    ScopeHandle newScopeTemp(const Span& sp);
    /// Scope for split code paths (e.g. `if`)
    ScopeHandle newScopeSplit(const Span& sp);
    /// Scope for escapable code paths (e.g. `loop`)
    ScopeHandle newScopeLoop(const Span& sp);
    /// Prevent any mutation of states above this scope until `unfreeze_scope` is called
    ScopeHandle newScopeFreeze(const Span& sp);

    /// Raises every variable defined in the source scope into the target scope
    void raiseAll(const Span& sp, ScopeHandle src, const ScopeHandle& target);
    /// Drop all defined values in the scope (emits the drops if `cleanup` is set)
    void terminateScope(const Span& sp, ScopeHandle, bool cleanup = true);
    /// Terminates a scope early (e.g. via return/break/...)
    void terminateScopeEarly(const Span& sp, const ScopeHandle&, bool loopExit = false);
    /// Marks the end of a split arm (end match arm, if body, ...)
    void endSplitArm(const Span& sp, const ScopeHandle&, bool reachable, bool early = false);
    /// Terminates the current split early (TODO: What does this mean?)
    void endSplitArmEarly(const Span& sp);
    /// Terminates the current split condition clause (used for the conditional portion of a match arm)
    void endSplitCondition(const Span& sp, const ScopeHandle&);
    /// Allows mutation through a freeze scope (see `new_scope_freeze`)
    void unfreezeScope(const Span& sp, const ScopeHandle&);

    const ScopeHandle& fcnScope() const {
        return mFcnScope;
    }

    /// Schedule a local's value drop in the current variable scope.
    void scheduleVariableDrop(unsigned int idx);
    /// Register a local's state in the current variable scope without scheduling its drop.
    void registerVariableState(unsigned int idx);
    /// Schedule the drop of a local whose state is already registered.
    void scheduleRegisteredVariableDrop(unsigned int idx);
    /// Schedule an argument's value drop in the current variable scope.
    void scheduleArgumentDrop(unsigned int idx);
    /// Move a temporary's drop entry from `source` into the nearest variable scope.
    void moveTemporaryDropToVariableScope(const Span& sp, const MIRLValue& value, const ScopeHandle& source);
    /// Move a local binding from its lexical block into the scope selected for a `super let`.
    void moveVariableToScope(const Span& sp, unsigned int idx, const ScopeHandle& target);
    /// Drop a live value on the current control-flow path and mark it invalid.
    void dropLvalue(const Span& sp, const MIRLValue& value);
    // Helper - Marks a variable/... as moved (and checks if the move is valid)
    void movedLvalue(const Span& sp, const MIRLValue& lv);

private:
    enum class SlotType {
        /// @brief Local variable (either a binding or a temporary, it matters not). Maps to `LValue::Local`
        Local, // Local ~0u is return
        /// Function argument. Maps to `LValue::Argument`
        Argument
    };
    const VarState& getSlotState(const Span& sp, unsigned int idx, SlotType type, const ScopeHandle* aboveScope = nullptr) const;
    VarState& getSlotStateMut(const Span& sp, unsigned int idx, SlotType type);

    VarState* getValStateMutP(const Span& sp, const MIRLValue& lv, bool expectValid = false);

    void mergeSplitLists(const Span& sp, const ScopeHandle& handle, const ::std::map<unsigned int, VarState>& states, ::std::map<unsigned int, VarState>& endStates, MirBuilder::SlotType type);

    void terminateLoopEarly(const Span& sp, ScopeType::Data_Loop& sdLoop);

    void dropValueFromState(const Span& sp, VarState& vs, MIRLValue lv);
    void dropScopeValues(ScopeDef& sd);
    MIRUnwindAction makeUnwindAction(const Span& sp, const MIRLValue* consumedValue = nullptr);
    void pushDropTerminator(const Span& sp, MIRDropKind kind, MIRLValue val, unsigned int dropFlag);
    /// Finalise a scope before it's fully destroyed. Doesn't emit destructors (already done by `drop_scope_values`)
    void completeScope(ScopeDef& sd);

public:
    void withValType(const Span& sp, const MIRLValue& val, ::std::function<void(const ::HIR::TypeData*)> cb, const MIRLValue::Wrapper* stopWrapper = nullptr) const;
    bool lvalueIsCopy(const Span& sp, const MIRLValue& lv) const;

    // Obtain the base fat poiner for a dst reference. Errors if it wasn't via a fat pointer
    MIRLValue getPtrToDst(const Span& sp, const MIRLValue& lv) const;

    /// Get the set of currently valid (fully,optional,partial) variables
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

    // Calls `drop_value_from_state` on the value
    void dropActveLocal(const Span& sp, MIRLValue lv, const SavedActiveLocal& loc);

    /// Emits the drops needed when unwinding from the current point without
    /// changing the state used by the normal path.
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
SaveAndEditVal<T> saveAndEdit(T& dst, typename ::std::remove_reference<T&>::type newval) {
    return SaveAndEditVal<T>{dst, mv$(newval)};
}

using PatternDropOrder = ::HIR::PatternBindingOrder;

/// Wrapper interfae
class MirConverter: public ::HIR::ExprVisitor {
public:
    //virtual void destructure_from(const Span& sp, const ::HIR::Pattern& pat, ::MIR::LValue lval, bool allow_refutable=false) = 0;
    virtual void schedulePatternDrops(const Span& sp, const ::HIR::Pattern& pat, PatternDropOrder order) = 0;
    virtual void registerPatternVariables(const Span& sp, const ::HIR::Pattern& pat, PatternDropOrder order) = 0;
    virtual void scheduleRegisteredPatternDrops(const Span& sp, const ::HIR::Pattern& pat, PatternDropOrder order) = 0;

    virtual void destructureFromList(const Span& sp, const ::HIR::TypeData* ty, MIRLValue lval, const ::std::vector<PatternBinding>& bindings, bool updateStates = true) = 0;
    virtual MIRLValue getValueForBindingPath(const Span& sp, const ::HIR::TypeData* outerTy, const MIRLValue& outerLval, const PatternBinding& b) = 0;
    virtual const HIR::TypeData* getBindingType(const Span& sp, unsigned index) const = 0;

    virtual SaveAndEditVal<const ScopeHandle*> disableBorrowExtension() = 0;
};

extern void MIRLowerHIRMatch(MirBuilder& builder, MirConverter& conv, ::HIR::ExprNodeMatch& node, MIRLValue matchVal, const std::vector<unsigned>& letElseInitializerTemps);
extern void MIRLowerHIRLet(MirBuilder& builder, MirConverter& conv, const Span& sp, const ::HIR::Pattern& pat, MIRLValue val, const ::HIR::ExprNode* elseNode);

extern void MIRLowerHIRGetTypeValueForPath(
    const Span& sp,
    MirBuilder& builder,
    const ::HIR::TypeData* topTy,
    const MIRLValue& topVal,
    const fieldPathT& fieldPath, // unsigned int field_path_ofs,
    /*Out ->*/ ::HIR::TypeRef& outTy,
    MIRLValue& outVal
);
