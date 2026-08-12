#include "trans_codegen.h"
#include "trans_main_bindings.h"
#include "trans_trans_list.h"
#include "hir_hir.h"
#include "mir_mir.h"
#include "mir_operations.h"
#include <algorithm>
#include "trans_target.h"

#include "trans_monomorphise.h"
#include "hir_typeck_static.h"
#include "mir_helpers.h"
#include "trans_mangling.h"
#include <iomanip>
#include <fstream>

void TransCodegen(const ::std::string& outfile, CodegenOutput outTy, const TransOptions& opt, ::HIR::Crate* cratePtr, TransList list, const ::std::string& hirFile) {
    static Span sp;

    ::std::unique_ptr<CodeGenerator> codegen;
    if (opt.mode == "monomir") {
        codegen = TransCodegenGetGeneratorMonoMir(*cratePtr, outfile);
    } else if (opt.mode == "c") {
        codegen = TransCodegenGetGeneratorC(*cratePtr, outfile);
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
                TU_MATCHA(
                    (te->binding),
                    (tpb),
                    (Unbound, throw "";),
                    (Opaque, throw "";),
                    (
                        ExternType,
                        //codegen->emit_extern_type(sp, te->path.m_data.as_Generic(), *tpb);
                    ),
                    (Struct, codegen->emitStruct(sp, te->path.mData.as_Generic(), *tpb);),
                    (Union, codegen->emitUnion(sp, te->path.mData.as_Generic(), *tpb);),
                    (Enum, codegen->emitEnum(sp, te->path.mData.as_Generic(), *tpb);)
                )
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
        const ::HIR::Module* modPtr = nullptr;
        if (path.mPath.components().size() > 1) {
            const auto& nse = cratePtr->getTypeitemByPath(sp, path.mPath, false, true);
            if (const auto* e = nse.opt_Enum()) {
                auto varIdx = e->findVariant(path.mPath.components().back());
                codegen->emitConstructorEnum(sp, path, *e, varIdx);
                continue;
            }
            modPtr = &nse.as_Module();
        } else {
            modPtr = &cratePtr->getModByPath(sp, path.mPath, true);
        }

        // Not an enum, currently must be a struct
        const auto& te = modPtr->modItems.at(path.mPath.components().back())->ent;
        codegen->emitConstructorStruct(sp, path, te.as_Struct());
    }
    list.constructors.clear();

    // 2. Emit function prototypes
    for (const auto& ent : list.functions) {
        DEBUG("FUNCTION " << ent.first);
        assert(ent.second->ptr);
        const auto& fcn = *ent.second->ptr;
        // Extern if there isn't any HIR
        bool isExtern = !static_cast<bool>(fcn.mCode);
        if (fcn.mCode.mir && !ent.second->forcePrototype) {
            codegen->emitFunctionProto(ent.first, fcn, ent.second->pp, isExtern);
        }
    }
    // - External functions
    for (const auto& ent : list.functions) {
        //DEBUG("FUNCTION " << ent.first);
        assert(ent.second->ptr);
        const auto& fcn = *ent.second->ptr;
        if (fcn.mCode.mir && !ent.second->forcePrototype) {
        } else {
            // TODO: Why would an intrinsic be in the queue?
            // - If it's exported it does.
            if (fcn.mAbi == "rust-intrinsic") {
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
                            << "(m_value_generated=" << stat.valueGenerated << " && !m_no_emit_value=" << stat.noEmitValue << ") || is_generic=" << stat.mParams.isGeneric()
        );
        if ((stat.valueGenerated && !stat.noEmitValue) || stat.mParams.isGeneric()) {
            codegen->emitStaticProto(ent.first, stat, ent.second->pp);
        } else {
            codegen->emitStaticExt(ent.first, stat, ent.second->pp);
        }
    }
    for (const auto& ent : list.statics) {
        DEBUG("STATIC " << ent.first);
        assert(ent.second->ptr);
        const auto& stat = *ent.second->ptr;

        if (stat.mParams.isGeneric()) {
            codegen->emitStaticLocal(ent.first, stat, ent.second->pp, stat.monomorphCache.at(ent.first));
        } else if (stat.valueGenerated && !stat.noEmitValue) {
            codegen->emitStaticLocal(ent.first, stat, ent.second->pp, stat.valueRes);
        } else {
        }
    }
    list.statics.clear();

    // 4. Emit function code
    for (const auto& ent : list.functions) {
        if (ent.second->ptr && ent.second->ptr->mCode.mir && !ent.second->forcePrototype) {
            const auto& path = ent.first;
            const auto& fcn = *ent.second->ptr;
            const auto& pp = ent.second->pp;
            TRACE_FUNCTION_F(path);
            DEBUG("FUNCTION CODE " << path);
            // `is_extern` is set if there's no HIR (i.e. this function is from an external crate)
            bool isExtern = !static_cast<bool>(fcn.mCode);
            // If this is a provided trait method, it needs to be monomorphised too.
            bool isMethod = (fcn.mArgs.size() > 0 && visitTyWith(fcn.mArgs[0].second, [&](const auto& x) {
                return x == cratePtr->types.self();
            }));

            bool isMonomorph = pp.hasTypes() || isMethod;
            if (ent.second->monomorphised.code) {
                // TODO: Flag that this should be a weak (or weak-er) symbol?
                // - If it's from an external crate, it should be weak, but what about local ones?
                codegen->emitFunctionCode(path, fcn, pp, isExtern, ent.second->monomorphised.code);
            } else {
                ASSERT_BUG(sp, !isMonomorph, "Function that required monomorphisation wasn't monomorphised");
                codegen->emitFunctionCode(path, fcn, pp, isExtern, fcn.mCode.mir);
            }
        }
    }
    list.functions.clear();

    for (const auto& a : cratePtr->globalAsm) {
        codegen->emitGlobalAsm(a);
    }

    // NOTE: Completely reinitialise the `TransList` to free all monomorphised memory before calling the backend compilation tool
    // - This can save several GB of working set
    list = TransList();
    // Would drop the entire crate, but finalise tends to need it
    codegen->finalise(opt, outTy, hirFile);
}



namespace {

    template <typename T>
    struct Fmt {
        const T& e;

        Fmt(const T& e)
            : e(e)
        {
        }
    };

    template <typename T>
    Fmt<T> fmt(const T& v) {
        return Fmt<T>(v);
    }

    ::std::ostream& operator<<(::std::ostream& os, const Fmt<::HIR::Path>& x) {
        return os << TransMangle(x.e);
    }

    ::std::ostream& operator<<(::std::ostream& os, const Fmt<::HIR::GenericPath>& x) {
        return os << TransMangle(x.e);
    }

    ::std::ostream& operator<<(::std::ostream& os, const Fmt<::HIR::SimplePath>& x) {
        return os << TransMangle(x.e);
    }

    ::std::ostream& operator<<(::std::ostream& os, const Fmt<::HIR::TypeRef>& x) {
        TU_MATCH_HDRA( (*x.e), {)
        TU_ARMA(Infer, te)  BUG(Span(), "" << x.e);
            TU_ARMA(Diverge, te) {
                os << "!";
            }
            TU_ARMA(Primitive, te) {
                os << te;
            }
            TU_ARMA(Path, te) {
                os << TransMangle(te.path);
            }
            TU_ARMA(Generic, te) {
                BUG(Span(), "" << x.e);
            }
            TU_ARMA(TraitObject, te) {
                auto path = te.mTrait.mPath.clone();
                os << "dyn " << TransMangle(path);
            }
            TU_ARMA(ErasedType, te) {
                BUG(Span(), "" << x.e);
            }
            TU_ARMA(Array, te) {
                os << "[" << fmt(te.inner) << "; " << te.size << "]";
            }
            TU_ARMA(Slice, te) {
                os << "[" << fmt(te.inner) << "]";
            }
            TU_ARMA(Tuple, te) {
                if (te.empty()) {
                    os << "()";
                } else {
                    os << TransMangle(x.e);
                }
            }
            TU_ARMA(Borrow, te) {
                switch (te.type) {
                    case ::HIR::BorrowType::Shared:
                        os << "&";
                        break;
                    case ::HIR::BorrowType::Unique:
                        os << "&mut ";
                        break;
                    case ::HIR::BorrowType::Owned:
                        os << "&move ";
                        break;
                }
                os << fmt(te.inner);
            }
            TU_ARMA(Pointer, te) {
                switch (te.type) {
                    case ::HIR::BorrowType::Shared:
                        os << "*const ";
                        break;
                    case ::HIR::BorrowType::Unique:
                        os << "*mut ";
                        break;
                    case ::HIR::BorrowType::Owned:
                        os << "*move ";
                        break;
                }
                os << fmt(te.inner);
            }
            TU_ARMA(NamedFunction, te) {
                os << "fn " << TransMangle(te.path);
            }
            TU_ARMA(Function, e) {
                if (e.isUnsafe) {
                    os << "unsafe ";
                }
                if (e.mAbi != "") {
                    os << "extern \"" << e.mAbi << "\" ";
                }
                os << "fn(";
                for (const auto& t : e.argTypes) {
                    os << fmt(t) << ", ";
                }
                os << ") -> " << fmt(e.mRettype);
            }
            break;
            case ::HIR::TypeData::TAG_NodeType:
                BUG(Span(), "Unexpected type in trans: " << x.e);
                break;
        }
        return os;
    }

    ::std::ostream& operator<<(::std::ostream& os, const Fmt<MIRLValue>& x) {
        for (const auto& w : ::reverse(x.e.wrappers)) {
            if (w.is_Deref()) {
                os << "(*";
            }
        }
        TU_MATCHA((x.e.root), (e), (Return, os << "RETURN";), (Local, os << "var" << e;), (Argument, os << "arg" << e;), (Static, os << fmt(e);))
        bool wasNum = false;
        for (const auto& w : x.e.wrappers) {
            bool prevWasNum = wasNum;
            wasNum = false;
            switch (w.tag()) {
                case MIRLValue::Wrapper::TAGDEAD:
                    throw "";
                    TU_ARM(w, Deref, e)
                    os << ")";
                    break;
                    TU_ARM(w, Field, fieldIndex) {
                        // Add a space to prevent accidental float literals
                        if (prevWasNum) {
                            os << " ";
                        }
                        os << "." << fieldIndex;
                        wasNum = true;
                    }
                    break;
                    TU_ARM(w, Index, e) {
                        os << "[" << fmt(MIRLValue::newLocal(e)) << "]";
                    }
                    break;
                    TU_ARM(w, Downcast, variantIndex) {
                        os << "@" << variantIndex;
                        wasNum = true;
                    }
                    break;
            }
        }
        return os;
    }

    ::std::ostream& operator<<(::std::ostream& os, const Fmt<MIRConstant>& x) {
        struct H {
            static uint64_t doubleToU64(double v) {
                uint64_t rv;
                ::std::memcpy(&rv, &v, sizeof(double));
                return rv;
            }
        };

        const auto& e = x.e;
        switch (e.tag()) {
            case MIRConstant::TAGDEAD:
                throw "";
                TU_ARM(e, Int, v) {
                    os << (v.v < 0 ? "" : "+") << v.v << " " << v.t;
                }
                break;
                TU_ARM(e, Uint, v)
                os << v.v << " " << v.t;
                break;
                TU_ARM(e, Float, v) {
                    // TODO: Infinity/nan/...
                    auto vi = H::doubleToU64(static_cast<double>(v.v));
                    bool sign = (vi & (1ull << 63)) != 0;
                    int exp = (vi >> 52) & 0x7FF;
                    uint64_t frac = vi & ((1ull << 52) - 1);
                    os << (sign ? "-" : "+") << "0x1." << ::std::setw(52 / 4) << ::std::setfill('0') << ::std::hex << frac << ::std::dec << "p" << (exp - 1023);
                    os << " " << v.t;
                }
                break;
                TU_ARM(e, ItemAddr, v) {
                    os << "ADDROF " << fmt(*v);
                    if (v.offset != U128(0)) {
                        os << " + " << v.offset;
                    }
                }
                break;
                TU_ARM(e, Const, v) {
                    BUG(Span(), "Stray named constant in MIR after cleanup - " << e);
                }
                break;
            default:
                os << e;
                break;
        }
        return os;
    }

    ::std::ostream& operator<<(::std::ostream& os, const Fmt<MIRParam>& x) {
        switch (x.e.tag()) {
            case MIRParam::TAGDEAD:
                throw "";
                TU_ARM(x.e, LValue, e)
                os << fmt(e);
                break;
                TU_ARM(x.e, Borrow, e) {
                    os << "&";
                    switch (e.type) {
                        case ::HIR::BorrowType::Shared:
                            break;
                        case ::HIR::BorrowType::Unique:
                            os << "mut ";
                            break;
                        case ::HIR::BorrowType::Owned:
                            os << "move ";
                            break;
                    }
                    os << fmt(e.val);
                }
                break;
                TU_ARM(x.e, Constant, e)
                os << fmt(e);
                break;
        }
        return os;
    }

    class CodeGeneratorMonoMir: public CodeGenerator {
        enum class MetadataType {
            None,
            Slice,
            TraitObject,
        };

        static Span sp;

        const ::HIR::Crate& crate;
        ::StaticTraitResolve mResolve;

        ::std::string outfilePath;
        ::std::ofstream of;
        const MIRTypeResolve* mirRes;

    public:
        CodeGeneratorMonoMir(const ::HIR::Crate& crate, const ::std::string& outfile)
            : crate(crate)
            , mResolve(crate)
            , outfilePath(outfile)
            , of(outfilePath + ".mir")
        {
            for (const auto& crateName : crate.extCratesOrdered) {
                of << "crate \"" << FmtEscaped(crate.extCrates.at(crateName).mPath) << ".mir\";\n";
            }
        }

        void finalise(const TransOptions& opt, CodegenOutput outTy, const ::std::string& hirFile) override {
            if (outTy == CodegenOutput::Executable) {
                if (!crate.noMain) {
                    of << "fn main#(isize, *const *const i8): isize {\n";
                    auto cStartPath = mResolve.crate.getLangItemPathOpt("mrustc-start");
                    if (cStartPath == ::HIR::SimplePath()) {
                        auto mainPath = mResolve.crate.getLangItemPath(Span(), "mrustc-main");
                        const auto& startPath = mResolve.crate.getLangItemPathOpt("start");
                        if (crate.isNoCore && startPath == ::HIR::SimplePath()) {
                            const auto& mainFcn = crate.getFunctionByPath(Span(), mainPath);
                            of << "\tlet direct_main_result: " << fmt(mainFcn.returnType) << ";\n";
                            of << "\t0: {\n";
                            of << "\t\tCALL direct_main_result = " << fmt(::HIR::GenericPath(mainPath)) << "() goto 1 else 1\n";
                        } else {
                            of << "\tlet m: fn();\n";
                            of << "\t0: {\n";
                            of << "\t\tASSIGN m = ADDROF " << fmt(::HIR::GenericPath(mainPath)) << ";\n";
                            of << "\t\tCALL RETURN = " << fmt(::HIR::GenericPath(mResolve.crate.getLangItemPath(Span(), "start"))) << "(m, arg0, arg1) goto 1 else 1\n";
                        }
                    } else {
                        of << "\t0: {\n";
                        of << "\t\tCALL RETURN = " << fmt(::HIR::GenericPath(cStartPath)) << "(arg0, arg1) goto 1 else 1;\n";
                    }
                    of << "\t}\n";
                    of << "\t1: {\n";
                    of << "\t\tRETURN\n";
                    of << "\t}\n";
                    of << "}\n";
                }

                // Bind `panic_impl` lang item to the item tagged with `panic_implementation`.
                const auto& panicImplPath = crate.getLangItemPathOpt("mrustc-panic_implementation");
                if (panicImplPath != ::HIR::SimplePath()) {
                    of << "fn panic_impl#(usize): u32 = \"panic_impl\":\"Rust\" {\n";
                    of << "\t0: {\n";
                    of << "\t\tCALL RETURN = " << fmt(panicImplPath) << "(arg0) goto 1 else 2\n";
                    of << "\t}\n";
                    of << "\t1: { RETURN }\n";
                    of << "\t2: { DIVERGE }\n";
                    of << "}\n";
                } else if (!crate.isNoCore) {
                    crate.getLangItemPath(Span(), "mrustc-panic_implementation");
                }

                // TODO: OOM impl?
            }

            of.flush();
            of.close();

            // The requested output is a completion marker; MonoMIR is stored in the sibling `.mir` file.
            {
                ::std::ofstream of(outfilePath);
                if (!of.good()) {
                    // TODO: Error?
                }
            }
        }

        void emitType(const ::HIR::TypeData* ty) override {
            TRACE_FUNCTION_F(ty);
            MIRFunction emptyFcn;
            MIRTypeResolve topMirRes {
                sp, mResolve, FMT_CB(ss, ss << "type " << ty;), ::HIR::TypeRef(), {}, emptyFcn
            };
            mirRes = &topMirRes;

            if (const auto* te = ty->opt_Tuple()) {
                if (te->size() > 0) {
                    const auto* repr = TargetGetTypeRepr(sp, mResolve, ty);
                    MIR_ASSERT(*mirRes, repr, "No repr for tuple " << ty);

                    bool hasDropGlue = mResolve.typeNeedsDropGlue(sp, ty);
                    auto dropGluePath = ::HIR::Path(ty, "#drop_glue");

                    of << "type " << fmt(ty) << " {\n";
                    of << "\tSIZE " << repr->size << ", ALIGN " << repr->align << ";\n";
                    if (hasDropGlue) {
                        of << "\tDROP " << fmt(dropGluePath) << ";\n";
                    }
                    for (const auto& e : repr->fields) {
                        of << "\t" << e.offset << " = " << fmt(e.ty) << ";\n";
                    }
                    of << "}\n";
                }
            } else {
            }

            mirRes = nullptr;
        }

        // TODO: Move this to a more common location
        MetadataType metadataType(const ::HIR::TypeData* ty) const {
            if ((ty->is_Primitive() && ty->as_Primitive() == ::HIR::CoreType::Str) || ty->is_Slice()) {
                return MetadataType::Slice;
            } else if (ty->is_TraitObject()) {
                return MetadataType::TraitObject;
            } else if (ty->is_Path()) {
                const auto& te = ty->as_Path();
                switch (te.binding.tag()) {
                    TU_ARM(te.binding, Struct, tpb) {
                        switch (tpb->structMarkings.dstType) {
                            case ::HIR::StructMarkings::DstType::None:
                                return MetadataType::None;
                            case ::HIR::StructMarkings::DstType::Possible: {
                                // TODO: How to figure out? Lazy way is to check the monomorpised type of the last field (structs only)
                                const auto& path = ty->as_Path().path.mData.as_Generic();
                                const auto& str = *ty->as_Path().binding.as_Struct();
                                auto monomorph = [&](const auto& tpl) {
                                    return mResolve.monomorphExpand(sp, tpl, MonomorphStatePtr(crate.types, nullptr, &path.mParams, nullptr));
                                };
                                TU_MATCHA((str.mData), (se), (Unit, MIR_BUG(*mirRes, "Unit-like struct with DstType::Possible");), (Tuple, return metadataType(monomorph(se.back().ent));), (Named, return metadataType(monomorph(se.back().ty));))
                                //MIR_TODO(*m_mir_res, "Determine DST type when ::Possible - " << ty);
                                return MetadataType::None;
                            }
                            case ::HIR::StructMarkings::DstType::Slice:
                                return MetadataType::Slice;
                            case ::HIR::StructMarkings::DstType::TraitObject:
                                return MetadataType::TraitObject;
                        }
                        throw "";
                    }
                    break;
                    TU_ARM(te.binding, Union, tpb)
                    return MetadataType::None;
                    TU_ARM(te.binding, Enum, tpb)
                    return MetadataType::None;
                    default:
                        MIR_BUG(*mirRes, "Unbound/opaque path in trans - " << ty);
                }
                throw "";
            } else {
                return MetadataType::None;
            }
        }

        void emitStruct(const Span& sp, const ::HIR::GenericPath& p, const ::HIR::Struct& item) override {
            MIRFunction emptyFcn;
            MIRTypeResolve topMirRes {
                sp, mResolve, FMT_CB(ss, ss << "struct " << p;), ::HIR::TypeRef(), {}, emptyFcn
            };
            mirRes = &topMirRes;

            auto dropGluePath = ::HIR::Path(crate.types.path(p.clone(), &item), "#drop_glue");

            TRACE_FUNCTION_F(p);
            ::HIR::TypeRef ty = crate.types.path(p.clone(), &item);

            struct H {
                static ::HIR::TypeRef getMetadataType(const Span& sp, const ::StaticTraitResolve& resolve, const TypeRepr& r) {
                    ASSERT_BUG(sp, r.fields.size() > 0, "");
                    auto& t = r.fields.back().ty;
                    if (t->is_Primitive() && t->as_Primitive() == ::HIR::CoreType::Str) {
                        return resolve.crate.types.primitive(::HIR::CoreType::Usize);
                    } else if (t->is_Slice()) {
                        return resolve.crate.types.primitive(::HIR::CoreType::Usize);
                    } else if (t->is_TraitObject()) {
                        const auto& te = t->as_TraitObject();
                        //auto vtp = t.m_data.as_TraitObject().m_trait.m_path;

                        const auto& trait = resolve.crate.getTraitByPath(sp, te.mTrait.mPath.mPath);
                        auto vtableTy = trait.getVtableType(sp, resolve.crate, te);
                        return resolve.crate.types.pointer(::HIR::BorrowType::Shared, vtableTy);
                    } else if (t->is_Path() && t->as_Path().binding.is_ExternType()) {
                        return resolve.crate.types.unit();
                    } else if (t->is_Path()) {
                        auto* repr = TargetGetTypeRepr(sp, resolve, t);
                        ASSERT_BUG(sp, repr, "No repr for " << t);
                        return getMetadataType(sp, resolve, *repr);
                    } else {
                        BUG(sp, "Unexpected type in get_metadata_type - " << t);
                    }
                }
            };

            // Generate the drop glue (and determine if there is any)
            bool hasDropGlue = mResolve.typeNeedsDropGlue(sp, ty);

            const auto* repr = TargetGetTypeRepr(sp, mResolve, ty);
            MIR_ASSERT(*mirRes, repr, "No repr for struct " << ty);
            of << "type " << TransMangle(p) << " {\n";
            of << "\tSIZE " << repr->size << ", ALIGN " << repr->align << ";\n";
            if (repr->size == SIZE_MAX) {
                of << "\tDSTMETA " << H::getMetadataType(sp, mResolve, *repr) << ";\n";
            }
            if (hasDropGlue) {
                of << "\tDROP " << fmt(dropGluePath) << ";\n";
            }
            for (const auto& e : repr->fields) {
                of << "\t" << e.offset << " = " << fmt(e.ty) << ";\n";
            }
            of << "}\n";

            mirRes = nullptr;
        }

        void emitConstructorEnum(const Span& sp, const ::HIR::GenericPath& varPath, const ::HIR::Enum& item, size_t varIdx) override {
            TRACE_FUNCTION_F(varPath);

            ::HIR::TypeRef tmp;
            MonomorphStatePtr ms(crate.types, nullptr, &varPath.mParams, nullptr);
            auto monomorph = [&](const auto& x) {
                return mResolve.monomorphExpandOpt(sp, tmp, x, ms);
            };

            auto enumPath = varPath.clone();
            enumPath.mPath.popComponent();

            // Create constructor function
            const auto& varTy = item.mData.as_Data().at(varIdx).type;
            const auto& e = varTy->as_Path().binding.as_Struct()->mData.as_Tuple();
            of << "/* " << varPath << " */\n";
            of << "fn " << fmt(varPath) << "(";
            for (unsigned int i = 0; i < e.size(); i++) {
                if (i != 0) {
                    of << ", ";
                }
                of << fmt(monomorph(e[i].ent));
            }
            of << "): " << fmt(enumPath) << " {\n";
            of << "\t0: {\n";
            of << "\t\tASSIGN RETURN = ENUM " << fmt(enumPath) << " " << varIdx << " { ";
            for (unsigned int i = 0; i < e.size(); i++) {
                if (i != 0) {
                    of << ", ";
                }
                of << "arg" << i;
            }
            of << " };\n";
            of << "\t\tRETURN\n";
            of << "\t}\n";
            of << "}";
        }

        void emitConstructorStruct(const Span& sp, const ::HIR::GenericPath& p, const ::HIR::Struct& item) override {
            TRACE_FUNCTION_F(p);
            ::HIR::TypeRef tmp;
            MonomorphStatePtr ms(crate.types, nullptr, &p.mParams, nullptr);
            auto monomorph = [&](const auto& x) {
                return mResolve.monomorphExpandOpt(sp, tmp, x, ms);
            };
            // Create constructor function
            const auto& e = item.mData.as_Tuple();
            of << "/* " << p << " */\n";
            of << "fn " << fmt(p) << "(";
            for (unsigned int i = 0; i < e.size(); i++) {
                if (i != 0) {
                    of << ", ";
                }
                of << fmt(monomorph(e[i].ent));
            }
            of << "): " << fmt(p) << " {\n";
            of << "\t0: {\n";
            of << "\t\tASSIGN RETURN = { ";
            for (unsigned int i = 0; i < e.size(); i++) {
                if (i != 0) {
                    of << ", ";
                }
                of << "arg" << i;
            }
            of << " }: " << fmt(p) << ";\n";
            of << "\t\tRETURN\n";
            of << "\t}\n";
            of << "}\n";
        }

        void emitUnion(const Span& sp, const ::HIR::GenericPath& p, const ::HIR::Union& item) override {
            MIRFunction emptyFcn;
            MIRTypeResolve topMirRes {
                sp, mResolve, FMT_CB(ss, ss << "union " << p;), ::HIR::TypeRef(), {}, emptyFcn
            };
            mirRes = &topMirRes;

            TRACE_FUNCTION_F(p);
            ::HIR::TypeRef ty = crate.types.path(p.clone(), &item);

            bool hasDropGlue = mResolve.typeNeedsDropGlue(sp, ty);
            auto dropGluePath = ::HIR::Path(ty, "#drop_glue");

            const auto* repr = TargetGetTypeRepr(sp, mResolve, ty);
            MIR_ASSERT(*mirRes, repr, "No repr for union " << ty);
            of << "type " << fmt(p) << " {\n";
            of << "\tSIZE " << repr->size << ", ALIGN " << repr->align << ";\n";
            if (hasDropGlue) {
                of << "\tDROP " << fmt(dropGluePath) << ";\n";
            }
            for (const auto& e : repr->fields) {
                of << "\t" << e.offset << " = " << fmt(e.ty) << ";\n";
            }
            of << "}\n";
        }

        void emitEnum(const Span& sp, const ::HIR::GenericPath& p, const ::HIR::Enum& item) override {
            MIRFunction emptyFcn;
            MIRTypeResolve topMirRes {
                sp, mResolve, FMT_CB(ss, ss << "enum " << p;), ::HIR::TypeRef(), {}, emptyFcn
            };
            mirRes = &topMirRes;

            TRACE_FUNCTION_F(p);
            ::HIR::TypeRef ty = crate.types.path(p.clone(), &item);

            // Generate the drop glue (and determine if there is any)
            bool hasDropGlue = mResolve.typeNeedsDropGlue(sp, ty);
            auto dropGluePath = ::HIR::Path(ty, "#drop_glue");

            const auto* repr = TargetGetTypeRepr(sp, mResolve, ty);
            MIR_ASSERT(*mirRes, repr, "No repr for enum " << ty);
            of << "type " << fmt(p) << " {\n";
            of << "\tSIZE " << repr->size << ", ALIGN " << repr->align << ";\n";
            if (hasDropGlue) {
                of << "\tDROP " << fmt(dropGluePath) << ";\n";
            }
            for (const auto& e : repr->fields) {
                of << "\t" << e.offset << " = " << fmt(e.ty) << ";\n";
            }

            auto emitValue = [&](const TypeRepr::FieldPath& path, U128 v) {
                of << "\"";
                for (size_t i = 0; i < path.size; i++) {
                    int val = ((v >> (i * 8)) & U128(0xFF)).truncateU64();
                    if (val < 16) {
                        of << ::std::hex << "\\x0" << val << ::std::dec;
                    } else {
                        of << ::std::hex << "\\x" << val << ::std::dec;
                    }
                }
                of << "\"";
            };

            switch (repr->variants.tag()) {
                case TypeRepr::VariantMode::TAGDEAD:
                    throw "";
                    TU_ARM(repr->variants, None, _e) {
                    }
                    TU_ARM(repr->variants, Linear, e) {
                        of << "\t@[" << e.field.index << ", " << e.field.subFields << "] = {\n";
                        for (size_t i = 0; i < e.numVariants; i++) {
                            of << "\t\t";

                            if (e.isNiche(i)) {
                                of << "*";
                            } else {
                                emitValue(e.field, U128(e.offset + i));
                            }
                            // - Data field number (optional)
                            if (!item.isValue()) {
                                of << " =" << i;
                            }
                            of << ",\n";
                        }
                        of << "\t\t}\n";
                    }
                    TU_ARM(repr->variants, Values, e) {
                        of << "\t@[" << e.field.index << ", " << e.field.subFields << "] = {\n";
                        for (size_t idx = 0; idx < e.values.size(); idx++) {
                            of << "\t\t";
                            // - Tag value
                            emitValue(e.field, e.values[idx]);
                            // - Data field number (optional)
                            if (!item.isValue()) {
                                of << " =" << idx;
                            }
                            of << ",\n";
                        }
                        of << "\t}\n";
                    }
                    TU_ARM(repr->variants, NonZero, e) {
                        of << "\t@[" << e.field.index << ", " << e.field.subFields << "] = { ";
                        for (size_t i = 0; i < 2; i++) {
                            if (i == 1) {
                                of << ", ";
                            }

                            if (e.zeroVariant == i) {
                                of << "\"";
                                for (size_t i = 0; i < e.field.size; i++) {
                                    of << "\\0";
                                }
                                of << "\"";
                            } else {
                                of << "* =" << i;
                            }
                        }
                        of << " }\n";
                    }
            }
            of << "}\n";

            mirRes = nullptr;
        }

        void emitStrByte(uint8_t b) {
            if (b == 0) {
                of << "\\0";
            } else if (b == '\\') {
                of << "\\\\";
            } else if (b == '"') {
                of << "\\\"";
            } else if (' ' <= b && b <= 'z' && b != '\\') {
                of << b;
            } else if (b < 16) {
                of << "\\x0" << ::std::hex << int(b) << ::std::dec;
            } else {
                of << "\\x" << ::std::hex << int(b) << ::std::dec;
            }
        }

        void emitStaticLocal(const ::HIR::Path& p, const ::HIR::Static& item, const TransParams& params, const EncodedLiteral& encoded) override {
            MIRFunction emptyFcn;
            MIRTypeResolve topMirRes {
                sp, mResolve, FMT_CB(ss, ss << "static " << p;), ::HIR::TypeRef(), {}, emptyFcn
            };
            mirRes = &topMirRes;

            TRACE_FUNCTION_F(p);

            auto type = params.monomorph(mResolve, item.mType);

            of << "static " << fmt(p) << ": " << fmt(type) << " = \"";
            for (auto b : encoded.bytes) {
                emitStrByte(b);
            }
            of << "\"";
            of << "{";
            for (const auto& r : encoded.relocations) {
                of << "@" << r.ofs << "+" << r.len << " = ";
                if (r.p) {
                    of << fmt(*r.p);
                } else {
                    of << "\"" << FmtEscaped(r.bytes) << "\"";
                }
                of << ",";
            }
            of << "}";
            of << ";\n";

            mirRes = nullptr;
        }

        void emitFunctionExt(const ::HIR::Path& p, const ::HIR::Function& item, const TransParams& params) override {
            MIRFunction emptyFcn;
            MIRTypeResolve topMirRes {
                sp, mResolve, FMT_CB(ss, ss << "extern fn " << p;), ::HIR::TypeRef(), {}, emptyFcn
            };
            mirRes = &topMirRes;
            TRACE_FUNCTION_F(p);

            // If the function is a C external, emit as such
            if (item.linkage.name != "") {
                ::HIR::TypeRef retTypeTmp;
                const auto& retType = monomorphiseFcnReturn(retTypeTmp, item, params);

                of << "/* " << p << " */\n";
                of << "fn " << fmt(p) << "(";
                for (unsigned int i = 0; i < item.mArgs.size(); i++) {
                    if (i != 0) {
                        of << ", ";
                    }
                    of << fmt(params.monomorph(mResolve, item.mArgs[i].second));
                }
                of << "): " << fmt(retType) << " = \"" << item.linkage.name << "\":\"" << item.mAbi << "\";\n";
            }

            mirRes = nullptr;
        }

        void emitFunctionCode(const ::HIR::Path& p, const ::HIR::Function& item, const TransParams& params, bool isExternDef, const MIRFunctionPointer& code) override {
            TRACE_FUNCTION_F(p);

            MIRTypeResolve::argsT argTypes;
            for (const auto& ent : item.mArgs) {
                argTypes.push_back(::std::make_pair(::HIR::Pattern{}, params.monomorph(mResolve, ent.second)));
            }

            ::HIR::TypeRef retTypeTmp;
            const auto& retType = monomorphiseFcnReturn(retTypeTmp, item, params);

            MIRTypeResolve localMirRes {
                sp, mResolve, FMT_CB(ss, ss << p;), retType, argTypes, *code
            };
            mirRes = &localMirRes;

            // - Signature
            of << "/* " << p << " */\n";
            of << "fn " << fmt(p) << "(";
            for (unsigned int i = 0; i < item.mArgs.size(); i++) {
                if (i != 0) {
                    of << ", ";
                }
                of << fmt(params.monomorph(mResolve, item.mArgs[i].second));
            }
            of << "): " << fmt(retType);
            if (item.linkage.name != "") {
                of << " = \"" << item.linkage.name << "\":\"" << item.mAbi << "\"";
            }
            of << " {\n";
            // - Locals
            for (unsigned int i = 0; i < code->locals.size(); i++) {
                DEBUG("var" << i << " : " << code->locals[i]);
                of << "\tlet var" << i << ": " << fmt(code->locals[i]) << ";\n";
            }
            for (unsigned int i = 0; i < code->dropFlags.size(); i++) {
                of << "\tlet df" << i << " = " << code->dropFlags[i] << ";\n";
            }

            for (unsigned int i = 0; i < code->blocks.size(); i++) {
                TRACE_FUNCTION_F(p << " bb" << i);

                of << "\t" << i << ": {\n";

                for (const auto& stmt : code->blocks[i].statements) {
                    of << "\t\t";
                    localMirRes.setCurStmt(i, (&stmt - &code->blocks[i].statements.front()));
                    DEBUG(stmt);
                    switch (stmt.tag()) {
                        case MIRStatement::TAGDEAD:
                            throw "";
                            TU_ARM(stmt, Assign, se) {
                                of << "ASSIGN " << fmt(se.dst) << " = ";
                                switch (se.src.tag()) {
                                    case MIRRValue::TAGDEAD:
                                        throw "";
                                        TU_ARM(se.src, Use, e)
                                        of << "=" << fmt(e);
                                        break;
                                        TU_ARM(se.src, Constant, e)
                                        of << fmt(e);
                                        break;
                                        TU_ARM(se.src, SizedArray, e)
                                        of << "[" << fmt(e.val) << "; " << e.count << "]";
                                        break;
                                        TU_ARM(se.src, Borrow, e) {
                                            of << "&";
                                            switch (e.type) {
                                                case ::HIR::BorrowType::Shared:
                                                    break;
                                                case ::HIR::BorrowType::Unique:
                                                    of << "mut ";
                                                    break;
                                                case ::HIR::BorrowType::Owned:
                                                    of << "move ";
                                                    break;
                                            }
                                            of << fmt(e.val);
                                        }
                                        break;
                                        TU_ARM(se.src, Cast, e)
                                        of << "CAST " << fmt(e.val) << " as " << fmt(e.type);
                                        break;
                                        TU_ARM(se.src, BinOp, e) {
                                            of << "BINOP " << fmt(e.valL) << " ";
                                            switch (e.op) {
                                                case MIRBinOp::ADD:
                                                    of << "+";
                                                    break;
                                                case MIRBinOp::ADD_OV:
                                                    of << "+^";
                                                    break;
                                                case MIRBinOp::SUB:
                                                    of << "-";
                                                    break;
                                                case MIRBinOp::SUB_OV:
                                                    of << "-^";
                                                    break;
                                                case MIRBinOp::MUL:
                                                    of << "*";
                                                    break;
                                                case MIRBinOp::MUL_OV:
                                                    of << "*^";
                                                    break;
                                                case MIRBinOp::DIV:
                                                    of << "/";
                                                    break;
                                                case MIRBinOp::DIV_OV:
                                                    of << "/^";
                                                    break;
                                                case MIRBinOp::MOD:
                                                    of << "%";
                                                    break;
                                                case MIRBinOp::BIT_OR:
                                                    of << "|";
                                                    break;
                                                case MIRBinOp::BIT_AND:
                                                    of << "&";
                                                    break;
                                                case MIRBinOp::BIT_XOR:
                                                    of << "^";
                                                    break;
                                                case MIRBinOp::BIT_SHR:
                                                    of << ">>";
                                                    break;
                                                case MIRBinOp::BIT_SHL:
                                                    of << "<<";
                                                    break;
                                                case MIRBinOp::NE:
                                                    of << "!=";
                                                    break;
                                                case MIRBinOp::EQ:
                                                    of << "==";
                                                    break;
                                                case MIRBinOp::GT:
                                                    of << ">";
                                                    break;
                                                case MIRBinOp::GE:
                                                    of << ">=";
                                                    break;
                                                case MIRBinOp::LT:
                                                    of << "<";
                                                    break;
                                                case MIRBinOp::LE:
                                                    of << "<=";
                                                    break;
                                            }
                                            of << " " << fmt(e.valR);
                                        }
                                        break;
                                        TU_ARM(se.src, UniOp, e) {
                                            of << "UNIOP ";
                                            switch (e.op) {
                                                case MIRUniOp::INV:
                                                    of << "!";
                                                    break;
                                                case MIRUniOp::NEG:
                                                    of << "-";
                                                    break;
                                            }
                                            of << " " << fmt(e.val);
                                        }
                                        break;
                                        TU_ARM(se.src, DstMeta, e)
                                        of << "DSTMETA " << fmt(e.val);
                                        break;
                                        TU_ARM(se.src, DstPtr, e)
                                        of << "DSTPTR " << fmt(e.val);
                                        break;
                                        TU_ARM(se.src, MakeDst, e)
                                        of << "MAKEDST " << fmt(e.ptrVal) << ", " << fmt(e.metaVal);
                                        break;
                                        TU_ARM(se.src, UnionVariant, e)
                                        of << "UNION " << fmt(e.path) << " " << e.index << " " << fmt(e.val);
                                        break;
                                        TU_ARM(se.src, EnumVariant, e) {
                                            of << "ENUM " << fmt(e.path) << " " << e.index << " { ";
                                            for (const auto& v : e.vals) {
                                                of << fmt(v) << ", ";
                                            }
                                            of << "}";
                                        }
                                        break;
                                        TU_ARM(se.src, Array, e) {
                                            of << "[ ";
                                            for (const auto& v : e.vals) {
                                                of << fmt(v) << ", ";
                                            }
                                            of << "]";
                                        }
                                        break;
                                        TU_ARM(se.src, Tuple, e) {
                                            of << "( ";
                                            for (const auto& v : e.vals) {
                                                of << fmt(v) << ", ";
                                            }
                                            of << ")";
                                        }
                                        break;
                                        TU_ARM(se.src, Struct, e) {
                                            of << "{ ";
                                            for (const auto& v : e.vals) {
                                                of << fmt(v) << ", ";
                                            }
                                            of << "}: " << fmt(e.path);
                                        }
                                        break;
                                }
                            }
                            break;
                            TU_ARM(stmt, SetDropFlag, se) {
                                of << "SETFLAG df" << se.idx << " = ";
                                if (se.other == ~0u) {
                                    of << se.newVal;
                                } else {
                                    of << (se.newVal ? "" : "!") << "df" << se.other;
                                }
                            }
                            break;
                            TU_ARM(stmt, LoadDropFlag, se) {
                                of << "LOADFLAG df" << se.idx << " = " << fmt(se.slot) << " BIT " << se.bitIndex;
                            }
                            break;
                            TU_ARM(stmt, SaveDropFlag, se) {
                                of << "SAVEFLAG " << fmt(se.slot) << " BIT " << se.bitIndex << " = df" << se.idx;
                            }
                            break;
                            TU_ARM(stmt, Asm, se) {
                                of << "ASM (";
                                for (const auto& v : se.outputs) {
                                    of << "\"" << v.first << "\" : " << fmt(v.second) << ", ";
                                }
                                of << ") = \"" << FmtEscaped(se.tpl) << "\"(";
                                for (const auto& v : se.inputs) {
                                    of << "\"" << v.first << "\" : " << fmt(v.second) << ", ";
                                }
                                of << ") [";

                                for (const auto& v : se.clobbers) {
                                    of << "\"" << v << "\", ";
                                }
                                of << ":" << se.flags << "]";
                            }
                            break;
                            TU_ARM(stmt, Asm2, se) {
                                of << "ASM2 (";
                                for (const auto& l : se.lines) {
                                    of << l;
                                }
                                for (const auto& p : se.params) {
                                    of << ", ";
                            TU_MATCH_HDRA((p), {)
                            TU_ARMA(Const, v)
                                of << "const " << fmt(v);
                                        TU_ARMA(Sym, v)
                                        of << "sym " << fmt(v);
                                        TU_ARMA(Reg, v) {
                                            of << "reg(" << v.dir << " " << v.spec << ") ";
                                            if (v.input) {
                                                of << fmt(*v.input);
                                            } else {
                                                of << "_";
                                            }
                                            of << " => ";
                                            if (v.output) {
                                                of << fmt(*v.output);
                                            } else {
                                                of << "_";
                                            }
                                        }
                            }
                                }
                                of << ", ";
                                se.options.fmt(of);
                                of << ")";
                            }
                            break;
                            TU_ARM(stmt, ScopeEnd, se) {
                                (void)se;
                                continue;
                            }
                            break;
                    }
                    of << ";\n";
                }

                localMirRes.setCurStmtTerm(i);
                const auto& term = code->blocks[i].terminator;
                DEBUG("- " << term);
                of << "\t\t";
                switch (term.tag()) {
                    case MIRTerminator::TAGDEAD:
                        throw "";
                        TU_ARM(term, Incomplete, _e)(void) _e;
                        of << "INCOMPLETE\n";
                        break;
                        TU_ARM(term, Return, _e)(void) _e;
                        of << "RETURN\n";
                        break;
                        TU_ARM(term, UnwindResume, _e)(void) _e;
                        of << "UNWIND RESUME\n";
                        break;
                        TU_ARM(term, UnwindTerminate, _e)(void) _e;
                        of << "UNWIND TERMINATE\n";
                        break;
                        TU_ARM(term, Unreachable, _e)(void) _e;
                        of << "UNREACHABLE\n";
                        break;
                        TU_ARM(term, Goto, e)
                        of << "GOTO " << e << "\n";
                        break;
                        TU_ARM(term, If, e)
                        of << "IF " << fmt(e.cond) << " goto " << e.bbTrue << " else " << e.bbFalse << "\n";
                        break;
                        TU_ARM(term, Switch, e) {
                            of << "SWITCH " << fmt(e.val) << " { ";
                            of << e.targets;
                            of << " }\n";
                        }
                        break;
                        TU_ARM(term, SwitchValue, e) {
                            of << "SWITCHVALUE " << fmt(e.val) << " { ";
                            switch (e.values.tag()) {
                                case MIRSwitchValues::TAGDEAD:
                                    throw "";
                                    TU_ARM(e.values, String, ve)
                                    for (size_t i = 0; i < ve.size(); i++) {
                                        of << "\"" << FmtEscaped(ve[i]) << "\" = " << e.targets[i] << ",";
                                    }
                                    break;
                                    TU_ARM(e.values, ByteString, ve) {
                                        for (size_t j = 0; j < ve.size(); j++) {
                                            of << "b\"";
                                            for (size_t i = 0; i < ve[j].size(); i++) {
                                                auto b = ve[j][i];
                                                switch (b) {
                                                    case '\\':
                                                        of << "\\\\";
                                                        break;
                                                    case '\"':
                                                        of << "\\\"";
                                                        break;
                                                    default:
                                                        if (' ' <= b && b < 0x7f) {
                                                            of << char(ve[j][i]);
                                                        } else {
                                                            of << "\\x";
                                                            of << "0123456789ABCDEF"[b >> 4];
                                                            of << "0123456789ABCDEF"[b & 15];
                                                        }
                                                        break;
                                                }
                                            }
                                            of << "\" = " << e.targets[i] << ",";
                                        }
                                    }
                                    break;
                                    TU_ARM(e.values, Unsigned, ve)
                                    for (size_t i = 0; i < ve.size(); i++) {
                                        of << ve[i] << " = " << e.targets[i] << ",";
                                    }
                                    break;
                                    TU_ARM(e.values, Signed, ve)
                                    for (size_t i = 0; i < ve.size(); i++) {
                                        of << (ve[i] < 0 ? "" : "+") << ve[i] << " = " << e.targets[i] << ",";
                                    }
                                    break;
                            }
                            // TODO: Values.
                            //if( e.values.size() > 0 )
                            //{
                            //    m_of << ", ";
                            //}
                            of << "_ = " << e.defTarget;
                            of << " }\n";
                        }
                        break;
                        TU_ARM(term, Drop, e) {
                            of << "DROP " << fmt(e.slot);
                            if (e.kind == MIRDropKind::SHALLOW) of << " SHALLOW";
                            if (e.flagIdx != ~0u) of << " IF df" << e.flagIdx;
                            of << " goto " << e.target << " unwind " << e.unwind.tagStr() << "\n";
                        }
                        break;
                        TU_ARM(term, Call, e) {
                            if (const auto* fP = e.fcn.opt_Intrinsic()) {
                                if (fP->name == "offset_of") {
                                    size_t val = localMirRes.intrinsicOffsetOf(fP->params.types.at(0), e.args);
                                    of << fmt(e.retVal) << " = " << val << " usize;\n";
                                    of << "\t\tGOTO " << e.retBlock;
                                    break;
                                }
                            }
                            of << "CALL " << fmt(e.retVal) << " = ";
                            switch (e.fcn.tag()) {
                                case MIRCallTarget::TAGDEAD:
                                    throw "";
                                    TU_ARM(e.fcn, Intrinsic, f) {
                                        of << "\"" << f.name << "\"";
                                        if (f.params.types.size() > 0) {
                                            of << "<";
                                            for (const auto& t : f.params.types) {
                                                of << fmt(t) << ",";
                                            }
                                            of << ">";
                                        }
                                    }
                                    break;
                                    TU_ARM(e.fcn, Value, f) of << "(" << fmt(f) << ")";
                                    break;
                                    TU_ARM(e.fcn, Path, f) of << fmt(f);
                                    break;
                            }
                            of << "(";
                            for (const auto& a : e.args) {
                                of << fmt(a) << ", ";
                            }
                            of << ") goto " << e.retBlock << " unwind " << e.unwind.tagStr() << "\n";
                        }
                        break;
                }
                of << "\t}\n";
            }

            of << "}\n";

            mirRes = nullptr;
        }

        void emitGlobalAsm(const ::HIR::GlobalAssembly&) override {
            TODO(Span(), "global_asm! codegen");
        }

    private:
        const ::HIR::TypeData* monomorphiseFcnReturn(::HIR::TypeRef& tmp, const ::HIR::Function& item, const TransParams& params) {
            bool hasErased = visitTyWith(item.returnType, [&](const auto& x) {
                return x->is_ErasedType();
            });

            if (hasErased || monomorphiseTypeNeeded(item.returnType)) {
                // If there's an erased type, make a copy with the erased type expanded
                if (hasErased) {
                    tmp = cloneTyWith(crate.types, sp, item.returnType, [&](const auto& x, auto& out) {
                        if (const auto* te = x->opt_ErasedType()) {
                            if (const auto* e = te->inner.opt_Fcn()) {
                                out = item.mCode.erasedTypes.at(e->index);
                                return true;
                            }
                        }
                        return false;
                    });
                    tmp = params.monomorphType(Span(), tmp);
                } else {
                    tmp = params.monomorphType(Span(), item.returnType);
                }
                mResolve.expandAssociatedTypes(Span(), tmp);
                return tmp;
            } else {
                return item.returnType;
            }
        }
    };

    Span CodeGeneratorMonoMir::sp;
}

::std::unique_ptr<CodeGenerator> TransCodegenGetGeneratorMonoMir(const ::HIR::Crate& crate, const ::std::string& outfile) {
    return ::std::unique_ptr<CodeGenerator>(new CodeGeneratorMonoMir(crate, outfile));
}

CodeGenerator::~CodeGenerator() {
}
