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
#include "hir_typeck_common.h"
#include "hir_typeck_static.h"
#include "trans_codegen_mir.h"
#include "trans_monomorphise.h"
#include "trans_main_bindings.h"

#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>

#include <fstream>
#include <iomanip>
#include <algorithm>

using namespace stl;

namespace {
    struct FunctionOrderNode;

    struct FunctionOrderEdge {
        FunctionOrderNode* target;
        FunctionOrderEdge* next;
    };

    struct FunctionOrderNode {
        const TransListFunction* function;
        FunctionOrderEdge* dependencies;
        unsigned index;
        unsigned lowLink;
        bool onStack;
        bool selfEdge;
        bool needsPrototype;
    };

    bool functionHasDefinition(const TransListFunction& function) {
        return function.ptr && function.ptr->code.mir && !function.forcePrototype;
    }

    const MIRFunctionPointer& functionCode(const TransListFunction& function) {
        return function.monomorphised.code ? function.monomorphised.code : function.ptr->code.mir;
    }

    struct FunctionOrder {
        TransList& list;
        const Span& sp;
        StaticTraitResolve resolve;
        ObjPool::Ref poolOwner = ObjPool::fromMemory();
        ObjPool* pool = poolOwner.mutPtr();
        Vector<FunctionOrderNode*> nodes;
        Vector<FunctionOrderNode*> tarjanStack;
        unsigned nextIndex = 0;

        FunctionOrderNode* findNode(const HIRPath& path) const;

        void addDependency(FunctionOrderNode& source, const HIRPath& path);

        void addDropDependency(FunctionOrderNode& source, HIRTypeRef type);

        void collectDependencies(FunctionOrderNode& source);

        void visit(FunctionOrderNode& node);

        Vector<FunctionOrderNode*> ordered;

        FunctionOrder(const WireBoard& wb, TransList& list, const Span& sp);
    };
}

void TransCodegen(const WireBoard& wb, const ::std::string& outfile, CodegenOutput outTy, const TransOptions& opt, HIRCrate* cratePtr, TransList list, const ::std::string& hirFile) {
    Span sp;

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
                        UNREACHABLE();
                    }
                    case HIRTypePathBinding::TAG_Opaque: {
                        UNREACHABLE();
                    }
                    case HIRTypePathBinding::TAG_ExternType: {
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

    FunctionOrder functionOrder(wb, list, sp);
    const bool orderedFunctions = opt.mode == "c";

    // 2. Emit function prototypes
    if (orderedFunctions) {
        for (const auto* node : functionOrder.ordered) {
            if (!node->needsPrototype) {
                continue;
            }
            const auto& function = *node->function;
            const auto& fcn = *function.ptr;
            const bool isExtern = !static_cast<bool>(fcn.code);
            codegen->emitFunctionProto(*function.path, fcn, function.pp, isExtern);
        }
    } else {
        for (const auto& ent : list.functions) {
            assert(ent.second->ptr);
            const auto& fcn = *ent.second->ptr;
            // Extern if there isn't any HIR
            bool isExtern = !static_cast<bool>(fcn.code);
            if (fcn.code.mir && !ent.second->forcePrototype) {
                codegen->emitFunctionProto(ent.first, fcn, ent.second->pp, isExtern);
            }
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

        if ((stat.valueGenerated && !stat.noEmitValue) || stat.params.isGeneric()) {
            codegen->emitStaticProto(ent.first, stat, ent.second->pp);
        } else {
            codegen->emitStaticExt(ent.first, stat, ent.second->pp);
        }
    }
    auto emitStaticDefinitions = [&]() {
        for (const auto& ent : list.statics) {
            assert(ent.second->ptr);
            const auto& stat = *ent.second->ptr;

            if (stat.params.isGeneric()) {
                codegen->emitStaticLocal(ent.first, stat, ent.second->pp, stat.monomorphCache.at(ent.first));
            } else if (stat.valueGenerated && !stat.noEmitValue) {
                codegen->emitStaticLocal(ent.first, stat, ent.second->pp, stat.valueRes);
            }
        }
    };
    if (!orderedFunctions) {
        emitStaticDefinitions();
    }

    // 4. Emit function code
    auto emitFunctionDefinition = [&](const TransListFunction& function, bool hasPrototype) {
        const auto& path = *function.path;
        const auto& fcn = *function.ptr;
        const auto& pp = function.pp;
        // `is_extern` is set if there's no HIR (i.e. this function is from an external crate)
        bool isExtern = !static_cast<bool>(fcn.code);
        // If this is a provided trait method, it needs to be monomorphised too.
        bool isMethod = (fcn.args.size() > 0 && visitTyWith(fcn.args[0].second, [&](const auto& x) {
            return x == cratePtr->types.self();
        }));

        bool isMonomorph = pp.hasTypes() || isMethod;
        if (function.monomorphised.code) {
            // TODO: Flag that this should be a weak (or weak-er) symbol?
            // - If it's from an external crate, it should be weak, but what about local ones?
            codegen->emitFunctionCode(path, fcn, pp, isExtern, function.monomorphised.code, hasPrototype);
        } else {
            ASSERT_BUG(sp, !isMonomorph, "Function that required monomorphisation wasn't monomorphised");
            codegen->emitFunctionCode(path, fcn, pp, isExtern, fcn.code.mir, hasPrototype);
        }
    };
    if (orderedFunctions) {
        for (const auto* node : functionOrder.ordered) {
            emitFunctionDefinition(*node->function, node->needsPrototype);
        }
    } else {
        for (const auto& ent : list.functions) {
            if (functionHasDefinition(*ent.second)) {
                emitFunctionDefinition(*ent.second, true);
            }
        }
    }
    if (orderedFunctions) {
        emitStaticDefinitions();
    }
    list.statics.clear();
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

auto FunctionOrder::findNode(const HIRPath& path) const -> FunctionOrderNode* {
    size_t begin = 0;
    size_t end = nodes.length();
    while (begin < end) {
        const auto middle = begin + (end - begin) / 2;
        const auto& candidate = *nodes[middle]->function->path;
        if (candidate < path) {
            begin = middle + 1;
        } else {
            end = middle;
        }
    }
    if (begin < nodes.length() && !(*nodes[begin]->function->path < path) && !(path < *nodes[begin]->function->path)) {
        return nodes[begin];
    }
    return nullptr;
}

auto FunctionOrder::addDependency(FunctionOrderNode& source, const HIRPath& path) -> void {
    const auto* function = list.findFunction(path);
    if (!function && path.data.is_UfcsKnown()) {
        MonomorphState params(resolve.hirCrate().types);
        StaticTraitResolve::ResolvedTraitImplPath implPath;
        resolve.getValue(sp, path, params, false, nullptr, &implPath);
        if (implPath.type) {
            auto canonical = path.clone();
            auto& data = canonical.data.as_UfcsKnown();
            data.type = implPath.type;
            data.trait.params = implPath.traitParams.clone();
            function = list.findFunction(canonical);
        }
    }
    if (!function) {
        return;
    }
    auto* target = findNode(*function->path);
    if (!target) {
        return;
    }
    source.dependencies = pool->make<FunctionOrderEdge>(FunctionOrderEdge{target, source.dependencies});
    source.selfEdge = source.selfEdge || target == &source;
}

auto FunctionOrder::addDropDependency(FunctionOrderNode& source, HIRTypeRef type) -> void {
    if (!resolve.typeNeedsDropGlue(sp, type)) {
        return;
    }
    switch (type->tag()) {
        case HIRTypeData::TAG_Borrow: {
            const auto& borrow = type->as_Borrow();
            if (borrow.type == HIRBorrowType::Owned) {
                addDropDependency(source, borrow.inner);
            }
            break;
        }
        case HIRTypeData::TAG_Path:
            addDependency(source, HIRPath(type, "#drop_glue"));
            break;
        case HIRTypeData::TAG_Array:
            addDropDependency(source, type->as_Array().inner);
            break;
        case HIRTypeData::TAG_Tuple:
            for (const auto& field : type->as_Tuple()) {
                addDropDependency(source, field);
            }
            break;
        case HIRTypeData::TAG_Slice:
            addDropDependency(source, type->as_Slice().inner);
            break;
        case HIRTypeData::TAG_Pattern:
            addDropDependency(source, type->as_Pattern().inner);
            break;
        case HIRTypeData::TAG_Diverge:
        case HIRTypeData::TAG_Infer:
        case HIRTypeData::TAG_ErasedType:
        case HIRTypeData::TAG_NodeType:
        case HIRTypeData::TAG_Generic:
        case HIRTypeData::TAG_Primitive:
        case HIRTypeData::TAG_Pointer:
        case HIRTypeData::TAG_NamedFunction:
        case HIRTypeData::TAG_Function:
        case HIRTypeData::TAG_TraitObject:
            break;
    }
}

auto FunctionOrder::collectDependencies(FunctionOrderNode& source) -> void {
    const auto& function = *source.function;
    const auto& mir = *functionCode(function);
    const auto& returnType = function.monomorphised.code ? function.monomorphised.retTy : function.ptr->returnType;
    const auto& args = function.monomorphised.code ? function.monomorphised.argTys : function.ptr->args;
    auto pathCallback = makeCallable<MIRPathCb>([&](auto& os) {
        os << *function.path;
    });
    MIRTypeResolve mirResolve{sp, resolve, pathCallback, returnType, args, mir};

    struct DependencyVisitor final: public MIRVisitor {
        FunctionOrder& order;
        FunctionOrderNode& source;
        MIRTypeResolve& mirResolve;

        DependencyVisitor(FunctionOrder& order, FunctionOrderNode& source, MIRTypeResolve& mirResolve)
            : order(order)
            , source(source)
            , mirResolve(mirResolve)
        {
        }

        void visitType(HIRTypeRef type) override {
            visitTyWith(type, [&](HIRTypeRef nested) {
                if (const auto* function = nested->opt_NamedFunction()) {
                    order.addDependency(source, function->path);
                }
                return false;
            });
        }

        void visitPath(const HIRPath& path) override {
            order.addDependency(source, path);
            MIRVisitor::visitPath(path);
        }

        bool visitTerminator(const MIRTerminator& terminator) override {
            if (const auto* drop = terminator.opt_Drop()) {
                HIRTypeRef tmp;
                order.addDropDependency(source, mirResolve.getLvalueType(tmp, drop->slot));
            }
            const MIRCallTarget* callTarget = nullptr;
            if (const auto* call = terminator.opt_Call()) {
                callTarget = &call->fcn;
            } else if (const auto* call = terminator.opt_TailCall()) {
                callTarget = &call->fcn;
            }
            if (callTarget) {
                if (const auto* intrinsic = callTarget->opt_Intrinsic()) {
                    if (intrinsic->name == "drop_in_place") {
                        order.addDropDependency(source, intrinsic->params.types.at(0));
                    }
                }
            }
            return MIRVisitor::visitTerminator(terminator);
        }
    } visitor{*this, source, mirResolve};

    visitor.visitFunction(mirResolve, mir);
}

auto FunctionOrder::visit(FunctionOrderNode& node) -> void {
    node.index = nextIndex;
    node.lowLink = nextIndex;
    nextIndex++;
    node.onStack = true;
    tarjanStack.pushBack(&node);

    for (auto* edge = node.dependencies; edge; edge = edge->next) {
        auto& target = *edge->target;
        if (target.index == ~0u) {
            visit(target);
            node.lowLink = ::std::min(node.lowLink, target.lowLink);
        } else if (target.onStack) {
            node.lowLink = ::std::min(node.lowLink, target.index);
        }
    }

    if (node.lowLink != node.index) {
        return;
    }
    const auto componentStart = ordered.length();
    FunctionOrderNode* member;
    do {
        member = tarjanStack.popBack();
        member->onStack = false;
        ordered.pushBack(member);
    } while (member != &node);
    const auto componentSize = ordered.length() - componentStart;
    if (componentSize > 1 || node.selfEdge) {
        for (size_t i = componentStart; i < ordered.length(); i++) {
            ordered[i]->needsPrototype = true;
        }
    }
}

FunctionOrder::FunctionOrder(const WireBoard& wb, TransList& list, const Span& sp)
    : list(list)
    , sp(sp)
    , resolve(wb, OpaqueReveal::All)
{
    for (const auto& entry : list.functions) {
        if (!functionHasDefinition(*entry.second)) {
            continue;
        }
        auto* node = pool->make<FunctionOrderNode>(FunctionOrderNode{entry.second.get(), nullptr, ~0u, ~0u, false, false, false});
        nodes.pushBack(node);
    }
    for (auto* node : nodes) {
        collectDependencies(*node);
    }
    for (auto* node : nodes) {
        if (node->index == ~0u) {
            visit(*node);
        }
    }
}
