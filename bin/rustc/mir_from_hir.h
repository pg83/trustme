#pragma once

#include "mir_mir.h"
#include "hir_type.h"
#include "hir_expr.h"          // for ExprNodeMatch
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
             ::std::vector<VarState> inner_states;
             unsigned int outer_flag; // If ~0u, the outer discriminant is always valid.
         }),
        (MovedOut,
         struct {
             ::std::unique_ptr<VarState> inner_state;
             unsigned int outer_flag; // If ~0u, the outer is always valid. If set, then the outer may have been moved (but inner state still maybe valid)
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
     bool get_used_drop_flags(std::set<unsigned>* out) const;)
);
extern ::std::ostream& operator<<(::std::ostream& os, const VarState& x);

struct SplitArm {
    bool has_early_terminated = false;
    bool alwaysEarlyTerminated = false; // Populated on completion
    //BasicBlockId  source_block;
    ::std::map<unsigned int, VarState> states;
    ::std::map<unsigned int, VarState> arg_states;
};

struct SplitEnd {
    ::std::map<unsigned int, VarState> states;
    ::std::map<unsigned int, VarState> arg_states;
};

struct ScopeDropSlot {
    bool is_argument;
    unsigned int index;
};

TAGGED_UNION(
    ScopeType,
    Owning,
    (Owning,
     struct {
         bool is_temporary;
         ::std::vector<unsigned int> slots; // Locals whose state is owned by this scope
         ::std::vector<ScopeDropSlot> drop_slots; // Locals and arguments in scheduled drop order
     }),
    (Split,
     struct {
         bool end_state_valid = false;
         SplitEnd condState;
         SplitEnd end_state;
         ::std::vector<SplitArm> arms;
     }),
    (Loop,
     struct {
         // NOTE: This contains the original state for variables changed after `exit_state_valid` is true
         ::std::map<unsigned int, VarState> changedSlots;
         ::std::map<unsigned int, VarState> changedArgs;
         bool exit_state_valid;
         SplitEnd exit_state;
         // TODO: Any drop flags allocated in the loop must be re-initialised at the start of the loop (or before a loopback)
         ::MIR::BasicBlockId entry_bb;
         ::std::vector<unsigned> drop_flags;
     }),
    (Freeze, struct {
        /// Has `unfreeze_scope` been called on this entry?
        bool unfrozen = false;
    })
);

#define FIELD_DEREF 0xFFFF
#define FIELD_INDEX_MAX 0x8000 // Above this is a negative field offset

struct field_path_t {
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

    bool operator==(const field_path_t& x) const {
        return data == x.data;
    }

    Ordering ord(const field_path_t& x) const {
        return ::ord(data, x.data);
    }

    friend ::std::ostream& operator<<(::std::ostream& os, const field_path_t& x);
};

/// Binding from an expanded pattern
struct PatternBinding {
    field_path_t field;
    const ::HIR::PatternBinding* binding;
    std::pair<size_t, size_t> split_slice;

    PatternBinding(field_path_t field, const ::HIR::PatternBinding& binding);

    bool is_split_slice() const {
        return split_slice.first != SIZE_MAX;
    }

    bool operator==(const PatternBinding& x) const {
        return field == x.field && binding == x.binding && split_slice == x.split_slice;
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
    ::MIR::Function& output;

    const ::HIR::SimplePath* mLangBox;

    unsigned int currentBlock;
    bool blockActive;
    bool buildingCleanup = false;
    const MIR::LValue* unwindConsumedValue = nullptr;

    ::MIR::RValue result;
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
    ScopeHandle fcnScope;

    typedef std::pair<HIR::PatternBinding::Type, MIR::LValue> var_alias_t;
    ::std::vector<var_alias_t> variableAliases;

    // LValue used only for the condition of `if`
    // - Using a fixed temporary simplifies parts of lowering (scope related) and reduces load on
    //   the optimiser.
    ::MIR::LValue ifCondLval;

public:
    MirBuilder(const Span& sp, const StaticTraitResolve& resolve, const ::HIR::TypeData* ret_ty, const ::HIR::Function::argsT& args, ::MIR::Function& output);

    void final_cleanup();

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
    const ::HIR::TypeData* is_type_owned_box(const ::HIR::TypeData* ty) const;

    class SavedAliases {
        friend class MirBuilder;
        // Just remember which variables had aliases on them, as we want to clear anything added while saving.
        ::std::vector<bool> set_aliases;
    };

    /// Save the current state of aliases (see add_variable_alias)
    SavedAliases save_aliases() const;

    void restore_aliases(SavedAliases a);

    // Variable aliases (used for match guards)
    void addVariableAlias(const Span& sp, unsigned idx, HIR::PatternBinding::Type ty, MIR::LValue lv);

    const var_alias_t* get_variable_alias(const Span& sp, unsigned idx) const;

    // - Values
    ::MIR::LValue get_variable(const Span& sp, unsigned idx) const;

    ::MIR::LValue new_temporary(const ::HIR::TypeData* ty);
    ::MIR::LValue lvalue_or_temp(const Span& sp, const ::HIR::TypeData* ty, ::MIR::RValue val);
    size_t local_count() const {
        return output.locals.size();
    }

    bool has_result() const {
        return resultValid;
    }

    void set_result(const Span& sp, ::MIR::RValue val);
    ::MIR::RValue get_result(const Span& sp);
    /// Obtains the result, unwrapping into a LValue (and erroring if not)
    ::MIR::LValue get_result_unwrap_lvalue(const Span& sp);
    /// Obtains the result, copying into a temporary if required
    ::MIR::LValue get_result_in_lvalue(const Span& sp, const ::HIR::TypeData* ty, bool allowMissingValue = false);
    /// Obtains a result in a param (or a lvalue)
    ::MIR::Param get_result_in_param(const Span& sp, const ::HIR::TypeData* ty, bool allowMissingValue = false);

    ::MIR::LValue get_if_cond() const {
        return ifCondLval.clone();
    }

    ::MIR::LValue get_rval_in_if_cond(const Span& sp, ::MIR::RValue val);

    ::MIR::LValue get_result_in_if_cond(const Span& sp) {
        return get_rval_in_if_cond(sp, get_result(sp));
    }

    // - Statements
    // Push an assignment. NOTE: This also marks the rvalue as moved
    void push_stmt_assign(const Span& sp, ::MIR::LValue dst, ::MIR::RValue val, bool update_dest_state = true);
    // Push a drop (likely only used by scope cleanup)
    void push_stmt_drop(const Span& sp, ::MIR::LValue val, unsigned int drop_flag = ~0u);
    // Push a shallow drop (for Box)
    void push_stmt_drop_shallow(const Span& sp, ::MIR::LValue val, unsigned int drop_flag = ~0u);
    // Push an inline assembly statement (NOTE: inputs aren't marked as moved)
    void push_stmt_asm(const Span& sp, ::MIR::Statement::Data_Asm data);
    // Push a setting/clearing of a drop flag
    void push_stmt_set_dropflag_val(const Span& sp, unsigned int index, bool value);
    void push_stmt_set_dropflag_other(const Span& sp, unsigned int index, unsigned int other);
    void push_stmt_set_dropflag_default(const Span& sp, unsigned int index);

    void push_stmt(const Span& sp, ::MIR::Statement stmt);

    // - Block management
    bool block_active() const {
        return blockActive;
    }

    // Mark a value as initialised (used for Call, because it has to be done after the panic block is populated)
    void mark_value_assigned(const Span& sp, const ::MIR::LValue& val);

    // Moves control of temporaries up to the specified scope (or to above it)
    void raise_temporaries(const Span& sp, const ::MIR::LValue& val, const ScopeHandle& scope, bool to_above = false);
    void raise_temporaries(const Span& sp, const ::MIR::RValue& rval, const ScopeHandle& scope, bool to_above = false);

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
        virtual MIR::BasicBlockId update_bb_ref(MIR::BasicBlockId bbIdx) = 0;
    };

    /// @brief Insert saved code, applying the supplied mapper
    void insert_cloned(const Span& sp, const SavedCode& c, CloneMapper& mapper);

private:
    struct CodeSaveStackEnt {
        /// Unique index to catch stack violations
        size_t index;
        /// Basic blocks in the copied region
        std::vector<unsigned> blocks;
    };

    std::vector<CodeSaveStackEnt> codeSaveStack;

public:
    void set_cur_block(unsigned int new_block);
    ::MIR::BasicBlockId pause_cur_block();

    void end_block(::MIR::Terminator term);

    ::MIR::BasicBlockId new_bb_linked();
    ::MIR::BasicBlockId new_bb_unlinked();

    unsigned int new_drop_flag(bool default_state);
    unsigned int new_drop_flag_and_set(const Span& sp, bool set_state);
    bool get_drop_flag_default(const Span& sp, unsigned int index);
    /// Add a drop flag to be set when another is also set (used to rewrite drop flags after the fact)
    void drop_flag_alias(unsigned int old_idx, unsigned int new_idx);

    // --- Scopes ---
    /// Scope controlling the state of defined variables
    ScopeHandle new_scope_var(const Span& sp);
    /// Scope controlling the state of temporaries created within it
    ScopeHandle new_scope_temp(const Span& sp);
    /// Scope for split code paths (e.g. `if`)
    ScopeHandle new_scope_split(const Span& sp);
    /// Scope for escapable code paths (e.g. `loop`)
    ScopeHandle new_scope_loop(const Span& sp);
    /// Prevent any mutation of states above this scope until `unfreeze_scope` is called
    ScopeHandle new_scope_freeze(const Span& sp);

    /// Raises every variable defined in the source scope into the target scope
    void raise_all(const Span& sp, ScopeHandle src, const ScopeHandle& target);
    /// Drop all defined values in the scope (emits the drops if `cleanup` is set)
    void terminate_scope(const Span& sp, ScopeHandle, bool cleanup = true);
    /// Terminates a scope early (e.g. via return/break/...)
    void terminate_scope_early(const Span& sp, const ScopeHandle&, bool loop_exit = false);
    /// Marks the end of a split arm (end match arm, if body, ...)
    void end_split_arm(const Span& sp, const ScopeHandle&, bool reachable, bool early = false);
    /// Terminates the current split early (TODO: What does this mean?)
    void end_split_arm_early(const Span& sp);
    /// Terminates the current split condition clause (used for the conditional portion of a match arm)
    void end_split_condition(const Span& sp, const ScopeHandle&);
    /// Allows mutation through a freeze scope (see `new_scope_freeze`)
    void unfreeze_scope(const Span& sp, const ScopeHandle&);

    const ScopeHandle& fcn_scope() const {
        return fcnScope;
    }

    /// Schedule a local's value drop in the current variable scope.
    void schedule_variable_drop(unsigned int idx);
    /// Register a local's state in the current variable scope without scheduling its drop.
    void register_variable_state(unsigned int idx);
    /// Schedule the drop of a local whose state is already registered.
    void schedule_registered_variable_drop(unsigned int idx);
    /// Schedule an argument's value drop in the current variable scope.
    void schedule_argument_drop(unsigned int idx);
    /// Move a temporary's drop entry from `source` into the nearest variable scope.
    void move_temporary_drop_to_variable_scope(const Span& sp, const ::MIR::LValue& value, const ScopeHandle& source);
    /// Move a local binding from its lexical block into the scope selected for a `super let`.
    void move_variable_to_scope(const Span& sp, unsigned int idx, const ScopeHandle& target);
    /// Drop a live value on the current control-flow path and mark it invalid.
    void drop_lvalue(const Span& sp, const ::MIR::LValue& value);
    // Helper - Marks a variable/... as moved (and checks if the move is valid)
    void moved_lvalue(const Span& sp, const ::MIR::LValue& lv);

private:
    enum class SlotType {
        /// @brief Local variable (either a binding or a temporary, it matters not). Maps to `LValue::Local`
        Local, // Local ~0u is return
        /// Function argument. Maps to `LValue::Argument`
        Argument
    };
    const VarState& get_slot_state(const Span& sp, unsigned int idx, SlotType type, const ScopeHandle* aboveScope = nullptr) const;
    VarState& get_slot_state_mut(const Span& sp, unsigned int idx, SlotType type);

    VarState* get_val_state_mut_p(const Span& sp, const ::MIR::LValue& lv, bool expect_valid = false);

    void merge_split_lists(const Span& sp, const ScopeHandle& handle, const ::std::map<unsigned int, VarState>& states, ::std::map<unsigned int, VarState>& end_states, MirBuilder::SlotType type);

    void terminate_loop_early(const Span& sp, ScopeType::Data_Loop& sd_loop);

    void drop_value_from_state(const Span& sp, VarState& vs, ::MIR::LValue lv);
    void drop_scope_values(ScopeDef& sd);
    ::MIR::UnwindAction make_unwind_action(const Span& sp, const ::MIR::LValue* consumedValue = nullptr);
    void push_drop_terminator(const Span& sp, ::MIR::eDropKind kind, ::MIR::LValue val, unsigned int drop_flag);
    /// Finalise a scope before it's fully destroyed. Doesn't emit destructors (already done by `drop_scope_values`)
    void completeScope(ScopeDef& sd);

public:
    void with_val_type(const Span& sp, const ::MIR::LValue& val, ::std::function<void(const ::HIR::TypeData*)> cb, const ::MIR::LValue::Wrapper* stop_wrapper = nullptr) const;
    bool lvalue_is_copy(const Span& sp, const ::MIR::LValue& lv) const;

    // Obtain the base fat poiner for a dst reference. Errors if it wasn't via a fat pointer
    ::MIR::LValue get_ptr_to_dst(const Span& sp, const ::MIR::LValue& lv) const;

    /// Get the set of currently valid (fully,optional,partial) variables
    class SavedActiveLocal {
        friend class MirBuilder;
        VarState state;

        SavedActiveLocal(VarState vs);

    public:
        const VarState& get_state() const {
            return state;
        }
    };

    std::map<unsigned, SavedActiveLocal> get_active_locals(const Span& sp, std::set<unsigned>& saved_drop_flags) const;

    // Calls `drop_value_from_state` on the value
    void drop_actve_local(const Span& sp, ::MIR::LValue lv, const SavedActiveLocal& loc);

    /// Emits the drops needed when unwinding from the current point without
    /// changing the state used by the normal path.
    void emit_unwind_cleanup(const Span& sp);
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
SaveAndEditVal<T> save_and_edit(T& dst, typename ::std::remove_reference<T&>::type newval) {
    return SaveAndEditVal<T>{dst, mv$(newval)};
}

using PatternDropOrder = ::HIR::PatternBindingOrder;

/// Wrapper interfae
class MirConverter: public ::HIR::ExprVisitor {
public:
    //virtual void destructure_from(const Span& sp, const ::HIR::Pattern& pat, ::MIR::LValue lval, bool allow_refutable=false) = 0;
    virtual void schedule_pattern_drops(const Span& sp, const ::HIR::Pattern& pat, PatternDropOrder order) = 0;
    virtual void register_pattern_variables(const Span& sp, const ::HIR::Pattern& pat, PatternDropOrder order) = 0;
    virtual void schedule_registered_pattern_drops(const Span& sp, const ::HIR::Pattern& pat, PatternDropOrder order) = 0;

    virtual void destructure_from_list(const Span& sp, const ::HIR::TypeData* ty, ::MIR::LValue lval, const ::std::vector<PatternBinding>& bindings, bool update_states = true) = 0;
    virtual MIR::LValue get_value_for_binding_path(const Span& sp, const ::HIR::TypeData* outer_ty, const ::MIR::LValue& outer_lval, const PatternBinding& b) = 0;
    virtual const HIR::TypeData* get_binding_type(const Span& sp, unsigned index) const = 0;

    virtual SaveAndEditVal<const ScopeHandle*> disable_borrow_extension() = 0;
};

extern void MIRLowerHIRMatch(MirBuilder& builder, MirConverter& conv, ::HIR::ExprNodeMatch& node, ::MIR::LValue match_val, const std::vector<unsigned>& let_else_initializer_temps);
extern void MIRLowerHIRLet(MirBuilder& builder, MirConverter& conv, const Span& sp, const ::HIR::Pattern& pat, ::MIR::LValue val, const ::HIR::ExprNode* else_node);

extern void MIRLowerHIRGetTypeValueForPath(
    const Span& sp,
    MirBuilder& builder,
    const ::HIR::TypeData* top_ty,
    const ::MIR::LValue& top_val,
    const field_path_t& field_path, // unsigned int field_path_ofs,
    /*Out ->*/ ::HIR::TypeRef& out_ty,
    ::MIR::LValue& out_val
);
