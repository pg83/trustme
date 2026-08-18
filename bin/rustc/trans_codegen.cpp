#include "trans_codegen.h"

#include "hir_hir.h"
#include "mir_mir.h"
#include "wire_board.h"
#include "mir_helpers.h"
#include "trans_target.h"
#include "mir_operations.h"
#include "trans_mangling.h"
#include "trans_codegen_c.h"
#include "trans_trans_list.h"
#include "hir_typeck_static.h"
#include "trans_codegen_mir.h"
#include "trans_monomorphise.h"
#include "trans_main_bindings.h"

#include <fstream>
#include <iomanip>
#include <algorithm>

void TransCodegen(const WireBoard& wb, const ::std::string& outfile, CodegenOutput outTy, const TransOptions& opt, HIRCrate* cratePtr, TransList list, const ::std::string& hirFile) {
    static Span sp;

    ::std::unique_ptr<CodeGenerator> codegen;
    if (opt.mode == "monomir") {
        codegen = TransCodegenGetGeneratorMonoMir(wb, *cratePtr, outfile);
    } else if (opt.mode == "c") {
        codegen = TransCodegenGetGeneratorC(wb, *cratePtr, outfile);
    } else {
        BUG(sp, "Unknown codegen mode '" << opt.mode << "'");
    }

    // 1. Emit structure/type definitions.
    // - Emit in the order they're needed.
    for (const auto& ty : list.types) {
        if (ty.second) {
            codegen->emitTypeProto(ty.first);
        } else {
            if (const auto* te = ty.first->opt_Path()) {
                switch (te->binding.tag()) {
                    case HIRTypePathBinding::TAG_Unbound: {
                        auto& tpb = te->binding.as_Unbound();
                        (void)tpb;
                        throw "";
                    }
                    case HIRTypePathBinding::TAG_Opaque: {
                        auto& tpb = te->binding.as_Opaque();
                        (void)tpb;
                        throw "";
                    }
                    case HIRTypePathBinding::TAG_ExternType: {
                        auto& tpb = te->binding.as_ExternType();
                        (void)tpb;
                        break;
                    }
                    case HIRTypePathBinding::TAG_Struct: {
                        auto& tpb = te->binding.as_Struct();
                        codegen->emitStruct(sp, te->path.data.as_Generic(), *tpb);
                        break;
                    }
                    case HIRTypePathBinding::TAG_Union: {
                        auto& tpb = te->binding.as_Union();
                        codegen->emitUnion(sp, te->path.data.as_Generic(), *tpb);
                        break;
                    }
                    case HIRTypePathBinding::TAG_Enum: {
                        auto& tpb = te->binding.as_Enum();
                        codegen->emitEnum(sp, te->path.data.as_Generic(), *tpb);
                        break;
                    }
                }
            }
            codegen->emitType(ty.first);
        }
    }
    list.clearTypes();
    for (const auto& ty : list.typeids) {
        codegen->emitTypeId(ty);
    }
    list.typeids.clear();
    // Emit required constructor methods (and other wrappers)
    for (const auto& path : list.constructors) {
        // Get the item type
        // - Function (must be an intrinsic)
        // - Struct (must be a tuple struct)
        // - Enum variant (must be a tuple variant)
        const HIRModule* modPtr = nullptr;
        if (path.path.components().size() > 1) {
            const auto& nse = cratePtr->getTypeitemByPath(sp, path.path, false, true);
            if (const auto* e = nse.opt_Enum()) {
                auto varIdx = e->findVariant(path.path.components().back());
                codegen->emitConstructorEnum(sp, path, *e, varIdx);
                continue;
            }
            modPtr = &nse.as_Module();
        } else {
            modPtr = &cratePtr->getModByPath(sp, path.path, true);
        }

        // Not an enum, currently must be a struct
        const auto& te = modPtr->modItems.at(path.path.components().back())->ent;
        codegen->emitConstructorStruct(sp, path, te.as_Struct());
    }
    list.constructors.clear();

    // 2. Emit function prototypes
    for (const auto& ent : list.functions) {
        DEBUG("FUNCTION " << ent.first);
        assert(ent.second->ptr);
        const auto& fcn = *ent.second->ptr;
        // Extern if there isn't any HIR
        bool isExtern = !static_cast<bool>(fcn.code);
        if (fcn.code.mir && !ent.second->forcePrototype) {
            codegen->emitFunctionProto(ent.first, fcn, ent.second->pp, isExtern);
        }
    }
    // - External functions
    for (const auto& ent : list.functions) {
        assert(ent.second->ptr);
        const auto& fcn = *ent.second->ptr;
        if (fcn.code.mir && !ent.second->forcePrototype) {
        } else {
            // TODO: Why would an intrinsic be in the queue?
            // - If it's exported it does.
            if (fcn.abi == "rust-intrinsic") {
            } else {
                codegen->emitFunctionExt(ent.first, fcn, ent.second->pp);
            }
        }
    }
    // VTables (may be needed by statics)
    assert(list.vtables.empty());
    // 3. Emit statics
    for (const auto& ent : list.statics) {
        assert(ent.second->ptr);
        const auto& stat = *ent.second->ptr;

        DEBUG(
            "STATIC proto " << ent.first << ": "
                            << "(m_value_generated=" << stat.valueGenerated << " && !m_no_emit_value=" << stat.noEmitValue << ") || is_generic=" << stat.params.isGeneric()
        );
        if ((stat.valueGenerated && !stat.noEmitValue) || stat.params.isGeneric()) {
            codegen->emitStaticProto(ent.first, stat, ent.second->pp);
        } else {
            codegen->emitStaticExt(ent.first, stat, ent.second->pp);
        }
    }
    for (const auto& ent : list.statics) {
        DEBUG("STATIC " << ent.first);
        assert(ent.second->ptr);
        const auto& stat = *ent.second->ptr;

        if (stat.params.isGeneric()) {
            codegen->emitStaticLocal(ent.first, stat, ent.second->pp, stat.monomorphCache.at(ent.first));
        } else if (stat.valueGenerated && !stat.noEmitValue) {
            codegen->emitStaticLocal(ent.first, stat, ent.second->pp, stat.valueRes);
        } else {
        }
    }
    list.statics.clear();

    // 4. Emit function code
    for (const auto& ent : list.functions) {
        if (ent.second->ptr && ent.second->ptr->code.mir && !ent.second->forcePrototype) {
            const auto& path = ent.first;
            const auto& fcn = *ent.second->ptr;
            const auto& pp = ent.second->pp;
            TRACE_FUNCTION_F(path);
            DEBUG("FUNCTION CODE " << path);
            // `is_extern` is set if there's no HIR (i.e. this function is from an external crate)
            bool isExtern = !static_cast<bool>(fcn.code);
            // If this is a provided trait method, it needs to be monomorphised too.
            bool isMethod = (fcn.args.size() > 0 && visitTyWith(fcn.args[0].second, [&](const auto& x) {
                return x == cratePtr->types.self();
            }));

            bool isMonomorph = pp.hasTypes() || isMethod;
            if (ent.second->monomorphised.code) {
                // TODO: Flag that this should be a weak (or weak-er) symbol?
                // - If it's from an external crate, it should be weak, but what about local ones?
                codegen->emitFunctionCode(path, fcn, pp, isExtern, ent.second->monomorphised.code);
            } else {
                ASSERT_BUG(sp, !isMonomorph, "Function that required monomorphisation wasn't monomorphised");
                codegen->emitFunctionCode(path, fcn, pp, isExtern, fcn.code.mir);
            }
        }
    }
    list.functions.clear();

    auto emitGlobalAsm = [&](auto&& self, const HIRModule& mod) -> void {
        for (const auto& item : mod.globalAsm) {
            codegen->emitGlobalAsm(item);
        }
        for (const auto& named : mod.modItems) {
            if (const auto* child = named.second->ent.opt_Module()) {
                self(self, *child);
            }
        }
    };
    emitGlobalAsm(emitGlobalAsm, cratePtr->rootModule);

    // NOTE: Completely reinitialise the `TransList` to free all monomorphised memory before calling the backend compilation tool
    // - This can save several GB of working set
    list = TransList();
    // Would drop the entire crate, but finalise tends to need it
    codegen->finalise(opt, outTy, hirFile);
}

CodeGenerator::~CodeGenerator() {
}

void CodeGenerator::finalise(const TransOptions& opt, CodegenOutput outTy, const ::std::string& hirFile) {
}

// Called on all types directly mentioned (e.g. variables, arguments, and fields)
// - Inner-most types are visited first.
void CodeGenerator::emitTypeProto(const HIRTypeData*) {
}

void CodeGenerator::emitType(const HIRTypeData*) {
}

void CodeGenerator::emitTypeId(const HIRTypeData*) {
}

void CodeGenerator::emitStaticExt(const HIRPath& p, const HIRStatic& item, const TransParams& params) {
}

void CodeGenerator::emitStaticProto(const HIRPath& p, const HIRStatic& item, const TransParams& params) {
}

void CodeGenerator::emitFunctionExt(const HIRPath& p, const HIRFunction& item, const TransParams& params) {
}

void CodeGenerator::emitFunctionProto(const HIRPath& p, const HIRFunction& item, const TransParams& params, bool isExternDef) {
}
