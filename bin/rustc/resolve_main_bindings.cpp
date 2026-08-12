#include "resolve_main_bindings.h"

#include "ast_crate.h"
#include "ast_ast.h"
#include "ast_expr.h"
#include "main_bindings.h"
#include "hir_hir.h"
#include "macro_rules_macro_rules.h"
#include "stdspan.h" // std::span
#include "pop_on_drop.h"

#define FLAG_CONST_GENERIC (1u << 31)

namespace {
    static const RcString rcstringSelf = RcString::new_interned("Self");

    AST::AbsolutePath sp_to_ap(const HIR::SimplePath& sp) {
        return AST::AbsolutePath(sp.crate_name(), sp.componentsVec());
    }

    struct GenericSlot {
        enum class Level {
            Top,
            Method,
            UnusedPlaceholder,
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
            (MacroDefinition,
             struct {
                 unsigned int level;
                 unsigned int definition_id;
                 Ident::Hygiene token_hygiene;
                 Ident::Hygiene definition_hygiene;
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

        const ::AST::Crate& crate;
        const ::AST::Module& mMod;
        ::std::vector<Ent> nameContext;

        struct PatternStackEnt {
            unsigned firstArmDone = false;
            std::set<Ident> createdVariables;
            std::set<Ident> firstArmVariables;
        };

        ::std::vector<PatternStackEnt> patternStack;
        unsigned int varCount;
        unsigned int blockLevel;

        // Destination `GenericParams` for in_band_lifetimes
        ::AST::GenericParams* iblTargetGenerics;

        Context(const ::AST::Crate& crate, const ::AST::Module& mod)
            : crate(crate)
            , mMod(mod)
            , varCount(~0u)
            , blockLevel(0)
            , iblTargetGenerics(nullptr)
        {
        }

        void push(const ::AST::HigherRankedBounds& params) {
            auto e = Ent::make_Generic({GenericSlot::Level::Hrb, nullptr /*, &params*/});
            auto& data = e.as_Generic();

            for (size_t i = 0; i < params.mLifetimes.size(); i++) {
                data.lifetimes.push_back(NamedI<GenericSlot>{params.mLifetimes[i].name(), GenericSlot{GenericSlot::Level::Hrb, static_cast<unsigned short>(i)}});
            }

            nameContext.push_back(mv$(e));
        }

        void push(/*const */ ::AST::GenericParams& params, GenericSlot::Level level, bool hasSelf = false) {
            auto e = Ent::make_Generic({level, &params});
            auto& data = e.as_Generic();

            if (hasSelf) {
                //assert( level == GenericSlot::Level::Top );
                data.types.push_back(Named<GenericSlot>{rcstringSelf, GenericSlot{level, GENERICSelf}});
                nameContext.push_back(Ent::make_ConcreteSelf(nullptr));
            }
            if (!params.mParams.empty()) {
                unsigned short lft_idx = 0;
                unsigned short ty_idx = 0;
                unsigned short val_idx = 0;
                for (const auto& e : params.mParams) {
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

            nameContext.push_back(mv$(e));
        }

        void pop(const ::AST::HigherRankedBounds&) {
            if (!nameContext.back().is_Generic()) {
                BUG(Span(), "resolve/absolute.cpp - Context::pop(GenericParams) - Mismatched pop");
            }
            nameContext.pop_back();
        }

        void pop(const ::AST::GenericParams&, bool hasSelf = false) {
            if (!nameContext.back().is_Generic()) {
                BUG(Span(), "resolve/absolute.cpp - Context::pop(GenericParams) - Mismatched pop");
            }
            nameContext.pop_back();
            if (hasSelf) {
                if (!nameContext.back().is_ConcreteSelf()) {
                    BUG(Span(), "resolve/absolute.cpp - Context::pop(GenericParams) - Mismatched pop");
                }
                nameContext.pop_back();
            }
        }

        void push(const ::AST::Module& mod) {
            nameContext.push_back(Ent::make_Module({&mod}));
        }

        void pop(const ::AST::Module& mod) {
            if (!nameContext.back().is_Module()) {
                BUG(Span(), "resolve/absolute.cpp - Context::pop(GenericParams) - Mismatched pop");
            }
            nameContext.pop_back();
        }

        class RootBlockScope {
            friend struct Context;
            Context& ctxt;
            unsigned int old_varcount;

            RootBlockScope(Context& ctxt, unsigned int val)
                : ctxt(ctxt)
                , old_varcount(ctxt.varCount)
            {
                ctxt.varCount = val;
            }

        public:
            ~RootBlockScope() {
                ctxt.varCount = old_varcount;
            }
        };

        RootBlockScope enterRootblock() {
            return RootBlockScope(*this, 0);
        }

        RootBlockScope clearRootblock() {
            return RootBlockScope(*this, ~0u);
        }

        void push_self(const TypeRef& tr) {
            nameContext.push_back(Ent::make_ConcreteSelf(&tr));
        }

        void pop_self(const TypeRef& tr) {
            TU_IFLET(Ent, nameContext.back(), ConcreteSelf, e, nameContext.pop_back();)
            else {
                BUG(Span(), "resolve/absolute.cpp - Context::pop(TypeRef) - Mismatched pop");
            }
        }

        ::TypeRef getSelf() const {
            for (auto it = nameContext.rbegin(); it != nameContext.rend(); ++it) {
                TU_MATCH_DEF(Ent, (*it), (e), (), (ConcreteSelf, if (false && e) { return e->clone(); } else { return ::TypeRef(Span(), rcstringSelf, GENERICSelf); }))
            }

            TODO(Span(), "Error when get_self called with no self");
        }

        const ::TypeRef* getSelfOpt() const {
            for (auto it = nameContext.rbegin(); it != nameContext.rend(); ++it) {
                if (const auto* e = it->opt_ConcreteSelf()) {
                    return *e;
                }
            }
            return nullptr;
        }

        void push_block() {
            blockLevel += 1;
            DEBUG("Push block to " << blockLevel);
        }

        void push_macro_definition(unsigned int definition_id, const Ident::Hygiene& token_hygiene, const Ident::Hygiene& definition_hygiene) {
            assert(blockLevel > 0);
            nameContext.push_back(Ent::make_MacroDefinition({blockLevel, definition_id, token_hygiene, definition_hygiene}));
        }

        unsigned int push_var(const Span& sp, const Ident& name) {
            if (varCount == ~0u) {
                BUG(sp, "Assigning local when there's no variable context");
            }
            // If this variable is defined within a stack entry, then use it
            ASSERT_BUG(sp, !patternStack.empty(), "Pushing a variable with no active scopes");
            bool alreadyDefined = patternStack.back().firstArmDone;
            for (auto it = patternStack.rbegin(); it != patternStack.rend(); ++it) {
                if (it->firstArmVariables.count(name)) {
                    alreadyDefined = true;
                    break;
                }
            }
            if (!patternStack.back().createdVariables.insert(name).second) {
                ERROR(sp, E0000, "Duplicate definition of `" << name << "` in pattern arm");
            }
            // Are we currently in the second (or later) arm of a split pattern
            if (alreadyDefined) {
                if (!nameContext.back().is_VarBlock()) {
                    BUG(sp, "resolve/absolute.cpp - Context::push_var - No block");
                }
                auto& vb = nameContext.back().as_VarBlock();
                // Work backwards, in case there are multiple bindings in the same scope.
                for (const auto& v : ::reverse(vb.variables)) {
                    if (v.first == name) {
                        DEBUG("Arm defined var @ " << blockLevel << ": #" << v.second << " " << name);
                        return v.second;
                    }
                }
                ERROR(sp, E0000, "Mismatched bindings in pattern (`" << name << "` wasn't in the first arm)");
            } else {
                assert(blockLevel > 0);
                if (nameContext.empty() || !nameContext.back().is_VarBlock() || nameContext.back().as_VarBlock().level < blockLevel) {
                    nameContext.push_back(Ent::make_VarBlock({blockLevel, {}}));
                }
                DEBUG("New var @ " << blockLevel << ": #" << varCount << " " << name);
                auto& vb = nameContext.back().as_VarBlock();
                assert(vb.level == blockLevel);
                vb.variables.push_back(::std::make_pair(mv$(name), varCount));
                varCount += 1;
                assert(varCount >= vb.variables.size());
                return varCount - 1;
            }
        }

        void pop_block() {
            assert(blockLevel > 0);
            while (!nameContext.empty()) {
                if (const auto* e = nameContext.back().opt_VarBlock()) {
                    if (e->level != blockLevel) {
                        break;
                    }
                    DEBUG("Pop block from " << blockLevel << " with vars:" << FMT_CB(os, for (const auto& v : e->variables) os << " " << v.first << "#" << v.second;));
                    nameContext.pop_back();
                } else if (const auto* e = nameContext.back().opt_MacroDefinition()) {
                    if (e->level != blockLevel) {
                        break;
                    }
                    nameContext.pop_back();
                } else {
                    break;
                }
            }
            blockLevel -= 1;
        }

        /// Indicate that a multiple-pattern binding is started
        void start_patbind() {
            assert(blockLevel > 0);
            patternStack.push_back(PatternStackEnt());
        }

        /// Freeze the set of pattern bindings
        void endPatbindArm(const Span& sp) {
            auto& e = patternStack.back();
            if (e.firstArmDone) {
                if (e.firstArmVariables != e.createdVariables) {
                    ERROR(sp, E0000, "Mismatched bindings in pattern - [" << e.firstArmVariables << "] != [" << e.createdVariables << "]");
                }
            } else {
                e.firstArmVariables = std::move(e.createdVariables);
                e.firstArmDone = true;
            }
            e.createdVariables.clear();
        }

        /// End a multiple-pattern binding state (unfreeze really)
        void endPatbind() {
            assert(!patternStack.empty());
            // Propagate the created variables to the next level up.
            if (patternStack.size() > 1) {
                const auto& cur = patternStack[patternStack.size() - 1];
                auto& next = patternStack[patternStack.size() - 2];
                for (auto& var : cur.firstArmVariables) {
                    next.createdVariables.insert(std::move(var));
                }
            }
            patternStack.pop_back();
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
                    auto v = mod.namespaceItems.find(name);
                    if (v != mod.namespaceItems.end()) {
                        DEBUG("- " << mod.path() << " NS: Namespace " << v->second.path);
                        path = ::AST::Path(v->second.path);
                        return true;
                    }
                }
                    {
                        auto v = mod.typeItems.find(name);
                        if (v != mod.typeItems.end()) {
                            DEBUG("- " << mod.path() << " NS: Type " << v->second.path);
                            path = ::AST::Path(v->second.path);
                            return true;
                        }
                    }
                    break;

                case LookupMode::Type:
                case LookupMode::PatternType: {
                    auto v = mod.typeItems.find(name);
                    if (v != mod.typeItems.end()) {
                        DEBUG("- " << mod.path() << " TY: Type " << v->second.path);
                        path = ::AST::Path(v->second.path);
                        return true;
                    }
                }
                    // HACK: For `Enum::Var { .. }` patterns matching value variants
                    if (mode == LookupMode::PatternType) {
                        auto v = mod.valueItems.find(name);
                        if (v != mod.valueItems.end()) {
                            const auto& b = v->second.path.mBindings.value;
                            if (/*const auto* be =*/b.binding.opt_EnumVar()) {
                                DEBUG("- " << mod.path() << " TY: Enum variant " << b.path);
                                path = ::AST::Path(b);
                                return true;
                            }
                        }
                    }
                    break;
                case LookupMode::PatternValue: {
                    auto v = mod.valueItems.find(name);
                    if (v != mod.valueItems.end()) {
                        const auto& b = v->second.path.mBindings.value;
                        switch (b.binding.tag()) {
                            case ::AST::PathBindingValue::TAG_EnumVar:
                            case ::AST::PathBindingValue::TAG_Static:
                                DEBUG("- PV: Value " << v->second.path);
                                path = ::AST::Path(v->second.path);
                                return true;
                            case ::AST::PathBindingValue::TAG_Struct: {
                                const auto& be = b.binding.as_Struct();
                                // TODO: Restrict this to unit-like structs
                                if (be.struct_ && !be.struct_->mData.is_Unit())
                                    ;
                                else if (be.hir && !be.hir->mData.is_Unit())
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
                    auto v = mod.valueItems.find(name);
                    if (v != mod.valueItems.end()) {
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
            auto lookup_context = src_context;
            // NOTE: src_context may provide a module to search
            // TODO: This should be checked AFTER locals
            if (src_context.hasModPath()) {
                const auto& mp = src_context.mod_path();
                DEBUG(mp);
                if (mp.crate != "") {
                    HIR::SimplePath vis_path{mp.crate, mp.ents};

                    static Span sp;
                    // External crate path
                    ASSERT_BUG(sp, crate.externCrates.count(mp.crate), "Crate not loaded for " << mp);
                    const auto& extCrate = crate.externCrates.at(mp.crate);
                    const HIR::Module* mod = &extCrate.hir->rootModule;
                    for (const auto& n : mp.ents) {
                        ASSERT_BUG(sp, mod->modItems.count(n), "Node `" << n << "` missing in path " << mp);
                        const auto& i = *mod->modItems.at(n);
                        ASSERT_BUG(sp, i.ent.is_Module(), "Node `" << n << "` not a module in path " << mp);
                        mod = &i.ent.as_Module();
                    }
                    AST::Path::Bindings bindings;
                    const HIR::SimplePath* true_path = nullptr;
                    switch (mode) {
                        case LookupMode::Constant:
                        case LookupMode::PatternValue:
                        case LookupMode::Variable: {
                            auto it = mod->valueItems.find(name);
                            if (it != mod->valueItems.end()) {
                                const auto* item = &it->second->ent;
                                auto item_path = AST::AbsolutePath(mp.crate, mp.ents) + name;
                                if (item->is_Import()) {
                                    const auto& imp = item->as_Import();
                                    // Set the true path (so the returned path is canonical)
                                    true_path = &imp.path;

                                    auto item_path = sp_to_ap(imp.path) + name;
                                    if (imp.is_variant) {
                                        const auto& enm = crate.externCrates.at(imp.path.crate_name()).hir->getEnumByPath(sp, imp.path, /*ignore_crate_name*/ true, /*ignore_last*/ true);
                                        bindings.value.set(item_path, AST::PathBindingValue::make_EnumVar({nullptr, imp.idx, &enm}));
                                        break; // Break out of the switch
                                    } else {
                                        item = &crate.externCrates.at(imp.path.crate_name()).hir->getValitemByPath(sp, imp.path, true);
                                    }
                                }
                            TU_MATCH_HDRA( (*item), {)
                            default:
                                TODO(sp, "Bind value '" << name << "' for module path " << mp << " : " << item->tag_str());
                                    TU_ARMA(Function, e) {
                                        bindings.value.set(item_path, AST::PathBindingValue::make_Function({nullptr}));
                                    }
                                    TU_ARMA(Static, e) {
                                        bindings.value.set(item_path, AST::PathBindingValue::make_Static({nullptr}));
                                    }
                            }
                            }
                        } break;
                        case LookupMode::Namespace:
                        case LookupMode::PatternType:
                        case LookupMode::Type: {
                            auto it = mod->modItems.find(name);
                            if (it != mod->modItems.end()) {
                                const auto* item = &it->second->ent;
                                auto item_path = AST::AbsolutePath(mp.crate, mp.ents) + name;
                                if (item->is_Import()) {
                                    const auto& imp = item->as_Import();
                                    // Set the true path (so the returned path is canonical)
                                    true_path = &imp.path;

                                    auto item_path = sp_to_ap(imp.path) + name;
                                    if (imp.is_variant) {
                                        const auto& enm = crate.externCrates.at(imp.path.crate_name()).hir->getEnumByPath(sp, imp.path, /*ignore_crate_name*/ true, /*ignore_last*/ true);
                                        bindings.type.set(item_path, AST::PathBindingType::make_EnumVar({nullptr, imp.idx, &enm}));
                                        break; // Break out of the switch
                                    } else {
                                        item = &crate.externCrates.at(imp.path.crate_name()).hir->getTypeitemByPath(sp, imp.path, true);
                                    }
                                }
                            TU_MATCH_HDRA( (*item), {)
                            default:
                                TODO(sp, "Bind type/mod '" << name << "' for module path " << mp << " : " << item->tag_str());
                                    TU_ARMA(Module, e) {
                                        bindings.type.set(item_path, AST::PathBindingType::make_Module({nullptr, {&extCrate, &e}}));
                                    }
                                    TU_ARMA(Trait, e) {
                                        bindings.type.set(item_path, AST::PathBindingType::make_Trait({nullptr}));
                                    }
                                    TU_ARMA(TypeAlias, e) {
                                        bindings.type.set(item_path, AST::PathBindingType::make_TypeAlias({nullptr}));
                                    }
                                    TU_ARMA(Struct, e) {
                                        bindings.type.set(item_path, AST::PathBindingType::make_Struct({nullptr}));
                                    }
                                    TU_ARMA(Enum, e) {
                                        bindings.type.set(item_path, AST::PathBindingType::make_Enum({nullptr}));
                                    }
                                    TU_ARMA(Union, e) {
                                        bindings.type.set(item_path, AST::PathBindingType::make_Union({nullptr}));
                                    }
                            }
                            }
                        } break;
                    }
                    // If any bindings were populated, then generate a path
                    if (bindings.hasBinding()) {
                        auto rv = AST::Path(mp.crate, {});
                        if (true_path) {
                            rv.cls.as_Absolute().crate = true_path->crate_name();
                            for (const auto& e : true_path->components()) {
                                rv.nodes().push_back(e);
                            }
                        } else {
                            for (const auto& e : mp.ents) {
                                rv.nodes().push_back(e);
                            }
                            rv.nodes().push_back(name);
                        }
                        rv.mBindings = std::move(bindings);
                        return rv;
                    }
                    // Fall through
                } else {
                    const AST::Module* mod = &crate.root_module();
                    for (const auto& node : mp.ents) {
                        const AST::Module* next = nullptr;
                        if (node.c_str()[0] == '#') {
                            char c;
                            unsigned int idx;
                            ::std::stringstream ss(node.c_str());
                            ss >> c;
                            ss >> idx;
                            assert(idx < mod->anonMods().size());
                            assert(mod->anonMods()[idx]);
                            next = mod->anonMods()[idx].get();
                        } else {
                            for (const auto& i : mod->mItems) {
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
            for (auto it = nameContext.rbegin(); it != nameContext.rend(); ++it) {
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
                        if (name == rcstringSelf) {
                            switch (mode) {
                                case LookupMode::PatternType:
                                case LookupMode::Type:
                                case LookupMode::Namespace: {
                                    ::AST::Path rv(name);
                                    rv.mBindings.type.set(AST::AbsolutePath(), ::AST::PathBindingType::make_TypeParameter({0xFFFF}));
                                    return rv;
                                    }
                                case LookupMode::Constant:
                                case LookupMode::Variable:
                                    // TODO: Ensure validity? (I.e. that `Self` is a unit or tuple struct
                                    if (const auto* p = e->mData.opt_Path()) {
                                        // HACK! If `Self` points to a `type`, look through it
                                        // - rustc-1.90.0-src/compiler/rustc_codegen_llvm/src/context.rs:675
                                        if (const auto* pbe = (**p).mBindings.type.binding.opt_TypeAlias()) {
                                            assert(pbe->alias_);
                                            assert(pbe->alias_->mType.is_path());
                                            return *pbe->alias_->mType.mData.as_Path();
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
                        assert(e.level <= blockLevel);
                        if (mode != LookupMode::Variable) {
                            // ignore
                        } else {
                            for (auto it2 = e.variables.rbegin(); it2 != e.variables.rend(); ++it2) {
                                if (it2->first.name == name) {
                                    DEBUG("> Match: Hygiene " << it2->first.hygiene << " check against src_context");
                                }
                                if (it2->first.name == name && it2->first.hygiene.is_visible(lookup_context)) {
                                    ::AST::Path rv(name);
                                    rv.bindVariable(it2->second);
                                    return rv;
                                }
                            }
                        }
                    }
                    TU_ARMA(MacroDefinition, e) {
                        if (mode == LookupMode::Variable) {
                            lookup_context.leave_macro_definition(e.definition_id, e.token_hygiene, e.definition_hygiene);
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
                                        rv.mBindings.type.set(AST::AbsolutePath(), AST::PathBindingType::make_TypeParameter({it2->value.to_binding()}));
                                        return rv;
                                    }
                                }
                                break;
                            case LookupMode::Variable:
                            case LookupMode::Constant:
                                for (auto it2 = e.constants.rbegin(); it2 != e.constants.rend(); ++it2) {
                                    if (it2->name.name == name) {
                                        ::AST::Path rv(name);
                                        rv.mBindings.value.set(AST::AbsolutePath(), AST::PathBindingValue::make_Generic({it2->value.to_binding()}));
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
            DEBUG("- Top module (" << mMod.path() << ")");
            ::AST::Path rv;
            if (this->lookup_in_mod(mMod, name, mode, rv)) {
                return rv;
            }

            DEBUG("- Primitives");
            switch (mode) {
                case LookupMode::Namespace:
                case LookupMode::Type: {
                    // Look up primitive types
                    auto ct = coretypeFromstring(name.c_str());
                    if (ct != CORETYPE_INVAL) {
                        return ::AST::Path::new_ufcs_ty(TypeRef(Span(), ct), ::std::vector<::AST::PathNode>());
                    }
                } break;
                default:
                    break;
            }

            // #![feature(extern_prelude)] - 2018-style extern paths
            if (mode == LookupMode::Namespace /*&& m_crate.has_feature("extern_prelude")*/) {
                DEBUG("Extern crates - " << AST::gImplicitCrates);
                auto it = AST::gImplicitCrates.find(name);
                if (it != AST::gImplicitCrates.end()) {
                    DEBUG("- Found '" << name << "' (= " << it->second << ")");
                    return AST::Path(it->second, {});
                }
            }

            return AST::Path();
        }

        unsigned int lookup_local(const Span& sp, const RcString name, LookupMode mode) {
            for (auto it = nameContext.rbegin(); it != nameContext.rend(); ++it) {
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
                    TU_ARMA(MacroDefinition, e) {
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
                                    if (it2->name.name == name) {
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
        Context cloneMod() const {
            auto rv = Context(this->crate, this->mMod);
            for (const auto& v : nameContext) {
                if (const auto* e = v.opt_Module()) {
                    rv.nameContext.push_back(Ent::make_Module(*e));
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

void ResolveAbsolutePathBindAbsolute(Context& context, const Span& sp, Context::LookupMode& mode, ::AST::Path& path);
void ResolveAbsolutePath(/*const*/ Context& context, const Span& sp, Context::LookupMode mode, ::AST::Path& path);
void ResolveAbsoluteLifetime(Context& context, const Span& sp, AST::LifetimeRef& type);
void ResolveAbsoluteType(Context& context, TypeRef& type);
void ResolveAbsoluteExpr(Context& context, ::AST::Expr& expr);
void ResolveAbsoluteExprNode(Context& context, ::AST::ExprNode& node);
void ResolveAbsolutePattern(Context& context, bool allowRefutable, ::AST::Pattern& pat);
void ResolveAbsoluteMod(const ::AST::Crate& crate, ::AST::Module& mod);
void ResolveAbsoluteMod(Context item_context, ::AST::Module& mod);

void ResolveAbsoluteFunction(Context& item_context, ::AST::Function& fcn);

void ResolveAbsolutePathParams(/*const*/ Context& context, const Span& sp, ::AST::PathParams& args) {
    for (auto& ent : args.entries) {
        TU_MATCH_HDRA( (ent), {)
        TU_ARMA(Null, _) {
            }
            TU_ARMA(Lifetime, l) {
                ResolveAbsoluteLifetime(context, sp, l);
            }
            TU_ARMA(Type, t) {
                // A trivial path type might be refering to a generic value (e.g. `Foo<T,N>` where `N` is a const generic)
                if (t.mData.is_Path() && t.mData.as_Path()->is_trivial()) {
                    auto p = t.mData.as_Path()->cls.as_Relative();
                    // If type lookup fails
                    auto new_path = context.lookup_opt(p.nodes[0].name(), p.hygiene, Context::LookupMode::Type);
                    if (new_path == AST::Path()) {
                        // Try (constant) value lookup
                        auto new_path = context.lookup_opt(p.nodes[0].name(), p.hygiene, Context::LookupMode::Constant);
                        if (new_path != AST::Path()) {
                            // If that lookup succeeds, then create a value (and visit it - just in case)
                            ent = AST::PathParamEnt::make_Value(new AST::ExprNodeNamedValue(std::move(new_path)));
                            ResolveAbsoluteExprNode(context, *ent.as_Value());
                        } else {
                            // Otherwise, visit (which will most likely fail)
                            ResolveAbsoluteType(context, t);
                        }
                    } else {
                        // Normal type, update it then visit
                        *t.mData.as_Path() = std::move(new_path);
                        ResolveAbsoluteType(context, t);
                    }
                } else {
                    ResolveAbsoluteType(context, t);
                }
            }
            TU_ARMA(Value, n) {
                ResolveAbsoluteExprNode(context, *n);
            }
            TU_ARMA(AssociatedTyEqual, a) {
                ResolveAbsolutePathParams(context, sp, a.first.args());
                ResolveAbsoluteType(context, a.second);
            }
            TU_ARMA(AssociatedTyBound, a) {
                ResolveAbsolutePathParams(context, sp, a.first.args());
                for (auto& p : a.second) {
                    ResolveAbsolutePath(context, sp, Context::LookupMode::Type, p);
                }
            }
        }
    }
}

void ResolveAbsolutePathNodes(/*const*/ Context& context, const Span& sp, ::std::vector<::AST::PathNode>& nodes) {
    for (auto& node : nodes) {
        ResolveAbsolutePathParams(context, sp, node.args());
    }
}

void ResolveAbsolutePathBindUFCS(Context& context, const Span& sp, Context::LookupMode mode, ::AST::Path& path) {
    while (path.cls.as_UFCS().nodes.size() > 1) {
        // More than one node, break into inner UFCS
        // - Since traits can't be associated items, this will always be the same form

        auto span = path.cls.as_UFCS().type->span();
        auto nodes = mv$(path.cls.as_UFCS().nodes);
        auto inner_path = mv$(path);
        inner_path.cls.as_UFCS().nodes.push_back(mv$(nodes.front()));
        nodes.erase(nodes.begin());
        path = ::AST::Path::new_ufcs_ty(TypeRef(span, mv$(inner_path)), mv$(nodes));
    }

    if (path.cls.as_UFCS().type) {
        ResolveAbsoluteType(context, *path.cls.as_UFCS().type);
    }

    const auto& ufcs = path.cls.as_UFCS();
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
        const auto& pb = ufcs.trait->mBindings.type.binding;
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
                            path.mBindings.value.set(ufcs.trait->mBindings.type.path + item.name, AST::PathBindingValue::make_Function({&e}));
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
        np.mBindings = path.mBindings.clone();
        return np;
    }

    AST::Path split_into_ufcs_ty(const Span& sp, const AST::Path& path, unsigned int i /*item_name_idx*/) {
        const auto& path_abs = path.cls.as_Absolute();
        auto type_path = ::AST::Path(path);
        type_path.cls.as_Absolute().nodes.resize(i + 1);
        //Resolve_Absolute_Path(

        auto new_path = ::AST::Path::new_ufcs_ty(::TypeRef(sp, mv$(type_path)));
        for (unsigned int j = i + 1; j < path_abs.nodes.size(); j++) {
            new_path.nodes().push_back(mv$(path_abs.nodes[j]));
        }

        DEBUG(path << " -> " << new_path);

        return new_path;
    }

    AST::Path split_replace_into_ufcs_path(const Span& sp, AST::Path path, unsigned int i, const AST::Path& ty_path_tpl) {
        auto& path_abs = path.cls.as_Absolute();
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

    void ResolveAbsolutePathBindAbsoluteHirFromImport(Context& context, const Span& sp, bool is_value, AST::Path& path, const ::HIR::SimplePath& p) {
        TRACE_FUNCTION_FR("path=" << path << ", p=" << p, path);
        if (p.crate_name() == CRATE_BUILTINS) {
            AST::Path rv(p.crate_name(), {});
            rv.nodes().reserve(p.components().size());
            for (const auto& c : p.components()) {
                rv.nodes().push_back(AST::PathNode(c));
            }
            rv.nodes().back().args() = mv$(path.nodes().back().args());
            auto ap = sp_to_ap(p);

            if (coretypeFromstring(p.components().back().c_str()) != CORETYPE_INVAL) {
                rv.mBindings.type.set(ap, AST::PathBindingType::make_TypeAlias({nullptr}));
            } else {
                rv.mBindings.macro.set(ap, AST::PathBindingMacro::make_MacroRules({nullptr}));
            }
            path = mv$(rv);
            return;
        }
        const auto& ext_crate = context.crate.externCrates.at(p.crate_name());
        const ::HIR::Module* hmod = &ext_crate.hir->rootModule;
        for (unsigned int i = 0; i < p.components().size() - 1; i++) {
            const auto& name = p.components()[i];
            auto it = hmod->modItems.find(name);
            if (it == hmod->modItems.end()) {
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
                    auto var_idx = e.findVariant(varname);
                    ASSERT_BUG(sp, var_idx != SIZE_MAX, "Extern crate import path points to non-present variant - " << p);

                    // Construct output path (with same set of parameters)
                    AST::Path rv(p.crate_name(), {});
                    rv.nodes().reserve(p.components().size());
                    for (const auto& c : p.components()) {
                        rv.nodes().push_back(AST::PathNode(c));
                    }
                    rv.nodes().back().args() = mv$(path.nodes().back().args());
                    auto ap = sp_to_ap(p);
                    if (e.mData.is_Data() && e.mData.as_Data()[var_idx].is_struct) {
                        rv.mBindings.type.set(ap, ::AST::PathBindingType::make_EnumVar({nullptr, static_cast<unsigned>(var_idx), &e}));
                    } else {
                        rv.mBindings.value.set(ap, ::AST::PathBindingValue::make_EnumVar({nullptr, static_cast<unsigned>(var_idx), &e}));
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
            auto it = hmod->valueItems.find(name);
            if (it == hmod->valueItems.end()) {
                ERROR(sp, E0000, "Couldn't find final component of " << p);
            }
            AST::PathBindingValue pbv;
            TU_MATCH_HDRA( (it->second->ent), {)
            TU_ARMA(Import, e) {
                    // Wait? is this even valid?
                    BUG(sp, "HIR Import item pointed to an import");
                }
                TU_ARMA(Constant, e) {
                    pbv = ::AST::PathBindingValue::make_Static({nullptr, nullptr});
                }
                TU_ARMA(Static, e) {
                    pbv = ::AST::PathBindingValue::make_Static({nullptr, &e});
                }
                TU_ARMA(StructConstant, e) {
                    pbv = ::AST::PathBindingValue::make_Struct({nullptr, &ext_crate.hir->getTypeitemByPath(sp, e.ty, true).as_Struct()});
                }
                TU_ARMA(Function, e) {
                    pbv = ::AST::PathBindingValue::make_Function({nullptr /*, &e*/});
                }
                TU_ARMA(StructConstructor, e) {
                    pbv = ::AST::PathBindingValue::make_Struct({nullptr, &ext_crate.hir->getTypeitemByPath(sp, e.ty, true).as_Struct()});
                }
            }
            pb.value.set( ::std::move(ap), ::std::move(pbv) );
        } else {
            auto it = hmod->modItems.find(name);
            if (it == hmod->modItems.end()) {
                ERROR(sp, E0000, "Couldn't find final component of " << p);
            }
            AST::PathBindingType pbt;
            TU_MATCH_HDRA( (it->second->ent), {)
            TU_ARMA(Import, e) {
                    // Wait? is this even valid?
                    BUG(sp, "HIR Import item pointed to an import");
                }
                TU_ARMA(Module, e) {
                    pbt = ::AST::PathBindingType::make_Module({nullptr, {&ext_crate, &e}});
                }
                TU_ARMA(Trait, e) {
                    pbt = ::AST::PathBindingType::make_Trait({nullptr, &e});
                }
                TU_ARMA(TraitAlias, e) {
                    pbt = ::AST::PathBindingType::make_TraitAlias({nullptr, &e});
                }
                TU_ARMA(TypeAlias, e) {
                    pbt = ::AST::PathBindingType::make_TypeAlias({nullptr /*, &e*/});
                }
                TU_ARMA(ExternType, e) {
                    pbt = ::AST::PathBindingType::make_TypeAlias({nullptr /*, &e*/});
                }
                TU_ARMA(Struct, e) {
                    pbt = ::AST::PathBindingType::make_Struct({nullptr, &e});
                }
                TU_ARMA(Union, e) {
                    pbt = ::AST::PathBindingType::make_Union({nullptr, &e});
                }
                TU_ARMA(Enum, e) {
                    pbt = ::AST::PathBindingType::make_Enum({nullptr, &e});
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
        rv.mBindings = mv$(pb);
        path = mv$(rv);
    }

    void ResolveAbsolutePathBindAbsoluteHirFrom(Context& context, const Span& sp, Context::LookupMode& mode, ::AST::Path& path, const AST::ExternCrate& crate, unsigned int start) {
        assert(crate.hir->crateName == crate.mName);
        TRACE_FUNCTION_FR(crate.hir->crateName << " - " << path << " start=" << start, path);
        auto& path_abs = path.cls.as_Absolute();

        if (path_abs.nodes.empty()) {
            switch (mode) {
                case Context::LookupMode::Namespace:
                    path.mBindings.type.set({crate.mName, {}}, ::AST::PathBindingType::make_Module({nullptr, {&crate, &crate.hir->rootModule}}));
                    return;
                default:
                    TODO(sp, "Looking up a non-namespace, but pointed to crate root");
            }
        }

        const ::HIR::Module* hmod = &crate.hir->rootModule;
        for (unsigned int i = start; i < path_abs.nodes.size() - 1; i++) {
            auto& n = path_abs.nodes[i];
            assert(hmod);
            auto it = hmod->modItems.find(n.name());
            if (it == hmod->modItems.end()) {
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
                    ResolveAbsolutePathBindAbsolute(context, sp, mode, path);
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
                    AST::AbsolutePath ap(crate.mName, {});
                    for (unsigned int j = start; j <= i; j++) {
                        ap.nodes.push_back(path_abs.nodes[j].name());
                    }
                    AST::PathParams pp;
                    if (!n.args().is_empty()) {
                        pp = mv$(n.args());
                    } else {
                        for (const auto& typ : e.mParams.types) {
                            (void)typ;
                            pp.entries.push_back(::TypeRef(sp));
                        }
                    }
                    AST::Path trait_path(ap, std::move(pp));
                    trait_path.mBindings.type.set(::std::move(ap), ::AST::PathBindingType::make_Trait({nullptr, &e}));

                    ::AST::Path new_path;
                    const auto& next_node = path_abs.nodes[i + 1];
                    // If the named item can't be found in the trait, fall back to it being a type binding
                    // - What if this item is from a nested trait?
                    bool found = false;
                    switch (i + 1 < path_abs.nodes.size() ? Context::LookupMode::Namespace : mode) {
                        case Context::LookupMode::Namespace:
                        case Context::LookupMode::Type:
                        case Context::LookupMode::PatternType:
                            found = (e.types.find(next_node.name()) != e.types.end());
                        case Context::LookupMode::PatternValue:
                        case Context::LookupMode::Constant:
                        case Context::LookupMode::Variable:
                            found = (e.values.find(next_node.name()) != e.values.end());
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
                    return ResolveAbsolutePathBindUFCS(context, sp, mode, path);
                }
                case ::HIR::TypeItem::TAG_ExternType:
                case ::HIR::TypeItem::TAG_TypeAlias:
                case ::HIR::TypeItem::TAG_Struct:
                case ::HIR::TypeItem::TAG_Union:
                    path = split_into_crate(sp, mv$(path), start, crate.mName);
                    path = split_into_ufcs_ty(sp, mv$(path), i - start);
                    return ResolveAbsolutePathBindUFCS(context, sp, mode, path);
                    TU_ARMA(Enum, e) {
                        if (i + 1 < path_abs.nodes.size()) {
                            auto& next_node = path_abs.nodes[i + 1];
                            // If this refers to an enum variant, return the full path
                            // - Otherwise, assume it's an associated type?
                            auto idx = e.findVariant(next_node.name());
                            if (idx != SIZE_MAX) {
                                if (i != path_abs.nodes.size() - 2) {
                                    ERROR(sp, E0000, "Unexpected enum in path " << path);
                                }

                                AST::AbsolutePath ap(crate.mName, {});
                                auto trait_path = ::AST::Path(crate.mName, {});
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

                                if (e.mData.is_Data() && e.mData.as_Data()[idx].is_struct) {
                                    path.mBindings.type.set(ap, ::AST::PathBindingType::make_EnumVar({nullptr, static_cast<unsigned int>(idx), &e}));
                                } else {
                                    path.mBindings.value.set(ap, ::AST::PathBindingValue::make_EnumVar({nullptr, static_cast<unsigned int>(idx), &e}));
                                }
                                path = split_into_crate(sp, mv$(path), start, crate.mName);
                                return;
                            }
                        }
                        path = split_into_crate(sp, mv$(path), start, crate.mName);
                        path = split_into_ufcs_ty(sp, mv$(path), i - start);
                        return ResolveAbsolutePathBindUFCS(context, sp, mode, path);
                    }
            }
        }

        AST::AbsolutePath ap(crate.mName, {});
        auto trait_path = ::AST::Path(crate.mName, {});
        for (unsigned int j = start; j < path_abs.nodes.size(); j++) {
            ap.nodes.push_back(path_abs.nodes[j].name());
        }

        const auto& name = path_abs.nodes.back().name();
        switch (mode) {
            // TODO: Don't bind to a Module if LookupMode::Type
            case Context::LookupMode::Namespace:
            case Context::LookupMode::Type:
            case Context::LookupMode::PatternType: {
                auto v = hmod->modItems.find(name);
                if (v != hmod->modItems.end()) {
                    ::AST::PathBindingType pbt;
                    TU_MATCH_HDRA( (v->second->ent), {)
                    TU_ARMA(Import, e) {
                            DEBUG("= Import " << e.path);
                            ResolveAbsolutePathBindAbsoluteHirFromImport(context, sp, false, path, e.path);
                            return;
                        }
                        TU_ARMA(Trait, e) {
                            pbt = ::AST::PathBindingType::make_Trait({nullptr, &e});
                        }
                        TU_ARMA(TraitAlias, e) {
                            pbt = ::AST::PathBindingType::make_TraitAlias({nullptr, &e});
                        }
                        TU_ARMA(Module, e) {
                            pbt = ::AST::PathBindingType::make_Module({nullptr, {&crate, &e}});
                        }
                        TU_ARMA(ExternType, e) {
                            pbt = ::AST::PathBindingType::make_TypeAlias({nullptr /*, &e*/});
                        }
                        TU_ARMA(TypeAlias, e) {
                            pbt = ::AST::PathBindingType::make_TypeAlias({nullptr /*, &e*/});
                        }
                        TU_ARMA(Enum, e) {
                            pbt = ::AST::PathBindingType::make_Enum({nullptr, &e});
                        }
                        TU_ARMA(Struct, e) {
                            pbt = ::AST::PathBindingType::make_Struct({nullptr, &e});
                        }
                        TU_ARMA(Union, e) {
                            pbt = ::AST::PathBindingType::make_Union({nullptr, &e});
                        }
                    }
                    path.mBindings.type.set(::std::move(ap), ::std::move(pbt));
                    // Update path (trim down to `start` and set crate name)
                    path = split_into_crate(sp, mv$(path), start,  crate.mName);
                    return ;
                }
            } break;

            case Context::LookupMode::PatternValue: {
                auto v = hmod->valueItems.find(name);
                if (v != hmod->valueItems.end()) {
                    TU_MATCH_HDRA( (v->second->ent), {)
                    default:
                        DEBUG("Ignore - " << v->second->ent.tag_str());
                        TU_ARMA(StructConstant, e) {
                            auto ty_path = e.ty;
                            path.mBindings.value.set(::std::move(ap), ::AST::PathBindingValue::make_Struct({nullptr, &crate.hir->getStructByPath(sp, ty_path)}));
                            path = split_into_crate(sp, mv$(path), start, crate.mName);
                            return;
                        }
                        TU_ARMA(Import, e) {
                            ResolveAbsolutePathBindAbsoluteHirFromImport(context, sp, true, path, e.path);
                            return;
                        }
                        TU_ARMA(Constant, e) {
                            // Bind and update path
                            path.mBindings.value.set(::std::move(ap), ::AST::PathBindingValue::make_Static({nullptr, nullptr}));
                            path = split_into_crate(sp, mv$(path), start, crate.mName);
                            return;
                        }
                    }
                } else {
                    DEBUG("No value item for " << name);
                }
            } break;
            case Context::LookupMode::Constant:
            case Context::LookupMode::Variable: {
                auto v = hmod->valueItems.find(name);
                if (v != hmod->valueItems.end()) {
                    ::AST::PathBindingValue pbv;
                    TU_MATCH_HDRA( (v->second->ent), {)
                    TU_ARMA(Import, e) {
                            ResolveAbsolutePathBindAbsoluteHirFromImport(context, sp, true, path, e.path);
                            return;
                        }
                        TU_ARMA(Function, e) {
                            pbv = ::AST::PathBindingValue::make_Function({nullptr /*, &e*/});
                        }
                        TU_ARMA(StructConstructor, e) {
                            auto ty_path = e.ty;
                            pbv = ::AST::PathBindingValue::make_Struct({nullptr, &crate.hir->getStructByPath(sp, ty_path)});
                        }
                        TU_ARMA(StructConstant, e) {
                            auto ty_path = e.ty;
                            pbv = ::AST::PathBindingValue::make_Struct({nullptr, &crate.hir->getStructByPath(sp, ty_path)});
                        }
                        TU_ARMA(Static, e) {
                            pbv = ::AST::PathBindingValue::make_Static({nullptr, &e});
                        }
                        TU_ARMA(Constant, e) {
                            // Bind
                            pbv = ::AST::PathBindingValue::make_Static({nullptr, nullptr});
                        }
                    }
                    path.mBindings.value.set(::std::move(ap), ::std::move(pbv));
                    path = split_into_crate(sp, mv$(path), start,  crate.mName);
                    return ;
                }
            } break;
        }
        ERROR(sp, E0000, "Couldn't find " << Context::lookup_mode_msg(mode) << " '" << path_abs.nodes.back().name() << "' of " << path);
    }
}

void ResolveAbsolutePathBindAbsolute(Context& context, const Span& sp, Context::LookupMode& mode, ::AST::Path& path) {
    TRACE_FUNCTION_FR("path = " << path, path);
    auto& path_abs = path.cls.as_Absolute();

    if (path_abs.crate == "#intrinsics") {
        AST::AbsolutePath ap{path_abs.crate, {}};
        for (const auto& n : path.nodes()) {
            ap.nodes.push_back(n.name());
        }
        path.mBindings.value.set(std::move(ap), AST::PathBindingValue::make_Function({nullptr}));
        return;
    } else if (path_abs.crate == CRATE_BUILTINS) {
        ASSERT_BUG(sp, path.mBindings.hasBinding(), "");
        return;
    } else if (path_abs.crate != "" && path_abs.crate != context.crate.crateNameReal) {
        // TODO: Handle items from other crates (back-converting HIR paths)
        ASSERT_BUG(sp, context.crate.externCrates.count(path_abs.crate), "ERROR: Crate `" << path_abs.crate << "` not loaded");
        ResolveAbsolutePathBindAbsoluteHirFrom(context, sp, mode, path, context.crate.externCrates.at(path_abs.crate), 0);
        return;
    }

    if (path_abs.nodes.empty()) {
        path.mBindings.type.set(AST::AbsolutePath(path_abs.crate, {}), AST::PathBindingType::make_Module({&context.crate.rootModule}));
        return;
    }

    const ::AST::Module* mod = &context.crate.rootModule;
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
            assert(idx < mod->anonMods().size());
            assert(mod->anonMods()[idx]);
            mod = mod->anonMods()[idx].get();
        } else {
            auto it = mod->namespaceItems.find(n.name());
            if (it == mod->namespaceItems.end()) {
                ERROR(sp, E0000, "Couldn't find path component '" << n.name() << "' of " << path);
            }
            const auto& name_ref = it->second;
            DEBUG("#" << i << " \"" << n.name() << "\" = " << name_ref.path << (name_ref.is_import ? " (import)" : ""));

            TU_MATCH_HDRA( (name_ref.path.mBindings.type.binding), {)
            default:
                ERROR(sp, E0000, "Encountered non-namespace item '" << n.name() << "' ("<<name_ref.path<<") in path " << path);
                TU_ARMA(TypeAlias, e) {
                    path = split_replace_into_ufcs_path(sp, mv$(path), i, name_ref.path);
                    return ResolveAbsolutePathBindUFCS(context, sp, mode, path);
                }
                TU_ARMA(Crate, e) {
                    ResolveAbsolutePathBindAbsoluteHirFrom(context, sp, mode, path, *e.crate_, i + 1);
                    return;
                }
                TU_ARMA(Trait, e) {
                    assert(e.trait_ || e.hir);
                    auto trait_path = ::AST::Path(name_ref.path);
                    // HACK! If this was an import, recurse on it to fix paths. (Ideally, all index entries should have the canonical path, but don't currently)
                    if (name_ref.is_import) {
                        auto lm = Context::LookupMode::Type;
                        ResolveAbsolutePathBindAbsolute(context, sp, lm, trait_path);
                    }
                    if (!n.args().is_empty()) {
                        trait_path.nodes().back().args() = mv$(n.args());
                    } else {
                        if (e.trait_) {
                            for (const auto& param : e.trait_->params().mParams) {
                            TU_MATCH_HDRA( (param), {)
                            TU_ARMA(None, e) {
                                    }
                                    TU_ARMA(Lifetime, e) {
                                    }
                                    TU_ARMA(Type, typ) {
                                        trait_path.nodes().back().args().entries.push_back(::TypeRef(sp));
                                    }
                                    TU_ARMA(Value, val) {
                                        //trait_path.nodes().back().args().m_entries.push_back( ::TypeRef(sp) );
                                    }
                            }
                            }
                        } else {
                            for (const auto& typ : e.hir->mParams.types) {
                                (void)typ;
                                trait_path.nodes().back().args().entries.push_back(::TypeRef(sp));
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
                                found = (e.hir->values.count(item_name) != 0);
                                break;
                            case Context::LookupMode::Namespace:
                            case Context::LookupMode::Type:
                            case Context::LookupMode::PatternType:
                                found = (e.hir->types.count(item_name) != 0);
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
                    return ResolveAbsolutePathBindUFCS(context, sp, mode, path);
                }
                TU_ARMA(Enum, e) {
                    if (name_ref.is_import) {
                        auto newpath = name_ref.path;
                        for (unsigned int j = i + 1; j < path_abs.nodes.size(); j++) {
                            newpath.nodes().push_back(mv$(path_abs.nodes[j]));
                        }
                        path = mv$(newpath);
                        //TOOD: Recursion limit
                        ResolveAbsolutePathBindAbsolute(context, sp, mode, path);
                        return;
                    } else {
                        assert(e.enum_);
                        auto& last_node = path_abs.nodes.back();
                        for (const auto& var : e.enum_->variants()) {
                            if (var.mName == last_node.name()) {
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

                                DEBUG("Bound to enum variant '" << var.mName << "' (#" << idx << ")");
                                auto ap = name_ref.path.mBindings.type.path + var.mName;
                                if (var.mData.is_Struct()
                                    || mode == Context::LookupMode::Type
                                    || mode == Context::LookupMode::Namespace
                                    || mode == Context::LookupMode::PatternType) {
                                    path.mBindings.type.set(ap, AST::PathBindingType::make_EnumVar({e.enum_, idx}));
                                } else {
                                    path.mBindings.value.set(ap, AST::PathBindingValue::make_EnumVar({e.enum_, idx}));
                                }
                                return;
                            }
                        }

                        path = split_replace_into_ufcs_path(sp, mv$(path), i, name_ref.path);
                        return ResolveAbsolutePathBindUFCS(context, sp, mode, path);
                    }
                }
                TU_ARMA(Struct, e) {
                    path = split_replace_into_ufcs_path(sp, mv$(path), i, name_ref.path);
                    return ResolveAbsolutePathBindUFCS(context, sp, mode, path);
                }
                TU_ARMA(Union, e) {
                    path = split_replace_into_ufcs_path(sp, mv$(path), i, name_ref.path);
                    return ResolveAbsolutePathBindUFCS(context, sp, mode, path);
                }
                TU_ARMA(Module, e) {
                    if (name_ref.is_import) {
                        auto newpath = name_ref.path;
                        for (unsigned int j = i + 1; j < path_abs.nodes.size(); j++) {
                            newpath.nodes().push_back(mv$(path_abs.nodes[j]));
                        }
                        DEBUG("- Module import, " << path << " => " << newpath);
                        path = mv$(newpath);
                        ResolveAbsolutePathBindAbsolute(context, sp, mode, path);
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
    ASSERT_BUG(sp, tmp.mBindings.hasBinding(), "Lookup for " << path << " succeeded, but had no binding");

    // Replaces the path with the one returned by `lookup_in_mod`, ensuring that `use` aliases are eliminated
    DEBUG("Replace " << path << " with " << tmp);
    auto args = mv$(path.nodes().back().args());
    if (tmp != path) {
        // If the paths mismatch (i.e. there was an import involved), pass through resolution again
        // - This works around cases where the index contains paths that refer to aliases.
        DEBUG("- Recurse");
        ResolveAbsolutePathBindAbsolute(context, sp, mode, tmp);
    }
    tmp.nodes().back().args() = mv$(args);
    path = mv$(tmp);
}

void ResolveAbsolutePath(/*const*/ Context& context, const Span& sp, Context::LookupMode mode, ::AST::Path& path) {
    TRACE_FUNCTION_FR("mode = " << mode << ", path = " << path, path);

    TU_MATCH_HDRA( (path.cls), {)
    TU_ARMA(Invalid, e) {
            BUG(sp, "Attempted resolution of invalid path");
        }
        TU_ARMA(Local, e) {
            // Nothing to do (TODO: Check that it's valid?)
            if (mode == Context::LookupMode::Variable) {
                auto idx = context.lookup_local(sp, e.name, mode);
                if (idx >= FLAG_CONST_GENERIC) {
                    path.mBindings.value.set({}, ::AST::PathBindingValue::make_Generic({idx - FLAG_CONST_GENERIC}));
                } else {
                    path.mBindings.value.set({}, ::AST::PathBindingValue::make_Variable({idx}));
                }
            } else if (mode == Context::LookupMode::Type) {
                path.bindVariable(context.lookup_local(sp, e.name, mode));
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
                if (!p.cls.is_Local() && coretypeFromstring(e.nodes[0].name().c_str()) != CORETYPE_INVAL) {
                    if (const auto* pep = p.mBindings.type.binding.opt_Module()) {
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
                                    if (mod.modItems.find(name) != mod.modItems.end()) {
                                        found = true;
                                    }
                                    break;
                                case Context::LookupMode::PatternValue:
                                    TODO(sp, "Check " << p << " for an item named " << name << " (Pattern)");
                                case Context::LookupMode::Constant:
                                case Context::LookupMode::Variable:
                                    if (mod.valueItems.find(name) != mod.valueItems.end()) {
                                        found = true;
                                    }
                                    break;
                            }
                        } else {
                            const auto& mod = *pe.module_;
                            switch (e.nodes.size() == 2 ? mode : Context::LookupMode::Namespace) {
                                case Context::LookupMode::Namespace:
                                    if (mod.namespaceItems.find(name) != mod.namespaceItems.end()) {
                                        found = true;
                                    }
                                case Context::LookupMode::Type:
                                case Context::LookupMode::PatternType:
                                    if (mod.namespaceItems.find(name) != mod.namespaceItems.end()) {
                                        found = true;
                                    }
                                    break;
                                case Context::LookupMode::PatternValue:
                                    TODO(sp, "Check " << p << " for an item named " << name << " (Pattern)");
                                case Context::LookupMode::Constant:
                                case Context::LookupMode::Variable:
                                    if (mod.valueItems.find(name) != mod.valueItems.end()) {
                                        found = true;
                                    }
                                    break;
                            }
                        }
                        if (!found) {
                            auto ct = coretypeFromstring(e.nodes[0].name().c_str());
                            p = ::AST::Path::new_ufcs_ty(TypeRef(Span(), ct), ::std::vector<::AST::PathNode>());
                        }

                        DEBUG("Primitive module hack yeilded " << p);
                    }
                }

                if (e.nodes.size() > 1) {
                    // Only primitive types turn `Local` paths
                    if (p.cls.is_Local()) {
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
                    p.mBindings = ::AST::Path::Bindings{};
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
                ResolveAbsolutePathNodes(context, sp, path.nodes());
            }
        }
        TU_ARMA(Self, e) {
            DEBUG("- Self");
            const auto& mp_nodes = context.mMod.path().nodes;
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
                ResolveAbsolutePathNodes(context, sp, np_nodes);
            }

            path = mv$(np);
        }
        TU_ARMA(Super, e) {
            DEBUG("- Super");
            // - Determine how many components of the `self` path to use
            const auto& mp_nodes = context.mMod.path().nodes;
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
                ResolveAbsolutePathNodes(context, sp, np_nodes);
            }

            path = mv$(np);
        }
        TU_ARMA(Absolute, e) {
            DEBUG("- Absolute");
            // HACK: if the crate name starts with `=` it's a 2018 absolute path (references a crate loaded with `--extern`)
            if (/*context.m_crate.m_edition >= AST::Edition::Rust2018 &&*/ e.crate.c_str()[0] == '=') {
                // Absolute paths in 2018 edition are crate-prefixed?
                auto ecIt = AST::gImplicitCrates.find(e.crate.c_str() + 1);
                if (ecIt == AST::gImplicitCrates.end()) {
                    ERROR(sp, E0000, "Unable to find external crate for path " << path);
                }
                e.crate = ecIt->second;
            }
            // HACK: If this is `crate::foo::bar`, and `foo` doesn't exist in the root, but it is an implicit crate, then resolve to that
            // - This handles when a 2015 macro resolves to `::cratename::Bar` in a 2018+ crate
            else if (e.crate == "" && e.nodes.size() > 1 && context.crate.rootModule.namespaceItems.count(e.nodes.front().name()) == 0) {
                auto ecIt = AST::gImplicitCrates.find(e.nodes.front().name().c_str());
                if (ecIt != AST::gImplicitCrates.end()) {
                    e.crate = ecIt->second;
                    e.nodes.erase(e.nodes.begin());
                }
            }
            // Nothing to do (TODO: Bind?)
            ResolveAbsolutePathNodes(context, sp, e.nodes);
        }
        TU_ARMA(UFCS, e) {
            DEBUG("- UFCS");
            ResolveAbsoluteType(context, *e.type);
            if (e.trait && *e.trait != ::AST::Path()) {
                ResolveAbsolutePath(context, sp, Context::LookupMode::Type, *e.trait);
            }

            ResolveAbsolutePathNodes(context, sp, e.nodes);
        }
    }

    DEBUG("path = " << path);
    // TODO: Should this be deferred until the HIR?
    // - Doing it here so the HIR lowering has a bit more information
    // - Also handles splitting "absolute" paths into UFCS
    TU_MATCH_HDRA((path.cls), {)
    default:
        BUG(sp, "Path wasn't absolutised correctly");
        TU_ARMA(Local, e) {
            if (!path.mBindings.hasBinding()) {
                TODO(sp, "Bind unbound local path - " << path);
            }
        }
        TU_ARMA(Absolute, e) {
            ResolveAbsolutePathBindAbsolute(context, sp, mode, path);
        }
        TU_ARMA(UFCS, e) {
            ResolveAbsolutePathBindUFCS(context, sp, mode, path);
        }
    }

    // TODO: Expand default type parameters?
    // - Helps with cases like PartialOrd<Self>, but hinders when the default is a hint (in expressions)

    //
    if(const auto* e = path.cls.opt_UFCS())
    {
        if (!e->nodes.empty() && (!e->trait || !e->trait->is_valid()) && e->type->mData.is_Generic() && e->type->mData.as_Generic().index == GENERICSelf) {
            const auto& name = e->nodes.front().name();

            if (const auto* self_ty = context.getSelfOpt()) {
                // Check if we're in an enum
                if (const auto* ty_path = self_ty->mData.opt_Path()) {
                    const auto& p = **ty_path;
                    if (const auto* pbe = p.mBindings.type.binding.opt_Enum()) {
                        if (pbe->enum_) {
                            const auto& enm = *pbe->enum_;
                            auto it = std::find_if(enm.variants().begin(), enm.variants().end(), [&](const AST::EnumVariant& v) {
                                return v.mName == name;
                            });
                            if (it != enm.variants().end()) {
                                unsigned idx = it - enm.variants().begin();
                                auto p2 = p.mBindings.type.path + name;
                                auto new_path = std::move(p);
                                new_path.append(name);
                                if (it->mData.is_Struct()) {
                                    new_path.mBindings.type.set(p2, AST::PathBindingType::make_EnumVar({&enm, idx}));
                                } else {
                                    new_path.mBindings.value.set(p2, AST::PathBindingValue::make_EnumVar({&enm, idx}));
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

void ResolveAbsoluteLifetime(Context& context, const Span& sp, AST::LifetimeRef& lft) {
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

        for (auto it = context.nameContext.rbegin(); it != context.nameContext.rend(); ++it) {
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
            if (context.iblTargetGenerics) {
                DEBUG("Considering in-band-lifetimes");
                ASSERT_BUG(sp, !context.nameContext.empty(), "Name context stack is empty");
                auto it = context.nameContext.rbegin();
                ASSERT_BUG(sp, it->is_Generic(), "Name context stack end not Generic, instead " << it->tag_str());
                while (it->as_Generic().level == GenericSlot::Level::Hrb) {
                    it++;
                    ASSERT_BUG(sp, it != context.nameContext.rend(), "");
                    ASSERT_BUG(sp, it->is_Generic(), "Name context stack end not Generic, instead " << it->tag_str());
                }
                if (it->as_Generic().level != GenericSlot::Level::Hrb) {
                    auto& contextGen = it->as_Generic();
                    auto& defGen = *context.iblTargetGenerics;
                    auto level = contextGen.level;
                    // 1. Assert that the last item of `context.m_name_context` is Generic, and matches `m_ibl_target_generics`
                    ASSERT_BUG(sp, contextGen.lifetimes.size() + contextGen.types.size() + contextGen.constants.size() == defGen.mParams.size(), "");
                    // 2. Add the new lifetime to both `m_ibl_target_generics` and the last entry in m_name_context
                    size_t idx = contextGen.lifetimes.size();
                    defGen.addLftParam(AST::LifetimeParam(sp, {}, lft.name()));
                    contextGen.lifetimes.push_back(NamedI<GenericSlot>{lft.name(), GenericSlot{level, static_cast<unsigned short>(idx)}});
                    lft.set_binding(idx | (static_cast<int>(level) << 8));
                    return;
                }
            }
        }
        ERROR(sp, E0000, "Couldn't find lifetime " << lft);
    }
}

void ResolveAbsoluteType(Context& context, TypeRef& type) {
    TRACE_FUNCTION_FR("type = " << type, "type = " << type);
    const auto& sp = type.span();

    if (type.mData.is_Path() && type.mData.as_Path()->mBindings.type.binding.is_TypeParameter()) {
        auto& e = type.mData.as_Path()->mBindings.type.binding.as_TypeParameter();
        type.mData = TypeData::make_Generic({type.mData.as_Path()->asTrivial(), e.slot});
    }

    TU_MATCH_HDRA( (type.mData), {)
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
            ResolveAbsoluteType(context, *e.info.mRettype);
            for (auto& t : e.info.argTypes) {
                ResolveAbsoluteType(context, t);
            }
            context.pop(e.info.hrbs);
        }
        TU_ARMA(Tuple, e) {
            for (auto& t : e.inner_types) {
                ResolveAbsoluteType(context, t);
            }
        }
        TU_ARMA(Borrow, e) {
            ResolveAbsoluteLifetime(context, type.span(), e.lifetime);
            ResolveAbsoluteType(context, *e.inner);
        }
        TU_ARMA(Pointer, e) {
            ResolveAbsoluteType(context, *e.inner);
        }
        TU_ARMA(Array, e) {
            ResolveAbsoluteType(context, *e.inner);
            if (e.size) {
                auto _h = context.enterRootblock();
                ResolveAbsoluteExprNode(context, *e.size);
            }
        }
        TU_ARMA(Slice, e) {
            ResolveAbsoluteType(context, *e.inner);
        }
        TU_ARMA(Generic, e) {
            if (e.name == rcstringSelf) {
                type = context.getSelf();
            } else {
                auto idx = context.lookup_local(type.span(), e.name, Context::LookupMode::Type);
                // TODO: Should this be bound to the relevant index, or just leave as-is?
                e.index = idx;
            }
        }
        TU_ARMA(Path, e) {
            ResolveAbsolutePath(context, type.span(), Context::LookupMode::Type, *e);
            if (auto* ufcs = e->cls.opt_UFCS()) {
                if (ufcs->nodes.size() == 0 /*&& ufcs->trait && *ufcs->trait == ::AST::Path()*/) {
                    auto ty = mv$(*ufcs->type);
                    type = mv$(ty);
                    return;
                }
                assert(ufcs->nodes.size() == 1);
            }

            if (e->mBindings.type.binding.opt_Trait()) {
                auto tp = TypeTraitPath();
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
                ResolveAbsolutePath(context, type.span(), Context::LookupMode::Type, *trait.path);
                context.pop(trait.hrbs);
            }
            for (auto& lft : e.lifetimes) {
                ResolveAbsoluteLifetime(context, type.span(), lft);
            }
        }
        TU_ARMA(ErasedType, e) {
            for (auto& trait : e->traits) {
                context.push(trait.hrbs);
                ResolveAbsolutePath(context, type.span(), Context::LookupMode::Type, *trait.path);
                context.pop(trait.hrbs);
            }
            for (auto& trait : e->maybe_traits) {
                context.push(trait.hrbs);
                ResolveAbsolutePath(context, type.span(), Context::LookupMode::Type, *trait.path);
                context.pop(trait.hrbs);
            }
            for (auto& lft : e->lifetimes) {
                ResolveAbsoluteLifetime(context, type.span(), lft);
            }
            if (e->use) {
                ResolveAbsolutePathParams(context, type.span(), *e->use);
            }
        }
    }
}

void ResolveAbsoluteExpr(Context& context, ::AST::Expr& expr) {
    if (expr.is_valid()) {
        ResolveAbsoluteExprNode(context, expr.node());
    }
}

void ResolveAbsoluteExprNode(Context& context, ::AST::ExprNode& node) {
    TRACE_FUNCTION_F("");

    struct NV: public AST::NodeVisitorDef {
        Context& context;

        NV(Context& context)
            : context(context)
        {
        }

        void visit(AST::ExprNodeBlock& node) override {
            DEBUG("ExprNode_Block");
            if (node.localMod) {
                auto _h = context.clearRootblock();
                this->context.push(*node.localMod);

                // Clone just the module stack part of the current context
                ResolveAbsoluteMod(this->context.cloneMod(), *node.localMod);
            }
            this->context.push_block();
            for (auto& line : node.nodes) {
                if (const auto* definition = cast<AST::ExprNodeMacroDefinition>(line.node.get())) {
                    this->context.push_macro_definition(
                        definition->definitionId,
                        definition->tokenHygiene,
                        definition->definitionHygiene
                    );
                } else {
                    line.node->visit(*this);
                }
            }
            this->context.pop_block();
            if (node.localMod) {
                this->context.pop(*node.localMod);
            }
        }

        void visit(AST::ExprNodeMatch& node) override {
            DEBUG("ExprNode_Match");
            node.val->visit(*this);
            for (auto& arm : node.arms) {
                this->context.push_block();

                this->context.start_patbind();
                // TODO: Save the context, ensure that each arm results in the same state.
                // - Or just an equivalent state
                // OR! Have a mode in the context that handles multiple bindings.
                for (auto& pat : arm.patterns) {
                    // If this isn't the first pattern, save the newly created bindings, roll back entire state, and check afterwards
                    ResolveAbsolutePattern(this->context, true, pat);
                    this->context.endPatbindArm(pat.span());
                }
                this->context.endPatbind();

                for (auto& cond : arm.guard) {
                    cond.value->visit(*this);
                    if (cond.opt_pat) {
                        this->context.start_patbind();
                        ResolveAbsolutePattern(this->context, true, *cond.opt_pat);
                        this->context.endPatbind();
                    }
                }
                assert(arm.mCode);
                arm.mCode->visit(*this);

                this->context.pop_block();
            }
        }

        void visit(AST::ExprNodeLoop& node) override {
            this->context.push_block();
            node.mCode->visit(*this);
            this->context.pop_block();
        }

        void visit(AST::ExprNodeFor& node) override {
            BUG(node.span(), "`for` should be desugared");
        }

        void visit(AST::ExprNodeWhile& node) override {
            this->context.push_block();
            visit_if_let_conditions(node.conditions);
            node.mCode->visit(*this);
            this->context.pop_block();
        }

        void visit(AST::ExprNodeLetBinding& node) override {
            DEBUG("ExprNode_LetBinding");
            ResolveAbsoluteType(this->context, node.mType);
            AST::NodeVisitorDef::visit(node);
            this->context.start_patbind();
            auto count = this->context.varCount;
            ResolveAbsolutePattern(this->context, node.elseNode ? true : false, node.pat);
            this->context.endPatbind();
            auto n_vars = this->context.varCount - count;
            if (node.elseNode) {
                //auto& vb = this->context.m_name_context.back().as_VarBlock();
                node.letelseSlots = std::make_pair(this->context.varCount, n_vars);
                this->context.varCount += n_vars;
            }
        }

        void visit_if_let_conditions(std::vector<AST::IfLetCondition>& conds) {
            for (auto& cond : conds) {
                // Visit the value first, so it doesn't bind to the newly created variables in the pattern
                cond.value->visit(*this);

                if (cond.opt_pat) {
                    this->context.start_patbind();
                    ResolveAbsolutePattern(this->context, true, *cond.opt_pat);
                    this->context.endPatbindArm(cond.opt_pat->span());
                    this->context.endPatbind();
                }
            }
        }

        void visit(AST::ExprNodeIf& node) override {
            for (auto& arm : node.arms) {
                this->context.push_block();
                visit_if_let_conditions(arm.conditions);
                arm.body->visit(*this);
                this->context.pop_block();
            }
            if (node.elseNode) {
                node.elseNode->visit(*this);
            }
        }

        void visit(AST::ExprNodeStructLiteral& node) override {
            DEBUG("ExprNode_StructLiteral");
            ResolveAbsolutePath(this->context, node.span(), Context::LookupMode::Type, node.mPath);
            AST::NodeVisitorDef::visit(node);
        }

        void visit(AST::ExprNodeStructLiteralPattern& node) override {
            DEBUG("ExprNode_StructLiteralPattern");
            ResolveAbsolutePath(this->context, node.span(), Context::LookupMode::Type, node.mPath);
            AST::NodeVisitorDef::visit(node);
        }

        void visit(AST::ExprNodeCallPath& node) override {
            DEBUG("ExprNode_CallPath");
            ResolveAbsolutePath(this->context, node.span(), Context::LookupMode::Variable, node.mPath);
            AST::NodeVisitorDef::visit(node);
        }

        void visit(AST::ExprNodeCallMethod& node) override {
            DEBUG("ExprNode_CallMethod");
            ResolveAbsolutePathParams(this->context, node.span(), node.method.args());
            AST::NodeVisitorDef::visit(node);
        }

        void visit(AST::ExprNodeNamedValue& node) override {
            DEBUG("(" << node.span() << ") ExprNode_NamedValue - " << node.mPath);
            ResolveAbsolutePath(this->context, node.span(), Context::LookupMode::Variable, node.mPath);
        }

        void visit(AST::ExprNodeCast& node) override {
            DEBUG("ExprNode_Cast");
            ResolveAbsoluteType(this->context, node.mType);
            AST::NodeVisitorDef::visit(node);
        }

        void visit(AST::ExprNodeTypeAnnotation& node) override {
            DEBUG("ExprNode_TypeAnnotation");
            ResolveAbsoluteType(this->context, node.mType);
            AST::NodeVisitorDef::visit(node);
        }

        void visit(AST::ExprNodeClosure& node) override {
            DEBUG("ExprNode_Closure");

            ResolveAbsoluteType(this->context, node.returnType);

            this->context.push_block();
            for (auto& arg : node.mArgs) {
                ResolveAbsoluteType(this->context, arg.second);
                this->context.start_patbind();
                ResolveAbsolutePattern(this->context, false, arg.first);
                this->context.endPatbind();
            }

            node.mCode->visit(*this);

            this->context.pop_block();
        }
    } exprIter(context);

    node.visit(exprIter);
}

void ResolveAbsoluteGeneric(Context& context, ::AST::GenericParams& params) {
    for (auto& param : params.mParams) {
        TU_MATCH_HDRA( (param), {)
        TU_ARMA(None, _) {
            }
            TU_ARMA(Lifetime, param) {
            }
            TU_ARMA(Type, param) {
                ResolveAbsoluteType(context, param.getDefault());
            }
            TU_ARMA(Value, param) {
                ResolveAbsoluteType(context, param.type());
                ResolveAbsoluteExpr(context, param.default_value());
            }
        }
    }
    for (auto& bound : params.bounds) {
        TU_MATCH(::AST::GenericBound, (bound), (e), (None, ), (Lifetime, ResolveAbsoluteLifetime(context, bound.span, e.test); ResolveAbsoluteLifetime(context, bound.span, e.bound);), (TypeLifetime, ResolveAbsoluteType(context, e.type); ResolveAbsoluteLifetime(context, bound.span, e.bound);), (IsTrait, context.push(e.outer_hrbs); ResolveAbsoluteType(context, e.type); context.push(e.inner_hrbs); ResolveAbsolutePath(context, bound.span, Context::LookupMode::Type, e.trait); context.pop(e.inner_hrbs); context.pop(e.outer_hrbs);), (MaybeTrait, ResolveAbsoluteType(context, e.type); ResolveAbsolutePath(context, bound.span, Context::LookupMode::Type, e.trait);), (NotTrait, ResolveAbsoluteType(context, e.type); ResolveAbsolutePath(context, bound.span, Context::LookupMode::Type, e.trait);), (Equality, ResolveAbsoluteType(context, e.type); ResolveAbsoluteType(context, e.replacement);))
    }
}

// Locals shouldn't be possible, as they'd end up as MaybeBind. Will assert the path class.
void ResolveAbsolutePatternValue(/*const*/ Context& context, const Span& sp, ::AST::Pattern::Value& val) {
    TU_IFLET(
        ::AST::Pattern::Value,
        val,
        Named,
        e,
        //assert( ! e.is_trivial() );
        ResolveAbsolutePath(context, sp, Context::LookupMode::Constant, e);
    )
}

void ResolveAbsolutePattern(Context& context, bool allowRefutable, ::AST::Pattern& pat) {
    TRACE_FUNCTION_FR("allow_refutable = " << allowRefutable << ", pat = " << pat, pat);
    for (auto& pb : pat.bindings()) {
        //if( !pat.data().is_Any() && ! allow_refutable )
        //    TODO(pat.span(), "Resolve_Absolute_Pattern - Encountered bound destructuring pattern");
        pb.slot = context.push_var(pat.span(), pb.mName);
        DEBUG("- Binding #" << pb.slot << " '" << pb.mName << "'");
    }

    TU_MATCH_HDRA( (pat.data()), {)
    TU_ARMA(MaybeBind, e) {
            auto name = mv$(e.name);
            // Attempt to resolve the name in the current namespace, and if it fails, it's a binding
            auto p = context.lookup_opt(name.name, name.hygiene, Context::LookupMode::PatternValue);
            if (p.is_valid()) {
                ResolveAbsolutePath(context, pat.span(), Context::LookupMode::PatternValue, p);
                pat.data() = AST::Pattern::Data::make_Value({::AST::Pattern::Value::make_Named(mv$(p)), AST::Pattern::Value()});
                DEBUG("MaybeBind resolved to " << pat);
            } else {
                pat.bindings().push_back(AST::PatternBinding(mv$(name), AST::PatternBinding::Type::MOVE, false));
                pat.bindings().back().slot = context.push_var(pat.span(), pat.bindings().back().mName);
                pat.data() = AST::Pattern::Data::make_Any({});
                DEBUG("- Binding #" << pat.bindings().back().slot << " '" << pat.bindings().back().mName << "' (was MaybeBind)");
            }
        }
        TU_ARMA(Macro, e) {
            BUG(pat.span(), "Resolve_Absolute_Pattern - Encountered Macro - " << pat);
        }
        TU_ARMA(Any, e) {
            // Ignore '_'
        }
        TU_ARMA(Box, e) {
            ResolveAbsolutePattern(context, allowRefutable, *e.sub);
        }
        TU_ARMA(Ref, e) {
            ResolveAbsolutePattern(context, allowRefutable, *e.sub);
        }
        TU_ARMA(Value, e) {
            // Disabled check : Some code does `let (Foo | Bar);` where those are the only options
            //if( ! allow_refutable )
            //{
            //    // TODO: If this is a single value of a unit-like struct, accept
            //    BUG(pat.span(), "Resolve_Absolute_Pattern - Encountered refutable pattern where only irrefutable allowed - " << pat);
            //}
            ResolveAbsolutePatternValue(context, pat.span(), e.start);
            ResolveAbsolutePatternValue(context, pat.span(), e.end);
        }
        TU_ARMA(ValueLeftInc, e) {
            if (!allowRefutable) {
                // TODO: If this is a single value of a unit-like struct, accept
                BUG(pat.span(), "Resolve_Absolute_Pattern - Encountered refutable pattern where only irrefutable allowed - " << pat);
            }
            ResolveAbsolutePatternValue(context, pat.span(), e.start);
            ResolveAbsolutePatternValue(context, pat.span(), e.end);
        }
        TU_ARMA(Tuple, e) {
            for (auto& sp : e.start) {
                ResolveAbsolutePattern(context, allowRefutable, sp);
            }
            for (auto& sp : e.end) {
                ResolveAbsolutePattern(context, allowRefutable, sp);
            }
        }
        TU_ARMA(StructTuple, e) {
            ResolveAbsolutePath(context, pat.span(), Context::LookupMode::Constant, e.path);
            for (auto& sp : e.tup_pat.start) {
                ResolveAbsolutePattern(context, allowRefutable, sp);
            }
            for (auto& sp : e.tup_pat.end) {
                ResolveAbsolutePattern(context, allowRefutable, sp);
            }
        }
        TU_ARMA(Struct, e) {
            // `Struct { .. }` patterns can match anything, so switch lookup mode in that case
            ResolveAbsolutePath(context, pat.span(), e.sub_patterns.empty() ? Context::LookupMode::PatternType : Context::LookupMode::Type, e.path);
            for (auto& sp : e.sub_patterns) {
                ResolveAbsolutePattern(context, allowRefutable, sp.pat);
            }
        }
        TU_ARMA(Slice, e) {
            // NOTE: Can be irrefutable (if the type is array)
            for (auto& sp : e.sub_pats) {
                ResolveAbsolutePattern(context, allowRefutable, sp);
            }
        }
        TU_ARMA(SplitSlice, e) {
            // NOTE: Can be irrefutable (if the type is array)
            for (auto& sp : e.leading) {
                ResolveAbsolutePattern(context, allowRefutable, sp);
            }
            if (e.extraBind.is_valid()) {
                e.extraBind.slot = context.push_var(pat.span(), e.extraBind.mName);
            }
            for (auto& sp : e.trailing) {
                ResolveAbsolutePattern(context, allowRefutable, sp);
            }
        }
        TU_ARMA(Or, e) {
            // TODO: Need to ensure that all arms bind the same set of variables
            context.start_patbind();
            for (auto& sp : e) {
                ResolveAbsolutePattern(context, allowRefutable, sp);
                context.endPatbindArm(sp.span());
            }
            context.endPatbind();
        }
    }
}

// - For traits
void ResolveAbsoluteImplItems(Context& item_context, ::AST::NamedList<::AST::Item>& items) {
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
                ResolveAbsoluteGeneric(item_context, e.mParams);
                ResolveAbsoluteGeneric(item_context, e.selfBounds);

                ResolveAbsoluteType(item_context, e.type());

                item_context.pop(e.params(), true);
            }
            TU_ARMA(Function, e) {
                DEBUG("Function - " << i.name);
                ResolveAbsoluteFunction(item_context, e);
            }
            TU_ARMA(Static, e) {
                DEBUG("Static - " << i.name);
                ResolveAbsoluteType(item_context, e.type());
                auto _h = item_context.enterRootblock();
                ResolveAbsoluteExpr(item_context, e.value());
            }
        }
    }
}

// - For impl blocks
void ResolveAbsoluteImplItems(Context& item_context, ::std::vector<::AST::Impl::ImplItem>& items) {
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
             ResolveAbsoluteGeneric(item_context, e.params());

             ResolveAbsoluteType(item_context, e.type());

             item_context.pop(e.params(), true);),
            (Function, DEBUG("Function - " << i.name); ResolveAbsoluteFunction(item_context, e);),
            (Static, DEBUG("Static - " << i.name); ResolveAbsoluteType(item_context, e.type()); auto _h = item_context.enterRootblock(); ResolveAbsoluteExpr(item_context, e.value());)
        )
    }
}

void ResolveAbsoluteFunction(Context& item_context, ::AST::Function& fcn) {
    TRACE_FUNCTION_F("");
    item_context.push(fcn.params(), GenericSlot::Level::Method);
    item_context.iblTargetGenerics = &fcn.params();
    DEBUG("- Generics");
    ResolveAbsoluteGeneric(item_context, fcn.params());

    DEBUG("- Prototype types");
    ResolveAbsoluteType(item_context, fcn.rettype());
    for (auto& arg : fcn.args()) {
        ResolveAbsoluteType(item_context, arg.ty);
    }
    item_context.iblTargetGenerics = nullptr;

    DEBUG("- Body");
    {
        auto _h = item_context.enterRootblock();
        item_context.push_block();
        for (auto& arg : fcn.args()) {
            item_context.start_patbind();
            ResolveAbsolutePattern(item_context, false, arg.pat);
            item_context.endPatbind();
        }

        ResolveAbsoluteExpr(item_context, fcn.code());

        item_context.pop_block();
    }

    item_context.pop(fcn.params());
}

void ResolveAbsoluteStatic(Context& item_context, ::AST::Static& e) {
    ResolveAbsoluteType(item_context, e.type());
    auto _h = item_context.enterRootblock();
    ResolveAbsoluteExpr(item_context, e.value());
}

void ResolveAbsoluteStruct(Context& item_context, ::AST::Struct& e) {
    item_context.push(e.params(), GenericSlot::Level::Top, true);
    ResolveAbsoluteGeneric(item_context, e.params());

    TU_MATCH(::AST::StructData, (e.mData), (s), (Unit, ), (Tuple, for (auto& field : s.ents) { ResolveAbsoluteType(item_context, field.mType); }), (Struct, for (auto& field : s.ents) {
                 ResolveAbsoluteType(item_context, field.mType);
                 ResolveAbsoluteExpr(item_context, field.defaultValue);
             }))

    item_context.pop(e.params());
}

void ResolveAbsoluteUnion(Context& item_context, ::AST::Union& e) {
    item_context.push(e.mParams, GenericSlot::Level::Top, true);
    ResolveAbsoluteGeneric(item_context, e.mParams);

    for (auto& field : e.mVariants) {
        ResolveAbsoluteType(item_context, field.mType);
    }

    item_context.pop(e.mParams);
}

void ResolveAbsoluteTrait(Context& item_context, ::AST::Trait& e) {
    item_context.push(e.params(), GenericSlot::Level::Top, true);
    ResolveAbsoluteGeneric(item_context, e.params());

    for (auto& lft : e.lifetimes()) {
        ResolveAbsoluteLifetime(item_context, lft.sp, lft.ent);
    }
    for (auto& st : e.supertraits()) {
        if (!st.ent.path->is_valid()) {
            DEBUG("- ST 'static");
        } else {
            DEBUG("- ST " << st.ent.hrbs << *st.ent.path);
            item_context.push(st.ent.hrbs);
            ResolveAbsolutePath(item_context, st.sp, Context::LookupMode::Type, *st.ent.path);
            item_context.pop(st.ent.hrbs);
        }
    }

    ResolveAbsoluteImplItems(item_context, e.items());

    item_context.pop(e.params(), true);
}

void ResolveAbsoluteEnum(Context& item_context, ::AST::Enum& e) {
    item_context.push(e.params(), GenericSlot::Level::Top, true);
    ResolveAbsoluteGeneric(item_context, e.params());

    for (auto& variant : e.variants()) {
        TU_MATCH(::AST::EnumVariantData, (variant.mData), (s), (Unit, ), (Tuple, for (auto& field : s.mItems) { ResolveAbsoluteType(item_context, field.mType); }), (Struct, for (auto& field : s.fields) {
                     ResolveAbsoluteType(item_context, field.mType);
                     ResolveAbsoluteExpr(item_context, field.defaultValue);
                 }))
        auto _h = item_context.enterRootblock();
        ResolveAbsoluteExpr(item_context, variant.discriminantValue);
    }

    item_context.pop(e.params());
}

void ResolveAbsoluteMod(const ::AST::Crate& crate, ::AST::Module& mod) {
    ResolveAbsoluteMod(Context{crate, mod}, mod);
}

void ResolveAbsoluteMod(Context item_context, ::AST::Module& mod) {
    TRACE_FUNCTION_F("mod=" << mod.path());

    for (auto& i : mod.mItems) {
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
                    TU_MATCH_DEF(AST::Item, (i2.data), (e2), (BUG(i->span, "Unexpected item in ExternBlock - " << i2.data.tag_str());), (None, ), (Function, ResolveAbsoluteFunction(item_context, e2);), (Static, ResolveAbsoluteStatic(item_context, e2);))
                }
            }
            TU_ARMA(Impl, e) {
                auto& def = e.def();
                if (!def.type().is_valid()) {
                    TRACE_FUNCTION_F("impl " << def.trait().ent << " for ..");
                    item_context.push(def.params(), GenericSlot::Level::Top);

                    item_context.iblTargetGenerics = &def.params();
                    assert(def.trait().ent.is_valid());
                    ResolveAbsolutePath(item_context, def.trait().sp, Context::LookupMode::Type, def.trait().ent);
                    item_context.iblTargetGenerics = nullptr;

                    ResolveAbsoluteGeneric(item_context, def.params());

                    if (e.items().size() != 0) {
                        ERROR(i->span, E0000, "impl Trait for .. with methods");
                    }

                    item_context.pop(def.params());

                    // HACK: Mutate the source to indicate that it's an auto trait
                    const_cast<::AST::Trait*>(def.trait().ent.mBindings.type.binding.as_Trait().trait_)->set_is_marker();
                } else {
                    TRACE_FUNCTION_F("impl " << def.trait().ent << " for " << def.type());
                    item_context.push_self(def.type());
                    item_context.push(def.params(), GenericSlot::Level::Top);

                    item_context.iblTargetGenerics = &def.params();
                    ResolveAbsoluteType(item_context, def.type());
                    if (def.trait().ent.is_valid()) {
                        ResolveAbsolutePath(item_context, def.trait().sp, Context::LookupMode::Type, def.trait().ent);
                    }
                    item_context.iblTargetGenerics = nullptr;

                    ResolveAbsoluteGeneric(item_context, def.params());

                    ResolveAbsoluteImplItems(item_context, e.items());

                    item_context.pop(def.params());
                    item_context.pop_self(def.type());
                }
            }
            TU_ARMA(NegImpl, e) {
                auto& impl_def = e;
                TRACE_FUNCTION_F("impl ! " << impl_def.trait().ent << " for " << impl_def.type());
                item_context.push_self(impl_def.type());
                item_context.push(impl_def.params(), GenericSlot::Level::Top);

                item_context.iblTargetGenerics = &impl_def.params();
                ResolveAbsoluteType(item_context, impl_def.type());
                if (!impl_def.trait().ent.is_valid()) {
                    BUG(i->span, "Encountered negative impl with no trait");
                }
                ResolveAbsolutePath(item_context, impl_def.trait().sp, Context::LookupMode::Type, impl_def.trait().ent);
                item_context.iblTargetGenerics = nullptr;

                ResolveAbsoluteGeneric(item_context, impl_def.params());

                // No items

                item_context.pop(impl_def.params());
                item_context.pop_self(impl_def.type());
            }
            TU_ARMA(Module, e) {
                DEBUG("Module - " << i->name);
                ResolveAbsoluteMod(item_context.crate, e);
            }
            TU_ARMA(Crate, e) {
                // - Nothing
            }
            TU_ARMA(Enum, e) {
                DEBUG("Enum - " << i->name);
                ResolveAbsoluteEnum(item_context, e);
            }
            TU_ARMA(Trait, e) {
                DEBUG("Trait - " << i->name);
                ResolveAbsoluteTrait(item_context, e);
            }
            TU_ARMA(TraitAlias, e) {
                DEBUG("TraitAlias - " << i->name);
                item_context.push(e.params, GenericSlot::Level::Top, true);
                ResolveAbsoluteGeneric(item_context, e.params);

                for (auto& st : e.traits) {
                    item_context.push(st.ent.hrbs);
                    ResolveAbsolutePath(item_context, st.sp, Context::LookupMode::Type, *st.ent.path);
                    item_context.pop(st.ent.hrbs);
                }

                item_context.pop(e.params, true);
            }
            TU_ARMA(Type, e) {
                DEBUG("Type - " << i->name);
                item_context.push(e.params(), GenericSlot::Level::Top, true);
                ResolveAbsoluteGeneric(item_context, e.params());

                ResolveAbsoluteType(item_context, e.type());

                item_context.pop(e.params(), true);
            }
            TU_ARMA(Struct, e) {
                DEBUG("Struct - " << i->name);
                ResolveAbsoluteStruct(item_context, e);
            }
            TU_ARMA(Union, e) {
                DEBUG("Union - " << i->name);
                ResolveAbsoluteUnion(item_context, e);
            }
            TU_ARMA(Function, e) {
                DEBUG("Function - " << i->name);
                ResolveAbsoluteFunction(item_context, e);
            }
            TU_ARMA(Static, e) {
                DEBUG("Static - " << i->name);
                ResolveAbsoluteStatic(item_context, e);
            }
        }
    }

    // - Run through the indexed items and fix up those paths
    static Span sp;
    DEBUG("Imports (mod = " << mod.path() << ")");
    for (auto& i : mod.namespaceItems) {
        if (i.second.is_import) {
            ResolveAbsolutePath(item_context, sp, Context::LookupMode::Namespace, i.second.path);
        }
    }
    for (auto& i : mod.typeItems) {
        if (i.second.is_import) {
            ResolveAbsolutePath(item_context, sp, Context::LookupMode::Type, i.second.path);
        }
    }
    for (auto& i : mod.valueItems) {
        if (i.second.is_import) {
            ResolveAbsolutePath(item_context, sp, Context::LookupMode::Constant, i.second.path);
        }
    }
}

void ResolveAbsolutise(AST::Crate& crate) {
    ResolveAbsoluteMod(crate, crate.root_module());
}

#undef FLAG_CONST_GENERIC


enum class IndexName {
    Namespace,
    Type,
    Value,
    Macro,
};

void ResolveIndexModuleWildcardUseStmt(AST::Crate& crate, AST::Module& dstMod, const AST::UseItem::Ent& iData, const AST::Visibility& vis);

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

::std::unordered_map<RcString, ::AST::Module::IndexEnt>& getModIndex(::AST::Module& mod, IndexName location) {
    switch (location) {
        case IndexName::Namespace:
            return mod.namespaceItems;
        case IndexName::Type:
            return mod.typeItems;
        case IndexName::Value:
            return mod.valueItems;
        case IndexName::Macro:
            return mod.macroItems;
    }
    throw "";
}

namespace {
    AST::Path hirToAst(const HIR::SimplePath& p) {
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

void _add_item(const Span& sp, AST::Module& mod, IndexName location, const RcString& name, const AST::Visibility& vis, ::AST::Path ir, bool errorOnCollision = true) {
    ASSERT_BUG(sp, ir.mBindings.hasBinding(), "Adding item with no binding - " << ir);
    auto& list = getModIndex(mod, location);

    if (location != IndexName::Namespace) {
        ASSERT_BUG(sp, ir.cls.as_Absolute().nodes.size() > 0, "Non-namespace path must have nodes - " << location << " " << name << " = " << ir);
    }

    // Add traits to a separate list
    if (ir.mBindings.type.binding.is_Trait()) {
        auto it = std::find(mod.traits.begin(), mod.traits.end(), ir.mBindings.type.path);
        if (it == mod.traits.end()) {
            mod.traits.push_back(ir.mBindings.type.path);
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
        } else if (errorOnCollision) {
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

void _add_item_type(const Span& sp, AST::Module& mod, const RcString& name, const AST::Visibility& vis, ::AST::Path ir, bool errorOnCollision = true) {
    _add_item(sp, mod, IndexName::Namespace, name, vis, ::AST::Path(ir), errorOnCollision);
    _add_item(sp, mod, IndexName::Type, name, vis, ::std::move(ir), errorOnCollision);
}

void _add_item_value(const Span& sp, AST::Module& mod, const RcString& name, const AST::Visibility& vis, ::AST::Path ir, bool errorOnCollision = true) {
    _add_item(sp, mod, IndexName::Value, name, vis, mv$(ir), errorOnCollision);
}

void ResolveIndexModuleBase(const AST::Crate& crate, AST::Module& mod) {
    TRACE_FUNCTION_F("mod = " << mod.path());
    for (const auto& i : mod.mItems) {
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
                p.mBindings.type.set(ap, ::AST::PathBindingType::make_Module({&e}));
                _add_item(i->span, mod, IndexName::Namespace, i->name, i->vis, mv$(p));
            }
            TU_ARMA(Crate, e) {
                if (e.name != "") {
                    ASSERT_BUG(i->span, crate.externCrates.count(e.name) > 0, "Referenced crate '" << e.name << "' isn't loaded for `extern crate`");
                    p.mBindings.type.set(ap, ::AST::PathBindingType::make_Crate({&crate.externCrates.at(e.name)}));
                } else {
                    p.mBindings.type.set(ap, ::AST::PathBindingType::make_Module({&crate.rootModule}));
                }
                _add_item(i->span, mod, IndexName::Namespace, i->name, i->vis, mv$(p));
            }
            TU_ARMA(Enum, e) {
                p.mBindings.type.set(ap, ::AST::PathBindingType::make_Enum({&e}));
                _add_item_type(i->span, mod, i->name, i->vis, mv$(p));
            }
            TU_ARMA(Union, e) {
                p.mBindings.type.set(ap, ::AST::PathBindingType::make_Union({&e}));
                _add_item_type(i->span, mod, i->name, i->vis, mv$(p));
            }
            TU_ARMA(Trait, e) {
                p.mBindings.type.set(ap, ::AST::PathBindingType::make_Trait({&e}));
                _add_item_type(i->span, mod, i->name, i->vis, mv$(p));
            }
            TU_ARMA(TraitAlias, e) {
                p.mBindings.type.set(ap, ::AST::PathBindingType::make_TraitAlias({&e}));
                _add_item_type(i->span, mod, i->name, i->vis, mv$(p));
            }
            TU_ARMA(Type, e) {
                p.mBindings.type.set(ap, ::AST::PathBindingType::make_TypeAlias({&e}));
                _add_item_type(i->span, mod, i->name, i->vis, mv$(p));
            }
            // - Mixed
            TU_ARMA(Struct, e) {
                p.mBindings.type.set(ap, ::AST::PathBindingType::make_Struct({&e}));
                // - If the struct is a tuple-like struct (or unit-like), it presents in the value namespace
                if (!e.mData.is_Struct()) {
                    p.mBindings.value.set(ap, ::AST::PathBindingValue::make_Struct({&e}));
                    _add_item_value(i->span, mod, i->name, i->vis, p);
                }
                _add_item_type(i->span, mod, i->name, i->vis, mv$(p));
            }
            // - Values only
            TU_ARMA(Function, e) {
                p.mBindings.value.set(ap, ::AST::PathBindingValue::make_Function({&e}));
                _add_item_value(i->span, mod, i->name, i->vis, mv$(p));
            }
            TU_ARMA(Static, e) {
                p.mBindings.value.set(ap, ::AST::PathBindingValue::make_Static({&e}));
                _add_item_value(i->span, mod, i->name, i->vis, mv$(p));
            }
        }
    }

    for (const auto& item : mod.macros()) {
        ::AST::Path p = mod.path() + item.name;
        p.mBindings.macro.set(mod.path() + item.name, ::AST::PathBindingMacro::make_MacroRules({nullptr, &*item.data}));
        // NOTE: Macros can be freely duplicated, BUT the last entry takes precedence (TODO)
        _add_item(item.span, mod, IndexName::Macro, item.name, item.vis, mv$(p), /*error_on_collision=*/false);
    }

    bool hasPubWildcard = false;
    // Named imports
    for (const auto& ip : mod.mItems) {
        const auto& i = *ip;
        if (!i.data.is_Use()) {
            continue;
        }
        for (const auto& iData : i.data.as_Use().entries) {
            if (iData.name != "") {
                DEBUG("Use " << iData.name << " = " << iData.path);
                // TODO: Ensure that the path is canonical?

                const auto& sp = iData.sp;
                ASSERT_BUG(sp, iData.path.mBindings.hasBinding(), "`use " << iData.path << "` left unbound in module " << mod.path());
                const auto& pb = iData.path.mBindings;

                bool allowCollide = true; // Allow collisions (`use` can import mutliple namespaces, local gets priority)
                // - Types
            TU_MATCH_HDRA( (pb.type.binding), {)
            TU_ARMA(Unbound, _e) {
                        DEBUG(iData.name << " - Not a type/module");
                    }
                    TU_ARMA(TypeParameter, e)
                    BUG(sp, "Import was bound to type parameter");
                    TU_ARMA(Primitive, e)
                    _add_item_type(sp, mod, iData.name, i.vis, pb.type, !allowCollide);
                    TU_ARMA(Crate, e)
                    _add_item(sp, mod, IndexName::Namespace, iData.name, i.vis, pb.type, !allowCollide);
                    TU_ARMA(Module, e)
                    _add_item(sp, mod, IndexName::Namespace, iData.name, i.vis, pb.type, !allowCollide);
                    TU_ARMA(Enum, e)
                    _add_item_type(sp, mod, iData.name, i.vis, pb.type, !allowCollide);
                    TU_ARMA(Union, e)
                    _add_item_type(sp, mod, iData.name, i.vis, pb.type, !allowCollide);
                    TU_ARMA(Trait, e)
                    _add_item_type(sp, mod, iData.name, i.vis, pb.type, !allowCollide);
                    TU_ARMA(TraitAlias, e)
                    _add_item_type(sp, mod, iData.name, i.vis, pb.type, !allowCollide);
                    TU_ARMA(TypeAlias, e)
                    _add_item_type(sp, mod, iData.name, i.vis, pb.type, !allowCollide);
                    TU_ARMA(Struct, e)
                    _add_item_type(sp, mod, iData.name, i.vis, pb.type, !allowCollide);
                    TU_ARMA(EnumVar, e)
                    _add_item_type(sp, mod, iData.name, i.vis, pb.type, !allowCollide);
            }
            // - Values
            TU_MATCH_HDRA( (pb.value.binding), {)
            TU_ARMA(Unbound, _e) {
                        DEBUG(iData.name << " - Not a value");
                    }
                    TU_ARMA(Variable, e)
                    BUG(sp, "Import was bound to a variable");
                    TU_ARMA(Generic, e)
                    BUG(sp, "Import was bound to a generic value");
                    TU_ARMA(Struct, e)
                    _add_item_value(sp, mod, iData.name, i.vis, pb.value, !allowCollide);
                    TU_ARMA(EnumVar, e)
                    _add_item_value(sp, mod, iData.name, i.vis, pb.value, !allowCollide);
                    TU_ARMA(Static, e)
                    _add_item_value(sp, mod, iData.name, i.vis, pb.value, !allowCollide);
                    TU_ARMA(Function, e)
                    _add_item_value(sp, mod, iData.name, i.vis, pb.value, !allowCollide);
            }
            // - Macros
            TU_MATCH_HDRA( (pb.macro.binding), {)
            TU_ARMA(Unbound, _e) {
                        DEBUG(iData.name << " - Not a macro");
                    }
                    TU_ARMA(MacroRules, e) {
                        _add_item(sp, mod, IndexName::Macro, iData.name, i.vis, pb.macro, !allowCollide);
                    }
                    TU_ARMA(ProcMacro, e) {
                        _add_item(sp, mod, IndexName::Macro, iData.name, i.vis, pb.macro, !allowCollide);
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
                    hasPubWildcard = true;
                }
            }
        }
    }

    mod.indexPopulated = (hasPubWildcard ? 1 : 2);

    // Handle child modules
    for (auto& i : mod.mItems) {
        if (auto* e = i->data.opt_Module()) {
            ResolveIndexModuleBase(crate, *e);
        }
    }
    for (auto& mp : mod.anonMods()) {
        if (mp) {
            ResolveIndexModuleBase(crate, *mp);
        }
    }
}

void ResolveIndexModuleWildcardGlobInHirMod(
    const Span& sp,
    const AST::Crate& crate,
    AST::Module& dstMod,
    /*const AST::ExternCrate& hcrate,*/ const ::HIR::Module& hmod,
    const ::AST::Path& path,
    const ::AST::Visibility& vis,
    AST::AbsolutePath mod_ap
) {
    TRACE_FUNCTION_F(dstMod.path() << " <= " << mod_ap);
    for (const auto& it : hmod.modItems) {
        const auto& ve = *it.second;
        if (ve.publicity.is_global()) {
            const auto* vep = &ve.ent;

            ::AST::PathBinding<::AST::PathBindingType> pb;
            if (vep->is_Import()) {
                const auto& spath = vep->as_Import().path;
                pb.path.crate = spath.crate_name();
                pb.path.nodes = spath.componentsVec();

                ASSERT_BUG(sp, crate.externCrates.count(spath.crate_name()) == 1, "Crate " << spath.crate_name() << " is not loaded");
                const auto* hmod = &crate.externCrates.at(spath.crate_name()).hir->rootModule;
                // Import of the crate root
                if (spath.components().size() == 0) {
                    pb.binding = ::AST::PathBindingType::make_Module({nullptr, {nullptr, hmod}});
                    _add_item(sp, dstMod, IndexName::Namespace, it.first, vis, ::AST::Path(pb), false);
                    continue;
                }
                for (unsigned int i = 0; i < spath.components().size() - 1; i++) {
                    const auto& hit = hmod->modItems.at(spath.components()[i]);
                    // Only support enums on the penultimate component
                    if (i == spath.components().size() - 2 && hit->ent.is_Enum()) {
                        pb.binding = ::AST::PathBindingType::make_EnumVar({nullptr, 0});
                        _add_item_type(sp, dstMod, it.first, vis, mv$(pb), false);
                        hmod = nullptr;
                        break;
                    }
                    ASSERT_BUG(sp, hit->ent.is_Module(), "Path component " << spath.components()[i] << " of " << spath << " is not a module, instead " << hit->ent.tag_str());
                    hmod = &hit->ent.as_Module();
                }
                if (!hmod) {
                    continue;
                }
                vep = &hmod->modItems.at(spath.components().back())->ent;
            } else {
                pb.path = mod_ap + it.first;
            }
            TU_MATCH_HDRA( (*vep), {)
            TU_ARMA(Import, e) {
                    //throw "";
                    TODO(sp, "Get binding for HIR import? " << e.path);
                }
                TU_ARMA(Module, e) {
                    pb.binding = ::AST::PathBindingType::make_Module({nullptr, {nullptr, &e}});
                }
                TU_ARMA(Trait, e) {
                    pb.binding = ::AST::PathBindingType::make_Trait({nullptr, &e});
                }
                TU_ARMA(Struct, e) {
                    pb.binding = ::AST::PathBindingType::make_Struct({nullptr, &e});
                }
                TU_ARMA(TraitAlias, e) {
                    pb.binding = ::AST::PathBindingType::make_TraitAlias({nullptr, &e});
                }
                TU_ARMA(Union, e) {
                    pb.binding = ::AST::PathBindingType::make_Union({nullptr, &e});
                }
                TU_ARMA(Enum, e) {
                    pb.binding = ::AST::PathBindingType::make_Enum({nullptr});
                }
                TU_ARMA(TypeAlias, e) {
                    pb.binding = ::AST::PathBindingType::make_TypeAlias({nullptr});
                }
                TU_ARMA(ExternType, e) {
                    pb.binding = ::AST::PathBindingType::make_TypeAlias({nullptr});
                }
            }
            _add_item_type( sp, dstMod, it.first, vis, mv$(pb), false );
        }
    }
    for (const auto& it : hmod.valueItems) {
        const auto& ve = *it.second;
        if (ve.publicity.is_global()) {
            const auto* vep = &ve.ent;

            ::AST::PathBinding<::AST::PathBindingValue> pb;
            if (ve.ent.is_Import()) {
                const auto& spath = ve.ent.as_Import().path;
                pb.path.crate = spath.crate_name();
                pb.path.nodes = spath.componentsVec();

                ASSERT_BUG(sp, crate.externCrates.count(spath.crate_name()) == 1, "Crate " << spath.crate_name() << " is not loaded");
                const auto* hmod = &crate.externCrates.at(spath.crate_name()).hir->rootModule;
                for (unsigned int i = 0; i < spath.components().size() - 1; i++) {
                    const auto& hit = hmod->modItems.at(spath.components()[i]);
                    if (i == spath.components().size() - 2 && hit->ent.is_Enum()) {
                        auto idx = hit->ent.as_Enum().findVariant(spath.components().back());
                        ASSERT_BUG(sp, idx != SIZE_MAX, spath);
                        pb.binding = ::AST::PathBindingValue::make_EnumVar({nullptr, static_cast<unsigned>(idx)});
                        _add_item_value(sp, dstMod, it.first, vis, mv$(pb), false);
                        hmod = nullptr;
                        break;
                    }
                    ASSERT_BUG(sp, hit->ent.is_Module(), "Path component " << spath.components()[i] << " of " << spath << " is not a module, instead " << hit->ent.tag_str());
                    hmod = &hit->ent.as_Module();
                }
                if (!hmod) {
                    continue;
                }
                vep = &hmod->valueItems.at(spath.components().back())->ent;
            } else {
                pb.path = mod_ap + it.first;
            }
            assert(vep);
            TU_MATCH_HDRA( (*vep), {)
            TU_ARMA(Import, e) {
                    throw "";
                }
                TU_ARMA(Constant, e) {
                    pb.binding = ::AST::PathBindingValue::make_Static({nullptr});
                }
                TU_ARMA(Static, e) {
                    pb.binding = ::AST::PathBindingValue::make_Static({nullptr});
                }
                // TODO: What if these refer to an enum variant?
                TU_ARMA(StructConstant, e) {
                    pb.binding = ::AST::PathBindingValue::make_Struct({nullptr, &crate.externCrates.at(e.ty.crate_name()).hir->getTypeitemByPath(sp, e.ty, true).as_Struct()});
                }
                TU_ARMA(StructConstructor, e) {
                    pb.binding = ::AST::PathBindingValue::make_Struct({nullptr, &crate.externCrates.at(e.ty.crate_name()).hir->getTypeitemByPath(sp, e.ty, true).as_Struct()});
                }
                TU_ARMA(Function, e) {
                    pb.binding = ::AST::PathBindingValue::make_Function({nullptr});
                }
            }
            _add_item_value( sp, dstMod, it.first, vis, mv$(pb), false );
        }
    }
    for (const auto& it : hmod.macroItems) {
        const auto& e = *it.second;
        if (e.publicity.is_global()) {
            ::AST::PathBinding<::AST::PathBindingMacro> pb;
            if (const auto* ep = e.ent.opt_Import()) {
                pb.path.crate = ep->path.crate_name();
                pb.path.nodes = ep->path.componentsVec();
                // NOTE: This shouldn't ever be pointing at an import, and no other handling needed
            } else {
                pb.path = mod_ap + it.first;
            }

            TU_MATCH_HDRA( (e.ent), {)
            TU_ARMA(Import, _) {
                    pb.binding = ::AST::PathBindingMacro::make_MacroRules({nullptr, nullptr});
                }
                TU_ARMA(ProcMacro, me) {
                    pb.binding = ::AST::PathBindingMacro::make_ProcMacro({nullptr, me.name});
                }
                TU_ARMA(MacroRules, me) {
                    pb.binding = ::AST::PathBindingMacro::make_MacroRules({nullptr, &*me});
                }
            }
            _add_item(sp, dstMod, IndexName::Macro, it.first, vis, mv$(pb), false );
        }
    }
}

void ResolveIndexModuleWildcardSubmod(AST::Crate& crate, AST::Module& dstMod, const AST::Module& src_mod, const AST::Visibility& dstVis) {
    static Span sp;
    TRACE_FUNCTION_F(dstMod.path() << " <= " << src_mod.path());
    static ::std::set<const AST::Module*> stack;
    if (!stack.insert(&src_mod).second) {
        DEBUG("- Avoided recursion");
        return;
    }

    for (const auto& vi : src_mod.namespaceItems) {
        if (vi.second.vis.is_visible(dstMod.path() /*, src_mod.path()*/)) {
            _add_item(sp, dstMod, IndexName::Namespace, vi.first, dstVis, vi.second.path, false);
        }
    }
    for (const auto& vi : src_mod.typeItems) {
        if (vi.second.vis.is_visible(dstMod.path() /*, src_mod.path()*/)) {
            _add_item(sp, dstMod, IndexName::Type, vi.first, dstVis, vi.second.path, false);
        }
    }
    for (const auto& vi : src_mod.valueItems) {
        if (vi.second.vis.is_visible(dstMod.path() /*, src_mod.path()*/)) {
            _add_item(sp, dstMod, IndexName::Value, vi.first, dstVis, vi.second.path, false);
        }
    }
    for (const auto& vi : src_mod.macroItems) {
        if (vi.second.vis.is_visible(dstMod.path() /*, src_mod.path()*/)) {
            _add_item(sp, dstMod, IndexName::Macro, vi.first, dstVis, vi.second.path, false);
        }
    }

    if (src_mod.indexPopulated != 2) {
        for (const auto& i : src_mod.mItems) {
            if (!i->data.is_Use()) {
                continue;
            }
            if (!i->vis.is_visible(dstMod.path() /*, src_mod.path()*/)) {
                continue;
            }
            for (const auto& e : i->data.as_Use().entries) {
                if (e.name != "") {
                    continue;
                }
                ResolveIndexModuleWildcardUseStmt(crate, dstMod, e, dstVis);
            }
        }
    }

    stack.erase(&src_mod);
}

void ResolveIndexModuleWildcardUseStmt(AST::Crate& crate, AST::Module& dstMod, const AST::UseItem::Ent& iData, const AST::Visibility& vis) {
    const auto& sp = iData.sp;
    const auto& b = iData.path.mBindings.type;

    if (const auto* e = b.binding.opt_Crate()) {
        DEBUG("Glob crate " << iData.path);
        const auto& hmod = e->crate_->hir->rootModule;
        ResolveIndexModuleWildcardGlobInHirMod(sp, crate, dstMod, hmod, iData.path, vis, b.path);
    } else if (const auto* e = b.binding.opt_Module()) {
        DEBUG("Glob mod " << iData.path);
        if (!e->module_) {
            ASSERT_BUG(sp, e->hir.mod, "Glob import where HIR module pointer not set - " << iData.path);
            const auto& hmod = *e->hir.mod;
            ResolveIndexModuleWildcardGlobInHirMod(sp, crate, dstMod, hmod, iData.path, vis, b.path);
        } else {
            ResolveIndexModuleWildcardSubmod(crate, dstMod, *e->module_, vis);
        }
    } else if (const auto* ep = b.binding.opt_Enum()) {
        const auto& e = *ep;
        ASSERT_BUG(sp, e.enum_ || e.hir, "Glob import but enum pointer not set - " << iData.path);
        if (e.enum_) {
            DEBUG("Glob enum " << iData.path << " (AST)");
            unsigned int idx = 0;
            for (const auto& ev : e.enum_->variants()) {
                if (ev.mData.is_Struct()) {
                    AST::PathBinding<AST::PathBindingType> pb;
                    pb.path = b.path + ev.mName;
                    pb.binding = ::AST::PathBindingType::make_EnumVar({e.enum_, idx});
                    _add_item_type(sp, dstMod, ev.mName, vis, mv$(pb), false);
                } else {
                    AST::PathBinding<AST::PathBindingValue> pb;
                    pb.path = b.path + ev.mName;
                    pb.binding = ::AST::PathBindingValue::make_EnumVar({e.enum_, idx});
                    _add_item_value(sp, dstMod, ev.mName, vis, mv$(pb), false);
                }

                idx += 1;
            }
        } else {
            DEBUG("Glob enum " << iData.path << " (HIR)");
            unsigned int idx = 0;
            if (e.hir->mData.is_Value()) {
                const auto* de = e.hir->mData.opt_Value();
                for (const auto& ev : de->variants) {
                    AST::PathBinding<AST::PathBindingValue> pb;
                    pb.path = b.path + ev.name;
                    pb.binding = ::AST::PathBindingValue::make_EnumVar({nullptr, idx, e.hir});
                    _add_item_value(sp, dstMod, ev.name, vis, mv$(pb), false);

                    idx += 1;
                }
            } else {
                const auto* de = &e.hir->mData.as_Data();
                for (const auto& ev : *de) {
                    if (ev.is_struct) {
                        AST::PathBinding<AST::PathBindingType> pb;
                        pb.path = b.path + ev.name;
                        pb.binding = ::AST::PathBindingType::make_EnumVar({nullptr, idx, e.hir});
                        _add_item_type(sp, dstMod, ev.name, vis, mv$(pb), false);
                    } else {
                        AST::PathBinding<AST::PathBindingValue> pb;
                        pb.path = b.path + ev.name;
                        pb.binding = ::AST::PathBindingValue::make_EnumVar({nullptr, idx, e.hir});
                        _add_item_value(sp, dstMod, ev.name, vis, mv$(pb), false);
                    }

                    idx += 1;
                }
            }
        }
    } else {
        BUG(sp, "Invalid path binding for glob import: " << b.binding.tag_str() << " - " << iData.path);
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
void ResolveIndexModuleWildcard(AST::Crate& crate, AST::Module& mod) {
    TRACE_FUNCTION_F("mod = " << mod.path());
    for (const auto& i : mod.mItems) {
        if (!i->data.is_Use()) {
            continue;
        }
        for (const auto& e : i->data.as_Use().entries) {
            if (e.name != "") {
                continue;
            }
            ResolveIndexModuleWildcardUseStmt(crate, mod, e, i->vis);
        }
    }

    // Mark this as having all the items it ever will.
    mod.indexPopulated = 2;

    // Handle child modules
    for (auto& i : mod.mItems) {
        if (auto* e = i->data.opt_Module()) {
            ResolveIndexModuleWildcard(crate, *e);
        }
    }
    for (auto& mp : mod.anonMods()) {
        if (mp) {
            ResolveIndexModuleWildcard(crate, *mp);
        }
    }
}

void ResolveIndexModuleNormalisePathExt(const ::AST::Crate& crate, const Span& sp, ::AST::Path& path, IndexName loc, const ::AST::ExternCrate& ext, unsigned int start) {
    auto& info = path.cls.as_Absolute();
    const ::HIR::Module* hmod = &ext.hir->rootModule;

    // TODO: Mangle path into being absolute into the crate
    //info.crate = ext.m_name;
    //do {
    //    path.nodes().erase( path.nodes().begin() + i );
    //} while( --i > 0 );

    info.crate = ext.mName;
    info.nodes.erase(info.nodes.begin(), info.nodes.begin() + start);

    if (info.nodes.empty()) {
        return;
    }

    for (unsigned int i = 0; i < info.nodes.size() - 1; i++) {
        auto it = hmod->modItems.find(info.nodes[i].name());
        if (it == hmod->modItems.end()) {
            ERROR(sp, E0000, "Couldn't find node " << i << " of path " << path);
        }
        const auto* item_ptr = &it->second->ent;
        if (item_ptr->is_Import()) {
            const auto& e = item_ptr->as_Import();
            const auto& ec = crate.externCrates.at(e.path.crate_name());

            // TODO: Update the path (and update `i` while there)

            if (e.path.components().empty()) {
                hmod = &ec.hir->rootModule;
                continue;
            }
            item_ptr = &ec.hir->getTypeitemByPath(sp, e.path, /*ignore_crate_name=*/true);
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
            auto it_m = hmod->modItems.find(lastnode.name());
            if (it_m != hmod->modItems.end()) {
                TU_IFLET(
                    ::HIR::TypeItem,
                    it_m->second->ent,
                    Import,
                    e,
                    // Replace the path with this path (maintaining binding)
                    auto bindings = path.mBindings.clone();
                    path = hirToAst(e.path);
                    path.mBindings = mv$(bindings);
                )
                return;
            }
        } break;
        case IndexName::Value: {
            auto it_v = hmod->valueItems.find(lastnode.name());
            if (it_v != hmod->valueItems.end()) {
                TU_IFLET(
                    ::HIR::ValueItem,
                    it_v->second->ent,
                    Import,
                    e,
                    // Replace the path with this path (maintaining binding)
                    auto bindings = path.mBindings.clone();
                    path = hirToAst(e.path);
                    path.mBindings = mv$(bindings);
                )
                return;
            }
        } break;
        case IndexName::Macro: {
            auto it_v = hmod->macroItems.find(lastnode.name());
            if (it_v != hmod->macroItems.end()) {
                if (const auto* e = it_v->second->ent.opt_Import()) {
                    // Replace the path with this path (maintaining binding)
                    auto bindings = path.mBindings.clone();
                    path = hirToAst(e->path);
                    path.mBindings = mv$(bindings);
                }
                return;
            }
        } break;
    }

    ERROR(sp, E0000, "Couldn't find final node of path " << path);
}

// Returns true if a change was made
bool ResolveIndexModuleNormalisePath(const ::AST::Crate& crate, const Span& sp, ::AST::Path& path, IndexName loc) {
    const auto& info = path.cls.as_Absolute();
    if (info.crate != "") {
        if (info.crate == CRATE_BUILTINS) {
            //TODO(sp, "Normalise builtin paths");
            return false;
        }
        ResolveIndexModuleNormalisePathExt(crate, sp, path, loc, crate.externCrates.at(info.crate), 0);
        return false;
    }
    if (info.nodes.empty()) {
        return false;
    }

    const ::AST::Module* mod = &crate.rootModule;
    ASSERT_BUG(sp, info.nodes.size() > 0, "Empty node list in " << path);
    for (unsigned int i = 0; i < info.nodes.size() - 1; i++) {
        const auto& node = info.nodes[i];

        auto it = mod->namespaceItems.find(node.name());
        if (it == mod->namespaceItems.end()) {
            ERROR(sp, E0000, "Couldn't find node " << i << " of path " << path);
        }
        const auto& ie = it->second;

        if (ie.is_import) {
            // Need to replace all nodes up to and including the current with the import path
            auto new_path = ie.path;
            for (unsigned int j = i + 1; j < info.nodes.size(); j++) {
                new_path.nodes().push_back(mv$(info.nodes[j]));
            }
            new_path.mBindings = path.mBindings.clone();
            path = mv$(new_path);
            return ResolveIndexModuleNormalisePath(crate, sp, path, loc);
        } else {
            TU_MATCH_HDRA( (ie.path.mBindings.type.binding), {)
            default:
                BUG(sp, "Path " << path << " pointed to non-module " << ie.path);
                TU_ARMA(Module, e) {
                    mod = e.module_;
                }
                TU_ARMA(Crate, e) {
                    ResolveIndexModuleNormalisePathExt(crate, sp, path, loc, *e.crate_, i + 1);
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
            auto it = mod->namespaceItems.find(node.name());
            if (it != mod->namespaceItems.end()) {
                ie_p = &it->second;
            }
        } break;
        case IndexName::Value: {
            auto it = mod->valueItems.find(node.name());
            if (it != mod->valueItems.end()) {
                ie_p = &it->second;
            }
        } break;
        case IndexName::Type: {
            auto it = mod->typeItems.find(node.name());
            if (it != mod->typeItems.end()) {
                ie_p = &it->second;
            }
        } break;
        case IndexName::Macro: {
            auto it = mod->macroItems.find(node.name());
            if (it != mod->macroItems.end()) {
                ie_p = &it->second;
            } else {
                // Workaround for `use` on an exporter macro
                const AST::Module::MacroImport* found = nullptr;
                for (const auto& a : mod->macroImports) {
                    //DEBUG("MI " << a.name << " = " << a.ref.tag_str() << " " << a.path);
                    if (a.name == node.name()) {
                        found = &a;
                    }
                }
                if (found && found->ref.is_MacroRules()) {
                    DEBUG("in " << mod->path() << " " << node.name() << " imported using: " << path << " = " << found->path);
                    assert(path != found->path);
                    path = found->path;
                    path.mBindings.macro.set(found->path, AST::PathBindingMacro::make_MacroRules({nullptr, found->ref.as_MacroRules()}));
                    DEBUG("macro_export? -> " << path);
                    ResolveIndexModuleNormalisePath(crate, sp, path, loc);
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
        ResolveIndexModuleNormalisePath(crate, sp, path, loc);
        return true;
    } else {
        // All good
        return false;
    }
}

void ResolveIndexModuleNormalise(const ::AST::Crate& crate, const Span& mod_span, ::AST::Module& mod) {
    TRACE_FUNCTION_F("mod = " << mod.path());
    for (auto& item : mod.mItems) {
        if (auto* e = item->data.opt_Module()) {
            ResolveIndexModuleNormalise(crate, item->span, *e);
        }
    }

    DEBUG("Index for " << mod.path());
    for (auto& ent : mod.namespaceItems) {
        ResolveIndexModuleNormalisePath(crate, mod_span, ent.second.path, IndexName::Namespace);
        DEBUG("NS " << ent.first << " = " << ent.second.path);
    }
    for (auto& ent : mod.typeItems) {
        ResolveIndexModuleNormalisePath(crate, mod_span, ent.second.path, IndexName::Type);
        DEBUG("Ty " << ent.first << " = " << ent.second.path);
    }
    for (auto& ent : mod.valueItems) {
        ResolveIndexModuleNormalisePath(crate, mod_span, ent.second.path, IndexName::Value);
        DEBUG("Val " << ent.first << " = " << ent.second.path);
    }
    for (auto& ent : mod.macroItems) {
        ResolveIndexModuleNormalisePath(crate, mod_span, ent.second.path, IndexName::Macro);
        DEBUG("Macro " << ent.first << " = " << ent.second.path);
    }
}

void ResolveIndexModuleExportedMacros(::AST::Crate& crate, const Span& mod_span, ::AST::Module& mod) {
    TRACE_FUNCTION_F("mod = " << mod.path());

    if (&mod != &crate.rootModule) {
        for (const auto& item : mod.macros()) {
            if (item.data->exported) {
                ASSERT_BUG(item.span, mod.macroItems.count(item.name), "Missing " << item.name << " in " << mod.path());
                _add_item(item.span, crate.rootModule, IndexName::Macro, item.name, AST::Visibility::make_global(), mod.macroItems.at(item.name).path);
            }
        }
    }

    for (auto& item : mod.mItems) {
        if (auto* e = item->data.opt_Module()) {
            ResolveIndexModuleExportedMacros(crate, item->span, *e);
        }
    }
}

void ResolveIndex(AST::Crate& crate) {
    // - Index all explicitly named items
    ResolveIndexModuleBase(crate, crate.rootModule);
    // - Index wildcard imports
    ResolveIndexModuleWildcard(crate, crate.rootModule);

    // Macros marked with `#[macro_export]` actually live in the root
    ResolveIndexModuleExportedMacros(crate, Span(), crate.rootModule);

    // - Normalise the index (ensuring all paths point directly to the item)
    ResolveIndexModuleNormalise(crate, Span(), crate.rootModule);
}


enum class Lookup {
    Any,    // Allow binding to anything
    AnyOpt, // Allow binding to anything, but don't error on failure
    Type,   // Allow only type bindings
    Value,  // Allow only value bindings
};

namespace {
    const RcString rcstring_crate_builtins = RcString::new_interned(CRATE_BUILTINS);
}

void ResolveUseMod(const ::AST::Crate& crate, ::AST::Module& mod, ::AST::Path path, ::std::span<const ::AST::Module*> parent_modules = {});
::AST::Path::Bindings ResolveUseGetBinding(const Span& span, const ::AST::Crate& crate, const ::AST::AbsolutePath& source_mod_path, const ::AST::Path& path, ::std::span<const ::AST::Module*> parent_modules, bool types_only = false, bool soft_fail = false);

::AST::Path::Bindings ResolveUseGetBindingMod(const Span& span, const ::AST::Crate& crate, const ::AST::AbsolutePath& source_mod_path, const ::AST::Module& mod, const RcString& desItemName, ::std::span<const ::AST::Module*> parent_modules, bool types_only = false, bool require_visible = false);
::AST::Path::Bindings ResolveUseGetBindingExt(const Span& span, const ::AST::Crate& crate, const AST::ExternCrate& ec, const ::HIR::Module& hmodr, const ::AST::Path& path, unsigned int start, AST::AbsolutePath ap = {});
::AST::Path::Bindings ResolveUseGetBindingExt(const Span& span, const ::AST::Crate& crate, const ::AST::Path& path, const AST::ExternCrate& ec, unsigned int start);

void ResolveUse(::AST::Crate& crate) {
    ResolveUseMod(crate, crate.rootModule, ::AST::Path("", {}));
}

// - Convert self::/super:: paths into non-canonical absolute forms
::AST::Path ResolveUseAbsolutisePath(const Span& span, const AST::Crate& crate, const ::AST::Path& basePath, ::AST::Path path) {
    TU_MATCH_HDRA( (path.cls), {)
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
            if (crate.edition >= AST::Edition::Rust2018) {
                const auto& name = e.nodes.at(0).name();
                auto ecIt = AST::gImplicitCrates.find(name);
                if (ecIt != AST::gImplicitCrates.end()) {
                    DEBUG("Found implict crate " << name);
                    e.nodes.erase(e.nodes.begin());
                    return AST::Path(ecIt->second, e.nodes);
                } else {
                    DEBUG("No implicit crate " << name);
                }
            }

            // If there's only one node, then check for primitives.
            if (path.nodes().size() == 1) {
                auto ct = coretypeFromstring(path.nodes()[0].name().c_str());
                if (ct != CORETYPE_INVAL) {
                    DEBUG("Found builtin type for `use` - " << path);
                    // TODO: only if the item doesn't already exist?
                    AST::Path rv{rcstring_crate_builtins, path.nodes()};
                    rv.mBindings.type.set(AST::AbsolutePath(rcstring_crate_builtins, {path.nodes().back().name()}), {});
                    return rv;
                }
            }

            // EVIL HACK: If the current module is an anon module, refer to the parent
            // TODO: Check if the desired item is in this module,
            if (basePath.nodes().size() > 0 && basePath.nodes().back().name().c_str()[0] == '#') {
                std::vector<const AST::Module*> parent_mods;
                const AST::Module* curMod = &crate.rootModule;
                parent_mods.push_back(curMod);
                // Walk the path to create a list of parent modules
                // - Resets the list every time there's a non-anon module
                for (unsigned int i = 0; i < basePath.nodes().size(); i++) {
                    const auto& name = basePath.nodes()[i].name();
                    const AST::Module* next_mod = nullptr;

                    // If the desired item is an anon module (starts with #) then parse and index
                    if (name.size() > 0 && name.c_str()[0] == '#') {
                        unsigned int idx = 0;
                        if (::std::sscanf(name.c_str(), "#%u", &idx) != 1) {
                            BUG(span, "Invalid anon path segment '" << name << "'");
                        }
                        ASSERT_BUG(span, idx < curMod->anonMods().size(), "Invalid anon path segment '" << name << "'");
                        assert(curMod->anonMods()[idx]);
                        next_mod = &*curMod->anonMods()[idx];
                    } else {
                        for (const auto& item : curMod->mItems) {
                            if (item->name == name && item->data.is_Module()) {
                                next_mod = &item->data.as_Module();
                                break;
                            }
                        }
                        ASSERT_BUG(span, next_mod, "Could not find module '" << name << "' in " << curMod->path());
                    }
                    curMod = next_mod;
                    if (name.c_str()[0] != '#') {
                        parent_mods.clear();
                    }
                    parent_mods.push_back(curMod);
                }
                parent_mods.pop_back();
                DEBUG("parent_mods.size() = " << parent_mods.size());
                ASSERT_BUG(span, !parent_mods.empty(), "Anon module with no named parent");
                const AST::Module* source_mod = parent_mods.front();

                for (;;) {
                    DEBUG("Module " << curMod->path());
                    if (ResolveUseGetBindingMod(span, crate, source_mod->path(), *curMod, e.nodes.front().name(), parent_mods, /*types_only*/ e.nodes.size() > 1).hasBinding()) {
                        break;
                    }
                    if (parent_mods.empty()) {
                        ERROR(span, E0000, "Unable to find " << e.nodes.front().name());
                    }
                    curMod = parent_mods.back();
                    parent_mods.pop_back();
                }
                DEBUG("Found item in " << curMod->path());

                AST::Path np("", {});
                for (unsigned int i = 0; i < curMod->path().nodes.size(); i++) {
                    np.nodes().push_back(curMod->path().nodes[i]);
                }
                np += path;
                return np;
            } else {
                return basePath + path;
            }
        }
        TU_ARMA(Self, e) {
            DEBUG("Self " << path);
            // EVIL HACK: If the current module is an anon module, refer to the parent
            if (basePath.nodes().size() > 0 && basePath.nodes().back().name().c_str()[0] == '#') {
                AST::Path np("", {});
                for (unsigned int i = 0; i < basePath.nodes().size() - 1; i++) {
                    np.nodes().push_back(basePath.nodes()[i]);
                }
                np += path;
                return np;
            } else {
                return basePath + path;
            }
        }
        TU_ARMA(Super, e) {
            DEBUG("Super " << path);
            assert(e.count >= 1);
            AST::Path np("", {});
            if (e.count > basePath.nodes().size()) {
                ERROR(span, E0000, "Too many `super` components");
            }
            // TODO: Do this in a cleaner manner.
            unsigned int n_anon = 0;
            // Skip any anon modules in the way (i.e. if the current module is an anon, go to the parent)
            while (basePath.nodes().size() > n_anon && basePath.nodes()[basePath.nodes().size() - 1 - n_anon].name().c_str()[0] == '#') {
                n_anon++;
            }
            for (unsigned int i = 0; i < basePath.nodes().size() - e.count - n_anon; i++) {
                np.nodes().push_back(basePath.nodes()[i]);
            }
            np += path;
            return np;
        }
        TU_ARMA(Absolute, e) {
            DEBUG("Absolute " << path);
            // HACK: if the crate name starts with `=` it's a 2018 absolute path (references a crate loaded with `--extern`)
            if (crate.edition >= AST::Edition::Rust2018 && e.crate.c_str()[0] == '=') {
                // Absolute paths in 2018 edition are crate-prefixed?
                auto ecIt = AST::gImplicitCrates.find(e.crate.c_str() + 1);
                if (ecIt == AST::gImplicitCrates.end()) {
                    ERROR(span, E0000, "Unable to find external crate for path " << path);
                }
                e.crate = ecIt->second;
            }
            // Leave as is
            return path;
        }
    }
    throw "BUG: Reached end of Resolve_Use_AbsolutisePath";
}

void ResolveUseMod(const ::AST::Crate& crate, ::AST::Module& mod, ::AST::Path path, ::std::span<const ::AST::Module*> parent_modules) {
    TRACE_FUNCTION_F("path = " << path);

    for (auto& use_stmt : mod.mItems) {
        if (!use_stmt->data.is_Use()) {
            continue;
        }
        auto& use_stmt_data = use_stmt->data.as_Use();

        const Span& span = use_stmt_data.sp;
        for (auto& use_ent : use_stmt_data.entries) {
            TRACE_FUNCTION_F(use_ent);

            use_ent.path = ResolveUseAbsolutisePath(span, crate, path, use_ent.path);
            if (!use_ent.path.cls.is_Absolute()) {
                BUG(span, "Use path is not absolute after absolutisation");
            }

            // NOTE: Use statements can refer to _three_ different items
            // - types/modules ("type namespace")
            // - values ("value namespace")
            // - macros ("macro namespace")
            // TODO: Have Resolve_Use_GetBinding return the actual path
            use_ent.path.mBindings = ResolveUseGetBinding(span, crate, mod.path(), use_ent.path, parent_modules);
            if (!use_ent.path.mBindings.hasBinding()) {
                ERROR(span, E0000, "Unable to resolve `use` target " << use_ent.path);
            }
            DEBUG("'" << use_ent.name << "' = " << use_ent.path);

            // - If doing a glob, ensure the item type is valid
            if (use_ent.name == "") {
                TU_MATCH_DEF(::AST::PathBindingType, (use_ent.path.mBindings.type.binding), (e), (ERROR(span, E0000, "Wildcard import of invalid item type - " << use_ent.path);), (Enum, ), (Crate, ), (Module, ))
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

        void visit(AST::ExprNodeBlock& node) override {
            if (node.localMod) {
                ResolveUseMod(this->crate, *node.localMod, node.localMod->path(), this->parent_modules);

                parent_modules.push_back(&*node.localMod);
            }
            AST::NodeVisitorDef::visit(node);
            if (node.localMod) {
                parent_modules.pop_back();
            }
        }
    } exprIter(crate, mod, parent_modules);

    // TODO: Check that all code blocks are covered by these
    // - NOTE: Handle anon modules by iterating code (allowing correct item mappings)
    for (auto& ip : mod.mItems) {
        auto& i = *ip;
        TU_MATCH_HDRA( (i.data),  {)
        default:
            break;
            TU_ARMA(Module, e) {
                ResolveUseMod(crate, e, path + i.name);
            }
            TU_ARMA(Impl, e) {
                for (auto& i : e.items()) {
                    TU_MATCH_DEF(AST::Item, (*i.data), (e), (), (Function, if (e.code().is_valid()) { e.code().node().visit(exprIter); }), (Static, if (e.value().is_valid()) { e.value().node().visit(exprIter); }))
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
                        (Function, if (e.code().is_valid()) { e.code().node().visit(exprIter); }),
                        (Static, if (e.value().is_valid()) { e.value().node().visit(exprIter); })
                    )
                }
            }
            TU_ARMA(Function, e) {
                if (e.code().is_valid()) {
                    e.code().node().visit(exprIter);
                }
            }
            TU_ARMA(Static, e) {
                if (e.value().is_valid()) {
                    e.value().node().visit(exprIter);
                }
            }
        }
    }
}

::AST::Path::Bindings ResolveUseGetBindingMod(
    const Span& span,
    const ::AST::Crate& crate,
    const ::AST::AbsolutePath& source_mod_path,
    const ::AST::Module& mod,
    const RcString& desItemName,
    ::std::span<const ::AST::Module*> parent_modules,
    bool types_only,     // = false
    bool require_visible // = false
) {
    ::AST::Path::Bindings rv;
    TRACE_FUNCTION_F(mod.path() << ", des_item_name=" << desItemName);

    static ::std::vector<std::pair<const AST::Module*, const char*>> s_recurse_stack;
    auto recurse_ent = std::make_pair(&mod, desItemName.c_str());
    // EVIL: Allow a single recursion before returning empty
    if (std::count(s_recurse_stack.begin(), s_recurse_stack.end(), recurse_ent) > 1) {
        DEBUG("Recursion detected, returning empty bindings");
        return rv;
    }
    auto _ = push_and_pop_at_end(s_recurse_stack, recurse_ent);

    // TODO: Catch and prevent recursion?
    // If the desired item is an anon module (starts with #) then parse and index
    if (desItemName.size() > 0 && desItemName.c_str()[0] == '#') {
        unsigned int idx = 0;
        if (::std::sscanf(desItemName.c_str(), "#%u", &idx) != 1) {
            BUG(span, "Invalid anon path segment '" << desItemName << "'");
        }
        ASSERT_BUG(span, idx < mod.anonMods().size(), "Invalid anon path segment '" << desItemName << "'");
        assert(mod.anonMods()[idx]);
        const auto& m = *mod.anonMods()[idx];
        rv.type.set(m.path(), ::AST::PathBindingType::make_Module({&m, {nullptr}}));
        return rv;
    }

    // Seach for the name defined in the module.
    for (const auto& ip : mod.mItems) {
        const auto& item = *ip;
        if (item.data.is_None()) {
            continue;
        }
        // When reached through a glob import, private items aren't re-exported (usvg's
        // crate-root `pub use parser::*` must not expose the private `parser::filter`).
        if (require_visible && !item.vis.is_visible(source_mod_path)) {
            continue;
        }

        if (item.name == desItemName) {
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
                        ASSERT_BUG(span, crate.externCrates.count(e.name), "Crate '" << e.name << "' not loaded");
                        rv.type.set(AST::AbsolutePath(e.name, {}), ::AST::PathBindingType::make_Crate({&crate.externCrates.at(e.name)}));
                    } else {
                        rv.type.set(AST::AbsolutePath(e.name, {}), ::AST::PathBindingType::make_Module({&crate.rootModule}));
                    }
                }
                TU_ARMA(Type, e) {
                    rv.type.set(p, ::AST::PathBindingType::make_TypeAlias({&e}));
                }
                TU_ARMA(Trait, e) {
                    rv.type.set(p, ::AST::PathBindingType::make_Trait({&e}));
                }
                TU_ARMA(TraitAlias, e) {
                    rv.type.set(p, ::AST::PathBindingType::make_TraitAlias({&e}));
                }

                TU_ARMA(Function, e) {
                    rv.value.set(p, ::AST::PathBindingValue::make_Function({&e}));
                }
                TU_ARMA(Static, e) {
                    rv.value.set(p, ::AST::PathBindingValue::make_Static({&e}));
                }
                TU_ARMA(Struct, e) {
                    // TODO: What happens with name collisions?
                    if (!e.mData.is_Struct()) {
                        rv.value.set(p, ::AST::PathBindingValue::make_Struct({&e}));
                    }
                    rv.type.set(p, ::AST::PathBindingType::make_Struct({&e}));
                }
                TU_ARMA(Enum, e) {
                    rv.type.set(p, ::AST::PathBindingType::make_Enum({&e}));
                }
                TU_ARMA(Union, e) {
                    rv.type.set(p, ::AST::PathBindingType::make_Union({&e}));
                }
                TU_ARMA(Module, e) {
                    rv.type.set(p, ::AST::PathBindingType::make_Module({&e}));
                }
            }
        }
    }
    for (const auto& mac : mod.macros()) {
        if (mac.name == desItemName) {
            rv.macro.set(mod.path() + mac.name, ::AST::PathBindingMacro::make_MacroRules({nullptr, &*mac.data}));
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
                for (const auto& e : mods.back()->anonMods()) {
                    if (e && e->path().nodes.back() == n) {
                        nm = &*e;
                        break;
                    }
                }
            } else {
                for (const auto& e : mods.back()->mItems) {
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
            const auto& checkMod = *mods[i];
            for (const auto& mac : checkMod.macroImports) {
                if (mac.name == desItemName) {
                    DEBUG("Macro Import - " << mac.path);
                    TU_MATCH_HDRA( (mac.ref), { )
                    TU_ARMA(None, e) {
                        }
                        TU_ARMA(MacroRules, e) {
                            rv.macro.set(mac.path, ::AST::PathBindingMacro::make_MacroRules({nullptr, e}));
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
    if (rv.macro.is_Unbound() && &mod == &crate.rootModule) {
        auto it = crate.exportedMacros.find(desItemName);
        if (it != crate.exportedMacros.end()) {
            rv.macro.set(mod.path() + desItemName, ::AST::PathBindingMacro::make_MacroRules({nullptr, &*it->second}));
            DEBUG("Crate-exported macro - " << rv.macro.path);
        }
    }

    if (types_only && !rv.type.is_Unbound()) {
        return rv;
    }

    const bool canSeePrivate = false || mod.path().is_parent_of(source_mod_path) || (parent_modules.size() > 0 && parent_modules[0]->path().is_parent_of(source_mod_path));

    // Imports
    // - Explicitly named imports first (they take priority over anon imports)
    for (const auto& imp : mod.mItems) {
        if (!imp->data.is_Use()) {
            continue;
        }
        const auto& imp_data = imp->data.as_Use();
        for (const auto& imp_e : imp_data.entries) {
            const Span& sp2 = imp_e.sp;
            if (imp_e.name == desItemName) {
                DEBUG("- Named import " << imp_e.name << " = " << imp_e.path);
                if (!(canSeePrivate || imp->vis.is_visible(source_mod_path /*, mod.path()*/))) {
                    DEBUG("Ignore private import");
                    continue;
                }
                if (!imp_e.path.mBindings.hasBinding()) {
                    DEBUG(" > Needs resolve p=" << &imp_e.path);
                    static ::std::vector<const ::AST::Path*> s_mods;
                    if (::std::find(s_mods.begin(), s_mods.end(), &imp_e.path) == s_mods.end()) {
                        s_mods.push_back(&imp_e.path);
                        rv.merge_from(ResolveUseGetBinding(sp2, crate, mod.path(), ResolveUseAbsolutisePath(sp2, crate, mod.path(), imp_e.path), parent_modules));
                        s_mods.pop_back();
                    } else {
                        DEBUG("Recursion on path " << &imp_e.path << " " << imp_e.path);
                    }
                } else {
                    //out_path = imp_e.path;
                    rv.merge_from(imp_e.path.mBindings.clone());
                }
                continue;
            }
        }
    }

    for (const auto& imp : mod.mItems) {
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
            if ((canSeePrivate || imp->vis.is_visible(source_mod_path /*, mod.path()*/))) {
                DEBUG("- Search glob of " << imp_e.path << " in " << mod.path());
                // INEFFICIENT! Resolves and throws away the result (because we can't/shouldn't mutate here)
                ::AST::Path::Bindings bindings_;
                const auto* bindings = &imp_e.path.mBindings;
                if (bindings->type.is_Unbound()) {
                    DEBUG("Temp resolving wildcard " << imp_e.path);
                    // Handle possibility of recursion
                    static ::std::vector<const ::AST::UseItem*> resolve_stack_ptrs;
                    if (::std::find(resolve_stack_ptrs.begin(), resolve_stack_ptrs.end(), &imp_data) == resolve_stack_ptrs.end()) {
                        resolve_stack_ptrs.push_back(&imp_data);
                        bindings_ = ResolveUseGetBinding(sp2, crate, mod.path(), ResolveUseAbsolutisePath(sp2, crate, mod.path(), imp_e.path), parent_modules, /*type_only=*/true, /*soft_fail=*/true);
                        if (bindings_.type.is_Unbound()) {
                            DEBUG("Recursion detected, skipping " << imp_e.path);
                            resolve_stack_ptrs.pop_back();
                            continue;
                        }
                        // *waves hand* I'm not evil.
                        const_cast<::AST::Path::Bindings&>(imp_e.path.mBindings) = bindings_.clone();
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
                        rv.merge_from(ResolveUseGetBindingExt(sp2, crate, AST::Path("", {AST::PathNode(desItemName, {})}), *e.crate_, 0));
                    }
                    TU_ARMA(Module, e) {
                        if (e.module_) {
                            // Prevent infinite recursion - keyed by (module, name) so an
                            // in-flight search for a *different* name doesn't block this one
                            // (libc resolves `crate::linux` through `new::*` while a search
                            // inside `new` is still on the stack).
                            static ::std::vector<::std::pair<const AST::Module*, RcString>> s_use_glob_mod_stack;
                            auto ent = ::std::make_pair(&*e.module_, desItemName);
                            if (::std::find(s_use_glob_mod_stack.begin(), s_use_glob_mod_stack.end(), ent) == s_use_glob_mod_stack.end()) {
                                s_use_glob_mod_stack.push_back(ent);
                                rv.merge_from(ResolveUseGetBindingMod(span, crate, mod.path(), *e.module_, desItemName, {}, /*types_only=*/false, /*require_visible=*/true));
                                s_use_glob_mod_stack.pop_back();
                            } else {
                                DEBUG("Recursion prevented of " << e.module_->path());
                            }
                        } else if (e.hir.mod) {
                            rv.merge_from(ResolveUseGetBindingExt(sp2, crate, *e.hir.crate, *e.hir.mod, AST::Path("", {AST::PathNode(desItemName, {})}), 0, bindings->type.path));
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
                                if (var.mName == desItemName) {
                                    ::AST::Path::Bindings tmp_rv;
                                    if (var.mData.is_Struct()) {
                                        tmp_rv.type.set(bindings->type.path + desItemName, ::AST::PathBindingType::make_EnumVar({&enm, i}));
                                    } else {
                                        tmp_rv.value.set(bindings->type.path + desItemName, ::AST::PathBindingValue::make_EnumVar({&enm, i}));
                                    }
                                    rv.merge_from(tmp_rv);
                                    break;
                                }
                                i++;
                            }
                        } else {
                            const auto& enm = *e.hir;
                            auto idx = enm.findVariant(desItemName);
                            if (idx != SIZE_MAX) {
                                ::AST::Path::Bindings tmp_rv;
                                if (enm.mData.is_Data() && enm.mData.as_Data()[idx].is_struct) {
                                    tmp_rv.type.set(bindings->type.path + desItemName, ::AST::PathBindingType::make_EnumVar({nullptr, static_cast<unsigned>(idx), &enm}));
                                } else {
                                    tmp_rv.value.set(bindings->type.path + desItemName, ::AST::PathBindingValue::make_EnumVar({nullptr, static_cast<unsigned>(idx), &enm}));
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
    if (rv.hasBinding()) {
        return rv;
    }

    if (mod.path().nodes.size() > 0 && mod.path().nodes.back().c_str()[0] == '#') {
        ASSERT_BUG(span, parent_modules.size() > 0, "Anon module with no parent modules - " << mod.path());
        return ResolveUseGetBindingMod(span, crate, source_mod_path, *parent_modules.back(), desItemName, parent_modules.subspan(0, parent_modules.size() - 1));
    } else {
        //if( allow == Lookup::Any )
        //    ERROR(span, E0000, "Could not find node '" << des_item_name << "' in module " << mod.path());
        return ::AST::Path::Bindings();
    }
}

namespace {
    const ::HIR::Module* getHirModByPath(const Span& sp, const ::AST::Crate& crate, const ::HIR::SimplePath& path);

    const void* getHirModenumByPath(const Span& sp, const ::AST::Crate& crate, const ::HIR::SimplePath& path, bool& is_enum) {
        const auto* hmod = &crate.externCrates.at(path.crate_name()).hir->rootModule;
        for (const auto& node : path.components()) {
            auto it = hmod->modItems.find(node);
            if (it == hmod->modItems.end()) {
                BUG(sp, "");
            }
            TU_IFLET(::HIR::TypeItem, (it->second->ent), Module, mod, hmod = &mod;)
            else TU_IFLET(::HIR::TypeItem, (it->second->ent), Import, import, hmod = getHirModByPath(sp, crate, import.path); if (!hmod) BUG(sp, "Import in module position didn't resolve as a module - " << import.path);) else TU_IFLET(::HIR::TypeItem, (it->second->ent), Enum, enm, if (&node == &path.components().back()) {
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

    const ::HIR::Module* getHirModByPath(const Span& sp, const ::AST::Crate& crate, const ::HIR::SimplePath& path) {
        bool is_enum = false;
        auto rv = getHirModenumByPath(sp, crate, path, is_enum);
        if (!rv) {
            return nullptr;
        }
        ASSERT_BUG(sp, !is_enum, "");
        return reinterpret_cast<const ::HIR::Module*>(rv);
    }
}

::AST::Path::Bindings ResolveUseGetBindingExt(const Span& span, const ::AST::Crate& crate, const AST::ExternCrate& hcrate, const ::HIR::Module& hmodr, const ::AST::Path& path, unsigned int start, AST::AbsolutePath ap) {
    if (ap.crate == "") {
        ap.crate = hcrate.mName;
    }

    ::AST::Path::Bindings rv;
    //TRACE_FUNCTION_FR(path << " offset " << start, rv.value << rv.type << rv.macro);
    TRACE_FUNCTION_F(path << " offset " << start << " [" << ap << "]");
    const auto& nodes = path.nodes();
    const ::HIR::Module* hmod = &hmodr;

    //for(unsigned int i = start; i < nodes.size(); i ++)
    //    ap.nodes.push_back( nodes[i].name() );

    if (nodes.size() == start) {
        rv.type.set(ap, ::AST::PathBindingType::make_Module({nullptr, {&hcrate, hmod}}));
        return rv;
    }
    for (unsigned int i = start; i < nodes.size() - 1; i++) {
        ap.nodes.push_back(nodes[i].name());
        DEBUG("m_mod_items = {" << FMT_CB(ss, for (const auto& e : hmod->modItems) ss << e.first << ", ";) << "}");
        auto it = hmod->modItems.find(nodes[i].name());
        if (it == hmod->modItems.end()) {
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
                auto ptr = getHirModenumByPath(span, crate, e.path, is_enum);
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

                    auto idx = enm.findVariant(name);
                    if (idx == SIZE_MAX) {
                        ERROR(span, E0000, "Unable to find variant " << path);
                    }
                    ap.crate = e.path.crate_name();
                    ap.nodes = e.path.componentsVec();
                    ap.nodes.push_back(name);
                    if (enm.mData.is_Data() && enm.mData.as_Data()[idx].is_struct) {
                        rv.type.set(ap, ::AST::PathBindingType::make_EnumVar({nullptr, static_cast<unsigned int>(idx), &enm}));
                    } else {
                        rv.value.set(ap, ::AST::PathBindingValue::make_EnumVar({nullptr, static_cast<unsigned int>(idx), &enm}));
                    }
                    return rv;
                } else {
                    ap.crate = e.path.crate_name();
                    ap.nodes = e.path.componentsVec();
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

                auto idx = e.findVariant(name);
                if (idx == SIZE_MAX) {
                    ERROR(span, E0000, "Unable to find variant " << path);
                }
                if (e.mData.is_Data() && e.mData.as_Data()[idx].is_struct) {
                    rv.type.set(ap, ::AST::PathBindingType::make_EnumVar({nullptr, static_cast<unsigned int>(idx), &e}));
                } else {
                    rv.value.set(ap, ::AST::PathBindingValue::make_EnumVar({nullptr, static_cast<unsigned int>(idx), &e}));
                }
                return rv;
            }
        }
    }
    // > Found the target module
    ap.nodes.push_back(nodes.back().name());

    // - namespace/type items
    {
        auto it = hmod->modItems.find(nodes.back().name());
        if (it == hmod->modItems.end()) {
            DEBUG("E: : Types = " << FMT_CB(ss, for (const auto& e : hmod->modItems) { ss << e.first << ":" << e.second->ent.tag_str() << ","; }));
        } else if (!it->second->publicity.is_global()) {
            DEBUG("E : Mod " << nodes.back().name() << " = " << it->second->ent.tag_str() << " [private]");
        } else {
            const auto* item_ptr = &it->second->ent;
            auto ap2 = ap;
            auto ap = ap2;
            DEBUG("E : Mod " << nodes.back().name() << " = " << item_ptr->tag_str());
            if (item_ptr->is_Import()) {
                const auto& e = item_ptr->as_Import();
                ap = AST::AbsolutePath(e.path.crate_name(), e.path.componentsVec());
                if (e.path.crate_name() == rcstring_crate_builtins) {
                    auto t = coretypeFromstring(e.path.components().front().c_str());
                    rv.type.set(ap, ::AST::PathBindingType::make_Primitive(t));
                } else {
                    ASSERT_BUG(span, crate.externCrates.count(e.path.crate_name()) != 0, "Crate not loaded for " << e.path);
                    const auto& ec = crate.externCrates.at(e.path.crate_name());
                    // This doesn't need to recurse - it can just do a single layer (as no Import should refer to another)
                    if (e.is_variant) {
                        const auto& enm = ec.hir->getTypeitemByPath(span, e.path, /*ignore_crate_name*/ true, /*ignore_last_node*/ true).as_Enum();
                        assert(e.idx < enm.num_variants());
                        rv.type.set(ap, ::AST::PathBindingType::make_EnumVar({nullptr, e.idx, &enm}));
                    } else if (e.path.components().empty()) {
                        rv.type.set(ap, ::AST::PathBindingType::make_Module({nullptr, {&ec, &ec.hir->rootModule}}));
                    } else {
                        item_ptr = &ec.hir->getTypeitemByPath(span, e.path, /*ignore_crate_name=*/true);
                    }
                }
            } else {
            }
            if (rv.type.is_Unbound()) {
                TU_MATCHA(
                    (*item_ptr),
                    (e),
                    (Import, BUG(span, "Recursive import in " << path << " - " << it->second->ent.as_Import().path << " -> " << e.path);),
                    (Module, rv.type.set(ap, ::AST::PathBindingType::make_Module({nullptr, {&hcrate, &e}}));),
                    (TypeAlias, rv.type.set(ap, ::AST::PathBindingType::make_TypeAlias({nullptr}));),
                    (
                        ExternType, rv.type.set(ap, ::AST::PathBindingType::make_TypeAlias({nullptr})); // Lazy.
                    ),
                    (Enum, rv.type.set(ap, ::AST::PathBindingType::make_Enum({nullptr, &e}));),
                    (Struct, rv.type.set(ap, ::AST::PathBindingType::make_Struct({nullptr, &e}));),
                    (Union, rv.type.set(ap, ::AST::PathBindingType::make_Union({nullptr, &e}));),
                    (Trait, rv.type.set(ap, ::AST::PathBindingType::make_Trait({nullptr, &e}));),
                    (TraitAlias, rv.type.set(ap, ::AST::PathBindingType::make_TraitAlias({nullptr, &e}));)
                )
            }
        }
    }
    // - Values
    {
        auto it = hmod->valueItems.find(nodes.back().name());
        if (it == hmod->valueItems.end()) {
            DEBUG("E : Values = " << FMT_CB(ss, for (const auto& e : hmod->valueItems) { ss << e.first << ":" << e.second->ent.tag_str() << ","; }));
        } else if (!it->second->publicity.is_global()) {
            DEBUG("E : Value " << nodes.back().name() << " = " << it->second->ent.tag_str() << " [private]");
        } else {
            const auto* item_ptr = &it->second->ent;
            auto ap2 = ap;
            auto ap = ap2;
            DEBUG("E : Value " << nodes.back().name() << " = " << item_ptr->tag_str());
            if (item_ptr->is_Import()) {
                const auto& e = item_ptr->as_Import();
                ap = AST::AbsolutePath(e.path.crate_name(), e.path.componentsVec());
                // This doesn't need to recurse - it can just do a single layer (as no Import should refer to another)
                const auto& ec = crate.externCrates.at(e.path.crate_name());
                if (e.is_variant) {
                    auto p = e.path;
                    p.pop_component();
                    const auto& enm = ec.hir->getTypeitemByPath(span, p, true).as_Enum();
                    assert(e.idx < enm.num_variants());
                    rv.value.set(ap, ::AST::PathBindingValue::make_EnumVar({nullptr, e.idx, &enm}));
                } else {
                    item_ptr = &ec.hir->getValitemByPath(span, e.path, true); // ignore_crate_name=true
                }
            }
            if (rv.value.is_Unbound()) {
                TU_MATCH_HDRA( (*item_ptr), {)
                TU_ARMA(Import, e) {
                        BUG(span, "Recursive import in " << path << " - " << it->second->ent.as_Import().path << " -> " << e.path);
                    }
                    TU_ARMA(Constant, e) {
                        rv.value.set(ap, ::AST::PathBindingValue::make_Static({nullptr}));
                    }
                    TU_ARMA(Static, e) {
                        rv.value.set(ap, ::AST::PathBindingValue::make_Static({nullptr}));
                    }
                    // TODO: What happens if these two refer to an enum constructor?
                    TU_ARMA(StructConstant, e) {
                        ASSERT_BUG(span, crate.externCrates.count(e.ty.crate_name()), "Crate '" << e.ty.crate_name() << "' not loaded for " << e.ty);
                        rv.value.set(ap, ::AST::PathBindingValue::make_Struct({nullptr, &crate.externCrates.at(e.ty.crate_name()).hir->getTypeitemByPath(span, e.ty, true).as_Struct()}));
                    }
                    TU_ARMA(StructConstructor, e) {
                        ASSERT_BUG(span, crate.externCrates.count(e.ty.crate_name()), "Crate '" << e.ty.crate_name() << "' not loaded for " << e.ty);
                        rv.value.set(ap, ::AST::PathBindingValue::make_Struct({nullptr, &crate.externCrates.at(e.ty.crate_name()).hir->getTypeitemByPath(span, e.ty, true).as_Struct()}));
                    }
                    TU_ARMA(Function, e) {
                        rv.value.set(ap, ::AST::PathBindingValue::make_Function({nullptr}));
                    }
                }
            }
        }
    }
    // - Macros
    {
        auto it = hmod->macroItems.find(nodes.back().name());
        if (it == hmod->macroItems.end()) {
            DEBUG("E : Macros = " << FMT_CB(ss, for (const auto& e : hmod->macroItems) { ss << e.first << ":" << e.second->ent.tag_str() << ","; }));
        } else if (!it->second->publicity.is_global()) {
            DEBUG("E : Macro " << nodes.back().name() << " = " << it->second->ent.tag_str() << " [private]");
        } else {
            const auto* item_ptr = &it->second->ent;
            auto ap2 = ap;
            auto ap = ap2;
            DEBUG("E : Macro " << nodes.back().name() << " = " << item_ptr->tag_str());

            if (const auto* imp = item_ptr->opt_Import()) {
                if (imp->path.crate_name() == rcstring_crate_builtins) {
                    rv.macro.set(AST::AbsolutePath(rcstring_crate_builtins, {nodes.back().name()}), AST::PathBindingMacro::make_MacroRules({nullptr}));
                    return rv;
                }
                ASSERT_BUG(span, crate.externCrates.count(imp->path.crate_name()) > 0, "Unable to find crate for " << imp->path);
                const auto& c = *crate.externCrates.at(imp->path.crate_name()).hir; // Have to manually look up, AST doesn't have a `get_mod_by_path`
                const auto& mod = c.getModByPath(span, imp->path, /*ignore_last=*/true, /*ignore_crate=*/true);
                item_ptr = &mod.macroItems.at(imp->path.components().back())->ent;
                ap = AST::AbsolutePath(imp->path.crate_name(), imp->path.componentsVec());
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
                        rv.macro.set(ap, ::AST::PathBindingMacro::make_MacroRules({nullptr, nullptr}));
                    }
                    TU_ARMA(ProcMacro, e) {
                        rv.macro.set(ap, ::AST::PathBindingMacro::make_ProcMacro({&hcrate, e.name}));
                    }
                    TU_ARMA(MacroRules, e) {
                        rv.macro.set(ap, ::AST::PathBindingMacro::make_MacroRules({nullptr, &*e}));
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

::AST::Path::Bindings ResolveUseGetBindingExt(const Span& span, const ::AST::Crate& crate, const ::AST::Path& path, const AST::ExternCrate& ec, unsigned int start) {
    DEBUG("Crate " << ec.mName);
    auto rv = ResolveUseGetBindingExt(span, crate, ec, ec.hir->rootModule, path, start);
    if (auto* e = rv.macro.binding.opt_MacroRules()) {
        if (e->crate_ == nullptr) {
            e->crate_ = &ec;
        }
    }
    return rv;
}

::AST::Path::Bindings ResolveUseGetBinding(
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
    if (path.cls.is_Absolute() && (path.cls.as_Absolute().crate != "" && path.cls.as_Absolute().crate != crate.crateNameReal)) {
        const auto& path_abs = path.cls.as_Absolute();
        // Builtin macro imports
        if (path_abs.crate == rcstring_crate_builtins) {
            ::AST::Path::Bindings rv;
            ASSERT_BUG(span, !path_abs.nodes.empty(), "");
            if (coretypeFromstring(path.nodes()[0].name().c_str()) != CORETYPE_INVAL) {
                rv.type.set(AST::AbsolutePath(rcstring_crate_builtins, {path_abs.nodes.back().name()}), AST::PathBindingType::make_TypeAlias({nullptr}));
            } else {
                rv.macro.set(AST::AbsolutePath(rcstring_crate_builtins, {path_abs.nodes.back().name()}), AST::PathBindingMacro::make_MacroRules({nullptr}));
            }
            return rv;
        }

        ASSERT_BUG(span, crate.externCrates.count(path_abs.crate.c_str()), "Crate '" << path_abs.crate << "' not loaded");
        return ResolveUseGetBindingExt(span, crate, path, crate.externCrates.at(path_abs.crate.c_str()), 0);
    }

    ::AST::Path::Bindings rv;

    const AST::Module* mod = &crate.rootModule;
    const auto& nodes = path.nodes();
    if (nodes.size() == 0) {
        // An import of the root.
        rv.type.set(mod->path(), ::AST::PathBindingType::make_Module({mod, {nullptr}}));
        return rv;
    }

    std::vector<const AST::Module*> inner_parent_modules;
    for (unsigned int i = 0; i < nodes.size() - 1; i++) {
        DEBUG("Component " << nodes.at(i).name());
        // TODO: If this came from an import, return the real path?

        //rv = Resolve_Use_CanoniseAndBind_Mod(span, crate, *mod, mv$(rv), nodes[i].name(), parent_modules, Lookup::Type);
        //const auto& b = rv.binding();
        assert(mod);
        auto b = ResolveUseGetBindingMod(span, crate, source_mod_path, *mod, nodes.at(i).name(), inner_parent_modules, /*types_only=*/true);
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
                return ResolveUseGetBindingExt(span, crate, path, *e.crate_, i + 1);
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
                    size_t idx = enum_.findVariant(node2.name());
                    if (idx == ~0u) {
                        ERROR(span, E0000, "Unknown enum variant " << path);
                    }
                TU_MATCH_HDRA( (enum_.mData), {)
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
                        if (var.mName == node2.name()) {
                            is_value = !var.mData.is_Struct();
                            break;
                        }
                        variant_index++;
                    }
                    if (variant_index == enum_.variants().size()) {
                        ERROR(span, E0000, "Unknown enum variant '" << node2.name() << "'");
                    }

                    DEBUG("AST Enum variant - " << variant_index << ", is_value=" << is_value << " " << enum_.variants()[variant_index].mData.tag_str());
                }
                if (is_value) {
                    rv.value.set(b.type.path + node2.name(), ::AST::PathBindingValue::make_EnumVar({e.enum_, variant_index, e.hir}));
                } else {
                    rv.type.set(b.type.path + node2.name(), ::AST::PathBindingType::make_EnumVar({e.enum_, variant_index, e.hir}));
                }
                return rv;
            }
            TU_ARMA(Module, e) {
                ASSERT_BUG(span, e.module_ || e.hir.mod, "nullptr module pointer in node " << i << " of " << path);
                if (!e.module_) {
                    assert(e.hir.crate);
                    assert(e.hir.mod);
                    return ResolveUseGetBindingExt(span, crate, *e.hir.crate, *e.hir.mod, path, i + 1, b.type.path);
                }
                inner_parent_modules.push_back(mod);
                mod = e.module_;
            }
        }
    }

    assert(mod);
    return ResolveUseGetBindingMod(span, crate, source_mod_path, *mod, nodes.back().name(), parent_modules, types_only);
}

//::AST::PathBinding_Macro Resolve_Use_GetBinding_Macro(const Span& span, const ::AST::Crate& crate, const ::AST::Path& path, ::std::span< const ::AST::Module* > parent_modules)
//{
//    throw "";
//}
