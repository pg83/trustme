#include "trans_codegen_mir.h"

#include "hir_hir.h"
#include "mir_mir.h"
#include "wire_board.h"
#include "mir_helpers.h"
#include "trans_target.h"
#include "trans_codegen.h"
#include "mir_operations.h"
#include "trans_mangling.h"
#include "trans_trans_list.h"
#include "hir_typeck_static.h"
#include "trans_main_bindings.h"

#include <fstream>
#include <iomanip>
#include <algorithm>

namespace {

    template <typename T>
    struct Fmt {
        const WireBoard& wb;
        const T& e;

        Fmt(const WireBoard& wb, const T& e);
    };

    struct CodeGeneratorMonoMir: public CodeGenerator {
        enum class MetadataType {
            None,
            Slice,
            TraitObject,
        };

        Span sp;

        const HIRCrate& crate;
        const WireBoard& wb_;
        ::StaticTraitResolve resolve_;

        template <typename T>
        Fmt<T> fmt(const T& value) const;

        template <typename T>
        RcString TransMangle(const T& value) const;

        std::string outfilePath;
        std::ofstream of;
        const MIRTypeResolve* mirRes;

        CodeGeneratorMonoMir(const WireBoard& wb, const HIRCrate& crate, const std::string& outfile);

        void finalise(const TransOptions& opt, CodegenOutput outTy, const std::string& hirFile) override;

        void emitType(const HIRTypeData* ty) override;

        // TODO: Move this to a more common location
        MetadataType metadataType(const HIRTypeData* ty) const;

        void emitStruct(const Span& sp, const HIRGenericPath& p, const HIRStruct& item) override;

        void emitConstructorEnum(const Span& sp, const HIRGenericPath& varPath, const HIREnum& item, size_t varIdx) override;

        void emitConstructorStruct(const Span& sp, const HIRGenericPath& p, const HIRStruct& item) override;

        void emitUnion(const Span& sp, const HIRGenericPath& p, const HIRUnion& item) override;

        void emitEnum(const Span& sp, const HIRGenericPath& p, const HIREnum& item) override;

        void emitStrByte(u8 b);

        void emitStaticLocal(const HIRPath& p, const HIRStatic& item, const TransParams& params, const EncodedLiteral& encoded) override;

        void emitFunctionExt(const HIRPath& p, const HIRFunction& item, const TransParams& params) override;

        void emitFunctionCode(const HIRPath& p, const HIRFunction& item, const TransParams& params, bool isExternDef, const MIRFunctionPointer& code, bool hasPrototype) override;

        void emitGlobalAsm(const HIRGlobalAssembly&) override;

        const HIRTypeData* monomorphiseFcnReturn(HIRTypeRef& tmp, const HIRFunction& item, const TransParams& params);
    };

    template <typename T>
    Fmt<T> fmt(const WireBoard& wb, const T& v) {
        return Fmt<T>(wb, v);
    }

    std::ostream& operator<<(std::ostream& os, const Fmt<HIRPath>& x) {
        return os << TransMangle(x.wb, x.e);
    }

    std::ostream& operator<<(std::ostream& os, const Fmt<HIRGenericPath>& x) {
        return os << TransMangle(x.wb, x.e);
    }

    std::ostream& operator<<(std::ostream& os, const Fmt<HIRSimplePath>& x) {
        return os << TransMangle(x.wb, x.e);
    }

    std::ostream& operator<<(std::ostream& os, const Fmt<HIRTypeRef>& x) {
        {
            auto& tuMatch = (*x.e);
            switch (tuMatch.tag()) {
                case HIRTypeData::TAG_Infer: {
                    BUG(Span(), "" << x.e);
                    break;
                }
                case HIRTypeData::TAG_Diverge: {
                    os << "!";
                    break;
                }
                case HIRTypeData::TAG_Primitive: {
                    auto& te = tuMatch.as_Primitive();
                    os << te;
                    break;
                }
                case HIRTypeData::TAG_Path: {
                    auto& te = tuMatch.as_Path();
                    os << TransMangle(x.wb, te.path);
                    break;
                }
                case HIRTypeData::TAG_Generic: {
                    BUG(Span(), "" << x.e);
                    break;
                }
                case HIRTypeData::TAG_TraitObject: {
                    auto& te = tuMatch.as_TraitObject();
                    auto path = te.trait.path.clone();
                    os << "dyn " << TransMangle(x.wb, path);
                    break;
                }
                case HIRTypeData::TAG_ErasedType: {
                    BUG(Span(), "" << x.e);
                    break;
                }
                case HIRTypeData::TAG_Array: {
                    auto& te = tuMatch.as_Array();
                    os << "[" << fmt(x.wb, te.inner) << "; " << te.size << "]";
                    break;
                }
                case HIRTypeData::TAG_Slice: {
                    auto& te = tuMatch.as_Slice();
                    os << "[" << fmt(x.wb, te.inner) << "]";
                    break;
                }
                case HIRTypeData::TAG_Pattern: {
                    auto& te = tuMatch.as_Pattern();
                    os << fmt(x.wb, te.inner);
                    break;
                }
                case HIRTypeData::TAG_Tuple: {
                    auto& te = tuMatch.as_Tuple();
                    if (te.empty()) {
                        os << "()";
                    } else {
                        os << TransMangle(x.wb, x.e);
                    }
                    break;
                }
                case HIRTypeData::TAG_Borrow: {
                    auto& te = tuMatch.as_Borrow();
                    switch (te.type) {
                        case HIRBorrowType::Shared:
                            os << "&";
                            break;
                        case HIRBorrowType::Unique:
                            os << "&mut ";
                            break;
                        case HIRBorrowType::Owned:
                            os << "&move ";
                            break;
                    }
                    os << fmt(x.wb, te.inner);
                    break;
                }
                case HIRTypeData::TAG_Pointer: {
                    auto& te = tuMatch.as_Pointer();
                    switch (te.type) {
                        case HIRBorrowType::Shared:
                            os << "*const ";
                            break;
                        case HIRBorrowType::Unique:
                            os << "*mut ";
                            break;
                        case HIRBorrowType::Owned:
                            os << "*move ";
                            break;
                    }
                    os << fmt(x.wb, te.inner);
                    break;
                }
                case HIRTypeData::TAG_NamedFunction: {
                    auto& te = tuMatch.as_NamedFunction();
                    os << "fn " << TransMangle(x.wb, te.path);
                    break;
                }
                case HIRTypeData::TAG_Function: {
                    auto& e = tuMatch.as_Function();
                    if (e.isUnsafe) {
                        os << "unsafe ";
                    }
                    if (e.abi != "") {
                        os << "extern \"" << e.abi << "\" ";
                    }
                    os << "fn(";
                    for (const auto& t : e.argTypes) {
                        os << fmt(x.wb, t) << ", ";
                    }
                    os << ") -> " << fmt(x.wb, e.rettype);
                    break;
                } break;
                case HIRTypeData::TAG_NodeType:
                    BUG(Span(), "Unexpected type in trans: " << x.e);
                    break;
            }
        }
        return os;
    }

    std::ostream& operator<<(std::ostream& os, const Fmt<MIRLValue>& x) {
        for (const auto& w : ::reverse(x.e.wrappers)) {
            if (w.is_Deref()) {
                os << "(*";
            }
        }
        switch (x.e.root.tag()) {
            case MIRLValue::Storage::TAG_Return: {
                os << "RETURN";
                break;
            }
            case MIRLValue::Storage::TAG_Local: {
                decltype(x.e.root.as_Local()) e = x.e.root.as_Local();
                os << "var" << e;
                break;
            }
            case MIRLValue::Storage::TAG_Argument: {
                decltype(x.e.root.as_Argument()) e = x.e.root.as_Argument();
                os << "arg" << e;
                break;
            }
            case MIRLValue::Storage::TAG_Static: {
                decltype(x.e.root.as_Static()) e = x.e.root.as_Static();
                os << fmt(x.wb, e);
                break;
            }
        }
        bool wasNum = false;
        for (const auto& w : x.e.wrappers) {
            bool prevWasNum = wasNum;
            wasNum = false;
            switch (w.tag()) {
                break;
                case MIRLValue::Wrapper::TAG_Deref: {
                    os << ")";
                } break;
                    break;
                case MIRLValue::Wrapper::TAG_Field: {
                    decltype(w.as_Field()) fieldIndex = w.as_Field();
                    if (prevWasNum) {
                        os << " ";
                    }
                    os << "." << fieldIndex;
                    wasNum = true;

                } break;
                    break;
                case MIRLValue::Wrapper::TAG_Index: {
                    decltype(w.as_Index()) e = w.as_Index();
                    os << "[" << fmt(x.wb, MIRLValue::newLocal(e)) << "]";

                } break;
                    break;
                case MIRLValue::Wrapper::TAG_Downcast: {
                    decltype(w.as_Downcast()) variantIndex = w.as_Downcast();
                    os << "@" << variantIndex;
                    wasNum = true;

                } break;
            }
        }
        return os;
    }

    std::ostream& operator<<(std::ostream& os, const Fmt<MIRConstant>& x) {
        struct H {
            static u64 doubleToU64(double v) {
                u64 rv;
                std::memcpy(&rv, &v, sizeof(double));
                return rv;
            }
        };

        const auto& e = x.e;
        switch (e.tag()) {
            break;
            case MIRConstant::TAG_Int: {
                auto& v = e.as_Int();
                os << (v.v < 0 ? "" : "+") << v.v << " " << v.t;

            } break;
                break;
            case MIRConstant::TAG_Uint: {
                auto& v = e.as_Uint();
                os << v.v << " " << v.t;
            } break;
                break;
            case MIRConstant::TAG_Float: {
                auto& v = e.as_Float();
                // TODO: Infinity/nan/...
                auto vi = H::doubleToU64(static_cast<double>(v.v));
                bool sign = (vi & (1ull << 63)) != 0;
                int exp = (vi >> 52) & 0x7FF;
                u64 frac = vi & ((1ull << 52) - 1);
                os << (sign ? "-" : "+") << "0x1." << std::setw(52 / 4) << std::setfill('0') << std::hex << frac << std::dec << "p" << (exp - 1023);
                os << " " << v.t;

            } break;
                break;
            case MIRConstant::TAG_ItemAddr: {
                auto& v = e.as_ItemAddr();
                os << "ADDROF " << fmt(x.wb, *v);
                if (v.offset != U128(0)) {
                    os << " + " << v.offset;
                }

            } break;
                break;
            case MIRConstant::TAG_Const: {
                BUG(Span(), "Stray named constant in MIR after cleanup - " << e);

            } break;
            default:
                os << e;
                break;
        }
        return os;
    }

    std::ostream& operator<<(std::ostream& os, const Fmt<MIRParam>& x) {
        switch (x.e.tag()) {
            break;
            case MIRParam::TAG_LValue: {
                auto& e = x.e.as_LValue();
                os << fmt(x.wb, e);
            } break;
                break;
            case MIRParam::TAG_Borrow: {
                auto& e = x.e.as_Borrow();
                os << "&";
                switch (e.type) {
                    case HIRBorrowType::Shared:
                        break;
                    case HIRBorrowType::Unique:
                        os << "mut ";
                        break;
                    case HIRBorrowType::Owned:
                        os << "move ";
                        break;
                }
                os << fmt(x.wb, e.val);

            } break;
                break;
            case MIRParam::TAG_Constant: {
                auto& e = x.e.as_Constant();
                os << fmt(x.wb, e);
            } break;
        }
        return os;
    }

}

std::unique_ptr<CodeGenerator> TransCodegenGetGeneratorMonoMir(const WireBoard& wb, const HIRCrate& crate, const std::string& outfile) {
    return std::unique_ptr<CodeGenerator>(new CodeGeneratorMonoMir(wb, crate, outfile));
}

template <typename T>
Fmt<T>::Fmt(const WireBoard& wb, const T& e)
    : wb(wb)
    , e(e)
{
}

template <typename T>
auto CodeGeneratorMonoMir::fmt(const T& value) const -> Fmt<T> {
    return ::fmt(wb_, value);
}

template <typename T>
auto CodeGeneratorMonoMir::TransMangle(const T& value) const -> RcString {
    return ::TransMangle(wb_, value);
}

CodeGeneratorMonoMir::CodeGeneratorMonoMir(const WireBoard& wb, const HIRCrate& crate, const std::string& outfile)
    : crate(crate)
    , wb_(wb)
    , resolve_(wb, OpaqueReveal::All)
    , outfilePath(outfile)
    , of(outfilePath + ".mir")
{
    for (const auto& crateName : crate.extCratesOrdered) {
        of << "crate \"" << FmtEscaped(crate.extCrates.at(crateName).path) << ".mir\";\n";
    }
}

auto CodeGeneratorMonoMir::finalise(const TransOptions& opt, CodegenOutput outTy, const std::string& hirFile) -> void {
    if (outTy == CodegenOutput::Executable) {
        if (!crate.noMain) {
            of << "fn main#(isize, *const *const i8): isize {\n";
            auto cStartPath = resolve_.hirCrate().getLangItemPathOpt("trustme-start");
            if (cStartPath == HIRSimplePath()) {
                auto mainPath = resolve_.hirCrate().getLangItemPath(Span(), "trustme-main");
                const auto& startPath = resolve_.hirCrate().getLangItemPathOpt("start");
                if (crate.isNoCore && startPath == HIRSimplePath()) {
                    const auto& mainFcn = crate.getFunctionByPath(Span(), mainPath);
                    of << "\tlet direct_main_result: " << fmt(mainFcn.returnType) << ";\n";
                    of << "\t0: {\n";
                    of << "\t\tCALL direct_main_result = " << fmt(HIRGenericPath(mainPath)) << "() goto 1 else 1\n";
                } else {
                    of << "\tlet m: fn();\n";
                    of << "\t0: {\n";
                    of << "\t\tASSIGN m = ADDROF " << fmt(HIRGenericPath(mainPath)) << ";\n";
                    of << "\t\tCALL RETURN = " << fmt(HIRGenericPath(resolve_.hirCrate().getLangItemPath(Span(), "start"))) << "(m, arg0, arg1) goto 1 else 1\n";
                }
            } else {
                of << "\t0: {\n";
                of << "\t\tCALL RETURN = " << fmt(HIRGenericPath(cStartPath)) << "(arg0, arg1) goto 1 else 1;\n";
            }
            of << "\t}\n";
            of << "\t1: {\n";
            of << "\t\tRETURN\n";
            of << "\t}\n";
            of << "}\n";
        }

        const auto& panicImplPath = crate.getLangItemPathOpt("trustme-panic_implementation");
        if (panicImplPath != HIRSimplePath()) {
            of << "fn panic_impl#(usize): u32 = \"panic_impl\":\"Rust\" {\n";
            of << "\t0: {\n";
            of << "\t\tCALL RETURN = " << fmt(panicImplPath) << "(arg0) goto 1 else 2\n";
            of << "\t}\n";
            of << "\t1: { RETURN }\n";
            of << "\t2: { DIVERGE }\n";
            of << "}\n";
        } else if (!crate.isNoCore) {
            crate.getLangItemPath(Span(), "trustme-panic_implementation");
        }

        // TODO: OOM impl?
    }

    of.flush();
    of.close();

    {
        std::ofstream of(outfilePath);
        if (!of.good()) {
            // TODO: Error?
        }
    }
}

auto CodeGeneratorMonoMir::emitType(const HIRTypeData* ty) -> void {
    MIRFunction emptyFcn;
    auto pathCallback = makeCallable<MIRPathCb>([&](auto& os) {
        os << "type " << ty;
    });
    MIRTypeResolve topMirRes{sp, resolve_, pathCallback, HIRTypeRef(), {}, emptyFcn};
    mirRes = &topMirRes;

    if (const auto* te = ty->opt_Tuple()) {
        if (te->size() > 0) {
            const auto* repr = TargetGetTypeRepr(sp, resolve_, ty);
            MIR_ASSERT(*mirRes, repr, "No repr for tuple " << ty);

            bool hasDropGlue = resolve_.typeNeedsDropGlue(sp, ty);
            auto dropGluePath = HIRPath(ty, "#drop_glue");

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

auto CodeGeneratorMonoMir::metadataType(const HIRTypeData* ty) const -> MetadataType {
    if ((ty->is_Primitive() && ty->as_Primitive() == HIRCoreType::Str) || ty->is_Slice()) {
        return MetadataType::Slice;
    } else if (ty->is_TraitObject()) {
        return MetadataType::TraitObject;
    } else if (ty->is_Path()) {
        const auto& te = ty->as_Path();
        switch (te.binding.tag()) {
            break;
            case HIRTypePathBinding::TAG_Struct: {
                auto& tpb = te.binding.as_Struct();
                {
                    switch (tpb->structMarkings.dstType) {
                        case HIRStructMarkings::DstType::None:
                            return MetadataType::None;
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
                                    MIR_BUG(*mirRes, "Unit-like struct with DstType::Possible");
                                    break;
                                }
                                case HIRStructData::TAG_Tuple: {
                                    auto& se = str.data.as_Tuple();
                                    return metadataType(monomorph(se.back().ent));
                                }
                                case HIRStructData::TAG_Named: {
                                    auto& se = str.data.as_Named();
                                    return metadataType(monomorph(se.back().ty));
                                }
                            }
                            return MetadataType::None;
                        }
                        case HIRStructMarkings::DstType::Slice:
                            return MetadataType::Slice;
                        case HIRStructMarkings::DstType::TraitObject:
                            return MetadataType::TraitObject;
                    }
                    UNREACHABLE();
                }
            } break;
            case HIRTypePathBinding::TAG_Union: {
                return MetadataType::None;
            }
            case HIRTypePathBinding::TAG_Enum: {
                return MetadataType::None;
            }
            default:
                MIR_BUG(*mirRes, "Unbound/opaque path in trans - " << ty);
        }
        UNREACHABLE();
    } else {
        return MetadataType::None;
    }
}

auto CodeGeneratorMonoMir::emitStruct(const Span& sp, const HIRGenericPath& p, const HIRStruct& item) -> void {
    MIRFunction emptyFcn;
    auto pathCallback = makeCallable<MIRPathCb>([&](auto& os) {
        os << "struct " << p;
    });
    MIRTypeResolve topMirRes{sp, resolve_, pathCallback, HIRTypeRef(), {}, emptyFcn};
    mirRes = &topMirRes;

    auto dropGluePath = HIRPath(crate.types.path(p.clone(), &item), "#drop_glue");

    HIRTypeRef ty = crate.types.path(p.clone(), &item);

    struct H {
        static HIRTypeRef getMetadataType(const Span& sp, const ::StaticTraitResolve& resolve, const TypeRepr& r) {
            ASSERT_BUG(sp, r.fields.size() > 0, "");
            auto& t = r.fields.back().ty;
            if (t->is_Primitive() && t->as_Primitive() == HIRCoreType::Str) {
                return resolve.hirCrate().types.primitive(HIRCoreType::Usize);
            } else if (t->is_Slice()) {
                return resolve.hirCrate().types.primitive(HIRCoreType::Usize);
            } else if (t->is_TraitObject()) {
                const auto& te = t->as_TraitObject();

                const auto& trait = resolve.hirCrate().getTraitByPath(sp, te.trait.path.path);
                auto vtableTy = trait.getVtableType(sp, resolve.hirCrate(), te);
                return resolve.hirCrate().types.pointer(HIRBorrowType::Shared, vtableTy);
            } else if (t->is_Path() && t->as_Path().binding.is_ExternType()) {
                return resolve.hirCrate().types.unit();
            } else if (t->is_Path()) {
                auto* repr = TargetGetTypeRepr(sp, resolve, t);
                ASSERT_BUG(sp, repr, "No repr for " << t);
                return getMetadataType(sp, resolve, *repr);
            } else {
                BUG(sp, "Unexpected type in get_metadata_type - " << t);
            }
        }
    };

    bool hasDropGlue = resolve_.typeNeedsDropGlue(sp, ty);

    const auto* repr = TargetGetTypeRepr(sp, resolve_, ty);
    MIR_ASSERT(*mirRes, repr, "No repr for struct " << ty);
    of << "type " << TransMangle(p) << " {\n";
    of << "\tSIZE " << repr->size << ", ALIGN " << repr->align << ";\n";
    if (repr->size == SIZE_MAX) {
        of << "\tDSTMETA " << H::getMetadataType(sp, resolve_, *repr) << ";\n";
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

auto CodeGeneratorMonoMir::emitConstructorEnum(const Span& sp, const HIRGenericPath& varPath, const HIREnum& item, size_t varIdx) -> void {
    HIRTypeRef tmp;
    MonomorphStatePtr ms(crate.types, nullptr, &varPath.params, nullptr);
    auto monomorph = [&](const auto& x) {
        return resolve_.monomorphExpandOpt(sp, tmp, x, ms);
    };

    auto enumPath = varPath.clone();
    enumPath.path.popComponent();

    const auto& varTy = item.data.as_Data().at(varIdx).type;
    const auto& e = varTy->as_Path().binding.as_Struct()->data.as_Tuple();
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

auto CodeGeneratorMonoMir::emitConstructorStruct(const Span& sp, const HIRGenericPath& p, const HIRStruct& item) -> void {
    HIRTypeRef tmp;
    MonomorphStatePtr ms(crate.types, nullptr, &p.params, nullptr);
    auto monomorph = [&](const auto& x) {
        return resolve_.monomorphExpandOpt(sp, tmp, x, ms);
    };
    const auto& e = item.data.as_Tuple();
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

auto CodeGeneratorMonoMir::emitUnion(const Span& sp, const HIRGenericPath& p, const HIRUnion& item) -> void {
    MIRFunction emptyFcn;
    auto pathCallback = makeCallable<MIRPathCb>([&](auto& os) {
        os << "union " << p;
    });
    MIRTypeResolve topMirRes{sp, resolve_, pathCallback, HIRTypeRef(), {}, emptyFcn};
    mirRes = &topMirRes;

    HIRTypeRef ty = crate.types.path(p.clone(), &item);

    bool hasDropGlue = resolve_.typeNeedsDropGlue(sp, ty);
    auto dropGluePath = HIRPath(ty, "#drop_glue");

    const auto* repr = TargetGetTypeRepr(sp, resolve_, ty);
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

auto CodeGeneratorMonoMir::emitEnum(const Span& sp, const HIRGenericPath& p, const HIREnum& item) -> void {
    MIRFunction emptyFcn;
    auto pathCallback = makeCallable<MIRPathCb>([&](auto& os) {
        os << "enum " << p;
    });
    MIRTypeResolve topMirRes{sp, resolve_, pathCallback, HIRTypeRef(), {}, emptyFcn};
    mirRes = &topMirRes;

    HIRTypeRef ty = crate.types.path(p.clone(), &item);

    bool hasDropGlue = resolve_.typeNeedsDropGlue(sp, ty);
    auto dropGluePath = HIRPath(ty, "#drop_glue");

    const auto* repr = TargetGetTypeRepr(sp, resolve_, ty);
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
                of << std::hex << "\\x0" << val << std::dec;
            } else {
                of << std::hex << "\\x" << val << std::dec;
            }
        }
        of << "\"";
    };

    switch (repr->variants.tag()) {
        break;
        case TypeReprVariantMode::TAG_None: {
        } break;
        case TypeReprVariantMode::TAG_Linear: {
            auto& e = repr->variants.as_Linear();
            of << "\t@[" << e.field.index << ", " << e.field.subFields << "] = {\n";
            for (size_t i = 0; i < e.numVariants; i++) {
                of << "\t\t";

                if (e.isNiche(i)) {
                    of << "*";
                } else {
                    emitValue(e.field, U128(e.tagValue(i)));
                }
                if (!item.isValue()) {
                    of << " =" << i;
                }
                of << ",\n";
            }
            of << "\t\t}\n";

        } break;
        case TypeReprVariantMode::TAG_Values: {
            auto& e = repr->variants.as_Values();
            of << "\t@[" << e.field.index << ", " << e.field.subFields << "] = {\n";
            for (size_t idx = 0; idx < e.values.size(); idx++) {
                of << "\t\t";
                emitValue(e.field, e.values[idx]);
                if (!item.isValue()) {
                    of << " =" << idx;
                }
                of << ",\n";
            }
            of << "\t}\n";

        } break;
        case TypeReprVariantMode::TAG_NonZero: {
            auto& e = repr->variants.as_NonZero();
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

auto CodeGeneratorMonoMir::emitStrByte(u8 b) -> void {
    if (b == 0) {
        of << "\\0";
    } else if (b == '\\') {
        of << "\\\\";
    } else if (b == '"') {
        of << "\\\"";
    } else if (' ' <= b && b <= 'z' && b != '\\') {
        of << b;
    } else if (b < 16) {
        of << "\\x0" << std::hex << int(b) << std::dec;
    } else {
        of << "\\x" << std::hex << int(b) << std::dec;
    }
}

auto CodeGeneratorMonoMir::emitStaticLocal(const HIRPath& p, const HIRStatic& item, const TransParams& params, const EncodedLiteral& encoded) -> void {
    MIRFunction emptyFcn;
    auto pathCallback = makeCallable<MIRPathCb>([&](auto& os) {
        os << "static " << p;
    });
    MIRTypeResolve topMirRes{sp, resolve_, pathCallback, HIRTypeRef(), {}, emptyFcn};
    mirRes = &topMirRes;

    auto type = params.monomorph(resolve_, item.type);

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

auto CodeGeneratorMonoMir::emitFunctionExt(const HIRPath& p, const HIRFunction& item, const TransParams& params) -> void {
    MIRFunction emptyFcn;
    auto pathCallback = makeCallable<MIRPathCb>([&](auto& os) {
        os << "extern fn " << p;
    });
    MIRTypeResolve topMirRes{sp, resolve_, pathCallback, HIRTypeRef(), {}, emptyFcn};
    mirRes = &topMirRes;

    if (item.linkage.name != "") {
        HIRTypeRef retTypeTmp;
        const auto& retType = monomorphiseFcnReturn(retTypeTmp, item, params);

        of << "/* " << p << " */\n";
        of << "fn " << fmt(p) << "(";
        for (unsigned int i = 0; i < item.args.size(); i++) {
            if (i != 0) {
                of << ", ";
            }
            of << fmt(params.monomorph(resolve_, item.args[i].second));
        }
        of << "): " << fmt(retType) << " = \"" << item.linkage.name << "\":\"" << item.abi << "\";\n";
    }

    mirRes = nullptr;
}

auto CodeGeneratorMonoMir::emitFunctionCode(const HIRPath& p, const HIRFunction& item, const TransParams& params, bool isExternDef, const MIRFunctionPointer& code, bool hasPrototype) -> void {
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

    of << "/* " << p << " */\n";
    of << "fn " << fmt(p) << "(";
    for (unsigned int i = 0; i < item.args.size(); i++) {
        if (i != 0) {
            of << ", ";
        }
        of << fmt(params.monomorph(resolve_, item.args[i].second));
    }
    of << "): " << fmt(retType);
    if (item.linkage.name != "") {
        of << " = \"" << item.linkage.name << "\":\"" << item.abi << "\"";
    }
    of << " {\n";
    for (unsigned int i = 0; i < code->locals.size(); i++) {
        of << "\tlet var" << i << ": " << fmt(code->locals[i]) << ";\n";
    }
    for (unsigned int i = 0; i < code->dropFlags.size(); i++) {
        of << "\tlet df" << i << " = " << code->dropFlags[i] << ";\n";
    }

    for (unsigned int i = 0; i < code->blocks.size(); i++) {
        of << "\t" << i << ": {\n";

        for (const auto& stmt : code->blocks[i].statements) {
            of << "\t\t";
            localMirRes.setCurStmt(i, (&stmt - &code->blocks[i].statements.front()));
            switch (stmt.tag()) {
                break;
                case MIRStatement::TAG_Assign: {
                    auto& se = stmt.as_Assign();
                    of << "ASSIGN " << fmt(se.dst) << " = ";
                    switch (se.src.tag()) {
                        break;
                        case MIRRValue::TAG_Use: {
                            auto& e = se.src.as_Use();
                            of << "=" << fmt(e);
                        } break;
                            break;
                        case MIRRValue::TAG_Constant: {
                            auto& e = se.src.as_Constant();
                            of << fmt(e);
                        } break;
                            break;
                        case MIRRValue::TAG_SizedArray: {
                            auto& e = se.src.as_SizedArray();
                            of << "[" << fmt(e.val) << "; " << e.count << "]";
                        } break;
                            break;
                        case MIRRValue::TAG_Borrow: {
                            auto& e = se.src.as_Borrow();
                            of << "&";
                            switch (e.type) {
                                case HIRBorrowType::Shared:
                                    break;
                                case HIRBorrowType::Unique:
                                    of << "mut ";
                                    break;
                                case HIRBorrowType::Owned:
                                    of << "move ";
                                    break;
                            }
                            of << fmt(e.val);

                        } break;
                            break;
                        case MIRRValue::TAG_Cast: {
                            auto& e = se.src.as_Cast();
                            of << "CAST " << fmt(e.val) << " as " << fmt(e.type);
                        } break;
                            break;
                        case MIRRValue::TAG_BinOp: {
                            auto& e = se.src.as_BinOp();
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

                        } break;
                            break;
                        case MIRRValue::TAG_UniOp: {
                            auto& e = se.src.as_UniOp();
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

                        } break;
                            break;
                        case MIRRValue::TAG_DstMeta: {
                            auto& e = se.src.as_DstMeta();
                            of << "DSTMETA " << fmt(e.val);
                        } break;
                            break;
                        case MIRRValue::TAG_DstPtr: {
                            auto& e = se.src.as_DstPtr();
                            of << "DSTPTR " << fmt(e.val);
                        } break;
                            break;
                        case MIRRValue::TAG_MakeDst: {
                            auto& e = se.src.as_MakeDst();
                            of << "MAKEDST " << fmt(e.ptrVal) << ", " << fmt(e.metaVal);
                        } break;
                            break;
                        case MIRRValue::TAG_UnionVariant: {
                            auto& e = se.src.as_UnionVariant();
                            of << "UNION " << fmt(e.path) << " " << e.index << " " << fmt(e.val);
                        } break;
                            break;
                        case MIRRValue::TAG_EnumVariant: {
                            auto& e = se.src.as_EnumVariant();
                            of << "ENUM " << fmt(e.path) << " " << e.index << " { ";
                            for (const auto& v : e.vals) {
                                of << fmt(v) << ", ";
                            }
                            of << "}";

                        } break;
                            break;
                        case MIRRValue::TAG_Array: {
                            auto& e = se.src.as_Array();
                            of << "[ ";
                            for (const auto& v : e.vals) {
                                of << fmt(v) << ", ";
                            }
                            of << "]";

                        } break;
                            break;
                        case MIRRValue::TAG_Tuple: {
                            auto& e = se.src.as_Tuple();
                            of << "( ";
                            for (const auto& v : e.vals) {
                                of << fmt(v) << ", ";
                            }
                            of << ")";

                        } break;
                            break;
                        case MIRRValue::TAG_Struct: {
                            auto& e = se.src.as_Struct();
                            of << "{ ";
                            for (const auto& v : e.vals) {
                                of << fmt(v) << ", ";
                            }
                            of << "}: " << fmt(e.path);

                        } break;
                    }

                } break;
                    break;
                case MIRStatement::TAG_SetDropFlag: {
                    auto& se = stmt.as_SetDropFlag();
                    of << "SETFLAG df" << se.idx << " = ";
                    if (se.other == ~0u) {
                        of << se.newVal;
                    } else {
                        of << (se.newVal ? "" : "!") << "df" << se.other;
                    }

                } break;
                    break;
                case MIRStatement::TAG_LoadDropFlag: {
                    auto& se = stmt.as_LoadDropFlag();
                    of << "LOADFLAG df" << se.idx << " = " << fmt(se.slot) << " BIT " << se.bitIndex;

                } break;
                    break;
                case MIRStatement::TAG_SaveDropFlag: {
                    auto& se = stmt.as_SaveDropFlag();
                    of << "SAVEFLAG " << fmt(se.slot) << " BIT " << se.bitIndex << " = df" << se.idx;

                } break;
                    break;
                case MIRStatement::TAG_Asm: {
                    auto& se = stmt.as_Asm();
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

                } break;
                    break;
                case MIRStatement::TAG_Asm2: {
                    auto& se = stmt.as_Asm2();
                    of << "ASM2 (";
                    for (const auto& l : se.lines) {
                        of << l;
                    }
                    for (const auto& p : se.params) {
                        of << ", ";
                        switch (p.tag()) {
                            case MIRAsmParam::TAG_Const: {
                                auto& v = p.as_Const();
                                of << "const " << fmt(v);
                                break;
                            }
                            case MIRAsmParam::TAG_Sym: {
                                auto& v = p.as_Sym();
                                of << "sym " << fmt(v);
                                break;
                            }
                            case MIRAsmParam::TAG_Reg: {
                                auto& v = p.as_Reg();
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
                                break;
                            }
                            case MIRAsmParam::TAG_Label: {
                                auto& v = p.as_Label();
                                of << "label " << v;
                                break;
                            }
                        }
                    }
                    of << ", ";
                    se.options.fmt(of);
                    of << ")";

                } break;
                    break;
                case MIRStatement::TAG_ScopeEnd: {
                    break;

                } break;
            }
            of << ";\n";
        }

        localMirRes.setCurStmtTerm(i);
        const auto& term = code->blocks[i].terminator;
        of << "\t\t";
        switch (term.tag()) {
            break;
            case MIRTerminator::TAG_Incomplete: {
                auto& _e = term.as_Incomplete();
                (void)_e;
            }
                of << "INCOMPLETE\n";
                break;
                break;
            case MIRTerminator::TAG_Return: {
                auto& _e = term.as_Return();
                (void)_e;
            }
                of << "RETURN\n";
                break;
                break;
            case MIRTerminator::TAG_UnwindResume: {
                auto& _e = term.as_UnwindResume();
                (void)_e;
            }
                of << "UNWIND RESUME\n";
                break;
                break;
            case MIRTerminator::TAG_UnwindTerminate: {
                auto& _e = term.as_UnwindTerminate();
                (void)_e;
            }
                of << "UNWIND TERMINATE\n";
                break;
                break;
            case MIRTerminator::TAG_Unreachable: {
                auto& _e = term.as_Unreachable();
                (void)_e;
            }
                of << "UNREACHABLE\n";
                break;
                break;
            case MIRTerminator::TAG_Asm2: {
                auto& e = term.as_Asm2();
                of << "ASM2_GOTO (";
                for (const auto& line : e.lines) {
                    of << line;
                }
                of << ") -> ";
                if (e.retBlock != ~0u) {
                    of << e.retBlock << ", ";
                }
                for (const auto& p : e.params) {
                    if (const auto* target = p.opt_Label()) {
                        of << *target << ", ";
                    }
                }
                of << "\n";

            } break;
                break;
            case MIRTerminator::TAG_Goto: {
                auto& e = term.as_Goto();
                of << "GOTO " << e << "\n";
            } break;
                break;
            case MIRTerminator::TAG_If: {
                auto& e = term.as_If();
                of << "IF " << fmt(e.cond) << " goto " << e.bbTrue << " else " << e.bbFalse << "\n";
            } break;
                break;
            case MIRTerminator::TAG_Switch: {
                auto& e = term.as_Switch();
                of << "SWITCH " << fmt(e.val) << " { ";
                of << e.targets;
                of << " }\n";

            } break;
                break;
            case MIRTerminator::TAG_SwitchValue: {
                auto& e = term.as_SwitchValue();
                of << "SWITCHVALUE " << fmt(e.val) << " { ";
                switch (e.values.tag()) {
                    break;
                    case MIRSwitchValues::TAG_String: {
                        auto& ve = e.values.as_String();
                        for (size_t i = 0; i < ve.size(); i++) {
                            of << "\"" << FmtEscaped(ve[i]) << "\" = " << e.targets[i] << ",";
                        }
                        break;
                    } break;
                    case MIRSwitchValues::TAG_ByteString: {
                        auto& ve = e.values.as_ByteString();
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

                    } break;
                        break;
                    case MIRSwitchValues::TAG_Unsigned: {
                        auto& ve = e.values.as_Unsigned();
                        for (size_t i = 0; i < ve.size(); i++) {
                            of << ve[i] << " = " << e.targets[i] << ",";
                        }
                        break;
                    } break;
                    case MIRSwitchValues::TAG_Signed: {
                        auto& ve = e.values.as_Signed();
                        for (size_t i = 0; i < ve.size(); i++) {
                            of << (ve[i] < 0 ? "" : "+") << ve[i] << " = " << e.targets[i] << ",";
                        }
                        break;
                    }
                }
                // TODO: Values.

                of << "_ = " << e.defTarget;
                of << " }\n";

            } break;
                break;
            case MIRTerminator::TAG_Drop: {
                auto& e = term.as_Drop();
                of << "DROP " << fmt(e.slot);
                if (e.kind == MIRDropKind::SHALLOW) {
                    of << " SHALLOW";
                }
                if (e.flagIdx != ~0u) {
                    of << " IF df" << e.flagIdx;
                }
                of << " goto " << e.target << " unwind " << e.unwind.tagStr() << "\n";

            } break;
                break;
            case MIRTerminator::TAG_Call: {
                auto& e = term.as_Call();
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
                    break;
                    case MIRCallTarget::TAG_Intrinsic: {
                        auto& f = e.fcn.as_Intrinsic();
                        of << "\"" << f.name << "\"";
                        if (f.params.types.size() > 0) {
                            of << "<";
                            for (const auto& t : f.params.types) {
                                of << fmt(t) << ",";
                            }
                            of << ">";
                        }

                    } break;
                        break;
                    case MIRCallTarget::TAG_Value: {
                        auto& f = e.fcn.as_Value();
                        of << "(" << fmt(f) << ")";
                    } break;
                        break;
                    case MIRCallTarget::TAG_Path: {
                        auto& f = e.fcn.as_Path();
                        of << fmt(f);
                    } break;
                }
                of << "(";
                for (const auto& a : e.args) {
                    of << fmt(a) << ", ";
                }
                of << ") goto " << e.retBlock << " unwind " << e.unwind.tagStr() << "\n";

            } break;
                break;
            case MIRTerminator::TAG_TailCall: {
                auto& e = term.as_TailCall();
                of << "TAILCALL ";
                switch (e.fcn.tag()) {
                    case MIRCallTarget::TAG_Intrinsic: {
                        auto& f = e.fcn.as_Intrinsic();
                        of << "\"" << f.name << "\"";
                        break;
                    }
                    case MIRCallTarget::TAG_Value: {
                        auto& f = e.fcn.as_Value();
                        of << "(" << fmt(f) << ")";
                        break;
                    }
                    case MIRCallTarget::TAG_Path: {
                        auto& f = e.fcn.as_Path();
                        of << fmt(f);
                        break;
                    }
                }
                of << "(";
                for (const auto& arg : e.args) {
                    of << fmt(arg) << ", ";
                }
                of << ")\n";

            } break;
        }
        of << "\t}\n";
    }

    of << "}\n";

    mirRes = nullptr;
}

auto CodeGeneratorMonoMir::emitGlobalAsm(const HIRGlobalAssembly&) -> void {
    TODO(Span(), "global_asm! codegen");
}

auto CodeGeneratorMonoMir::monomorphiseFcnReturn(HIRTypeRef& tmp, const HIRFunction& item, const TransParams& params) -> const HIRTypeData* {
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
