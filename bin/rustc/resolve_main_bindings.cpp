#include "resolve_main_bindings.h"

#include "ast_ast.h"
#include "hir_hir.h"
#include <span> // std::span
#include "ast_expr.h"
#include "settings.h"
#include "ast_crate.h"
#include "wire_board.h"
#include "pop_on_drop.h"
#include "main_bindings.h"
#include "macro_rules_macro_rules.h"

#define FLAG_CONST_GENERIC (1u << 31)

namespace {
    static const RcString rcstringSelf = RcString::newInterned("Self");

    ASTAbsolutePath spToAp(const HIRSimplePath& sp) {
        return ASTAbsolutePath(sp.crateName(), sp.componentsVec());
    }

    struct GenericSlot {
        enum class Level {
            Top,
            Method,
            UnusedPlaceholder,
            Hrb,
        } level;
        unsigned short index;

        unsigned int toBinding() const {
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

    // Definitions generated from resolve_ctx_ent.tu.
    #include "resolve_ctx_ent_tu.h"

    struct Context {

        const ASTCrate& crate;
        const ASTModule& mod;
        ::std::vector<Ent> nameContext;

        struct PatternStackEnt {
            unsigned firstArmDone = false;
            std::set<Ident> createdVariables;
            std::set<Ident> firstArmVariables;
        };

        ::std::vector<PatternStackEnt> patternStack;
        unsigned int varCount;
        unsigned int blockLevel;

        /// Index of the `Self` carried in from a method around this item, if
        /// any: it names a constructor here, but not a type. Anything pushed
        /// above it is a `Self` of this item's own and names both.
        size_t selfCtorOnlyIdx = ~size_t(0);

        // Destination `GenericParams` for in_band_lifetimes
        ASTGenericParams* iblTargetGenerics;

        const Settings& settings;

        // Pool that owns AST type nodes created during resolution.
        stl::ObjPool& typePool() const {
            return *crate.pool;
        }

        Context(const Settings& settings, const ASTCrate& crate, const ASTModule& mod)
            : settings(settings)
            , crate(crate)
            , mod(mod)
            , varCount(~0u)
            , blockLevel(0)
            , iblTargetGenerics(nullptr)
        {
        }

        void push(const ASTHigherRankedBounds& params) {
            auto e = Ent::make_Generic({GenericSlot::Level::Hrb, nullptr /*, &params*/});
            auto& data = e.as_Generic();

            for (size_t i = 0; i < params.lifetimes.size(); i++) {
                data.lifetimes.push_back(NamedI<GenericSlot>{params.lifetimes[i].name(), GenericSlot{GenericSlot::Level::Hrb, static_cast<unsigned short>(i)}});
            }

            nameContext.push_back(mv$(e));
        }

        /// A generic parameter of an item may not shadow one of the item around
        /// it: `trait T<'a> { fn f<'a>(); }` is an error, not a new `'a`.
        void checkGenericNotShadowed(const Span& sp, const RcString& name, const char* what) const {
            for (auto it = nameContext.rbegin(); it != nameContext.rend(); ++it) {
                const auto* g = it->opt_Generic();
                if (!g) {
                    continue;
                }
                if (g->level == GenericSlot::Level::Hrb) {
                    // A `for<..>` binder is its own scope, and shadowing one is
                    // a lint in rustc rather than an error.
                    continue;
                }
                // The lists spell their names differently: a lifetime and a
                // const keep the identifier they were written with, a type just
                // its name.
                auto namedI = [&](const auto& list) {
                    for (const auto& v : list) {
                        if (v.name.name == name) {
                            return true;
                        }
                    }
                    return false;
                };
                auto named = [&](const auto& list) {
                    for (const auto& v : list) {
                        if (v.name == name) {
                            return true;
                        }
                    }
                    return false;
                };
                if (namedI(g->lifetimes) || named(g->types) || namedI(g->constants)) {
                    ERROR(sp, E0000, "the name `" << name << "` is already used for a generic parameter in this item's generic parameters");
                }
            }
        }

        void push(/*const */ ASTGenericParams& params, GenericSlot::Level level, bool hasSelf = false, bool allowShadowing = false) {
            auto e = Ent::make_Generic({level, &params});
            auto& data = e.as_Generic();

            if (hasSelf) {
                data.types.push_back(Named<GenericSlot>{rcstringSelf, GenericSlot{level, GENERICSelf}});
                nameContext.push_back(Ent::make_ConcreteSelf(nullptr));
            }
            if (!params.params.empty()) {
                unsigned short lftIdx = 0;
                unsigned short tyIdx = 0;
                unsigned short valIdx = 0;
                for (const auto& e : params.params) {
                    switch (e.tag()) {
                        case GenericParam::TAG_None: {
                            break;
                        }
                        case GenericParam::TAG_Lifetime: {
                            auto& lft = e.as_Lifetime();
                            if (!allowShadowing) {
                                checkGenericNotShadowed(Span(), lft.name().name, "lifetime");
                            }
                            data.lifetimes.push_back(NamedI<GenericSlot>{lft.name(), GenericSlot{level, lftIdx}});
                            lftIdx += 1;
                            break;
                        }
                        case GenericParam::TAG_Type: {
                            auto& tyDef = e.as_Type();
                            if (!allowShadowing) {
                                checkGenericNotShadowed(Span(), tyDef.name(), "type");
                            }
                            data.types.push_back(Named<GenericSlot>{tyDef.name(), GenericSlot{level, tyIdx}});
                            tyIdx += 1;
                            break;
                        }
                        case GenericParam::TAG_Value: {
                            auto& valDef = e.as_Value();
                            if (!allowShadowing) {
                                checkGenericNotShadowed(Span(), valDef.name().name, "const");
                            }
                            data.constants.push_back(NamedI<GenericSlot>{valDef.name(), GenericSlot{level, valIdx}});
                            valIdx += 1;
                            break;
                        }
                    }
                }
            }

            nameContext.push_back(mv$(e));
        }

        void pop(const ASTHigherRankedBounds&) {
            if (!nameContext.back().is_Generic()) {
                BUG(Span(), "resolve/absolute.cpp - Context::pop(GenericParams) - Mismatched pop");
            }
            nameContext.pop_back();
        }

        void pop(const ASTGenericParams&, bool hasSelf = false) {
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

        void push(const ASTModule& mod) {
            nameContext.push_back(Ent::make_Module({&mod}));
        }

        void pop(const ASTModule& mod) {
            if (!nameContext.back().is_Module()) {
                BUG(Span(), "resolve/absolute.cpp - Context::pop(GenericParams) - Mismatched pop");
            }
            nameContext.pop_back();
        }

        class RootBlockScope {
            friend struct Context;
            Context& ctxt;
            unsigned int oldVarcount;

            RootBlockScope(Context& ctxt, unsigned int val)
                : ctxt(ctxt)
                , oldVarcount(ctxt.varCount)
            {
                ctxt.varCount = val;
            }

        public:
            ~RootBlockScope() {
                ctxt.varCount = oldVarcount;
            }
        };

        RootBlockScope enterRootblock() {
            return RootBlockScope(*this, 0);
        }

        RootBlockScope clearRootblock() {
            return RootBlockScope(*this, ~0u);
        }

        void pushSelf(ASTType*& tr) {
            // Store the address of the caller's (stable) self-type slot, NOT a
            // by-value parameter copy — the ConcreteSelf entry must stay valid
            // (and see in-place resolution of the self type) until popSelf.
            nameContext.push_back(Ent::make_ConcreteSelf(&tr));
        }

        void popSelf(ASTType* tr) {
            if (nameContext.back().is_ConcreteSelf()) {
                nameContext.pop_back();
            }
            else {
                BUG(Span(), "resolve/absolute.cpp - Context::pop(ASTType*) - Mismatched pop");
            }
        }

        ::ASTType* getSelf() const {
            for (auto it = nameContext.rbegin(); it != nameContext.rend(); ++it) {
                switch ((*it).tag()) {
                    case Ent::TAG_ConcreteSelf: {
                        auto& e = (*it).as_ConcreteSelf();
                        if (false && e) { return (*e)->clone(); } else { return ::mkType(typePool(), Span(), rcstringSelf, GENERICSelf); }
                        break;
                    }
                    default: {
                        break;
                    }
                }
            }

            TODO(Span(), "Error when get_self called with no self");
        }

        ::ASTType* const* getSelfOpt() const {
            for (auto it = nameContext.rbegin(); it != nameContext.rend(); ++it) {
                if (const auto* e = it->opt_ConcreteSelf()) {
                    return *e;
                }
            }
            return nullptr;
        }

        void pushBlock() {
            blockLevel += 1;
            DEBUG("Push block to " << blockLevel);
        }

        void pushMacroDefinition(unsigned int definitionId, const Ident::Hygiene& tokenHygiene, const Ident::Hygiene& definitionHygiene) {
            assert(blockLevel > 0);
            nameContext.push_back(Ent::make_MacroDefinition({blockLevel, definitionId, tokenHygiene, definitionHygiene}));
        }

        unsigned int pushVar(const Span& sp, const Ident& name) {
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

        void popBlock() {
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
        void startPatbind() {
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

        static const char* lookupModeMsg(LookupMode mode) {
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

        ASTPath lookup(const Span& sp, const RcString& name, const Ident::Hygiene& srcContext, LookupMode mode) const {
            auto rv = this->lookupOpt(sp, name, srcContext, mode);
            if (!rv.isValid()) {
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

        /// Two globs offering one name shadow nothing: the name is an error
        /// where it is used, not where it was brought in.
        static void checkUnambiguous(const Span& sp, const ASTModule& mod, const RcString& name, const ASTModule::IndexEnt& ent) {
            if (ent.ambiguous) {
                ERROR(sp, E0000, "`" << name << "` is ambiguous: more than one glob import in " << mod.path() << " provides it");
            }
        }

        static bool lookupInMod(const Span& sp, const ASTModule& mod, const RcString& name, LookupMode mode, ASTPath& path) {
            switch (mode) {
                case LookupMode::Namespace: {
                    auto v = mod.namespaceItems.find(name);
                    if (v != mod.namespaceItems.end()) {
                        DEBUG("- " << mod.path() << " NS: Namespace " << v->second.path);
                        checkUnambiguous(sp, mod, name, v->second);
                        path = ASTPath(v->second.path);
                        return true;
                    }
                }
                    {
                        auto v = mod.typeItems.find(name);
                        if (v != mod.typeItems.end()) {
                            DEBUG("- " << mod.path() << " NS: Type " << v->second.path);
                            checkUnambiguous(sp, mod, name, v->second);
                            path = ASTPath(v->second.path);
                            return true;
                        }
                    }
                    break;

                case LookupMode::Type:
                case LookupMode::PatternType: {
                    auto v = mod.typeItems.find(name);
                    if (v != mod.typeItems.end()) {
                        DEBUG("- " << mod.path() << " TY: Type " << v->second.path);
                        checkUnambiguous(sp, mod, name, v->second);
                        path = ASTPath(v->second.path);
                        return true;
                    }
                }
                    // HACK: For `Enum::Var { .. }` patterns matching value variants
                    if (mode == LookupMode::PatternType) {
                        auto v = mod.valueItems.find(name);
                        if (v != mod.valueItems.end()) {
                            const auto& b = v->second.path.bindings.value;
                            if (/*const auto* be =*/b.binding.opt_EnumVar()) {
                                DEBUG("- " << mod.path() << " TY: Enum variant " << b.path);
                                checkUnambiguous(sp, mod, name, v->second);
                                path = ASTPath(b);
                                return true;
                            }
                        }
                    }
                    break;
                case LookupMode::PatternValue: {
                    auto v = mod.valueItems.find(name);
                    if (v != mod.valueItems.end()) {
                        const auto& b = v->second.path.bindings.value;
                        switch (b.binding.tag()) {
                            case ASTPathBindingValue::TAG_EnumVar:
                            case ASTPathBindingValue::TAG_Static:
                                DEBUG("- PV: Value " << v->second.path);
                                checkUnambiguous(sp, mod, name, v->second);
                                path = ASTPath(v->second.path);
                                return true;
                            case ASTPathBindingValue::TAG_Struct: {
                                const auto& be = b.binding.as_Struct();
                                // TODO: Restrict this to unit-like structs
                                if (be.struct_ && !be.struct_->data.is_Unit())
                                    ;
                                else if (be.hir && !be.hir->data.is_Unit())
                                    ;
                                else {
                                    DEBUG("- " << mod.path() << " PV: Value " << b.path);
                                    checkUnambiguous(sp, mod, name, v->second);
                                    path = ASTPath(b);
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
                        checkUnambiguous(sp, mod, name, v->second);
                        path = ASTPath(v->second.path);
                        return true;
                    }
                } break;
            }
            return false;
        }

        ASTPath lookupOpt(const Span& sp, const RcString& name, const Ident::Hygiene& srcContext, LookupMode mode) const {
            DEBUG("name=" << name << ", src_context=" << srcContext);
            auto lookupContext = srcContext;
            // NOTE: src_context may provide a module to search
            // TODO: This should be checked AFTER locals
            if (srcContext.hasModPath()) {
                const auto& mp = srcContext.modPath();
                DEBUG(mp);
                if (mp.crate != "") {
                    HIRSimplePath visPath{mp.crate, mp.ents};

                    static Span sp;
                    // External crate path
                    ASSERT_BUG(sp, crate.externCrates.count(mp.crate), "Crate not loaded for " << mp);
                    const auto& extCrate = crate.externCrates.at(mp.crate);
                    const HIRModule* mod = &extCrate.hir->rootModule;
                    for (const auto& n : mp.ents) {
                        ASSERT_BUG(sp, mod->modItems.count(n), "Node `" << n << "` missing in path " << mp);
                        const auto& i = *mod->modItems.at(n);
                        ASSERT_BUG(sp, i.ent.is_Module(), "Node `" << n << "` not a module in path " << mp);
                        mod = &i.ent.as_Module();
                    }
                    ASTPath::Bindings bindings;
                    const HIRSimplePath* truePath = nullptr;
                    switch (mode) {
                        case LookupMode::Constant:
                        case LookupMode::PatternValue:
                        case LookupMode::Variable: {
                            auto it = mod->valueItems.find(name);
                            if (it != mod->valueItems.end()) {
                                const auto* item = &it->second->ent;
                                auto itemPath = ASTAbsolutePath(mp.crate, mp.ents) + name;
                                if (item->is_Import()) {
                                    const auto& imp = item->as_Import();
                                    // Set the true path (so the returned path is canonical)
                                    truePath = &imp.path;

                                    auto itemPath = spToAp(imp.path) + name;
                                    if (imp.isVariant) {
                                        const auto& enm = crate.externCrates.at(imp.path.crateName()).hir->getEnumByPath(sp, imp.path, /*ignore_crate_name*/ true, /*ignore_last*/ true);
                                        bindings.value.set(itemPath, ASTPathBindingValue::make_EnumVar({nullptr, imp.idx, &enm}));
                                        break; // Break out of the switch
                                    } else {
                                        item = &crate.externCrates.at(imp.path.crateName()).hir->getValitemByPath(sp, imp.path, true);
                                    }
                                }
                            switch ((*item).tag()) {
default:
                                TODO(sp, "Bind value '" << name << "' for module path " << mp << " : " << item->tagStr());
                                case HIRValueItem::TAG_Function: {
                                    bindings.value.set(itemPath, ASTPathBindingValue::make_Function({nullptr}));
                                    break;
                                }
                                case HIRValueItem::TAG_Static: {
                                    bindings.value.set(itemPath, ASTPathBindingValue::make_Static({nullptr}));
                                    break;
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
                                auto itemPath = ASTAbsolutePath(mp.crate, mp.ents) + name;
                                if (item->is_Import()) {
                                    const auto& imp = item->as_Import();
                                    // Set the true path (so the returned path is canonical)
                                    truePath = &imp.path;

                                    auto itemPath = spToAp(imp.path) + name;
                                    if (imp.isVariant) {
                                        const auto& enm = crate.externCrates.at(imp.path.crateName()).hir->getEnumByPath(sp, imp.path, /*ignore_crate_name*/ true, /*ignore_last*/ true);
                                        bindings.type.set(itemPath, ASTPathBindingType::make_EnumVar({nullptr, imp.idx, &enm}));
                                        break; // Break out of the switch
                                    } else {
                                        item = &crate.externCrates.at(imp.path.crateName()).hir->getTypeitemByPath(sp, imp.path, true);
                                    }
                                }
                            switch ((*item).tag()) {
default:
                                TODO(sp, "Bind type/mod '" << name << "' for module path " << mp << " : " << item->tagStr());
                                case HIRTypeItem::TAG_Module: {
                                    auto& e = (*item).as_Module();
                                    bindings.type.set(itemPath, ASTPathBindingType::make_Module({nullptr, {&extCrate, &e}}));
                                    break;
                                }
                                case HIRTypeItem::TAG_Trait: {
                                    bindings.type.set(itemPath, ASTPathBindingType::make_Trait({nullptr}));
                                    break;
                                }
                                case HIRTypeItem::TAG_TypeAlias: {
                                    bindings.type.set(itemPath, ASTPathBindingType::make_TypeAlias({nullptr}));
                                    break;
                                }
                                case HIRTypeItem::TAG_Struct: {
                                    bindings.type.set(itemPath, ASTPathBindingType::make_Struct({nullptr}));
                                    break;
                                }
                                case HIRTypeItem::TAG_Enum: {
                                    bindings.type.set(itemPath, ASTPathBindingType::make_Enum({nullptr}));
                                    break;
                                }
                                case HIRTypeItem::TAG_Union: {
                                    bindings.type.set(itemPath, ASTPathBindingType::make_Union({nullptr}));
                                    break;
                                }
                            }
                            }
                        } break;
                    }
                    // If any bindings were populated, then generate a path
                    if (bindings.hasBinding()) {
                        auto rv = ASTPath(mp.crate, {});
                        if (truePath) {
                            rv.cls.as_Absolute().crate = truePath->crateName();
                            for (const auto& e : truePath->components()) {
                                rv.nodes().push_back(e);
                            }
                        } else {
                            for (const auto& e : mp.ents) {
                                rv.nodes().push_back(e);
                            }
                            rv.nodes().push_back(name);
                        }
                        rv.bindings = std::move(bindings);
                        return rv;
                    }
                    // Fall through
                } else {
                    const ASTModule* mod = &crate.rootModule();
                    for (const auto& node : mp.ents) {
                        const ASTModule* next = nullptr;
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
                            for (const auto& i : mod->items) {
                                if (i->name == node) {
                                    next = &i->data.as_Module();
                                    break;
                                }
                            }
                        }
                        ASSERT_BUG(Span(), next, "Failed to find module `" << node << "` in " << mod->path() << " for " << mp);
                        mod = next;
                    }
                    ASTPath rv;
                    if (this->lookupInMod(sp, *mod, name, mode, rv)) {
                        return rv;
                    }
                }
            }
            for (auto it = nameContext.rbegin(); it != nameContext.rend(); ++it) {
                switch ((*it).tag()) {
                    case Ent::TAG_Module: {
                        auto& e = (*it).as_Module();
                        DEBUG("- Module " << e.mod->path());
                        ASTPath rv;
                        if (this->lookupInMod(sp, *e.mod, name, mode, rv)) {
                            return rv;
                        }
                        break;
                    }
                    case Ent::TAG_ConcreteSelf: {
                        auto& e = (*it).as_ConcreteSelf();
                        DEBUG("- ConcreteSelf");
                        if (name == rcstringSelf) {
                            switch (mode) {
                                case LookupMode::PatternType:
                                case LookupMode::Type:
                                case LookupMode::Namespace: {
                                    if (static_cast<size_t>(nameContext.rend() - it) - 1 == this->selfCtorOnlyIdx) {
                                        break;
                                    }
                                    ASTPath rv(name);
                                    rv.bindings.type.set(ASTAbsolutePath(), ASTPathBindingType::make_TypeParameter({0xFFFF}));
                                    return rv;
                                }
                                case LookupMode::Constant:
                                case LookupMode::Variable:
                                    // TODO: Ensure validity? (I.e. that `Self` is a unit or tuple struct
                                    if (const auto* p = (*e)->data.opt_Path()) {
                                        // HACK! If `Self` points to a `type`, look through it
                                        // - rustc-1.90.0-src/compiler/rustc_codegen_llvm/src/context.rs:675
                                        if (const auto* pbe = (**p).bindings.type.binding.opt_TypeAlias()) {
                                            assert(pbe->alias_);
                                            assert(pbe->alias_->type_->isPath());
                                            return *pbe->alias_->type_->data.as_Path();
                                        }
                                        return **p;
                                    }
                                default:
                                    break;
                            }
                        }
                        break;
                    }
                    case Ent::TAG_VarBlock: {
                        auto& e = (*it).as_VarBlock();
                        DEBUG("- VarBlock");
                        assert(e.level <= blockLevel);
                        if (mode != LookupMode::Variable) {
                            // ignore
                        } else {
                            for (auto it2 = e.variables.rbegin(); it2 != e.variables.rend(); ++it2) {
                                if (it2->first.name == name) {
                                    DEBUG("> Match: Hygiene " << it2->first.hygiene << " check against src_context");
                                }
                                if (it2->first.name == name && it2->first.hygiene.isVisible(lookupContext)) {
                                    ASTPath rv(name);
                                    rv.bindVariable(it2->second);
                                    return rv;
                                }
                            }
                        }
                        break;
                    }
                    case Ent::TAG_MacroDefinition: {
                        auto& e = (*it).as_MacroDefinition();
                        if (mode == LookupMode::Variable) {
                            lookupContext.leaveMacroDefinition(typePool(), e.definitionId, e.tokenHygiene, e.definitionHygiene);
                        }
                        break;
                    }
                    case Ent::TAG_Generic: {
                        auto& e = (*it).as_Generic();
                        DEBUG("- Generic");
                        switch (mode) {
                            case LookupMode::Type:
                            case LookupMode::Namespace:
                                for (auto it2 = e.types.rbegin(); it2 != e.types.rend(); ++it2) {
                                    if (it2->name == name) {
                                        ASTPath rv(name);
                                        rv.bindings.type.set(ASTAbsolutePath(), ASTPathBindingType::make_TypeParameter({it2->value.toBinding()}));
                                        return rv;
                                    }
                                }
                                break;
                            case LookupMode::Variable:
                            case LookupMode::Constant:
                                for (auto it2 = e.constants.rbegin(); it2 != e.constants.rend(); ++it2) {
                                    if (it2->name.name == name) {
                                        ASTPath rv(name);
                                        rv.bindings.value.set(ASTAbsolutePath(), ASTPathBindingValue::make_Generic({it2->value.toBinding()}));
                                        return rv;
                                    }
                                }
                                break;
                            default:
                                // ignore.
                                // TODO: Integer generics
                                break;
                        }
                        break;
                    }
                }
            }

            // Top-level module
            DEBUG("- Top module (" << mod.path() << ")");
            ASTPath rv;
            if (this->lookupInMod(sp, mod, name, mode, rv)) {
                return rv;
            }

            DEBUG("- Primitives");
            switch (mode) {
                case LookupMode::Namespace:
                case LookupMode::Type: {
                    // Look up primitive types
                    auto ct = coretypeFromstring(name.c_str());
                    if (ct != CORETYPE_INVAL) {
                        return ASTPath::newUfcsTy(mkType(typePool(), Span(), ct), ::std::vector<ASTPathNode>());
                    }
                } break;
                default:
                    break;
            }

            // #![feature(extern_prelude)] - 2018-style extern paths
            if (mode == LookupMode::Namespace /*&& m_crate.has_feature("extern_prelude")*/) {
                DEBUG("Extern crates - " << this->settings.implicitCrates);
                auto it = this->settings.implicitCrates.find(name);
                if (it != this->settings.implicitCrates.end()) {
                    DEBUG("- Found '" << name << "' (= " << it->second << ")");
                    return ASTPath(it->second, {});
                }
            }

            return ASTPath();
        }

        unsigned int lookupLocal(const Span& sp, const RcString name, LookupMode mode) {
            for (auto it = nameContext.rbegin(); it != nameContext.rend(); ++it) {
                switch ((*it).tag()) {
                    case Ent::TAG_Module: {
                        break;
                    }
                    case Ent::TAG_ConcreteSelf: {
                        break;
                    }
                    case Ent::TAG_VarBlock: {
                        auto& e = (*it).as_VarBlock();
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
                        break;
                    }
                    case Ent::TAG_MacroDefinition: {
                        break;
                    }
                    case Ent::TAG_Generic: {
                        auto& e = (*it).as_Generic();
                        DEBUG("- Generic");
                        switch (mode) {
                            case LookupMode::Type:
                                for (auto it2 = e.types.rbegin(); it2 != e.types.rend(); ++it2) {
                                    if (it2->name == name) {
                                        return it2->value.toBinding();
                                    }
                                }
                                break;
                            case LookupMode::Variable:
                                for (auto it2 = e.constants.rbegin(); it2 != e.constants.rend(); ++it2) {
                                    if (it2->name.name == name) {
                                        //TODO(sp, "Return a reference to a constant generic '" << name << "'");
                                        // Need to disambiguate it... could set a high bit
                                        return it2->value.toBinding() | FLAG_CONST_GENERIC;
                                    }
                                }
                                break;
                            default:
                                // ignore.
                                // TODO: Integer generics
                                break;
                        }
                        break;
                    }
                }
            }

            ERROR(sp, E0000, "Unable to find local " << (mode == LookupMode::Variable ? "variable" : "type") << " '" << name << "'");
        }

        /// Clones the context, including only the module-level items (i.e. just the Module entries)
        Context cloneMod() const {
            auto rv = Context(this->settings, this->crate, this->mod);
            for (const auto& v : nameContext) {
                if (const auto* e = v.opt_Module()) {
                    rv.nameContext.push_back(Ent::make_Module(*e));
                }
            }
            // An item nested in a method still reaches the `Self` constructor:
            // rustc deprecated that but still accepts it, and only where the
            // outer item is not generic -- a generic there would name a
            // parameter this item does not have. `Self` as a type is not
            // carried over, so only a value lookup answers here.
            if (const auto* selfTy = this->getSelfOpt()) {
                if (*selfTy && (*selfTy)->isPath()) {
                    const auto& path = (*selfTy)->path();
                    bool isConcrete = true;
                    // A local path is a bare name, so it carries no arguments.
                    if (!path.cls.is_Local()) {
                        for (const auto& node : path.nodes()) {
                            isConcrete = isConcrete && node.args().isEmpty();
                        }
                    }
                    if (isConcrete) {
                        rv.selfCtorOnlyIdx = rv.nameContext.size();
                        rv.nameContext.push_back(Ent::make_ConcreteSelf(const_cast<ASTType**>(selfTy)));
                    }
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

void ResolveAbsolutePathBindAbsolute(Context& context, const Span& sp, Context::LookupMode& mode, ASTPath& path);
void ResolveAbsolutePath(/*const*/ Context& context, const Span& sp, Context::LookupMode mode, ASTPath& path);
void ResolveAbsoluteLifetime(Context& context, const Span& sp, ASTLifetimeRef& type);
void ResolveAbsoluteType(Context& context, ASTType*& type);
void ResolveAbsoluteExpr(Context& context, ASTExpr& expr);
void ResolveAbsoluteExprNode(Context& context, ASTExprNode& node);
void ResolveAbsolutePattern(Context& context, bool allowRefutable, ASTPattern& pat);
void ResolveAbsoluteMod(const Settings& settings, const ASTCrate& crate, ASTModule& mod);
void ResolveAbsoluteMod(Context itemContext, ASTModule& mod);

struct DelegationSignatureSource {
    const ASTFunction* ast = nullptr;
    const HIRFunction* hir = nullptr;
};

void ResolveAbsoluteFunction(
    Context& itemContext,
    ASTFunction& fcn,
    DelegationSignatureSource signatureSource = {},
    bool hasParentSelf = false,
    bool isTraitImpl = false
);
void ResolveAbsoluteStatic(Context& itemContext, ASTStatic& e);

void ResolveAbsolutePathParams(/*const*/ Context& context, const Span& sp, ASTPathParams& args) {
    for (auto& ent : args.entries) {
        switch (ent.tag()) {
            case ASTPathParamEnt::TAG_Null: {
                auto& _ = ent.as_Null();
                break;
            }
            case ASTPathParamEnt::TAG_Lifetime: {
                auto& l = ent.as_Lifetime();
                ResolveAbsoluteLifetime(context, sp, l);
                break;
            }
            case ASTPathParamEnt::TAG_Type: {
                auto& t = ent.as_Type();
                // A trivial path type might be refering to a generic value (e.g. `Foo<T,N>` where `N` is a const generic)
                if (t->data.is_Path() && t->data.as_Path()->isTrivial()) {
                    auto p = t->data.as_Path()->cls.as_Relative();
                    // If type lookup fails
                    auto newPath = context.lookupOpt(sp, p.nodes[0].name(), p.hygiene, Context::LookupMode::Type);
                    if (newPath == ASTPath()) {
                        // Try (constant) value lookup
                        auto newPath = context.lookupOpt(sp, p.nodes[0].name(), p.hygiene, Context::LookupMode::Constant);
                        if (newPath != ASTPath()) {
                            // If that lookup succeeds, then create a value (and visit it - just in case)
                            ent = ASTPathParamEnt::make_Value(new ASTExprNodeNamedValue(std::move(newPath)));
                            ResolveAbsoluteExprNode(context, *ent.as_Value());
                        } else {
                            // Otherwise, visit (which will most likely fail)
                            ResolveAbsoluteType(context, t);
                        }
                    } else {
                        // Normal type, update it then visit
                        *t->data.as_Path() = std::move(newPath);
                        ResolveAbsoluteType(context, t);
                    }
                } else {
                    ResolveAbsoluteType(context, t);
                }
                break;
            }
            case ASTPathParamEnt::TAG_Value: {
                auto& n = ent.as_Value();
                ResolveAbsoluteExprNode(context, *n);
                break;
            }
            case ASTPathParamEnt::TAG_AssociatedTyEqual: {
                auto& a = ent.as_AssociatedTyEqual();
                ResolveAbsolutePathParams(context, sp, a.first.args());
                ResolveAbsoluteType(context, a.second);
                break;
            }
            case ASTPathParamEnt::TAG_AssociatedTyBound: {
                auto& a = ent.as_AssociatedTyBound();
                ResolveAbsolutePathParams(context, sp, a.first.args());
                for (auto& p : a.second) {
                    context.push(p.hrbs);
                    ResolveAbsolutePath(context, sp, Context::LookupMode::Type, *p.path);
                    context.pop(p.hrbs);
                }
                break;
            }
        }
    }
}

void ResolveAbsolutePathNodes(/*const*/ Context& context, const Span& sp, ::std::vector<ASTPathNode>& nodes) {
    for (auto& node : nodes) {
        ResolveAbsolutePathParams(context, sp, node.args());
    }
}

void ResolveAbsolutePathBindUFCS(Context& context, const Span& sp, Context::LookupMode mode, ASTPath& path) {
    while (path.cls.as_UFCS().nodes.size() > 1) {
        // More than one node, break into inner UFCS
        // - Since traits can't be associated items, this will always be the same form

        auto span = path.cls.as_UFCS().type->span();
        auto nodes = mv$(path.cls.as_UFCS().nodes);
        auto innerPath = mv$(path);
        innerPath.cls.as_UFCS().nodes.push_back(mv$(nodes.front()));
        nodes.erase(nodes.begin());
        path = ASTPath::newUfcsTy(mkType(context.typePool(), span, mv$(innerPath)), mv$(nodes));
    }

    if (path.cls.as_UFCS().type) {
        ResolveAbsoluteType(context, path.cls.as_UFCS().type);
    }

    const auto& ufcs = path.cls.as_UFCS();
    if (ufcs.nodes.size() == 0) {
        if (mode == Context::LookupMode::Type && (!ufcs.trait || *ufcs.trait == ASTPath())) {
            return;
        }

        BUG(sp, "UFCS with no nodes encountered - " << path);
    }
    const auto& node = ufcs.nodes.at(0);

    if (ufcs.trait && ufcs.trait->isValid()) {
        // Trait is specified, definitely a trait item
        // - Must resolve here
        const auto& pb = ufcs.trait->bindings.type.binding;
        if (!pb.is_Trait()) {
            ERROR(sp, E0000, "UFCS trait was not a trait - " << *ufcs.trait);
        }
        if (!pb.as_Trait().trait_) {
            return;
        }
        assert(pb.as_Trait().trait_);
        const auto& tr = *pb.as_Trait().trait_;

        switch (mode) {
            // A qualified path names a type in a pattern the same way it does
            // anywhere else: `let <Foo as A>::Assoc { .. } = ..` matches the
            // struct the associated type resolves to.
            case Context::LookupMode::PatternValue:
            case Context::LookupMode::PatternType:
            case Context::LookupMode::Namespace:
            case Context::LookupMode::Type:
                for (const auto& item : tr.items()) {
                    if (item.name != node.name()) {
                        continue;
                    }
                    switch (item.data.tag()) {
                        case ASTItem::TAG_Type: {
                            // Resolve to asociated type
                            break;
                        }
                        default: {

                            // TODO: Error

                            break;
                        }
                    }
                }
                break;
            case Context::LookupMode::Constant:
            case Context::LookupMode::Variable:
                for (const auto& item : tr.items()) {
                    if (item.name != node.name()) {
                        continue;
                    }
                switch (item.data.tag()) {
default:
                    // TODO: Error
                    break;
                    case ASTItem::TAG_Function: {
                        auto& e = item.data.as_Function();
                        // Bind as trait method
                        path.bindings.value.set(ufcs.trait->bindings.type.path + item.name, ASTPathBindingValue::make_Function({&e}));
                        break;
                    }
                    case ASTItem::TAG_Static: {
                        // Resolve to asociated static
                        break;
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
    ASTPath splitIntoCrate(const Span& sp, ASTPath path, unsigned int start, const RcString& crateName) {
        auto& nodes = path.nodes();
        ASTPath np = ASTPath(crateName, {});
        for (unsigned int i = start; i < nodes.size(); i++) {
            np.nodes().push_back(mv$(nodes[i]));
        }
        np.bindings = path.bindings.clone();
        return np;
    }

    ASTPath splitIntoUfcsTy(stl::ObjPool& pool, const Span& sp, const ASTPath& path, unsigned int i /*item_name_idx*/) {
        const auto& pathAbs = path.cls.as_Absolute();
        auto typePath = ASTPath(path);
        typePath.cls.as_Absolute().nodes.resize(i + 1);
        //Resolve_Absolute_Path(

        auto newPath = ASTPath::newUfcsTy(::mkType(pool, sp, mv$(typePath)));
        for (unsigned int j = i + 1; j < pathAbs.nodes.size(); j++) {
            newPath.nodes().push_back(mv$(pathAbs.nodes[j]));
        }

        DEBUG(path << " -> " << newPath);

        return newPath;
    }

    ASTPath splitReplaceIntoUfcsPath(stl::ObjPool& pool, const Span& sp, ASTPath path, unsigned int i, const ASTPath& tyPathTpl) {
        auto& pathAbs = path.cls.as_Absolute();
        auto& n = pathAbs.nodes[i];

        auto typePath = ASTPath(tyPathTpl);
        if (!n.args().isEmpty()) {
            typePath.nodes().back().args() = mv$(n.args());
        }
        auto newPath = ASTPath::newUfcsTy(::mkType(pool, sp, mv$(typePath)));
        for (unsigned int j = i + 1; j < pathAbs.nodes.size(); j++) {
            newPath.nodes().push_back(mv$(pathAbs.nodes[j]));
        }

        return newPath;
    }

    void ResolveAbsolutePathBindAbsoluteHirFromImport(Context& context, const Span& sp, bool isValue, ASTPath& path, const HIRSimplePath& p) {
        TRACE_FUNCTION_FR("path=" << path << ", p=" << p, path);
        if (p.crateName() == CRATE_BUILTINS) {
            ASTPath rv(p.crateName(), {});
            rv.nodes().reserve(p.components().size());
            for (const auto& c : p.components()) {
                rv.nodes().push_back(ASTPathNode(c));
            }
            rv.nodes().back().args() = mv$(path.nodes().back().args());
            auto ap = spToAp(p);

            if (coretypeFromstring(p.components().back().c_str()) != CORETYPE_INVAL) {
                rv.bindings.type.set(ap, ASTPathBindingType::make_TypeAlias({nullptr}));
            } else {
                rv.bindings.macro.set(ap, ASTPathBindingMacro::make_MacroRules({nullptr}));
            }
            path = mv$(rv);
            return;
        }
        const auto& extCrate = context.crate.externCrates.at(p.crateName());
        const HIRModule* hmod = &extCrate.hir->rootModule;
        for (unsigned int i = 0; i < p.components().size() - 1; i++) {
            const auto& name = p.components()[i];
            auto it = hmod->modItems.find(name);
            if (it == hmod->modItems.end()) {
                ERROR(sp, E0000, "Couldn't find path component '" << name << "' of " << p);
            }

            switch (it->second->ent.tag()) {
default:
                TODO(sp, "Unknown item type in path - " << i << " " << p << " - " << it->second->ent.tagStr());
                case HIRTypeItem::TAG_Enum: {
                    auto& e = it->second->ent.as_Enum();
                    if (i != p.components().size() - 2) {
                        ERROR(sp, E0000, "Enum as path component in unexpected location - " << p);
                    }
                    const auto& varname = p.components().back();
                    auto varIdx = e.findVariant(varname);
                    ASSERT_BUG(sp, varIdx != SIZE_MAX, "Extern crate import path points to non-present variant - " << p);

                    // Construct output path (with same set of parameters)
                    ASTPath rv(p.crateName(), {});
                    rv.nodes().reserve(p.components().size());
                    for (const auto& c : p.components()) {
                        rv.nodes().push_back(ASTPathNode(c));
                    }
                    rv.nodes().back().args() = mv$(path.nodes().back().args());
                    auto ap = spToAp(p);
                    if (e.data.is_Data() && e.data.as_Data()[varIdx].isStruct) {
                        rv.bindings.type.set(ap, ASTPathBindingType::make_EnumVar({nullptr, static_cast<unsigned>(varIdx), &e}));
                    } else {
                        rv.bindings.value.set(ap, ASTPathBindingValue::make_EnumVar({nullptr, static_cast<unsigned>(varIdx), &e}));
                    }
                    path = mv$(rv);

                    return;
                }
                case HIRTypeItem::TAG_Module: {
                    auto& e = it->second->ent.as_Module();
                    hmod = &e;
                    break;
                }
            }
        }

        ASTPath::Bindings pb;

        const auto& name = p.components().back();
        auto ap = spToAp(p);
        if (isValue) {
            auto it = hmod->valueItems.find(name);
            if (it == hmod->valueItems.end()) {
                ERROR(sp, E0000, "Couldn't find final component of " << p);
            }
            ASTPathBindingValue pbv;
            switch (it->second->ent.tag()) {
                case HIRValueItem::TAG_Import: {
                    // Wait? is this even valid?
                    BUG(sp, "HIR Import item pointed to an import");
                    break;
                }
                case HIRValueItem::TAG_Constant: {
                    pbv = ASTPathBindingValue::make_Static({nullptr, nullptr});
                    break;
                }
                case HIRValueItem::TAG_Static: {
                    pbv = ASTPathBindingValue::make_Static({nullptr, it->second->ent.as_Static()});
                    break;
                }
                case HIRValueItem::TAG_StructConstant: {
                    auto& e = it->second->ent.as_StructConstant();
                    pbv = ASTPathBindingValue::make_Struct({nullptr, &extCrate.hir->getTypeitemByPath(sp, e.ty, true).as_Struct()});
                    break;
                }
                case HIRValueItem::TAG_Function: {
                    pbv = ASTPathBindingValue::make_Function({nullptr /*, &e*/});
                    break;
                }
                case HIRValueItem::TAG_StructConstructor: {
                    auto& e = it->second->ent.as_StructConstructor();
                    pbv = ASTPathBindingValue::make_Struct({nullptr, &extCrate.hir->getTypeitemByPath(sp, e.ty, true).as_Struct()});
                    break;
                }
            }
            pb.value.set( ::std::move(ap), ::std::move(pbv) );
        } else {
            auto it = hmod->modItems.find(name);
            if (it == hmod->modItems.end()) {
                ERROR(sp, E0000, "Couldn't find final component of " << p);
            }
            ASTPathBindingType pbt;
            switch (it->second->ent.tag()) {
                case HIRTypeItem::TAG_Import: {
                    // Wait? is this even valid?
                    BUG(sp, "HIR Import item pointed to an import");
                    break;
                }
                case HIRTypeItem::TAG_Module: {
                    auto& e = it->second->ent.as_Module();
                    pbt = ASTPathBindingType::make_Module({nullptr, {&extCrate, &e}});
                    break;
                }
                case HIRTypeItem::TAG_Trait: {
                    auto& e = it->second->ent.as_Trait();
                    pbt = ASTPathBindingType::make_Trait({nullptr, &e});
                    break;
                }
                case HIRTypeItem::TAG_TraitAlias: {
                    auto& e = it->second->ent.as_TraitAlias();
                    pbt = ASTPathBindingType::make_TraitAlias({nullptr, &e});
                    break;
                }
                case HIRTypeItem::TAG_TypeAlias: {
                    pbt = ASTPathBindingType::make_TypeAlias({nullptr /*, &e*/});
                    break;
                }
                case HIRTypeItem::TAG_ExternType: {
                    pbt = ASTPathBindingType::make_TypeAlias({nullptr /*, &e*/});
                    break;
                }
                case HIRTypeItem::TAG_Struct: {
                    auto& e = it->second->ent.as_Struct();
                    pbt = ASTPathBindingType::make_Struct({nullptr, &e});
                    break;
                }
                case HIRTypeItem::TAG_Union: {
                    auto& e = it->second->ent.as_Union();
                    pbt = ASTPathBindingType::make_Union({nullptr, &e});
                    break;
                }
                case HIRTypeItem::TAG_Enum: {
                    auto& e = it->second->ent.as_Enum();
                    pbt = ASTPathBindingType::make_Enum({nullptr, &e});
                    break;
                }
            }
            pb.type.set( ::std::move(ap), ::std::move(pbt) );
        }

        // Construct output path (with same set of parameters)
        ASTPath rv(p.crateName(), {});
        rv.nodes().reserve(p.components().size());
        for (const auto& c : p.components()) {
            rv.nodes().push_back(ASTPathNode(c));
        }
        rv.nodes().back().args() = mv$(path.nodes().back().args());
        rv.bindings = mv$(pb);
        path = mv$(rv);
    }

    void ResolveAbsolutePathBindAbsoluteHirFrom(Context& context, const Span& sp, Context::LookupMode& mode, ASTPath& path, const ASTExternCrate& crate, unsigned int start) {
        assert(crate.hir->crateName == crate.name);
        TRACE_FUNCTION_FR(crate.hir->crateName << " - " << path << " start=" << start, path);
        auto& pathAbs = path.cls.as_Absolute();

        if (pathAbs.nodes.empty()) {
            switch (mode) {
                case Context::LookupMode::Namespace:
                    path.bindings.type.set({crate.name, {}}, ASTPathBindingType::make_Module({nullptr, {&crate, &crate.hir->rootModule}}));
                    return;
                default:
                    TODO(sp, "Looking up a non-namespace, but pointed to crate root");
            }
        }

        const HIRModule* hmod = &crate.hir->rootModule;
        for (unsigned int i = start; i < pathAbs.nodes.size() - 1; i++) {
            auto& n = pathAbs.nodes[i];
            assert(hmod);
            auto it = hmod->modItems.find(n.name());
            if (it == hmod->modItems.end()) {
                ERROR(sp, E0000, "Couldn't find path component '" << n.name() << "' of " << path);
            }

            switch (it->second->ent.tag()) {
                case HIRTypeItem::TAG_Import: {
                    auto& e = it->second->ent.as_Import();
                    DEBUG("`" << n.name() << "`: Import " << e.path);
                    // - Update path then restart
                    auto newpath = ASTPath(e.path.crateName(), {});
                    for (const auto& n : e.path.components()) {
                        newpath.nodes().push_back(ASTPathNode(n));
                    }
                    if (newpath.nodes().empty()) {
                        ASSERT_BUG(sp, n.args().isEmpty(), "Params present, but name resolves to a crate root - " << path << " #" << i << " -> " << newpath);
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
                case HIRTypeItem::TAG_Module: {
                    auto& e = it->second->ent.as_Module();
                    hmod = &e;
                    break;
                }
                case HIRTypeItem::TAG_TraitAlias: {
                    //for(const auto& trait_path_hir : e.m_traits)
                    //{
                    //}
                    TODO(sp, "Path referring to a trait alias - " << path);
                    break;
                }
                case HIRTypeItem::TAG_Trait: {
                    auto& e = it->second->ent.as_Trait();
                    ASTAbsolutePath ap(crate.name, {});
                    for (unsigned int j = start; j <= i; j++) {
                        ap.nodes.push_back(pathAbs.nodes[j].name());
                    }
                    ASTPathParams pp;
                    if (!n.args().isEmpty()) {
                        pp = mv$(n.args());
                    } else {
                        for (const auto& typ : e.params.types) {
                            pp.entries.push_back(::mkType(context.typePool(), sp));
                        }
                    }
                    ASTPath traitPath(ap, std::move(pp));
                    traitPath.bindings.type.set(::std::move(ap), ASTPathBindingType::make_Trait({nullptr, &e}));

                    ASTPath newPath;
                    const auto& nextNode = pathAbs.nodes[i + 1];
                    // If the named item can't be found in the trait, fall back to it being a type binding
                    // - What if this item is from a nested trait?
                    bool found = false;
                    switch (i + 1 < pathAbs.nodes.size() ? Context::LookupMode::Namespace : mode) {
                        case Context::LookupMode::Namespace:
                        case Context::LookupMode::Type:
                        case Context::LookupMode::PatternType:
                            found = (e.types.find(nextNode.name()) != e.types.end());
                        case Context::LookupMode::PatternValue:
                        case Context::LookupMode::Constant:
                        case Context::LookupMode::Variable:
                            found = (e.values.find(nextNode.name()) != e.values.end());
                            break;
                    }

                    if (!found) {
                        newPath = ASTPath::newUfcsTy(::mkType(context.typePool(), sp, mv$(traitPath)));
                    } else {
                        newPath = ASTPath::newUfcsTrait(::mkType(context.typePool(), sp), mv$(traitPath));
                    }
                    for (unsigned int j = i + 1; j < pathAbs.nodes.size(); j++) {
                        newPath.nodes().push_back(mv$(pathAbs.nodes[j]));
                    }

                    path = mv$(newPath);
                    return ResolveAbsolutePathBindUFCS(context, sp, mode, path);
                }
case HIRTypeItem::TAG_ExternType:
                case HIRTypeItem::TAG_TypeAlias:
                case HIRTypeItem::TAG_Struct:
                case HIRTypeItem::TAG_Union:
                    path = splitIntoCrate(sp, mv$(path), start, crate.name);
                    path = splitIntoUfcsTy(context.typePool(), sp, mv$(path), i - start);
                    return ResolveAbsolutePathBindUFCS(context, sp, mode, path);
                case HIRTypeItem::TAG_Enum: {
                    auto& e = it->second->ent.as_Enum();
                    if (i + 1 < pathAbs.nodes.size()) {
                        auto& nextNode = pathAbs.nodes[i + 1];
                        // If this refers to an enum variant, return the full path
                        // - Otherwise, assume it's an associated type?
                        auto idx = e.findVariant(nextNode.name());
                        if (idx != SIZE_MAX) {
                            if (i != pathAbs.nodes.size() - 2) {
                                ERROR(sp, E0000, "Unexpected enum in path " << path);
                            }

                            ASTAbsolutePath ap(crate.name, {});
                            auto traitPath = ASTPath(crate.name, {});
                            for (unsigned int j = start; j < pathAbs.nodes.size(); j++) {
                                ap.nodes.push_back(pathAbs.nodes[j].name());
                            }

                            // NOTE: Type parameters for enums go after the _variant_
                            if (!n.args().isEmpty()) {
                                if (nextNode.args().isEmpty()) {
                                    DEBUG("Moving type params from on the enum to the variant");
                                    nextNode.args() = std::move(n.args());
                                } else {
                                    ERROR(sp, E0000, "Type parameters were not expected here (enum params go on the variant)");
                                }
                            }

                            if (e.data.is_Data() && e.data.as_Data()[idx].isStruct) {
                                path.bindings.type.set(ap, ASTPathBindingType::make_EnumVar({nullptr, static_cast<unsigned int>(idx), &e}));
                            } else {
                                path.bindings.value.set(ap, ASTPathBindingValue::make_EnumVar({nullptr, static_cast<unsigned int>(idx), &e}));
                            }
                            path = splitIntoCrate(sp, mv$(path), start, crate.name);
                            return;
                        }
                    }
                    path = splitIntoCrate(sp, mv$(path), start, crate.name);
                    path = splitIntoUfcsTy(context.typePool(), sp, mv$(path), i - start);
                    return ResolveAbsolutePathBindUFCS(context, sp, mode, path);
                }
            }
        }

        ASTAbsolutePath ap(crate.name, {});
        auto traitPath = ASTPath(crate.name, {});
        for (unsigned int j = start; j < pathAbs.nodes.size(); j++) {
            ap.nodes.push_back(pathAbs.nodes[j].name());
        }

        const auto& name = pathAbs.nodes.back().name();
        switch (mode) {
            // TODO: Don't bind to a Module if LookupMode::Type
            case Context::LookupMode::Namespace:
            case Context::LookupMode::Type:
            case Context::LookupMode::PatternType: {
                auto v = hmod->modItems.find(name);
                if (v != hmod->modItems.end()) {
                    ASTPathBindingType pbt;
                    switch (v->second->ent.tag()) {
                        case HIRTypeItem::TAG_Import: {
                            auto& e = v->second->ent.as_Import();
                            DEBUG("= Import " << e.path);
                            ResolveAbsolutePathBindAbsoluteHirFromImport(context, sp, false, path, e.path);
                            return;
                        }
                        case HIRTypeItem::TAG_Trait: {
                            auto& e = v->second->ent.as_Trait();
                            pbt = ASTPathBindingType::make_Trait({nullptr, &e});
                            break;
                        }
                        case HIRTypeItem::TAG_TraitAlias: {
                            auto& e = v->second->ent.as_TraitAlias();
                            pbt = ASTPathBindingType::make_TraitAlias({nullptr, &e});
                            break;
                        }
                        case HIRTypeItem::TAG_Module: {
                            auto& e = v->second->ent.as_Module();
                            pbt = ASTPathBindingType::make_Module({nullptr, {&crate, &e}});
                            break;
                        }
                        case HIRTypeItem::TAG_ExternType: {
                            pbt = ASTPathBindingType::make_TypeAlias({nullptr /*, &e*/});
                            break;
                        }
                        case HIRTypeItem::TAG_TypeAlias: {
                            pbt = ASTPathBindingType::make_TypeAlias({nullptr /*, &e*/});
                            break;
                        }
                        case HIRTypeItem::TAG_Enum: {
                            auto& e = v->second->ent.as_Enum();
                            pbt = ASTPathBindingType::make_Enum({nullptr, &e});
                            break;
                        }
                        case HIRTypeItem::TAG_Struct: {
                            auto& e = v->second->ent.as_Struct();
                            pbt = ASTPathBindingType::make_Struct({nullptr, &e});
                            break;
                        }
                        case HIRTypeItem::TAG_Union: {
                            auto& e = v->second->ent.as_Union();
                            pbt = ASTPathBindingType::make_Union({nullptr, &e});
                            break;
                        }
                    }
                    path.bindings.type.set(::std::move(ap), ::std::move(pbt));
                    // Update path (trim down to `start` and set crate name)
                    path = splitIntoCrate(sp, mv$(path), start,  crate.name);
                    return ;
                }
            } break;

            case Context::LookupMode::PatternValue: {
                auto v = hmod->valueItems.find(name);
                if (v != hmod->valueItems.end()) {
                    switch (v->second->ent.tag()) {
default:
                        DEBUG("Ignore - " << v->second->ent.tagStr());
                        break;
                        case HIRValueItem::TAG_StructConstant: {
                            auto& e = v->second->ent.as_StructConstant();
                            auto tyPath = e.ty;
                            path.bindings.value.set(::std::move(ap), ASTPathBindingValue::make_Struct({nullptr, &crate.hir->getStructByPath(sp, tyPath)}));
                            path = splitIntoCrate(sp, mv$(path), start, crate.name);
                            return;
                        }
                        case HIRValueItem::TAG_Import: {
                            auto& e = v->second->ent.as_Import();
                            ResolveAbsolutePathBindAbsoluteHirFromImport(context, sp, true, path, e.path);
                            return;
                        }
                        case HIRValueItem::TAG_Constant: {
                            // Bind and update path
                            path.bindings.value.set(::std::move(ap), ASTPathBindingValue::make_Static({nullptr, nullptr}));
                            path = splitIntoCrate(sp, mv$(path), start, crate.name);
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
                    ASTPathBindingValue pbv;
                    switch (v->second->ent.tag()) {
                        case HIRValueItem::TAG_Import: {
                            auto& e = v->second->ent.as_Import();
                            ResolveAbsolutePathBindAbsoluteHirFromImport(context, sp, true, path, e.path);
                            return;
                        }
                        case HIRValueItem::TAG_Function: {
                            pbv = ASTPathBindingValue::make_Function({nullptr /*, &e*/});
                            break;
                        }
                        case HIRValueItem::TAG_StructConstructor: {
                            auto& e = v->second->ent.as_StructConstructor();
                            auto tyPath = e.ty;
                            pbv = ASTPathBindingValue::make_Struct({nullptr, &crate.hir->getStructByPath(sp, tyPath)});
                            break;
                        }
                        case HIRValueItem::TAG_StructConstant: {
                            auto& e = v->second->ent.as_StructConstant();
                            auto tyPath = e.ty;
                            pbv = ASTPathBindingValue::make_Struct({nullptr, &crate.hir->getStructByPath(sp, tyPath)});
                            break;
                        }
                        case HIRValueItem::TAG_Static: {
                            pbv = ASTPathBindingValue::make_Static({nullptr, v->second->ent.as_Static()});
                            break;
                        }
                        case HIRValueItem::TAG_Constant: {
                            // Bind
                            pbv = ASTPathBindingValue::make_Static({nullptr, nullptr});
                            break;
                        }
                    }
                    path.bindings.value.set(::std::move(ap), ::std::move(pbv));
                    path = splitIntoCrate(sp, mv$(path), start,  crate.name);
                    return ;
                }
            } break;
        }
        ERROR(sp, E0000, "Couldn't find " << Context::lookupModeMsg(mode) << " '" << pathAbs.nodes.back().name() << "' of " << path);
    }
}

void ResolveAbsolutePathBindAbsolute(Context& context, const Span& sp, Context::LookupMode& mode, ASTPath& path) {
    TRACE_FUNCTION_FR("path = " << path, path);
    auto& pathAbs = path.cls.as_Absolute();

    if (pathAbs.crate == "#intrinsics") {
        ASTAbsolutePath ap{pathAbs.crate, {}};
        for (const auto& n : path.nodes()) {
            ap.nodes.push_back(n.name());
        }
        path.bindings.value.set(std::move(ap), ASTPathBindingValue::make_Function({nullptr}));
        return;
    } else if (pathAbs.crate == CRATE_BUILTINS) {
        ASSERT_BUG(sp, path.bindings.hasBinding(), "");
        return;
    } else if (pathAbs.crate != "" && pathAbs.crate != context.crate.crateNameReal) {
        // TODO: Handle items from other crates (back-converting HIR paths)
        ASSERT_BUG(sp, context.crate.externCrates.count(pathAbs.crate), "ERROR: Crate `" << pathAbs.crate << "` not loaded");
        ResolveAbsolutePathBindAbsoluteHirFrom(context, sp, mode, path, context.crate.externCrates.at(pathAbs.crate), 0);
        return;
    }

    if (pathAbs.nodes.empty()) {
        path.bindings.type.set(ASTAbsolutePath(pathAbs.crate, {}), ASTPathBindingType::make_Module({&context.crate.rootModule_}));
        return;
    }

    const ASTModule* mod = &context.crate.rootModule_;
    for (unsigned int i = 0; i < pathAbs.nodes.size() - 1; i++) {
        auto& n = pathAbs.nodes[i];

        if (n.name().c_str()[0] == '#') {
            if (!n.args().isEmpty()) {
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
            const auto& nameRef = it->second;
            DEBUG("#" << i << " \"" << n.name() << "\" = " << nameRef.path << (nameRef.isImport ? " (import)" : ""));

            switch (nameRef.path.bindings.type.binding.tag()) {
default:
                ERROR(sp, E0000, "Encountered non-namespace item '" << n.name() << "' ("<<nameRef.path<<") in path " << path);
                case ASTPathBindingType::TAG_TypeAlias: {
                    path = splitReplaceIntoUfcsPath(context.typePool(), sp, mv$(path), i, nameRef.path);
                    return ResolveAbsolutePathBindUFCS(context, sp, mode, path);
                }
                case ASTPathBindingType::TAG_Crate: {
                    auto& e = nameRef.path.bindings.type.binding.as_Crate();
                    ResolveAbsolutePathBindAbsoluteHirFrom(context, sp, mode, path, *e.crate_, i + 1);
                    return;
                }
                case ASTPathBindingType::TAG_Trait: {
                    auto& e = nameRef.path.bindings.type.binding.as_Trait();
                    assert(e.trait_ || e.hir);
                    auto traitPath = ASTPath(nameRef.path);
                    // HACK! If this was an import, recurse on it to fix paths. (Ideally, all index entries should have the canonical path, but don't currently)
                    if (nameRef.isImport) {
                        auto lm = Context::LookupMode::Type;
                        ResolveAbsolutePathBindAbsolute(context, sp, lm, traitPath);
                    }
                    if (!n.args().isEmpty()) {
                        traitPath.nodes().back().args() = mv$(n.args());
                    } else {
                        if (e.trait_) {
                            for (const auto& param : e.trait_->params().params) {
                            switch (param.tag()) {
                                case GenericParam::TAG_None: {
                                    break;
                                }
                                case GenericParam::TAG_Lifetime: {
                                    break;
                                }
                                case GenericParam::TAG_Type: {
                                    traitPath.nodes().back().args().entries.push_back(::mkType(context.typePool(), sp));
                                    break;
                                }
                                case GenericParam::TAG_Value: {
                                    break;
                                }
                            }
                            }
                        } else {
                            for (const auto& typ : e.hir->params.types) {
                                traitPath.nodes().back().args().entries.push_back(::mkType(context.typePool(), sp));
                            }
                        }
                    }
                    // TODO: If the named item can't be found in the trait, fall back to it being a type binding
                    // - What if this item is from a nested trait?
                    ASTPath newPath;
                    bool found = false;
                    assert(i + 1 < pathAbs.nodes.size());
                    const auto& itemName = pathAbs.nodes[i + 1].name();
                    if (e.trait_) {
                        auto it = ::std::find_if(e.trait_->items().begin(), e.trait_->items().end(), [&](const auto& x) {
                            return x.name == itemName;
                        });
                        if (it != e.trait_->items().end()) {
                            found = true;
                        }
                    } else {
                        switch (mode) {
                            case Context::LookupMode::Constant:
                            case Context::LookupMode::Variable:
                            case Context::LookupMode::PatternValue:
                                found = (e.hir->values.count(itemName) != 0);
                                break;
                            case Context::LookupMode::Namespace:
                            case Context::LookupMode::Type:
                            case Context::LookupMode::PatternType:
                                found = (e.hir->types.count(itemName) != 0);
                                break;
                        }
                    }
                    if (!found) {
                        newPath = ASTPath::newUfcsTy(::mkType(context.typePool(), sp, mv$(traitPath)));
                    } else {
                        newPath = ASTPath::newUfcsTrait(::mkType(context.typePool(), sp), mv$(traitPath));
                    }
                    for (unsigned int j = i + 1; j < pathAbs.nodes.size(); j++) {
                        newPath.nodes().push_back(mv$(pathAbs.nodes[j]));
                    }

                    path = mv$(newPath);
                    return ResolveAbsolutePathBindUFCS(context, sp, mode, path);
                }
                case ASTPathBindingType::TAG_Enum: {
                    auto& e = nameRef.path.bindings.type.binding.as_Enum();
                    if (nameRef.isImport) {
                        auto newpath = nameRef.path;
                        for (unsigned int j = i + 1; j < pathAbs.nodes.size(); j++) {
                            newpath.nodes().push_back(mv$(pathAbs.nodes[j]));
                        }
                        path = mv$(newpath);
                        //TOOD: Recursion limit
                        ResolveAbsolutePathBindAbsolute(context, sp, mode, path);
                        return;
                    } else {
                        assert(e.enum_);
                        auto& lastNode = pathAbs.nodes.back();
                        for (const auto& var : e.enum_->variants()) {
                            if (var.name == lastNode.name()) {
                                if (i != pathAbs.nodes.size() - 2) {
                                    ERROR(sp, E0000, "Unexpected enum in path " << path);
                                }
                                // NOTE: Type parameters for enums go after the _variant_
                                if (!n.args().isEmpty()) {
                                    if (lastNode.args().isEmpty()) {
                                        DEBUG("Moving type params from on the enum to the variant");
                                        lastNode.args() = std::move(n.args());
                                    } else {
                                        ERROR(sp, E0000, "Type parameters were not expected here (enum params go on the variant)");
                                    }
                                }

                                unsigned int idx = &var - &e.enum_->variants().front();

                                DEBUG("Bound to enum variant '" << var.name << "' (#" << idx << ")");
                                auto ap = nameRef.path.bindings.type.path + var.name;
                                if (var.data.is_Struct() || mode == Context::LookupMode::Type || mode == Context::LookupMode::Namespace || mode == Context::LookupMode::PatternType) {
                                    path.bindings.type.set(ap, ASTPathBindingType::make_EnumVar({e.enum_, idx}));
                                } else {
                                    path.bindings.value.set(ap, ASTPathBindingValue::make_EnumVar({e.enum_, idx}));
                                }
                                return;
                            }
                        }

                        path = splitReplaceIntoUfcsPath(context.typePool(), sp, mv$(path), i, nameRef.path);
                        return ResolveAbsolutePathBindUFCS(context, sp, mode, path);
                    }
                    break;
                }
                case ASTPathBindingType::TAG_Struct: {
                    path = splitReplaceIntoUfcsPath(context.typePool(), sp, mv$(path), i, nameRef.path);
                    return ResolveAbsolutePathBindUFCS(context, sp, mode, path);
                }
                case ASTPathBindingType::TAG_Union: {
                    path = splitReplaceIntoUfcsPath(context.typePool(), sp, mv$(path), i, nameRef.path);
                    return ResolveAbsolutePathBindUFCS(context, sp, mode, path);
                }
                case ASTPathBindingType::TAG_Module: {
                    auto& e = nameRef.path.bindings.type.binding.as_Module();
                    if (nameRef.isImport) {
                        auto newpath = nameRef.path;
                        for (unsigned int j = i + 1; j < pathAbs.nodes.size(); j++) {
                            newpath.nodes().push_back(mv$(pathAbs.nodes[j]));
                        }
                        DEBUG("- Module import, " << path << " => " << newpath);
                        path = mv$(newpath);
                        ResolveAbsolutePathBindAbsolute(context, sp, mode, path);
                        return;
                    } else {
                        mod = e.module_;
                    }
                    break;
                }
            }
        }
    }

    // Set binding to binding of node in last module
    ASTPath tmp;
    if (!Context::lookupInMod(sp, *mod, pathAbs.nodes.back().name(), mode, tmp)) {
        ERROR(sp, E0000, "Couldn't find " << Context::lookupModeMsg(mode) << " '" << pathAbs.nodes.back().name() << "' of " << path);
    }
    ASSERT_BUG(sp, tmp.bindings.hasBinding(), "Lookup for " << path << " succeeded, but had no binding");

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

void ResolveAbsolutePath(/*const*/ Context& context, const Span& sp, Context::LookupMode mode, ASTPath& path) {
    TRACE_FUNCTION_FR("mode = " << mode << ", path = " << path, path);

    switch (path.cls.tag()) {
        case ASTPathClass::TAG_Invalid: {
            BUG(sp, "Attempted resolution of invalid path");
            break;
        }
        case ASTPathClass::TAG_Local: {
            auto& e = path.cls.as_Local();
            // Nothing to do (TODO: Check that it's valid?)
            if (mode == Context::LookupMode::Variable) {
                auto idx = context.lookupLocal(sp, e.name, mode);
                if (idx >= FLAG_CONST_GENERIC) {
                    path.bindings.value.set({}, ASTPathBindingValue::make_Generic({idx - FLAG_CONST_GENERIC}));
                } else {
                    path.bindings.value.set({}, ASTPathBindingValue::make_Variable({idx}));
                }
            } else if (mode == Context::LookupMode::Type) {
                path.bindVariable(context.lookupLocal(sp, e.name, mode));
            } else {
            }
            break;
        }
        case ASTPathClass::TAG_Relative: {
            auto& e = path.cls.as_Relative();
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
                    if (const auto* pep = p.bindings.type.binding.opt_Module()) {
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
                            p = ASTPath::newUfcsTy(mkType(context.typePool(), Span(), ct), ::std::vector<ASTPathNode>());
                        }

                        DEBUG("Primitive module hack yeilded " << p);
                    }
                }

                if (e.nodes.size() > 1) {
                    // Only primitive types turn `Local` paths
                    if (p.cls.is_Local()) {
                        p = ASTPath::newUfcsTy(mkType(context.typePool(), sp, mv$(p)));
                    }
                    if (!e.nodes[0].args().isEmpty()) {
                        assert(p.nodes().size() > 0);
                        assert(p.nodes().back().args().isEmpty());
                        p.nodes().back().args() = mv$(e.nodes[0].args());
                    }
                    for (unsigned int i = 1; i < e.nodes.size(); i++) {
                        p.nodes().push_back(mv$(e.nodes[i]));
                    }
                    p.bindings = ASTPath::Bindings{};
                }
                path = mv$(p);
            } else {
                // Look up value
                auto p = context.lookup(sp, e.nodes[0].name(), e.hygiene, mode);
                if (p.isAbsolute()) {
                    assert(!p.nodes().empty());
                    // A value-level `Self` lookup returns the concrete impl type,
                    // including its impl generic arguments.  Replacing those with
                    // the syntactically empty arguments on `Self` loses the link to
                    // the current impl and creates fresh inference variables later.
                    if (e.nodes[0].name() != rcstringSelf) {
                        p.nodes().back().args() = mv$(e.nodes.back().args());
                    }
                }
                // A name imported from a trait (`use A::new;`) stands for the
                // path through the trait, which is what resolution knows how to
                // turn into a UFCS path -- so hand it back to resolution.
                if (p.cls.is_Relative() && p.cls.as_Relative().nodes.size() > 1) {
                    ResolveAbsolutePath(context, sp, mode, p);
                }
                path = mv$(p);
            }

            if (!path.isTrivial()) {
                ResolveAbsolutePathNodes(context, sp, path.nodes());
            }
            break;
        }
        case ASTPathClass::TAG_Self: {
            auto& e = path.cls.as_Self();
            DEBUG("- Self");
            const auto& mpNodes = context.mod.path().nodes;
            // Ignore any leading anon modules
            unsigned int startLen = mpNodes.size();
            while (startLen > 0 && mpNodes[startLen - 1].c_str()[0] == '#') {
                startLen--;
            }
            // `self::super::..` leaves the module the same way a leading `super`
            // does; only the spelling puts the `super` after the `self`.
            while (!e.nodes.empty() && e.nodes.front().name() == "super") {
                if (startLen == 0) {
                    ERROR(sp, E0000, "Too many `super` components");
                }
                startLen--;
                while (startLen > 0 && mpNodes[startLen - 1].c_str()[0] == '#') {
                    startLen--;
                }
                e.nodes.erase(e.nodes.begin());
            }

            // - Create a new path
            ASTPath np("", {});
            auto& npNodes = np.nodes();
            npNodes.reserve(startLen + e.nodes.size());
            for (unsigned int i = 0; i < startLen; i++) {
                npNodes.push_back(mpNodes[i]);
            }
            for (auto& en : e.nodes) {
                npNodes.push_back(mv$(en));
            }

            if (!path.isTrivial()) {
                ResolveAbsolutePathNodes(context, sp, npNodes);
            }

            path = mv$(np);
            break;
        }
        case ASTPathClass::TAG_Super: {
            auto& e = path.cls.as_Super();
            DEBUG("- Super");
            // - Determine how many components of the `self` path to use
            const auto& mpNodes = context.mod.path().nodes;
            assert(e.count >= 1);
            // TODO: The first super should ignore any anon modules.
            unsigned int startLen = e.count > mpNodes.size() ? 0 : mpNodes.size() - e.count;
            while (startLen > 0 && mpNodes[startLen - 1].c_str()[0] == '#') {
                startLen--;
            }

            // - Create a new path
            ASTPath np("", {});
            auto& npNodes = np.nodes();
            npNodes.reserve(startLen + e.nodes.size());
            for (unsigned int i = 0; i < startLen; i++) {
                npNodes.push_back(mpNodes[i]);
            }
            for (auto& en : e.nodes) {
                npNodes.push_back(mv$(en));
            }

            if (!path.isTrivial()) {
                ResolveAbsolutePathNodes(context, sp, npNodes);
            }

            path = mv$(np);
            break;
        }
        case ASTPathClass::TAG_Absolute: {
            auto& e = path.cls.as_Absolute();
            DEBUG("- Absolute");
            // HACK: if the crate name starts with `=` it's a 2018 absolute path (references a crate loaded with `--extern`)
            if (/*context.m_crate.m_edition >= AST::Edition::Rust2018 &&*/ e.crate.c_str()[0] == '=') {
                // Absolute paths in 2018 edition are crate-prefixed?
                auto ecIt = context.settings.implicitCrates.find(e.crate.c_str() + 1);
                if (ecIt == context.settings.implicitCrates.end()) {
                    ERROR(sp, E0000, "Unable to find external crate for path " << path);
                }
                e.crate = ecIt->second;
            }
            // HACK: If this is `crate::foo::bar`, and `foo` doesn't exist in the root, but it is an implicit crate, then resolve to that
            // - This handles when a 2015 macro resolves to `::cratename::Bar` in a 2018+ crate
            else if (e.crate == "" && e.nodes.size() > 1 && context.crate.rootModule_.namespaceItems.count(e.nodes.front().name()) == 0) {
                auto ecIt = context.settings.implicitCrates.find(e.nodes.front().name().c_str());
                if (ecIt != context.settings.implicitCrates.end()) {
                    e.crate = ecIt->second;
                    e.nodes.erase(e.nodes.begin());
                }
            }
            // Nothing to do (TODO: Bind?)
            ResolveAbsolutePathNodes(context, sp, e.nodes);
            break;
        }
        case ASTPathClass::TAG_UFCS: {
            auto& e = path.cls.as_UFCS();
            DEBUG("- UFCS");
            ResolveAbsoluteType(context, e.type);
            if (e.trait && *e.trait != ASTPath()) {
                ResolveAbsolutePath(context, sp, Context::LookupMode::Type, *e.trait);
            }

            ResolveAbsolutePathNodes(context, sp, e.nodes);
            break;
        }
    }

    DEBUG("path = " << path);
    // TODO: Should this be deferred until the HIR?
    // - Doing it here so the HIR lowering has a bit more information
    // - Also handles splitting "absolute" paths into UFCS
    switch (path.cls.tag()) {
default:
        BUG(sp, "Path wasn't absolutised correctly");
        case ASTPathClass::TAG_Local: {
            if (!path.bindings.hasBinding()) {
                TODO(sp, "Bind unbound local path - " << path);
            }
            break;
        }
        case ASTPathClass::TAG_Absolute: {
            ResolveAbsolutePathBindAbsolute(context, sp, mode, path);
            break;
        }
        case ASTPathClass::TAG_UFCS: {
            ResolveAbsolutePathBindUFCS(context, sp, mode, path);
            break;
        }
    }

    // TODO: Expand default type parameters?
    // - Helps with cases like PartialOrd<Self>, but hinders when the default is a hint (in expressions)

    if(const auto* e = path.cls.opt_UFCS())
    {
        if (!e->nodes.empty() && (!e->trait || !e->trait->isValid()) && e->type->data.is_Generic() && e->type->data.as_Generic().index == GENERICSelf) {
            const auto& name = e->nodes.front().name();

            if (const auto* selfTy = context.getSelfOpt()) {
                // Check if we're in an enum
                if (const auto* tyPath = (*selfTy)->data.opt_Path()) {
                    const auto& p = **tyPath;
                    if (const auto* pbe = p.bindings.type.binding.opt_Enum()) {
                        if (pbe->enum_) {
                            const auto& enm = *pbe->enum_;
                            auto it = std::find_if(enm.variants().begin(), enm.variants().end(), [&](const ASTEnumVariant& v) {
                                return v.name == name;
                            });
                            if (it != enm.variants().end()) {
                                unsigned idx = it - enm.variants().begin();
                                auto p2 = p.bindings.type.path + name;
                                auto newPath = std::move(p);
                                newPath.append(name);
                                if (it->data.is_Struct()) {
                                    newPath.bindings.type.set(p2, ASTPathBindingType::make_EnumVar({&enm, idx}));
                                } else {
                                    newPath.bindings.value.set(p2, ASTPathBindingValue::make_EnumVar({&enm, idx}));
                                }
                                DEBUG("UFCS of enum variant converted to Generic: " << newPath);
                                path = std::move(newPath);
                            }
                        } else if (pbe->hir) {
                            // The enum came from another crate, so only its HIR
                            // is here -- the variant list is the same either way.
                            const auto& enm = *pbe->hir;
                            auto idx = enm.findVariant(name);
                            if (idx != SIZE_MAX) {
                                auto p2 = p.bindings.type.path + name;
                                auto newPath = std::move(p);
                                newPath.append(name);
                                const bool isStruct = enm.data.is_Data() && enm.data.as_Data()[idx].isStruct;
                                if (isStruct) {
                                    newPath.bindings.type.set(p2, ASTPathBindingType::make_EnumVar({nullptr, static_cast<unsigned>(idx), &enm}));
                                } else {
                                    newPath.bindings.value.set(p2, ASTPathBindingValue::make_EnumVar({nullptr, static_cast<unsigned>(idx), &enm}));
                                }
                                DEBUG("UFCS of external enum variant converted to Generic: " << newPath);
                                path = std::move(newPath);
                            }
                        } else {
                        }
                    }
                }
            }
        }
    }
}

void ResolveAbsoluteLifetime(Context& context, const Span& sp, ASTLifetimeRef& lft) {
    TRACE_FUNCTION_FR("lft = " << lft, "lft = " << lft);
    if (lft.isUnbound()) {
        if (lft.name() == "static") {
            lft = ASTLifetimeRef::newStatic();
            return;
        }

        if (lft.name() == "_") {
            // Note: '_ is just an explicit elided lifetime
            lft.setBinding(ASTLifetimeRef::BINDING_INFER);
            return;
        }

        for (auto it = context.nameContext.rbegin(); it != context.nameContext.rend(); ++it) {
            if (const auto* e = it->opt_Generic()) {
                for (const auto& l : e->lifetimes) {
                    // NOTE: Hygiene doesn't apply to lifetime params!
                    if (l.name.name == lft.name().name /*&& l.name.hygiene.is_visible(lft.name().hygiene)*/) {
                        lft.setBinding(l.value.index | (static_cast<int>(l.value.level) << 8));
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
                ASSERT_BUG(sp, it->is_Generic(), "Name context stack end not Generic, instead " << it->tagStr());
                while (it->as_Generic().level == GenericSlot::Level::Hrb) {
                    it++;
                    ASSERT_BUG(sp, it != context.nameContext.rend(), "");
                    ASSERT_BUG(sp, it->is_Generic(), "Name context stack end not Generic, instead " << it->tagStr());
                }
                if (it->as_Generic().level != GenericSlot::Level::Hrb) {
                    auto& contextGen = it->as_Generic();
                    auto& defGen = *context.iblTargetGenerics;
                    auto level = contextGen.level;
                    // 1. Assert that the last item of `context.m_name_context` is Generic, and matches `m_ibl_target_generics`
                    ASSERT_BUG(sp, contextGen.lifetimes.size() + contextGen.types.size() + contextGen.constants.size() == defGen.params.size(), "");
                    // 2. Add the new lifetime to both `m_ibl_target_generics` and the last entry in m_name_context
                    size_t idx = contextGen.lifetimes.size();
                    defGen.addLftParam(ASTLifetimeParam(sp, {}, lft.name()));
                    contextGen.lifetimes.push_back(NamedI<GenericSlot>{lft.name(), GenericSlot{level, static_cast<unsigned short>(idx)}});
                    lft.setBinding(idx | (static_cast<int>(level) << 8));
                    return;
                }
            }
        }
        ERROR(sp, E0000, "Couldn't find lifetime " << lft);
    }
}

void ResolveAbsoluteType(Context& context, ASTType*& type) {
    TRACE_FUNCTION_FR("type = " << type, "type = " << type);
    const auto& sp = type->span();

    if (type->data.is_Path() && type->data.as_Path()->bindings.type.binding.is_TypeParameter()) {
        auto& e = type->data.as_Path()->bindings.type.binding.as_TypeParameter();
        type->data = TypeData::make_Generic({type->data.as_Path()->asTrivial(), e.slot});
    }

    switch (type->data.tag()) {
        case TypeData::TAG_None: {
            // invalid type
            break;
        }
        case TypeData::TAG_Any: {
            // _ type
            break;
        }
        case TypeData::TAG_Unit: {
            break;
        }
        case TypeData::TAG_Bang: {
            // ! type
            break;
        }
        case TypeData::TAG_Macro: {
            BUG(sp, "Resolve_Absolute_Type - Encountered an unexpanded macro in type - " << type);
            break;
        }
        case TypeData::TAG_Primitive: {
            break;
        }
        case TypeData::TAG_Function: {
            auto& e = type->data.as_Function();
            context.push(e.info.hrbs);
            ResolveAbsoluteType(context, e.info.rettype);
            for (auto& t : e.info.argTypes) {
                ResolveAbsoluteType(context, t);
            }
            context.pop(e.info.hrbs);
            break;
        }
        case TypeData::TAG_Tuple: {
            auto& e = type->data.as_Tuple();
            for (auto& t : e.innerTypes) {
                ResolveAbsoluteType(context, t);
            }
            break;
        }
        case TypeData::TAG_Borrow: {
            auto& e = type->data.as_Borrow();
            ResolveAbsoluteLifetime(context, type->span(), e.lifetime);
            ResolveAbsoluteType(context, e.inner);
            break;
        }
        case TypeData::TAG_Pointer: {
            auto& e = type->data.as_Pointer();
            ResolveAbsoluteType(context, e.inner);
            break;
        }
        case TypeData::TAG_Array: {
            auto& e = type->data.as_Array();
            ResolveAbsoluteType(context, e.inner);
            if (e.size) {
                auto _h = context.enterRootblock();
                ResolveAbsoluteExprNode(context, *e.size);
            }
            break;
        }
        case TypeData::TAG_Slice: {
            auto& e = type->data.as_Slice();
            ResolveAbsoluteType(context, e.inner);
            break;
        }
        case TypeData::TAG_Pattern: {
            auto& e = type->data.as_Pattern();
            ResolveAbsoluteType(context, e.inner);
            ResolveAbsolutePattern(context, true, *e.pattern);
            break;
        }
        case TypeData::TAG_Generic: {
            auto& e = type->data.as_Generic();
            if (e.name == rcstringSelf) {
                type = context.getSelf();
            } else {
                auto idx = context.lookupLocal(type->span(), e.name, Context::LookupMode::Type);
                // TODO: Should this be bound to the relevant index, or just leave as-is?
                e.index = idx;
            }
            break;
        }
        case TypeData::TAG_Path: {
            auto& e = type->data.as_Path();
            ResolveAbsolutePath(context, type->span(), Context::LookupMode::Type, *e);
            if (auto* ufcs = e->cls.opt_UFCS()) {
                if (ufcs->nodes.size() == 0 /*&& ufcs->trait && *ufcs->trait == ::AST::Path()*/) {
                    type = ufcs->type;
                    return;
                }
                assert(ufcs->nodes.size() == 1);
            }

            if (e->bindings.type.binding.opt_Trait()) {
                auto tp = TypeTraitPath();
                tp.path = std::make_unique<ASTPath>(*e);
                type = ::mkType(context.typePool(), type->span(), ::makeVec1(mv$(tp)), {});
                return;
            }
            //else if(auto* be = e->m_bindings.type.binding.opt_TypeParameter())
            //{
            //}
            break;
        }
        case TypeData::TAG_TraitObject: {
            auto& e = type->data.as_TraitObject();
            for (auto& trait : e.traits) {
                context.push(trait.hrbs);
                ResolveAbsolutePath(context, type->span(), Context::LookupMode::Type, *trait.path);
                context.pop(trait.hrbs);
            }
            for (auto& lft : e.lifetimes) {
                ResolveAbsoluteLifetime(context, type->span(), lft);
            }
            break;
        }
        case TypeData::TAG_ErasedType: {
            auto& e = type->data.as_ErasedType();
            for (auto& trait : e->traits) {
                context.push(trait.hrbs);
                ResolveAbsolutePath(context, type->span(), Context::LookupMode::Type, *trait.path);
                context.pop(trait.hrbs);
            }
            for (auto& trait : e->maybeTraits) {
                context.push(trait.hrbs);
                ResolveAbsolutePath(context, type->span(), Context::LookupMode::Type, *trait.path);
                context.pop(trait.hrbs);
            }
            for (auto& lft : e->lifetimes) {
                ResolveAbsoluteLifetime(context, type->span(), lft);
            }
            if (e->use) {
                ResolveAbsolutePathParams(context, type->span(), *e->use);
            }
            break;
        }
    }
}

void ResolveAbsoluteExpr(Context& context, ASTExpr& expr) {
    if (expr.isValid()) {
        ResolveAbsoluteExprNode(context, expr.node());
    }
}

void ResolveAbsoluteExprNode(Context& context, ASTExprNode& node) {
    TRACE_FUNCTION_F("");

    struct NV: public ASTNodeVisitorDef {
        Context& context;

        NV(Context& context)
            : context(context)
        {
        }

        void visit(ASTExprNodeBlock& node) override {
            DEBUG("ExprNode_Block");
            if (node.localMod) {
                auto _h = context.clearRootblock();
                this->context.push(*node.localMod);

                // Clone just the module stack part of the current context
                ResolveAbsoluteMod(this->context.cloneMod(), *node.localMod);
            }
            this->context.pushBlock();
            for (auto& line : node.nodes) {
                if (const auto* definition = cast<ASTExprNodeMacroDefinition>(line.node.get())) {
                    this->context.pushMacroDefinition(definition->definitionId, definition->tokenHygiene, definition->definitionHygiene);
                } else {
                    line.node->visit(*this);
                }
            }
            this->context.popBlock();
            if (node.localMod) {
                this->context.pop(*node.localMod);
            }
        }

        void visit(ASTExprNodeMatch& node) override {
            DEBUG("ExprNode_Match");
            node.val->visit(*this);
            for (auto& arm : node.arms) {
                this->context.pushBlock();

                this->context.startPatbind();
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
                    if (cond.optPat) {
                        this->context.startPatbind();
                        ResolveAbsolutePattern(this->context, true, *cond.optPat);
                        this->context.endPatbind();
                    }
                }
                assert(arm.code);
                arm.code->visit(*this);

                this->context.popBlock();
            }
        }

        void visit(ASTExprNodeLoop& node) override {
            this->context.pushBlock();
            node.code->visit(*this);
            this->context.popBlock();
        }

        void visit(ASTExprNodeFor& node) override {
            BUG(node.span(), "`for` should be desugared");
        }

        void visit(ASTExprNodeWhile& node) override {
            this->context.pushBlock();
            visitIfLetConditions(node.conditions);
            node.code->visit(*this);
            this->context.popBlock();
        }

        void visit(ASTExprNodeLetBinding& node) override {
            DEBUG("ExprNode_LetBinding");
            ResolveAbsoluteType(this->context, node.type);
            ASTNodeVisitorDef::visit(node);
            this->context.startPatbind();
            auto count = this->context.varCount;
            ResolveAbsolutePattern(this->context, node.elseNode ? true : false, node.pat);
            this->context.endPatbind();
            auto nVars = this->context.varCount - count;
            if (node.elseNode) {
                node.letelseSlots = std::make_pair(this->context.varCount, nVars);
                this->context.varCount += nVars;
            }
        }

        void visitIfLetConditions(std::vector<ASTIfLetCondition>& conds) {
            for (auto& cond : conds) {
                // Visit the value first, so it doesn't bind to the newly created variables in the pattern
                cond.value->visit(*this);

                if (cond.optPat) {
                    this->context.startPatbind();
                    ResolveAbsolutePattern(this->context, true, *cond.optPat);
                    this->context.endPatbindArm(cond.optPat->span());
                    this->context.endPatbind();
                }
            }
        }

        void visit(ASTExprNodeIf& node) override {
            for (auto& arm : node.arms) {
                this->context.pushBlock();
                visitIfLetConditions(arm.conditions);
                arm.body->visit(*this);
                this->context.popBlock();
            }
            if (node.elseNode) {
                node.elseNode->visit(*this);
            }
        }

        void visit(ASTExprNodeStructLiteral& node) override {
            DEBUG("ExprNode_StructLiteral");
            // A unit or tuple variant lives in the value namespace, and naming
            // one in a struct expression is as valid as naming it in a pattern.
            ResolveAbsolutePath(this->context, node.span(), Context::LookupMode::PatternType, node.path);
            ASTNodeVisitorDef::visit(node);
        }

        void visit(ASTExprNodeStructLiteralPattern& node) override {
            DEBUG("ExprNode_StructLiteralPattern");
            ResolveAbsolutePath(this->context, node.span(), Context::LookupMode::PatternType, node.path);
            ASTNodeVisitorDef::visit(node);
        }

        void visit(ASTExprNodeCallPath& node) override {
            DEBUG("ExprNode_CallPath");
            ResolveAbsolutePath(this->context, node.span(), Context::LookupMode::Variable, node.path);
            ASTNodeVisitorDef::visit(node);
        }

        void visit(ASTExprNodeCallMethod& node) override {
            DEBUG("ExprNode_CallMethod");
            ResolveAbsolutePathParams(this->context, node.span(), node.method.args());
            ASTNodeVisitorDef::visit(node);
        }

        void visit(ASTExprNodeNamedValue& node) override {
            DEBUG("(" << node.span() << ") ExprNode_NamedValue - " << node.path);
            ResolveAbsolutePath(this->context, node.span(), Context::LookupMode::Variable, node.path);
        }

        void visit(ASTExprNodeAsm2& node) override {
            for (auto& operand : node.params) {
                if (auto* path = operand.opt_Sym()) {
                    ResolveAbsolutePath(this->context, node.span(), Context::LookupMode::Variable, *path);
                }
            }
            ASTNodeVisitorDef::visit(node);
        }

        void visit(ASTExprNodeCast& node) override {
            DEBUG("ExprNode_Cast");
            ResolveAbsoluteType(this->context, node.type);
            ASTNodeVisitorDef::visit(node);
        }

        void visit(ASTExprNodeTypeAnnotation& node) override {
            DEBUG("ExprNode_TypeAnnotation");
            ResolveAbsoluteType(this->context, node.type);
            ASTNodeVisitorDef::visit(node);
        }

        void visit(ASTExprNodeClosure& node) override {
            DEBUG("ExprNode_Closure");

            // A closure's own lifetime binder scopes over its signature and body.
            this->context.push(node.hrbs);

            ResolveAbsoluteType(this->context, node.returnType);

            this->context.pushBlock();
            for (auto& arg : node.args) {
                ResolveAbsoluteType(this->context, arg.second);
                this->context.startPatbind();
                ResolveAbsolutePattern(this->context, false, arg.first);
                this->context.endPatbind();
            }

            node.code->visit(*this);

            this->context.popBlock();
            this->context.pop(node.hrbs);
        }
    } exprIter(context);

    node.visit(exprIter);
}

void ResolveAbsoluteGeneric(Context& context, ASTGenericParams& params) {
    for (auto& param : params.params) {
        {
            auto& tuMatch = param;
            switch (tuMatch.tag()) {
                case GenericParam::TAG_None: {
                    auto& _ = tuMatch.as_None();
                    break;
                }
                case GenericParam::TAG_Lifetime: {
                    break;
                }
                case GenericParam::TAG_Type: {
                    auto& param = tuMatch.as_Type();
                    ResolveAbsoluteType(context, param.getDefault());
                    break;
                }
                case GenericParam::TAG_Value: {
                    auto& param = tuMatch.as_Value();
                    ResolveAbsoluteType(context, param.type());
                    ResolveAbsoluteExpr(context, param.defaultValue());
                    break;
                }
            }
        }
    }
    for (auto& bound : params.bounds) {
        switch (bound.tag()) {
            case ASTGenericBound::TAG_None: {
                break;
            }
            case ASTGenericBound::TAG_Lifetime: {
                auto& e = bound.as_Lifetime();
                ResolveAbsoluteLifetime(context, bound.span, e.test); ResolveAbsoluteLifetime(context, bound.span, e.bound);
                break;
            }
            case ASTGenericBound::TAG_TypeLifetime: {
                auto& e = bound.as_TypeLifetime();
                ResolveAbsoluteType(context, e.type); ResolveAbsoluteLifetime(context, bound.span, e.bound);
                break;
            }
            case ASTGenericBound::TAG_IsTrait: {
                auto& e = bound.as_IsTrait();
                context.push(e.outerHrbs); ResolveAbsoluteType(context, e.type); context.push(e.innerHrbs); ResolveAbsolutePath(context, bound.span, Context::LookupMode::Type, e.trait); context.pop(e.innerHrbs); context.pop(e.outerHrbs);
                break;
            }
            case ASTGenericBound::TAG_MaybeTrait: {
                auto& e = bound.as_MaybeTrait();
                ResolveAbsoluteType(context, e.type); ResolveAbsolutePath(context, bound.span, Context::LookupMode::Type, e.trait);
                break;
            }
            case ASTGenericBound::TAG_NotTrait: {
                auto& e = bound.as_NotTrait();
                ResolveAbsoluteType(context, e.type); ResolveAbsolutePath(context, bound.span, Context::LookupMode::Type, e.trait);
                break;
            }
            case ASTGenericBound::TAG_Equality: {
                auto& e = bound.as_Equality();
                ResolveAbsoluteType(context, e.type); ResolveAbsoluteType(context, e.replacement);
                break;
            }
        }
    }
    // Bare `where T:` predicate types carry no constraint, but resolving them
    // absolutizes any anon-const items they contain (which lowering will visit).
    for (auto& t : params.bareBoundTypes) {
        ResolveAbsoluteType(context, t);
    }
}

// Locals shouldn't be possible, as they'd end up as MaybeBind. Will assert the path class.
void ResolveAbsolutePatternValue(/*const*/ Context& context, const Span& sp, ASTPattern::Value& val) {
    if (val.is_Named()) {
        auto& e = val.as_Named();
        ResolveAbsolutePath(context, sp, Context::LookupMode::Constant, e);
    }
}

void ResolveAbsolutePattern(Context& context, bool allowRefutable, ASTPattern& pat) {
    TRACE_FUNCTION_FR("allow_refutable = " << allowRefutable << ", pat = " << pat, pat);
    for (auto& pb : pat.bindings()) {
        //if( !pat.data().is_Any() && ! allow_refutable )
        //    TODO(pat.span(), "Resolve_Absolute_Pattern - Encountered bound destructuring pattern");
        pb.slot = context.pushVar(pat.span(), pb.name);
        DEBUG("- Binding #" << pb.slot << " '" << pb.name << "'");
    }

    switch (pat.data().tag()) {
        case ASTPatternData::TAG_Never: {
            // `!` names nothing and binds nothing.
            break;
        }
        case ASTPatternData::TAG_MaybeBind: {
            auto& e = pat.data().as_MaybeBind();
            auto name = mv$(e.name);
            // Attempt to resolve the name in the current namespace, and if it fails, it's a binding
            auto p = context.lookupOpt(pat.span(), name.name, name.hygiene, Context::LookupMode::PatternValue);
            if (p.isValid()) {
                ResolveAbsolutePath(context, pat.span(), Context::LookupMode::PatternValue, p);
                pat.data() = ASTPattern::Data::make_Value({ASTPattern::Value::make_Named(mv$(p)), ASTPattern::Value()});
                DEBUG("MaybeBind resolved to " << pat);
            } else {
                pat.bindings().push_back(ASTPatternBinding(mv$(name), ASTPatternBinding::Type::MOVE, false));
                pat.bindings().back().slot = context.pushVar(pat.span(), pat.bindings().back().name);
                pat.data() = ASTPattern::Data::make_Any({});
                DEBUG("- Binding #" << pat.bindings().back().slot << " '" << pat.bindings().back().name << "' (was MaybeBind)");
            }
            break;
        }
        case ASTPatternData::TAG_Macro: {
            BUG(pat.span(), "Resolve_Absolute_Pattern - Encountered Macro - " << pat);
            break;
        }
        case ASTPatternData::TAG_Any: {
            // Ignore '_'
            break;
        }
        case ASTPatternData::TAG_Box: {
            auto& e = pat.data().as_Box();
            ResolveAbsolutePattern(context, allowRefutable, *e.sub);
            break;
        }
        case ASTPatternData::TAG_Deref: {
            auto& e = pat.data().as_Deref();
            ResolveAbsolutePattern(context, allowRefutable, *e.sub);
            break;
        }
        case ASTPatternData::TAG_Ref: {
            auto& e = pat.data().as_Ref();
            ResolveAbsolutePattern(context, allowRefutable, *e.sub);
            break;
        }
        case ASTPatternData::TAG_Value: {
            auto& e = pat.data().as_Value();
            // Disabled check : Some code does `let (Foo | Bar);` where those are the only options
            //if( ! allow_refutable )
            //{
            //    // TODO: If this is a single value of a unit-like struct, accept
            //}
            ResolveAbsolutePatternValue(context, pat.span(), e.start);
            ResolveAbsolutePatternValue(context, pat.span(), e.end);
            break;
        }
        case ASTPatternData::TAG_ValueLeftInc: {
            auto& e = pat.data().as_ValueLeftInc();
            if (!allowRefutable) {
                // TODO: If this is a single value of a unit-like struct, accept
                // A range does not match every value, so it cannot stand where a
                // pattern has to (a parameter, a `let` without an `else`). That
                // is a program error, not a compiler one.
                ERROR(pat.span(), E0000, "refutable pattern where an irrefutable one is required - " << pat);
            }
            ResolveAbsolutePatternValue(context, pat.span(), e.start);
            ResolveAbsolutePatternValue(context, pat.span(), e.end);
            break;
        }
        case ASTPatternData::TAG_Tuple: {
            auto& e = pat.data().as_Tuple();
            for (auto& sp : e.start) {
                ResolveAbsolutePattern(context, allowRefutable, sp);
            }
            for (auto& sp : e.end) {
                ResolveAbsolutePattern(context, allowRefutable, sp);
            }
            break;
        }
        case ASTPatternData::TAG_StructTuple: {
            auto& e = pat.data().as_StructTuple();
            ResolveAbsolutePath(context, pat.span(), Context::LookupMode::Constant, e.path);
            for (auto& sp : e.tupPat.start) {
                ResolveAbsolutePattern(context, allowRefutable, sp);
            }
            for (auto& sp : e.tupPat.end) {
                ResolveAbsolutePattern(context, allowRefutable, sp);
            }
            break;
        }
        case ASTPatternData::TAG_Struct: {
            auto& e = pat.data().as_Struct();
            // `Struct { .. }` patterns can match anything, so switch lookup mode in that case
            ResolveAbsolutePath(context, pat.span(), e.subPatterns.empty() ? Context::LookupMode::PatternType : Context::LookupMode::Type, e.path);
            for (auto& sp : e.subPatterns) {
                ResolveAbsolutePattern(context, allowRefutable, sp.pat);
            }
            break;
        }
        case ASTPatternData::TAG_Slice: {
            auto& e = pat.data().as_Slice();
            // NOTE: Can be irrefutable (if the type is array)
            for (auto& sp : e.subPats) {
                ResolveAbsolutePattern(context, allowRefutable, sp);
            }
            break;
        }
        case ASTPatternData::TAG_SplitSlice: {
            auto& e = pat.data().as_SplitSlice();
            // NOTE: Can be irrefutable (if the type is array)
            for (auto& sp : e.leading) {
                ResolveAbsolutePattern(context, allowRefutable, sp);
            }
            if (e.extraBind.isValid()) {
                e.extraBind.slot = context.pushVar(pat.span(), e.extraBind.name);
            }
            for (auto& sp : e.trailing) {
                ResolveAbsolutePattern(context, allowRefutable, sp);
            }
            break;
        }
        case ASTPatternData::TAG_Or: {
            auto& e = pat.data().as_Or();
            // TODO: Need to ensure that all arms bind the same set of variables
            context.startPatbind();
            for (auto& sp : e) {
                ResolveAbsolutePattern(context, allowRefutable, sp);
                context.endPatbindArm(sp.span());
            }
            context.endPatbind();
            break;
        }
    }
}

// - For traits
void ResolveAbsoluteImplItems(Context& itemContext, ASTNamedList<ASTItem>& items) {
    TRACE_FUNCTION_F("");
    for (auto& i : items) {
        switch (i.data.tag()) {
            case ASTItem::TAG_None: {
                break;
            }
            case ASTItem::TAG_MacroInv: {
                break;
            }
            case ASTItem::TAG_ExternBlock: {
                BUG(i.span, "Resolve_Absolute_ImplItems - " << i.data.tagStr());
                break;
            }
            case ASTItem::TAG_Impl: {
                BUG(i.span, "Resolve_Absolute_ImplItems - " << i.data.tagStr());
                break;
            }
            case ASTItem::TAG_NegImpl: {
                BUG(i.span, "Resolve_Absolute_ImplItems - " << i.data.tagStr());
                break;
            }
            case ASTItem::TAG_GlobalAsm: {
                BUG(i.span, "Resolve_Absolute_ImplItems - " << i.data.tagStr());
                break;
            }
            case ASTItem::TAG_Macro: {
                BUG(i.span, "Resolve_Absolute_ImplItems - " << i.data.tagStr());
                break;
            }
            case ASTItem::TAG_Use: {
                BUG(i.span, "Resolve_Absolute_ImplItems - Use");
                break;
            }
            case ASTItem::TAG_Module: {
                BUG(i.span, "Resolve_Absolute_ImplItems - Module");
                break;
            }
            case ASTItem::TAG_Crate: {
                BUG(i.span, "Resolve_Absolute_ImplItems - Crate");
                break;
            }
            case ASTItem::TAG_Enum: {
                BUG(i.span, "Resolve_Absolute_ImplItems - Enum");
                break;
            }
            case ASTItem::TAG_Trait: {
                BUG(i.span, "Resolve_Absolute_ImplItems - " << i.data.tagStr());
                break;
            }
            case ASTItem::TAG_TraitAlias: {
                BUG(i.span, "Resolve_Absolute_ImplItems - " << i.data.tagStr());
                break;
            }
            case ASTItem::TAG_Struct: {
                BUG(i.span, "Resolve_Absolute_ImplItems - Struct");
                break;
            }
            case ASTItem::TAG_Union: {
                BUG(i.span, "Resolve_Absolute_ImplItems - Union");
                break;
            }
            case ASTItem::TAG_Type: {
                auto& e = i.data.as_Type();
                DEBUG("Type - " << i.name);
                //ASSERT_BUG( i.span, e.params().m_params.size() == 0, "TODO: Generic Associated Types (Trait)" );
                itemContext.push(e.params(), GenericSlot::Level::Method, true);
                ResolveAbsoluteGeneric(itemContext, e.params_);
                ResolveAbsoluteGeneric(itemContext, e.selfBounds);

                ResolveAbsoluteType(itemContext, e.type());

                itemContext.pop(e.params(), true);
                break;
            }
            case ASTItem::TAG_Function: {
                auto& e = i.data.as_Function();
                DEBUG("Function - " << i.name);
                ResolveAbsoluteFunction(itemContext, e, {}, true, false);
                break;
            }
            case ASTItem::TAG_Static: {
                auto& e = i.data.as_Static();
                DEBUG("Static - " << i.name);
                ResolveAbsoluteStatic(itemContext, e);
                break;
            }
        }
    }
}

const ASTFunction* FindTraitFunction(const ASTTrait& trait, const RcString& name) {
    for (const auto& item : trait.items()) {
        if (item.name == name && item.data.is_Function()) {
            return &item.data.as_Function();
        }
    }
    return nullptr;
}

void ExpandDelegationGlobs(Context& itemContext, ASTImpl& impl) {
    ::std::set<RcString> explicitNames;
    const auto originalSize = impl.items().size();
    for (size_t i = 0; i < originalSize; i++) {
        if (impl.items()[i].name != "") {
            explicitNames.insert(impl.items()[i].name);
        }
    }

    for (size_t i = 0; i < originalSize; i++) {
        auto& item = impl.items()[i];
        if (!item.data->is_Function()) {
            continue;
        }
        auto& fcn = item.data->as_Function();
        if (!fcn.delegation() || fcn.delegation()->targets.size() != 1
            || fcn.delegation()->targets.front().name != "") {
            continue;
        }

        auto& targetPath = fcn.delegation()->targets.front().path;
        const ASTTrait* astTrait = nullptr;
        const HIRTrait* hirTrait = nullptr;
        if (targetPath.cls.is_UFCS()) {
            auto& ufcs = targetPath.cls.as_UFCS();
            ResolveAbsoluteType(itemContext, ufcs.type);
            if (!ufcs.trait || !ufcs.trait->isValid()) {
                ERROR(item.sp, E0000, "Qualified path without a trait in glob delegation");
            }
            ResolveAbsolutePath(itemContext, item.sp, Context::LookupMode::Type, *ufcs.trait);
            const auto* binding = ufcs.trait->bindings.type.binding.opt_Trait();
            if (!binding) {
                ERROR(item.sp, E0000, "Delegation glob target is not a trait: " << *ufcs.trait);
            }
            astTrait = binding->trait_;
            hirTrait = binding->hir;
        } else {
            ResolveAbsolutePath(itemContext, item.sp, Context::LookupMode::Type, targetPath);
            const auto* binding = targetPath.bindings.type.binding.opt_Trait();
            if (!binding) {
                ERROR(item.sp, E0000, "Delegation glob target is not a trait: " << targetPath);
            }
            astTrait = binding->trait_;
            hirTrait = binding->hir;
        }

        ::std::vector<RcString> names;
        if (astTrait) {
            for (const auto& traitItem : astTrait->items()) {
                if (traitItem.data.is_Function()) {
                    names.push_back(traitItem.name);
                }
            }
        } else {
            ASSERT_BUG(item.sp, hirTrait, "Trait binding without AST or HIR data");
            for (const auto& traitItem : hirTrait->values) {
                if (traitItem.second.is_Function()) {
                    names.push_back(traitItem.first);
                }
            }
            ::std::sort(names.begin(), names.end());
        }
        if (names.empty()) {
            ERROR(item.sp, E0000, "Empty glob delegation is not supported");
        }

        const auto span = item.sp;
        const auto attrs = item.attrs.clone();
        const auto vis = item.vis;
        const auto isSpecialisable = item.isSpecialisable;
        const auto templateFcn = fcn.clone();
        *item.data = ASTItem::make_None({});
        for (const auto& name : names) {
            if (explicitNames.count(name) != 0) {
                continue;
            }
            auto expanded = templateFcn.clone();
            auto delegation = expanded.takeDelegation();
            delegation->targets.front().path.append(ASTPathNode(name, {}));
            delegation->targets.front().name = name;
            expanded.setDelegation(mv$(*delegation));
            impl.addFunction(span, attrs.clone(), vis, isSpecialisable, name, mv$(expanded));
        }
    }
}

// - For impl blocks
void ResolveAbsoluteImplItems(Context& itemContext, ASTImpl& impl) {
    TRACE_FUNCTION_F("");
    const ASTTrait* implementedTrait = nullptr;
    const HIRTrait* implementedHirTrait = nullptr;
    if (impl.def().trait().ent.isValid()) {
        if (const auto* binding = impl.def().trait().ent.bindings.type.binding.opt_Trait()) {
            implementedTrait = binding->trait_;
            implementedHirTrait = binding->hir;
        }
    }
    for (auto& i : impl.items()) {
        switch ((*i.data).tag()) {
            case ASTItem::TAG_None: {
                break;
            }
            case ASTItem::TAG_MacroInv: {
                break;
            }
            case ASTItem::TAG_Impl: {
                BUG(i.sp, "Resolve_Absolute_ImplItems - " << i.data->tagStr());
                break;
            }
            case ASTItem::TAG_NegImpl: {
                BUG(i.sp, "Resolve_Absolute_ImplItems - " << i.data->tagStr());
                break;
            }
            case ASTItem::TAG_ExternBlock: {
                BUG(i.sp, "Resolve_Absolute_ImplItems - " << i.data->tagStr());
                break;
            }
            case ASTItem::TAG_GlobalAsm: {
                BUG(i.sp, "Resolve_Absolute_ImplItems - " << i.data->tagStr());
                break;
            }
            case ASTItem::TAG_Macro: {
                BUG(i.sp, "Resolve_Absolute_ImplItems - " << i.data->tagStr());
                break;
            }
            case ASTItem::TAG_Use: {
                BUG(i.sp, "Resolve_Absolute_ImplItems - " << i.data->tagStr());
                break;
            }
            case ASTItem::TAG_Module: {
                BUG(i.sp, "Resolve_Absolute_ImplItems - " << i.data->tagStr());
                break;
            }
            case ASTItem::TAG_Crate: {
                BUG(i.sp, "Resolve_Absolute_ImplItems - " << i.data->tagStr());
                break;
            }
            case ASTItem::TAG_Enum: {
                BUG(i.sp, "Resolve_Absolute_ImplItems - " << i.data->tagStr());
                break;
            }
            case ASTItem::TAG_Trait: {
                BUG(i.sp, "Resolve_Absolute_ImplItems - " << i.data->tagStr());
                break;
            }
            case ASTItem::TAG_TraitAlias: {
                BUG(i.sp, "Resolve_Absolute_ImplItems - " << i.data->tagStr());
                break;
            }
            case ASTItem::TAG_Struct: {
                BUG(i.sp, "Resolve_Absolute_ImplItems - " << i.data->tagStr());
                break;
            }
            case ASTItem::TAG_Union: {
                BUG(i.sp, "Resolve_Absolute_ImplItems - " << i.data->tagStr());
                break;
            }
            case ASTItem::TAG_Type: {
                auto& e = (*i.data).as_Type();
                DEBUG("Type - " << i.name);
                //ASSERT_BUG( i.span, e.params().m_params.size() == 0, "TODO: Generic Associated Types (impl)" );
                itemContext.push(e.params(), GenericSlot::Level::Method, true);
                ResolveAbsoluteGeneric(itemContext, e.params());

                ResolveAbsoluteType(itemContext, e.type());

                itemContext.pop(e.params(), true);
                break;
            }
            case ASTItem::TAG_Function: {
                auto& e = (*i.data).as_Function();
                DEBUG("Function - " << i.name);
                DelegationSignatureSource signatureSource;
                if (implementedTrait) {
                    signatureSource.ast = FindTraitFunction(*implementedTrait, i.name);
                } else if (implementedHirTrait) {
                    auto it = implementedHirTrait->values.find(i.name);
                    if (it != implementedHirTrait->values.end()) {
                        signatureSource.hir = it->second.opt_Function();
                    }
                }
                ResolveAbsoluteFunction(itemContext, e, signatureSource, true, impl.def().trait().ent.isValid());
                break;
            }
            case ASTItem::TAG_Static: {
                auto& e = (*i.data).as_Static();
                DEBUG("Static - " << i.name); ResolveAbsoluteStatic(itemContext, e);
                break;
            }
        }
    }
}

void AppendGenericParams(ASTGenericParams& destination, const ASTGenericParams& source) {
    auto cloned = source.clone();
    const auto boundOffset = destination.bounds.size();
    for (auto& param : cloned.params) {
        if (param.boundsStart != SIZE_MAX) {
            param.boundsStart += boundOffset;
            param.boundsEnd += boundOffset;
        }
        destination.params.push_back(mv$(param));
    }
    for (auto& bound : cloned.bounds) {
        destination.bounds.push_back(mv$(bound));
    }
    for (auto*& type : cloned.bareBoundTypes) {
        destination.bareBoundTypes.push_back(mv$(type));
    }
}

void ReplaceDelegatedSelf(ASTType*& type, const RcString& replacementName);
void ReplaceDelegatedSelf(ASTPath& path, const RcString& replacementName);

void ReplaceDelegatedSelf(ASTPathParams& params, const RcString& replacementName) {
    for (auto& param : params.entries) {
        switch (param.tag()) {
            case ASTPathParamEnt::TAG_Null: {
                break;
            }
            case ASTPathParamEnt::TAG_Lifetime: {
                break;
            }
            case ASTPathParamEnt::TAG_Type: {
                auto& e = param.as_Type();
                ReplaceDelegatedSelf(e, replacementName);
                break;
            }
            case ASTPathParamEnt::TAG_Value: {
                break;
            }
            case ASTPathParamEnt::TAG_AssociatedTyEqual: {
                auto& e = param.as_AssociatedTyEqual();
                ReplaceDelegatedSelf(e.first.args(), replacementName);
                ReplaceDelegatedSelf(e.second, replacementName);
                break;
            }
            case ASTPathParamEnt::TAG_AssociatedTyBound: {
                auto& e = param.as_AssociatedTyBound();
                ReplaceDelegatedSelf(e.first.args(), replacementName);
                for (auto& trait : e.second) {
                    ReplaceDelegatedSelf(*trait.path, replacementName);
                }
                break;
            }
        }
    }
}

void ReplaceDelegatedSelf(ASTPath& path, const RcString& replacementName) {
    if (!path.cls.is_Local() && !path.cls.is_Invalid()) {
        for (auto& node : path.nodes()) {
            ReplaceDelegatedSelf(node.args(), replacementName);
        }
    }
    if (auto* ufcs = path.cls.opt_UFCS()) {
        ReplaceDelegatedSelf(ufcs->type, replacementName);
        if (ufcs->trait) {
            ReplaceDelegatedSelf(*ufcs->trait, replacementName);
        }
    }
}

void ReplaceDelegatedSelf(ASTType*& type, const RcString& replacementName) {
    switch (type->data.tag()) {
        case TypeData::TAG_None: {
            break;
        }
        case TypeData::TAG_Any: {
            break;
        }
        case TypeData::TAG_Bang: {
            break;
        }
        case TypeData::TAG_Unit: {
            break;
        }
        case TypeData::TAG_Macro: {
            break;
        }
        case TypeData::TAG_Primitive: {
            break;
        }
        case TypeData::TAG_Function: {
            auto& e = type->data.as_Function();
            ReplaceDelegatedSelf(e.info.rettype, replacementName);
            for (auto*& arg : e.info.argTypes) { ReplaceDelegatedSelf(arg, replacementName); }
            break;
        }
        case TypeData::TAG_Tuple: {
            auto& e = type->data.as_Tuple();
            for (auto*& inner : e.innerTypes) { ReplaceDelegatedSelf(inner, replacementName); }
            break;
        }
        case TypeData::TAG_Borrow: {
            auto& e = type->data.as_Borrow();
            ReplaceDelegatedSelf(e.inner, replacementName);
            break;
        }
        case TypeData::TAG_Pointer: {
            auto& e = type->data.as_Pointer();
            ReplaceDelegatedSelf(e.inner, replacementName);
            break;
        }
        case TypeData::TAG_Array: {
            auto& e = type->data.as_Array();
            ReplaceDelegatedSelf(e.inner, replacementName);
            break;
        }
        case TypeData::TAG_Slice: {
            auto& e = type->data.as_Slice();
            ReplaceDelegatedSelf(e.inner, replacementName);
            break;
        }
        case TypeData::TAG_Pattern: {
            auto& e = type->data.as_Pattern();
            ReplaceDelegatedSelf(e.inner, replacementName);
            break;
        }
        case TypeData::TAG_Generic: {
            auto& e = type->data.as_Generic();
            if (e.name == rcstringSelf) { e.name = replacementName; }
            break;
        }
        case TypeData::TAG_Path: {
            auto& e = type->data.as_Path();
            ReplaceDelegatedSelf(*e, replacementName);
            break;
        }
        case TypeData::TAG_TraitObject: {
            auto& e = type->data.as_TraitObject();
            for (auto& trait : e.traits) { ReplaceDelegatedSelf(*trait.path, replacementName); }
            break;
        }
        case TypeData::TAG_ErasedType: {
            auto& e = type->data.as_ErasedType();
            for (auto& trait : e->traits) { ReplaceDelegatedSelf(*trait.path, replacementName); }
            for (auto& trait : e->maybeTraits) { ReplaceDelegatedSelf(*trait.path, replacementName); }
            if (e->use) { ReplaceDelegatedSelf(*e->use, replacementName); }
            break;
        }
    }
}

void ReplaceDelegatedSelf(ASTFunction& function, const RcString& replacementName) {
    ReplaceDelegatedSelf(function.rettype(), replacementName);
    for (auto& arg : function.args()) {
        ReplaceDelegatedSelf(arg.ty, replacementName);
    }
    for (auto& param : function.params().params) {
        switch (param.tag()) {
            case GenericParam::TAG_None: {
                break;
            }
            case GenericParam::TAG_Lifetime: {
                break;
            }
            case GenericParam::TAG_Type: {
                auto& e = param.as_Type();
                ReplaceDelegatedSelf(e.getDefault(), replacementName);
                break;
            }
            case GenericParam::TAG_Value: {
                auto& e = param.as_Value();
                ReplaceDelegatedSelf(e.type(), replacementName);
                break;
            }
        }
    }
    for (auto& bound : function.params().bounds) {
        switch (bound.tag()) {
            case ASTGenericBound::TAG_None: {
                break;
            }
            case ASTGenericBound::TAG_Lifetime: {
                break;
            }
            case ASTGenericBound::TAG_TypeLifetime: {
                auto& e = bound.as_TypeLifetime();
                ReplaceDelegatedSelf(e.type, replacementName);
                break;
            }
            case ASTGenericBound::TAG_IsTrait: {
                auto& e = bound.as_IsTrait();
                ReplaceDelegatedSelf(e.type, replacementName); ReplaceDelegatedSelf(e.trait, replacementName);
                break;
            }
            case ASTGenericBound::TAG_MaybeTrait: {
                auto& e = bound.as_MaybeTrait();
                ReplaceDelegatedSelf(e.type, replacementName); ReplaceDelegatedSelf(e.trait, replacementName);
                break;
            }
            case ASTGenericBound::TAG_NotTrait: {
                auto& e = bound.as_NotTrait();
                ReplaceDelegatedSelf(e.type, replacementName); ReplaceDelegatedSelf(e.trait, replacementName);
                break;
            }
            case ASTGenericBound::TAG_Equality: {
                auto& e = bound.as_Equality();
                ReplaceDelegatedSelf(e.type, replacementName); ReplaceDelegatedSelf(e.replacement, replacementName);
                break;
            }
        }
    }
}

eCoreType HIRCoreTypeToAST(HIRCoreType type) {
    switch (type) {
        case HIRCoreType::Usize: return CORETYPE_UINT;
        case HIRCoreType::Isize: return CORETYPE_INT;
        case HIRCoreType::U8: return CORETYPE_U8;
        case HIRCoreType::I8: return CORETYPE_I8;
        case HIRCoreType::U16: return CORETYPE_U16;
        case HIRCoreType::I16: return CORETYPE_I16;
        case HIRCoreType::U32: return CORETYPE_U32;
        case HIRCoreType::I32: return CORETYPE_I32;
        case HIRCoreType::U64: return CORETYPE_U64;
        case HIRCoreType::I64: return CORETYPE_I64;
        case HIRCoreType::U128: return CORETYPE_U128;
        case HIRCoreType::I128: return CORETYPE_I128;
        case HIRCoreType::F16: return CORETYPE_F16;
        case HIRCoreType::F32: return CORETYPE_F32;
        case HIRCoreType::F64: return CORETYPE_F64;
        case HIRCoreType::F128: return CORETYPE_F128;
        case HIRCoreType::Bool: return CORETYPE_BOOL;
        case HIRCoreType::Char: return CORETYPE_CHAR;
        case HIRCoreType::Str: return CORETYPE_STR;
    }
    throw "";
}

ASTBoundConstness HIRConstnessToAST(HIRBoundConstness constness) {
    switch (constness) {
        case HIRBoundConstness::Never: return ASTBoundConstness::Never;
        case HIRBoundConstness::Always: return ASTBoundConstness::Always;
        case HIRBoundConstness::Maybe: return ASTBoundConstness::Maybe;
    }
    throw "";
}

ASTType* HIRTypeToAST(Context& context, const Span& span, HIRTypeRef type);

ASTPathParams HIRPathParamsToAST(Context& context, const Span& span, const HIRPathParams& params) {
    ASTPathParams rv;
    for (const auto& type : params.types) {
        rv.entries.push_back(HIRTypeToAST(context, span, type));
    }
    ASSERT_BUG(span, params.values.empty(), "Const generics in an external delegation signature");
    return rv;
}

ASTPath HIRGenericPathToAST(Context& context, const Span& span, const HIRGenericPath& path) {
    return ASTPath(spToAp(path.path), HIRPathParamsToAST(context, span, path.params));
}

ASTPath HIRTraitPathToAST(Context& context, const Span& span, const HIRTraitPath& trait) {
    auto rv = HIRGenericPathToAST(context, span, trait.path);
    for (const auto& assoc : trait.typeBounds) {
        rv.nodes().back().args().entries.push_back(ASTPathParamEnt::make_AssociatedTyEqual({
            ASTPathNode(assoc.first, HIRPathParamsToAST(context, span, assoc.second.atyParams)),
            HIRTypeToAST(context, span, assoc.second.type)
        }));
    }
    return rv;
}

ASTPath HIRPathToAST(Context& context, const Span& span, const HIRPath& path) {
    switch (path.data.tag()) {
        case HIRPathData::TAG_Generic: {
            auto& e = path.data.as_Generic();
            return HIRGenericPathToAST(context, span, e);
        }
        case HIRPathData::TAG_UfcsInherent: {
            auto& e = path.data.as_UfcsInherent();
            return ASTPath::newUfcsTy(
                HIRTypeToAST(context, span, e.type),
                {ASTPathNode(e.item, HIRPathParamsToAST(context, span, e.params))}
            );
        }
        case HIRPathData::TAG_UfcsKnown: {
            auto& e = path.data.as_UfcsKnown();
            return ASTPath::newUfcsTrait(
                HIRTypeToAST(context, span, e.type),
                HIRGenericPathToAST(context, span, e.trait),
                {ASTPathNode(e.item, HIRPathParamsToAST(context, span, e.params))}
            );
        }
        case HIRPathData::TAG_UfcsUnknown: {
            auto& e = path.data.as_UfcsUnknown();
            return ASTPath::newUfcsTy(
                HIRTypeToAST(context, span, e.type),
                {ASTPathNode(e.item, HIRPathParamsToAST(context, span, e.params))}
            );
        }
    }
    throw "";
}

ASTType* HIRTypeToAST(Context& context, const Span& span, HIRTypeRef type) {
    auto& pool = context.typePool();
    switch ((*type).tag()) {
        case HIRTypeData::TAG_Infer: {
            return mkType(pool, span);
        }
        case HIRTypeData::TAG_Diverge: {
            return mkType(pool, span, TypeData::make_Bang({}));
        }
        case HIRTypeData::TAG_Primitive: {
            auto& e = (*type).as_Primitive();
            return mkType(pool, span, HIRCoreTypeToAST(e));
        }
        case HIRTypeData::TAG_Path: {
            auto& e = (*type).as_Path();
            return mkType(pool, span, HIRPathToAST(context, span, e.path));
        }
        case HIRTypeData::TAG_Generic: {
            auto& e = (*type).as_Generic();
            const auto name = e.binding != GENERICSelf && e.group() == GENERICItem
                ? RcString::newInterned(FMT("#hir-item-" << e.idx()))
                : e.name;
            return mkType(pool, span, name, e.binding);
        }
        case HIRTypeData::TAG_TraitObject: {
            auto& e = (*type).as_TraitObject();
            ::std::vector<TypeTraitPath> traits;
            traits.push_back(TypeTraitPath({}, HIRTraitPathToAST(context, span, e.trait), HIRConstnessToAST(e.trait.constness)));
            for (const auto& marker : e.markers) {
                traits.push_back(TypeTraitPath({}, HIRGenericPathToAST(context, span, marker)));
            }
            return mkType(pool, span, mv$(traits), {});
        }
        case HIRTypeData::TAG_ErasedType: {
            BUG(span, "Erased type in an external delegation signature");
            break;
        }
        case HIRTypeData::TAG_Array: {
            BUG(span, "Array in an external delegation signature");
            break;
        }
        case HIRTypeData::TAG_Slice: {
            auto& e = (*type).as_Slice();
            return mkType(pool, ASTTypeTags::UnsizedArray(), span, HIRTypeToAST(context, span, e.inner));
        }
        case HIRTypeData::TAG_Pattern: {
            BUG(span, "Pattern type in an external delegation signature");
            break;
        }
        case HIRTypeData::TAG_Tuple: {
            auto& e = (*type).as_Tuple();
            ::std::vector<ASTType*> types;
            for (const auto& inner : e) { types.push_back(HIRTypeToAST(context, span, inner)); }
            return mkType(pool, ASTTypeTags::Tuple(), span, mv$(types));
        }
        case HIRTypeData::TAG_Borrow: {
            auto& e = (*type).as_Borrow();
            return mkType(pool, ASTTypeTags::Reference(), span, ASTLifetimeRef(), e.type == HIRBorrowType::Unique, HIRTypeToAST(context, span, e.inner));
        }
        case HIRTypeData::TAG_Pointer: {
            auto& e = (*type).as_Pointer();
            return mkType(pool, ASTTypeTags::Pointer(), span, e.type == HIRBorrowType::Unique, HIRTypeToAST(context, span, e.inner));
        }
        case HIRTypeData::TAG_NamedFunction: {
            BUG(span, "Named function type in an external delegation signature");
            break;
        }
        case HIRTypeData::TAG_Function: {
            auto& e = (*type).as_Function();
            ::std::vector<ASTType*> args;
            for (const auto& arg : e.argTypes) { args.push_back(HIRTypeToAST(context, span, arg)); }
            return mkType(pool, ASTTypeTags::Function(), span, {}, e.isUnsafe, e.abi.c_str(), mv$(args), e.isVariadic, HIRTypeToAST(context, span, e.rettype));
        }
        case HIRTypeData::TAG_NodeType: {
            BUG(span, "Node type in an external delegation signature");
            break;
        }
    }
    throw "";
}

ASTGenericParams HIRGenericParamsToAST(Context& context, const Span& span, const HIRGenericParams& params) {
    ASTGenericParams rv;
    for (size_t i = 0; i < params.types.size(); i++) {
        rv.addTyParam(ASTTypeParam(context.typePool(), span, {}, RcString::newInterned(FMT("#hir-item-" << i))));
    }
    for (const auto& value : params.values) {
        rv.addValueParam(span, {}, Ident(value.name), HIRTypeToAST(context, span, value.type), {});
    }
    for (const auto& bound : params.bounds) {
        switch (bound.tag()) {
            case HIRGenericBound::TAG_TraitBound: {
                auto& e = bound.as_TraitBound();
                rv.addBound(ASTGenericBound::make_IsTrait({
                    span,
                    {},
                    HIRTypeToAST(context, span, e.type),
                    {},
                    HIRTraitPathToAST(context, span, e.trait),
                    HIRConstnessToAST(e.constness)
                }));
                break;
            }
            case HIRGenericBound::TAG_TypeEquality: {
                auto& e = bound.as_TypeEquality();
                rv.addBound(ASTGenericBound::make_Equality({
                    HIRTypeToAST(context, span, e.type),
                    HIRTypeToAST(context, span, e.otherType)
                }));
                break;
            }
        }
    }
    return rv;
}

ASTFunction HIRFunctionToAST(Context& context, const Span& span, const HIRFunction& function) {
    ASTFunction::Arglist args;
    for (size_t i = 0; i < function.args.size(); i++) {
        const bool receiver = i == 0 && function.receiver != HIRFunction::Receiver::Free;
        const auto name = receiver ? RcString::newInterned("self") : RcString::newInterned(FMT("arg" << i));
        args.push_back(ASTFunction::Arg(
            ASTPattern(ASTPattern::TagBind(), span, name),
            HIRTypeToAST(context, span, function.args[i].second)
        ));
    }
    auto flags = ASTFunction::Flags();
    if (function.unsafe) { flags = flags.setUnsafe(); }
    if (function.isConst) { flags = flags.setConst(); }
    return ASTFunction(
        span,
        function.abi.c_str(),
        flags,
        HIRGenericParamsToAST(context, span, function.params),
        HIRTypeToAST(context, span, function.returnType),
        mv$(args),
        function.variadic
    );
}

const HIRFunction* FindHIRTraitFunction(const HIRTrait& trait, const RcString& name) {
    auto it = trait.values.find(name);
    return it == trait.values.end() ? nullptr : it->second.opt_Function();
}

void ResolveAbsoluteFunction(
    Context& itemContext,
    ASTFunction& fcn,
    DelegationSignatureSource signatureSource,
    bool hasParentSelf,
    bool isTraitImpl
) {
    TRACE_FUNCTION_F("");
    // A `reuse` delegation copies the generic parameters of the function it
    // stands for, so those may repeat what the impl around it already named.
    const bool fromDelegation = fcn.delegation() != nullptr;
    if (auto delegation = fcn.takeDelegation()) {
        ASSERT_BUG(fcn.sp(), delegation->targets.size() == 1, "TODO: Expand delegation lists before name resolution");
        auto target = mv$(delegation->targets.front().path);
        ResolveAbsolutePath(itemContext, fcn.sp(), Context::LookupMode::Variable, target);
        const auto* binding = target.bindings.value.binding.opt_Function();
        const auto* targetFunction = binding ? binding->func_ : nullptr;
        const HIRFunction* targetHirFunction = nullptr;
        if (target.cls.is_UFCS()) {
            const auto& ufcs = target.cls.as_UFCS();
            if (ufcs.trait && ufcs.trait->isValid() && !ufcs.nodes.empty()) {
                if (const auto* traitBinding = ufcs.trait->bindings.type.binding.opt_Trait(); traitBinding && traitBinding->hir) {
                    targetHirFunction = FindHIRTraitFunction(*traitBinding->hir, ufcs.nodes.back().name());
                }
            }
        }
        ASSERT_BUG(fcn.sp(), targetFunction || targetHirFunction, "Delegation target is not a function: " << target);

        auto replacement = signatureSource.ast
            ? signatureSource.ast->clone()
            : signatureSource.hir
                ? HIRFunctionToAST(itemContext, fcn.sp(), *signatureSource.hir)
                : targetFunction
                    ? targetFunction->clone()
                    : HIRFunctionToAST(itemContext, fcn.sp(), *targetHirFunction);
        const ASTTrait* targetTrait = nullptr;
        if (target.cls.is_UFCS()) {
            const auto& ufcs = target.cls.as_UFCS();
            if (ufcs.trait && ufcs.trait->isValid()) {
                if (const auto* traitBinding = ufcs.trait->bindings.type.binding.opt_Trait()) {
                    targetTrait = traitBinding->trait_;
                }
            }
        }
        if (targetTrait && !isTraitImpl) {
            ASTGenericParams merged;
            if (!hasParentSelf) {
                const auto delegatedSelf = RcString::newInterned("#delegation-Self");
                merged.addTyParam(ASTTypeParam(itemContext.typePool(), fcn.sp(), {}, delegatedSelf));
                ReplaceDelegatedSelf(replacement, delegatedSelf);

                const auto& ufcs = target.cls.as_UFCS();
                auto traitBound = ASTPath(*ufcs.trait);
                auto& traitArgs = traitBound.nodes().back().args().entries;
                traitArgs.clear();
                for (const auto& param : targetTrait->params().params) {
                    switch (param.tag()) {
                        case GenericParam::TAG_None: {
                            break;
                        }
                        case GenericParam::TAG_Lifetime: {
                            auto& e = param.as_Lifetime();
                            traitArgs.push_back(ASTLifetimeRef(e.name()));
                            break;
                        }
                        case GenericParam::TAG_Type: {
                            auto& e = param.as_Type();
                            traitArgs.push_back(mkType(itemContext.typePool(), fcn.sp(), e.name()));
                            break;
                        }
                        case GenericParam::TAG_Value: {
                            auto& e = param.as_Value();
                            traitArgs.push_back(ASTExprNodeP(new ASTExprNodeNamedValue(ASTPath(e.name().name))));
                            break;
                        }
                    }
                }
                merged.addBound(ASTGenericBound::make_IsTrait({
                    fcn.sp(),
                    {},
                    mkType(itemContext.typePool(), fcn.sp(), delegatedSelf),
                    {},
                    mv$(traitBound)
                }));
            }
            AppendGenericParams(merged, targetTrait->params());
            AppendGenericParams(merged, replacement.params());
            ::std::stable_sort(merged.params.begin(), merged.params.end(), [](const auto& left, const auto& right) {
                return left.is_Lifetime() && !right.is_Lifetime();
            });
            replacement.params() = mv$(merged);
        }
        const bool isMethod = hasParentSelf && !replacement.args().empty()
            && replacement.args().front().pat.bindings().size() == 1
            && replacement.args().front().pat.bindings().front().name.name == "self";
        ::std::vector<ASTExprNodeP> args;
        for (size_t i = 0; i < replacement.args().size(); i++) {
            auto name = isMethod && i == 0 ? RcString::newInterned("self") : RcString::newInterned(FMT("arg" << i));
            replacement.args()[i].pat = ASTPattern(ASTPattern::TagBind(), fcn.sp(), name);
            ASTExprNodeP arg(new ASTExprNodeNamedValue(ASTPath(name)));
            if (i == 0 && delegation->body.isValid()) {
                arg = delegation->body.node().clone();
                if (auto* block = cast<ASTExprNodeBlock>(arg.get()); block && !block->localMod
                    && block->nodes.size() == 1 && !block->nodes.front().hasSemicolon) {
                    auto unwrapped = mv$(block->nodes.front().node);
                    arg = mv$(unwrapped);
                }
                struct ReplaceDelegationSelf: ASTNodeVisitorDef {
                    RcString replacement;
                    ReplaceDelegationSelf(RcString replacement): replacement(mv$(replacement)) {}
                    void visit(ASTExprNodeNamedValue& node) override {
                        if (node.path.cls.is_Local() && node.path.cls.as_Local().name == "#delegation-self") {
                            node.path = ASTPath(replacement);
                        }
                    }
                } visitor(name);
                arg->visit(visitor);

                const auto targetBorrow = targetFunction && !targetFunction->args().empty() && targetFunction->args().front().ty->data.is_Borrow();
                const auto targetHirBorrow = targetHirFunction && (
                    targetHirFunction->receiver == HIRFunction::Receiver::BorrowOwned
                    || targetHirFunction->receiver == HIRFunction::Receiver::BorrowUnique
                    || targetHirFunction->receiver == HIRFunction::Receiver::BorrowShared
                );
                if (targetBorrow || targetHirBorrow) {
                    const bool isMut = targetBorrow
                        ? targetFunction->args().front().ty->data.as_Borrow().isMut
                        : targetHirFunction->receiver == HIRFunction::Receiver::BorrowUnique;
                    arg = ASTExprNodeP(new ASTExprNodeUniOp(
                        isMut ? ASTExprNodeUniOp::REFMUT : ASTExprNodeUniOp::REF,
                        mv$(arg)
                    ));
                }
            }
            args.push_back(mv$(arg));
        }
        replacement.setCode(ASTExpr(new ASTExprNodeCallPath(mv$(target), mv$(args))));
        fcn = mv$(replacement);
    }
    itemContext.push(fcn.params(), GenericSlot::Level::Method, /*hasSelf=*/false, /*allowShadowing=*/fromDelegation);
    itemContext.iblTargetGenerics = &fcn.params();
    DEBUG("- Generics");
    ResolveAbsoluteGeneric(itemContext, fcn.params());

    DEBUG("- Prototype types");
    ResolveAbsoluteType(itemContext, fcn.rettype());
    for (auto& arg : fcn.args()) {
        ResolveAbsoluteType(itemContext, arg.ty);
    }
    itemContext.iblTargetGenerics = nullptr;

    DEBUG("- Body");
    {
        auto _h = itemContext.enterRootblock();
        itemContext.pushBlock();
        for (auto& arg : fcn.args()) {
            itemContext.startPatbind();
            ResolveAbsolutePattern(itemContext, false, arg.pat);
            itemContext.endPatbind();
        }

        ResolveAbsoluteExpr(itemContext, fcn.code());

        itemContext.popBlock();
    }

    itemContext.pop(fcn.params());
}

void ResolveAbsoluteStatic(Context& itemContext, ASTStatic& e) {
    itemContext.push(e.params(), GenericSlot::Level::Method);
    auto* previousIblTarget = itemContext.iblTargetGenerics;
    itemContext.iblTargetGenerics = &e.params();
    ResolveAbsoluteGeneric(itemContext, e.params());
    ResolveAbsoluteType(itemContext, e.type());
    itemContext.iblTargetGenerics = previousIblTarget;
    {
        auto _h = itemContext.enterRootblock();
        ResolveAbsoluteExpr(itemContext, e.value());
    }
    itemContext.pop(e.params());
}

void ResolveAbsoluteStruct(Context& itemContext, ASTStruct& e) {
    itemContext.push(e.params(), GenericSlot::Level::Top, true);
    ResolveAbsoluteGeneric(itemContext, e.params());

    switch (e.data.tag()) {
        case ASTStructData::TAG_Unit: {
            break;
        }
        case ASTStructData::TAG_Tuple: {
            auto& s = e.data.as_Tuple();
            for (auto& field : s.ents) { ResolveAbsoluteType(itemContext, field.type); }
            break;
        }
        case ASTStructData::TAG_Struct: {
            auto& s = e.data.as_Struct();
            for (auto& field : s.ents) {
                ResolveAbsoluteType(itemContext, field.type);
                ResolveAbsoluteExpr(itemContext, field.defaultValue);
            }
            break;
        }
    }

    itemContext.pop(e.params());
}

void ResolveAbsoluteUnion(Context& itemContext, ASTUnion& e) {
    itemContext.push(e.params_, GenericSlot::Level::Top, true);
    ResolveAbsoluteGeneric(itemContext, e.params_);

    for (auto& field : e.variants) {
        ResolveAbsoluteType(itemContext, field.type);
    }

    itemContext.pop(e.params_);
}

void ResolveAbsoluteTrait(Context& itemContext, ASTTrait& e) {
    itemContext.push(e.params(), GenericSlot::Level::Top, true);
    ResolveAbsoluteGeneric(itemContext, e.params());

    for (auto& lft : e.lifetimes()) {
        ResolveAbsoluteLifetime(itemContext, lft.sp, lft.ent);
    }
    for (auto& st : e.supertraits()) {
        if (!st.ent.path->isValid()) {
            DEBUG("- ST 'static");
        } else {
            DEBUG("- ST " << st.ent.hrbs << *st.ent.path);
            itemContext.push(st.ent.hrbs);
            ResolveAbsolutePath(itemContext, st.sp, Context::LookupMode::Type, *st.ent.path);
            itemContext.pop(st.ent.hrbs);
        }
    }

    ResolveAbsoluteImplItems(itemContext, e.items());

    itemContext.pop(e.params(), true);
}

void ResolveAbsoluteEnum(Context& itemContext, ASTEnum& e) {
    itemContext.push(e.params(), GenericSlot::Level::Top, true);
    ResolveAbsoluteGeneric(itemContext, e.params());

    for (auto& variant : e.variants()) {
        switch (variant.data.tag()) {
            case ASTEnumVariantData::TAG_Unit: {
                break;
            }
            case ASTEnumVariantData::TAG_Tuple: {
                auto& s = variant.data.as_Tuple();
                for (auto& field : s.items) { ResolveAbsoluteType(itemContext, field.type); }
                break;
            }
            case ASTEnumVariantData::TAG_Struct: {
                auto& s = variant.data.as_Struct();
                for (auto& field : s.fields) {
                    ResolveAbsoluteType(itemContext, field.type);
                    ResolveAbsoluteExpr(itemContext, field.defaultValue);
                }
                break;
            }
        }
        auto _h = itemContext.enterRootblock();
        ResolveAbsoluteExpr(itemContext, variant.discriminantValue);
    }

    itemContext.pop(e.params());
}

void ResolveAbsoluteMod(const Settings& settings, const ASTCrate& crate, ASTModule& mod) {
    ResolveAbsoluteMod(Context{settings, crate, mod}, mod);
}

void ResolveAbsoluteMod(Context itemContext, ASTModule& mod) {
    TRACE_FUNCTION_F("mod=" << mod.path());

    for (auto& i : mod.items) {
        switch (i->data.tag()) {
            case ASTItem::TAG_None: {
                break;
            }
            case ASTItem::TAG_MacroInv: {
                break;
            }
            case ASTItem::TAG_Use: {
                break;
            }
            case ASTItem::TAG_Macro: {
                break;
            }
            case ASTItem::TAG_GlobalAsm: {
                auto& e = i->data.as_GlobalAsm();
                for (auto& operand : e.operands) {
                    switch (operand.tag()) {
                        case ASTGlobalAsmOperand::TAG_Const: {
                            auto& expr = operand.as_Const();
                            auto rootBlock = itemContext.enterRootblock();
                            ResolveAbsoluteExprNode(itemContext, *expr);
                            break;
                        }
                        case ASTGlobalAsmOperand::TAG_Sym: {
                            auto& path = operand.as_Sym();
                            ResolveAbsolutePath(itemContext, i->span, Context::LookupMode::Variable, path);
                            break;
                        }
                    }
                }
                break;
            }
            case ASTItem::TAG_ExternBlock: {
                auto& e = i->data.as_ExternBlock();
                for (auto& i2 : e.items()) {
                    switch (i2.data.tag()) {
                        case ASTItem::TAG_None: {
                            break;
                        }
                        case ASTItem::TAG_Function: {
                            auto& e2 = i2.data.as_Function();
                            ResolveAbsoluteFunction(itemContext, e2);
                            break;
                        }
                        case ASTItem::TAG_Static: {
                            auto& e2 = i2.data.as_Static();
                            ResolveAbsoluteStatic(itemContext, e2);
                            break;
                        }
                        default: {
                            BUG(i->span, "Unexpected item in ExternBlock - " << i2.data.tagStr());
                            break;
                        }
                    }
                }
                break;
            }
            case ASTItem::TAG_Impl: {
                auto& e = i->data.as_Impl();
                auto& def = e.def();
                if (!def.type()->isValid()) {
                    TRACE_FUNCTION_F("impl " << def.trait().ent << " for ..");
                    itemContext.push(def.params(), GenericSlot::Level::Top);

                    itemContext.iblTargetGenerics = &def.params();
                    assert(def.trait().ent.isValid());
                    ResolveAbsolutePath(itemContext, def.trait().sp, Context::LookupMode::Type, def.trait().ent);
                    itemContext.iblTargetGenerics = nullptr;

                    ResolveAbsoluteGeneric(itemContext, def.params());

                    if (e.items().size() != 0) {
                        ERROR(i->span, E0000, "impl Trait for .. with methods");
                    }

                    itemContext.pop(def.params());

                    // HACK: Mutate the source to indicate that it's an auto trait
                    const_cast<ASTTrait*>(def.trait().ent.bindings.type.binding.as_Trait().trait_)->setIsMarker();
                } else {
                    TRACE_FUNCTION_F("impl " << def.trait().ent << " for " << def.type());
                    itemContext.pushSelf(def.type());
                    itemContext.push(def.params(), GenericSlot::Level::Top);

                    itemContext.iblTargetGenerics = &def.params();
                    ResolveAbsoluteType(itemContext, def.type());
                    if (def.trait().ent.isValid()) {
                        ResolveAbsolutePath(itemContext, def.trait().sp, Context::LookupMode::Type, def.trait().ent);
                    }
                    itemContext.iblTargetGenerics = nullptr;

                    ResolveAbsoluteGeneric(itemContext, def.params());

                    ExpandDelegationGlobs(itemContext, e);
                    ResolveAbsoluteImplItems(itemContext, e);

                    itemContext.pop(def.params());
                    itemContext.popSelf(def.type());
                }
                break;
            }
            case ASTItem::TAG_NegImpl: {
                auto& e = i->data.as_NegImpl();
                auto& implDef = e;
                TRACE_FUNCTION_F("impl ! " << implDef.trait().ent << " for " << implDef.type());
                itemContext.pushSelf(implDef.type());
                itemContext.push(implDef.params(), GenericSlot::Level::Top);

                itemContext.iblTargetGenerics = &implDef.params();
                ResolveAbsoluteType(itemContext, implDef.type());
                if (!implDef.trait().ent.isValid()) {
                    BUG(i->span, "Encountered negative impl with no trait");
                }
                ResolveAbsolutePath(itemContext, implDef.trait().sp, Context::LookupMode::Type, implDef.trait().ent);
                itemContext.iblTargetGenerics = nullptr;

                ResolveAbsoluteGeneric(itemContext, implDef.params());

                // No items

                itemContext.pop(implDef.params());
                itemContext.popSelf(implDef.type());
                break;
            }
            case ASTItem::TAG_Module: {
                auto& e = i->data.as_Module();
                DEBUG("Module - " << i->name);
                ResolveAbsoluteMod(itemContext.settings, itemContext.crate, e);
                break;
            }
            case ASTItem::TAG_Crate: {
                // - Nothing
                break;
            }
            case ASTItem::TAG_Enum: {
                auto& e = i->data.as_Enum();
                DEBUG("Enum - " << i->name);
                ResolveAbsoluteEnum(itemContext, e);
                break;
            }
            case ASTItem::TAG_Trait: {
                auto& e = i->data.as_Trait();
                DEBUG("Trait - " << i->name);
                ResolveAbsoluteTrait(itemContext, e);
                break;
            }
            case ASTItem::TAG_TraitAlias: {
                auto& e = i->data.as_TraitAlias();
                DEBUG("TraitAlias - " << i->name);
                itemContext.push(e.params, GenericSlot::Level::Top, true);
                ResolveAbsoluteGeneric(itemContext, e.params);

                for (auto& lft : e.lifetimes) {
                    ResolveAbsoluteLifetime(itemContext, lft.sp, lft.ent);
                }
                for (auto& st : e.traits) {
                    itemContext.push(st.ent.hrbs);
                    ResolveAbsolutePath(itemContext, st.sp, Context::LookupMode::Type, *st.ent.path);
                    itemContext.pop(st.ent.hrbs);
                }

                itemContext.pop(e.params, true);
                break;
            }
            case ASTItem::TAG_Type: {
                auto& e = i->data.as_Type();
                DEBUG("Type - " << i->name);
                itemContext.push(e.params(), GenericSlot::Level::Top, true);
                ResolveAbsoluteGeneric(itemContext, e.params());

                ResolveAbsoluteType(itemContext, e.type());

                itemContext.pop(e.params(), true);
                break;
            }
            case ASTItem::TAG_Struct: {
                auto& e = i->data.as_Struct();
                DEBUG("Struct - " << i->name);
                ResolveAbsoluteStruct(itemContext, e);
                break;
            }
            case ASTItem::TAG_Union: {
                auto& e = i->data.as_Union();
                DEBUG("Union - " << i->name);
                ResolveAbsoluteUnion(itemContext, e);
                break;
            }
            case ASTItem::TAG_Function: {
                auto& e = i->data.as_Function();
                DEBUG("Function - " << i->name);
                ResolveAbsoluteFunction(itemContext, e);
                break;
            }
            case ASTItem::TAG_Static: {
                auto& e = i->data.as_Static();
                DEBUG("Static - " << i->name);
                ResolveAbsoluteStatic(itemContext, e);
                break;
            }
        }
    }

    // - Run through the indexed items and fix up those paths
    static Span sp;
    DEBUG("Imports (mod = " << mod.path() << ")");
    for (auto& i : mod.namespaceItems) {
        if (i.second.isImport) {
            ResolveAbsolutePath(itemContext, sp, Context::LookupMode::Namespace, i.second.path);
        }
    }
    for (auto& i : mod.typeItems) {
        if (i.second.isImport) {
            ResolveAbsolutePath(itemContext, sp, Context::LookupMode::Type, i.second.path);
        }
    }
    for (auto& i : mod.valueItems) {
        if (i.second.isImport) {
            ResolveAbsolutePath(itemContext, sp, Context::LookupMode::Constant, i.second.path);
        }
    }
}

void ResolveAbsolutise(const WireBoard& wb, ASTCrate& crate) {
    ResolveAbsoluteMod(*wb.settings, crate, crate.rootModule());
}

#undef FLAG_CONST_GENERIC

enum class IndexName {
    Namespace,
    Type,
    Value,
    Macro,
};

void ResolveIndexModuleWildcardUseStmt(ASTCrate& crate, ASTModule& dstMod, const ASTUseItem::Ent& iData, const ASTVisibility& vis, bool fromPrelude);

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

::std::unordered_map<RcString, ASTModule::IndexEnt>& getModIndex(ASTModule& mod, IndexName location) {
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
    ASTPath hirToAst(const HIRSimplePath& p) {
        // The crate name here has to be non-empty, because it's external.
        assert(p.crateName() != "");
        ASTPath rv(p.crateName(), {});
        rv.nodes().reserve(p.components().size());
        for (const auto& n : p.components()) {
            rv.nodes().push_back(ASTPathNode(n));
        }
        return rv;
    }
} // namespace

void _add_item(const Span& sp, ASTModule& mod, IndexName location, const RcString& name, const ASTVisibility& vis, ASTPath ir, bool errorOnCollision = true, bool fromPrelude = false, bool fromGlob = false) {
    ASSERT_BUG(sp, ir.bindings.hasBinding(), "Adding item with no binding - " << ir);
    auto& list = getModIndex(mod, location);

    if (location != IndexName::Namespace) {
        ASSERT_BUG(sp, ir.cls.as_Absolute().nodes.size() > 0, "Non-namespace path must have nodes - " << location << " " << name << " = " << ir);
    }

    // Add traits to a separate list
    if (ir.bindings.type.binding.is_Trait()) {
        auto it = std::find(mod.traits.begin(), mod.traits.end(), ir.bindings.type.path);
        if (it == mod.traits.end()) {
            mod.traits.push_back(ir.bindings.type.path);
        }
    }

    bool wasImport = (ir != mod.path() + name);
    if (list.count(name) > 0) {
        auto& e = list.at(name);
        if (e.path == ir) {
            // Ignore, re-import of the same thing
            e.fromPrelude = e.fromPrelude && fromPrelude;

            // Update the visibility, if this new visibility adds anything
            if (!e.vis.contains(vis)) {
                e.vis.inplaceUnion(vis);
                DEBUG("### Import " << location << " item " << mod.path() << " :: " << name << " = " << ir << " (update to " << e.vis << ")");
            }
        } else if (errorOnCollision) {
            ERROR(sp, E0000, "Duplicate definition of name '" << name << "' in " << location << " scope (" << mod.path() << ") " << ir << ", and " << e.path);
        } else {
            // An item written here, or named by a `use`, shadows a glob; two
            // globs shadow nothing, and the name is ambiguous where it is used.
            // The prelude is not one of the two: it is what a module falls back
            // to, and anything brought in another way shadows it.
            if (fromGlob && e.fromGlob && !fromPrelude && !e.fromPrelude) {
                DEBUG(location << " name ambiguity - '" << name << "' = " << ir << " and " << e.path << " (mod=" << mod.path() << ")");
                e.ambiguous = true;
            } else {
                DEBUG(location << " name collision - '" << name << "' = " << ir << ", ignored (mod=" << mod.path() << ", was " << e.path << ")");
            }
        }
    } else {
        DEBUG("### " << (wasImport ? "Import" : "Add") << " " << location << " item " << mod.path() << " :: " << name << " = " << ir << vis);
        auto rec = list.insert(::std::make_pair(name, ASTModule::IndexEnt{wasImport, fromPrelude, mv$(vis), mv$(ir), fromGlob, false}));
        assert(rec.second);
    }
}

void _add_item_type(const Span& sp, ASTModule& mod, const RcString& name, const ASTVisibility& vis, ASTPath ir, bool errorOnCollision = true, bool fromPrelude = false, bool fromGlob = false) {
    _add_item(sp, mod, IndexName::Namespace, name, vis, ASTPath(ir), errorOnCollision, fromPrelude, fromGlob);
    _add_item(sp, mod, IndexName::Type, name, vis, ::std::move(ir), errorOnCollision, fromPrelude, fromGlob);
}

void _add_item_value(const Span& sp, ASTModule& mod, const RcString& name, const ASTVisibility& vis, ASTPath ir, bool errorOnCollision = true, bool fromPrelude = false, bool fromGlob = false) {
    _add_item(sp, mod, IndexName::Value, name, vis, mv$(ir), errorOnCollision, fromPrelude, fromGlob);
}

void ResolveIndexModuleBase(const ASTCrate& crate, ASTModule& mod) {
    TRACE_FUNCTION_F("mod = " << mod.path());
    for (const auto& i : mod.items) {
        auto ap = mod.path() + i->name;
        auto p = ASTPath(ap);

        switch (i->data.tag()) {
            case ASTItem::TAG_None: {
                break;
            }
            case ASTItem::TAG_MacroInv: {
                break;
            }
            case ASTItem::TAG_ExternBlock: {
                break;
            }
            case ASTItem::TAG_Impl: {
                break;
            }
            case ASTItem::TAG_NegImpl: {
                break;
            }
            case ASTItem::TAG_GlobalAsm: {
                break;
            }
            case ASTItem::TAG_Macro: {
                // Handled by `for(const auto& item : mod.macros())` below
                break;
            }
            case ASTItem::TAG_Use: {
                // Skip for now
                break;
            }
            case ASTItem::TAG_Module: {
                auto& e = i->data.as_Module();
                p.bindings.type.set(ap, ASTPathBindingType::make_Module({&e}));
                _add_item(i->span, mod, IndexName::Namespace, i->name, i->vis, mv$(p));
                break;
            }
            case ASTItem::TAG_Crate: {
                auto& e = i->data.as_Crate();
                if (e.name != "") {
                    ASSERT_BUG(i->span, crate.externCrates.count(e.name) > 0, "Referenced crate '" << e.name << "' isn't loaded for `extern crate`");
                    p.bindings.type.set(ap, ASTPathBindingType::make_Crate({&crate.externCrates.at(e.name)}));
                } else {
                    p.bindings.type.set(ap, ASTPathBindingType::make_Module({&crate.rootModule_}));
                }
                _add_item(i->span, mod, IndexName::Namespace, i->name, i->vis, mv$(p));
                break;
            }
            case ASTItem::TAG_Enum: {
                auto& e = i->data.as_Enum();
                p.bindings.type.set(ap, ASTPathBindingType::make_Enum({&e}));
                _add_item_type(i->span, mod, i->name, i->vis, mv$(p));
                break;
            }
            case ASTItem::TAG_Union: {
                auto& e = i->data.as_Union();
                p.bindings.type.set(ap, ASTPathBindingType::make_Union({&e}));
                _add_item_type(i->span, mod, i->name, i->vis, mv$(p));
                break;
            }
            case ASTItem::TAG_Trait: {
                auto& e = i->data.as_Trait();
                p.bindings.type.set(ap, ASTPathBindingType::make_Trait({&e}));
                _add_item_type(i->span, mod, i->name, i->vis, mv$(p));
                break;
            }
            case ASTItem::TAG_TraitAlias: {
                auto& e = i->data.as_TraitAlias();
                p.bindings.type.set(ap, ASTPathBindingType::make_TraitAlias({&e}));
                _add_item_type(i->span, mod, i->name, i->vis, mv$(p));
                break;
            }
            case ASTItem::TAG_Type: {
                auto& e = i->data.as_Type();
                p.bindings.type.set(ap, ASTPathBindingType::make_TypeAlias({&e}));
                _add_item_type(i->span, mod, i->name, i->vis, mv$(p));
                break;
            }
            case ASTItem::TAG_Struct: {
                auto& e = i->data.as_Struct();
                p.bindings.type.set(ap, ASTPathBindingType::make_Struct({&e}));
                // - If the struct is a tuple-like struct (or unit-like), it presents in the value namespace
                if (!e.data.is_Struct()) {
                    p.bindings.value.set(ap, ASTPathBindingValue::make_Struct({&e}));
                    _add_item_value(i->span, mod, i->name, i->vis, p);
                }
                _add_item_type(i->span, mod, i->name, i->vis, mv$(p));
                break;
            }
            case ASTItem::TAG_Function: {
                auto& e = i->data.as_Function();
                p.bindings.value.set(ap, ASTPathBindingValue::make_Function({&e}));
                _add_item_value(i->span, mod, i->name, i->vis, mv$(p));
                break;
            }
            case ASTItem::TAG_Static: {
                auto& e = i->data.as_Static();
                p.bindings.value.set(ap, ASTPathBindingValue::make_Static({&e}));
                _add_item_value(i->span, mod, i->name, i->vis, mv$(p));
                break;
            }
        }
    }

    for (const auto& item : mod.macros()) {
        ASTPath p = mod.path() + item.name;
        p.bindings.macro.set(mod.path() + item.name, ASTPathBindingMacro::make_MacroRules({nullptr, &*item.data}));
        // NOTE: Macros can be freely duplicated, BUT the last entry takes precedence (TODO)
        _add_item(item.span, mod, IndexName::Macro, item.name, item.vis, mv$(p), /*error_on_collision=*/false);
    }

    bool hasPubWildcard = false;
    // Named imports
    for (const auto& ip : mod.items) {
        const auto& i = *ip;
        if (!i.data.is_Use()) {
            continue;
        }
        for (const auto& iData : i.data.as_Use().entries) {
            if (iData.name != "") {
                DEBUG("Use " << iData.name << " = " << iData.path);
                // TODO: Ensure that the path is canonical?

                const auto& sp = iData.sp;
                ASSERT_BUG(sp, iData.path.bindings.hasBinding(), "`use " << iData.path << "` left unbound in module " << mod.path());
                const auto& pb = iData.path.bindings;

                bool allowCollide = true; // Allow collisions (`use` can import mutliple namespaces, local gets priority)
                // - Types
            switch (pb.type.binding.tag()) {
                case ASTPathBindingType::TAG_Unbound: {
                    DEBUG(iData.name << " - Not a type/module");
                    break;
                }
                case ASTPathBindingType::TAG_TypeParameter: {
                    BUG(sp, "Import was bound to type parameter");
                    break;
                }
                case ASTPathBindingType::TAG_Primitive: {
                    _add_item_type(sp, mod, iData.name, i.vis, pb.type, !allowCollide);
                    break;
                }
                case ASTPathBindingType::TAG_Crate: {
                    _add_item(sp, mod, IndexName::Namespace, iData.name, i.vis, pb.type, !allowCollide);
                    break;
                }
                case ASTPathBindingType::TAG_Module: {
                    _add_item(sp, mod, IndexName::Namespace, iData.name, i.vis, pb.type, !allowCollide);
                    break;
                }
                case ASTPathBindingType::TAG_Enum: {
                    _add_item_type(sp, mod, iData.name, i.vis, pb.type, !allowCollide);
                    break;
                }
                case ASTPathBindingType::TAG_Union: {
                    _add_item_type(sp, mod, iData.name, i.vis, pb.type, !allowCollide);
                    break;
                }
                case ASTPathBindingType::TAG_Trait: {
                    _add_item_type(sp, mod, iData.name, i.vis, pb.type, !allowCollide);
                    break;
                }
                case ASTPathBindingType::TAG_TraitAlias: {
                    _add_item_type(sp, mod, iData.name, i.vis, pb.type, !allowCollide);
                    break;
                }
                case ASTPathBindingType::TAG_TypeAlias: {
                    _add_item_type(sp, mod, iData.name, i.vis, pb.type, !allowCollide);
                    break;
                }
                case ASTPathBindingType::TAG_Struct: {
                    _add_item_type(sp, mod, iData.name, i.vis, pb.type, !allowCollide);
                    break;
                }
                case ASTPathBindingType::TAG_EnumVar: {
                    _add_item_type(sp, mod, iData.name, i.vis, pb.type, !allowCollide);
                    break;
                }
            }
            // - Values
            // `use m::foo::{self}` names the module, not the function beside it.
            switch (iData.isSelf ? ASTPathBindingValue::TAG_Unbound : pb.value.binding.tag()) {
                case ASTPathBindingValue::TAG_Unbound: {
                    DEBUG(iData.name << " - Not a value");
                    break;
                }
                case ASTPathBindingValue::TAG_Variable: {
                    BUG(sp, "Import was bound to a variable");
                    break;
                }
                case ASTPathBindingValue::TAG_Generic: {
                    BUG(sp, "Import was bound to a generic value");
                    break;
                }
                case ASTPathBindingValue::TAG_Struct: {
                    _add_item_value(sp, mod, iData.name, i.vis, pb.value, !allowCollide);
                    break;
                }
                case ASTPathBindingValue::TAG_EnumVar: {
                    _add_item_value(sp, mod, iData.name, i.vis, pb.value, !allowCollide);
                    break;
                }
                case ASTPathBindingValue::TAG_Static: {
                    _add_item_value(sp, mod, iData.name, i.vis, pb.value, !allowCollide);
                    break;
                }
                case ASTPathBindingValue::TAG_Function: {
                    _add_item_value(sp, mod, iData.name, i.vis, pb.value, !allowCollide);
                    break;
                }
            }
            // - Macros
            switch (pb.macro.binding.tag()) {
                case ASTPathBindingMacro::TAG_Unbound: {
                    DEBUG(iData.name << " - Not a macro");
                    break;
                }
                case ASTPathBindingMacro::TAG_MacroRules: {
                    _add_item(sp, mod, IndexName::Macro, iData.name, i.vis, pb.macro, !allowCollide);
                    break;
                }
                case ASTPathBindingMacro::TAG_ProcMacro: {
                    _add_item(sp, mod, IndexName::Macro, iData.name, i.vis, pb.macro, !allowCollide);
                    break;
                }
                case ASTPathBindingMacro::TAG_ProcMacroAttribute: {
                    TODO(sp, "ProcMacroAttribute import");
                    break;
                }
                case ASTPathBindingMacro::TAG_ProcMacroDerive: {
                    TODO(sp, "ProcMacroDerive import");
                    break;
                }
            }
            } else {
                if (i.vis.ty() != ASTVisibility::Ty::Private) {
                    hasPubWildcard = true;
                }
            }
        }
    }

    mod.indexPopulated = (hasPubWildcard ? 1 : 2);

    // Handle child modules
    for (auto& i : mod.items) {
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
    const ASTCrate& crate,
    ASTModule& dstMod,
    /*const AST::ExternCrate& hcrate,*/ const HIRModule& hmod,
    const ASTPath& path,
    const ASTVisibility& vis,
    ASTAbsolutePath modAp,
    bool fromPrelude
) {
    TRACE_FUNCTION_F(dstMod.path() << " <= " << modAp);
    for (const auto& it : hmod.modItems) {
        const auto& ve = *it.second;
        if (ve.publicity.isGlobal()) {
            const auto* vep = &ve.ent;

            ASTPathBinding<ASTPathBindingType> pb;
            if (vep->is_Import()) {
                const auto& spath = vep->as_Import().path;
                pb.path.crate = spath.crateName();
                pb.path.nodes = spath.componentsVec();

                ASSERT_BUG(sp, crate.externCrates.count(spath.crateName()) == 1, "Crate " << spath.crateName() << " is not loaded");
                const auto* hmod = &crate.externCrates.at(spath.crateName()).hir->rootModule;
                // Import of the crate root
                if (spath.components().size() == 0) {
                    pb.binding = ASTPathBindingType::make_Module({nullptr, {nullptr, hmod}});
                    _add_item(sp, dstMod, IndexName::Namespace, it.first, vis, ASTPath(pb), false, fromPrelude, /*from_glob=*/true);
                    continue;
                }
                for (unsigned int i = 0; i < spath.components().size() - 1; i++) {
                    const auto& hit = hmod->modItems.at(spath.components()[i]);
                    // Only support enums on the penultimate component
                    if (i == spath.components().size() - 2 && hit->ent.is_Enum()) {
                        pb.binding = ASTPathBindingType::make_EnumVar({nullptr, 0});
                        _add_item_type(sp, dstMod, it.first, vis, mv$(pb), false, fromPrelude, /*from_glob=*/true);
                        hmod = nullptr;
                        break;
                    }
                    ASSERT_BUG(sp, hit->ent.is_Module(), "Path component " << spath.components()[i] << " of " << spath << " is not a module, instead " << hit->ent.tagStr());
                    hmod = &hit->ent.as_Module();
                }
                if (!hmod) {
                    continue;
                }
                vep = &hmod->modItems.at(spath.components().back())->ent;
            } else {
                pb.path = modAp + it.first;
            }
            switch ((*vep).tag()) {
                case HIRTypeItem::TAG_Import: {
                    auto& e = (*vep).as_Import();
                    //throw "";
                    TODO(sp, "Get binding for HIR import? " << e.path);
                    break;
                }
                case HIRTypeItem::TAG_Module: {
                    auto& e = (*vep).as_Module();
                    pb.binding = ASTPathBindingType::make_Module({nullptr, {nullptr, &e}});
                    break;
                }
                case HIRTypeItem::TAG_Trait: {
                    auto& e = (*vep).as_Trait();
                    pb.binding = ASTPathBindingType::make_Trait({nullptr, &e});
                    break;
                }
                case HIRTypeItem::TAG_Struct: {
                    auto& e = (*vep).as_Struct();
                    pb.binding = ASTPathBindingType::make_Struct({nullptr, &e});
                    break;
                }
                case HIRTypeItem::TAG_TraitAlias: {
                    auto& e = (*vep).as_TraitAlias();
                    pb.binding = ASTPathBindingType::make_TraitAlias({nullptr, &e});
                    break;
                }
                case HIRTypeItem::TAG_Union: {
                    auto& e = (*vep).as_Union();
                    pb.binding = ASTPathBindingType::make_Union({nullptr, &e});
                    break;
                }
                case HIRTypeItem::TAG_Enum: {
                    pb.binding = ASTPathBindingType::make_Enum({nullptr});
                    break;
                }
                case HIRTypeItem::TAG_TypeAlias: {
                    pb.binding = ASTPathBindingType::make_TypeAlias({nullptr});
                    break;
                }
                case HIRTypeItem::TAG_ExternType: {
                    pb.binding = ASTPathBindingType::make_TypeAlias({nullptr});
                    break;
                }
            }
            _add_item_type( sp, dstMod, it.first, vis, mv$(pb), false, fromPrelude, /*from_glob=*/true );
        }
    }
    for (const auto& it : hmod.valueItems) {
        const auto& ve = *it.second;
        if (ve.publicity.isGlobal()) {
            const auto* vep = &ve.ent;

            ASTPathBinding<ASTPathBindingValue> pb;
            if (ve.ent.is_Import()) {
                const auto& spath = ve.ent.as_Import().path;
                pb.path.crate = spath.crateName();
                pb.path.nodes = spath.componentsVec();

                ASSERT_BUG(sp, crate.externCrates.count(spath.crateName()) == 1, "Crate " << spath.crateName() << " is not loaded");
                const auto* hmod = &crate.externCrates.at(spath.crateName()).hir->rootModule;
                for (unsigned int i = 0; i < spath.components().size() - 1; i++) {
                    const auto& hit = hmod->modItems.at(spath.components()[i]);
                    if (i == spath.components().size() - 2 && hit->ent.is_Enum()) {
                        auto idx = hit->ent.as_Enum().findVariant(spath.components().back());
                        ASSERT_BUG(sp, idx != SIZE_MAX, spath);
                        pb.binding = ASTPathBindingValue::make_EnumVar({nullptr, static_cast<unsigned>(idx)});
                        _add_item_value(sp, dstMod, it.first, vis, mv$(pb), false, fromPrelude, /*from_glob=*/true);
                        hmod = nullptr;
                        break;
                    }
                    ASSERT_BUG(sp, hit->ent.is_Module(), "Path component " << spath.components()[i] << " of " << spath << " is not a module, instead " << hit->ent.tagStr());
                    hmod = &hit->ent.as_Module();
                }
                if (!hmod) {
                    continue;
                }
                vep = &hmod->valueItems.at(spath.components().back())->ent;
            } else {
                pb.path = modAp + it.first;
            }
            assert(vep);
            switch ((*vep).tag()) {
                case HIRValueItem::TAG_Import: {
                    throw "";
                }
                case HIRValueItem::TAG_Constant: {
                    pb.binding = ASTPathBindingValue::make_Static({nullptr});
                    break;
                }
                case HIRValueItem::TAG_Static: {
                    pb.binding = ASTPathBindingValue::make_Static({nullptr});
                    break;
                }
                case HIRValueItem::TAG_StructConstant: {
                    auto& e = (*vep).as_StructConstant();
                    pb.binding = ASTPathBindingValue::make_Struct({nullptr, &crate.externCrates.at(e.ty.crateName()).hir->getTypeitemByPath(sp, e.ty, true).as_Struct()});
                    break;
                }
                case HIRValueItem::TAG_StructConstructor: {
                    auto& e = (*vep).as_StructConstructor();
                    pb.binding = ASTPathBindingValue::make_Struct({nullptr, &crate.externCrates.at(e.ty.crateName()).hir->getTypeitemByPath(sp, e.ty, true).as_Struct()});
                    break;
                }
                case HIRValueItem::TAG_Function: {
                    pb.binding = ASTPathBindingValue::make_Function({nullptr});
                    break;
                }
            }
            _add_item_value( sp, dstMod, it.first, vis, mv$(pb), false, fromPrelude, /*from_glob=*/true );
        }
    }
    for (const auto& it : hmod.macroItems) {
        const auto& e = *it.second;
        if (e.publicity.isGlobal()) {
            ASTPathBinding<ASTPathBindingMacro> pb;
            if (const auto* ep = e.ent.opt_Import()) {
                pb.path.crate = ep->path.crateName();
                pb.path.nodes = ep->path.componentsVec();
                // NOTE: This shouldn't ever be pointing at an import, and no other handling needed
            } else {
                pb.path = modAp + it.first;
            }

            switch (e.ent.tag()) {
                case HIRMacroItem::TAG_Import: {
                    auto& _ = e.ent.as_Import();
                    pb.binding = ASTPathBindingMacro::make_MacroRules({nullptr, nullptr});
                    break;
                }
                case HIRMacroItem::TAG_ProcMacro: {
                    auto& me = e.ent.as_ProcMacro();
                    pb.binding = ASTPathBindingMacro::make_ProcMacro({nullptr, me.name});
                    break;
                }
                case HIRMacroItem::TAG_MacroRules: {
                    auto& me = e.ent.as_MacroRules();
                    pb.binding = ASTPathBindingMacro::make_MacroRules({nullptr, &*me});
                    break;
                }
            }
            _add_item(sp, dstMod, IndexName::Macro, it.first, vis, mv$(pb), false, fromPrelude, /*from_glob=*/true );
        }
    }
}

void ResolveIndexModuleWildcardSubmod(ASTCrate& crate, ASTModule& dstMod, const ASTModule& srcMod, const ASTVisibility& dstVis, bool fromPrelude) {
    static Span sp;
    TRACE_FUNCTION_F(dstMod.path() << " <= " << srcMod.path());
    static ::std::set<const ASTModule*> stack;
    if (!stack.insert(&srcMod).second) {
        DEBUG("- Avoided recursion");
        return;
    }

    for (const auto& vi : srcMod.namespaceItems) {
        if (!vi.second.fromPrelude && vi.second.vis.isVisible(dstMod.path() /*, src_mod.path()*/)) {
            _add_item(sp, dstMod, IndexName::Namespace, vi.first, dstVis, vi.second.path, false, fromPrelude, /*from_glob=*/true);
        }
    }
    for (const auto& vi : srcMod.typeItems) {
        if (!vi.second.fromPrelude && vi.second.vis.isVisible(dstMod.path() /*, src_mod.path()*/)) {
            _add_item(sp, dstMod, IndexName::Type, vi.first, dstVis, vi.second.path, false, fromPrelude, /*from_glob=*/true);
        }
    }
    for (const auto& vi : srcMod.valueItems) {
        if (!vi.second.fromPrelude && vi.second.vis.isVisible(dstMod.path() /*, src_mod.path()*/)) {
            _add_item(sp, dstMod, IndexName::Value, vi.first, dstVis, vi.second.path, false, fromPrelude, /*from_glob=*/true);
        }
    }
    for (const auto& vi : srcMod.macroItems) {
        if (!vi.second.fromPrelude && vi.second.vis.isVisible(dstMod.path() /*, src_mod.path()*/)) {
            _add_item(sp, dstMod, IndexName::Macro, vi.first, dstVis, vi.second.path, false, fromPrelude, /*from_glob=*/true);
        }
    }

    if (srcMod.indexPopulated != 2) {
        for (const auto& i : srcMod.items) {
            if (!i->data.is_Use()) {
                continue;
            }
            if (i->data.as_Use().isPrelude) {
                continue;
            }
            if (!i->vis.isVisible(dstMod.path() /*, src_mod.path()*/)) {
                continue;
            }
            for (const auto& e : i->data.as_Use().entries) {
                if (e.name != "") {
                    continue;
                }
                ResolveIndexModuleWildcardUseStmt(crate, dstMod, e, dstVis, fromPrelude);
            }
        }
    }

    stack.erase(&srcMod);
}

void ResolveIndexModuleWildcardUseStmt(ASTCrate& crate, ASTModule& dstMod, const ASTUseItem::Ent& iData, const ASTVisibility& vis, bool fromPrelude) {
    const auto& sp = iData.sp;
    const auto& b = iData.path.bindings.type;

    if (const auto* e = b.binding.opt_Crate()) {
        DEBUG("Glob crate " << iData.path);
        const auto& hmod = e->crate_->hir->rootModule;
        ResolveIndexModuleWildcardGlobInHirMod(sp, crate, dstMod, hmod, iData.path, vis, b.path, fromPrelude);
    } else if (const auto* e = b.binding.opt_Module()) {
        DEBUG("Glob mod " << iData.path);
        if (!e->module_) {
            ASSERT_BUG(sp, e->hir.mod, "Glob import where HIR module pointer not set - " << iData.path);
            const auto& hmod = *e->hir.mod;
            ResolveIndexModuleWildcardGlobInHirMod(sp, crate, dstMod, hmod, iData.path, vis, b.path, fromPrelude);
        } else {
            ResolveIndexModuleWildcardSubmod(crate, dstMod, *e->module_, vis, fromPrelude);
        }
    } else if (const auto* ep = b.binding.opt_Enum()) {
        const auto& e = *ep;
        ASSERT_BUG(sp, e.enum_ || e.hir, "Glob import but enum pointer not set - " << iData.path);
        if (e.enum_) {
            DEBUG("Glob enum " << iData.path << " (AST)");
            unsigned int idx = 0;
            for (const auto& ev : e.enum_->variants()) {
                if (ev.data.is_Struct()) {
                    ASTPathBinding<ASTPathBindingType> pb;
                    pb.path = b.path + ev.name;
                    pb.binding = ASTPathBindingType::make_EnumVar({e.enum_, idx});
                    _add_item_type(sp, dstMod, ev.name, vis, mv$(pb), false, fromPrelude, /*from_glob=*/true);
                } else {
                    ASTPathBinding<ASTPathBindingValue> pb;
                    pb.path = b.path + ev.name;
                    pb.binding = ASTPathBindingValue::make_EnumVar({e.enum_, idx});
                    _add_item_value(sp, dstMod, ev.name, vis, mv$(pb), false, fromPrelude, /*from_glob=*/true);
                }

                idx += 1;
            }
        } else {
            DEBUG("Glob enum " << iData.path << " (HIR)");
            unsigned int idx = 0;
            if (e.hir->data.is_Value()) {
                const auto* de = e.hir->data.opt_Value();
                for (const auto& ev : de->variants) {
                    ASTPathBinding<ASTPathBindingValue> pb;
                    pb.path = b.path + ev.name;
                    pb.binding = ASTPathBindingValue::make_EnumVar({nullptr, idx, e.hir});
                    _add_item_value(sp, dstMod, ev.name, vis, mv$(pb), false, fromPrelude, /*from_glob=*/true);

                    idx += 1;
                }
            } else {
                const auto* de = &e.hir->data.as_Data();
                for (const auto& ev : *de) {
                    if (ev.isStruct) {
                        ASTPathBinding<ASTPathBindingType> pb;
                        pb.path = b.path + ev.name;
                        pb.binding = ASTPathBindingType::make_EnumVar({nullptr, idx, e.hir});
                        _add_item_type(sp, dstMod, ev.name, vis, mv$(pb), false, fromPrelude, /*from_glob=*/true);
                    } else {
                        ASTPathBinding<ASTPathBindingValue> pb;
                        pb.path = b.path + ev.name;
                        pb.binding = ASTPathBindingValue::make_EnumVar({nullptr, idx, e.hir});
                        _add_item_value(sp, dstMod, ev.name, vis, mv$(pb), false, fromPrelude, /*from_glob=*/true);
                    }

                    idx += 1;
                }
            }
        }
    } else if (const auto* tp = b.binding.opt_Trait()) {
        // `use A::*;` brings in the associated items of a trait
        // (`import_trait_associated_functions`). Each stands for the path
        // through the trait, which resolution turns into a UFCS path.
        DEBUG("Glob trait " << iData.path);
        auto addValue = [&](const RcString& name) {
            ASTPathBinding<ASTPathBindingValue> pb;
            pb.path = b.path + name;
            pb.binding = ASTPathBindingValue::make_Static({nullptr, nullptr});
            _add_item_value(sp, dstMod, name, vis, mv$(pb), false, fromPrelude, /*from_glob=*/true);
        };
        auto addType = [&](const RcString& name) {
            ASTPathBinding<ASTPathBindingType> pb;
            pb.path = b.path + name;
            pb.binding = ASTPathBindingType::make_TypeAlias({nullptr});
            _add_item_type(sp, dstMod, name, vis, mv$(pb), false, fromPrelude, /*from_glob=*/true);
        };
        if (tp->hir) {
            for (const auto& v : tp->hir->values) {
                addValue(v.first);
            }
            for (const auto& t : tp->hir->types) {
                addType(t.first);
            }
        } else {
            for (const auto& item : tp->trait_->items()) {
                if (item.data.is_Function() || item.data.is_Static()) {
                    addValue(item.name);
                } else if (item.data.is_Type()) {
                    addType(item.name);
                }
            }
        }
    } else {
        BUG(sp, "Invalid path binding for glob import: " << b.binding.tagStr() << " - " << iData.path);
    }
}

// Wildcard (aka glob) import resolution
// Strategy:
// - HIR just imports the items
// - Enums import all variants
// - AST modules: (See Resolve_Index_Module_Wildcard__submod)
//  - Clone index in (marked as publicity and weak)
//  - Recurse into globs
void ResolveIndexModuleWildcard(ASTCrate& crate, ASTModule& mod) {
    TRACE_FUNCTION_F("mod = " << mod.path());
    for (const auto& i : mod.items) {
        if (!i->data.is_Use()) {
            continue;
        }
        for (const auto& e : i->data.as_Use().entries) {
            if (e.name != "") {
                continue;
            }
            ResolveIndexModuleWildcardUseStmt(crate, mod, e, i->vis, i->data.as_Use().isPrelude);
        }
    }

    // Mark this as having all the items it ever will.
    mod.indexPopulated = 2;

    // Handle child modules
    for (auto& i : mod.items) {
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

void ResolveIndexModuleNormalisePathExt(const ASTCrate& crate, const Span& sp, ASTPath& path, IndexName loc, const ASTExternCrate& ext, unsigned int start) {
    auto& info = path.cls.as_Absolute();
    const HIRModule* hmod = &ext.hir->rootModule;

    // TODO: Mangle path into being absolute into the crate

    info.crate = ext.name;
    info.nodes.erase(info.nodes.begin(), info.nodes.begin() + start);

    if (info.nodes.empty()) {
        return;
    }

    for (unsigned int i = 0; i < info.nodes.size() - 1; i++) {
        auto it = hmod->modItems.find(info.nodes[i].name());
        if (it == hmod->modItems.end()) {
            ERROR(sp, E0000, "Couldn't find node " << i << " of path " << path);
        }
        const auto* itemPtr = &it->second->ent;
        if (itemPtr->is_Import()) {
            const auto& e = itemPtr->as_Import();
            const auto& ec = crate.externCrates.at(e.path.crateName());

            // TODO: Update the path (and update `i` while there)

            if (e.path.components().empty()) {
                hmod = &ec.hir->rootModule;
                continue;
            }
            itemPtr = &ec.hir->getTypeitemByPath(sp, e.path, /*ignore_crate_name=*/true);
        }
        switch ((*itemPtr).tag()) {
            case HIRTypeItem::TAG_Import: {
                auto& e = (*itemPtr).as_Import();
                BUG(sp, "Recursive import in " << path << " - " << it->second->ent.as_Import().path << " -> " << e.path);
                break;
            }
            case HIRTypeItem::TAG_Enum: {
                if (i != info.nodes.size() - 2) { BUG(sp, "Path " << path << " pointed to non-module in component " << i); }
                // Lazy, not checking
                return;
            }
            case HIRTypeItem::TAG_Trait: {
                // An associated item of a trait, which resolution handles where
                // the path is written out.
                if (i != info.nodes.size() - 2) { BUG(sp, "Path " << path << " pointed to non-module in component " << i); }
                return;
            }
            case HIRTypeItem::TAG_Module: {
                auto& e = (*itemPtr).as_Module();
                hmod = &e;
                break;
            }
            default: {
                BUG(sp, "Path " << path << " pointed to non-module in component " << i);
                break;
            }
        }
    }
    const auto& lastnode = info.nodes.back();

    switch (loc) {
        case IndexName::Type:
        case IndexName::Namespace: {
            auto itM = hmod->modItems.find(lastnode.name());
            if (itM != hmod->modItems.end()) {
                if (itM->second->ent.is_Import()) {
                    auto& e = itM->second->ent.as_Import();
                    // Replace the path with this path (maintaining binding)
                    auto bindings = path.bindings.clone();
                    path = hirToAst(e.path);
                    path.bindings = mv$(bindings);
                }
                return;
            }
        } break;
        case IndexName::Value: {
            auto itV = hmod->valueItems.find(lastnode.name());
            if (itV != hmod->valueItems.end()) {
                if (itV->second->ent.is_Import()) {
                    auto& e = itV->second->ent.as_Import();
                    // Replace the path with this path (maintaining binding)
                    auto bindings = path.bindings.clone();
                    path = hirToAst(e.path);
                    path.bindings = mv$(bindings);
                }
                return;
            }
        } break;
        case IndexName::Macro: {
            auto itV = hmod->macroItems.find(lastnode.name());
            if (itV != hmod->macroItems.end()) {
                if (const auto* e = itV->second->ent.opt_Import()) {
                    // Replace the path with this path (maintaining binding)
                    auto bindings = path.bindings.clone();
                    path = hirToAst(e->path);
                    path.bindings = mv$(bindings);
                }
                return;
            }
        } break;
    }

    ERROR(sp, E0000, "Couldn't find final node of path " << path);
}

// Returns true if a change was made
bool ResolveIndexModuleNormalisePath(const ASTCrate& crate, const Span& sp, ASTPath& path, IndexName loc) {
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

    const ASTModule* mod = &crate.rootModule_;
    ASSERT_BUG(sp, info.nodes.size() > 0, "Empty node list in " << path);
    for (unsigned int i = 0; i < info.nodes.size() - 1; i++) {
        const auto& node = info.nodes[i];

        auto it = mod->namespaceItems.find(node.name());
        if (it == mod->namespaceItems.end()) {
            ERROR(sp, E0000, "Couldn't find node " << i << " of path " << path);
        }
        const auto& ie = it->second;

        if (ie.isImport) {
            // Need to replace all nodes up to and including the current with the import path
            auto newPath = ie.path;
            for (unsigned int j = i + 1; j < info.nodes.size(); j++) {
                newPath.nodes().push_back(mv$(info.nodes[j]));
            }
            newPath.bindings = path.bindings.clone();
            path = mv$(newPath);
            return ResolveIndexModuleNormalisePath(crate, sp, path, loc);
        } else {
            switch (ie.path.bindings.type.binding.tag()) {
default:
                BUG(sp, "Path " << path << " pointed to non-module " << ie.path);
                case ASTPathBindingType::TAG_Module: {
                    auto& e = ie.path.bindings.type.binding.as_Module();
                    mod = e.module_;
                    break;
                }
                case ASTPathBindingType::TAG_Crate: {
                    auto& e = ie.path.bindings.type.binding.as_Crate();
                    ResolveIndexModuleNormalisePathExt(crate, sp, path, loc, *e.crate_, i + 1);
                    return false;
                }
                case ASTPathBindingType::TAG_Enum: {
                    // NOTE: Just assuming that if an Enum is hit, it's sane
                    return false;
                }
                case ASTPathBindingType::TAG_Trait: {
                    // An associated item of a trait, which resolution handles
                    // where the path is written out.
                    return false;
                }
            }
        }
    }

    const auto& node = info.nodes.back();

    // TODO: Use get_mod_index instead.
    const ASTModule::IndexEnt* ieP = nullptr;
    switch (loc) {
        case IndexName::Namespace: {
            auto it = mod->namespaceItems.find(node.name());
            if (it != mod->namespaceItems.end()) {
                ieP = &it->second;
            }
        } break;
        case IndexName::Value: {
            auto it = mod->valueItems.find(node.name());
            if (it != mod->valueItems.end()) {
                ieP = &it->second;
            }
        } break;
        case IndexName::Type: {
            auto it = mod->typeItems.find(node.name());
            if (it != mod->typeItems.end()) {
                ieP = &it->second;
            }
        } break;
        case IndexName::Macro: {
            auto it = mod->macroItems.find(node.name());
            if (it != mod->macroItems.end()) {
                ieP = &it->second;
            } else {
                // Workaround for `use` on an exporter macro
                const ASTModule::MacroImport* found = nullptr;
                for (const auto& a : mod->macroImports) {
                    if (a.name == node.name()) {
                        found = &a;
                    }
                }
                if (found && found->ref.is_MacroRules()) {
                    DEBUG("in " << mod->path() << " " << node.name() << " imported using: " << path << " = " << found->path);
                    assert(path != found->path);
                    path = found->path;
                    path.bindings.macro.set(found->path, ASTPathBindingMacro::make_MacroRules({nullptr, found->ref.as_MacroRules()}));
                    DEBUG("macro_export? -> " << path);
                    ResolveIndexModuleNormalisePath(crate, sp, path, loc);
                    return true;
                }
            }
        } break;
    }
    if (!ieP) {
        DEBUG("Was in " << mod->path());
        ERROR(sp, E0000, "Couldn't find final node of path " << path);
    }
    const auto& ie = *ieP;

    if (ie.isImport) {
        // TODO: Prevent infinite recursion if the user does something dumb
        path = ASTPath(ie.path);
        ResolveIndexModuleNormalisePath(crate, sp, path, loc);
        return true;
    } else {
        // All good
        return false;
    }
}

void ResolveIndexModuleNormalise(const ASTCrate& crate, const Span& modSpan, ASTModule& mod) {
    TRACE_FUNCTION_F("mod = " << mod.path());
    for (auto& item : mod.items) {
        if (auto* e = item->data.opt_Module()) {
            ResolveIndexModuleNormalise(crate, item->span, *e);
        }
    }

    DEBUG("Index for " << mod.path());
    for (auto& ent : mod.namespaceItems) {
        ResolveIndexModuleNormalisePath(crate, modSpan, ent.second.path, IndexName::Namespace);
        DEBUG("NS " << ent.first << " = " << ent.second.path);
    }
    for (auto& ent : mod.typeItems) {
        ResolveIndexModuleNormalisePath(crate, modSpan, ent.second.path, IndexName::Type);
        DEBUG("Ty " << ent.first << " = " << ent.second.path);
    }
    for (auto& ent : mod.valueItems) {
        ResolveIndexModuleNormalisePath(crate, modSpan, ent.second.path, IndexName::Value);
        DEBUG("Val " << ent.first << " = " << ent.second.path);
    }
    for (auto& ent : mod.macroItems) {
        ResolveIndexModuleNormalisePath(crate, modSpan, ent.second.path, IndexName::Macro);
        DEBUG("Macro " << ent.first << " = " << ent.second.path);
    }
}

void ResolveIndexModuleExportedMacros(ASTCrate& crate, const Span& modSpan, ASTModule& mod) {
    TRACE_FUNCTION_F("mod = " << mod.path());

    if (&mod != &crate.rootModule_) {
        for (const auto& item : mod.macros()) {
            if (item.data->exported) {
                ASSERT_BUG(item.span, mod.macroItems.count(item.name), "Missing " << item.name << " in " << mod.path());
                _add_item(item.span, crate.rootModule_, IndexName::Macro, item.name, ASTVisibility::makeGlobal(), mod.macroItems.at(item.name).path);
            }
        }
    }

    for (auto& item : mod.items) {
        if (auto* e = item->data.opt_Module()) {
            ResolveIndexModuleExportedMacros(crate, item->span, *e);
        }
    }
}

void ResolveIndex(ASTCrate& crate) {
    // - Index all explicitly named items
    ResolveIndexModuleBase(crate, crate.rootModule_);
    // - Index wildcard imports
    ResolveIndexModuleWildcard(crate, crate.rootModule_);

    // Macros marked with `#[macro_export]` actually live in the root
    ResolveIndexModuleExportedMacros(crate, Span(), crate.rootModule_);

    // - Normalise the index (ensuring all paths point directly to the item)
    ResolveIndexModuleNormalise(crate, Span(), crate.rootModule_);
}

enum class Lookup {
    Any,    // Allow binding to anything
    AnyOpt, // Allow binding to anything, but don't error on failure
    Type,   // Allow only type bindings
    Value,  // Allow only value bindings
};

namespace {
    const RcString rcstringCrateBuiltins = RcString::newInterned(CRATE_BUILTINS);
}

void ResolveUseMod(const Settings& settings, const ASTCrate& crate, ASTModule& mod, ASTPath path, ::std::span<const ASTModule*> parentModules = {});
ASTPath::Bindings ResolveUseGetBinding(const Span& span, const Settings& settings, const ASTCrate& crate, const ASTAbsolutePath& sourceModPath, const ASTPath& path, ::std::span<const ASTModule*> parentModules, bool typesOnly = false, bool softFail = false);

ASTPath::Bindings ResolveUseGetBindingMod(const Span& span, const Settings& settings, const ASTCrate& crate, const ASTAbsolutePath& sourceModPath, const ASTModule& mod, const RcString& desItemName, ::std::span<const ASTModule*> parentModules, bool typesOnly = false, bool requireVisible = false);
ASTPath::Bindings ResolveUseGetBindingExt(const Span& span, const ASTCrate& crate, const ASTExternCrate& ec, const HIRModule& hmodr, const ASTPath& path, unsigned int start, ASTAbsolutePath ap = {});
ASTPath::Bindings ResolveUseGetBindingExt(const Span& span, const ASTCrate& crate, const ASTPath& path, const ASTExternCrate& ec, unsigned int start);

void ResolveUse(const WireBoard& wb, ASTCrate& crate) {
    ResolveUseMod(*wb.settings, crate, crate.rootModule_, ASTPath("", {}));
}

// - Convert self::/super:: paths into non-canonical absolute forms
ASTPath ResolveUseAbsolutisePath(const Span& span, const Settings& settings, const ASTCrate& crate, const ASTPath& basePath, ASTPath path) {
    switch (path.cls.tag()) {
        case ASTPathClass::TAG_Invalid: {
            // Should never happen
            BUG(span, "Invalid path class encountered");
            break;
        }
        case ASTPathClass::TAG_Local: {
            // Wait, how is this already known?
            BUG(span, "Local path class in use statement");
            break;
        }
        case ASTPathClass::TAG_UFCS: {
            // Wait, how is this already known?
            BUG(span, "UFCS path class in use statement");
            break;
        }
        case ASTPathClass::TAG_Relative: {
            auto& e = path.cls.as_Relative();
            // How can this happen?
            DEBUG("Relative " << path);

            // 2018 edition and later: all extern crates are implicitly in the namespace.
            // - Non-use paths use the extern prelude too, while use paths remain edition-sensitive.
            if (crate.edition >= ASTEdition::Rust2018) {
                const auto& name = e.nodes.at(0).name();
                auto ecIt = settings.implicitCrates.find(name);
                if (ecIt != settings.implicitCrates.end()) {
                    DEBUG("Found implict crate " << name);
                    e.nodes.erase(e.nodes.begin());
                    return ASTPath(ecIt->second, e.nodes);
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
                    ASTPath rv{rcstringCrateBuiltins, path.nodes()};
                    rv.bindings.type.set(ASTAbsolutePath(rcstringCrateBuiltins, {path.nodes().back().name()}), {});
                    return rv;
                }
            }

            // EVIL HACK: If the current module is an anon module, refer to the parent
            // TODO: Check if the desired item is in this module,
            if (basePath.nodes().size() > 0 && basePath.nodes().back().name().c_str()[0] == '#') {
                std::vector<const ASTModule*> parentMods;
                const ASTModule* curMod = &crate.rootModule_;
                parentMods.push_back(curMod);
                // Walk the path to create a list of parent modules
                // - Resets the list every time there's a non-anon module
                for (unsigned int i = 0; i < basePath.nodes().size(); i++) {
                    const auto& name = basePath.nodes()[i].name();
                    const ASTModule* nextMod = nullptr;

                    // If the desired item is an anon module (starts with #) then parse and index
                    if (name.size() > 0 && name.c_str()[0] == '#') {
                        unsigned int idx = 0;
                        if (::std::sscanf(name.c_str(), "#%u", &idx) != 1) {
                            BUG(span, "Invalid anon path segment '" << name << "'");
                        }
                        ASSERT_BUG(span, idx < curMod->anonMods().size(), "Invalid anon path segment '" << name << "'");
                        assert(curMod->anonMods()[idx]);
                        nextMod = &*curMod->anonMods()[idx];
                    } else {
                        for (const auto& item : curMod->items) {
                            if (item->name == name && item->data.is_Module()) {
                                nextMod = &item->data.as_Module();
                                break;
                            }
                        }
                        ASSERT_BUG(span, nextMod, "Could not find module '" << name << "' in " << curMod->path());
                    }
                    curMod = nextMod;
                    if (name.c_str()[0] != '#') {
                        parentMods.clear();
                    }
                    parentMods.push_back(curMod);
                }
                parentMods.pop_back();
                DEBUG("parent_mods.size() = " << parentMods.size());
                ASSERT_BUG(span, !parentMods.empty(), "Anon module with no named parent");
                const ASTModule* sourceMod = parentMods.front();

                for (;;) {
                    DEBUG("Module " << curMod->path());
                    if (ResolveUseGetBindingMod(span, settings, crate, sourceMod->path(), *curMod, e.nodes.front().name(), parentMods, /*types_only*/ e.nodes.size() > 1).hasBinding()) {
                        break;
                    }
                    if (parentMods.empty()) {
                        ERROR(span, E0000, "Unable to find " << e.nodes.front().name());
                    }
                    curMod = parentMods.back();
                    parentMods.pop_back();
                }
                DEBUG("Found item in " << curMod->path());

                ASTPath np("", {});
                for (unsigned int i = 0; i < curMod->path().nodes.size(); i++) {
                    np.nodes().push_back(curMod->path().nodes[i]);
                }
                np += path;
                return np;
            } else {
                return basePath + path;
            }
            break;
        }
        case ASTPathClass::TAG_Self: {
            DEBUG("Self " << path);
            // `self::super::..` leaves the module the same way a leading `super`
            // does; only the spelling puts the `super` after the `self`.
            {
                unsigned int superCount = 0;
                while (superCount < path.nodes().size() && path.nodes()[superCount].name() == "super") {
                    superCount++;
                }
                if (superCount > 0) {
                    ::std::vector<ASTPathNode> nodes(path.nodes().begin() + superCount, path.nodes().end());
                    auto inner = ASTPath::newSuper(superCount, mv$(nodes));
                    return ResolveUseAbsolutisePath(span, settings, crate, basePath, mv$(inner));
                }
            }
            // EVIL HACK: If the current module is an anon module, refer to the parent
            if (basePath.nodes().size() > 0 && basePath.nodes().back().name().c_str()[0] == '#') {
                ASTPath np("", {});
                for (unsigned int i = 0; i < basePath.nodes().size() - 1; i++) {
                    np.nodes().push_back(basePath.nodes()[i]);
                }
                np += path;
                return np;
            } else {
                return basePath + path;
            }
            break;
        }
        case ASTPathClass::TAG_Super: {
            auto& e = path.cls.as_Super();
            DEBUG("Super " << path);
            assert(e.count >= 1);
            ASTPath np("", {});
            if (e.count > basePath.nodes().size()) {
                ERROR(span, E0000, "Too many `super` components");
            }
            // TODO: Do this in a cleaner manner.
            unsigned int nAnon = 0;
            // Skip any anon modules in the way (i.e. if the current module is an anon, go to the parent)
            while (basePath.nodes().size() > nAnon && basePath.nodes()[basePath.nodes().size() - 1 - nAnon].name().c_str()[0] == '#') {
                nAnon++;
            }
            for (unsigned int i = 0; i < basePath.nodes().size() - e.count - nAnon; i++) {
                np.nodes().push_back(basePath.nodes()[i]);
            }
            np += path;
            return np;
        }
        case ASTPathClass::TAG_Absolute: {
            auto& e = path.cls.as_Absolute();
            DEBUG("Absolute " << path);
            // HACK: if the crate name starts with `=` it's a 2018 absolute path (references a crate loaded with `--extern`)
            if (crate.edition >= ASTEdition::Rust2018 && e.crate.c_str()[0] == '=') {
                // Absolute paths in 2018 edition are crate-prefixed?
                auto ecIt = settings.implicitCrates.find(e.crate.c_str() + 1);
                if (ecIt == settings.implicitCrates.end()) {
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

void ResolveUseMod(const Settings& settings, const ASTCrate& crate, ASTModule& mod, ASTPath path, ::std::span<const ASTModule*> parentModules) {
    TRACE_FUNCTION_F("path = " << path);

    for (auto& useStmt : mod.items) {
        if (!useStmt->data.is_Use()) {
            continue;
        }
        auto& useStmtData = useStmt->data.as_Use();

        const Span& span = useStmtData.sp;
        for (auto& useEnt : useStmtData.entries) {
            TRACE_FUNCTION_F(useEnt);

            useEnt.path = ResolveUseAbsolutisePath(span, settings, crate, path, useEnt.path);
            if (!useEnt.path.cls.is_Absolute()) {
                BUG(span, "Use path is not absolute after absolutisation");
            }

            // NOTE: Use statements can refer to _three_ different items
            // - types/modules ("type namespace")
            // - values ("value namespace")
            // - macros ("macro namespace")
            // TODO: Have Resolve_Use_GetBinding return the actual path
            useEnt.path.bindings = ResolveUseGetBinding(span, settings, crate, mod.path(), useEnt.path, parentModules);
            if (!useEnt.path.bindings.hasBinding()) {
                ERROR(span, E0000, "Unable to resolve `use` target " << useEnt.path);
            }
            DEBUG("'" << useEnt.name << "' = " << useEnt.path);

            // - If doing a glob, ensure the item type is valid
            if (useEnt.name == "") {
                switch (useEnt.path.bindings.type.binding.tag()) {
                    case ASTPathBindingType::TAG_Enum: {
                        break;
                    }
                    case ASTPathBindingType::TAG_Crate: {
                        break;
                    }
                    case ASTPathBindingType::TAG_Module: {
                        break;
                    }
                    case ASTPathBindingType::TAG_Trait: {
                        // `use A::*;` brings in the trait's associated items
                        // (`import_trait_associated_functions`).
                        break;
                    }
                    default: {
                        ERROR(span, E0000, "Wildcard import of invalid item type - " << useEnt.path);
                        break;
                    }
                }
            } else {
            }
        }
    }

    struct NV: public ASTNodeVisitorDef {
        const Settings& settings;
        const ASTCrate& crate;
        ::std::vector<const ASTModule*> parentModules;

        NV(const Settings& settings, const ASTCrate& crate, const ASTModule& curModule, ::std::span<const ASTModule*> parentModules)
            : settings(settings)
            , crate(crate)
            , parentModules(parentModules.begin(), parentModules.end())
        {
            this->parentModules.push_back(&curModule);
        }

        void visit(ASTExprNodeBlock& node) override {
            if (node.localMod) {
                ResolveUseMod(this->settings, this->crate, *node.localMod, node.localMod->path(), this->parentModules);

                parentModules.push_back(&*node.localMod);
            }
            ASTNodeVisitorDef::visit(node);
            if (node.localMod) {
                parentModules.pop_back();
            }
        }
    } exprIter(settings, crate, mod, parentModules);

    // TODO: Check that all code blocks are covered by these
    // - NOTE: Handle anon modules by iterating code (allowing correct item mappings)
    for (auto& ip : mod.items) {
        auto& i = *ip;
        switch (i.data.tag()) {
default:
            break;
            case ASTItem::TAG_Module: {
                auto& e = i.data.as_Module();
                ResolveUseMod(settings, crate, e, path + i.name);
                break;
            }
            case ASTItem::TAG_Impl: {
                auto& e = i.data.as_Impl();
                for (auto& i : e.items()) {
                    switch ((*i.data).tag()) {
                        case ASTItem::TAG_Function: {
                            auto& e = (*i.data).as_Function();
                            if (e.delegation() && e.delegation()->body.isValid()) { e.delegation()->body.node().visit(exprIter); }
                            if (e.code().isValid()) { e.code().node().visit(exprIter); }
                            break;
                        }
                        case ASTItem::TAG_Static: {
                            auto& e = (*i.data).as_Static();
                            if (e.value().isValid()) { e.value().node().visit(exprIter); }
                            break;
                        }
                        default: {
                            break;
                        }
                    }
                }
                break;
            }
            case ASTItem::TAG_Trait: {
                auto& e = i.data.as_Trait();
                for (auto& ti : e.items()) {
                    switch (ti.data.tag()) {
                        case ASTItem::TAG_None: {
                            // Deleted, ignore
                            break;
                        }
                        case ASTItem::TAG_MacroInv: {
                            // TODO: Should this already be deleted?
                            break;
                        }
                        case ASTItem::TAG_Type: {
                            break;
                        }
                        case ASTItem::TAG_Function: {
                            auto& e = ti.data.as_Function();
                            if (e.delegation() && e.delegation()->body.isValid()) { e.delegation()->body.node().visit(exprIter); }
                            if (e.code().isValid()) { e.code().node().visit(exprIter); }
                            break;
                        }
                        case ASTItem::TAG_Static: {
                            auto& e = ti.data.as_Static();
                            if (e.value().isValid()) { e.value().node().visit(exprIter); }
                            break;
                        }
                        default: {
                            BUG(Span(), "Unexpected item in trait - " << ti.data.tagStr());
                            break;
                        }
                    }
                }
                break;
            }
            case ASTItem::TAG_Function: {
                auto& e = i.data.as_Function();
                if (e.delegation() && e.delegation()->body.isValid()) {
                    e.delegation()->body.node().visit(exprIter);
                }
                if (e.code().isValid()) {
                    e.code().node().visit(exprIter);
                }
                break;
            }
            case ASTItem::TAG_Static: {
                auto& e = i.data.as_Static();
                if (e.value().isValid()) {
                    e.value().node().visit(exprIter);
                }
                break;
            }
            case ASTItem::TAG_Enum: {
                // A variant's discriminant is an expression like any other, and
                // may hold a module of its own.
                auto& e = i.data.as_Enum();
                for (auto& var : e.variants()) {
                    if (var.discriminantValue.isValid()) {
                        var.discriminantValue.node().visit(exprIter);
                    }
                }
                break;
            }
        }
    }
}

ASTPath::Bindings ResolveUseGetBindingMod(
    const Span& span,
    const Settings& settings,
    const ASTCrate& crate,
    const ASTAbsolutePath& sourceModPath,
    const ASTModule& mod,
    const RcString& desItemName,
    ::std::span<const ASTModule*> parentModules,
    bool typesOnly,     // = false
    bool requireVisible // = false
) {
    ASTPath::Bindings rv;
    TRACE_FUNCTION_F(mod.path() << ", des_item_name=" << desItemName);

    static ::std::vector<std::pair<const ASTModule*, const char*>> sRecurseStack;
    auto recurseEnt = std::make_pair(&mod, desItemName.c_str());
    // EVIL: Allow a single recursion before returning empty
    if (std::count(sRecurseStack.begin(), sRecurseStack.end(), recurseEnt) > 1) {
        DEBUG("Recursion detected, returning empty bindings");
        return rv;
    }
    auto _ = pushAndPopAtEnd(sRecurseStack, recurseEnt);

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
        rv.type.set(m.path(), ASTPathBindingType::make_Module({&m, {nullptr}}));
        return rv;
    }

    // Seach for the name defined in the module.
    for (const auto& ip : mod.items) {
        const auto& item = *ip;
        if (item.data.is_None()) {
            continue;
        }
        // When reached through a glob import, private items aren't re-exported (usvg's
        // crate-root `pub use parser::*` must not expose the private `parser::filter`).
        if (requireVisible && !item.vis.isVisible(sourceModPath)) {
            continue;
        }

        if (item.name == desItemName) {
            auto p = mod.path() + item.name;
            DEBUG("Matching item: " << item.data.tagStr());
            switch (item.data.tag()) {
                case ASTItem::TAG_None: {
                    // IMPOSSIBLE - Handled above
                    break;
                }
                case ASTItem::TAG_MacroInv: {
                    BUG(span, "Hit MacroInv in use resolution");
                    break;
                }
                case ASTItem::TAG_GlobalAsm: {
                    BUG(span, "Hit GlobalAsm in use resolution");
                    break;
                }
                case ASTItem::TAG_Macro: {
                    break;
                }
                case ASTItem::TAG_Use: {
                    break; // Skip for now
                    break;
                }
                case ASTItem::TAG_Impl: {
                    BUG(span, "Hit Impl in use resolution");
                    break;
                }
                case ASTItem::TAG_NegImpl: {
                    BUG(span, "Hit NegImpl in use resolution");
                    break;
                }
                case ASTItem::TAG_ExternBlock: {
                    BUG(span, "Hit Extern in use resolution");
                    break;
                }
                case ASTItem::TAG_Crate: {
                    auto& e = item.data.as_Crate();
                    if (!rv.type.is_Unbound()) {
                        // This is a hack for when a crate defines a module with the name `std` (or `core` with `#![no_std]`)
                        DEBUG("Ignore, already bound");
                    } else if (e.name != "") {
                        ASSERT_BUG(span, crate.externCrates.count(e.name), "Crate '" << e.name << "' not loaded");
                        rv.type.set(ASTAbsolutePath(e.name, {}), ASTPathBindingType::make_Crate({&crate.externCrates.at(e.name)}));
                    } else {
                        rv.type.set(ASTAbsolutePath(e.name, {}), ASTPathBindingType::make_Module({&crate.rootModule_}));
                    }
                    break;
                }
                case ASTItem::TAG_Type: {
                    auto& e = item.data.as_Type();
                    rv.type.set(p, ASTPathBindingType::make_TypeAlias({&e}));
                    break;
                }
                case ASTItem::TAG_Trait: {
                    auto& e = item.data.as_Trait();
                    rv.type.set(p, ASTPathBindingType::make_Trait({&e}));
                    break;
                }
                case ASTItem::TAG_TraitAlias: {
                    auto& e = item.data.as_TraitAlias();
                    rv.type.set(p, ASTPathBindingType::make_TraitAlias({&e}));
                    break;
                }
                case ASTItem::TAG_Function: {
                    auto& e = item.data.as_Function();
                    rv.value.set(p, ASTPathBindingValue::make_Function({&e}));
                    break;
                }
                case ASTItem::TAG_Static: {
                    auto& e = item.data.as_Static();
                    rv.value.set(p, ASTPathBindingValue::make_Static({&e}));
                    break;
                }
                case ASTItem::TAG_Struct: {
                    auto& e = item.data.as_Struct();
                    // TODO: What happens with name collisions?
                    if (!e.data.is_Struct()) {
                        rv.value.set(p, ASTPathBindingValue::make_Struct({&e}));
                    }
                    rv.type.set(p, ASTPathBindingType::make_Struct({&e}));
                    break;
                }
                case ASTItem::TAG_Enum: {
                    auto& e = item.data.as_Enum();
                    rv.type.set(p, ASTPathBindingType::make_Enum({&e}));
                    break;
                }
                case ASTItem::TAG_Union: {
                    auto& e = item.data.as_Union();
                    rv.type.set(p, ASTPathBindingType::make_Union({&e}));
                    break;
                }
                case ASTItem::TAG_Module: {
                    auto& e = item.data.as_Module();
                    rv.type.set(p, ASTPathBindingType::make_Module({&e}));
                    break;
                }
            }
        }
    }
    for (const auto& mac : mod.macros()) {
        if (mac.name == desItemName) {
            rv.macro.set(mod.path() + mac.name, ASTPathBindingMacro::make_MacroRules({nullptr, &*mac.data}));
            DEBUG("Macro definition: " << rv.macro.path);
            break;
        }
    }
    // NOTE: `use macroname;` can refer to a macro already in-scope via a parent `#[macro_use]`
    // - So, need to look upwards
    // - Can't use `parent_modules`, as that's only for anon.
    if (rv.macro.is_Unbound()) {
        ::std::vector<const ASTModule*> mods;
        mods.push_back(&crate.rootModule());
        for (size_t i = 0; i < mod.path().nodes.size(); i++) {
            const auto& n = mod.path().nodes[i];
            const ASTModule* nm = nullptr;
            if (n.c_str()[0] == '#') {
                // Lazy option: just enumerate and check, instead of parsing the index
                for (const auto& e : mods.back()->anonMods()) {
                    if (e && e->path().nodes.back() == n) {
                        nm = &*e;
                        break;
                    }
                }
            } else {
                for (const auto& e : mods.back()->items) {
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
                    switch (mac.ref.tag()) {
                        case MacroRef::TAG_None: {
                            break;
                        }
                        case MacroRef::TAG_MacroRules: {
                            auto& e = mac.ref.as_MacroRules();
                            rv.macro.set(mac.path, ASTPathBindingMacro::make_MacroRules({nullptr, e}));
                            break;
                        }
                        case MacroRef::TAG_BuiltinProcMacro: {
                            break;
                        }
                        case MacroRef::TAG_ExternalProcMacro: {
                            break;
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
    if (rv.macro.is_Unbound() && &mod == &crate.rootModule_) {
        auto it = crate.exportedMacros.find(desItemName);
        if (it != crate.exportedMacros.end()) {
            rv.macro.set(mod.path() + desItemName, ASTPathBindingMacro::make_MacroRules({nullptr, &*it->second}));
            DEBUG("Crate-exported macro - " << rv.macro.path);
        }
    }

    if (typesOnly && !rv.type.is_Unbound()) {
        return rv;
    }

    const bool canSeePrivate = false || mod.path().isParentOf(sourceModPath) || (parentModules.size() > 0 && parentModules[0]->path().isParentOf(sourceModPath));

    // Imports
    // - Explicitly named imports first (they take priority over anon imports)
    for (const auto& imp : mod.items) {
        if (!imp->data.is_Use()) {
            continue;
        }
        const auto& impData = imp->data.as_Use();
        // The prelude is in scope for everything lexically inside the module it
        // was added to, which includes the anonymous modules holding the items
        // of its function bodies: a `use format_args as f;` written in a block
        // reaches the same prelude the module around it does.
        if (impData.isPrelude && !mod.path().isParentOf(sourceModPath)) {
            continue;
        }
        for (const auto& impE : impData.entries) {
            const Span& sp2 = impE.sp;
            if (impE.name == desItemName) {
                DEBUG("- Named import " << impE.name << " = " << impE.path);
                if (!(canSeePrivate || imp->vis.isVisible(sourceModPath /*, mod.path()*/))) {
                    DEBUG("Ignore private import");
                    continue;
                }
                if (!impE.path.bindings.hasBinding()) {
                    DEBUG(" > Needs resolve p=" << &impE.path);
                    static ::std::vector<const ASTPath*> sMods;
                    if (::std::find(sMods.begin(), sMods.end(), &impE.path) == sMods.end()) {
                        sMods.push_back(&impE.path);
                        rv.mergeFrom(ResolveUseGetBinding(sp2, settings, crate, mod.path(), ResolveUseAbsolutisePath(sp2, settings, crate, mod.path(), impE.path), parentModules));
                        sMods.pop_back();
                    } else {
                        DEBUG("Recursion on path " << &impE.path << " " << impE.path);
                    }
                } else {
                    rv.mergeFrom(impE.path.bindings.clone());
                }
                continue;
            }
        }
    }

    for (const auto& imp : mod.items) {
        // A satisfied types-only lookup can stop before touching later globs; resolving them
        // here can hit the recursion limit and raise a spurious error (e.g. libc 0.2.189's
        // `new` module, where sibling globs re-export through an earlier glob import).
        if (typesOnly && !rv.type.is_Unbound()) {
            break;
        }
        if (!imp->data.is_Use()) {
            continue;
        }
        const auto& impData = imp->data.as_Use();
        // The prelude is in scope for everything lexically inside the module it
        // was added to, which includes the anonymous modules holding the items
        // of its function bodies: a `use format_args as f;` written in a block
        // reaches the same prelude the module around it does.
        if (impData.isPrelude && !mod.path().isParentOf(sourceModPath)) {
            continue;
        }
        for (const auto& impE : impData.entries) {
            const Span& sp2 = impE.sp;
            if (impE.name != "") {
                continue;
            }

            // TODO: Correct privacy rules (if the origin of this lookup can see this item)
            if ((canSeePrivate || imp->vis.isVisible(sourceModPath /*, mod.path()*/))) {
                DEBUG("- Search glob of " << impE.path << " in " << mod.path());
                // INEFFICIENT! Resolves and throws away the result (because we can't/shouldn't mutate here)
                ASTPath::Bindings bindings_;
                const auto* bindings = &impE.path.bindings;
                if (bindings->type.is_Unbound()) {
                    DEBUG("Temp resolving wildcard " << impE.path);
                    // Handle possibility of recursion
                    static ::std::vector<const ASTUseItem*> resolveStackPtrs;
                    if (::std::find(resolveStackPtrs.begin(), resolveStackPtrs.end(), &impData) == resolveStackPtrs.end()) {
                        resolveStackPtrs.push_back(&impData);
                        bindings_ = ResolveUseGetBinding(sp2, settings, crate, mod.path(), ResolveUseAbsolutisePath(sp2, settings, crate, mod.path(), impE.path), parentModules, /*type_only=*/true, /*soft_fail=*/true);
                        if (bindings_.type.is_Unbound()) {
                            DEBUG("Recursion detected, skipping " << impE.path);
                            resolveStackPtrs.pop_back();
                            continue;
                        }
                        // *waves hand* I'm not evil.
                        const_cast<ASTPath::Bindings&>(impE.path.bindings) = bindings_.clone();
                        bindings = &bindings_;
                        resolveStackPtrs.pop_back();
                    } else {
                        DEBUG("Recursion detected (resolve_stack_ptrs), skipping " << impE.path);
                        continue;
                    }
                } else {
                }

                switch (bindings->type.binding.tag()) {
                    case ASTPathBindingType::TAG_Crate: {
                        auto& e = bindings->type.binding.as_Crate();
                        assert(e.crate_);
                        rv.mergeFrom(ResolveUseGetBindingExt(sp2, crate, ASTPath("", {ASTPathNode(desItemName, {})}), *e.crate_, 0));
                        break;
                    }
                    case ASTPathBindingType::TAG_Module: {
                        auto& e = bindings->type.binding.as_Module();
                        if (e.module_) {
                            // Prevent infinite recursion - keyed by (module, name) so an
                            // in-flight search for a *different* name doesn't block this one
                            // (libc resolves `crate::linux` through `new::*` while a search
                            // inside `new` is still on the stack).
                            static ::std::vector<::std::pair<const ASTModule*, RcString>> sUseGlobModStack;
                            auto ent = ::std::make_pair(&*e.module_, desItemName);
                            if (::std::find(sUseGlobModStack.begin(), sUseGlobModStack.end(), ent) == sUseGlobModStack.end()) {
                                sUseGlobModStack.push_back(ent);
                                rv.mergeFrom(ResolveUseGetBindingMod(span, settings, crate, mod.path(), *e.module_, desItemName, {}, /*types_only=*/false, /*require_visible=*/true));
                                sUseGlobModStack.pop_back();
                            } else {
                                DEBUG("Recursion prevented of " << e.module_->path());
                            }
                        } else if (e.hir.mod) {
                            rv.mergeFrom(ResolveUseGetBindingExt(sp2, crate, *e.hir.crate, *e.hir.mod, ASTPath("", {ASTPathNode(desItemName, {})}), 0, bindings->type.path));
                        } else {
                            BUG(span, "NULL module for binding on glob of " << impE.path);
                        }
                        break;
                    }
                    case ASTPathBindingType::TAG_Trait: {
                        // `use A::*;` brings in the associated items of a trait
                        // (`import_trait_associated_functions`).
                        auto& e = bindings->type.binding.as_Trait();
                        assert(e.trait_ || e.hir);
                        bool isValue = false;
                        bool isType = false;
                        if (e.hir) {
                            isValue = e.hir->values.count(desItemName) != 0;
                            isType = e.hir->types.count(desItemName) != 0;
                        } else {
                            for (const auto& item : e.trait_->items()) {
                                if (item.name != desItemName) {
                                    continue;
                                }
                                isValue = item.data.is_Function() || item.data.is_Static();
                                isType = item.data.is_Type();
                                break;
                            }
                        }
                        if (isValue || isType) {
                            ASTPath::Bindings tmpRv;
                            if (isValue) {
                                tmpRv.value.set(bindings->type.path + desItemName, ASTPathBindingValue::make_Static({nullptr, nullptr}));
                            }
                            if (isType) {
                                tmpRv.type.set(bindings->type.path + desItemName, ASTPathBindingType::make_TypeAlias({nullptr}));
                            }
                            rv.mergeFrom(tmpRv);
                        }
                        break;
                    }
                    case ASTPathBindingType::TAG_Enum: {
                        auto& e = bindings->type.binding.as_Enum();
                        assert(e.enum_ || e.hir);
                        if (e.enum_) {
                            const auto& enm = *e.enum_;
                            unsigned int i = 0;
                            for (const auto& var : enm.variants()) {
                                if (var.name == desItemName) {
                                    ASTPath::Bindings tmpRv;
                                    if (var.data.is_Struct()) {
                                        tmpRv.type.set(bindings->type.path + desItemName, ASTPathBindingType::make_EnumVar({&enm, i}));
                                    } else {
                                        tmpRv.value.set(bindings->type.path + desItemName, ASTPathBindingValue::make_EnumVar({&enm, i}));
                                    }
                                    rv.mergeFrom(tmpRv);
                                    break;
                                }
                                i++;
                            }
                        } else {
                            const auto& enm = *e.hir;
                            auto idx = enm.findVariant(desItemName);
                            if (idx != SIZE_MAX) {
                                ASTPath::Bindings tmpRv;
                                if (enm.data.is_Data() && enm.data.as_Data()[idx].isStruct) {
                                    tmpRv.type.set(bindings->type.path + desItemName, ASTPathBindingType::make_EnumVar({nullptr, static_cast<unsigned>(idx), &enm}));
                                } else {
                                    tmpRv.value.set(bindings->type.path + desItemName, ASTPathBindingValue::make_EnumVar({nullptr, static_cast<unsigned>(idx), &enm}));
                                }
                                rv.mergeFrom(tmpRv);
                                break;
                            }
                        }
                        break;
                    }
break;
                    default:
                        BUG(sp2, "Wildcard import expanded to an invalid item class - " << bindings->type.binding.tagStr());
                        break;
                }
            }
        }
    }
    if (rv.hasBinding()) {
        return rv;
    }

    if (mod.path().nodes.size() > 0 && mod.path().nodes.back().c_str()[0] == '#') {
        ASSERT_BUG(span, parentModules.size() > 0, "Anon module with no parent modules - " << mod.path());
        return ResolveUseGetBindingMod(span, settings, crate, sourceModPath, *parentModules.back(), desItemName, parentModules.subspan(0, parentModules.size() - 1));
    } else {
        //if( allow == Lookup::Any )
        return ASTPath::Bindings();
    }
}

namespace {
    const HIRModule* getHirModByPath(const Span& sp, const ASTCrate& crate, const HIRSimplePath& path);

    const void* getHirModenumByPath(const Span& sp, const ASTCrate& crate, const HIRSimplePath& path, bool& is_enum) {
        const auto* hmod = &crate.externCrates.at(path.crateName()).hir->rootModule;
        for (const auto& node : path.components()) {
            auto it = hmod->modItems.find(node);
            if (it == hmod->modItems.end()) {
                BUG(sp, "");
            }
            if (it->second->ent.is_Module()) {
                auto& mod = it->second->ent.as_Module();
                hmod = &mod;
            }
            else if (it->second->ent.is_Import()) {
                auto& import = it->second->ent.as_Import();
                hmod = getHirModByPath(sp, crate, import.path); if (!hmod) BUG(sp, "Import in module position didn't resolve as a module - " << import.path);
            } else if (it->second->ent.is_Enum()) {
                auto& enm = it->second->ent.as_Enum();
                if (&node == &path.components().back()) {
                    is_enum = true;
                    return &enm;
                } BUG(sp, "");
            } else {
                if (&node == &path.components().back()) {
                    return nullptr;
                }
                BUG(sp, "");
            }
        }
        is_enum = false;
        return hmod;
    }

    const HIRModule* getHirModByPath(const Span& sp, const ASTCrate& crate, const HIRSimplePath& path) {
        bool is_enum = false;
        auto rv = getHirModenumByPath(sp, crate, path, is_enum);
        if (!rv) {
            return nullptr;
        }
        ASSERT_BUG(sp, !is_enum, "");
        return reinterpret_cast<const HIRModule*>(rv);
    }
}

ASTPath::Bindings ResolveUseGetBindingExt(const Span& span, const ASTCrate& crate, const ASTExternCrate& hcrate, const HIRModule& hmodr, const ASTPath& path, unsigned int start, ASTAbsolutePath ap) {
    if (ap.crate == "") {
        ap.crate = hcrate.name;
    }

    ASTPath::Bindings rv;
    TRACE_FUNCTION_F(path << " offset " << start << " [" << ap << "]");
    const auto& nodes = path.nodes();
    const HIRModule* hmod = &hmodr;

    //for(unsigned int i = start; i < nodes.size(); i ++)

    if (nodes.size() == start) {
        rv.type.set(ap, ASTPathBindingType::make_Module({nullptr, {&hcrate, hmod}}));
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
        DEBUG(i << " : " << nodes[i].name() << " = " << it->second->ent.tagStr());
        switch (it->second->ent.tag()) {
default:
            ERROR(span, E0000, "Unexpected item type in import " << path << " @ " << i << " - " << it->second->ent.tagStr());
            case HIRTypeItem::TAG_Import: {
                auto& e = it->second->ent.as_Import();
                // TODO: This is kinda like a duplicate of Resolve_Absolute_Path_BindAbsolute__hir_from ?
                bool is_enum = false;
                auto ptr = getHirModenumByPath(span, crate, e.path, is_enum);
                if (!ptr) {
                    // The import may name a trait, whose associated items can be
                    // imported (`import_trait_associated_functions`).
                    const auto& extCrate = *crate.externCrates.at(e.path.crateName()).hir;
                    const auto& ti = extCrate.getTypeitemByPath(span, e.path, /*ignore_crate*/ true, /*ignore_last*/ false);
                    if (const auto* tr = ti.opt_Trait()) {
                        i += 1;
                        if (i != nodes.size() - 1) {
                            ERROR(span, E0000, "Encountered trait at unexpected location in import");
                        }
                        const auto& name = nodes[i].name();
                        ap.crate = e.path.crateName();
                        ap.nodes = e.path.componentsVec();
                        ap.nodes.push_back(name);
                        const bool isValue = tr->values.count(name) != 0;
                        const bool isType = tr->types.count(name) != 0;
                        if (!isValue && !isType) {
                            ERROR(span, E0000, "Unable to find associated item " << name << " of trait in " << path);
                        }
                        if (isValue) {
                            rv.value.set(ap, ASTPathBindingValue::make_Static({nullptr, nullptr}));
                        }
                        if (isType) {
                            rv.type.set(ap, ASTPathBindingType::make_TypeAlias({nullptr}));
                        }
                        return rv;
                    }
                    BUG(span, "Path component " << nodes[i].name() << " pointed to non-module (" << path << ")");
                }
                if (is_enum) {
                    const auto& enm = *reinterpret_cast<const HIREnum*>(ptr);
                    i += 1;
                    if (i != nodes.size() - 1) {
                        ERROR(span, E0000, "Encountered enum at unexpected location in import");
                    }
                    const auto& name = nodes[i].name();

                    auto idx = enm.findVariant(name);
                    if (idx == SIZE_MAX) {
                        ERROR(span, E0000, "Unable to find variant " << path);
                    }
                    ap.crate = e.path.crateName();
                    ap.nodes = e.path.componentsVec();
                    ap.nodes.push_back(name);
                    if (enm.data.is_Data() && enm.data.as_Data()[idx].isStruct) {
                        rv.type.set(ap, ASTPathBindingType::make_EnumVar({nullptr, static_cast<unsigned int>(idx), &enm}));
                    } else {
                        rv.value.set(ap, ASTPathBindingValue::make_EnumVar({nullptr, static_cast<unsigned int>(idx), &enm}));
                    }
                    return rv;
                } else {
                    ap.crate = e.path.crateName();
                    ap.nodes = e.path.componentsVec();
                    hmod = reinterpret_cast<const HIRModule*>(ptr);
                }
                break;
            }
            case HIRTypeItem::TAG_Module: {
                auto& e = it->second->ent.as_Module();
                hmod = &e;
                break;
            }
            case HIRTypeItem::TAG_Trait: {
                // `use std::default::Default::default;` names an associated item
                // of a trait from another crate.
                auto& e = it->second->ent.as_Trait();
                i += 1;
                if (i != nodes.size() - 1) {
                    ERROR(span, E0000, "Encountered trait at unexpected location in import");
                }
                const auto& name = nodes[i].name();
                ap.nodes.push_back(name);

                const bool isValue = e.values.count(name) != 0;
                const bool isType = e.types.count(name) != 0;
                if (!isValue && !isType) {
                    ERROR(span, E0000, "Unable to find associated item " << name << " of trait in " << path);
                }
                if (isValue) {
                    rv.value.set(ap, ASTPathBindingValue::make_Static({nullptr, nullptr}));
                }
                if (isType) {
                    rv.type.set(ap, ASTPathBindingType::make_TypeAlias({nullptr}));
                }
                return rv;
            }
            case HIRTypeItem::TAG_Enum: {
                auto& e = it->second->ent.as_Enum();
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
                if (e.data.is_Data() && e.data.as_Data()[idx].isStruct) {
                    rv.type.set(ap, ASTPathBindingType::make_EnumVar({nullptr, static_cast<unsigned int>(idx), &e}));
                } else {
                    rv.value.set(ap, ASTPathBindingValue::make_EnumVar({nullptr, static_cast<unsigned int>(idx), &e}));
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
            DEBUG("E: : Types = " << FMT_CB(ss, for (const auto& e : hmod->modItems) { ss << e.first << ":" << e.second->ent.tagStr() << ","; }));
        } else if (!it->second->publicity.isGlobal()) {
            DEBUG("E : Mod " << nodes.back().name() << " = " << it->second->ent.tagStr() << " [private]");
        } else {
            const auto* itemPtr = &it->second->ent;
            auto ap2 = ap;
            auto ap = ap2;
            DEBUG("E : Mod " << nodes.back().name() << " = " << itemPtr->tagStr());
            if (itemPtr->is_Import()) {
                const auto& e = itemPtr->as_Import();
                ap = ASTAbsolutePath(e.path.crateName(), e.path.componentsVec());
                if (e.path.crateName() == rcstringCrateBuiltins) {
                    auto t = coretypeFromstring(e.path.components().front().c_str());
                    rv.type.set(ap, ASTPathBindingType::make_Primitive(t));
                } else {
                    ASSERT_BUG(span, crate.externCrates.count(e.path.crateName()) != 0, "Crate not loaded for " << e.path);
                    const auto& ec = crate.externCrates.at(e.path.crateName());
                    // This doesn't need to recurse - it can just do a single layer (as no Import should refer to another)
                    if (e.isVariant) {
                        const auto& enm = ec.hir->getTypeitemByPath(span, e.path, /*ignore_crate_name*/ true, /*ignore_last_node*/ true).as_Enum();
                        assert(e.idx < enm.numVariants());
                        rv.type.set(ap, ASTPathBindingType::make_EnumVar({nullptr, e.idx, &enm}));
                    } else if (e.path.components().empty()) {
                        rv.type.set(ap, ASTPathBindingType::make_Module({nullptr, {&ec, &ec.hir->rootModule}}));
                    } else {
                        itemPtr = &ec.hir->getTypeitemByPath(span, e.path, /*ignore_crate_name=*/true);
                    }
                }
            } else {
            }
            if (rv.type.is_Unbound()) {
                switch ((*itemPtr).tag()) {
                    case HIRTypeItem::TAG_Import: {
                        auto& e = (*itemPtr).as_Import();
                        BUG(span, "Recursive import in " << path << " - " << it->second->ent.as_Import().path << " -> " << e.path);
                        break;
                    }
                    case HIRTypeItem::TAG_Module: {
                        auto& e = (*itemPtr).as_Module();
                        rv.type.set(ap, ASTPathBindingType::make_Module({nullptr, {&hcrate, &e}}));
                        break;
                    }
                    case HIRTypeItem::TAG_TypeAlias: {
                        rv.type.set(ap, ASTPathBindingType::make_TypeAlias({nullptr}));
                        break;
                    }
                    case HIRTypeItem::TAG_ExternType: {
                        rv.type.set(ap, ASTPathBindingType::make_TypeAlias({nullptr})); // Lazy.
                        break;
                    }
                    case HIRTypeItem::TAG_Enum: {
                        auto& e = (*itemPtr).as_Enum();
                        rv.type.set(ap, ASTPathBindingType::make_Enum({nullptr, &e}));
                        break;
                    }
                    case HIRTypeItem::TAG_Struct: {
                        auto& e = (*itemPtr).as_Struct();
                        rv.type.set(ap, ASTPathBindingType::make_Struct({nullptr, &e}));
                        break;
                    }
                    case HIRTypeItem::TAG_Union: {
                        auto& e = (*itemPtr).as_Union();
                        rv.type.set(ap, ASTPathBindingType::make_Union({nullptr, &e}));
                        break;
                    }
                    case HIRTypeItem::TAG_Trait: {
                        auto& e = (*itemPtr).as_Trait();
                        rv.type.set(ap, ASTPathBindingType::make_Trait({nullptr, &e}));
                        break;
                    }
                    case HIRTypeItem::TAG_TraitAlias: {
                        auto& e = (*itemPtr).as_TraitAlias();
                        rv.type.set(ap, ASTPathBindingType::make_TraitAlias({nullptr, &e}));
                        break;
                    }
                }
            }
        }
    }
    // - Values
    {
        auto it = hmod->valueItems.find(nodes.back().name());
        if (it == hmod->valueItems.end()) {
            DEBUG("E : Values = " << FMT_CB(ss, for (const auto& e : hmod->valueItems) { ss << e.first << ":" << e.second->ent.tagStr() << ","; }));
        } else if (!it->second->publicity.isGlobal()) {
            DEBUG("E : Value " << nodes.back().name() << " = " << it->second->ent.tagStr() << " [private]");
        } else {
            const auto* itemPtr = &it->second->ent;
            auto ap2 = ap;
            auto ap = ap2;
            DEBUG("E : Value " << nodes.back().name() << " = " << itemPtr->tagStr());
            if (itemPtr->is_Import()) {
                const auto& e = itemPtr->as_Import();
                ap = ASTAbsolutePath(e.path.crateName(), e.path.componentsVec());
                // This doesn't need to recurse - it can just do a single layer (as no Import should refer to another)
                const auto& ec = crate.externCrates.at(e.path.crateName());
                if (e.isVariant) {
                    auto p = e.path;
                    p.popComponent();
                    const auto& enm = ec.hir->getTypeitemByPath(span, p, true).as_Enum();
                    assert(e.idx < enm.numVariants());
                    rv.value.set(ap, ASTPathBindingValue::make_EnumVar({nullptr, e.idx, &enm}));
                } else {
                    itemPtr = &ec.hir->getValitemByPath(span, e.path, true); // ignore_crate_name=true
                }
            }
            if (rv.value.is_Unbound()) {
                switch ((*itemPtr).tag()) {
                    case HIRValueItem::TAG_Import: {
                        auto& e = (*itemPtr).as_Import();
                        BUG(span, "Recursive import in " << path << " - " << it->second->ent.as_Import().path << " -> " << e.path);
                        break;
                    }
                    case HIRValueItem::TAG_Constant: {
                        rv.value.set(ap, ASTPathBindingValue::make_Static({nullptr}));
                        break;
                    }
                    case HIRValueItem::TAG_Static: {
                        rv.value.set(ap, ASTPathBindingValue::make_Static({nullptr}));
                        break;
                    }
                    case HIRValueItem::TAG_StructConstant: {
                        auto& e = (*itemPtr).as_StructConstant();
                        ASSERT_BUG(span, crate.externCrates.count(e.ty.crateName()), "Crate '" << e.ty.crateName() << "' not loaded for " << e.ty);
                        rv.value.set(ap, ASTPathBindingValue::make_Struct({nullptr, &crate.externCrates.at(e.ty.crateName()).hir->getTypeitemByPath(span, e.ty, true).as_Struct()}));
                        break;
                    }
                    case HIRValueItem::TAG_StructConstructor: {
                        auto& e = (*itemPtr).as_StructConstructor();
                        ASSERT_BUG(span, crate.externCrates.count(e.ty.crateName()), "Crate '" << e.ty.crateName() << "' not loaded for " << e.ty);
                        rv.value.set(ap, ASTPathBindingValue::make_Struct({nullptr, &crate.externCrates.at(e.ty.crateName()).hir->getTypeitemByPath(span, e.ty, true).as_Struct()}));
                        break;
                    }
                    case HIRValueItem::TAG_Function: {
                        rv.value.set(ap, ASTPathBindingValue::make_Function({nullptr}));
                        break;
                    }
                }
            }
        }
    }
    // - Macros
    {
        auto it = hmod->macroItems.find(nodes.back().name());
        if (it == hmod->macroItems.end()) {
            DEBUG("E : Macros = " << FMT_CB(ss, for (const auto& e : hmod->macroItems) { ss << e.first << ":" << e.second->ent.tagStr() << ","; }));
        } else if (!it->second->publicity.isGlobal()) {
            DEBUG("E : Macro " << nodes.back().name() << " = " << it->second->ent.tagStr() << " [private]");
        } else {
            const auto* itemPtr = &it->second->ent;
            auto ap2 = ap;
            auto ap = ap2;
            DEBUG("E : Macro " << nodes.back().name() << " = " << itemPtr->tagStr());

            if (const auto* imp = itemPtr->opt_Import()) {
                if (imp->path.crateName() == rcstringCrateBuiltins) {
                    rv.macro.set(ASTAbsolutePath(rcstringCrateBuiltins, {nodes.back().name()}), ASTPathBindingMacro::make_MacroRules({nullptr}));
                    return rv;
                }
                ASSERT_BUG(span, crate.externCrates.count(imp->path.crateName()) > 0, "Unable to find crate for " << imp->path);
                const auto& c = *crate.externCrates.at(imp->path.crateName()).hir; // Have to manually look up, AST doesn't have a `get_mod_by_path`
                const auto& mod = c.getModByPath(span, imp->path, /*ignore_last=*/true, /*ignore_crate=*/true);
                itemPtr = &mod.macroItems.at(imp->path.components().back())->ent;
                ap = ASTAbsolutePath(imp->path.crateName(), imp->path.componentsVec());
            } else {
            }

            if (rv.macro.is_Unbound()) {
                switch ((*itemPtr).tag()) {
                    case HIRMacroItem::TAG_Import: {
                        auto& e = (*itemPtr).as_Import();
                        if (e.path.crateName() == rcstringCrateBuiltins)
                            ;
                        else {
                            BUG(span, "Recursive import in " << path << " - " << it->second->ent.as_Import().path << " -> " << e.path);
                        }
                        rv.macro.set(ap, ASTPathBindingMacro::make_MacroRules({nullptr, nullptr}));
                        break;
                    }
                    case HIRMacroItem::TAG_ProcMacro: {
                        auto& e = (*itemPtr).as_ProcMacro();
                        rv.macro.set(ap, ASTPathBindingMacro::make_ProcMacro({&hcrate, e.name}));
                        break;
                    }
                    case HIRMacroItem::TAG_MacroRules: {
                        auto& e = (*itemPtr).as_MacroRules();
                        rv.macro.set(ap, ASTPathBindingMacro::make_MacroRules({nullptr, &*e}));
                        break;
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

ASTPath::Bindings ResolveUseGetBindingExt(const Span& span, const ASTCrate& crate, const ASTPath& path, const ASTExternCrate& ec, unsigned int start) {
    DEBUG("Crate " << ec.name);
    auto rv = ResolveUseGetBindingExt(span, crate, ec, ec.hir->rootModule, path, start);
    if (auto* e = rv.macro.binding.opt_MacroRules()) {
        if (e->crate_ == nullptr) {
            e->crate_ = &ec;
        }
    }
    return rv;
}

ASTPath::Bindings ResolveUseGetBinding(
    const Span& span,
    const Settings& settings,
    const ASTCrate& crate,
    const ASTAbsolutePath& sourceModPath,
    const ASTPath& path,
    ::std::span<const ASTModule*> parentModules,
    bool typesOnly /*=false*/,
    bool softFail /*=false*/
) {
    TRACE_FUNCTION_F(path);
    //::AST::Path rv;

    // If the path is directly referring to an external crate - call __ext
    if (path.cls.is_Absolute() && (path.cls.as_Absolute().crate != "" && path.cls.as_Absolute().crate != crate.crateNameReal)) {
        const auto& pathAbs = path.cls.as_Absolute();
        // Builtin macro imports
        if (pathAbs.crate == rcstringCrateBuiltins) {
            ASTPath::Bindings rv;
            ASSERT_BUG(span, !pathAbs.nodes.empty(), "");
            if (coretypeFromstring(path.nodes()[0].name().c_str()) != CORETYPE_INVAL) {
                rv.type.set(ASTAbsolutePath(rcstringCrateBuiltins, {pathAbs.nodes.back().name()}), ASTPathBindingType::make_TypeAlias({nullptr}));
            } else {
                rv.macro.set(ASTAbsolutePath(rcstringCrateBuiltins, {pathAbs.nodes.back().name()}), ASTPathBindingMacro::make_MacroRules({nullptr}));
            }
            return rv;
        }

        ASSERT_BUG(span, crate.externCrates.count(pathAbs.crate.c_str()), "Crate '" << pathAbs.crate << "' not loaded");
        return ResolveUseGetBindingExt(span, crate, path, crate.externCrates.at(pathAbs.crate.c_str()), 0);
    }

    ASTPath::Bindings rv;

    const ASTModule* mod = &crate.rootModule_;
    const auto& nodes = path.nodes();
    if (nodes.size() == 0) {
        // An import of the root.
        rv.type.set(mod->path(), ASTPathBindingType::make_Module({mod, {nullptr}}));
        return rv;
    }

    std::vector<const ASTModule*> innerParentModules;
    for (unsigned int i = 0; i < nodes.size() - 1; i++) {
        DEBUG("Component " << nodes.at(i).name());
        // TODO: If this came from an import, return the real path?

        assert(mod);
        auto b = ResolveUseGetBindingMod(span, settings, crate, sourceModPath, *mod, nodes.at(i).name(), innerParentModules, /*types_only=*/true);
        switch (b.type.binding.tag()) {
default:
            ERROR(span, E0000, "Unexpected item type " << b.type.binding.tagStr() << " in import of " << path);
            case ASTPathBindingType::TAG_Unbound: {
                // During speculative glob resolution a miss just skips the glob; the recursion
                // guard can hide a module that a later direct resolution will find.
                if (softFail) {
                    return ASTPath::Bindings();
                }
                ERROR(span, E0000, "Cannot find component " << i << " of " << path << " (" << b.type.binding << ")");
                break;
            }
            case ASTPathBindingType::TAG_Crate: {
                auto& e = b.type.binding.as_Crate();
                // TODO: Mangle the original path (or return a new path somehow)
                DEBUG("Extern - Call _ext with remainder");
                return ResolveUseGetBindingExt(span, crate, path, *e.crate_, i + 1);
            }
            case ASTPathBindingType::TAG_Trait: {
                // `use A::new;` imports an associated item of a trait
                // (`import_trait_associated_functions`). The import stands for
                // the path through the trait, which resolution already handles
                // where it is written out.
                auto& e = b.type.binding.as_Trait();
                ASSERT_BUG(span, e.trait_ || e.hir, "nullptr trait pointer in node " << i << " of " << path);
                i += 1;
                if (i != nodes.size() - 1) {
                    ERROR(span, E0000, "Encountered trait at unexpected location in import");
                }
                const auto& node2 = nodes[i];

                bool isValue = false;
                bool isType = false;
                const ASTFunction* astFunc = nullptr;
                if (e.hir) {
                    if (e.hir->values.count(node2.name())) {
                        isValue = true;
                    }
                    if (e.hir->types.count(node2.name())) {
                        isType = true;
                    }
                } else {
                    for (const auto& item : e.trait_->items()) {
                        if (item.name != node2.name()) {
                            continue;
                        }
                        switch (item.data.tag()) {
                            case ASTItem::TAG_Function:
                                isValue = true;
                                astFunc = &item.data.as_Function();
                                break;
                            case ASTItem::TAG_Static:
                                isValue = true;
                                break;
                            case ASTItem::TAG_Type:
                                isType = true;
                                break;
                            default:
                                break;
                        }
                        break;
                    }
                }
                if (!isValue && !isType) {
                    if (softFail) {
                        return ASTPath::Bindings();
                    }
                    ERROR(span, E0000, "Unknown associated item " << node2.name() << " of trait in " << path);
                }
                auto itemPath = b.type.path + node2.name();
                if (isValue) {
                    if (astFunc) {
                        rv.value.set(itemPath, ASTPathBindingValue::make_Function({astFunc}));
                    } else {
                        rv.value.set(itemPath, ASTPathBindingValue::make_Static({nullptr, nullptr}));
                    }
                }
                if (isType) {
                    rv.type.set(itemPath, ASTPathBindingType::make_TypeAlias({nullptr}));
                }
                return rv;
            }
            case ASTPathBindingType::TAG_Enum: {
                auto& e = b.type.binding.as_Enum();
                ASSERT_BUG(span, e.enum_ || e.hir, "nullptr enum pointer in node " << i << " of " << path);
                ASSERT_BUG(span, e.enum_ == nullptr || e.hir == nullptr, "both AST and HIR pointers set in node " << i << " of " << path);
                i += 1;
                if (i != nodes.size() - 1) {
                    ERROR(span, E0000, "Encountered enum at unexpected location in import");
                }
                ASSERT_BUG(span, i < nodes.size(), "Enum import position error, " << i << " >= " << nodes.size() << " - " << path);

                const auto& node2 = nodes[i];

                unsigned variantIndex = 0;
                bool isValue = false;
                if (e.hir) {
                    const auto& enum_ = *e.hir;
                    size_t idx = enum_.findVariant(node2.name());
                    if (idx == ~0u) {
                        ERROR(span, E0000, "Unknown enum variant " << path);
                    }
                switch (enum_.data.tag()) {
                    case HIREnumClass::TAG_Value: {
                        isValue = true;
                        break;
                    }
                    case HIREnumClass::TAG_Data: {
                        auto& ve = enum_.data.as_Data();
                        isValue = !ve[idx].isStruct;
                        break;
                    }
                }
                DEBUG("HIR Enum variant - " << variantIndex << ", is_value=" << isValue);
                } else {
                    const auto& enum_ = *e.enum_;
                    for (const auto& var : enum_.variants()) {
                        if (var.name == node2.name()) {
                            isValue = !var.data.is_Struct();
                            break;
                        }
                        variantIndex++;
                    }
                    if (variantIndex == enum_.variants().size()) {
                        ERROR(span, E0000, "Unknown enum variant '" << node2.name() << "'");
                    }

                    DEBUG("AST Enum variant - " << variantIndex << ", is_value=" << isValue << " " << enum_.variants()[variantIndex].data.tagStr());
                }
                if (isValue) {
                    rv.value.set(b.type.path + node2.name(), ASTPathBindingValue::make_EnumVar({e.enum_, variantIndex, e.hir}));
                } else {
                    rv.type.set(b.type.path + node2.name(), ASTPathBindingType::make_EnumVar({e.enum_, variantIndex, e.hir}));
                }
                return rv;
            }
            case ASTPathBindingType::TAG_Module: {
                auto& e = b.type.binding.as_Module();
                ASSERT_BUG(span, e.module_ || e.hir.mod, "nullptr module pointer in node " << i << " of " << path);
                if (!e.module_) {
                    assert(e.hir.crate);
                    assert(e.hir.mod);
                    return ResolveUseGetBindingExt(span, crate, *e.hir.crate, *e.hir.mod, path, i + 1, b.type.path);
                }
                innerParentModules.push_back(mod);
                mod = e.module_;
                break;
            }
        }
    }

    assert(mod);
    return ResolveUseGetBindingMod(span, settings, crate, sourceModPath, *mod, nodes.back().name(), parentModules, typesOnly);
}

//::AST::PathBinding_Macro Resolve_Use_GetBinding_Macro(const Span& span, const ::AST::Crate& crate, const ::AST::Path& path, ::std::span< const ::AST::Module* > parent_modules)
//{
//    throw "";
//}

// Bodies of the generated local unions (see resolve_ctx_ent.tu).
#include "resolve_ctx_ent_tu.cpp"
