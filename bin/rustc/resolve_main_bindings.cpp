#include "resolve_main_bindings.h"

#include "ast_crate.h"
#include "ast_ast.h"
#include "ast_expr.h"
#include "main_bindings.h"
#include "hir_hir.h"

#define FLAG_CONST_GENERIC (1u << 31)

namespace {
    static const RcString rcstring_Self = RcString::new_interned("Self");

    AST::AbsolutePath sp_to_ap(const HIR::SimplePath& sp) {
        return AST::AbsolutePath(sp.crate_name(), sp.components_vec());
    }

    struct GenericSlot {
        enum class Level {
            Top,
            Method,
            Unused_Placeholder,
            Hrb,
        } level;
        unsigned short index;

        unsigned int to_binding() const {
            if (level == Level::Method && index != 0xFFFF) {
                return (unsigned int)index + 256 * static_cast<unsigned int>(level);
            } else {
                return (unsigned int)index;
            }
        }
    };

    template <typename Val>
    struct Named {
        RcString name;
        Val value;
    };

    template <typename Val>
    struct NamedI {
        const Ident& name;
        Val value;
    };

    struct Context {
        TAGGED_UNION(
            Ent,
            Module,
            (Module, struct { const ::AST::Module* mod; }),
            (ConcreteSelf, const TypeRef*),
            (VarBlock,
             struct {
                 unsigned int level;
                 // "Map" of names to function-level variable slots
                 ::std::vector<::std::pair<Ident, unsigned int>> variables;
             }),
            (Generic, struct {
                // Map of names to slots
                GenericSlot::Level level;
                ::AST::GenericParams* params_def; // TODO: What if it's HRBs?, they have a different type
                //::AST::HigherRankedBounds*  hrbs_def;
                ::std::vector<Named<GenericSlot>> types;
                ::std::vector<NamedI<GenericSlot>> constants;
                ::std::vector<NamedI<GenericSlot>> lifetimes;
            })
        );

        const ::AST::Crate& m_crate;
        const ::AST::Module& m_mod;
        ::std::vector<Ent> m_name_context;

        struct PatternStackEnt {
            unsigned first_arm_done = false;
            std::set<Ident> created_variables;
            std::set<Ident> first_arm_variables;
        };

        ::std::vector<PatternStackEnt> m_pattern_stack;
        unsigned int m_var_count;
        unsigned int m_block_level;

        // Destination `GenericParams` for in_band_lifetimes
        ::AST::GenericParams* m_ibl_target_generics;

        Context(const ::AST::Crate& crate, const ::AST::Module& mod)
            : m_crate(crate)
            , m_mod(mod)
            , m_var_count(~0u)
            , m_block_level(0)
            , m_ibl_target_generics(nullptr)
        {
        }

        void push(const ::AST::HigherRankedBounds& params) {
            auto e = Ent::make_Generic({GenericSlot::Level::Hrb, nullptr /*, &params*/});
            auto& data = e.as_Generic();

            for (size_t i = 0; i < params.m_lifetimes.size(); i++) {
                data.lifetimes.push_back(NamedI<GenericSlot>{params.m_lifetimes[i].name(), GenericSlot{GenericSlot::Level::Hrb, static_cast<unsigned short>(i)}});
            }

            m_name_context.push_back(mv$(e));
        }

        void push(/*const */ ::AST::GenericParams& params, GenericSlot::Level level, bool has_self = false) {
            auto e = Ent::make_Generic({level, &params});
            auto& data = e.as_Generic();

            if (has_self) {
                //assert( level == GenericSlot::Level::Top );
                data.types.push_back(Named<GenericSlot>{rcstring_Self, GenericSlot{level, GENERIC_Self}});
                m_name_context.push_back(Ent::make_ConcreteSelf(nullptr));
            }
            if (!params.m_params.empty()) {
                unsigned short lft_idx = 0;
                unsigned short ty_idx = 0;
                unsigned short val_idx = 0;
                for (const auto& e : params.m_params) {
                    TU_MATCH_HDRA( (e), {)
                    TU_ARMA(None, param) {
                        }
                        TU_ARMA(Lifetime, lft) {
                            data.lifetimes.push_back(NamedI<GenericSlot>{lft.name(), GenericSlot{level, lft_idx}});
                            lft_idx += 1;
                        }
                        TU_ARMA(Type, ty_def) {
                            data.types.push_back(Named<GenericSlot>{ty_def.name(), GenericSlot{level, ty_idx}});
                            ty_idx += 1;
                        }
                        TU_ARMA(Value, val_def) {
                            data.constants.push_back(NamedI<GenericSlot>{val_def.name(), GenericSlot{level, val_idx}});
                            val_idx += 1;
                        }
                    }
                }
            }

            m_name_context.push_back(mv$(e));
        }

        void pop(const ::AST::HigherRankedBounds&) {
            if (!m_name_context.back().is_Generic()) {
                BUG(Span(), "resolve/absolute.cpp - Context::pop(GenericParams) - Mismatched pop");
            }
            m_name_context.pop_back();
        }

        void pop(const ::AST::GenericParams&, bool has_self = false) {
            if (!m_name_context.back().is_Generic()) {
                BUG(Span(), "resolve/absolute.cpp - Context::pop(GenericParams) - Mismatched pop");
            }
            m_name_context.pop_back();
            if (has_self) {
                if (!m_name_context.back().is_ConcreteSelf()) {
                    BUG(Span(), "resolve/absolute.cpp - Context::pop(GenericParams) - Mismatched pop");
                }
                m_name_context.pop_back();
            }
        }

        void push(const ::AST::Module& mod) {
            m_name_context.push_back(Ent::make_Module({&mod}));
        }

        void pop(const ::AST::Module& mod) {
            if (!m_name_context.back().is_Module()) {
                BUG(Span(), "resolve/absolute.cpp - Context::pop(GenericParams) - Mismatched pop");
            }
            m_name_context.pop_back();
        }

        class RootBlockScope {
            friend struct Context;
            Context& ctxt;
            unsigned int old_varcount;

            RootBlockScope(Context& ctxt, unsigned int val)
                : ctxt(ctxt)
                , old_varcount(ctxt.m_var_count)
            {
                ctxt.m_var_count = val;
            }

        public:
            ~RootBlockScope() {
                ctxt.m_var_count = old_varcount;
            }
        };

        RootBlockScope enter_rootblock() {
            return RootBlockScope(*this, 0);
        }

        RootBlockScope clear_rootblock() {
            return RootBlockScope(*this, ~0u);
        }

        void push_self(const TypeRef& tr) {
            m_name_context.push_back(Ent::make_ConcreteSelf(&tr));
        }

        void pop_self(const TypeRef& tr) {
            TU_IFLET(Ent, m_name_context.back(), ConcreteSelf, e, m_name_context.pop_back();)
            else {
                BUG(Span(), "resolve/absolute.cpp - Context::pop(TypeRef) - Mismatched pop");
            }
        }

        ::TypeRef get_self() const {
            for (auto it = m_name_context.rbegin(); it != m_name_context.rend(); ++it) {
                TU_MATCH_DEF(Ent, (*it), (e), (), (ConcreteSelf, if (false && e) { return e->clone(); } else { return ::TypeRef(Span(), rcstring_Self, GENERIC_Self); }))
            }

            TODO(Span(), "Error when get_self called with no self");
        }

        const ::TypeRef* get_self_opt() const {
            for (auto it = m_name_context.rbegin(); it != m_name_context.rend(); ++it) {
                if (const auto* e = it->opt_ConcreteSelf()) {
                    return *e;
                }
            }
            return nullptr;
        }

        void push_block() {
            m_block_level += 1;
            DEBUG("Push block to " << m_block_level);
        }

        unsigned int push_var(const Span& sp, const Ident& name) {
            if (m_var_count == ~0u) {
                BUG(sp, "Assigning local when there's no variable context");
            }
            // If this variable is defined within a stack entry, then use it
            ASSERT_BUG(sp, !m_pattern_stack.empty(), "Pushing a variable with no active scopes");
            bool already_defined = m_pattern_stack.back().first_arm_done;
            for (auto it = m_pattern_stack.rbegin(); it != m_pattern_stack.rend(); ++it) {
                if (it->first_arm_variables.count(name)) {
                    already_defined = true;
                    break;
                }
            }
            if (!m_pattern_stack.back().created_variables.insert(name).second) {
                ERROR(sp, E0000, "Duplicate definition of `" << name << "` in pattern arm");
            }
            // Are we currently in the second (or later) arm of a split pattern
            if (already_defined) {
                if (!m_name_context.back().is_VarBlock()) {
                    BUG(sp, "resolve/absolute.cpp - Context::push_var - No block");
                }
                auto& vb = m_name_context.back().as_VarBlock();
                // Work backwards, in case there are multiple bindings in the same scope.
                for (const auto& v : ::reverse(vb.variables)) {
                    if (v.first == name) {
                        DEBUG("Arm defined var @ " << m_block_level << ": #" << v.second << " " << name);
                        return v.second;
                    }
                }
                ERROR(sp, E0000, "Mismatched bindings in pattern (`" << name << "` wasn't in the first arm)");
            } else {
                assert(m_block_level > 0);
                if (m_name_context.empty() || !m_name_context.back().is_VarBlock() || m_name_context.back().as_VarBlock().level < m_block_level) {
                    m_name_context.push_back(Ent::make_VarBlock({m_block_level, {}}));
                }
                DEBUG("New var @ " << m_block_level << ": #" << m_var_count << " " << name);
                auto& vb = m_name_context.back().as_VarBlock();
                assert(vb.level == m_block_level);
                vb.variables.push_back(::std::make_pair(mv$(name), m_var_count));
                m_var_count += 1;
                assert(m_var_count >= vb.variables.size());
                return m_var_count - 1;
            }
        }

        void pop_block() {
            assert(m_block_level > 0);
            if (m_name_context.size() > 0 && m_name_context.back().is_VarBlock() && m_name_context.back().as_VarBlock().level == m_block_level) {
                DEBUG("Pop block from " << m_block_level << " with vars:" << FMT_CB(os, for (const auto& v : m_name_context.back().as_VarBlock().variables) os << " " << v.first << "#" << v.second;));
                m_name_context.pop_back();
            } else {
                DEBUG("Pop block from " << m_block_level << " - no vars");
                for (const auto& ent : ::reverse(m_name_context)) {
                    TU_IFLET(
                        Ent,
                        ent,
                        VarBlock,
                        e,
                        //DEBUG("Block @" << e.level << ": " << e.variables.size() << " vars");
                        assert(e.level < m_block_level);
                    )
                }
            }
            m_block_level -= 1;
        }

        /// Indicate that a multiple-pattern binding is started
        void start_patbind() {
            assert(m_block_level > 0);
            m_pattern_stack.push_back(PatternStackEnt());
        }

        /// Freeze the set of pattern bindings
        void end_patbind_arm(const Span& sp) {
            auto& e = m_pattern_stack.back();
            if (e.first_arm_done) {
                if (e.first_arm_variables != e.created_variables) {
                    ERROR(sp, E0000, "Mismatched bindings in pattern - [" << e.first_arm_variables << "] != [" << e.created_variables << "]");
                }
            } else {
                e.first_arm_variables = std::move(e.created_variables);
                e.first_arm_done = true;
            }
            e.created_variables.clear();
        }

        /// End a multiple-pattern binding state (unfreeze really)
        void end_patbind() {
            assert(!m_pattern_stack.empty());
            // Propagate the created variables to the next level up.
            if (m_pattern_stack.size() > 1) {
                const auto& cur = m_pattern_stack[m_pattern_stack.size() - 1];
                auto& next = m_pattern_stack[m_pattern_stack.size() - 2];
                for (auto& var : cur.first_arm_variables) {
                    next.created_variables.insert(std::move(var));
                }
            }
            m_pattern_stack.pop_back();
        }

        enum class LookupMode {
            Namespace,
            Type,
            Constant,
            PatternValue,
            PatternType,
            Variable,
        };

        static const char* lookup_mode_msg(LookupMode mode) {
            switch (mode) {
                case LookupMode::Namespace:
                    return "path component";
                case LookupMode::Type:
                    return "type name";
                case LookupMode::PatternValue:
                    return "pattern constant";
                case LookupMode::PatternType:
                    return "pattern type";
                case LookupMode::Constant:
                    return "constant name";
                case LookupMode::Variable:
                    return "variable name";
            }
            return "";
        }

        AST::Path lookup(const Span& sp, const RcString& name, const Ident::Hygiene& src_context, LookupMode mode) const {
            auto rv = this->lookup_opt(name, src_context, mode);
            if (!rv.is_valid()) {
                switch (mode) {
                    case LookupMode::Namespace:
                        ERROR(sp, E0000, "Couldn't find path component '" << name << "'");
                    case LookupMode::Type:
                        ERROR(sp, E0000, "Couldn't find type name '" << name << "'");
                    case LookupMode::PatternValue:
                        ERROR(sp, E0000, "Couldn't find pattern value '" << name << "'");
                    case LookupMode::PatternType:
                        ERROR(sp, E0000, "Couldn't find pattern type '" << name << "'");
                    case LookupMode::Constant:
                        ERROR(sp, E0000, "Couldn't find constant name '" << name << "'");
                    case LookupMode::Variable:
                        ERROR(sp, E0000, "Couldn't find variable name '" << name << "'");
                }
            }
            return rv;
        }

        static bool lookup_in_mod(const ::AST::Module& mod, const RcString& name, LookupMode mode, ::AST::Path& path) {
            switch (mode) {
                case LookupMode::Namespace: {
                    auto v = mod.m_namespace_items.find(name);
                    if (v != mod.m_namespace_items.end()) {
                        DEBUG("- " << mod.path() << " NS: Namespace " << v->second.path);
                        path = ::AST::Path(v->second.path);
                        return true;
                    }
                }
                    {
                        auto v = mod.m_type_items.find(name);
                        if (v != mod.m_type_items.end()) {
                            DEBUG("- " << mod.path() << " NS: Type " << v->second.path);
                            path = ::AST::Path(v->second.path);
                            return true;
                        }
                    }
                    break;

                case LookupMode::Type:
                case LookupMode::PatternType: {
                    auto v = mod.m_type_items.find(name);
                    if (v != mod.m_type_items.end()) {
                        DEBUG("- " << mod.path() << " TY: Type " << v->second.path);
                        path = ::AST::Path(v->second.path);
                        return true;
                    }
                }
                    // HACK: For `Enum::Var { .. }` patterns matching value variants
                    if (mode == LookupMode::PatternType) {
                        auto v = mod.m_value_items.find(name);
                        if (v != mod.m_value_items.end()) {
                            const auto& b = v->second.path.m_bindings.value;
                            if (/*const auto* be =*/b.binding.opt_EnumVar()) {
                                DEBUG("- " << mod.path() << " TY: Enum variant " << b.path);
                                path = ::AST::Path(b);
                                return true;
                            }
                        }
                    }
                    break;
                case LookupMode::PatternValue: {
                    auto v = mod.m_value_items.find(name);
                    if (v != mod.m_value_items.end()) {
                        const auto& b = v->second.path.m_bindings.value;
                        switch (b.binding.tag()) {
                            case ::AST::PathBinding_Value::TAG_EnumVar:
                            case ::AST::PathBinding_Value::TAG_Static:
                                DEBUG("- PV: Value " << v->second.path);
                                path = ::AST::Path(v->second.path);
                                return true;
                            case ::AST::PathBinding_Value::TAG_Struct: {
                                const auto& be = b.binding.as_Struct();
                                // TODO: Restrict this to unit-like structs
                                if (be.struct_ && !be.struct_->m_data.is_Unit())
                                    ;
                                else if (be.hir && !be.hir->m_data.is_Unit())
                                    ;
                                else {
                                    DEBUG("- " << mod.path() << " PV: Value " << b.path);
                                    path = ::AST::Path(b);
                                    return true;
                                }
                                break;
                            }
                            default:
                                break;
                        }
                    }
                } break;
                case LookupMode::Constant:
                case LookupMode::Variable: {
                    auto v = mod.m_value_items.find(name);
                    if (v != mod.m_value_items.end()) {
                        DEBUG("- " << mod.path() << " C/V: Value " << v->second.path);
                        path = ::AST::Path(v->second.path);
                        return true;
                    }
                } break;
            }
            return false;
        }

        AST::Path lookup_opt(const RcString& name, const Ident::Hygiene& src_context, LookupMode mode) const {
            DEBUG("name=" << name << ", src_context=" << src_context);
            // NOTE: src_context may provide a module to search
            // TODO: This should be checked AFTER locals
            if (src_context.has_mod_path()) {
                const auto& mp = src_context.mod_path();
                DEBUG(mp);
                if (mp.crate != "") {
                    HIR::SimplePath vis_path{mp.crate, mp.ents};

                    static Span sp;
                    // External crate path
                    ASSERT_BUG(sp, m_crate.m_extern_crates.count(mp.crate), "Crate not loaded for " << mp);
                    const auto& crate = m_crate.m_extern_crates.at(mp.crate);
                    const HIR::Module* mod = &crate.m_hir->m_root_module;
                    for (const auto& n : mp.ents) {
                        ASSERT_BUG(sp, mod->m_mod_items.count(n), "Node `" << n << "` missing in path " << mp);
                        const auto& i = *mod->m_mod_items.at(n);
                        ASSERT_BUG(sp, i.ent.is_Module(), "Node `" << n << "` not a module in path " << mp);
                        mod = &i.ent.as_Module();
                    }
                    AST::Path::Bindings bindings;
                    const HIR::SimplePath* true_path = nullptr;
                    switch (mode) {
                        case LookupMode::Constant:
                        case LookupMode::PatternValue:
                        case LookupMode::Variable: {
                            auto it = mod->m_value_items.find(name);
                            if (it != mod->m_value_items.end()) {
                                const auto* item = &it->second->ent;
                                auto item_path = AST::AbsolutePath(mp.crate, mp.ents) + name;
                                if (item->is_Import()) {
                                    const auto& imp = item->as_Import();
                                    // Set the true path (so the returned path is canonical)
                                    true_path = &imp.path;

                                    auto item_path = sp_to_ap(imp.path) + name;
                                    if (imp.is_variant) {
                                        const auto& enm = m_crate.m_extern_crates.at(imp.path.crate_name()).m_hir->get_enum_by_path(sp, imp.path, /*ignore_crate_name*/ true, /*ignore_last*/ true);
                                        bindings.value.set(item_path, AST::PathBinding_Value::make_EnumVar({nullptr, imp.idx, &enm}));
                                        break; // Break out of the switch
                                    } else {
                                        item = &m_crate.m_extern_crates.at(imp.path.crate_name()).m_hir->get_valitem_by_path(sp, imp.path, true);
                                    }
                                }
                            TU_MATCH_HDRA( (*item), {)
                            default:
                                TODO(sp, "Bind value '" << name << "' for module path " << mp << " : " << item->tag_str());
                                    TU_ARMA(Function, e) {
                                        bindings.value.set(item_path, AST::PathBinding_Value::make_Function({nullptr}));
                                    }
                                    TU_ARMA(Static, e) {
                                        bindings.value.set(item_path, AST::PathBinding_Value::make_Static({nullptr}));
                                    }
                            }
                            }
                        } break;
                        case LookupMode::Namespace:
                        case LookupMode::PatternType:
                        case LookupMode::Type: {
                            auto it = mod->m_mod_items.find(name);
                            if (it != mod->m_mod_items.end()) {
                                const auto* item = &it->second->ent;
                                auto item_path = AST::AbsolutePath(mp.crate, mp.ents) + name;
                                if (item->is_Import()) {
                                    const auto& imp = item->as_Import();
                                    // Set the true path (so the returned path is canonical)
                                    true_path = &imp.path;

                                    auto item_path = sp_to_ap(imp.path) + name;
                                    if (imp.is_variant) {
                                        const auto& enm = m_crate.m_extern_crates.at(imp.path.crate_name()).m_hir->get_enum_by_path(sp, imp.path, /*ignore_crate_name*/ true, /*ignore_last*/ true);
                                        bindings.type.set(item_path, AST::PathBinding_Type::make_EnumVar({nullptr, imp.idx, &enm}));
                                        break; // Break out of the switch
                                    } else {
                                        item = &m_crate.m_extern_crates.at(imp.path.crate_name()).m_hir->get_typeitem_by_path(sp, imp.path, true);
                                    }
                                }
                            TU_MATCH_HDRA( (*item), {)
                            default:
                                TODO(sp, "Bind type/mod '" << name << "' for module path " << mp << " : " << item->tag_str());
                                    TU_ARMA(Module, e) {
                                        bindings.type.set(item_path, AST::PathBinding_Type::make_Module({nullptr, {&crate, &e}}));
                                    }
                                    TU_ARMA(Trait, e) {
                                        bindings.type.set(item_path, AST::PathBinding_Type::make_Trait({nullptr}));
                                    }
                                    TU_ARMA(TypeAlias, e) {
                                        bindings.type.set(item_path, AST::PathBinding_Type::make_TypeAlias({nullptr}));
                                    }
                                    TU_ARMA(Struct, e) {
                                        bindings.type.set(item_path, AST::PathBinding_Type::make_Struct({nullptr}));
                                    }
                                    TU_ARMA(Enum, e) {
                                        bindings.type.set(item_path, AST::PathBinding_Type::make_Enum({nullptr}));
                                    }
                                    TU_ARMA(Union, e) {
                                        bindings.type.set(item_path, AST::PathBinding_Type::make_Union({nullptr}));
                                    }
                            }
                            }
                        } break;
                    }
                    // If any bindings were populated, then generate a path
                    if (bindings.has_binding()) {
                        auto rv = AST::Path(mp.crate, {});
                        if (true_path) {
                            rv.m_class.as_Absolute().crate = true_path->crate_name();
                            for (const auto& e : true_path->components()) {
                                rv.nodes().push_back(e);
                            }
                        } else {
                            for (const auto& e : mp.ents) {
                                rv.nodes().push_back(e);
                            }
                            rv.nodes().push_back(name);
                        }
                        rv.m_bindings = std::move(bindings);
                        return rv;
                    }
                    // Fall through
                } else {
                    const AST::Module* mod = &m_crate.root_module();
                    for (const auto& node : mp.ents) {
                        const AST::Module* next = nullptr;
                        if (node.c_str()[0] == '#') {
                            char c;
                            unsigned int idx;
                            ::std::stringstream ss(node.c_str());
                            ss >> c;
                            ss >> idx;
                            assert(idx < mod->anon_mods().size());
                            assert(mod->anon_mods()[idx]);
                            next = mod->anon_mods()[idx].get();
                        } else {
                            for (const auto& i : mod->m_items) {
                                if (i->name == node) {
                                    next = &i->data.as_Module();
                                    break;
                                }
                            }
                        }
                        ASSERT_BUG(Span(), next, "Failed to find module `" << node << "` in " << mod->path() << " for " << mp);
                        mod = next;
                    }
                    ::AST::Path rv;
                    if (this->lookup_in_mod(*mod, name, mode, rv)) {
                        return rv;
                    }
                }
            }
            for (auto it = m_name_context.rbegin(); it != m_name_context.rend(); ++it) {
                TU_MATCH_HDRA( (*it), {)
                TU_ARMA(Module, e) {
                        DEBUG("- Module " << e.mod->path());
                        ::AST::Path rv;
                        if (this->lookup_in_mod(*e.mod, name, mode, rv)) {
                            return rv;
                        }
                    }
                    TU_ARMA(ConcreteSelf, e) {
                        DEBUG("- ConcreteSelf");
                        if (name == rcstring_Self) {
                            switch (mode) {
                                case LookupMode::PatternType:
                                case LookupMode::Type:
                                case LookupMode::Namespace:
                                    // TODO: Want to return the type if handling a struct literal
                                    if (false) {
                                        return ::AST::Path::new_ufcs_ty(e->clone(), ::std::vector<::AST::PathNode>());
                                    } else {
                                        ::AST::Path rv(name);
                                        rv.m_bindings.type.set(AST::AbsolutePath(), ::AST::PathBinding_Type::make_TypeParameter({0xFFFF}));
                                        return rv;
                                    }
                                    break;
                                case LookupMode::Constant:
                                case LookupMode::Variable:
                                    // TODO: Ensure validity? (I.e. that `Self` is a unit or tuple struct
                                    if (const auto* p = e->m_data.opt_Path()) {
                                        // HACK! If `Self` points to a `type`, look through it
                                        // - rustc-1.90.0-src/compiler/rustc_codegen_llvm/src/context.rs:675
                                        if (const auto* pbe = (**p).m_bindings.type.binding.opt_TypeAlias()) {
                                            assert(pbe->alias_);
                                            assert(pbe->alias_->m_type.is_path());
                                            return *pbe->alias_->m_type.m_data.as_Path();
                                        }
                                        return **p;
                                    }
                                default:
                                    break;
                            }
                        }
                    }
                    TU_ARMA(VarBlock, e) {
                        DEBUG("- VarBlock");
                        assert(e.level <= m_block_level);
                        if (mode != LookupMode::Variable) {
                            // ignore
                        } else {
                            for (auto it2 = e.variables.rbegin(); it2 != e.variables.rend(); ++it2) {
                                if (it2->first.name == name) {
                                    DEBUG("> Match: Hygiene " << it2->first.hygiene << " check against src_context");
                                }
                                if (it2->first.name == name && it2->first.hygiene.is_visible(src_context)) {
                                    ::AST::Path rv(name);
                                    rv.bind_variable(it2->second);
                                    return rv;
                                }
                            }
                        }
                    }
                    TU_ARMA(Generic, e) {
                        DEBUG("- Generic");
                        switch (mode) {
                            case LookupMode::Type:
                            case LookupMode::Namespace:
                                for (auto it2 = e.types.rbegin(); it2 != e.types.rend(); ++it2) {
                                    if (it2->name == name) {
                                        ::AST::Path rv(name);
                                        rv.m_bindings.type.set(AST::AbsolutePath(), AST::PathBinding_Type::make_TypeParameter({it2->value.to_binding()}));
                                        return rv;
                                    }
                                }
                                break;
                            case LookupMode::Variable:
                            case LookupMode::Constant:
                                for (auto it2 = e.constants.rbegin(); it2 != e.constants.rend(); ++it2) {
                                    if (it2->name == name) {
                                        ::AST::Path rv(name);
                                        rv.m_bindings.value.set(AST::AbsolutePath(), AST::PathBinding_Value::make_Generic({it2->value.to_binding()}));
                                        return rv;
                                    }
                                }
                                break;
                            default:
                                // ignore.
                                // TODO: Integer generics
                                break;
                        }
                    }
                }
            }

            // Top-level module
            DEBUG("- Top module (" << m_mod.path() << ")");
            ::AST::Path rv;
            if (this->lookup_in_mod(m_mod, name, mode, rv)) {
                return rv;
            }

            DEBUG("- Primitives");
            switch (mode) {
                case LookupMode::Namespace:
                case LookupMode::Type: {
                    // Look up primitive types
                    auto ct = coretype_fromstring(name.c_str());
                    if (ct != CORETYPE_INVAL) {
                        return ::AST::Path::new_ufcs_ty(TypeRef(Span(), ct), ::std::vector<::AST::PathNode>());
                    }
                } break;
                default:
                    break;
            }

            // #![feature(extern_prelude)] - 2018-style extern paths
            if (mode == LookupMode::Namespace /*&& m_crate.has_feature("extern_prelude")*/) {
                DEBUG("Extern crates - " << AST::g_implicit_crates);
                auto it = AST::g_implicit_crates.find(name);
                if (it != AST::g_implicit_crates.end()) {
                    DEBUG("- Found '" << name << "' (= " << it->second << ")");
                    return AST::Path(it->second, {});
                }
            }

            return AST::Path();
        }

        unsigned int lookup_local(const Span& sp, const RcString name, LookupMode mode) {
            for (auto it = m_name_context.rbegin(); it != m_name_context.rend(); ++it) {
                TU_MATCH_HDRA( (*it), {)
                TU_ARMA(Module, e) {
                    }
                    TU_ARMA(ConcreteSelf, e) {
                    }
                    TU_ARMA(VarBlock, e) {
                        if (mode == LookupMode::Variable) {
                            DEBUG("- VarBlock lvl" << e.level);
                            for (auto it2 = e.variables.rbegin(); it2 != e.variables.rend(); ++it2) {
                                // TODO: Hyginic lookup?
                                DEBUG(" > " << it2->first.name);
                                if (it2->first.name == name) {
                                    return it2->second;
                                }
                            }
                        }
                    }
                    TU_ARMA(Generic, e) {
                        DEBUG("- Generic");
                        switch (mode) {
                            case LookupMode::Type:
                                for (auto it2 = e.types.rbegin(); it2 != e.types.rend(); ++it2) {
                                    if (it2->name == name) {
                                        return it2->value.to_binding();
                                    }
                                }
                                break;
                            case LookupMode::Variable:
                                for (auto it2 = e.constants.rbegin(); it2 != e.constants.rend(); ++it2) {
                                    if (it2->name == name) {
                                        //TODO(sp, "Return a reference to a constant generic '" << name << "'");
                                        // Need to disambiguate it... could set a high bit
                                        return it2->value.to_binding() | FLAG_CONST_GENERIC;
                                    }
                                }
                                break;
                            default:
                                // ignore.
                                // TODO: Integer generics
                                break;
                        }
                    }
                }
            }

            ERROR(sp, E0000, "Unable to find local " << (mode == LookupMode::Variable ? "variable" : "type") << " '" << name << "'");
        }

        /// Clones the context, including only the module-level items (i.e. just the Module entries)
        Context clone_mod() const {
            auto rv = Context(this->m_crate, this->m_mod);
            for (const auto& v : m_name_context) {
                if (const auto* e = v.opt_Module()) {
                    rv.m_name_context.push_back(Ent::make_Module(*e));
                }
            }
            return rv;
        }
    };
} // Namespace

::std::ostream& operator<<(::std::ostream& os, const Context::LookupMode& v) {
    switch (v) {
        case Context::LookupMode::Namespace:
            os << "Namespace";
            break;
        case Context::LookupMode::Type:
            os << "Type";
            break;
        case Context::LookupMode::PatternValue:
            os << "PatternValue";
            break;
        case Context::LookupMode::PatternType:
            os << "PatternType";
            break;
        case Context::LookupMode::Constant:
            os << "Constant";
            break;
        case Context::LookupMode::Variable:
            os << "Variable";
            break;
    }
    return os;
}

void Resolve_Absolute_Path_BindAbsolute(Context& context, const Span& sp, Context::LookupMode& mode, ::AST::Path& path);
void Resolve_Absolute_Path(/*const*/ Context& context, const Span& sp, Context::LookupMode mode, ::AST::Path& path);
void Resolve_Absolute_Lifetime(Context& context, const Span& sp, AST::LifetimeRef& type);
void Resolve_Absolute_Type(Context& context, TypeRef& type);
void Resolve_Absolute_Expr(Context& context, ::AST::Expr& expr);
void Resolve_Absolute_ExprNode(Context& context, ::AST::ExprNode& node);
void Resolve_Absolute_Pattern(Context& context, bool allow_refutable, ::AST::Pattern& pat);
void Resolve_Absolute_Mod(const ::AST::Crate& crate, ::AST::Module& mod);
void Resolve_Absolute_Mod(Context item_context, ::AST::Module& mod);

void Resolve_Absolute_Function(Context& item_context, ::AST::Function& fcn);

void Resolve_Absolute_PathParams(/*const*/ Context& context, const Span& sp, ::AST::PathParams& args) {
    for (auto& ent : args.m_entries) {
        TU_MATCH_HDRA( (ent), {)
        TU_ARMA(Null, _) {
            }
            TU_ARMA(Lifetime, l) {
                Resolve_Absolute_Lifetime(context, sp, l);
            }
            TU_ARMA(Type, t) {
                // A trivial path type might be refering to a generic value (e.g. `Foo<T,N>` where `N` is a const generic)
                if (t.m_data.is_Path() && t.m_data.as_Path()->is_trivial()) {
                    auto p = t.m_data.as_Path()->m_class.as_Relative();
                    // If type lookup fails
                    auto new_path = context.lookup_opt(p.nodes[0].name(), p.hygiene, Context::LookupMode::Type);
                    if (new_path == AST::Path()) {
                        // Try (constant) value lookup
                        auto new_path = context.lookup_opt(p.nodes[0].name(), p.hygiene, Context::LookupMode::Constant);
                        if (new_path != AST::Path()) {
                            // If that lookup succeeds, then create a value (and visit it - just in case)
                            ent = AST::PathParamEnt::make_Value(new AST::ExprNode_NamedValue(std::move(new_path)));
                            Resolve_Absolute_ExprNode(context, *ent.as_Value());
                        } else {
                            // Otherwise, visit (which will most likely fail)
                            Resolve_Absolute_Type(context, t);
                        }
                    } else {
                        // Normal type, update it then visit
                        *t.m_data.as_Path() = std::move(new_path);
                        Resolve_Absolute_Type(context, t);
                    }
                } else {
                    Resolve_Absolute_Type(context, t);
                }
            }
            TU_ARMA(Value, n) {
                Resolve_Absolute_ExprNode(context, *n);
            }
            TU_ARMA(AssociatedTyEqual, a) {
                Resolve_Absolute_PathParams(context, sp, a.first.args());
                Resolve_Absolute_Type(context, a.second);
            }
            TU_ARMA(AssociatedTyBound, a) {
                Resolve_Absolute_PathParams(context, sp, a.first.args());
                for (auto& p : a.second) {
                    Resolve_Absolute_Path(context, sp, Context::LookupMode::Type, p);
                }
            }
        }
    }
}

void Resolve_Absolute_PathNodes(/*const*/ Context& context, const Span& sp, ::std::vector<::AST::PathNode>& nodes) {
    for (auto& node : nodes) {
        Resolve_Absolute_PathParams(context, sp, node.args());
    }
}

void Resolve_Absolute_Path_BindUFCS(Context& context, const Span& sp, Context::LookupMode mode, ::AST::Path& path) {
    while (path.m_class.as_UFCS().nodes.size() > 1) {
        // More than one node, break into inner UFCS
        // - Since traits can't be associated items, this will always be the same form

        auto span = path.m_class.as_UFCS().type->span();
        auto nodes = mv$(path.m_class.as_UFCS().nodes);
        auto inner_path = mv$(path);
        inner_path.m_class.as_UFCS().nodes.push_back(mv$(nodes.front()));
        nodes.erase(nodes.begin());
        path = ::AST::Path::new_ufcs_ty(TypeRef(span, mv$(inner_path)), mv$(nodes));
    }

    if (path.m_class.as_UFCS().type) {
        Resolve_Absolute_Type(context, *path.m_class.as_UFCS().type);
    }

    const auto& ufcs = path.m_class.as_UFCS();
    if (ufcs.nodes.size() == 0) {
        if (mode == Context::LookupMode::Type && (!ufcs.trait || *ufcs.trait == ::AST::Path())) {
            return;
        }

        BUG(sp, "UFCS with no nodes encountered - " << path);
    }
    const auto& node = ufcs.nodes.at(0);

    if (ufcs.trait && ufcs.trait->is_valid()) {
        // Trait is specified, definitely a trait item
        // - Must resolve here
        const auto& pb = ufcs.trait->m_bindings.type.binding;
        if (!pb.is_Trait()) {
            ERROR(sp, E0000, "UFCS trait was not a trait - " << *ufcs.trait);
        }
        if (!pb.as_Trait().trait_) {
            return;
        }
        assert(pb.as_Trait().trait_);
        const auto& tr = *pb.as_Trait().trait_;

        switch (mode) {
            case Context::LookupMode::PatternValue:
            case Context::LookupMode::PatternType:
                ERROR(sp, E0000, "Invalid use of UFCS in pattern");
                break;
            case Context::LookupMode::Namespace:
            case Context::LookupMode::Type:
                for (const auto& item : tr.items()) {
                    if (item.name != node.name()) {
                        continue;
                    }
                    TU_MATCH_DEF(
                        ::AST::Item,
                        (item.data),
                        (e),
                        (
                            // TODO: Error
                        ),
                        (
                            Type,
                            // Resolve to asociated type
                        )
                    )
                }
                break;
            case Context::LookupMode::Constant:
            case Context::LookupMode::Variable:
                for (const auto& item : tr.items()) {
                    if (item.name != node.name()) {
                        continue;
                    }
                TU_MATCH_HDRA( (item.data), {)
                default:
                    // TODO: Error
                TU_ARMA(Function, e) {
                            // Bind as trait method
                            path.m_bindings.value.set(ufcs.trait->m_bindings.type.path + item.name, AST::PathBinding_Value::make_Function({&e}));
                        }
                        TU_ARMA(Static, e) {
                            // Resolve to asociated static
                        }
                }
                }
                break;
        }
    } else {
        // Trait is unknown or inherent, search for items on the type (if known) otherwise leave it until type resolution
        // - Methods can't be known until typeck (after the impl map is created)
    }
}

namespace {
    AST::Path split_into_crate(const Span& sp, AST::Path path, unsigned int start, const RcString& crate_name) {
        auto& nodes = path.nodes();
        AST::Path np = AST::Path(crate_name, {});
        for (unsigned int i = start; i < nodes.size(); i++) {
            np.nodes().push_back(mv$(nodes[i]));
        }
        np.m_bindings = path.m_bindings.clone();
        return np;
    }

    AST::Path split_into_ufcs_ty(const Span& sp, const AST::Path& path, unsigned int i /*item_name_idx*/) {
        const auto& path_abs = path.m_class.as_Absolute();
        auto type_path = ::AST::Path(path);
        type_path.m_class.as_Absolute().nodes.resize(i + 1);
        //Resolve_Absolute_Path(

        auto new_path = ::AST::Path::new_ufcs_ty(::TypeRef(sp, mv$(type_path)));
        for (unsigned int j = i + 1; j < path_abs.nodes.size(); j++) {
            new_path.nodes().push_back(mv$(path_abs.nodes[j]));
        }

        DEBUG(path << " -> " << new_path);

        return new_path;
    }

    AST::Path split_replace_into_ufcs_path(const Span& sp, AST::Path path, unsigned int i, const AST::Path& ty_path_tpl) {
        auto& path_abs = path.m_class.as_Absolute();
        auto& n = path_abs.nodes[i];

        auto type_path = ::AST::Path(ty_path_tpl);
        if (!n.args().is_empty()) {
            type_path.nodes().back().args() = mv$(n.args());
        }
        auto new_path = ::AST::Path::new_ufcs_ty(::TypeRef(sp, mv$(type_path)));
        for (unsigned int j = i + 1; j < path_abs.nodes.size(); j++) {
            new_path.nodes().push_back(mv$(path_abs.nodes[j]));
        }

        return new_path;
    }

    void Resolve_Absolute_Path_BindAbsolute__hir_from_import(Context& context, const Span& sp, bool is_value, AST::Path& path, const ::HIR::SimplePath& p) {
        TRACE_FUNCTION_FR("path=" << path << ", p=" << p, path);
        if (p.crate_name() == CRATE_BUILTINS) {
            AST::Path rv(p.crate_name(), {});
            rv.nodes().reserve(p.components().size());
            for (const auto& c : p.components()) {
                rv.nodes().push_back(AST::PathNode(c));
            }
            rv.nodes().back().args() = mv$(path.nodes().back().args());
            auto ap = sp_to_ap(p);

#if 0
            ASSERT_BUG(sp, p.m_components.size() == 2, "Invalid component count in " << p);

            if( p.m_components.front() == "types" ) {
            }
            else if( p.m_components.front() == "macros" ) {
            }
            else if( p.m_components.front() == "intrinsics" ) {
            }
            else {
                BUG(sp, "Invalid class (first) component in " << p);
            }
            TODO(sp, "");
#else
            if (coretype_fromstring(p.components().back().c_str()) != CORETYPE_INVAL) {
                rv.m_bindings.type.set(ap, AST::PathBinding_Type::make_TypeAlias({nullptr}));
            } else {
                rv.m_bindings.macro.set(ap, AST::PathBinding_Macro::make_MacroRules({nullptr}));
            }
#endif
            path = mv$(rv);
            return;
        }
        const auto& ext_crate = context.m_crate.m_extern_crates.at(p.crate_name());
        const ::HIR::Module* hmod = &ext_crate.m_hir->m_root_module;
        for (unsigned int i = 0; i < p.components().size() - 1; i++) {
            const auto& name = p.components()[i];
            auto it = hmod->m_mod_items.find(name);
            if (it == hmod->m_mod_items.end()) {
                ERROR(sp, E0000, "Couldn't find path component '" << name << "' of " << p);
            }

            TU_MATCH_HDRA( (it->second->ent), {)
            default:
                TODO(sp, "Unknown item type in path - " << i << " " << p << " - " << it->second->ent.tag_str());
                TU_ARMA(Enum, e) {
                    if (i != p.components().size() - 2) {
                        ERROR(sp, E0000, "Enum as path component in unexpected location - " << p);
                    }
                    const auto& varname = p.components().back();
                    auto var_idx = e.find_variant(varname);
                    ASSERT_BUG(sp, var_idx != SIZE_MAX, "Extern crate import path points to non-present variant - " << p);

                    // Construct output path (with same set of parameters)
                    AST::Path rv(p.crate_name(), {});
                    rv.nodes().reserve(p.components().size());
                    for (const auto& c : p.components()) {
                        rv.nodes().push_back(AST::PathNode(c));
                    }
                    rv.nodes().back().args() = mv$(path.nodes().back().args());
                    auto ap = sp_to_ap(p);
                    if (e.m_data.is_Data() && e.m_data.as_Data()[var_idx].is_struct) {
                        rv.m_bindings.type.set(ap, ::AST::PathBinding_Type::make_EnumVar({nullptr, static_cast<unsigned>(var_idx), &e}));
                    } else {
                        rv.m_bindings.value.set(ap, ::AST::PathBinding_Value::make_EnumVar({nullptr, static_cast<unsigned>(var_idx), &e}));
                    }
                    path = mv$(rv);

                    return;
                }
                TU_ARMA(Module, e) {
                    hmod = &e;
                }
            }
        }

        ::AST::Path::Bindings pb;

        const auto& name = p.components().back();
        auto ap = sp_to_ap(p);
        if (is_value) {
            auto it = hmod->m_value_items.find(name);
            if (it == hmod->m_value_items.end()) {
                ERROR(sp, E0000, "Couldn't find final component of " << p);
            }
            AST::PathBinding_Value pbv;
            TU_MATCH_HDRA( (it->second->ent), {)
            TU_ARMA(Import, e) {
                    // Wait? is this even valid?
                    BUG(sp, "HIR Import item pointed to an import");
                }
                TU_ARMA(Constant, e) {
                    pbv = ::AST::PathBinding_Value::make_Static({nullptr, nullptr});
                }
                TU_ARMA(Static, e) {
                    pbv = ::AST::PathBinding_Value::make_Static({nullptr, &e});
                }
                TU_ARMA(StructConstant, e) {
                    pbv = ::AST::PathBinding_Value::make_Struct({nullptr, &ext_crate.m_hir->get_typeitem_by_path(sp, e.ty, true).as_Struct()});
                }
                TU_ARMA(Function, e) {
                    pbv = ::AST::PathBinding_Value::make_Function({nullptr /*, &e*/});
                }
                TU_ARMA(StructConstructor, e) {
                    pbv = ::AST::PathBinding_Value::make_Struct({nullptr, &ext_crate.m_hir->get_typeitem_by_path(sp, e.ty, true).as_Struct()});
                }
            }
            pb.value.set( ::std::move(ap), ::std::move(pbv) );
        } else {
            auto it = hmod->m_mod_items.find(name);
            if (it == hmod->m_mod_items.end()) {
                ERROR(sp, E0000, "Couldn't find final component of " << p);
            }
            AST::PathBinding_Type pbt;
            TU_MATCH_HDRA( (it->second->ent), {)
            TU_ARMA(Import, e) {
                    // Wait? is this even valid?
                    BUG(sp, "HIR Import item pointed to an import");
                }
                TU_ARMA(Module, e) {
                    pbt = ::AST::PathBinding_Type::make_Module({nullptr, {&ext_crate, &e}});
                }
                TU_ARMA(Trait, e) {
                    pbt = ::AST::PathBinding_Type::make_Trait({nullptr, &e});
                }
                TU_ARMA(TraitAlias, e) {
                    pbt = ::AST::PathBinding_Type::make_TraitAlias({nullptr, &e});
                }
                TU_ARMA(TypeAlias, e) {
                    pbt = ::AST::PathBinding_Type::make_TypeAlias({nullptr /*, &e*/});
                }
                TU_ARMA(ExternType, e) {
                    pbt = ::AST::PathBinding_Type::make_TypeAlias({nullptr /*, &e*/});
                }
                TU_ARMA(Struct, e) {
                    pbt = ::AST::PathBinding_Type::make_Struct({nullptr, &e});
                }
                TU_ARMA(Union, e) {
                    pbt = ::AST::PathBinding_Type::make_Union({nullptr, &e});
                }
                TU_ARMA(Enum, e) {
                    pbt = ::AST::PathBinding_Type::make_Enum({nullptr, &e});
                }
            }
            pb.type.set( ::std::move(ap), ::std::move(pbt) );
        }

        // Construct output path (with same set of parameters)
        AST::Path rv(p.crate_name(), {});
        rv.nodes().reserve(p.components().size());
        for (const auto& c : p.components()) {
            rv.nodes().push_back(AST::PathNode(c));
        }
        rv.nodes().back().args() = mv$(path.nodes().back().args());
        rv.m_bindings = mv$(pb);
        path = mv$(rv);
    }

    void Resolve_Absolute_Path_BindAbsolute__hir_from(Context& context, const Span& sp, Context::LookupMode& mode, ::AST::Path& path, const AST::ExternCrate& crate, unsigned int start) {
        assert(crate.m_hir->m_crate_name == crate.m_name);
        TRACE_FUNCTION_FR(crate.m_hir->m_crate_name << " - " << path << " start=" << start, path);
        auto& path_abs = path.m_class.as_Absolute();

        if (path_abs.nodes.empty()) {
            switch (mode) {
                case Context::LookupMode::Namespace:
                    path.m_bindings.type.set({crate.m_name, {}}, ::AST::PathBinding_Type::make_Module({nullptr, {&crate, &crate.m_hir->m_root_module}}));
                    return;
                default:
                    TODO(sp, "Looking up a non-namespace, but pointed to crate root");
            }
        }

        const ::HIR::Module* hmod = &crate.m_hir->m_root_module;
        for (unsigned int i = start; i < path_abs.nodes.size() - 1; i++) {
            auto& n = path_abs.nodes[i];
            assert(hmod);
            auto it = hmod->m_mod_items.find(n.name());
            if (it == hmod->m_mod_items.end()) {
                ERROR(sp, E0000, "Couldn't find path component '" << n.name() << "' of " << path);
            }

            TU_MATCH_HDRA( (it->second->ent), {)
            TU_ARMA(Import, e) {
                    DEBUG("`" << n.name() << "`: Import " << e.path);
                    // - Update path then restart
                    auto newpath = AST::Path(e.path.crate_name(), {});
                    for (const auto& n : e.path.components()) {
                        newpath.nodes().push_back(AST::PathNode(n));
                    }
                    if (newpath.nodes().empty()) {
                        ASSERT_BUG(sp, n.args().is_empty(), "Params present, but name resolves to a crate root - " << path << " #" << i << " -> " << newpath);
                    } else {
                        newpath.nodes().back().args() = mv$(path.nodes()[i].args());
                    }
                    for (unsigned int j = i + 1; j < path.nodes().size(); j++) {
                        newpath.nodes().push_back(mv$(path.nodes()[j]));
                    }
                    DEBUG("> Recurse with " << newpath);
                    path = mv$(newpath);
                    // TODO: Recursion limit
                    Resolve_Absolute_Path_BindAbsolute(context, sp, mode, path);
                    return;
                }
                TU_ARMA(Module, e) {
                    hmod = &e;
                }
                TU_ARMA(TraitAlias, e) {
                    //for(const auto& trait_path_hir : e.m_traits)
                    //{
                    //}
                    TODO(sp, "Path referring to a trait alias - " << path);
                }
                TU_ARMA(Trait, e) {
                    AST::AbsolutePath ap(crate.m_name, {});
                    for (unsigned int j = start; j <= i; j++) {
                        ap.nodes.push_back(path_abs.nodes[j].name());
                    }
                    AST::PathParams pp;
                    if (!n.args().is_empty()) {
                        pp = mv$(n.args());
                    } else {
                        for (const auto& typ : e.m_params.m_types) {
                            (void)typ;
                            pp.m_entries.push_back(::TypeRef(sp));
                        }
                    }
                    AST::Path trait_path(ap, std::move(pp));
                    trait_path.m_bindings.type.set(::std::move(ap), ::AST::PathBinding_Type::make_Trait({nullptr, &e}));

                    ::AST::Path new_path;
                    const auto& next_node = path_abs.nodes[i + 1];
                    // If the named item can't be found in the trait, fall back to it being a type binding
                    // - What if this item is from a nested trait?
                    bool found = false;
                    switch (i + 1 < path_abs.nodes.size() ? Context::LookupMode::Namespace : mode) {
                        case Context::LookupMode::Namespace:
                        case Context::LookupMode::Type:
                        case Context::LookupMode::PatternType:
                            found = (e.m_types.find(next_node.name()) != e.m_types.end());
                        case Context::LookupMode::PatternValue:
                        case Context::LookupMode::Constant:
                        case Context::LookupMode::Variable:
                            found = (e.m_values.find(next_node.name()) != e.m_values.end());
                            break;
                    }

                    if (!found) {
                        new_path = ::AST::Path::new_ufcs_ty(::TypeRef(sp, mv$(trait_path)));
                    } else {
                        new_path = ::AST::Path::new_ufcs_trait(::TypeRef(sp), mv$(trait_path));
                    }
                    for (unsigned int j = i + 1; j < path_abs.nodes.size(); j++) {
                        new_path.nodes().push_back(mv$(path_abs.nodes[j]));
                    }

                    path = mv$(new_path);
                    return Resolve_Absolute_Path_BindUFCS(context, sp, mode, path);
                }
                case ::HIR::TypeItem::TAG_ExternType:
                case ::HIR::TypeItem::TAG_TypeAlias:
                case ::HIR::TypeItem::TAG_Struct:
                case ::HIR::TypeItem::TAG_Union:
                    path = split_into_crate(sp, mv$(path), start, crate.m_name);
                    path = split_into_ufcs_ty(sp, mv$(path), i - start);
                    return Resolve_Absolute_Path_BindUFCS(context, sp, mode, path);
                    TU_ARMA(Enum, e) {
                        if (i + 1 < path_abs.nodes.size()) {
                            auto& next_node = path_abs.nodes[i + 1];
                            // If this refers to an enum variant, return the full path
                            // - Otherwise, assume it's an associated type?
                            auto idx = e.find_variant(next_node.name());
                            if (idx != SIZE_MAX) {
                                if (i != path_abs.nodes.size() - 2) {
                                    ERROR(sp, E0000, "Unexpected enum in path " << path);
                                }

                                AST::AbsolutePath ap(crate.m_name, {});
                                auto trait_path = ::AST::Path(crate.m_name, {});
                                for (unsigned int j = start; j < path_abs.nodes.size(); j++) {
                                    ap.nodes.push_back(path_abs.nodes[j].name());
                                }

                                // NOTE: Type parameters for enums go after the _variant_
                                if (!n.args().is_empty()) {
                                    if (next_node.args().is_empty()) {
                                        DEBUG("Moving type params from on the enum to the variant");
                                        next_node.args() = std::move(n.args());
                                    } else {
                                        ERROR(sp, E0000, "Type parameters were not expected here (enum params go on the variant)");
                                    }
                                }

                                if (e.m_data.is_Data() && e.m_data.as_Data()[idx].is_struct) {
                                    path.m_bindings.type.set(ap, ::AST::PathBinding_Type::make_EnumVar({nullptr, static_cast<unsigned int>(idx), &e}));
                                } else {
                                    path.m_bindings.value.set(ap, ::AST::PathBinding_Value::make_EnumVar({nullptr, static_cast<unsigned int>(idx), &e}));
                                }
                                path = split_into_crate(sp, mv$(path), start, crate.m_name);
                                return;
                            }
                        }
                        path = split_into_crate(sp, mv$(path), start, crate.m_name);
                        path = split_into_ufcs_ty(sp, mv$(path), i - start);
                        return Resolve_Absolute_Path_BindUFCS(context, sp, mode, path);
                    }
            }
        }

        AST::AbsolutePath ap(crate.m_name, {});
        auto trait_path = ::AST::Path(crate.m_name, {});
        for (unsigned int j = start; j < path_abs.nodes.size(); j++) {
            ap.nodes.push_back(path_abs.nodes[j].name());
        }

        const auto& name = path_abs.nodes.back().name();
        switch (mode) {
            // TODO: Don't bind to a Module if LookupMode::Type
            case Context::LookupMode::Namespace:
            case Context::LookupMode::Type:
            case Context::LookupMode::PatternType: {
                auto v = hmod->m_mod_items.find(name);
                if (v != hmod->m_mod_items.end()) {
                    ::AST::PathBinding_Type pbt;
                    TU_MATCH_HDRA( (v->second->ent), {)
                    TU_ARMA(Import, e) {
                            DEBUG("= Import " << e.path);
                            Resolve_Absolute_Path_BindAbsolute__hir_from_import(context, sp, false, path, e.path);
                            return;
                        }
                        TU_ARMA(Trait, e) {
                            pbt = ::AST::PathBinding_Type::make_Trait({nullptr, &e});
                        }
                        TU_ARMA(TraitAlias, e) {
                            pbt = ::AST::PathBinding_Type::make_TraitAlias({nullptr, &e});
                        }
                        TU_ARMA(Module, e) {
                            pbt = ::AST::PathBinding_Type::make_Module({nullptr, {&crate, &e}});
                        }
                        TU_ARMA(ExternType, e) {
                            pbt = ::AST::PathBinding_Type::make_TypeAlias({nullptr /*, &e*/});
                        }
                        TU_ARMA(TypeAlias, e) {
                            pbt = ::AST::PathBinding_Type::make_TypeAlias({nullptr /*, &e*/});
                        }
                        TU_ARMA(Enum, e) {
                            pbt = ::AST::PathBinding_Type::make_Enum({nullptr, &e});
                        }
                        TU_ARMA(Struct, e) {
                            pbt = ::AST::PathBinding_Type::make_Struct({nullptr, &e});
                        }
                        TU_ARMA(Union, e) {
                            pbt = ::AST::PathBinding_Type::make_Union({nullptr, &e});
                        }
                    }
                    path.m_bindings.type.set(::std::move(ap), ::std::move(pbt));
                    // Update path (trim down to `start` and set crate name)
                    path = split_into_crate(sp, mv$(path), start,  crate.m_name);
                    return ;
                }
            } break;

            case Context::LookupMode::PatternValue: {
                auto v = hmod->m_value_items.find(name);
                if (v != hmod->m_value_items.end()) {
                    TU_MATCH_HDRA( (v->second->ent), {)
                    default:
                        DEBUG("Ignore - " << v->second->ent.tag_str());
                        TU_ARMA(StructConstant, e) {
                            auto ty_path = e.ty;
                            path.m_bindings.value.set(::std::move(ap), ::AST::PathBinding_Value::make_Struct({nullptr, &crate.m_hir->get_struct_by_path(sp, ty_path)}));
                            path = split_into_crate(sp, mv$(path), start, crate.m_name);
                            return;
                        }
                        TU_ARMA(Import, e) {
                            Resolve_Absolute_Path_BindAbsolute__hir_from_import(context, sp, true, path, e.path);
                            return;
                        }
                        TU_ARMA(Constant, e) {
                            // Bind and update path
                            path.m_bindings.value.set(::std::move(ap), ::AST::PathBinding_Value::make_Static({nullptr, nullptr}));
                            path = split_into_crate(sp, mv$(path), start, crate.m_name);
                            return;
                        }
                    }
                } else {
                    DEBUG("No value item for " << name);
                }
            } break;
            case Context::LookupMode::Constant:
            case Context::LookupMode::Variable: {
                auto v = hmod->m_value_items.find(name);
                if (v != hmod->m_value_items.end()) {
                    ::AST::PathBinding_Value pbv;
                    TU_MATCH_HDRA( (v->second->ent), {)
                    TU_ARMA(Import, e) {
                            Resolve_Absolute_Path_BindAbsolute__hir_from_import(context, sp, true, path, e.path);
                            return;
                        }
                        TU_ARMA(Function, e) {
                            pbv = ::AST::PathBinding_Value::make_Function({nullptr /*, &e*/});
                        }
                        TU_ARMA(StructConstructor, e) {
                            auto ty_path = e.ty;
                            pbv = ::AST::PathBinding_Value::make_Struct({nullptr, &crate.m_hir->get_struct_by_path(sp, ty_path)});
                        }
                        TU_ARMA(StructConstant, e) {
                            auto ty_path = e.ty;
                            pbv = ::AST::PathBinding_Value::make_Struct({nullptr, &crate.m_hir->get_struct_by_path(sp, ty_path)});
                        }
                        TU_ARMA(Static, e) {
                            pbv = ::AST::PathBinding_Value::make_Static({nullptr, &e});
                        }
                        TU_ARMA(Constant, e) {
                            // Bind
                            pbv = ::AST::PathBinding_Value::make_Static({nullptr, nullptr});
                        }
                    }
                    path.m_bindings.value.set(::std::move(ap), ::std::move(pbv));
                    path = split_into_crate(sp, mv$(path), start,  crate.m_name);
                    return ;
                }
            } break;
        }
        ERROR(sp, E0000, "Couldn't find " << Context::lookup_mode_msg(mode) << " '" << path_abs.nodes.back().name() << "' of " << path);
    }
}

void Resolve_Absolute_Path_BindAbsolute(Context& context, const Span& sp, Context::LookupMode& mode, ::AST::Path& path) {
    TRACE_FUNCTION_FR("path = " << path, path);
    auto& path_abs = path.m_class.as_Absolute();

    if (path_abs.crate == "#intrinsics") {
        AST::AbsolutePath ap{path_abs.crate, {}};
        for (const auto& n : path.nodes()) {
            ap.nodes.push_back(n.name());
        }
        path.m_bindings.value.set(std::move(ap), AST::PathBinding_Value::make_Function({nullptr}));
        return;
    } else if (path_abs.crate == CRATE_BUILTINS) {
        ASSERT_BUG(sp, path.m_bindings.has_binding(), "");
        return;
    } else if (path_abs.crate != "" && path_abs.crate != context.m_crate.m_crate_name_real) {
        // TODO: Handle items from other crates (back-converting HIR paths)
        ASSERT_BUG(sp, context.m_crate.m_extern_crates.count(path_abs.crate), "ERROR: Crate `" << path_abs.crate << "` not loaded");
        Resolve_Absolute_Path_BindAbsolute__hir_from(context, sp, mode, path, context.m_crate.m_extern_crates.at(path_abs.crate), 0);
        return;
    }

    if (path_abs.nodes.empty()) {
        path.m_bindings.type.set(AST::AbsolutePath(path_abs.crate, {}), AST::PathBinding_Type::make_Module({&context.m_crate.m_root_module}));
        return;
    }

    const ::AST::Module* mod = &context.m_crate.m_root_module;
    for (unsigned int i = 0; i < path_abs.nodes.size() - 1; i++) {
        auto& n = path_abs.nodes[i];

        if (n.name().c_str()[0] == '#') {
            if (!n.args().is_empty()) {
                ERROR(sp, E0000, "Type parameters were not expected here");
            }

            if (n.name() == "#") {
                TODO(sp, "magic module");
            }

            char c;
            unsigned int idx;
            ::std::stringstream ss(n.name().c_str());
            ss >> c;
            ss >> idx;
            assert(idx < mod->anon_mods().size());
            assert(mod->anon_mods()[idx]);
            mod = mod->anon_mods()[idx].get();
        } else {
            auto it = mod->m_namespace_items.find(n.name());
            if (it == mod->m_namespace_items.end()) {
                ERROR(sp, E0000, "Couldn't find path component '" << n.name() << "' of " << path);
            }
            const auto& name_ref = it->second;
            DEBUG("#" << i << " \"" << n.name() << "\" = " << name_ref.path << (name_ref.is_import ? " (import)" : ""));

            TU_MATCH_HDRA( (name_ref.path.m_bindings.type.binding), {)
            default:
                ERROR(sp, E0000, "Encountered non-namespace item '" << n.name() << "' ("<<name_ref.path<<") in path " << path);
                TU_ARMA(TypeAlias, e) {
                    path = split_replace_into_ufcs_path(sp, mv$(path), i, name_ref.path);
                    return Resolve_Absolute_Path_BindUFCS(context, sp, mode, path);
                }
                TU_ARMA(Crate, e) {
                    Resolve_Absolute_Path_BindAbsolute__hir_from(context, sp, mode, path, *e.crate_, i + 1);
                    return;
                }
                TU_ARMA(Trait, e) {
                    assert(e.trait_ || e.hir);
                    auto trait_path = ::AST::Path(name_ref.path);
                    // HACK! If this was an import, recurse on it to fix paths. (Ideally, all index entries should have the canonical path, but don't currently)
                    if (name_ref.is_import) {
                        auto lm = Context::LookupMode::Type;
                        Resolve_Absolute_Path_BindAbsolute(context, sp, lm, trait_path);
                    }
                    if (!n.args().is_empty()) {
                        trait_path.nodes().back().args() = mv$(n.args());
                    } else {
                        if (e.trait_) {
                            for (const auto& param : e.trait_->params().m_params) {
                            TU_MATCH_HDRA( (param), {)
                            TU_ARMA(None, e) {
                                    }
                                    TU_ARMA(Lifetime, e) {
                                    }
                                    TU_ARMA(Type, typ) {
                                        trait_path.nodes().back().args().m_entries.push_back(::TypeRef(sp));
                                    }
                                    TU_ARMA(Value, val) {
                                        //trait_path.nodes().back().args().m_entries.push_back( ::TypeRef(sp) );
                                    }
                            }
                            }
                        } else {
                            for (const auto& typ : e.hir->m_params.m_types) {
                                (void)typ;
                                trait_path.nodes().back().args().m_entries.push_back(::TypeRef(sp));
                            }
                        }
                    }
                    // TODO: If the named item can't be found in the trait, fall back to it being a type binding
                    // - What if this item is from a nested trait?
                    ::AST::Path new_path;
                    bool found = false;
                    assert(i + 1 < path_abs.nodes.size());
                    const auto& item_name = path_abs.nodes[i + 1].name();
                    if (e.trait_) {
                        auto it = ::std::find_if(e.trait_->items().begin(), e.trait_->items().end(), [&](const auto& x) {
                            return x.name == item_name;
                        });
                        if (it != e.trait_->items().end()) {
                            found = true;
                        }
                    } else {
                        switch (mode) {
                            case Context::LookupMode::Constant:
                            case Context::LookupMode::Variable:
                            case Context::LookupMode::PatternValue:
                                found = (e.hir->m_values.count(item_name) != 0);
                                break;
                            case Context::LookupMode::Namespace:
                            case Context::LookupMode::Type:
                            case Context::LookupMode::PatternType:
                                found = (e.hir->m_types.count(item_name) != 0);
                                break;
                        }
                    }
                    if (!found) {
                        new_path = ::AST::Path::new_ufcs_ty(::TypeRef(sp, mv$(trait_path)));
                    } else {
                        new_path = ::AST::Path::new_ufcs_trait(::TypeRef(sp), mv$(trait_path));
                    }
                    for (unsigned int j = i + 1; j < path_abs.nodes.size(); j++) {
                        new_path.nodes().push_back(mv$(path_abs.nodes[j]));
                    }

                    path = mv$(new_path);
                    return Resolve_Absolute_Path_BindUFCS(context, sp, mode, path);
                }
                TU_ARMA(Enum, e) {
                    if (name_ref.is_import) {
                        auto newpath = name_ref.path;
                        for (unsigned int j = i + 1; j < path_abs.nodes.size(); j++) {
                            newpath.nodes().push_back(mv$(path_abs.nodes[j]));
                        }
                        path = mv$(newpath);
                        //TOOD: Recursion limit
                        Resolve_Absolute_Path_BindAbsolute(context, sp, mode, path);
                        return;
                    } else {
                        assert(e.enum_);
                        auto& last_node = path_abs.nodes.back();
                        for (const auto& var : e.enum_->variants()) {
                            if (var.m_name == last_node.name()) {
                                if (i != path_abs.nodes.size() - 2) {
                                    ERROR(sp, E0000, "Unexpected enum in path " << path);
                                }
                                // NOTE: Type parameters for enums go after the _variant_
                                if (!n.args().is_empty()) {
                                    if (last_node.args().is_empty()) {
                                        DEBUG("Moving type params from on the enum to the variant");
                                        last_node.args() = std::move(n.args());
                                    } else {
                                        ERROR(sp, E0000, "Type parameters were not expected here (enum params go on the variant)");
                                    }
                                }

                                unsigned int idx = &var - &e.enum_->variants().front();

                                DEBUG("Bound to enum variant '" << var.m_name << "' (#" << idx << ")");
                                auto ap = name_ref.path.m_bindings.type.path + var.m_name;
                                if (var.m_data.is_Struct()) {
                                    path.m_bindings.type.set(ap, AST::PathBinding_Type::make_EnumVar({e.enum_, idx}));
                                } else {
                                    path.m_bindings.value.set(ap, AST::PathBinding_Value::make_EnumVar({e.enum_, idx}));
                                }
                                return;
                            }
                        }

                        path = split_replace_into_ufcs_path(sp, mv$(path), i, name_ref.path);
                        return Resolve_Absolute_Path_BindUFCS(context, sp, mode, path);
                    }
                }
                TU_ARMA(Struct, e) {
                    path = split_replace_into_ufcs_path(sp, mv$(path), i, name_ref.path);
                    return Resolve_Absolute_Path_BindUFCS(context, sp, mode, path);
                }
                TU_ARMA(Union, e) {
                    path = split_replace_into_ufcs_path(sp, mv$(path), i, name_ref.path);
                    return Resolve_Absolute_Path_BindUFCS(context, sp, mode, path);
                }
                TU_ARMA(Module, e) {
                    if (name_ref.is_import) {
                        auto newpath = name_ref.path;
                        for (unsigned int j = i + 1; j < path_abs.nodes.size(); j++) {
                            newpath.nodes().push_back(mv$(path_abs.nodes[j]));
                        }
                        DEBUG("- Module import, " << path << " => " << newpath);
                        path = mv$(newpath);
                        Resolve_Absolute_Path_BindAbsolute(context, sp, mode, path);
                        return;
                    } else {
                        mod = e.module_;
                    }
                }
            }
        }
    }

    // Set binding to binding of node in last module
    ::AST::Path tmp;
    if (!Context::lookup_in_mod(*mod, path_abs.nodes.back().name(), mode, tmp)) {
        ERROR(sp, E0000, "Couldn't find " << Context::lookup_mode_msg(mode) << " '" << path_abs.nodes.back().name() << "' of " << path);
    }
    ASSERT_BUG(sp, tmp.m_bindings.has_binding(), "Lookup for " << path << " succeeded, but had no binding");

    // Replaces the path with the one returned by `lookup_in_mod`, ensuring that `use` aliases are eliminated
    DEBUG("Replace " << path << " with " << tmp);
    auto args = mv$(path.nodes().back().args());
    if (tmp != path) {
        // If the paths mismatch (i.e. there was an import involved), pass through resolution again
        // - This works around cases where the index contains paths that refer to aliases.
        DEBUG("- Recurse");
        Resolve_Absolute_Path_BindAbsolute(context, sp, mode, tmp);
    }
    tmp.nodes().back().args() = mv$(args);
    path = mv$(tmp);
}

void Resolve_Absolute_Path(/*const*/ Context& context, const Span& sp, Context::LookupMode mode, ::AST::Path& path) {
    TRACE_FUNCTION_FR("mode = " << mode << ", path = " << path, path);

    TU_MATCH_HDRA( (path.m_class), {)
    TU_ARMA(Invalid, e) {
            BUG(sp, "Attempted resolution of invalid path");
        }
        TU_ARMA(Local, e) {
            // Nothing to do (TODO: Check that it's valid?)
            if (mode == Context::LookupMode::Variable) {
                auto idx = context.lookup_local(sp, e.name, mode);
                if (idx >= FLAG_CONST_GENERIC) {
                    path.m_bindings.value.set({}, ::AST::PathBinding_Value::make_Generic({idx - FLAG_CONST_GENERIC}));
                } else {
                    path.m_bindings.value.set({}, ::AST::PathBinding_Value::make_Variable({idx}));
                }
            } else if (mode == Context::LookupMode::Type) {
                path.bind_variable(context.lookup_local(sp, e.name, mode));
            } else {
            }
        }
        TU_ARMA(Relative, e) {
            DEBUG("- Relative");
            if (e.nodes.size() == 0) {
                BUG(sp, "Resolve_Absolute_Path - Relative path with no nodes");
            }
            if (e.nodes.size() > 1) {
                // Look up type/module name
                auto p = context.lookup(sp, e.nodes[0].name(), e.hygiene, Context::LookupMode::Namespace);
                DEBUG("Found type/mod - " << p);
                // HACK: If this is a primitive name, and resolved to a module.
                // - If the next component isn't found in the located module
                //  > Instead use the type name.
                if (!p.m_class.is_Local() && coretype_fromstring(e.nodes[0].name().c_str()) != CORETYPE_INVAL) {
                    if (const auto* pep = p.m_bindings.type.binding.opt_Module()) {
                        const auto& pe = *pep;
                        bool found = false;
                        const auto& name = e.nodes[1].name();
                        if (!pe.module_) {
                            assert(pe.hir.mod);
                            const auto& mod = *pe.hir.mod;

                            switch (e.nodes.size() == 2 ? mode : Context::LookupMode::Namespace) {
                                case Context::LookupMode::Namespace:
                                case Context::LookupMode::Type:
                                case Context::LookupMode::PatternType:
                                    // TODO: Restrict if ::Type
                                    if (mod.m_mod_items.find(name) != mod.m_mod_items.end()) {
                                        found = true;
                                    }
                                    break;
                                case Context::LookupMode::PatternValue:
                                    TODO(sp, "Check " << p << " for an item named " << name << " (Pattern)");
                                case Context::LookupMode::Constant:
                                case Context::LookupMode::Variable:
                                    if (mod.m_value_items.find(name) != mod.m_value_items.end()) {
                                        found = true;
                                    }
                                    break;
                            }
                        } else {
                            const auto& mod = *pe.module_;
                            switch (e.nodes.size() == 2 ? mode : Context::LookupMode::Namespace) {
                                case Context::LookupMode::Namespace:
                                    if (mod.m_namespace_items.find(name) != mod.m_namespace_items.end()) {
                                        found = true;
                                    }
                                case Context::LookupMode::Type:
                                case Context::LookupMode::PatternType:
                                    if (mod.m_namespace_items.find(name) != mod.m_namespace_items.end()) {
                                        found = true;
                                    }
                                    break;
                                case Context::LookupMode::PatternValue:
                                    TODO(sp, "Check " << p << " for an item named " << name << " (Pattern)");
                                case Context::LookupMode::Constant:
                                case Context::LookupMode::Variable:
                                    if (mod.m_value_items.find(name) != mod.m_value_items.end()) {
                                        found = true;
                                    }
                                    break;
                            }
                        }
                        if (!found) {
                            auto ct = coretype_fromstring(e.nodes[0].name().c_str());
                            p = ::AST::Path::new_ufcs_ty(TypeRef(Span(), ct), ::std::vector<::AST::PathNode>());
                        }

                        DEBUG("Primitive module hack yeilded " << p);
                    }
                }

                if (e.nodes.size() > 1) {
                    // Only primitive types turn `Local` paths
                    if (p.m_class.is_Local()) {
                        p = ::AST::Path::new_ufcs_ty(TypeRef(sp, mv$(p)));
                    }
                    if (!e.nodes[0].args().is_empty()) {
                        assert(p.nodes().size() > 0);
                        assert(p.nodes().back().args().is_empty());
                        p.nodes().back().args() = mv$(e.nodes[0].args());
                    }
                    for (unsigned int i = 1; i < e.nodes.size(); i++) {
                        p.nodes().push_back(mv$(e.nodes[i]));
                    }
                    p.m_bindings = ::AST::Path::Bindings{};
                }
                path = mv$(p);
            } else {
                // Look up value
                auto p = context.lookup(sp, e.nodes[0].name(), e.hygiene, mode);
                //DEBUG("Found path " << p << " for " << path);
                if (p.is_absolute()) {
                    assert(!p.nodes().empty());
                    p.nodes().back().args() = mv$(e.nodes.back().args());
                }
                path = mv$(p);
            }

            if (!path.is_trivial()) {
                Resolve_Absolute_PathNodes(context, sp, path.nodes());
            }
        }
        TU_ARMA(Self, e) {
            DEBUG("- Self");
            const auto& mp_nodes = context.m_mod.path().nodes;
            // Ignore any leading anon modules
            unsigned int start_len = mp_nodes.size();
            while (start_len > 0 && mp_nodes[start_len - 1].c_str()[0] == '#') {
                start_len--;
            }

            // - Create a new path
            ::AST::Path np("", {});
            auto& np_nodes = np.nodes();
            np_nodes.reserve(start_len + e.nodes.size());
            for (unsigned int i = 0; i < start_len; i++) {
                np_nodes.push_back(mp_nodes[i]);
            }
            for (auto& en : e.nodes) {
                np_nodes.push_back(mv$(en));
            }

            if (!path.is_trivial()) {
                Resolve_Absolute_PathNodes(context, sp, np_nodes);
            }

            path = mv$(np);
        }
        TU_ARMA(Super, e) {
            DEBUG("- Super");
            // - Determine how many components of the `self` path to use
            const auto& mp_nodes = context.m_mod.path().nodes;
            assert(e.count >= 1);
            // TODO: The first super should ignore any anon modules.
            unsigned int start_len = e.count > mp_nodes.size() ? 0 : mp_nodes.size() - e.count;
            while (start_len > 0 && mp_nodes[start_len - 1].c_str()[0] == '#') {
                start_len--;
            }

            // - Create a new path
            ::AST::Path np("", {});
            auto& np_nodes = np.nodes();
            np_nodes.reserve(start_len + e.nodes.size());
            for (unsigned int i = 0; i < start_len; i++) {
                np_nodes.push_back(mp_nodes[i]);
            }
            for (auto& en : e.nodes) {
                np_nodes.push_back(mv$(en));
            }

            if (!path.is_trivial()) {
                Resolve_Absolute_PathNodes(context, sp, np_nodes);
            }

            path = mv$(np);
        }
        TU_ARMA(Absolute, e) {
            DEBUG("- Absolute");
            // HACK: if the crate name starts with `=` it's a 2018 absolute path (references a crate loaded with `--extern`)
            if (/*context.m_crate.m_edition >= AST::Edition::Rust2018 &&*/ e.crate.c_str()[0] == '=') {
                // Absolute paths in 2018 edition are crate-prefixed?
                auto ec_it = AST::g_implicit_crates.find(e.crate.c_str() + 1);
                if (ec_it == AST::g_implicit_crates.end()) {
                    ERROR(sp, E0000, "Unable to find external crate for path " << path);
                }
                e.crate = ec_it->second;
            }
            // HACK: If this is `crate::foo::bar`, and `foo` doesn't exist in the root, but it is an implicit crate, then resolve to that
            // - This handles when a 2015 macro resolves to `::cratename::Bar` in a 2018+ crate
            else if (e.crate == "" && e.nodes.size() > 1 && context.m_crate.m_root_module.m_namespace_items.count(e.nodes.front().name()) == 0) {
                auto ec_it = AST::g_implicit_crates.find(e.nodes.front().name().c_str());
                if (ec_it != AST::g_implicit_crates.end()) {
                    e.crate = ec_it->second;
                    e.nodes.erase(e.nodes.begin());
                }
            }
            // Nothing to do (TODO: Bind?)
            Resolve_Absolute_PathNodes(context, sp, e.nodes);
        }
        TU_ARMA(UFCS, e) {
            DEBUG("- UFCS");
            Resolve_Absolute_Type(context, *e.type);
            if (e.trait && *e.trait != ::AST::Path()) {
                Resolve_Absolute_Path(context, sp, Context::LookupMode::Type, *e.trait);
            }

            Resolve_Absolute_PathNodes(context, sp, e.nodes);
        }
    }

    DEBUG("path = " << path);
    // TODO: Should this be deferred until the HIR?
    // - Doing it here so the HIR lowering has a bit more information
    // - Also handles splitting "absolute" paths into UFCS
    TU_MATCH_HDRA((path.m_class), {)
    default:
        BUG(sp, "Path wasn't absolutised correctly");
        TU_ARMA(Local, e) {
            if (!path.m_bindings.has_binding()) {
                TODO(sp, "Bind unbound local path - " << path);
            }
        }
        TU_ARMA(Absolute, e) {
            Resolve_Absolute_Path_BindAbsolute(context, sp, mode, path);
        }
        TU_ARMA(UFCS, e) {
            Resolve_Absolute_Path_BindUFCS(context, sp, mode, path);
        }
    }

    // TODO: Expand default type parameters?
    // - Helps with cases like PartialOrd<Self>, but hinders when the default is a hint (in expressions)

    //
    if(const auto* e = path.m_class.opt_UFCS())
    {
        if (!e->nodes.empty() && (!e->trait || !e->trait->is_valid()) && e->type->m_data.is_Generic() && e->type->m_data.as_Generic().index == GENERIC_Self) {
            const auto& name = e->nodes.front().name();

            if (const auto* self_ty = context.get_self_opt()) {
                // Check if we're in an enum
                if (const auto* ty_path = self_ty->m_data.opt_Path()) {
                    const auto& p = **ty_path;
                    if (const auto* pbe = p.m_bindings.type.binding.opt_Enum()) {
                        if (pbe->enum_) {
                            const auto& enm = *pbe->enum_;
                            auto it = std::find_if(enm.variants().begin(), enm.variants().end(), [&](const AST::EnumVariant& v) {
                                return v.m_name == name;
                            });
                            if (it != enm.variants().end()) {
                                unsigned idx = it - enm.variants().begin();
                                auto p2 = p.m_bindings.type.path + name;
                                auto new_path = std::move(p);
                                new_path.append(name);
                                if (it->m_data.is_Struct()) {
                                    new_path.m_bindings.type.set(p2, AST::PathBinding_Type::make_EnumVar({&enm, idx}));
                                } else {
                                    new_path.m_bindings.value.set(p2, AST::PathBinding_Value::make_EnumVar({&enm, idx}));
                                }
                                DEBUG("UFCS of enum variant converted to Generic: " << new_path);
                                path = std::move(new_path);
                            }
                        } else if (pbe->hir) {
                            // TODO: Could be in an `impl Trait for Foo`
                        } else {
                        }
                    }
                }
            }
        }
    }
}

void Resolve_Absolute_Lifetime(Context& context, const Span& sp, AST::LifetimeRef& lft) {
    TRACE_FUNCTION_FR("lft = " << lft, "lft = " << lft);
    if (lft.is_unbound()) {
        if (lft.name() == "static") {
            lft = AST::LifetimeRef::new_static();
            return;
        }

        if (lft.name() == "_") {
            // Note: '_ is just an explicit elided lifetime
            lft.set_binding(AST::LifetimeRef::BINDING_INFER);
            return;
        }

        for (auto it = context.m_name_context.rbegin(); it != context.m_name_context.rend(); ++it) {
            if (const auto* e = it->opt_Generic()) {
                for (const auto& l : e->lifetimes) {
                    // NOTE: Hygiene doesn't apply to lifetime params!
                    if (l.name.name == lft.name().name /*&& l.name.hygiene.is_visible(lft.name().hygiene)*/) {
                        lft.set_binding(l.value.index | (static_cast<int>(l.value.level) << 8));
                        return;
                    }
                }
            }
        }

        {
            // If parsing a function header, add a new lifetime param to the function
            // - Does the same apply to impl headers? Yes it does.
            if (context.m_ibl_target_generics) {
                DEBUG("Considering in-band-lifetimes");
                ASSERT_BUG(sp, !context.m_name_context.empty(), "Name context stack is empty");
                auto it = context.m_name_context.rbegin();
                ASSERT_BUG(sp, it->is_Generic(), "Name context stack end not Generic, instead " << it->tag_str());
                while (it->as_Generic().level == GenericSlot::Level::Hrb) {
                    it++;
                    ASSERT_BUG(sp, it != context.m_name_context.rend(), "");
                    ASSERT_BUG(sp, it->is_Generic(), "Name context stack end not Generic, instead " << it->tag_str());
                }
                if (it->as_Generic().level != GenericSlot::Level::Hrb) {
                    auto& context_gen = it->as_Generic();
                    auto& def_gen = *context.m_ibl_target_generics;
                    auto level = context_gen.level;
                    // 1. Assert that the last item of `context.m_name_context` is Generic, and matches `m_ibl_target_generics`
                    ASSERT_BUG(sp, context_gen.lifetimes.size() + context_gen.types.size() + context_gen.constants.size() == def_gen.m_params.size(), "");
                    // 2. Add the new lifetime to both `m_ibl_target_generics` and the last entry in m_name_context
                    size_t idx = context_gen.lifetimes.size();
                    def_gen.add_lft_param(AST::LifetimeParam(sp, {}, lft.name()));
                    context_gen.lifetimes.push_back(NamedI<GenericSlot>{lft.name(), GenericSlot{level, static_cast<unsigned short>(idx)}});
                    lft.set_binding(idx | (static_cast<int>(level) << 8));
                    return;
                }
            }
        }
        ERROR(sp, E0000, "Couldn't find lifetime " << lft);
    }
}

void Resolve_Absolute_Type(Context& context, TypeRef& type) {
    TRACE_FUNCTION_FR("type = " << type, "type = " << type);
    const auto& sp = type.span();

    if (type.m_data.is_Path() && type.m_data.as_Path()->m_bindings.type.binding.is_TypeParameter()) {
        auto& e = type.m_data.as_Path()->m_bindings.type.binding.as_TypeParameter();
        type.m_data = TypeData::make_Generic({type.m_data.as_Path()->as_trivial(), e.slot});
    }

    TU_MATCH_HDRA( (type.m_data), {)
    TU_ARMA(None, e) {
            // invalid type
        }
        TU_ARMA(Any, e) {
            // _ type
        }
        TU_ARMA(Unit, e) {
        }
        TU_ARMA(Bang, e) {
            // ! type
        }
        TU_ARMA(Macro, e) {
            BUG(sp, "Resolve_Absolute_Type - Encountered an unexpanded macro in type - " << type);
        }
        TU_ARMA(Primitive, e) {
        }
        TU_ARMA(Function, e) {
            context.push(e.info.hrbs);
            Resolve_Absolute_Type(context, *e.info.m_rettype);
            for (auto& t : e.info.m_arg_types) {
                Resolve_Absolute_Type(context, t);
            }
            context.pop(e.info.hrbs);
        }
        TU_ARMA(Tuple, e) {
            for (auto& t : e.inner_types) {
                Resolve_Absolute_Type(context, t);
            }
        }
        TU_ARMA(Borrow, e) {
            Resolve_Absolute_Lifetime(context, type.span(), e.lifetime);
            Resolve_Absolute_Type(context, *e.inner);
        }
        TU_ARMA(Pointer, e) {
            Resolve_Absolute_Type(context, *e.inner);
        }
        TU_ARMA(Array, e) {
            Resolve_Absolute_Type(context, *e.inner);
            if (e.size) {
                auto _h = context.enter_rootblock();
                Resolve_Absolute_ExprNode(context, *e.size);
            }
        }
        TU_ARMA(Slice, e) {
            Resolve_Absolute_Type(context, *e.inner);
        }
        TU_ARMA(Generic, e) {
            if (e.name == rcstring_Self) {
                type = context.get_self();
            } else {
                auto idx = context.lookup_local(type.span(), e.name, Context::LookupMode::Type);
                // TODO: Should this be bound to the relevant index, or just leave as-is?
                e.index = idx;
            }
        }
        TU_ARMA(Path, e) {
            Resolve_Absolute_Path(context, type.span(), Context::LookupMode::Type, *e);
            if (auto* ufcs = e->m_class.opt_UFCS()) {
                if (ufcs->nodes.size() == 0 /*&& ufcs->trait && *ufcs->trait == ::AST::Path()*/) {
                    auto ty = mv$(*ufcs->type);
                    type = mv$(ty);
                    return;
                }
                assert(ufcs->nodes.size() == 1);
            }

            if (e->m_bindings.type.binding.opt_Trait()) {
                auto tp = Type_TraitPath();
                tp.path = std::move(e);
                auto ty = ::TypeRef(type.span(), ::make_vec1(mv$(tp)), {});
                type = mv$(ty);
                return;
            }
            //else if(auto* be = e->m_bindings.type.binding.opt_TypeParameter())
            //{
            //}
        }
        TU_ARMA(TraitObject, e) {
            for (auto& trait : e.traits) {
                context.push(trait.hrbs);
                Resolve_Absolute_Path(context, type.span(), Context::LookupMode::Type, *trait.path);
                context.pop(trait.hrbs);
            }
            for (auto& lft : e.lifetimes) {
                Resolve_Absolute_Lifetime(context, type.span(), lft);
            }
        }
        TU_ARMA(ErasedType, e) {
            for (auto& trait : e->traits) {
                context.push(trait.hrbs);
                Resolve_Absolute_Path(context, type.span(), Context::LookupMode::Type, *trait.path);
                context.pop(trait.hrbs);
            }
            for (auto& trait : e->maybe_traits) {
                context.push(trait.hrbs);
                Resolve_Absolute_Path(context, type.span(), Context::LookupMode::Type, *trait.path);
                context.pop(trait.hrbs);
            }
            for (auto& lft : e->lifetimes) {
                Resolve_Absolute_Lifetime(context, type.span(), lft);
            }
            if (e->use) {
                Resolve_Absolute_PathParams(context, type.span(), *e->use);
            }
        }
    }
}

void Resolve_Absolute_Expr(Context& context, ::AST::Expr& expr) {
    if (expr.is_valid()) {
        Resolve_Absolute_ExprNode(context, expr.node());
    }
}

void Resolve_Absolute_ExprNode(Context& context, ::AST::ExprNode& node) {
    TRACE_FUNCTION_F("");

    struct NV: public AST::NodeVisitorDef {
        Context& context;

        NV(Context& context)
            : context(context)
        {
        }

        void visit(AST::ExprNode_Block& node) override {
            DEBUG("ExprNode_Block");
            if (node.m_local_mod) {
                auto _h = context.clear_rootblock();
                this->context.push(*node.m_local_mod);

                // Clone just the module stack part of the current context
                Resolve_Absolute_Mod(this->context.clone_mod(), *node.m_local_mod);
            }
            this->context.push_block();
            AST::NodeVisitorDef::visit(node);
            this->context.pop_block();
            if (node.m_local_mod) {
                this->context.pop(*node.m_local_mod);
            }
        }

        void visit(AST::ExprNode_Match& node) override {
            DEBUG("ExprNode_Match");
            node.m_val->visit(*this);
            for (auto& arm : node.m_arms) {
                this->context.push_block();

                this->context.start_patbind();
                // TODO: Save the context, ensure that each arm results in the same state.
                // - Or just an equivalent state
                // OR! Have a mode in the context that handles multiple bindings.
                for (auto& pat : arm.m_patterns) {
                    // If this isn't the first pattern, save the newly created bindings, roll back entire state, and check afterwards
                    Resolve_Absolute_Pattern(this->context, true, pat);
                    this->context.end_patbind_arm(pat.span());
                }
                this->context.end_patbind();

                for (auto& cond : arm.m_guard) {
                    cond.value->visit(*this);
                    if (cond.opt_pat) {
                        this->context.start_patbind();
                        Resolve_Absolute_Pattern(this->context, true, *cond.opt_pat);
                        this->context.end_patbind();
                    }
                }
                assert(arm.m_code);
                arm.m_code->visit(*this);

                this->context.pop_block();
            }
        }

        void visit(AST::ExprNode_Loop& node) override {
            this->context.push_block();
            node.m_code->visit(*this);
            this->context.pop_block();
        }

        void visit(AST::ExprNode_For& node) override {
            BUG(node.span(), "`for` should be desugared");
        }

        void visit(AST::ExprNode_While& node) override {
            this->context.push_block();
            visit_if_let_conditions(node.m_conditions);
            node.m_code->visit(*this);
            this->context.pop_block();
        }

        void visit(AST::ExprNode_LetBinding& node) override {
            DEBUG("ExprNode_LetBinding");
            Resolve_Absolute_Type(this->context, node.m_type);
            AST::NodeVisitorDef::visit(node);
            this->context.start_patbind();
            auto count = this->context.m_var_count;
            Resolve_Absolute_Pattern(this->context, node.m_else ? true : false, node.m_pat);
            this->context.end_patbind();
            auto n_vars = this->context.m_var_count - count;
            if (node.m_else) {
                //auto& vb = this->context.m_name_context.back().as_VarBlock();
                node.m_letelse_slots = std::make_pair(this->context.m_var_count, n_vars);
                this->context.m_var_count += n_vars;
            }
        }

        void visit_if_let_conditions(std::vector<AST::IfLet_Condition>& conds) {
            for (auto& cond : conds) {
                // Visit the value first, so it doesn't bind to the newly created variables in the pattern
                cond.value->visit(*this);

                if (cond.opt_pat) {
                    this->context.start_patbind();
                    Resolve_Absolute_Pattern(this->context, true, *cond.opt_pat);
                    this->context.end_patbind_arm(cond.opt_pat->span());
                    this->context.end_patbind();
                }
            }
        }

        void visit(AST::ExprNode_If& node) override {
            for (auto& arm : node.m_arms) {
                this->context.push_block();
                visit_if_let_conditions(arm.m_conditions);
                arm.m_body->visit(*this);
                this->context.pop_block();
            }
            if (node.m_else) {
                node.m_else->visit(*this);
            }
        }

        void visit(AST::ExprNode_StructLiteral& node) override {
            DEBUG("ExprNode_StructLiteral");
            Resolve_Absolute_Path(this->context, node.span(), Context::LookupMode::Type, node.m_path);
            AST::NodeVisitorDef::visit(node);
        }

        void visit(AST::ExprNode_StructLiteralPattern& node) override {
            DEBUG("ExprNode_StructLiteralPattern");
            Resolve_Absolute_Path(this->context, node.span(), Context::LookupMode::Type, node.m_path);
            AST::NodeVisitorDef::visit(node);
        }

        void visit(AST::ExprNode_CallPath& node) override {
            DEBUG("ExprNode_CallPath");
            Resolve_Absolute_Path(this->context, node.span(), Context::LookupMode::Variable, node.m_path);
            AST::NodeVisitorDef::visit(node);
        }

        void visit(AST::ExprNode_CallMethod& node) override {
            DEBUG("ExprNode_CallMethod");
            Resolve_Absolute_PathParams(this->context, node.span(), node.m_method.args());
            AST::NodeVisitorDef::visit(node);
        }

        void visit(AST::ExprNode_NamedValue& node) override {
            DEBUG("(" << node.span() << ") ExprNode_NamedValue - " << node.m_path);
            Resolve_Absolute_Path(this->context, node.span(), Context::LookupMode::Variable, node.m_path);
        }

        void visit(AST::ExprNode_Cast& node) override {
            DEBUG("ExprNode_Cast");
            Resolve_Absolute_Type(this->context, node.m_type);
            AST::NodeVisitorDef::visit(node);
        }

        void visit(AST::ExprNode_TypeAnnotation& node) override {
            DEBUG("ExprNode_TypeAnnotation");
            Resolve_Absolute_Type(this->context, node.m_type);
            AST::NodeVisitorDef::visit(node);
        }

        void visit(AST::ExprNode_Closure& node) override {
            DEBUG("ExprNode_Closure");

            Resolve_Absolute_Type(this->context, node.m_return);

            this->context.push_block();
            for (auto& arg : node.m_args) {
                Resolve_Absolute_Type(this->context, arg.second);
                this->context.start_patbind();
                Resolve_Absolute_Pattern(this->context, false, arg.first);
                this->context.end_patbind();
            }

            node.m_code->visit(*this);

            this->context.pop_block();
        }
    } expr_iter(context);

    node.visit(expr_iter);
}

void Resolve_Absolute_Generic(Context& context, ::AST::GenericParams& params) {
    for (auto& param : params.m_params) {
        TU_MATCH_HDRA( (param), {)
        TU_ARMA(None, _) {
            }
            TU_ARMA(Lifetime, param) {
            }
            TU_ARMA(Type, param) {
                Resolve_Absolute_Type(context, param.get_default());
            }
            TU_ARMA(Value, param) {
                Resolve_Absolute_Type(context, param.type());
                Resolve_Absolute_Expr(context, param.default_value());
            }
        }
    }
    for (auto& bound : params.m_bounds) {
        TU_MATCH(::AST::GenericBound, (bound), (e), (None, ), (Lifetime, Resolve_Absolute_Lifetime(context, bound.span, e.test); Resolve_Absolute_Lifetime(context, bound.span, e.bound);), (TypeLifetime, Resolve_Absolute_Type(context, e.type); Resolve_Absolute_Lifetime(context, bound.span, e.bound);), (IsTrait, context.push(e.outer_hrbs); Resolve_Absolute_Type(context, e.type); context.push(e.inner_hrbs); Resolve_Absolute_Path(context, bound.span, Context::LookupMode::Type, e.trait); context.pop(e.inner_hrbs); context.pop(e.outer_hrbs);), (MaybeTrait, Resolve_Absolute_Type(context, e.type); Resolve_Absolute_Path(context, bound.span, Context::LookupMode::Type, e.trait);), (NotTrait, Resolve_Absolute_Type(context, e.type); Resolve_Absolute_Path(context, bound.span, Context::LookupMode::Type, e.trait);), (Equality, Resolve_Absolute_Type(context, e.type); Resolve_Absolute_Type(context, e.replacement);))
    }
}

// Locals shouldn't be possible, as they'd end up as MaybeBind. Will assert the path class.
void Resolve_Absolute_PatternValue(/*const*/ Context& context, const Span& sp, ::AST::Pattern::Value& val) {
    TU_IFLET(
        ::AST::Pattern::Value,
        val,
        Named,
        e,
        //assert( ! e.is_trivial() );
        Resolve_Absolute_Path(context, sp, Context::LookupMode::Constant, e);
    )
}

void Resolve_Absolute_Pattern(Context& context, bool allow_refutable, ::AST::Pattern& pat) {
    TRACE_FUNCTION_FR("allow_refutable = " << allow_refutable << ", pat = " << pat, pat);
    for (auto& pb : pat.bindings()) {
        //if( !pat.data().is_Any() && ! allow_refutable )
        //    TODO(pat.span(), "Resolve_Absolute_Pattern - Encountered bound destructuring pattern");
        pb.m_slot = context.push_var(pat.span(), pb.m_name);
        DEBUG("- Binding #" << pb.m_slot << " '" << pb.m_name << "'");
    }

    TU_MATCH_HDRA( (pat.data()), {)
    TU_ARMA(MaybeBind, e) {
            auto name = mv$(e.name);
            // Attempt to resolve the name in the current namespace, and if it fails, it's a binding
            auto p = context.lookup_opt(name.name, name.hygiene, Context::LookupMode::PatternValue);
            if (p.is_valid()) {
                Resolve_Absolute_Path(context, pat.span(), Context::LookupMode::PatternValue, p);
                pat.data() = AST::Pattern::Data::make_Value({::AST::Pattern::Value::make_Named(mv$(p)), AST::Pattern::Value()});
                DEBUG("MaybeBind resolved to " << pat);
            } else {
                pat.bindings().push_back(AST::PatternBinding(mv$(name), AST::PatternBinding::Type::MOVE, false));
                pat.bindings().back().m_slot = context.push_var(pat.span(), pat.bindings().back().m_name);
                pat.data() = AST::Pattern::Data::make_Any({});
                DEBUG("- Binding #" << pat.bindings().back().m_slot << " '" << pat.bindings().back().m_name << "' (was MaybeBind)");
            }
        }
        TU_ARMA(Macro, e) {
            BUG(pat.span(), "Resolve_Absolute_Pattern - Encountered Macro - " << pat);
        }
        TU_ARMA(Any, e) {
            // Ignore '_'
        }
        TU_ARMA(Box, e) {
            Resolve_Absolute_Pattern(context, allow_refutable, *e.sub);
        }
        TU_ARMA(Ref, e) {
            Resolve_Absolute_Pattern(context, allow_refutable, *e.sub);
        }
        TU_ARMA(Value, e) {
            // Disabled check : Some code does `let (Foo | Bar);` where those are the only options
            //if( ! allow_refutable )
            //{
            //    // TODO: If this is a single value of a unit-like struct, accept
            //    BUG(pat.span(), "Resolve_Absolute_Pattern - Encountered refutable pattern where only irrefutable allowed - " << pat);
            //}
            Resolve_Absolute_PatternValue(context, pat.span(), e.start);
            Resolve_Absolute_PatternValue(context, pat.span(), e.end);
        }
        TU_ARMA(ValueLeftInc, e) {
            if (!allow_refutable) {
                // TODO: If this is a single value of a unit-like struct, accept
                BUG(pat.span(), "Resolve_Absolute_Pattern - Encountered refutable pattern where only irrefutable allowed - " << pat);
            }
            Resolve_Absolute_PatternValue(context, pat.span(), e.start);
            Resolve_Absolute_PatternValue(context, pat.span(), e.end);
        }
        TU_ARMA(Tuple, e) {
            for (auto& sp : e.start) {
                Resolve_Absolute_Pattern(context, allow_refutable, sp);
            }
            for (auto& sp : e.end) {
                Resolve_Absolute_Pattern(context, allow_refutable, sp);
            }
        }
        TU_ARMA(StructTuple, e) {
            Resolve_Absolute_Path(context, pat.span(), Context::LookupMode::Constant, e.path);
            for (auto& sp : e.tup_pat.start) {
                Resolve_Absolute_Pattern(context, allow_refutable, sp);
            }
            for (auto& sp : e.tup_pat.end) {
                Resolve_Absolute_Pattern(context, allow_refutable, sp);
            }
        }
        TU_ARMA(Struct, e) {
            // `Struct { .. }` patterns can match anything, so switch lookup mode in that case
            Resolve_Absolute_Path(context, pat.span(), e.sub_patterns.empty() ? Context::LookupMode::PatternType : Context::LookupMode::Type, e.path);
            for (auto& sp : e.sub_patterns) {
                Resolve_Absolute_Pattern(context, allow_refutable, sp.pat);
            }
        }
        TU_ARMA(Slice, e) {
            // NOTE: Can be irrefutable (if the type is array)
            for (auto& sp : e.sub_pats) {
                Resolve_Absolute_Pattern(context, allow_refutable, sp);
            }
        }
        TU_ARMA(SplitSlice, e) {
            // NOTE: Can be irrefutable (if the type is array)
            for (auto& sp : e.leading) {
                Resolve_Absolute_Pattern(context, allow_refutable, sp);
            }
            if (e.extra_bind.is_valid()) {
                e.extra_bind.m_slot = context.push_var(pat.span(), e.extra_bind.m_name);
            }
            for (auto& sp : e.trailing) {
                Resolve_Absolute_Pattern(context, allow_refutable, sp);
            }
        }
        TU_ARMA(Or, e) {
            // TODO: Need to ensure that all arms bind the same set of variables
            context.start_patbind();
            for (auto& sp : e) {
                Resolve_Absolute_Pattern(context, allow_refutable, sp);
                context.end_patbind_arm(sp.span());
            }
            context.end_patbind();
        }
    }
}

// - For traits
void Resolve_Absolute_ImplItems(Context& item_context, ::AST::NamedList<::AST::Item>& items) {
    TRACE_FUNCTION_F("");
    for (auto& i : items) {
        TU_MATCH_HDRA((i.data), {)
        TU_ARMA(None, e) {
            }
            TU_ARMA(MacroInv, e) {
                //BUG(i.span, "Resolve_Absolute_ImplItems - MacroInv");
            }
            TU_ARMA(ExternBlock, e) BUG(i.span, "Resolve_Absolute_ImplItems - " << i.data.tag_str());
            TU_ARMA(Impl, e) BUG(i.span, "Resolve_Absolute_ImplItems - " << i.data.tag_str());
            TU_ARMA(NegImpl, e) BUG(i.span, "Resolve_Absolute_ImplItems - " << i.data.tag_str());
            TU_ARMA(GlobalAsm, e) BUG(i.span, "Resolve_Absolute_ImplItems - " << i.data.tag_str());
            TU_ARMA(Macro, e) BUG(i.span, "Resolve_Absolute_ImplItems - " << i.data.tag_str());
            TU_ARMA(Use, e) BUG(i.span, "Resolve_Absolute_ImplItems - Use");
            TU_ARMA(Module, e) BUG(i.span, "Resolve_Absolute_ImplItems - Module");
            TU_ARMA(Crate, e) BUG(i.span, "Resolve_Absolute_ImplItems - Crate");
            TU_ARMA(Enum, e) BUG(i.span, "Resolve_Absolute_ImplItems - Enum");
            TU_ARMA(Trait, e) BUG(i.span, "Resolve_Absolute_ImplItems - " << i.data.tag_str());
            TU_ARMA(TraitAlias, e) BUG(i.span, "Resolve_Absolute_ImplItems - " << i.data.tag_str());
            TU_ARMA(Struct, e) BUG(i.span, "Resolve_Absolute_ImplItems - Struct");
            TU_ARMA(Union, e) BUG(i.span, "Resolve_Absolute_ImplItems - Union");
            TU_ARMA(Type, e) {
                DEBUG("Type - " << i.name);
                //ASSERT_BUG( i.span, e.params().m_params.size() == 0, "TODO: Generic Associated Types (Trait)" );
                item_context.push(e.params(), GenericSlot::Level::Method, true);
                Resolve_Absolute_Generic(item_context, e.m_params);
                Resolve_Absolute_Generic(item_context, e.m_self_bounds);

                Resolve_Absolute_Type(item_context, e.type());

                item_context.pop(e.params(), true);
            }
            TU_ARMA(Function, e) {
                DEBUG("Function - " << i.name);
                Resolve_Absolute_Function(item_context, e);
            }
            TU_ARMA(Static, e) {
                DEBUG("Static - " << i.name);
                Resolve_Absolute_Type(item_context, e.type());
                auto _h = item_context.enter_rootblock();
                Resolve_Absolute_Expr(item_context, e.value());
            }
        }
    }
}

// - For impl blocks
void Resolve_Absolute_ImplItems(Context& item_context, ::std::vector<::AST::Impl::ImplItem>& items) {
    TRACE_FUNCTION_F("");
    for (auto& i : items) {
        TU_MATCH(
            AST::Item,
            (*i.data),
            (e),
            (None, ),
            (MacroInv, ),

            (Impl, BUG(i.sp, "Resolve_Absolute_ImplItems - " << i.data->tag_str());),
            (NegImpl, BUG(i.sp, "Resolve_Absolute_ImplItems - " << i.data->tag_str());),
            (ExternBlock, BUG(i.sp, "Resolve_Absolute_ImplItems - " << i.data->tag_str());),
            (GlobalAsm, BUG(i.sp, "Resolve_Absolute_ImplItems - " << i.data->tag_str());),
            (Macro, BUG(i.sp, "Resolve_Absolute_ImplItems - " << i.data->tag_str());),
            (Use, BUG(i.sp, "Resolve_Absolute_ImplItems - " << i.data->tag_str());),
            (Module, BUG(i.sp, "Resolve_Absolute_ImplItems - " << i.data->tag_str());),
            (Crate, BUG(i.sp, "Resolve_Absolute_ImplItems - " << i.data->tag_str());),
            (Enum, BUG(i.sp, "Resolve_Absolute_ImplItems - " << i.data->tag_str());),
            (Trait, BUG(i.sp, "Resolve_Absolute_ImplItems - " << i.data->tag_str());),
            (TraitAlias, BUG(i.sp, "Resolve_Absolute_ImplItems - " << i.data->tag_str());),
            (Struct, BUG(i.sp, "Resolve_Absolute_ImplItems - " << i.data->tag_str());),
            (Union, BUG(i.sp, "Resolve_Absolute_ImplItems - " << i.data->tag_str());),
            (Type, DEBUG("Type - " << i.name);
             //ASSERT_BUG( i.span, e.params().m_params.size() == 0, "TODO: Generic Associated Types (impl)" );
             item_context.push(e.params(), GenericSlot::Level::Method, true);
             Resolve_Absolute_Generic(item_context, e.params());

             Resolve_Absolute_Type(item_context, e.type());

             item_context.pop(e.params(), true);),
            (Function, DEBUG("Function - " << i.name); Resolve_Absolute_Function(item_context, e);),
            (Static, DEBUG("Static - " << i.name); Resolve_Absolute_Type(item_context, e.type()); auto _h = item_context.enter_rootblock(); Resolve_Absolute_Expr(item_context, e.value());)
        )
    }
}

void Resolve_Absolute_Function(Context& item_context, ::AST::Function& fcn) {
    TRACE_FUNCTION_F("");
    item_context.push(fcn.params(), GenericSlot::Level::Method);
    item_context.m_ibl_target_generics = &fcn.params();
    DEBUG("- Generics");
    Resolve_Absolute_Generic(item_context, fcn.params());

    DEBUG("- Prototype types");
    Resolve_Absolute_Type(item_context, fcn.rettype());
    for (auto& arg : fcn.args()) {
        Resolve_Absolute_Type(item_context, arg.ty);
    }
    item_context.m_ibl_target_generics = nullptr;

    DEBUG("- Body");
    {
        auto _h = item_context.enter_rootblock();
        item_context.push_block();
        for (auto& arg : fcn.args()) {
            item_context.start_patbind();
            Resolve_Absolute_Pattern(item_context, false, arg.pat);
            item_context.end_patbind();
        }

        Resolve_Absolute_Expr(item_context, fcn.code());

        item_context.pop_block();
    }

    item_context.pop(fcn.params());
}

void Resolve_Absolute_Static(Context& item_context, ::AST::Static& e) {
    Resolve_Absolute_Type(item_context, e.type());
    auto _h = item_context.enter_rootblock();
    Resolve_Absolute_Expr(item_context, e.value());
}

void Resolve_Absolute_Struct(Context& item_context, ::AST::Struct& e) {
    item_context.push(e.params(), GenericSlot::Level::Top, true);
    Resolve_Absolute_Generic(item_context, e.params());

    TU_MATCH(::AST::StructData, (e.m_data), (s), (Unit, ), (Tuple, for (auto& field : s.ents) { Resolve_Absolute_Type(item_context, field.m_type); }), (Struct, for (auto& field : s.ents) {
                 Resolve_Absolute_Type(item_context, field.m_type);
                 Resolve_Absolute_Expr(item_context, field.m_default);
             }))

    item_context.pop(e.params());
}

void Resolve_Absolute_Union(Context& item_context, ::AST::Union& e) {
    item_context.push(e.m_params, GenericSlot::Level::Top, true);
    Resolve_Absolute_Generic(item_context, e.m_params);

    for (auto& field : e.m_variants) {
        Resolve_Absolute_Type(item_context, field.m_type);
    }

    item_context.pop(e.m_params);
}

void Resolve_Absolute_Trait(Context& item_context, ::AST::Trait& e) {
    item_context.push(e.params(), GenericSlot::Level::Top, true);
    Resolve_Absolute_Generic(item_context, e.params());

    for (auto& lft : e.lifetimes()) {
        Resolve_Absolute_Lifetime(item_context, lft.sp, lft.ent);
    }
    for (auto& st : e.supertraits()) {
        if (!st.ent.path->is_valid()) {
            DEBUG("- ST 'static");
        } else {
            DEBUG("- ST " << st.ent.hrbs << *st.ent.path);
            item_context.push(st.ent.hrbs);
            Resolve_Absolute_Path(item_context, st.sp, Context::LookupMode::Type, *st.ent.path);
            item_context.pop(st.ent.hrbs);
        }
    }

    Resolve_Absolute_ImplItems(item_context, e.items());

    item_context.pop(e.params(), true);
}

void Resolve_Absolute_Enum(Context& item_context, ::AST::Enum& e) {
    item_context.push(e.params(), GenericSlot::Level::Top, true);
    Resolve_Absolute_Generic(item_context, e.params());

    for (auto& variant : e.variants()) {
        TU_MATCH(::AST::EnumVariantData, (variant.m_data), (s), (Unit, ), (Tuple, for (auto& field : s.m_items) { Resolve_Absolute_Type(item_context, field.m_type); }), (Struct, for (auto& field : s.m_fields) {
                     Resolve_Absolute_Type(item_context, field.m_type);
                     Resolve_Absolute_Expr(item_context, field.m_default);
                 }))
        auto _h = item_context.enter_rootblock();
        Resolve_Absolute_Expr(item_context, variant.m_discriminant_value);
    }

    item_context.pop(e.params());
}

void Resolve_Absolute_Mod(const ::AST::Crate& crate, ::AST::Module& mod) {
    Resolve_Absolute_Mod(Context{crate, mod}, mod);
}

void Resolve_Absolute_Mod(Context item_context, ::AST::Module& mod) {
    TRACE_FUNCTION_F("mod=" << mod.path());

    for (auto& i : mod.m_items) {
        TU_MATCH_HDRA( (i->data), {)
        TU_ARMA(None, e) {
            }
            TU_ARMA(MacroInv, e) {
            }
            TU_ARMA(Use, e) {
            }
            TU_ARMA(Macro, e) {
            }
            TU_ARMA(GlobalAsm, e) {
            }
            TU_ARMA(ExternBlock, e) {
                for (auto& i2 : e.items()) {
                    TU_MATCH_DEF(AST::Item, (i2.data), (e2), (BUG(i->span, "Unexpected item in ExternBlock - " << i2.data.tag_str());), (None, ), (Function, Resolve_Absolute_Function(item_context, e2);), (Static, Resolve_Absolute_Static(item_context, e2);))
                }
            }
            TU_ARMA(Impl, e) {
                auto& def = e.def();
                if (!def.type().is_valid()) {
                    TRACE_FUNCTION_F("impl " << def.trait().ent << " for ..");
                    item_context.push(def.params(), GenericSlot::Level::Top);

                    item_context.m_ibl_target_generics = &def.params();
                    assert(def.trait().ent.is_valid());
                    Resolve_Absolute_Path(item_context, def.trait().sp, Context::LookupMode::Type, def.trait().ent);
                    item_context.m_ibl_target_generics = nullptr;

                    Resolve_Absolute_Generic(item_context, def.params());

                    if (e.items().size() != 0) {
                        ERROR(i->span, E0000, "impl Trait for .. with methods");
                    }

                    item_context.pop(def.params());

                    // HACK: Mutate the source to indicate that it's an auto trait
                    const_cast<::AST::Trait*>(def.trait().ent.m_bindings.type.binding.as_Trait().trait_)->set_is_marker();
                } else {
                    TRACE_FUNCTION_F("impl " << def.trait().ent << " for " << def.type());
                    item_context.push_self(def.type());
                    item_context.push(def.params(), GenericSlot::Level::Top);

                    item_context.m_ibl_target_generics = &def.params();
                    Resolve_Absolute_Type(item_context, def.type());
                    if (def.trait().ent.is_valid()) {
                        Resolve_Absolute_Path(item_context, def.trait().sp, Context::LookupMode::Type, def.trait().ent);
                    }
                    item_context.m_ibl_target_generics = nullptr;

                    Resolve_Absolute_Generic(item_context, def.params());

                    Resolve_Absolute_ImplItems(item_context, e.items());

                    item_context.pop(def.params());
                    item_context.pop_self(def.type());
                }
            }
            TU_ARMA(NegImpl, e) {
                auto& impl_def = e;
                TRACE_FUNCTION_F("impl ! " << impl_def.trait().ent << " for " << impl_def.type());
                item_context.push_self(impl_def.type());
                item_context.push(impl_def.params(), GenericSlot::Level::Top);

                item_context.m_ibl_target_generics = &impl_def.params();
                Resolve_Absolute_Type(item_context, impl_def.type());
                if (!impl_def.trait().ent.is_valid()) {
                    BUG(i->span, "Encountered negative impl with no trait");
                }
                Resolve_Absolute_Path(item_context, impl_def.trait().sp, Context::LookupMode::Type, impl_def.trait().ent);
                item_context.m_ibl_target_generics = nullptr;

                Resolve_Absolute_Generic(item_context, impl_def.params());

                // No items

                item_context.pop(impl_def.params());
                item_context.pop_self(impl_def.type());
            }
            TU_ARMA(Module, e) {
                DEBUG("Module - " << i->name);
                Resolve_Absolute_Mod(item_context.m_crate, e);
            }
            TU_ARMA(Crate, e) {
                // - Nothing
            }
            TU_ARMA(Enum, e) {
                DEBUG("Enum - " << i->name);
                Resolve_Absolute_Enum(item_context, e);
            }
            TU_ARMA(Trait, e) {
                DEBUG("Trait - " << i->name);
                Resolve_Absolute_Trait(item_context, e);
            }
            TU_ARMA(TraitAlias, e) {
                DEBUG("TraitAlias - " << i->name);
                item_context.push(e.params, GenericSlot::Level::Top, true);
                Resolve_Absolute_Generic(item_context, e.params);

                for (auto& st : e.traits) {
                    item_context.push(st.ent.hrbs);
                    Resolve_Absolute_Path(item_context, st.sp, Context::LookupMode::Type, *st.ent.path);
                    item_context.pop(st.ent.hrbs);
                }

                item_context.pop(e.params, true);
            }
            TU_ARMA(Type, e) {
                DEBUG("Type - " << i->name);
                item_context.push(e.params(), GenericSlot::Level::Top, true);
                Resolve_Absolute_Generic(item_context, e.params());

                Resolve_Absolute_Type(item_context, e.type());

                item_context.pop(e.params(), true);
            }
            TU_ARMA(Struct, e) {
                DEBUG("Struct - " << i->name);
                Resolve_Absolute_Struct(item_context, e);
            }
            TU_ARMA(Union, e) {
                DEBUG("Union - " << i->name);
                Resolve_Absolute_Union(item_context, e);
            }
            TU_ARMA(Function, e) {
                DEBUG("Function - " << i->name);
                Resolve_Absolute_Function(item_context, e);
            }
            TU_ARMA(Static, e) {
                DEBUG("Static - " << i->name);
                Resolve_Absolute_Static(item_context, e);
            }
        }
    }

    // - Run through the indexed items and fix up those paths
    static Span sp;
    DEBUG("Imports (mod = " << mod.path() << ")");
    for (auto& i : mod.m_namespace_items) {
        if (i.second.is_import) {
            Resolve_Absolute_Path(item_context, sp, Context::LookupMode::Namespace, i.second.path);
        }
    }
    for (auto& i : mod.m_type_items) {
        if (i.second.is_import) {
            Resolve_Absolute_Path(item_context, sp, Context::LookupMode::Type, i.second.path);
        }
    }
    for (auto& i : mod.m_value_items) {
        if (i.second.is_import) {
            Resolve_Absolute_Path(item_context, sp, Context::LookupMode::Constant, i.second.path);
        }
    }
}

void Resolve_Absolutise(AST::Crate& crate) {
    Resolve_Absolute_Mod(crate, crate.root_module());
}

#undef FLAG_CONST_GENERIC

#include "ast_ast.h"
#include "ast_crate.h"
#include "main_bindings.h"
#include "hir_hir.h"
#include "macro_rules_macro_rules.h"

enum class IndexName {
    Namespace,
    Type,
    Value,
    Macro,
};

void Resolve_Index_Module_Wildcard__use_stmt(AST::Crate& crate, AST::Module& dst_mod, const AST::UseItem::Ent& i_data, const AST::Visibility& vis);

::std::ostream& operator<<(::std::ostream& os, const IndexName& loc) {
    switch (loc) {
        case IndexName::Namespace:
            return os << "namespace";
        case IndexName::Type:
            return os << "type";
        case IndexName::Value:
            return os << "value";
        case IndexName::Macro:
            return os << "macro";
    }
    throw "";
}

::std::unordered_map<RcString, ::AST::Module::IndexEnt>& get_mod_index(::AST::Module& mod, IndexName location) {
    switch (location) {
        case IndexName::Namespace:
            return mod.m_namespace_items;
        case IndexName::Type:
            return mod.m_type_items;
        case IndexName::Value:
            return mod.m_value_items;
        case IndexName::Macro:
            return mod.m_macro_items;
    }
    throw "";
}

namespace {
    AST::Path hir_to_ast(const HIR::SimplePath& p) {
        // The crate name here has to be non-empty, because it's external.
        assert(p.crate_name() != "");
        AST::Path rv(p.crate_name(), {});
        rv.nodes().reserve(p.components().size());
        for (const auto& n : p.components()) {
            rv.nodes().push_back(AST::PathNode(n));
        }
        return rv;
    }
} // namespace

void _add_item(const Span& sp, AST::Module& mod, IndexName location, const RcString& name, const AST::Visibility& vis, ::AST::Path ir, bool error_on_collision = true) {
    ASSERT_BUG(sp, ir.m_bindings.has_binding(), "Adding item with no binding - " << ir);
    auto& list = get_mod_index(mod, location);

    if (location != IndexName::Namespace) {
        ASSERT_BUG(sp, ir.m_class.as_Absolute().nodes.size() > 0, "Non-namespace path must have nodes - " << location << " " << name << " = " << ir);
    }

    // Add traits to a separate list
    if (ir.m_bindings.type.binding.is_Trait()) {
        auto it = std::find(mod.m_traits.begin(), mod.m_traits.end(), ir.m_bindings.type.path);
        if (it == mod.m_traits.end()) {
            mod.m_traits.push_back(ir.m_bindings.type.path);
        }
    }

    bool was_import = (ir != mod.path() + name);
    if (list.count(name) > 0) {
        auto& e = list.at(name);
        if (e.path == ir) {
            // Ignore, re-import of the same thing

            // Update the visibility, if this new visibility adds anything
            if (!e.vis.contains(vis)) {
                e.vis.inplace_union(vis);
                DEBUG("### Import " << location << " item " << mod.path() << " :: " << name << " = " << ir << " (update to " << e.vis << ")");
            }
        } else if (error_on_collision) {
            ERROR(sp, E0000, "Duplicate definition of name '" << name << "' in " << location << " scope (" << mod.path() << ") " << ir << ", and " << e.path);
        } else {
            DEBUG(location << " name collision - '" << name << "' = " << ir << ", ignored (mod=" << mod.path() << ", was " << e.path << ")");
        }
    } else {
        DEBUG("### " << (was_import ? "Import" : "Add") << " " << location << " item " << mod.path() << " :: " << name << " = " << ir << vis);
        auto rec = list.insert(::std::make_pair(name, ::AST::Module::IndexEnt{was_import, mv$(vis), mv$(ir)}));
        assert(rec.second);
    }
}

void _add_item_type(const Span& sp, AST::Module& mod, const RcString& name, const AST::Visibility& vis, ::AST::Path ir, bool error_on_collision = true) {
    _add_item(sp, mod, IndexName::Namespace, name, vis, ::AST::Path(ir), error_on_collision);
    _add_item(sp, mod, IndexName::Type, name, vis, ::std::move(ir), error_on_collision);
}

void _add_item_value(const Span& sp, AST::Module& mod, const RcString& name, const AST::Visibility& vis, ::AST::Path ir, bool error_on_collision = true) {
    _add_item(sp, mod, IndexName::Value, name, vis, mv$(ir), error_on_collision);
}

void Resolve_Index_Module_Base(const AST::Crate& crate, AST::Module& mod) {
    TRACE_FUNCTION_F("mod = " << mod.path());
    for (const auto& i : mod.m_items) {
        auto ap = mod.path() + i->name;
        auto p = ::AST::Path(ap);
        //DEBUG("- p = " << p << " : " << ::AST::Item::tag_to_str(i.data.tag()));

        TU_MATCH_HDRA( (i->data), {)
        TU_ARMA(None, e) {
            }
            TU_ARMA(MacroInv, e) {
            }
            // Unnamed
            TU_ARMA(ExternBlock, e) {
            }
            TU_ARMA(Impl, e) {
            }
            TU_ARMA(NegImpl, e) {
            }
            TU_ARMA(GlobalAsm, e) {
            }

            TU_ARMA(Macro, e) {
                // Handled by `for(const auto& item : mod.macros())` below
                //p.m_bindings.macro = ::AST::PathBinding_Macro::make_MacroRules({nullptr, e ? &*e : nullptr});
                //_add_item(i->span, mod, IndexName::Macro, i->name, i->vis, mv$(p));
            }

            TU_ARMA(Use, e) {
                // Skip for now
            }
            // - Types/modules only
            TU_ARMA(Module, e) {
                p.m_bindings.type.set(ap, ::AST::PathBinding_Type::make_Module({&e}));
                _add_item(i->span, mod, IndexName::Namespace, i->name, i->vis, mv$(p));
            }
            TU_ARMA(Crate, e) {
                if (e.name != "") {
                    ASSERT_BUG(i->span, crate.m_extern_crates.count(e.name) > 0, "Referenced crate '" << e.name << "' isn't loaded for `extern crate`");
                    p.m_bindings.type.set(ap, ::AST::PathBinding_Type::make_Crate({&crate.m_extern_crates.at(e.name)}));
                } else {
                    p.m_bindings.type.set(ap, ::AST::PathBinding_Type::make_Module({&crate.m_root_module}));
                }
                _add_item(i->span, mod, IndexName::Namespace, i->name, i->vis, mv$(p));
            }
            TU_ARMA(Enum, e) {
                p.m_bindings.type.set(ap, ::AST::PathBinding_Type::make_Enum({&e}));
                _add_item_type(i->span, mod, i->name, i->vis, mv$(p));
            }
            TU_ARMA(Union, e) {
                p.m_bindings.type.set(ap, ::AST::PathBinding_Type::make_Union({&e}));
                _add_item_type(i->span, mod, i->name, i->vis, mv$(p));
            }
            TU_ARMA(Trait, e) {
                p.m_bindings.type.set(ap, ::AST::PathBinding_Type::make_Trait({&e}));
                _add_item_type(i->span, mod, i->name, i->vis, mv$(p));
            }
            TU_ARMA(TraitAlias, e) {
                p.m_bindings.type.set(ap, ::AST::PathBinding_Type::make_TraitAlias({&e}));
                _add_item_type(i->span, mod, i->name, i->vis, mv$(p));
            }
            TU_ARMA(Type, e) {
                p.m_bindings.type.set(ap, ::AST::PathBinding_Type::make_TypeAlias({&e}));
                _add_item_type(i->span, mod, i->name, i->vis, mv$(p));
            }
            // - Mixed
            TU_ARMA(Struct, e) {
                p.m_bindings.type.set(ap, ::AST::PathBinding_Type::make_Struct({&e}));
                // - If the struct is a tuple-like struct (or unit-like), it presents in the value namespace
                if (!e.m_data.is_Struct()) {
                    p.m_bindings.value.set(ap, ::AST::PathBinding_Value::make_Struct({&e}));
                    _add_item_value(i->span, mod, i->name, i->vis, p);
                }
                _add_item_type(i->span, mod, i->name, i->vis, mv$(p));
            }
            // - Values only
            TU_ARMA(Function, e) {
                p.m_bindings.value.set(ap, ::AST::PathBinding_Value::make_Function({&e}));
                _add_item_value(i->span, mod, i->name, i->vis, mv$(p));
            }
            TU_ARMA(Static, e) {
                p.m_bindings.value.set(ap, ::AST::PathBinding_Value::make_Static({&e}));
                _add_item_value(i->span, mod, i->name, i->vis, mv$(p));
            }
        }
    }

    for (const auto& item : mod.macros()) {
        ::AST::Path p = mod.path() + item.name;
        p.m_bindings.macro.set(mod.path() + item.name, ::AST::PathBinding_Macro::make_MacroRules({nullptr, &*item.data}));
        // NOTE: Macros can be freely duplicated, BUT the last entry takes precedence (TODO)
        _add_item(item.span, mod, IndexName::Macro, item.name, item.vis, mv$(p), /*error_on_collision=*/false);
    }

    bool has_pub_wildcard = false;
    // Named imports
    for (const auto& ip : mod.m_items) {
        const auto& i = *ip;
        if (!i.data.is_Use()) {
            continue;
        }
        for (const auto& i_data : i.data.as_Use().entries) {
            if (i_data.name != "") {
                DEBUG("Use " << i_data.name << " = " << i_data.path);
                // TODO: Ensure that the path is canonical?

                const auto& sp = i_data.sp;
                ASSERT_BUG(sp, i_data.path.m_bindings.has_binding(), "`use " << i_data.path << "` left unbound in module " << mod.path());
                const auto& pb = i_data.path.m_bindings;

                bool allow_collide = true; // Allow collisions (`use` can import mutliple namespaces, local gets priority)
                // - Types
            TU_MATCH_HDRA( (pb.type.binding), {)
            TU_ARMA(Unbound, _e) {
                        DEBUG(i_data.name << " - Not a type/module");
                    }
                    TU_ARMA(TypeParameter, e)
                    BUG(sp, "Import was bound to type parameter");
                    TU_ARMA(Primitive, e)
                    _add_item_type(sp, mod, i_data.name, i.vis, pb.type, !allow_collide);
                    TU_ARMA(Crate, e)
                    _add_item(sp, mod, IndexName::Namespace, i_data.name, i.vis, pb.type, !allow_collide);
                    TU_ARMA(Module, e)
                    _add_item(sp, mod, IndexName::Namespace, i_data.name, i.vis, pb.type, !allow_collide);
                    TU_ARMA(Enum, e)
                    _add_item_type(sp, mod, i_data.name, i.vis, pb.type, !allow_collide);
                    TU_ARMA(Union, e)
                    _add_item_type(sp, mod, i_data.name, i.vis, pb.type, !allow_collide);
                    TU_ARMA(Trait, e)
                    _add_item_type(sp, mod, i_data.name, i.vis, pb.type, !allow_collide);
                    TU_ARMA(TraitAlias, e)
                    _add_item_type(sp, mod, i_data.name, i.vis, pb.type, !allow_collide);
                    TU_ARMA(TypeAlias, e)
                    _add_item_type(sp, mod, i_data.name, i.vis, pb.type, !allow_collide);
                    TU_ARMA(Struct, e)
                    _add_item_type(sp, mod, i_data.name, i.vis, pb.type, !allow_collide);
                    TU_ARMA(EnumVar, e)
                    _add_item_type(sp, mod, i_data.name, i.vis, pb.type, !allow_collide);
            }
            // - Values
            TU_MATCH_HDRA( (pb.value.binding), {)
            TU_ARMA(Unbound, _e) {
                        DEBUG(i_data.name << " - Not a value");
                    }
                    TU_ARMA(Variable, e)
                    BUG(sp, "Import was bound to a variable");
                    TU_ARMA(Generic, e)
                    BUG(sp, "Import was bound to a generic value");
                    TU_ARMA(Struct, e)
                    _add_item_value(sp, mod, i_data.name, i.vis, pb.value, !allow_collide);
                    TU_ARMA(EnumVar, e)
                    _add_item_value(sp, mod, i_data.name, i.vis, pb.value, !allow_collide);
                    TU_ARMA(Static, e)
                    _add_item_value(sp, mod, i_data.name, i.vis, pb.value, !allow_collide);
                    TU_ARMA(Function, e)
                    _add_item_value(sp, mod, i_data.name, i.vis, pb.value, !allow_collide);
            }
            // - Macros
            TU_MATCH_HDRA( (pb.macro.binding), {)
            TU_ARMA(Unbound, _e) {
                        DEBUG(i_data.name << " - Not a macro");
                    }
                    TU_ARMA(MacroRules, e) {
                        _add_item(sp, mod, IndexName::Macro, i_data.name, i.vis, pb.macro, !allow_collide);
                    }
                    TU_ARMA(ProcMacro, e) {
                        _add_item(sp, mod, IndexName::Macro, i_data.name, i.vis, pb.macro, !allow_collide);
                    }
                    TU_ARMA(ProcMacroAttribute, e) {
                        TODO(sp, "ProcMacroAttribute import");
                    }
                    TU_ARMA(ProcMacroDerive, e) {
                        TODO(sp, "ProcMacroDerive import");
                    }
            }
            } else {
                if (i.vis.ty() != AST::Visibility::Ty::Private) {
                    has_pub_wildcard = true;
                }
            }
        }
    }

    mod.m_index_populated = (has_pub_wildcard ? 1 : 2);

    // Handle child modules
    for (auto& i : mod.m_items) {
        if (auto* e = i->data.opt_Module()) {
            Resolve_Index_Module_Base(crate, *e);
        }
    }
    for (auto& mp : mod.anon_mods()) {
        if (mp) {
            Resolve_Index_Module_Base(crate, *mp);
        }
    }
}

void Resolve_Index_Module_Wildcard__glob_in_hir_mod(
    const Span& sp,
    const AST::Crate& crate,
    AST::Module& dst_mod,
    /*const AST::ExternCrate& hcrate,*/ const ::HIR::Module& hmod,
    const ::AST::Path& path,
    const ::AST::Visibility& vis,
    AST::AbsolutePath mod_ap
) {
    TRACE_FUNCTION_F(dst_mod.path() << " <= " << mod_ap);
    for (const auto& it : hmod.m_mod_items) {
        const auto& ve = *it.second;
        if (ve.publicity.is_global()) {
            const auto* vep = &ve.ent;

            ::AST::PathBinding<::AST::PathBinding_Type> pb;
            if (vep->is_Import()) {
                const auto& spath = vep->as_Import().path;
                pb.path.crate = spath.crate_name();
                pb.path.nodes = spath.components_vec();

                ASSERT_BUG(sp, crate.m_extern_crates.count(spath.crate_name()) == 1, "Crate " << spath.crate_name() << " is not loaded");
                const auto* hmod = &crate.m_extern_crates.at(spath.crate_name()).m_hir->m_root_module;
                // Import of the crate root
                if (spath.components().size() == 0) {
                    pb.binding = ::AST::PathBinding_Type::make_Module({nullptr, {nullptr, hmod}});
                    _add_item(sp, dst_mod, IndexName::Namespace, it.first, vis, ::AST::Path(pb), false);
                    continue;
                }
                for (unsigned int i = 0; i < spath.components().size() - 1; i++) {
                    const auto& hit = hmod->m_mod_items.at(spath.components()[i]);
                    // Only support enums on the penultimate component
                    if (i == spath.components().size() - 2 && hit->ent.is_Enum()) {
                        pb.binding = ::AST::PathBinding_Type::make_EnumVar({nullptr, 0});
                        _add_item_type(sp, dst_mod, it.first, vis, mv$(pb), false);
                        hmod = nullptr;
                        break;
                    }
                    ASSERT_BUG(sp, hit->ent.is_Module(), "Path component " << spath.components()[i] << " of " << spath << " is not a module, instead " << hit->ent.tag_str());
                    hmod = &hit->ent.as_Module();
                }
                if (!hmod) {
                    continue;
                }
                vep = &hmod->m_mod_items.at(spath.components().back())->ent;
            } else {
                pb.path = mod_ap + it.first;
            }
            TU_MATCH_HDRA( (*vep), {)
            TU_ARMA(Import, e) {
                    //throw "";
                    TODO(sp, "Get binding for HIR import? " << e.path);
                }
                TU_ARMA(Module, e) {
                    pb.binding = ::AST::PathBinding_Type::make_Module({nullptr, {nullptr, &e}});
                }
                TU_ARMA(Trait, e) {
                    pb.binding = ::AST::PathBinding_Type::make_Trait({nullptr, &e});
                }
                TU_ARMA(Struct, e) {
                    pb.binding = ::AST::PathBinding_Type::make_Struct({nullptr, &e});
                }
                TU_ARMA(TraitAlias, e) {
                    pb.binding = ::AST::PathBinding_Type::make_TraitAlias({nullptr, &e});
                }
                TU_ARMA(Union, e) {
                    pb.binding = ::AST::PathBinding_Type::make_Union({nullptr, &e});
                }
                TU_ARMA(Enum, e) {
                    pb.binding = ::AST::PathBinding_Type::make_Enum({nullptr});
                }
                TU_ARMA(TypeAlias, e) {
                    pb.binding = ::AST::PathBinding_Type::make_TypeAlias({nullptr});
                }
                TU_ARMA(ExternType, e) {
                    pb.binding = ::AST::PathBinding_Type::make_TypeAlias({nullptr});
                }
            }
            _add_item_type( sp, dst_mod, it.first, vis, mv$(pb), false );
        }
    }
    for (const auto& it : hmod.m_value_items) {
        const auto& ve = *it.second;
        if (ve.publicity.is_global()) {
            const auto* vep = &ve.ent;

            ::AST::PathBinding<::AST::PathBinding_Value> pb;
            if (ve.ent.is_Import()) {
                const auto& spath = ve.ent.as_Import().path;
                pb.path.crate = spath.crate_name();
                pb.path.nodes = spath.components_vec();

                ASSERT_BUG(sp, crate.m_extern_crates.count(spath.crate_name()) == 1, "Crate " << spath.crate_name() << " is not loaded");
                const auto* hmod = &crate.m_extern_crates.at(spath.crate_name()).m_hir->m_root_module;
                for (unsigned int i = 0; i < spath.components().size() - 1; i++) {
                    const auto& hit = hmod->m_mod_items.at(spath.components()[i]);
                    if (i == spath.components().size() - 2 && hit->ent.is_Enum()) {
                        auto idx = hit->ent.as_Enum().find_variant(spath.components().back());
                        ASSERT_BUG(sp, idx != SIZE_MAX, spath);
                        pb.binding = ::AST::PathBinding_Value::make_EnumVar({nullptr, static_cast<unsigned>(idx)});
                        _add_item_value(sp, dst_mod, it.first, vis, mv$(pb), false);
                        hmod = nullptr;
                        break;
                    }
                    ASSERT_BUG(sp, hit->ent.is_Module(), "Path component " << spath.components()[i] << " of " << spath << " is not a module, instead " << hit->ent.tag_str());
                    hmod = &hit->ent.as_Module();
                }
                if (!hmod) {
                    continue;
                }
                vep = &hmod->m_value_items.at(spath.components().back())->ent;
            } else {
                pb.path = mod_ap + it.first;
            }
            assert(vep);
            TU_MATCH_HDRA( (*vep), {)
            TU_ARMA(Import, e) {
                    throw "";
                }
                TU_ARMA(Constant, e) {
                    pb.binding = ::AST::PathBinding_Value::make_Static({nullptr});
                }
                TU_ARMA(Static, e) {
                    pb.binding = ::AST::PathBinding_Value::make_Static({nullptr});
                }
                // TODO: What if these refer to an enum variant?
                TU_ARMA(StructConstant, e) {
                    pb.binding = ::AST::PathBinding_Value::make_Struct({nullptr, &crate.m_extern_crates.at(e.ty.crate_name()).m_hir->get_typeitem_by_path(sp, e.ty, true).as_Struct()});
                }
                TU_ARMA(StructConstructor, e) {
                    pb.binding = ::AST::PathBinding_Value::make_Struct({nullptr, &crate.m_extern_crates.at(e.ty.crate_name()).m_hir->get_typeitem_by_path(sp, e.ty, true).as_Struct()});
                }
                TU_ARMA(Function, e) {
                    pb.binding = ::AST::PathBinding_Value::make_Function({nullptr});
                }
            }
            _add_item_value( sp, dst_mod, it.first, vis, mv$(pb), false );
        }
    }
    for (const auto& it : hmod.m_macro_items) {
        const auto& e = *it.second;
        if (e.publicity.is_global()) {
            ::AST::PathBinding<::AST::PathBinding_Macro> pb;
            if (const auto* ep = e.ent.opt_Import()) {
                pb.path.crate = ep->path.crate_name();
                pb.path.nodes = ep->path.components_vec();
                // NOTE: This shouldn't ever be pointing at an import, and no other handling needed
            } else {
                pb.path = mod_ap + it.first;
            }

            TU_MATCH_HDRA( (e.ent), {)
            TU_ARMA(Import, _) {
                    pb.binding = ::AST::PathBinding_Macro::make_MacroRules({nullptr, nullptr});
                }
                TU_ARMA(ProcMacro, me) {
                    pb.binding = ::AST::PathBinding_Macro::make_ProcMacro({nullptr, me.name});
                }
                TU_ARMA(MacroRules, me) {
                    pb.binding = ::AST::PathBinding_Macro::make_MacroRules({nullptr, &*me});
                }
            }
            _add_item(sp, dst_mod, IndexName::Macro, it.first, vis, mv$(pb), false );
        }
    }
}

void Resolve_Index_Module_Wildcard__submod(AST::Crate& crate, AST::Module& dst_mod, const AST::Module& src_mod, const AST::Visibility& dst_vis) {
    static Span sp;
    TRACE_FUNCTION_F(dst_mod.path() << " <= " << src_mod.path());
    static ::std::set<const AST::Module*> stack;
    if (!stack.insert(&src_mod).second) {
        DEBUG("- Avoided recursion");
        return;
    }

    for (const auto& vi : src_mod.m_namespace_items) {
        if (vi.second.vis.is_visible(dst_mod.path() /*, src_mod.path()*/)) {
            _add_item(sp, dst_mod, IndexName::Namespace, vi.first, dst_vis, vi.second.path, false);
        }
    }
    for (const auto& vi : src_mod.m_type_items) {
        if (vi.second.vis.is_visible(dst_mod.path() /*, src_mod.path()*/)) {
            _add_item(sp, dst_mod, IndexName::Type, vi.first, dst_vis, vi.second.path, false);
        }
    }
    for (const auto& vi : src_mod.m_value_items) {
        if (vi.second.vis.is_visible(dst_mod.path() /*, src_mod.path()*/)) {
            _add_item(sp, dst_mod, IndexName::Value, vi.first, dst_vis, vi.second.path, false);
        }
    }
    for (const auto& vi : src_mod.m_macro_items) {
        if (vi.second.vis.is_visible(dst_mod.path() /*, src_mod.path()*/)) {
            _add_item(sp, dst_mod, IndexName::Macro, vi.first, dst_vis, vi.second.path, false);
        }
    }

    if (src_mod.m_index_populated != 2) {
        for (const auto& i : src_mod.m_items) {
            if (!i->data.is_Use()) {
                continue;
            }
            if (!i->vis.is_visible(dst_mod.path() /*, src_mod.path()*/)) {
                continue;
            }
            for (const auto& e : i->data.as_Use().entries) {
                if (e.name != "") {
                    continue;
                }
                Resolve_Index_Module_Wildcard__use_stmt(crate, dst_mod, e, dst_vis);
            }
        }
    }

    stack.erase(&src_mod);
}

void Resolve_Index_Module_Wildcard__use_stmt(AST::Crate& crate, AST::Module& dst_mod, const AST::UseItem::Ent& i_data, const AST::Visibility& vis) {
    const auto& sp = i_data.sp;
    const auto& b = i_data.path.m_bindings.type;

    if (const auto* e = b.binding.opt_Crate()) {
        DEBUG("Glob crate " << i_data.path);
        const auto& hmod = e->crate_->m_hir->m_root_module;
        Resolve_Index_Module_Wildcard__glob_in_hir_mod(sp, crate, dst_mod, hmod, i_data.path, vis, b.path);
    } else if (const auto* e = b.binding.opt_Module()) {
        DEBUG("Glob mod " << i_data.path);
        if (!e->module_) {
            ASSERT_BUG(sp, e->hir.mod, "Glob import where HIR module pointer not set - " << i_data.path);
            const auto& hmod = *e->hir.mod;
            Resolve_Index_Module_Wildcard__glob_in_hir_mod(sp, crate, dst_mod, hmod, i_data.path, vis, b.path);
        } else {
            Resolve_Index_Module_Wildcard__submod(crate, dst_mod, *e->module_, vis);
        }
    } else if (const auto* ep = b.binding.opt_Enum()) {
        const auto& e = *ep;
        ASSERT_BUG(sp, e.enum_ || e.hir, "Glob import but enum pointer not set - " << i_data.path);
        if (e.enum_) {
            DEBUG("Glob enum " << i_data.path << " (AST)");
            unsigned int idx = 0;
            for (const auto& ev : e.enum_->variants()) {
                if (ev.m_data.is_Struct()) {
                    AST::PathBinding<AST::PathBinding_Type> pb;
                    pb.path = b.path + ev.m_name;
                    pb.binding = ::AST::PathBinding_Type::make_EnumVar({e.enum_, idx});
                    _add_item_type(sp, dst_mod, ev.m_name, vis, mv$(pb), false);
                } else {
                    AST::PathBinding<AST::PathBinding_Value> pb;
                    pb.path = b.path + ev.m_name;
                    pb.binding = ::AST::PathBinding_Value::make_EnumVar({e.enum_, idx});
                    _add_item_value(sp, dst_mod, ev.m_name, vis, mv$(pb), false);
                }

                idx += 1;
            }
        } else {
            DEBUG("Glob enum " << i_data.path << " (HIR)");
            unsigned int idx = 0;
            if (e.hir->m_data.is_Value()) {
                const auto* de = e.hir->m_data.opt_Value();
                for (const auto& ev : de->variants) {
                    AST::PathBinding<AST::PathBinding_Value> pb;
                    pb.path = b.path + ev.name;
                    pb.binding = ::AST::PathBinding_Value::make_EnumVar({nullptr, idx, e.hir});
                    _add_item_value(sp, dst_mod, ev.name, vis, mv$(pb), false);

                    idx += 1;
                }
            } else {
                const auto* de = &e.hir->m_data.as_Data();
                for (const auto& ev : *de) {
                    if (ev.is_struct) {
                        AST::PathBinding<AST::PathBinding_Type> pb;
                        pb.path = b.path + ev.name;
                        pb.binding = ::AST::PathBinding_Type::make_EnumVar({nullptr, idx, e.hir});
                        _add_item_type(sp, dst_mod, ev.name, vis, mv$(pb), false);
                    } else {
                        AST::PathBinding<AST::PathBinding_Value> pb;
                        pb.path = b.path + ev.name;
                        pb.binding = ::AST::PathBinding_Value::make_EnumVar({nullptr, idx, e.hir});
                        _add_item_value(sp, dst_mod, ev.name, vis, mv$(pb), false);
                    }

                    idx += 1;
                }
            }
        }
    } else {
        BUG(sp, "Invalid path binding for glob import: " << b.binding.tag_str() << " - " << i_data.path);
    }
}

// Wildcard (aka glob) import resolution
//
// Strategy:
// - HIR just imports the items
// - Enums import all variants
// - AST modules: (See Resolve_Index_Module_Wildcard__submod)
//  - Clone index in (marked as publicity and weak)
//  - Recurse into globs
void Resolve_Index_Module_Wildcard(AST::Crate& crate, AST::Module& mod) {
    TRACE_FUNCTION_F("mod = " << mod.path());
    for (const auto& i : mod.m_items) {
        if (!i->data.is_Use()) {
            continue;
        }
        for (const auto& e : i->data.as_Use().entries) {
            if (e.name != "") {
                continue;
            }
            Resolve_Index_Module_Wildcard__use_stmt(crate, mod, e, i->vis);
        }
    }

    // Mark this as having all the items it ever will.
    mod.m_index_populated = 2;

    // Handle child modules
    for (auto& i : mod.m_items) {
        if (auto* e = i->data.opt_Module()) {
            Resolve_Index_Module_Wildcard(crate, *e);
        }
    }
    for (auto& mp : mod.anon_mods()) {
        if (mp) {
            Resolve_Index_Module_Wildcard(crate, *mp);
        }
    }
}

void Resolve_Index_Module_Normalise_Path_ext(const ::AST::Crate& crate, const Span& sp, ::AST::Path& path, IndexName loc, const ::AST::ExternCrate& ext, unsigned int start) {
    auto& info = path.m_class.as_Absolute();
    const ::HIR::Module* hmod = &ext.m_hir->m_root_module;

    // TODO: Mangle path into being absolute into the crate
    //info.crate = ext.m_name;
    //do {
    //    path.nodes().erase( path.nodes().begin() + i );
    //} while( --i > 0 );

    info.crate = ext.m_name;
    info.nodes.erase(info.nodes.begin(), info.nodes.begin() + start);

    if (info.nodes.empty()) {
        return;
    }

    for (unsigned int i = 0; i < info.nodes.size() - 1; i++) {
        auto it = hmod->m_mod_items.find(info.nodes[i].name());
        if (it == hmod->m_mod_items.end()) {
            ERROR(sp, E0000, "Couldn't find node " << i << " of path " << path);
        }
        const auto* item_ptr = &it->second->ent;
        if (item_ptr->is_Import()) {
            const auto& e = item_ptr->as_Import();
            const auto& ec = crate.m_extern_crates.at(e.path.crate_name());

            // TODO: Update the path (and update `i` while there)

            if (e.path.components().empty()) {
                hmod = &ec.m_hir->m_root_module;
                continue;
            }
            item_ptr = &ec.m_hir->get_typeitem_by_path(sp, e.path, /*ignore_crate_name=*/true);
        }
        TU_MATCH_DEF(
            ::HIR::TypeItem,
            (*item_ptr),
            (e),
            (BUG(sp, "Path " << path << " pointed to non-module in component " << i);),
            (Import, BUG(sp, "Recursive import in " << path << " - " << it->second->ent.as_Import().path << " -> " << e.path);),
            (Enum,
             if (i != info.nodes.size() - 2) { BUG(sp, "Path " << path << " pointed to non-module in component " << i); }
             // Lazy, not checking
             return;),
            (Module, hmod = &e;)
        )
    }
    const auto& lastnode = info.nodes.back();

    switch (loc) {
        case IndexName::Type:
        case IndexName::Namespace: {
            auto it_m = hmod->m_mod_items.find(lastnode.name());
            if (it_m != hmod->m_mod_items.end()) {
                TU_IFLET(
                    ::HIR::TypeItem,
                    it_m->second->ent,
                    Import,
                    e,
                    // Replace the path with this path (maintaining binding)
                    auto bindings = path.m_bindings.clone();
                    path = hir_to_ast(e.path);
                    path.m_bindings = mv$(bindings);
                )
                return;
            }
        } break;
        case IndexName::Value: {
            auto it_v = hmod->m_value_items.find(lastnode.name());
            if (it_v != hmod->m_value_items.end()) {
                TU_IFLET(
                    ::HIR::ValueItem,
                    it_v->second->ent,
                    Import,
                    e,
                    // Replace the path with this path (maintaining binding)
                    auto bindings = path.m_bindings.clone();
                    path = hir_to_ast(e.path);
                    path.m_bindings = mv$(bindings);
                )
                return;
            }
        } break;
        case IndexName::Macro: {
            auto it_v = hmod->m_macro_items.find(lastnode.name());
            if (it_v != hmod->m_macro_items.end()) {
                if (const auto* e = it_v->second->ent.opt_Import()) {
                    // Replace the path with this path (maintaining binding)
                    auto bindings = path.m_bindings.clone();
                    path = hir_to_ast(e->path);
                    path.m_bindings = mv$(bindings);
                }
                return;
            }
        } break;
    }

    ERROR(sp, E0000, "Couldn't find final node of path " << path);
}

// Returns true if a change was made
bool Resolve_Index_Module_Normalise_Path(const ::AST::Crate& crate, const Span& sp, ::AST::Path& path, IndexName loc) {
    const auto& info = path.m_class.as_Absolute();
    if (info.crate != "") {
        if (info.crate == CRATE_BUILTINS) {
            //TODO(sp, "Normalise builtin paths");
            return false;
        }
        Resolve_Index_Module_Normalise_Path_ext(crate, sp, path, loc, crate.m_extern_crates.at(info.crate), 0);
        return false;
    }
    if (info.nodes.empty()) {
        return false;
    }

    const ::AST::Module* mod = &crate.m_root_module;
    ASSERT_BUG(sp, info.nodes.size() > 0, "Empty node list in " << path);
    for (unsigned int i = 0; i < info.nodes.size() - 1; i++) {
        const auto& node = info.nodes[i];

        auto it = mod->m_namespace_items.find(node.name());
        if (it == mod->m_namespace_items.end()) {
            ERROR(sp, E0000, "Couldn't find node " << i << " of path " << path);
        }
        const auto& ie = it->second;

        if (ie.is_import) {
            // Need to replace all nodes up to and including the current with the import path
            auto new_path = ie.path;
            for (unsigned int j = i + 1; j < info.nodes.size(); j++) {
                new_path.nodes().push_back(mv$(info.nodes[j]));
            }
            new_path.m_bindings = path.m_bindings.clone();
            path = mv$(new_path);
            return Resolve_Index_Module_Normalise_Path(crate, sp, path, loc);
        } else {
            TU_MATCH_HDRA( (ie.path.m_bindings.type.binding), {)
            default:
                BUG(sp, "Path " << path << " pointed to non-module " << ie.path);
                TU_ARMA(Module, e) {
                    mod = e.module_;
                }
                TU_ARMA(Crate, e) {
                    Resolve_Index_Module_Normalise_Path_ext(crate, sp, path, loc, *e.crate_, i + 1);
                    return false;
                }
                TU_ARMA(Enum, e) {
                    // NOTE: Just assuming that if an Enum is hit, it's sane
                    return false;
                }
            }
        }
    }

    const auto& node = info.nodes.back();

    // TODO: Use get_mod_index instead.
    const ::AST::Module::IndexEnt* ie_p = nullptr;
    switch (loc) {
        case IndexName::Namespace: {
            auto it = mod->m_namespace_items.find(node.name());
            if (it != mod->m_namespace_items.end()) {
                ie_p = &it->second;
            }
        } break;
        case IndexName::Value: {
            auto it = mod->m_value_items.find(node.name());
            if (it != mod->m_value_items.end()) {
                ie_p = &it->second;
            }
        } break;
        case IndexName::Type: {
            auto it = mod->m_type_items.find(node.name());
            if (it != mod->m_type_items.end()) {
                ie_p = &it->second;
            }
        } break;
        case IndexName::Macro: {
            auto it = mod->m_macro_items.find(node.name());
            if (it != mod->m_macro_items.end()) {
                ie_p = &it->second;
            } else {
                // Workaround for `use` on an exporter macro
                const AST::Module::MacroImport* found = nullptr;
                for (const auto& a : mod->m_macro_imports) {
                    //DEBUG("MI " << a.name << " = " << a.ref.tag_str() << " " << a.path);
                    if (a.name == node.name()) {
                        found = &a;
                    }
                }
                if (found && found->ref.is_MacroRules()) {
                    DEBUG("in " << mod->path() << " " << node.name() << " imported using: " << path << " = " << found->path);
                    assert(path != found->path);
                    path = found->path;
                    path.m_bindings.macro.set(found->path, AST::PathBinding_Macro::make_MacroRules({nullptr, found->ref.as_MacroRules()}));
                    DEBUG("macro_export? -> " << path);
                    Resolve_Index_Module_Normalise_Path(crate, sp, path, loc);
                    return true;
                }
            }
        } break;
    }
    if (!ie_p) {
        DEBUG("Was in " << mod->path());
        ERROR(sp, E0000, "Couldn't find final node of path " << path);
    }
    const auto& ie = *ie_p;

    if (ie.is_import) {
        // TODO: Prevent infinite recursion if the user does something dumb
        path = ::AST::Path(ie.path);
        Resolve_Index_Module_Normalise_Path(crate, sp, path, loc);
        return true;
    } else {
        // All good
        return false;
    }
}

void Resolve_Index_Module_Normalise(const ::AST::Crate& crate, const Span& mod_span, ::AST::Module& mod) {
    TRACE_FUNCTION_F("mod = " << mod.path());
    for (auto& item : mod.m_items) {
        if (auto* e = item->data.opt_Module()) {
            Resolve_Index_Module_Normalise(crate, item->span, *e);
        }
    }

    DEBUG("Index for " << mod.path());
    for (auto& ent : mod.m_namespace_items) {
        Resolve_Index_Module_Normalise_Path(crate, mod_span, ent.second.path, IndexName::Namespace);
        DEBUG("NS " << ent.first << " = " << ent.second.path);
    }
    for (auto& ent : mod.m_type_items) {
        Resolve_Index_Module_Normalise_Path(crate, mod_span, ent.second.path, IndexName::Type);
        DEBUG("Ty " << ent.first << " = " << ent.second.path);
    }
    for (auto& ent : mod.m_value_items) {
        Resolve_Index_Module_Normalise_Path(crate, mod_span, ent.second.path, IndexName::Value);
        DEBUG("Val " << ent.first << " = " << ent.second.path);
    }
    for (auto& ent : mod.m_macro_items) {
        Resolve_Index_Module_Normalise_Path(crate, mod_span, ent.second.path, IndexName::Macro);
        DEBUG("Macro " << ent.first << " = " << ent.second.path);
    }
}

void Resolve_Index_Module_ExportedMacros(::AST::Crate& crate, const Span& mod_span, ::AST::Module& mod) {
    TRACE_FUNCTION_F("mod = " << mod.path());

    if (&mod != &crate.m_root_module) {
        for (const auto& item : mod.macros()) {
            if (item.data->m_exported) {
                ASSERT_BUG(item.span, mod.m_macro_items.count(item.name), "Missing " << item.name << " in " << mod.path());
                _add_item(item.span, crate.m_root_module, IndexName::Macro, item.name, AST::Visibility::make_global(), mod.m_macro_items.at(item.name).path);
            }
        }
    }

    for (auto& item : mod.m_items) {
        if (auto* e = item->data.opt_Module()) {
            Resolve_Index_Module_ExportedMacros(crate, item->span, *e);
        }
    }
}

void Resolve_Index(AST::Crate& crate) {
    // - Index all explicitly named items
    Resolve_Index_Module_Base(crate, crate.m_root_module);
    // - Index wildcard imports
    Resolve_Index_Module_Wildcard(crate, crate.m_root_module);

    // Macros marked with `#[macro_export]` actually live in the root
    Resolve_Index_Module_ExportedMacros(crate, Span(), crate.m_root_module);

    // - Normalise the index (ensuring all paths point directly to the item)
    Resolve_Index_Module_Normalise(crate, Span(), crate.m_root_module);
}

#include "main_bindings.h"
#include "ast_crate.h"
#include "ast_ast.h"
#include "ast_expr.h"
#include "hir_hir.h"
#include "stdspan.h" // std::span
#include "pop_on_drop.h"

enum class Lookup {
    Any,    // Allow binding to anything
    AnyOpt, // Allow binding to anything, but don't error on failure
    Type,   // Allow only type bindings
    Value,  // Allow only value bindings
};

namespace {
    const RcString rcstring_crate_builtins = RcString::new_interned(CRATE_BUILTINS);
}

void Resolve_Use_Mod(const ::AST::Crate& crate, ::AST::Module& mod, ::AST::Path path, ::std::span<const ::AST::Module*> parent_modules = {});
::AST::Path::Bindings Resolve_Use_GetBinding(const Span& span, const ::AST::Crate& crate, const ::AST::AbsolutePath& source_mod_path, const ::AST::Path& path, ::std::span<const ::AST::Module*> parent_modules, bool types_only = false, bool soft_fail = false);

::AST::Path::Bindings Resolve_Use_GetBinding_Mod(const Span& span, const ::AST::Crate& crate, const ::AST::AbsolutePath& source_mod_path, const ::AST::Module& mod, const RcString& des_item_name, ::std::span<const ::AST::Module*> parent_modules, bool types_only = false, bool require_visible = false);
::AST::Path::Bindings Resolve_Use_GetBinding__ext(const Span& span, const ::AST::Crate& crate, const AST::ExternCrate& ec, const ::HIR::Module& hmodr, const ::AST::Path& path, unsigned int start, AST::AbsolutePath ap = {});
::AST::Path::Bindings Resolve_Use_GetBinding__ext(const Span& span, const ::AST::Crate& crate, const ::AST::Path& path, const AST::ExternCrate& ec, unsigned int start);

void Resolve_Use(::AST::Crate& crate) {
    Resolve_Use_Mod(crate, crate.m_root_module, ::AST::Path("", {}));
}

// - Convert self::/super:: paths into non-canonical absolute forms
::AST::Path Resolve_Use_AbsolutisePath(const Span& span, const AST::Crate& crate, const ::AST::Path& base_path, ::AST::Path path) {
    TU_MATCH_HDRA( (path.m_class), {)
    TU_ARMA(Invalid, e) {
            // Should never happen
            BUG(span, "Invalid path class encountered");
        }
        TU_ARMA(Local, e) {
            // Wait, how is this already known?
            BUG(span, "Local path class in use statement");
        }
        TU_ARMA(UFCS, e) {
            // Wait, how is this already known?
            BUG(span, "UFCS path class in use statement");
        }
        TU_ARMA(Relative, e) {
            // How can this happen?
            DEBUG("Relative " << path);

            // 2018 edition and later: all extern crates are implicitly in the namespace.
            // - Non-use paths use the extern prelude too, while use paths remain edition-sensitive.
            if (crate.m_edition >= AST::Edition::Rust2018) {
                const auto& name = e.nodes.at(0).name();
                auto ec_it = AST::g_implicit_crates.find(name);
                if (ec_it != AST::g_implicit_crates.end()) {
                    DEBUG("Found implict crate " << name);
                    e.nodes.erase(e.nodes.begin());
                    return AST::Path(ec_it->second, e.nodes);
                } else {
                    DEBUG("No implicit crate " << name);
                }
            }

            // If there's only one node, then check for primitives.
            if (path.nodes().size() == 1) {
                auto ct = coretype_fromstring(path.nodes()[0].name().c_str());
                if (ct != CORETYPE_INVAL) {
                    DEBUG("Found builtin type for `use` - " << path);
                    // TODO: only if the item doesn't already exist?
                    AST::Path rv{rcstring_crate_builtins, path.nodes()};
                    rv.m_bindings.type.set(AST::AbsolutePath(rcstring_crate_builtins, {path.nodes().back().name()}), {});
                    return rv;
                }
            }

            // EVIL HACK: If the current module is an anon module, refer to the parent
            // TODO: Check if the desired item is in this module,
            if (base_path.nodes().size() > 0 && base_path.nodes().back().name().c_str()[0] == '#') {
                std::vector<const AST::Module*> parent_mods;
                const AST::Module* cur_mod = &crate.m_root_module;
                parent_mods.push_back(cur_mod);
                // Walk the path to create a list of parent modules
                // - Resets the list every time there's a non-anon module
                for (unsigned int i = 0; i < base_path.nodes().size(); i++) {
                    const auto& name = base_path.nodes()[i].name();
                    const AST::Module* next_mod = nullptr;

                    // If the desired item is an anon module (starts with #) then parse and index
                    if (name.size() > 0 && name.c_str()[0] == '#') {
                        unsigned int idx = 0;
                        if (::std::sscanf(name.c_str(), "#%u", &idx) != 1) {
                            BUG(span, "Invalid anon path segment '" << name << "'");
                        }
                        ASSERT_BUG(span, idx < cur_mod->anon_mods().size(), "Invalid anon path segment '" << name << "'");
                        assert(cur_mod->anon_mods()[idx]);
                        next_mod = &*cur_mod->anon_mods()[idx];
                    } else {
                        for (const auto& item : cur_mod->m_items) {
                            if (item->name == name && item->data.is_Module()) {
                                next_mod = &item->data.as_Module();
                                break;
                            }
                        }
                        ASSERT_BUG(span, next_mod, "Could not find module '" << name << "' in " << cur_mod->path());
                    }
                    cur_mod = next_mod;
                    if (name.c_str()[0] != '#') {
                        parent_mods.clear();
                    }
                    parent_mods.push_back(cur_mod);
                }
                parent_mods.pop_back();
                DEBUG("parent_mods.size() = " << parent_mods.size());
                ASSERT_BUG(span, !parent_mods.empty(), "Anon module with no named parent");
                const AST::Module* source_mod = parent_mods.front();

                for (;;) {
                    DEBUG("Module " << cur_mod->path());
                    if (Resolve_Use_GetBinding_Mod(span, crate, source_mod->path(), *cur_mod, e.nodes.front().name(), parent_mods, /*types_only*/ e.nodes.size() > 1).has_binding()) {
                        break;
                    }
                    if (parent_mods.empty()) {
                        ERROR(span, E0000, "Unable to find " << e.nodes.front().name());
                    }
                    cur_mod = parent_mods.back();
                    parent_mods.pop_back();
                }
                DEBUG("Found item in " << cur_mod->path());

                AST::Path np("", {});
                for (unsigned int i = 0; i < cur_mod->path().nodes.size(); i++) {
                    np.nodes().push_back(cur_mod->path().nodes[i]);
                }
                np += path;
                return np;
            } else {
                return base_path + path;
            }
        }
        TU_ARMA(Self, e) {
            DEBUG("Self " << path);
            // EVIL HACK: If the current module is an anon module, refer to the parent
            if (base_path.nodes().size() > 0 && base_path.nodes().back().name().c_str()[0] == '#') {
                AST::Path np("", {});
                for (unsigned int i = 0; i < base_path.nodes().size() - 1; i++) {
                    np.nodes().push_back(base_path.nodes()[i]);
                }
                np += path;
                return np;
            } else {
                return base_path + path;
            }
        }
        TU_ARMA(Super, e) {
            DEBUG("Super " << path);
            assert(e.count >= 1);
            AST::Path np("", {});
            if (e.count > base_path.nodes().size()) {
                ERROR(span, E0000, "Too many `super` components");
            }
            // TODO: Do this in a cleaner manner.
            unsigned int n_anon = 0;
            // Skip any anon modules in the way (i.e. if the current module is an anon, go to the parent)
            while (base_path.nodes().size() > n_anon && base_path.nodes()[base_path.nodes().size() - 1 - n_anon].name().c_str()[0] == '#') {
                n_anon++;
            }
            for (unsigned int i = 0; i < base_path.nodes().size() - e.count - n_anon; i++) {
                np.nodes().push_back(base_path.nodes()[i]);
            }
            np += path;
            return np;
        }
        TU_ARMA(Absolute, e) {
            DEBUG("Absolute " << path);
            // HACK: if the crate name starts with `=` it's a 2018 absolute path (references a crate loaded with `--extern`)
            if (crate.m_edition >= AST::Edition::Rust2018 && e.crate.c_str()[0] == '=') {
                // Absolute paths in 2018 edition are crate-prefixed?
                auto ec_it = AST::g_implicit_crates.find(e.crate.c_str() + 1);
                if (ec_it == AST::g_implicit_crates.end()) {
                    ERROR(span, E0000, "Unable to find external crate for path " << path);
                }
                e.crate = ec_it->second;
            }
            // Leave as is
            return path;
        }
    }
    throw "BUG: Reached end of Resolve_Use_AbsolutisePath";
}

void Resolve_Use_Mod(const ::AST::Crate& crate, ::AST::Module& mod, ::AST::Path path, ::std::span<const ::AST::Module*> parent_modules) {
    TRACE_FUNCTION_F("path = " << path);

    for (auto& use_stmt : mod.m_items) {
        if (!use_stmt->data.is_Use()) {
            continue;
        }
        auto& use_stmt_data = use_stmt->data.as_Use();

        const Span& span = use_stmt_data.sp;
        for (auto& use_ent : use_stmt_data.entries) {
            TRACE_FUNCTION_F(use_ent);

            use_ent.path = Resolve_Use_AbsolutisePath(span, crate, path, use_ent.path);
            if (!use_ent.path.m_class.is_Absolute()) {
                BUG(span, "Use path is not absolute after absolutisation");
            }

            // NOTE: Use statements can refer to _three_ different items
            // - types/modules ("type namespace")
            // - values ("value namespace")
            // - macros ("macro namespace")
            // TODO: Have Resolve_Use_GetBinding return the actual path
            use_ent.path.m_bindings = Resolve_Use_GetBinding(span, crate, mod.path(), use_ent.path, parent_modules);
            if (!use_ent.path.m_bindings.has_binding()) {
                ERROR(span, E0000, "Unable to resolve `use` target " << use_ent.path);
            }
            DEBUG("'" << use_ent.name << "' = " << use_ent.path);

            // - If doing a glob, ensure the item type is valid
            if (use_ent.name == "") {
                TU_MATCH_DEF(::AST::PathBinding_Type, (use_ent.path.m_bindings.type.binding), (e), (ERROR(span, E0000, "Wildcard import of invalid item type - " << use_ent.path);), (Enum, ), (Crate, ), (Module, ))
            } else {
            }
        }
    }

    struct NV: public AST::NodeVisitorDef {
        const AST::Crate& crate;
        ::std::vector<const AST::Module*> parent_modules;

        NV(const AST::Crate& crate, const AST::Module& cur_module, ::std::span<const AST::Module*> parent_modules)
            : crate(crate)
            , parent_modules(parent_modules.begin(), parent_modules.end())
        {
            this->parent_modules.push_back(&cur_module);
        }

        void visit(AST::ExprNode_Block& node) override {
            if (node.m_local_mod) {
                Resolve_Use_Mod(this->crate, *node.m_local_mod, node.m_local_mod->path(), this->parent_modules);

                parent_modules.push_back(&*node.m_local_mod);
            }
            AST::NodeVisitorDef::visit(node);
            if (node.m_local_mod) {
                parent_modules.pop_back();
            }
        }
    } expr_iter(crate, mod, parent_modules);

    // TODO: Check that all code blocks are covered by these
    // - NOTE: Handle anon modules by iterating code (allowing correct item mappings)
    for (auto& ip : mod.m_items) {
        auto& i = *ip;
        TU_MATCH_HDRA( (i.data),  {)
        default:
            break;
            TU_ARMA(Module, e) {
                Resolve_Use_Mod(crate, e, path + i.name);
            }
            TU_ARMA(Impl, e) {
                for (auto& i : e.items()) {
                    TU_MATCH_DEF(AST::Item, (*i.data), (e), (), (Function, if (e.code().is_valid()) { e.code().node().visit(expr_iter); }), (Static, if (e.value().is_valid()) { e.value().node().visit(expr_iter); }))
                }
            }
            TU_ARMA(Trait, e) {
                for (auto& ti : e.items()) {
                    TU_MATCH_DEF(
                        AST::Item,
                        (ti.data),
                        (e),
                        (BUG(Span(), "Unexpected item in trait - " << ti.data.tag_str());),
                        (
                            None,
                            // Deleted, ignore
                        ),
                        (
                            MacroInv,
                            // TODO: Should this already be deleted?
                        ),
                        (Type, ),
                        (Function, if (e.code().is_valid()) { e.code().node().visit(expr_iter); }),
                        (Static, if (e.value().is_valid()) { e.value().node().visit(expr_iter); })
                    )
                }
            }
            TU_ARMA(Function, e) {
                if (e.code().is_valid()) {
                    e.code().node().visit(expr_iter);
                }
            }
            TU_ARMA(Static, e) {
                if (e.value().is_valid()) {
                    e.value().node().visit(expr_iter);
                }
            }
        }
    }
}

::AST::Path::Bindings Resolve_Use_GetBinding_Mod(
    const Span& span,
    const ::AST::Crate& crate,
    const ::AST::AbsolutePath& source_mod_path,
    const ::AST::Module& mod,
    const RcString& des_item_name,
    ::std::span<const ::AST::Module*> parent_modules,
    bool types_only,     // = false
    bool require_visible // = false
) {
    ::AST::Path::Bindings rv;
    TRACE_FUNCTION_F(mod.path() << ", des_item_name=" << des_item_name);

    static ::std::vector<std::pair<const AST::Module*, const char*>> s_recurse_stack;
    auto recurse_ent = std::make_pair(&mod, des_item_name.c_str());
    // EVIL: Allow a single recursion before returning empty
    if (std::count(s_recurse_stack.begin(), s_recurse_stack.end(), recurse_ent) > 1) {
        DEBUG("Recursion detected, returning empty bindings");
        return rv;
    }
    auto _ = push_and_pop_at_end(s_recurse_stack, recurse_ent);

    // TODO: Catch and prevent recursion?
    // If the desired item is an anon module (starts with #) then parse and index
    if (des_item_name.size() > 0 && des_item_name.c_str()[0] == '#') {
        unsigned int idx = 0;
        if (::std::sscanf(des_item_name.c_str(), "#%u", &idx) != 1) {
            BUG(span, "Invalid anon path segment '" << des_item_name << "'");
        }
        ASSERT_BUG(span, idx < mod.anon_mods().size(), "Invalid anon path segment '" << des_item_name << "'");
        assert(mod.anon_mods()[idx]);
        const auto& m = *mod.anon_mods()[idx];
        rv.type.set(m.path(), ::AST::PathBinding_Type::make_Module({&m, {nullptr}}));
        return rv;
    }

    // Seach for the name defined in the module.
    for (const auto& ip : mod.m_items) {
        const auto& item = *ip;
        if (item.data.is_None()) {
            continue;
        }
        // When reached through a glob import, private items aren't re-exported (usvg's
        // crate-root `pub use parser::*` must not expose the private `parser::filter`).
        if (require_visible && !item.vis.is_visible(source_mod_path)) {
            continue;
        }

        if (item.name == des_item_name) {
            auto p = mod.path() + item.name;
            DEBUG("Matching item: " << item.data.tag_str());
            TU_MATCH_HDRA( (item.data), {)
            TU_ARMA(None, _e) {
                    // IMPOSSIBLE - Handled above
                }
                TU_ARMA(MacroInv, e) {
                    BUG(span, "Hit MacroInv in use resolution");
                }
                TU_ARMA(GlobalAsm, e) {
                    BUG(span, "Hit GlobalAsm in use resolution");
                }
                TU_ARMA(Macro, e) {
                    //rv.macro = ::AST::PathBinding_Macro::make_MacroRules({nullptr, e.get()});
                }
                TU_ARMA(Use, e) {
                    continue; // Skip for now
                }
                TU_ARMA(Impl, e) {
                    BUG(span, "Hit Impl in use resolution");
                }
                TU_ARMA(NegImpl, e) {
                    BUG(span, "Hit NegImpl in use resolution");
                }
                TU_ARMA(ExternBlock, e) {
                    BUG(span, "Hit Extern in use resolution");
                }
                TU_ARMA(Crate, e) {
                    if (!rv.type.is_Unbound()) {
                        // This is a hack for when a crate defines a module with the name `std` (or `core` with `#![no_std]`)
                        DEBUG("Ignore, already bound");
                    } else if (e.name != "") {
                        ASSERT_BUG(span, crate.m_extern_crates.count(e.name), "Crate '" << e.name << "' not loaded");
                        rv.type.set(AST::AbsolutePath(e.name, {}), ::AST::PathBinding_Type::make_Crate({&crate.m_extern_crates.at(e.name)}));
                    } else {
                        rv.type.set(AST::AbsolutePath(e.name, {}), ::AST::PathBinding_Type::make_Module({&crate.m_root_module}));
                    }
                }
                TU_ARMA(Type, e) {
                    rv.type.set(p, ::AST::PathBinding_Type::make_TypeAlias({&e}));
                }
                TU_ARMA(Trait, e) {
                    rv.type.set(p, ::AST::PathBinding_Type::make_Trait({&e}));
                }
                TU_ARMA(TraitAlias, e) {
                    rv.type.set(p, ::AST::PathBinding_Type::make_TraitAlias({&e}));
                }

                TU_ARMA(Function, e) {
                    rv.value.set(p, ::AST::PathBinding_Value::make_Function({&e}));
                }
                TU_ARMA(Static, e) {
                    rv.value.set(p, ::AST::PathBinding_Value::make_Static({&e}));
                }
                TU_ARMA(Struct, e) {
                    // TODO: What happens with name collisions?
                    if (!e.m_data.is_Struct()) {
                        rv.value.set(p, ::AST::PathBinding_Value::make_Struct({&e}));
                    }
                    rv.type.set(p, ::AST::PathBinding_Type::make_Struct({&e}));
                }
                TU_ARMA(Enum, e) {
                    rv.type.set(p, ::AST::PathBinding_Type::make_Enum({&e}));
                }
                TU_ARMA(Union, e) {
                    rv.type.set(p, ::AST::PathBinding_Type::make_Union({&e}));
                }
                TU_ARMA(Module, e) {
                    rv.type.set(p, ::AST::PathBinding_Type::make_Module({&e}));
                }
            }
        }
    }
    for (const auto& mac : mod.macros()) {
        if (mac.name == des_item_name) {
            rv.macro.set(mod.path() + mac.name, ::AST::PathBinding_Macro::make_MacroRules({nullptr, &*mac.data}));
            DEBUG("Macro definition: " << rv.macro.path);
            break;
        }
    }
    // NOTE: `use macroname;` can refer to a macro already in-scope via a parent `#[macro_use]`
    // - So, need to look upwards
    // - Can't use `parent_modules`, as that's only for anon.
    if (rv.macro.is_Unbound()) {
        ::std::vector<const AST::Module*> mods;
        mods.push_back(&crate.root_module());
        for (size_t i = 0; i < mod.path().nodes.size(); i++) {
            const auto& n = mod.path().nodes[i];
            const AST::Module* nm = nullptr;
            if (n.c_str()[0] == '#') {
                // Lazy option: just enumerate and check, instead of parsing the index
                for (const auto& e : mods.back()->anon_mods()) {
                    if (e && e->path().nodes.back() == n) {
                        nm = &*e;
                        break;
                    }
                }
            } else {
                for (const auto& e : mods.back()->m_items) {
                    if (e->data.is_Module()) {
                        if (e->name == mod.path().nodes[i]) {
                            nm = &e->data.as_Module();
                        }
                    }
                }
            }
            ASSERT_BUG(span, nm, "Failed to find `" << n << " in " << mod.path());
            mods.push_back(nm);
        }
        for (size_t i = mods.size(); i--;) {
            const auto& check_mod = *mods[i];
            for (const auto& mac : check_mod.m_macro_imports) {
                if (mac.name == des_item_name) {
                    DEBUG("Macro Import - " << mac.path);
                    TU_MATCH_HDRA( (mac.ref), { )
                    TU_ARMA(None, e) {
                        }
                        TU_ARMA(MacroRules, e) {
                            rv.macro.set(mac.path, ::AST::PathBinding_Macro::make_MacroRules({nullptr, e}));
                        }
                        TU_ARMA(BuiltinProcMacro, e) {
                        }
                        TU_ARMA(ExternalProcMacro, e) {
                        }
                    }
                    if( ! rv.macro.is_Unbound() ) {
                        break;
                    }
                }
            }
            if (!rv.macro.is_Unbound()) {
                break;
            }
        }
    }
    // TODO: If target is the crate root AND the crate exports macros with `macro_export`
    if (rv.macro.is_Unbound() && &mod == &crate.m_root_module) {
        auto it = crate.m_exported_macros.find(des_item_name);
        if (it != crate.m_exported_macros.end()) {
            rv.macro.set(mod.path() + des_item_name, ::AST::PathBinding_Macro::make_MacroRules({nullptr, &*it->second}));
            DEBUG("Crate-exported macro - " << rv.macro.path);
        }
    }

    if (types_only && !rv.type.is_Unbound()) {
        return rv;
    }

    const bool can_see_private = false || mod.path().is_parent_of(source_mod_path) || (parent_modules.size() > 0 && parent_modules[0]->path().is_parent_of(source_mod_path));

    // Imports
    // - Explicitly named imports first (they take priority over anon imports)
    for (const auto& imp : mod.m_items) {
        if (!imp->data.is_Use()) {
            continue;
        }
        const auto& imp_data = imp->data.as_Use();
        for (const auto& imp_e : imp_data.entries) {
            const Span& sp2 = imp_e.sp;
            if (imp_e.name == des_item_name) {
                DEBUG("- Named import " << imp_e.name << " = " << imp_e.path);
                if (!(can_see_private || imp->vis.is_visible(source_mod_path /*, mod.path()*/))) {
                    DEBUG("Ignore private import");
                    continue;
                }
                if (!imp_e.path.m_bindings.has_binding()) {
                    DEBUG(" > Needs resolve p=" << &imp_e.path);
                    static ::std::vector<const ::AST::Path*> s_mods;
                    if (::std::find(s_mods.begin(), s_mods.end(), &imp_e.path) == s_mods.end()) {
                        s_mods.push_back(&imp_e.path);
                        rv.merge_from(Resolve_Use_GetBinding(sp2, crate, mod.path(), Resolve_Use_AbsolutisePath(sp2, crate, mod.path(), imp_e.path), parent_modules));
                        s_mods.pop_back();
                    } else {
                        DEBUG("Recursion on path " << &imp_e.path << " " << imp_e.path);
                    }
                } else {
                    //out_path = imp_e.path;
                    rv.merge_from(imp_e.path.m_bindings.clone());
                }
                continue;
            }
        }
    }

    for (const auto& imp : mod.m_items) {
        // A satisfied types-only lookup can stop before touching later globs; resolving them
        // here can hit the recursion limit and raise a spurious error (e.g. libc 0.2.189's
        // `new` module, where sibling globs re-export through an earlier glob import).
        if (types_only && !rv.type.is_Unbound()) {
            break;
        }
        if (!imp->data.is_Use()) {
            continue;
        }
        const auto& imp_data = imp->data.as_Use();
        for (const auto& imp_e : imp_data.entries) {
            const Span& sp2 = imp_e.sp;
            if (imp_e.name != "") {
                continue;
            }

            // TODO: Correct privacy rules (if the origin of this lookup can see this item)
            if ((can_see_private || imp->vis.is_visible(source_mod_path /*, mod.path()*/))) {
                DEBUG("- Search glob of " << imp_e.path << " in " << mod.path());
                // INEFFICIENT! Resolves and throws away the result (because we can't/shouldn't mutate here)
                ::AST::Path::Bindings bindings_;
                const auto* bindings = &imp_e.path.m_bindings;
                if (bindings->type.is_Unbound()) {
                    DEBUG("Temp resolving wildcard " << imp_e.path);
                    // Handle possibility of recursion
                    static ::std::vector<const ::AST::UseItem*> resolve_stack_ptrs;
                    if (::std::find(resolve_stack_ptrs.begin(), resolve_stack_ptrs.end(), &imp_data) == resolve_stack_ptrs.end()) {
                        resolve_stack_ptrs.push_back(&imp_data);
                        bindings_ = Resolve_Use_GetBinding(sp2, crate, mod.path(), Resolve_Use_AbsolutisePath(sp2, crate, mod.path(), imp_e.path), parent_modules, /*type_only=*/true, /*soft_fail=*/true);
                        if (bindings_.type.is_Unbound()) {
                            DEBUG("Recursion detected, skipping " << imp_e.path);
                            resolve_stack_ptrs.pop_back();
                            continue;
                        }
                        // *waves hand* I'm not evil.
                        const_cast<::AST::Path::Bindings&>(imp_e.path.m_bindings) = bindings_.clone();
                        bindings = &bindings_;
                        resolve_stack_ptrs.pop_back();
                    } else {
                        DEBUG("Recursion detected (resolve_stack_ptrs), skipping " << imp_e.path);
                        continue;
                    }
                } else {
                    //out_path = imp_e.path;
                }

                TU_MATCH_HDRA( (bindings->type.binding), {)
                TU_ARMA(Crate, e) {
                        assert(e.crate_);
                        //const ::HIR::Module& hmod = e.crate_->m_hir->m_root_module;
                        rv.merge_from(Resolve_Use_GetBinding__ext(sp2, crate, AST::Path("", {AST::PathNode(des_item_name, {})}), *e.crate_, 0));
                    }
                    TU_ARMA(Module, e) {
                        if (e.module_) {
                            // Prevent infinite recursion - keyed by (module, name) so an
                            // in-flight search for a *different* name doesn't block this one
                            // (libc resolves `crate::linux` through `new::*` while a search
                            // inside `new` is still on the stack).
                            static ::std::vector<::std::pair<const AST::Module*, RcString>> s_use_glob_mod_stack;
                            auto ent = ::std::make_pair(&*e.module_, des_item_name);
                            if (::std::find(s_use_glob_mod_stack.begin(), s_use_glob_mod_stack.end(), ent) == s_use_glob_mod_stack.end()) {
                                s_use_glob_mod_stack.push_back(ent);
                                rv.merge_from(Resolve_Use_GetBinding_Mod(span, crate, mod.path(), *e.module_, des_item_name, {}, /*types_only=*/false, /*require_visible=*/true));
                                s_use_glob_mod_stack.pop_back();
                            } else {
                                DEBUG("Recursion prevented of " << e.module_->path());
                            }
                        } else if (e.hir.mod) {
                            rv.merge_from(Resolve_Use_GetBinding__ext(sp2, crate, *e.hir.crate, *e.hir.mod, AST::Path("", {AST::PathNode(des_item_name, {})}), 0, bindings->type.path));
                        } else {
                            BUG(span, "NULL module for binding on glob of " << imp_e.path);
                        }
                    }
                    TU_ARMA(Enum, e) {
                        assert(e.enum_ || e.hir);
                        if (e.enum_) {
                            const auto& enm = *e.enum_;
                            unsigned int i = 0;
                            for (const auto& var : enm.variants()) {
                                if (var.m_name == des_item_name) {
                                    ::AST::Path::Bindings tmp_rv;
                                    if (var.m_data.is_Struct()) {
                                        tmp_rv.type.set(bindings->type.path + des_item_name, ::AST::PathBinding_Type::make_EnumVar({&enm, i}));
                                    } else {
                                        tmp_rv.value.set(bindings->type.path + des_item_name, ::AST::PathBinding_Value::make_EnumVar({&enm, i}));
                                    }
                                    rv.merge_from(tmp_rv);
                                    break;
                                }
                                i++;
                            }
                        } else {
                            const auto& enm = *e.hir;
                            auto idx = enm.find_variant(des_item_name);
                            if (idx != SIZE_MAX) {
                                ::AST::Path::Bindings tmp_rv;
                                if (enm.m_data.is_Data() && enm.m_data.as_Data()[idx].is_struct) {
                                    tmp_rv.type.set(bindings->type.path + des_item_name, ::AST::PathBinding_Type::make_EnumVar({nullptr, static_cast<unsigned>(idx), &enm}));
                                } else {
                                    tmp_rv.value.set(bindings->type.path + des_item_name, ::AST::PathBinding_Value::make_EnumVar({nullptr, static_cast<unsigned>(idx), &enm}));
                                }
                                rv.merge_from(tmp_rv);
                                break;
                            }
                        }
                    }
                    break;
                    default:
                        BUG(sp2, "Wildcard import expanded to an invalid item class - " << bindings->type.binding.tag_str());
                        break;
                }
            }
        }
    }
    if (rv.has_binding()) {
        return rv;
    }

    if (mod.path().nodes.size() > 0 && mod.path().nodes.back().c_str()[0] == '#') {
        ASSERT_BUG(span, parent_modules.size() > 0, "Anon module with no parent modules - " << mod.path());
        return Resolve_Use_GetBinding_Mod(span, crate, source_mod_path, *parent_modules.back(), des_item_name, parent_modules.subspan(0, parent_modules.size() - 1));
    } else {
        //if( allow == Lookup::Any )
        //    ERROR(span, E0000, "Could not find node '" << des_item_name << "' in module " << mod.path());
        return ::AST::Path::Bindings();
    }
}

namespace {
    const ::HIR::Module* get_hir_mod_by_path(const Span& sp, const ::AST::Crate& crate, const ::HIR::SimplePath& path);

    const void* get_hir_modenum_by_path(const Span& sp, const ::AST::Crate& crate, const ::HIR::SimplePath& path, bool& is_enum) {
        const auto* hmod = &crate.m_extern_crates.at(path.crate_name()).m_hir->m_root_module;
        for (const auto& node : path.components()) {
            auto it = hmod->m_mod_items.find(node);
            if (it == hmod->m_mod_items.end()) {
                BUG(sp, "");
            }
            TU_IFLET(::HIR::TypeItem, (it->second->ent), Module, mod, hmod = &mod;)
            else TU_IFLET(::HIR::TypeItem, (it->second->ent), Import, import, hmod = get_hir_mod_by_path(sp, crate, import.path); if (!hmod) BUG(sp, "Import in module position didn't resolve as a module - " << import.path);) else TU_IFLET(::HIR::TypeItem, (it->second->ent), Enum, enm, if (&node == &path.components().back()) {
                is_enum = true;
                return &enm;
            } BUG(sp, "");) else {
                if (&node == &path.components().back()) {
                    return nullptr;
                }
                BUG(sp, "");
            }
        }
        is_enum = false;
        return hmod;
    }

    const ::HIR::Module* get_hir_mod_by_path(const Span& sp, const ::AST::Crate& crate, const ::HIR::SimplePath& path) {
        bool is_enum = false;
        auto rv = get_hir_modenum_by_path(sp, crate, path, is_enum);
        if (!rv) {
            return nullptr;
        }
        ASSERT_BUG(sp, !is_enum, "");
        return reinterpret_cast<const ::HIR::Module*>(rv);
    }
}

::AST::Path::Bindings Resolve_Use_GetBinding__ext(const Span& span, const ::AST::Crate& crate, const AST::ExternCrate& hcrate, const ::HIR::Module& hmodr, const ::AST::Path& path, unsigned int start, AST::AbsolutePath ap) {
    if (ap.crate == "") {
        ap.crate = hcrate.m_name;
    }

    ::AST::Path::Bindings rv;
    //TRACE_FUNCTION_FR(path << " offset " << start, rv.value << rv.type << rv.macro);
    TRACE_FUNCTION_F(path << " offset " << start << " [" << ap << "]");
    const auto& nodes = path.nodes();
    const ::HIR::Module* hmod = &hmodr;

    //for(unsigned int i = start; i < nodes.size(); i ++)
    //    ap.nodes.push_back( nodes[i].name() );

    if (nodes.size() == start) {
        rv.type.set(ap, ::AST::PathBinding_Type::make_Module({nullptr, {&hcrate, hmod}}));
        return rv;
    }
    for (unsigned int i = start; i < nodes.size() - 1; i++) {
        ap.nodes.push_back(nodes[i].name());
        DEBUG("m_mod_items = {" << FMT_CB(ss, for (const auto& e : hmod->m_mod_items) ss << e.first << ", ";) << "}");
        auto it = hmod->m_mod_items.find(nodes[i].name());
        if (it == hmod->m_mod_items.end()) {
            // BZZT!
            ERROR(span, E0000, "Unable to find path component " << nodes[i].name() << " in " << path << " (" << ap << ")");
        }
        DEBUG(i << " : " << nodes[i].name() << " = " << it->second->ent.tag_str());
        TU_MATCH_HDRA( (it->second->ent), {)
        default:
            ERROR(span, E0000, "Unexpected item type in import " << path << " @ " << i << " - " << it->second->ent.tag_str());
            TU_ARMA(Import, e) {
                // TODO: This is kinda like a duplicate of Resolve_Absolute_Path_BindAbsolute__hir_from ?
                bool is_enum = false;
                auto ptr = get_hir_modenum_by_path(span, crate, e.path, is_enum);
                if (!ptr) {
                    BUG(span, "Path component " << nodes[i].name() << " pointed to non-module (" << path << ")");
                }
                if (is_enum) {
                    const auto& enm = *reinterpret_cast<const ::HIR::Enum*>(ptr);
                    i += 1;
                    if (i != nodes.size() - 1) {
                        ERROR(span, E0000, "Encountered enum at unexpected location in import");
                    }
                    const auto& name = nodes[i].name();

                    auto idx = enm.find_variant(name);
                    if (idx == SIZE_MAX) {
                        ERROR(span, E0000, "Unable to find variant " << path);
                    }
                    ap.crate = e.path.crate_name();
                    ap.nodes = e.path.components_vec();
                    ap.nodes.push_back(name);
                    if (enm.m_data.is_Data() && enm.m_data.as_Data()[idx].is_struct) {
                        rv.type.set(ap, ::AST::PathBinding_Type::make_EnumVar({nullptr, static_cast<unsigned int>(idx), &enm}));
                    } else {
                        rv.value.set(ap, ::AST::PathBinding_Value::make_EnumVar({nullptr, static_cast<unsigned int>(idx), &enm}));
                    }
                    return rv;
                } else {
                    ap.crate = e.path.crate_name();
                    ap.nodes = e.path.components_vec();
                    hmod = reinterpret_cast<const ::HIR::Module*>(ptr);
                }
            }
            TU_ARMA(Module, e) {
                hmod = &e;
            }
            TU_ARMA(Enum, e) {
                i += 1;
                if (i != nodes.size() - 1) {
                    ERROR(span, E0000, "Encountered enum at unexpected location in import");
                }
                const auto& name = nodes[i].name();
                ap.nodes.push_back(name);

                auto idx = e.find_variant(name);
                if (idx == SIZE_MAX) {
                    ERROR(span, E0000, "Unable to find variant " << path);
                }
                if (e.m_data.is_Data() && e.m_data.as_Data()[idx].is_struct) {
                    rv.type.set(ap, ::AST::PathBinding_Type::make_EnumVar({nullptr, static_cast<unsigned int>(idx), &e}));
                } else {
                    rv.value.set(ap, ::AST::PathBinding_Value::make_EnumVar({nullptr, static_cast<unsigned int>(idx), &e}));
                }
                return rv;
            }
        }
    }
    // > Found the target module
    ap.nodes.push_back(nodes.back().name());

    // - namespace/type items
    {
        auto it = hmod->m_mod_items.find(nodes.back().name());
        if (it == hmod->m_mod_items.end()) {
            DEBUG("E: : Types = " << FMT_CB(ss, for (const auto& e : hmod->m_mod_items) { ss << e.first << ":" << e.second->ent.tag_str() << ","; }));
        } else if (!it->second->publicity.is_global()) {
            DEBUG("E : Mod " << nodes.back().name() << " = " << it->second->ent.tag_str() << " [private]");
        } else {
            const auto* item_ptr = &it->second->ent;
            auto ap2 = ap;
            auto ap = ap2;
            DEBUG("E : Mod " << nodes.back().name() << " = " << item_ptr->tag_str());
            if (item_ptr->is_Import()) {
                const auto& e = item_ptr->as_Import();
                ap = AST::AbsolutePath(e.path.crate_name(), e.path.components_vec());
                if (e.path.crate_name() == rcstring_crate_builtins) {
                    auto t = coretype_fromstring(e.path.components().front().c_str());
                    rv.type.set(ap, ::AST::PathBinding_Type::make_Primitive(t));
                } else {
                    ASSERT_BUG(span, crate.m_extern_crates.count(e.path.crate_name()) != 0, "Crate not loaded for " << e.path);
                    const auto& ec = crate.m_extern_crates.at(e.path.crate_name());
                    // This doesn't need to recurse - it can just do a single layer (as no Import should refer to another)
                    if (e.is_variant) {
                        const auto& enm = ec.m_hir->get_typeitem_by_path(span, e.path, /*ignore_crate_name*/ true, /*ignore_last_node*/ true).as_Enum();
                        assert(e.idx < enm.num_variants());
                        rv.type.set(ap, ::AST::PathBinding_Type::make_EnumVar({nullptr, e.idx, &enm}));
                    } else if (e.path.components().empty()) {
                        rv.type.set(ap, ::AST::PathBinding_Type::make_Module({nullptr, {&ec, &ec.m_hir->m_root_module}}));
                    } else {
                        item_ptr = &ec.m_hir->get_typeitem_by_path(span, e.path, /*ignore_crate_name=*/true);
                    }
                }
            } else {
            }
            if (rv.type.is_Unbound()) {
                TU_MATCHA(
                    (*item_ptr),
                    (e),
                    (Import, BUG(span, "Recursive import in " << path << " - " << it->second->ent.as_Import().path << " -> " << e.path);),
                    (Module, rv.type.set(ap, ::AST::PathBinding_Type::make_Module({nullptr, {&hcrate, &e}}));),
                    (TypeAlias, rv.type.set(ap, ::AST::PathBinding_Type::make_TypeAlias({nullptr}));),
                    (
                        ExternType, rv.type.set(ap, ::AST::PathBinding_Type::make_TypeAlias({nullptr})); // Lazy.
                    ),
                    (Enum, rv.type.set(ap, ::AST::PathBinding_Type::make_Enum({nullptr, &e}));),
                    (Struct, rv.type.set(ap, ::AST::PathBinding_Type::make_Struct({nullptr, &e}));),
                    (Union, rv.type.set(ap, ::AST::PathBinding_Type::make_Union({nullptr, &e}));),
                    (Trait, rv.type.set(ap, ::AST::PathBinding_Type::make_Trait({nullptr, &e}));),
                    (TraitAlias, rv.type.set(ap, ::AST::PathBinding_Type::make_TraitAlias({nullptr, &e}));)
                )
            }
        }
    }
    // - Values
    {
        auto it = hmod->m_value_items.find(nodes.back().name());
        if (it == hmod->m_value_items.end()) {
            DEBUG("E : Values = " << FMT_CB(ss, for (const auto& e : hmod->m_value_items) { ss << e.first << ":" << e.second->ent.tag_str() << ","; }));
        } else if (!it->second->publicity.is_global()) {
            DEBUG("E : Value " << nodes.back().name() << " = " << it->second->ent.tag_str() << " [private]");
        } else {
            const auto* item_ptr = &it->second->ent;
            auto ap2 = ap;
            auto ap = ap2;
            DEBUG("E : Value " << nodes.back().name() << " = " << item_ptr->tag_str());
            if (item_ptr->is_Import()) {
                const auto& e = item_ptr->as_Import();
                ap = AST::AbsolutePath(e.path.crate_name(), e.path.components_vec());
                // This doesn't need to recurse - it can just do a single layer (as no Import should refer to another)
                const auto& ec = crate.m_extern_crates.at(e.path.crate_name());
                if (e.is_variant) {
                    auto p = e.path;
                    p.pop_component();
                    const auto& enm = ec.m_hir->get_typeitem_by_path(span, p, true).as_Enum();
                    assert(e.idx < enm.num_variants());
                    rv.value.set(ap, ::AST::PathBinding_Value::make_EnumVar({nullptr, e.idx, &enm}));
                } else {
                    item_ptr = &ec.m_hir->get_valitem_by_path(span, e.path, true); // ignore_crate_name=true
                }
            }
            if (rv.value.is_Unbound()) {
                TU_MATCH_HDRA( (*item_ptr), {)
                TU_ARMA(Import, e) {
                        BUG(span, "Recursive import in " << path << " - " << it->second->ent.as_Import().path << " -> " << e.path);
                    }
                    TU_ARMA(Constant, e) {
                        rv.value.set(ap, ::AST::PathBinding_Value::make_Static({nullptr}));
                    }
                    TU_ARMA(Static, e) {
                        rv.value.set(ap, ::AST::PathBinding_Value::make_Static({nullptr}));
                    }
                    // TODO: What happens if these two refer to an enum constructor?
                    TU_ARMA(StructConstant, e) {
                        ASSERT_BUG(span, crate.m_extern_crates.count(e.ty.crate_name()), "Crate '" << e.ty.crate_name() << "' not loaded for " << e.ty);
                        rv.value.set(ap, ::AST::PathBinding_Value::make_Struct({nullptr, &crate.m_extern_crates.at(e.ty.crate_name()).m_hir->get_typeitem_by_path(span, e.ty, true).as_Struct()}));
                    }
                    TU_ARMA(StructConstructor, e) {
                        ASSERT_BUG(span, crate.m_extern_crates.count(e.ty.crate_name()), "Crate '" << e.ty.crate_name() << "' not loaded for " << e.ty);
                        rv.value.set(ap, ::AST::PathBinding_Value::make_Struct({nullptr, &crate.m_extern_crates.at(e.ty.crate_name()).m_hir->get_typeitem_by_path(span, e.ty, true).as_Struct()}));
                    }
                    TU_ARMA(Function, e) {
                        rv.value.set(ap, ::AST::PathBinding_Value::make_Function({nullptr}));
                    }
                }
            }
        }
    }
    // - Macros
    {
        auto it = hmod->m_macro_items.find(nodes.back().name());
        if (it == hmod->m_macro_items.end()) {
            DEBUG("E : Macros = " << FMT_CB(ss, for (const auto& e : hmod->m_macro_items) { ss << e.first << ":" << e.second->ent.tag_str() << ","; }));
        } else if (!it->second->publicity.is_global()) {
            DEBUG("E : Macro " << nodes.back().name() << " = " << it->second->ent.tag_str() << " [private]");
        } else {
            const auto* item_ptr = &it->second->ent;
            auto ap2 = ap;
            auto ap = ap2;
            DEBUG("E : Macro " << nodes.back().name() << " = " << item_ptr->tag_str());

            if (const auto* imp = item_ptr->opt_Import()) {
                if (imp->path.crate_name() == rcstring_crate_builtins) {
                    rv.macro.set(AST::AbsolutePath(rcstring_crate_builtins, {nodes.back().name()}), AST::PathBinding_Macro::make_MacroRules({nullptr}));
                    return rv;
                }
                ASSERT_BUG(span, crate.m_extern_crates.count(imp->path.crate_name()) > 0, "Unable to find crate for " << imp->path);
                const auto& c = *crate.m_extern_crates.at(imp->path.crate_name()).m_hir; // Have to manually look up, AST doesn't have a `get_mod_by_path`
                const auto& mod = c.get_mod_by_path(span, imp->path, /*ignore_last=*/true, /*ignore_crate=*/true);
                item_ptr = &mod.m_macro_items.at(imp->path.components().back())->ent;
                ap = AST::AbsolutePath(imp->path.crate_name(), imp->path.components_vec());
            } else {
            }

            if (rv.macro.is_Unbound()) {
                TU_MATCH_HDRA( (*item_ptr), {)
                TU_ARMA(Import, e) {
                        if (e.path.crate_name() == rcstring_crate_builtins)
                            ;
                        else {
                            BUG(span, "Recursive import in " << path << " - " << it->second->ent.as_Import().path << " -> " << e.path);
                        }
                        rv.macro.set(ap, ::AST::PathBinding_Macro::make_MacroRules({nullptr, nullptr}));
                    }
                    TU_ARMA(ProcMacro, e) {
                        rv.macro.set(ap, ::AST::PathBinding_Macro::make_ProcMacro({&hcrate, e.name}));
                    }
                    TU_ARMA(MacroRules, e) {
                        rv.macro.set(ap, ::AST::PathBinding_Macro::make_MacroRules({nullptr, &*e}));
                    }
                }
            }
        }
    }

    if (rv.type.is_Unbound() && rv.value.is_Unbound() && rv.macro.is_Unbound()) {
        DEBUG("E : None");
    } else {
        DEBUG(rv.type << rv.value << rv.macro);
    }
    return rv;
}

::AST::Path::Bindings Resolve_Use_GetBinding__ext(const Span& span, const ::AST::Crate& crate, const ::AST::Path& path, const AST::ExternCrate& ec, unsigned int start) {
    DEBUG("Crate " << ec.m_name);
    auto rv = Resolve_Use_GetBinding__ext(span, crate, ec, ec.m_hir->m_root_module, path, start);
    if (auto* e = rv.macro.binding.opt_MacroRules()) {
        if (e->crate_ == nullptr) {
            e->crate_ = &ec;
        }
    }
    return rv;
}

::AST::Path::Bindings Resolve_Use_GetBinding(
    const Span& span,
    const ::AST::Crate& crate,
    const ::AST::AbsolutePath& source_mod_path,
    const ::AST::Path& path,
    ::std::span<const ::AST::Module*> parent_modules,
    bool types_only /*=false*/,
    bool soft_fail /*=false*/
) {
    TRACE_FUNCTION_F(path);
    //::AST::Path rv;

    // If the path is directly referring to an external crate - call __ext
    if (path.m_class.is_Absolute() && (path.m_class.as_Absolute().crate != "" && path.m_class.as_Absolute().crate != crate.m_crate_name_real)) {
        const auto& path_abs = path.m_class.as_Absolute();
        // Builtin macro imports
        if (path_abs.crate == rcstring_crate_builtins) {
            ::AST::Path::Bindings rv;
            ASSERT_BUG(span, !path_abs.nodes.empty(), "");
            if (coretype_fromstring(path.nodes()[0].name().c_str()) != CORETYPE_INVAL) {
                rv.type.set(AST::AbsolutePath(rcstring_crate_builtins, {path_abs.nodes.back().name()}), AST::PathBinding_Type::make_TypeAlias({nullptr}));
            } else {
                rv.macro.set(AST::AbsolutePath(rcstring_crate_builtins, {path_abs.nodes.back().name()}), AST::PathBinding_Macro::make_MacroRules({nullptr}));
            }
            return rv;
        }

        ASSERT_BUG(span, crate.m_extern_crates.count(path_abs.crate.c_str()), "Crate '" << path_abs.crate << "' not loaded");
        return Resolve_Use_GetBinding__ext(span, crate, path, crate.m_extern_crates.at(path_abs.crate.c_str()), 0);
    }

    ::AST::Path::Bindings rv;

    const AST::Module* mod = &crate.m_root_module;
    const auto& nodes = path.nodes();
    if (nodes.size() == 0) {
        // An import of the root.
        rv.type.set(mod->path(), ::AST::PathBinding_Type::make_Module({mod, {nullptr}}));
        return rv;
    }

    std::vector<const AST::Module*> inner_parent_modules;
    for (unsigned int i = 0; i < nodes.size() - 1; i++) {
        DEBUG("Component " << nodes.at(i).name());
        // TODO: If this came from an import, return the real path?

        //rv = Resolve_Use_CanoniseAndBind_Mod(span, crate, *mod, mv$(rv), nodes[i].name(), parent_modules, Lookup::Type);
        //const auto& b = rv.binding();
        assert(mod);
        auto b = Resolve_Use_GetBinding_Mod(span, crate, source_mod_path, *mod, nodes.at(i).name(), inner_parent_modules, /*types_only=*/true);
        TU_MATCH_HDRA( (b.type.binding), {)
        default:
            ERROR(span, E0000, "Unexpected item type " << b.type.binding.tag_str() << " in import of " << path);
            TU_ARMA(Unbound, e) {
                // During speculative glob resolution a miss just skips the glob; the recursion
                // guard can hide a module that a later direct resolution will find.
                if (soft_fail) {
                    return ::AST::Path::Bindings();
                }
                ERROR(span, E0000, "Cannot find component " << i << " of " << path << " (" << b.type.binding << ")");
            }
            TU_ARMA(Crate, e) {
                // TODO: Mangle the original path (or return a new path somehow)
                DEBUG("Extern - Call _ext with remainder");
                return Resolve_Use_GetBinding__ext(span, crate, path, *e.crate_, i + 1);
            }
            TU_ARMA(Enum, e) {
                ASSERT_BUG(span, e.enum_ || e.hir, "nullptr enum pointer in node " << i << " of " << path);
                ASSERT_BUG(span, e.enum_ == nullptr || e.hir == nullptr, "both AST and HIR pointers set in node " << i << " of " << path);
                i += 1;
                if (i != nodes.size() - 1) {
                    ERROR(span, E0000, "Encountered enum at unexpected location in import");
                }
                ASSERT_BUG(span, i < nodes.size(), "Enum import position error, " << i << " >= " << nodes.size() << " - " << path);

                const auto& node2 = nodes[i];

                unsigned variant_index = 0;
                bool is_value = false;
                if (e.hir) {
                    const auto& enum_ = *e.hir;
                    size_t idx = enum_.find_variant(node2.name());
                    if (idx == ~0u) {
                        ERROR(span, E0000, "Unknown enum variant " << path);
                    }
                TU_MATCH_HDRA( (enum_.m_data), {)
                TU_ARMA(Value, ve) {
                            is_value = true;
                        }
                        TU_ARMA(Data, ve) {
                            is_value = !ve[idx].is_struct;
                        }
                }
                DEBUG("HIR Enum variant - " << variant_index << ", is_value=" << is_value);
                } else {
                    const auto& enum_ = *e.enum_;
                    for (const auto& var : enum_.variants()) {
                        if (var.m_name == node2.name()) {
                            is_value = !var.m_data.is_Struct();
                            break;
                        }
                        variant_index++;
                    }
                    if (variant_index == enum_.variants().size()) {
                        ERROR(span, E0000, "Unknown enum variant '" << node2.name() << "'");
                    }

                    DEBUG("AST Enum variant - " << variant_index << ", is_value=" << is_value << " " << enum_.variants()[variant_index].m_data.tag_str());
                }
                if (is_value) {
                    rv.value.set(b.type.path + node2.name(), ::AST::PathBinding_Value::make_EnumVar({e.enum_, variant_index, e.hir}));
                } else {
                    rv.type.set(b.type.path + node2.name(), ::AST::PathBinding_Type::make_EnumVar({e.enum_, variant_index, e.hir}));
                }
                return rv;
            }
            TU_ARMA(Module, e) {
                ASSERT_BUG(span, e.module_ || e.hir.mod, "nullptr module pointer in node " << i << " of " << path);
                if (!e.module_) {
                    assert(e.hir.crate);
                    assert(e.hir.mod);
                    return Resolve_Use_GetBinding__ext(span, crate, *e.hir.crate, *e.hir.mod, path, i + 1, b.type.path);
                }
                inner_parent_modules.push_back(mod);
                mod = e.module_;
            }
        }
    }

    assert(mod);
    return Resolve_Use_GetBinding_Mod(span, crate, source_mod_path, *mod, nodes.back().name(), parent_modules, types_only);
}

//::AST::PathBinding_Macro Resolve_Use_GetBinding_Macro(const Span& span, const ::AST::Crate& crate, const ::AST::Path& path, ::std::span< const ::AST::Module* > parent_modules)
//{
//    throw "";
//}
