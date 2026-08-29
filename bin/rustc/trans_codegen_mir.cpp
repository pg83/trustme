#include "trans_codegen_mir.h"
#include "output.h"
#include "output_file.h"

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
#include <algorithm>

using namespace stl;

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
        OutputFile of;
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
        of << StringView("crate \"") << FmtEscaped(crate.extCrates.at(crateName).path) << StringView(".mir\";\n");
    }
}

auto CodeGeneratorMonoMir::finalise(const TransOptions& opt, CodegenOutput outTy, const std::string& hirFile) -> void {
    if (outTy == CodegenOutput::Executable) {
        if (!crate.noMain) {
            of << StringView("fn main#(isize, *const *const i8): isize {\n");
            auto cStartPath = resolve_.hirCrate().getLangItemPathOpt("trustme-start");
            if (cStartPath == HIRSimplePath()) {
                auto mainPath = resolve_.hirCrate().getLangItemPath(Span(), "trustme-main");
                const auto& startPath = resolve_.hirCrate().getLangItemPathOpt("start");
                if (crate.isNoCore && startPath == HIRSimplePath()) {
                    const auto& mainFcn = crate.getFunctionByPath(Span(), mainPath);
                    of << StringView("\tlet direct_main_result: ") << fmt(mainFcn.returnType) << StringView(";\n");
                    of << StringView("\t0: {\n");
                    of << StringView("\t\tCALL direct_main_result = ") << fmt(HIRGenericPath(mainPath)) << StringView("() goto 1 else 1\n");
                } else {
                    of << StringView("\tlet m: fn();\n");
                    of << StringView("\t0: {\n");
                    of << StringView("\t\tASSIGN m = ADDROF ") << fmt(HIRGenericPath(mainPath)) << StringView(";\n");
                    of << StringView("\t\tCALL RETURN = ") << fmt(HIRGenericPath(resolve_.hirCrate().getLangItemPath(Span(), "start"))) << StringView("(m, arg0, arg1) goto 1 else 1\n");
                }
            } else {
                of << StringView("\t0: {\n");
                of << StringView("\t\tCALL RETURN = ") << fmt(HIRGenericPath(cStartPath)) << StringView("(arg0, arg1) goto 1 else 1;\n");
            }
            of << StringView("\t}\n");
            of << StringView("\t1: {\n");
            of << StringView("\t\tRETURN\n");
            of << StringView("\t}\n");
            of << StringView("}\n");
        }

        const auto& panicImplPath = crate.getLangItemPathOpt("trustme-panic_implementation");
        if (panicImplPath != HIRSimplePath()) {
            of << StringView("fn panic_impl#(usize): u32 = \"panic_impl\":\"Rust\" {\n");
            of << StringView("\t0: {\n");
            of << StringView("\t\tCALL RETURN = ") << fmt(panicImplPath) << StringView("(arg0) goto 1 else 2\n");
            of << StringView("\t}\n");
            of << StringView("\t1: { RETURN }\n");
            of << StringView("\t2: { DIVERGE }\n");
            of << StringView("}\n");
        } else if (!crate.isNoCore) {
            crate.getLangItemPath(Span(), "trustme-panic_implementation");
        }

        // TODO: OOM impl?
    }

    of.close();

    {
        OutputFile output(outfilePath);
    }
}

auto CodeGeneratorMonoMir::emitType(const HIRTypeData* ty) -> void {
    MIRFunction emptyFcn;
    auto pathCallback = makeCallable<MIRPathCb>([&](auto& os) {
        os << StringView("type ") << ty;
    });
    MIRTypeResolve topMirRes{sp, resolve_, pathCallback, HIRTypeRef(), {}, emptyFcn};
    mirRes = &topMirRes;

    if (const auto* te = ty->opt_Tuple()) {
        if (te->size() > 0) {
            const auto* repr = TargetGetTypeRepr(sp, resolve_, ty);
            MIR_ASSERT(*mirRes, repr, StringView("No repr for tuple ") << ty);

            bool hasDropGlue = resolve_.typeNeedsDropGlue(sp, ty);
            auto dropGluePath = HIRPath(ty, "#drop_glue");

            of << StringView("type ") << fmt(ty) << StringView(" {\n");
            of << StringView("\tSIZE ") << repr->size << StringView(", ALIGN ") << repr->align << StringView(";\n");
            if (hasDropGlue) {
                of << StringView("\tDROP ") << fmt(dropGluePath) << StringView(";\n");
            }
            for (const auto& e : repr->fields) {
                of << StringView("\t") << e.offset << StringView(" = ") << fmt(e.ty) << StringView(";\n");
            }
            of << StringView("}\n");
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
                                    MIR_BUG(*mirRes, StringView("Unit-like struct with DstType::Possible"));
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
                MIR_BUG(*mirRes, StringView("Unbound/opaque path in trans - ") << ty);
        }
        UNREACHABLE();
    } else {
        return MetadataType::None;
    }
}

auto CodeGeneratorMonoMir::emitStruct(const Span& sp, const HIRGenericPath& p, const HIRStruct& item) -> void {
    MIRFunction emptyFcn;
    auto pathCallback = makeCallable<MIRPathCb>([&](auto& os) {
        os << StringView("struct ") << p;
    });
    MIRTypeResolve topMirRes{sp, resolve_, pathCallback, HIRTypeRef(), {}, emptyFcn};
    mirRes = &topMirRes;

    auto dropGluePath = HIRPath(crate.types.path(p.clone(), &item), "#drop_glue");

    TRACE_FUNCTION_F(p);
    HIRTypeRef ty = crate.types.path(p.clone(), &item);

    struct H {
        static HIRTypeRef getMetadataType(const Span& sp, const ::StaticTraitResolve& resolve, const TypeRepr& r) {
            ASSERT_BUG(sp, r.fields.size() > 0, StringView(""));
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
                ASSERT_BUG(sp, repr, StringView("No repr for ") << t);
                return getMetadataType(sp, resolve, *repr);
            } else {
                BUG(sp, StringView("Unexpected type in get_metadata_type - ") << t);
            }
        }
    };

    bool hasDropGlue = resolve_.typeNeedsDropGlue(sp, ty);

    const auto* repr = TargetGetTypeRepr(sp, resolve_, ty);
    MIR_ASSERT(*mirRes, repr, StringView("No repr for struct ") << ty);
    of << StringView("type ") << TransMangle(p) << StringView(" {\n");
    of << StringView("\tSIZE ") << repr->size << StringView(", ALIGN ") << repr->align << StringView(";\n");
    if (repr->size == SIZE_MAX) {
        of << StringView("\tDSTMETA ") << H::getMetadataType(sp, resolve_, *repr) << StringView(";\n");
    }
    if (hasDropGlue) {
        of << StringView("\tDROP ") << fmt(dropGluePath) << StringView(";\n");
    }
    for (const auto& e : repr->fields) {
        of << StringView("\t") << e.offset << StringView(" = ") << fmt(e.ty) << StringView(";\n");
    }
    of << StringView("}\n");

    mirRes = nullptr;
}

auto CodeGeneratorMonoMir::emitConstructorEnum(const Span& sp, const HIRGenericPath& varPath, const HIREnum& item, size_t varIdx) -> void {
    TRACE_FUNCTION_F(varPath);
    HIRTypeRef tmp;
    MonomorphStatePtr ms(crate.types, nullptr, &varPath.params, nullptr);
    auto monomorph = [&](const auto& x) {
        return resolve_.monomorphExpandOpt(sp, tmp, x, ms);
    };

    auto enumPath = varPath.clone();
    enumPath.path.popComponent();

    const auto& varTy = item.data.as_Data().at(varIdx).type;
    const auto& e = varTy->as_Path().binding.as_Struct()->data.as_Tuple();
    of << StringView("/* ") << varPath << StringView(" */\n");
    of << StringView("fn ") << fmt(varPath) << StringView("(");
    for (unsigned int i = 0; i < e.size(); i++) {
        if (i != 0) {
            of << StringView(", ");
        }
        of << fmt(monomorph(e[i].ent));
    }
    of << StringView("): ") << fmt(enumPath) << StringView(" {\n");
    of << StringView("\t0: {\n");
    of << StringView("\t\tASSIGN RETURN = ENUM ") << fmt(enumPath) << StringView(" ") << varIdx << StringView(" { ");
    for (unsigned int i = 0; i < e.size(); i++) {
        if (i != 0) {
            of << StringView(", ");
        }
        of << StringView("arg") << i;
    }
    of << StringView(" };\n");
    of << StringView("\t\tRETURN\n");
    of << StringView("\t}\n");
    of << StringView("}");
}

auto CodeGeneratorMonoMir::emitConstructorStruct(const Span& sp, const HIRGenericPath& p, const HIRStruct& item) -> void {
    TRACE_FUNCTION_F(p);
    HIRTypeRef tmp;
    MonomorphStatePtr ms(crate.types, nullptr, &p.params, nullptr);
    auto monomorph = [&](const auto& x) {
        return resolve_.monomorphExpandOpt(sp, tmp, x, ms);
    };
    const auto& e = item.data.as_Tuple();
    of << StringView("/* ") << p << StringView(" */\n");
    of << StringView("fn ") << fmt(p) << StringView("(");
    for (unsigned int i = 0; i < e.size(); i++) {
        if (i != 0) {
            of << StringView(", ");
        }
        of << fmt(monomorph(e[i].ent));
    }
    of << StringView("): ") << fmt(p) << StringView(" {\n");
    of << StringView("\t0: {\n");
    of << StringView("\t\tASSIGN RETURN = { ");
    for (unsigned int i = 0; i < e.size(); i++) {
        if (i != 0) {
            of << StringView(", ");
        }
        of << StringView("arg") << i;
    }
    of << StringView(" }: ") << fmt(p) << StringView(";\n");
    of << StringView("\t\tRETURN\n");
    of << StringView("\t}\n");
    of << StringView("}\n");
}

auto CodeGeneratorMonoMir::emitUnion(const Span& sp, const HIRGenericPath& p, const HIRUnion& item) -> void {
    MIRFunction emptyFcn;
    auto pathCallback = makeCallable<MIRPathCb>([&](auto& os) {
        os << StringView("union ") << p;
    });
    MIRTypeResolve topMirRes{sp, resolve_, pathCallback, HIRTypeRef(), {}, emptyFcn};
    mirRes = &topMirRes;

    TRACE_FUNCTION_F(p);
    HIRTypeRef ty = crate.types.path(p.clone(), &item);

    bool hasDropGlue = resolve_.typeNeedsDropGlue(sp, ty);
    auto dropGluePath = HIRPath(ty, "#drop_glue");

    const auto* repr = TargetGetTypeRepr(sp, resolve_, ty);
    MIR_ASSERT(*mirRes, repr, StringView("No repr for union ") << ty);
    of << StringView("type ") << fmt(p) << StringView(" {\n");
    of << StringView("\tSIZE ") << repr->size << StringView(", ALIGN ") << repr->align << StringView(";\n");
    if (hasDropGlue) {
        of << StringView("\tDROP ") << fmt(dropGluePath) << StringView(";\n");
    }
    for (const auto& e : repr->fields) {
        of << StringView("\t") << e.offset << StringView(" = ") << fmt(e.ty) << StringView(";\n");
    }
    of << StringView("}\n");
}

auto CodeGeneratorMonoMir::emitEnum(const Span& sp, const HIRGenericPath& p, const HIREnum& item) -> void {
    MIRFunction emptyFcn;
    auto pathCallback = makeCallable<MIRPathCb>([&](auto& os) {
        os << StringView("enum ") << p;
    });
    MIRTypeResolve topMirRes{sp, resolve_, pathCallback, HIRTypeRef(), {}, emptyFcn};
    mirRes = &topMirRes;

    HIRTypeRef ty = crate.types.path(p.clone(), &item);

    bool hasDropGlue = resolve_.typeNeedsDropGlue(sp, ty);
    auto dropGluePath = HIRPath(ty, "#drop_glue");

    const auto* repr = TargetGetTypeRepr(sp, resolve_, ty);
    MIR_ASSERT(*mirRes, repr, StringView("No repr for enum ") << ty);
    of << StringView("type ") << fmt(p) << StringView(" {\n");
    of << StringView("\tSIZE ") << repr->size << StringView(", ALIGN ") << repr->align << StringView(";\n");
    if (hasDropGlue) {
        of << StringView("\tDROP ") << fmt(dropGluePath) << StringView(";\n");
    }
    for (const auto& e : repr->fields) {
        of << StringView("\t") << e.offset << StringView(" = ") << fmt(e.ty) << StringView(";\n");
    }

    auto emitValue = [&](const TypeRepr::FieldPath& path, U128 v) {
        of << StringView("\"");
        for (size_t i = 0; i < path.size; i++) {
            int val = ((v >> (i * 8)) & U128(0xFF)).truncateU64();
            if (val < 16) {
                of << StringView("\\x0") << formatHex(val);
            } else {
                of << StringView("\\x") << formatHex(val);
            }
        }
        of << StringView("\"");
    };

    switch (repr->variants.tag()) {
        break;
        case TypeReprVariantMode::TAG_None: {
        } break;
        case TypeReprVariantMode::TAG_Linear: {
            auto& e = repr->variants.as_Linear();
            of << StringView("\t@[") << e.field.index << StringView(", ") << e.field.subFields << StringView("] = {\n");
            for (size_t i = 0; i < e.numVariants; i++) {
                of << StringView("\t\t");

                if (e.isNiche(i)) {
                    of << StringView("*");
                } else {
                    emitValue(e.field, U128(e.tagValue(i)));
                }
                if (!item.isValue()) {
                    of << StringView(" =") << i;
                }
                of << StringView(",\n");
            }
            of << StringView("\t\t}\n");
        } break;
        case TypeReprVariantMode::TAG_Values: {
            auto& e = repr->variants.as_Values();
            of << StringView("\t@[") << e.field.index << StringView(", ") << e.field.subFields << StringView("] = {\n");
            for (size_t idx = 0; idx < e.values.size(); idx++) {
                of << StringView("\t\t");
                emitValue(e.field, e.values[idx]);
                if (!item.isValue()) {
                    of << StringView(" =") << idx;
                }
                of << StringView(",\n");
            }
            of << StringView("\t}\n");
        } break;
        case TypeReprVariantMode::TAG_NonZero: {
            auto& e = repr->variants.as_NonZero();
            of << StringView("\t@[") << e.field.index << StringView(", ") << e.field.subFields << StringView("] = { ");
            for (size_t i = 0; i < 2; i++) {
                if (i == 1) {
                    of << StringView(", ");
                }

                if (e.zeroVariant == i) {
                    of << StringView("\"");
                    for (size_t i = 0; i < e.field.size; i++) {
                        of << StringView("\\0");
                    }
                    of << StringView("\"");
                } else {
                    of << StringView("* =") << i;
                }
            }
            of << StringView(" }\n");
        }
    }
    of << StringView("}\n");

    mirRes = nullptr;
}

auto CodeGeneratorMonoMir::emitStrByte(u8 b) -> void {
    if (b == 0) {
        of << StringView("\\0");
    } else if (b == '\\') {
        of << StringView("\\\\");
    } else if (b == '"') {
        of << StringView("\\\"");
    } else if (' ' <= b && b <= 'z' && b != '\\') {
        of << b;
    } else if (b < 16) {
        of << StringView("\\x0") << formatHex(b);
    } else {
        of << StringView("\\x") << formatHex(b);
    }
}

auto CodeGeneratorMonoMir::emitStaticLocal(const HIRPath& p, const HIRStatic& item, const TransParams& params, const EncodedLiteral& encoded) -> void {
    MIRFunction emptyFcn;
    auto pathCallback = makeCallable<MIRPathCb>([&](auto& os) {
        os << StringView("static ") << p;
    });
    MIRTypeResolve topMirRes{sp, resolve_, pathCallback, HIRTypeRef(), {}, emptyFcn};
    mirRes = &topMirRes;

    TRACE_FUNCTION_F(p);
    auto type = params.monomorph(resolve_, item.type);

    of << StringView("static ") << fmt(p) << StringView(": ") << fmt(type) << StringView(" = \"");
    for (auto b : encoded.bytes) {
        emitStrByte(b);
    }
    of << StringView("\"");
    of << StringView("{");
    for (const auto& r : encoded.relocations) {
        of << StringView("@") << r.ofs << StringView("+") << r.len << StringView(" = ");
        if (r.p) {
            of << fmt(*r.p);
        } else {
            of << StringView("\"") << FmtEscaped(r.bytes) << StringView("\"");
        }
        of << StringView(",");
    }
    of << StringView("}");
    of << StringView(";\n");

    mirRes = nullptr;
}

auto CodeGeneratorMonoMir::emitFunctionExt(const HIRPath& p, const HIRFunction& item, const TransParams& params) -> void {
    MIRFunction emptyFcn;
    auto pathCallback = makeCallable<MIRPathCb>([&](auto& os) {
        os << StringView("extern fn ") << p;
    });
    MIRTypeResolve topMirRes{sp, resolve_, pathCallback, HIRTypeRef(), {}, emptyFcn};
    mirRes = &topMirRes;

    if (item.linkage.name != "") {
        HIRTypeRef retTypeTmp;
        const auto& retType = monomorphiseFcnReturn(retTypeTmp, item, params);

        of << StringView("/* ") << p << StringView(" */\n");
        of << StringView("fn ") << fmt(p) << StringView("(");
        for (unsigned int i = 0; i < item.args.size(); i++) {
            if (i != 0) {
                of << StringView(", ");
            }
            of << fmt(params.monomorph(resolve_, item.args[i].second));
        }
        of << StringView("): ") << fmt(retType) << StringView(" = \"") << item.linkage.name << StringView("\":\"") << item.abi << StringView("\";\n");
    }

    mirRes = nullptr;
}

auto CodeGeneratorMonoMir::emitFunctionCode(const HIRPath& p, const HIRFunction& item, const TransParams& params, bool isExternDef, const MIRFunctionPointer& code, bool hasPrototype) -> void {
    TRACE_FUNCTION_F(p);
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

    of << StringView("/* ") << p << StringView(" */\n");
    of << StringView("fn ") << fmt(p) << StringView("(");
    for (unsigned int i = 0; i < item.args.size(); i++) {
        if (i != 0) {
            of << StringView(", ");
        }
        of << fmt(params.monomorph(resolve_, item.args[i].second));
    }
    of << StringView("): ") << fmt(retType);
    if (item.linkage.name != "") {
        of << StringView(" = \"") << item.linkage.name << StringView("\":\"") << item.abi << StringView("\"");
    }
    of << StringView(" {\n");
    for (unsigned int i = 0; i < code->locals.size(); i++) {
        DEBUG(StringView("var") << i << StringView(" : ") << code->locals[i]);
        of << StringView("\tlet var") << i << StringView(": ") << fmt(code->locals[i]) << StringView(";\n");
    }
    for (unsigned int i = 0; i < code->dropFlags.size(); i++) {
        of << StringView("\tlet df") << i << StringView(" = ") << code->dropFlags[i] << StringView(";\n");
    }

    for (unsigned int i = 0; i < code->blocks.size(); i++) {
        TRACE_FUNCTION_F(p << StringView(" bb") << i);
        of << StringView("\t") << i << StringView(": {\n");

        for (const auto& stmt : code->blocks[i].statements) {
            of << StringView("\t\t");
            localMirRes.setCurStmt(i, (&stmt - &code->blocks[i].statements.front()));
            DEBUG(stmt);
            switch (stmt.tag()) {
                break;
                case MIRStatement::TAG_Assign: {
                    auto& se = stmt.as_Assign();
                    of << StringView("ASSIGN ") << fmt(se.dst) << StringView(" = ");
                    switch (se.src.tag()) {
                        break;
                        case MIRRValue::TAG_Use: {
                            auto& e = se.src.as_Use();
                            of << StringView("=") << fmt(e);
                        } break;
                            break;
                        case MIRRValue::TAG_Constant: {
                            auto& e = se.src.as_Constant();
                            of << fmt(e);
                        } break;
                            break;
                        case MIRRValue::TAG_SizedArray: {
                            auto& e = se.src.as_SizedArray();
                            of << StringView("[") << fmt(e.val) << StringView("; ") << e.count << StringView("]");
                        } break;
                            break;
                        case MIRRValue::TAG_Borrow: {
                            auto& e = se.src.as_Borrow();
                            of << StringView("&");
                            switch (e.type) {
                                case HIRBorrowType::Shared:
                                    break;
                                case HIRBorrowType::Unique:
                                    of << StringView("mut ");
                                    break;
                                case HIRBorrowType::Owned:
                                    of << StringView("move ");
                                    break;
                            }
                            of << fmt(e.val);
                        } break;
                            break;
                        case MIRRValue::TAG_Cast: {
                            auto& e = se.src.as_Cast();
                            of << StringView("CAST ") << fmt(e.val) << StringView(" as ") << fmt(e.type);
                        } break;
                            break;
                        case MIRRValue::TAG_BinOp: {
                            auto& e = se.src.as_BinOp();
                            of << StringView("BINOP ") << fmt(e.valL) << StringView(" ");
                            switch (e.op) {
                                case MIRBinOp::ADD:
                                    of << StringView("+");
                                    break;
                                case MIRBinOp::ADD_OV:
                                    of << StringView("+^");
                                    break;
                                case MIRBinOp::SUB:
                                    of << StringView("-");
                                    break;
                                case MIRBinOp::SUB_OV:
                                    of << StringView("-^");
                                    break;
                                case MIRBinOp::MUL:
                                    of << StringView("*");
                                    break;
                                case MIRBinOp::MUL_OV:
                                    of << StringView("*^");
                                    break;
                                case MIRBinOp::DIV:
                                    of << StringView("/");
                                    break;
                                case MIRBinOp::DIV_OV:
                                    of << StringView("/^");
                                    break;
                                case MIRBinOp::MOD:
                                    of << StringView("%");
                                    break;
                                case MIRBinOp::BIT_OR:
                                    of << StringView("|");
                                    break;
                                case MIRBinOp::BIT_AND:
                                    of << StringView("&");
                                    break;
                                case MIRBinOp::BIT_XOR:
                                    of << StringView("^");
                                    break;
                                case MIRBinOp::BIT_SHR:
                                    of << StringView(">>");
                                    break;
                                case MIRBinOp::BIT_SHL:
                                    of << StringView("<<");
                                    break;
                                case MIRBinOp::NE:
                                    of << StringView("!=");
                                    break;
                                case MIRBinOp::EQ:
                                    of << StringView("==");
                                    break;
                                case MIRBinOp::GT:
                                    of << StringView(">");
                                    break;
                                case MIRBinOp::GE:
                                    of << StringView(">=");
                                    break;
                                case MIRBinOp::LT:
                                    of << StringView("<");
                                    break;
                                case MIRBinOp::LE:
                                    of << StringView("<=");
                                    break;
                            }
                            of << StringView(" ") << fmt(e.valR);
                        } break;
                            break;
                        case MIRRValue::TAG_UniOp: {
                            auto& e = se.src.as_UniOp();
                            of << StringView("UNIOP ");
                            switch (e.op) {
                                case MIRUniOp::INV:
                                    of << StringView("!");
                                    break;
                                case MIRUniOp::NEG:
                                    of << StringView("-");
                                    break;
                            }
                            of << StringView(" ") << fmt(e.val);
                        } break;
                            break;
                        case MIRRValue::TAG_DstMeta: {
                            auto& e = se.src.as_DstMeta();
                            of << StringView("DSTMETA ") << fmt(e.val);
                        } break;
                            break;
                        case MIRRValue::TAG_DstPtr: {
                            auto& e = se.src.as_DstPtr();
                            of << StringView("DSTPTR ") << fmt(e.val);
                        } break;
                            break;
                        case MIRRValue::TAG_MakeDst: {
                            auto& e = se.src.as_MakeDst();
                            of << StringView("MAKEDST ") << fmt(e.ptrVal) << StringView(", ") << fmt(e.metaVal);
                        } break;
                            break;
                        case MIRRValue::TAG_UnionVariant: {
                            auto& e = se.src.as_UnionVariant();
                            of << StringView("UNION ") << fmt(e.path) << StringView(" ") << e.index << StringView(" ") << fmt(e.val);
                        } break;
                            break;
                        case MIRRValue::TAG_EnumVariant: {
                            auto& e = se.src.as_EnumVariant();
                            of << StringView("ENUM ") << fmt(e.path) << StringView(" ") << e.index << StringView(" { ");
                            for (const auto& v : e.vals) {
                                of << fmt(v) << StringView(", ");
                            }
                            of << StringView("}");
                        } break;
                            break;
                        case MIRRValue::TAG_Array: {
                            auto& e = se.src.as_Array();
                            of << StringView("[ ");
                            for (const auto& v : e.vals) {
                                of << fmt(v) << StringView(", ");
                            }
                            of << StringView("]");
                        } break;
                            break;
                        case MIRRValue::TAG_Tuple: {
                            auto& e = se.src.as_Tuple();
                            of << StringView("( ");
                            for (const auto& v : e.vals) {
                                of << fmt(v) << StringView(", ");
                            }
                            of << StringView(")");
                        } break;
                            break;
                        case MIRRValue::TAG_Struct: {
                            auto& e = se.src.as_Struct();
                            of << StringView("{ ");
                            for (const auto& v : e.vals) {
                                of << fmt(v) << StringView(", ");
                            }
                            of << StringView("}: ") << fmt(e.path);
                        } break;
                    }
                } break;
                    break;
                case MIRStatement::TAG_SetDropFlag: {
                    auto& se = stmt.as_SetDropFlag();
                    of << StringView("SETFLAG df") << se.idx << StringView(" = ");
                    if (se.other == ~0u) {
                        of << se.newVal;
                    } else {
                        of << (se.newVal ? "" : "!") << StringView("df") << se.other;
                    }
                } break;
                    break;
                case MIRStatement::TAG_LoadDropFlag: {
                    auto& se = stmt.as_LoadDropFlag();
                    of << StringView("LOADFLAG df") << se.idx << StringView(" = ") << fmt(se.slot) << StringView(" BIT ") << se.bitIndex;
                } break;
                    break;
                case MIRStatement::TAG_SaveDropFlag: {
                    auto& se = stmt.as_SaveDropFlag();
                    of << StringView("SAVEFLAG ") << fmt(se.slot) << StringView(" BIT ") << se.bitIndex << StringView(" = df") << se.idx;
                } break;
                    break;
                case MIRStatement::TAG_Asm: {
                    auto& se = stmt.as_Asm();
                    of << StringView("ASM (");
                    for (const auto& v : se.outputs) {
                        of << StringView("\"") << v.first << StringView("\" : ") << fmt(v.second) << StringView(", ");
                    }
                    of << StringView(") = \"") << FmtEscaped(se.tpl) << StringView("\"(");
                    for (const auto& v : se.inputs) {
                        of << StringView("\"") << v.first << StringView("\" : ") << fmt(v.second) << StringView(", ");
                    }
                    of << StringView(") [");

                    for (const auto& v : se.clobbers) {
                        of << StringView("\"") << v << StringView("\", ");
                    }
                    of << StringView(":") << se.flags << StringView("]");
                } break;
                    break;
                case MIRStatement::TAG_Asm2: {
                    auto& se = stmt.as_Asm2();
                    of << StringView("ASM2 (");
                    for (const auto& l : se.lines) {
                        of << l;
                    }
                    for (const auto& p : se.params) {
                        of << StringView(", ");
                        switch (p.tag()) {
                            case MIRAsmParam::TAG_Const: {
                                auto& v = p.as_Const();
                                of << StringView("const ") << fmt(v);
                                break;
                            }
                            case MIRAsmParam::TAG_Sym: {
                                auto& v = p.as_Sym();
                                of << StringView("sym ") << fmt(v);
                                break;
                            }
                            case MIRAsmParam::TAG_Reg: {
                                auto& v = p.as_Reg();
                                of << StringView("reg(") << v.dir << StringView(" ") << v.spec << StringView(") ");
                                if (v.input) {
                                    of << fmt(*v.input);
                                } else {
                                    of << StringView("_");
                                }
                                of << StringView(" => ");
                                if (v.output) {
                                    of << fmt(*v.output);
                                } else {
                                    of << StringView("_");
                                }
                                break;
                            }
                            case MIRAsmParam::TAG_Label: {
                                auto& v = p.as_Label();
                                of << StringView("label ") << v;
                                break;
                            }
                        }
                    }
                    of << StringView(", ");
                    se.options.fmt(of);
                    of << StringView(")");
                } break;
                    break;
                case MIRStatement::TAG_ScopeEnd: {
                    break;
                } break;
            }
            of << StringView(";\n");
        }

        localMirRes.setCurStmtTerm(i);
        const auto& term = code->blocks[i].terminator;
        DEBUG(StringView("- ") << term);
        of << StringView("\t\t");
        switch (term.tag()) {
            break;
            case MIRTerminator::TAG_Incomplete: {
                auto& _e = term.as_Incomplete();
                (void)_e;
            }
                of << StringView("INCOMPLETE\n");
                break;
                break;
            case MIRTerminator::TAG_Return: {
                auto& _e = term.as_Return();
                (void)_e;
            }
                of << StringView("RETURN\n");
                break;
                break;
            case MIRTerminator::TAG_UnwindResume: {
                auto& _e = term.as_UnwindResume();
                (void)_e;
            }
                of << StringView("UNWIND RESUME\n");
                break;
                break;
            case MIRTerminator::TAG_UnwindTerminate: {
                auto& _e = term.as_UnwindTerminate();
                (void)_e;
            }
                of << StringView("UNWIND TERMINATE\n");
                break;
                break;
            case MIRTerminator::TAG_Unreachable: {
                auto& _e = term.as_Unreachable();
                (void)_e;
            }
                of << StringView("UNREACHABLE\n");
                break;
                break;
            case MIRTerminator::TAG_Asm2: {
                auto& e = term.as_Asm2();
                of << StringView("ASM2_GOTO (");
                for (const auto& line : e.lines) {
                    of << line;
                }
                of << StringView(") -> ");
                if (e.retBlock != ~0u) {
                    of << e.retBlock << StringView(", ");
                }
                for (const auto& p : e.params) {
                    if (const auto* target = p.opt_Label()) {
                        of << *target << StringView(", ");
                    }
                }
                of << StringView("\n");
            } break;
                break;
            case MIRTerminator::TAG_Goto: {
                auto& e = term.as_Goto();
                of << StringView("GOTO ") << e << StringView("\n");
            } break;
                break;
            case MIRTerminator::TAG_If: {
                auto& e = term.as_If();
                of << StringView("IF ") << fmt(e.cond) << StringView(" goto ") << e.bbTrue << StringView(" else ") << e.bbFalse << StringView("\n");
            } break;
                break;
            case MIRTerminator::TAG_Switch: {
                auto& e = term.as_Switch();
                of << StringView("SWITCH ") << fmt(e.val) << StringView(" { ");
                of << e.targets;
                of << StringView(" }\n");
            } break;
                break;
            case MIRTerminator::TAG_SwitchValue: {
                auto& e = term.as_SwitchValue();
                of << StringView("SWITCHVALUE ") << fmt(e.val) << StringView(" { ");
                switch (e.values.tag()) {
                    break;
                    case MIRSwitchValues::TAG_String: {
                        auto& ve = e.values.as_String();
                        for (size_t i = 0; i < ve.size(); i++) {
                            of << StringView("\"") << FmtEscaped(ve[i]) << StringView("\" = ") << e.targets[i] << StringView(",");
                        }
                        break;
                    } break;
                    case MIRSwitchValues::TAG_ByteString: {
                        auto& ve = e.values.as_ByteString();
                        for (size_t j = 0; j < ve.size(); j++) {
                            of << StringView("b\"");
                            for (size_t i = 0; i < ve[j].size(); i++) {
                                auto b = ve[j][i];
                                switch (b) {
                                    case '\\':
                                        of << StringView("\\\\");
                                        break;
                                    case '\"':
                                        of << StringView("\\\"");
                                        break;
                                    default:
                                        if (' ' <= b && b < 0x7f) {
                                            of << char(ve[j][i]);
                                        } else {
                                            of << StringView("\\x");
                                            of << StringView("0123456789ABCDEF")[b >> 4];
                                            of << StringView("0123456789ABCDEF")[b & 15];
                                        }
                                        break;
                                }
                            }
                            of << StringView("\" = ") << e.targets[i] << StringView(",");
                        }
                    } break;
                        break;
                    case MIRSwitchValues::TAG_Unsigned: {
                        auto& ve = e.values.as_Unsigned();
                        for (size_t i = 0; i < ve.size(); i++) {
                            of << ve[i] << StringView(" = ") << e.targets[i] << StringView(",");
                        }
                        break;
                    } break;
                    case MIRSwitchValues::TAG_Signed: {
                        auto& ve = e.values.as_Signed();
                        for (size_t i = 0; i < ve.size(); i++) {
                            of << (ve[i] < 0 ? "" : "+") << ve[i] << StringView(" = ") << e.targets[i] << StringView(",");
                        }
                        break;
                    }
                }
                // TODO: Values.

                of << StringView("_ = ") << e.defTarget;
                of << StringView(" }\n");
            } break;
                break;
            case MIRTerminator::TAG_Drop: {
                auto& e = term.as_Drop();
                of << StringView("DROP ") << fmt(e.slot);
                if (e.kind == MIRDropKind::SHALLOW) {
                    of << StringView(" SHALLOW");
                }
                if (e.flagIdx != ~0u) {
                    of << StringView(" IF df") << e.flagIdx;
                }
                of << StringView(" goto ") << e.target << StringView(" unwind ") << e.unwind.tagStr() << StringView("\n");
            } break;
                break;
            case MIRTerminator::TAG_Call: {
                auto& e = term.as_Call();
                if (const auto* fP = e.fcn.opt_Intrinsic()) {
                    if (fP->name == "offset_of") {
                        size_t val = localMirRes.intrinsicOffsetOf(fP->params.types.at(0), e.args);
                        of << fmt(e.retVal) << StringView(" = ") << val << StringView(" usize;\n");
                        of << StringView("\t\tGOTO ") << e.retBlock;
                        break;
                    }
                }
                of << StringView("CALL ") << fmt(e.retVal) << StringView(" = ");
                switch (e.fcn.tag()) {
                    break;
                    case MIRCallTarget::TAG_Intrinsic: {
                        auto& f = e.fcn.as_Intrinsic();
                        of << StringView("\"") << f.name << StringView("\"");
                        if (f.params.types.size() > 0) {
                            of << StringView("<");
                            for (const auto& t : f.params.types) {
                                of << fmt(t) << StringView(",");
                            }
                            of << StringView(">");
                        }
                    } break;
                        break;
                    case MIRCallTarget::TAG_Value: {
                        auto& f = e.fcn.as_Value();
                        of << StringView("(") << fmt(f) << StringView(")");
                    } break;
                        break;
                    case MIRCallTarget::TAG_Path: {
                        auto& f = e.fcn.as_Path();
                        of << fmt(f);
                    } break;
                }
                of << StringView("(");
                for (const auto& a : e.args) {
                    of << fmt(a) << StringView(", ");
                }
                of << StringView(") goto ") << e.retBlock << StringView(" unwind ") << e.unwind.tagStr() << StringView("\n");
            } break;
                break;
            case MIRTerminator::TAG_TailCall: {
                auto& e = term.as_TailCall();
                of << StringView("TAILCALL ");
                switch (e.fcn.tag()) {
                    case MIRCallTarget::TAG_Intrinsic: {
                        auto& f = e.fcn.as_Intrinsic();
                        of << StringView("\"") << f.name << StringView("\"");
                        break;
                    }
                    case MIRCallTarget::TAG_Value: {
                        auto& f = e.fcn.as_Value();
                        of << StringView("(") << fmt(f) << StringView(")");
                        break;
                    }
                    case MIRCallTarget::TAG_Path: {
                        auto& f = e.fcn.as_Path();
                        of << fmt(f);
                        break;
                    }
                }
                of << StringView("(");
                for (const auto& arg : e.args) {
                    of << fmt(arg) << StringView(", ");
                }
                of << StringView(")\n");
            } break;
        }
        of << StringView("\t}\n");
    }

    of << StringView("}\n");

    mirRes = nullptr;
}

auto CodeGeneratorMonoMir::emitGlobalAsm(const HIRGlobalAssembly&) -> void {
    TODO(Span(), StringView("global_asm! codegen"));
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

namespace stl {
template <>
void output<ZeroCopyOutput, Fmt<HIRPath>>(ZeroCopyOutput& os, Fmt<HIRPath> x) {
        os << TransMangle(x.wb, x.e);
    return;
    }

template <>
void output<ZeroCopyOutput, Fmt<HIRGenericPath>>(ZeroCopyOutput& os, Fmt<HIRGenericPath> x) {
        os << TransMangle(x.wb, x.e);
    return;
    }

template <>
void output<ZeroCopyOutput, Fmt<HIRSimplePath>>(ZeroCopyOutput& os, Fmt<HIRSimplePath> x) {
        os << TransMangle(x.wb, x.e);
    return;
    }

template <>
void output<ZeroCopyOutput, Fmt<HIRTypeRef>>(ZeroCopyOutput& os, Fmt<HIRTypeRef> x) {
        {
            auto& tuMatch = (*x.e);
            switch (tuMatch.tag()) {
                case HIRTypeData::TAG_Infer: {
                    BUG(Span(), StringView("") << x.e);
                    break;
                }
                case HIRTypeData::TAG_Diverge: {
                    os << StringView("!");
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
                    BUG(Span(), StringView("") << x.e);
                    break;
                }
                case HIRTypeData::TAG_TraitObject: {
                    auto& te = tuMatch.as_TraitObject();
                    auto path = te.trait.path.clone();
                    os << StringView("dyn ") << TransMangle(x.wb, path);
                    break;
                }
                case HIRTypeData::TAG_ErasedType: {
                    BUG(Span(), StringView("") << x.e);
                    break;
                }
                case HIRTypeData::TAG_Array: {
                    auto& te = tuMatch.as_Array();
                    os << StringView("[") << fmt(x.wb, te.inner) << StringView("; ") << te.size << StringView("]");
                    break;
                }
                case HIRTypeData::TAG_Slice: {
                    auto& te = tuMatch.as_Slice();
                    os << StringView("[") << fmt(x.wb, te.inner) << StringView("]");
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
                        os << StringView("()");
                    } else {
                        os << TransMangle(x.wb, x.e);
                    }
                    break;
                }
                case HIRTypeData::TAG_Borrow: {
                    auto& te = tuMatch.as_Borrow();
                    switch (te.type) {
                        case HIRBorrowType::Shared:
                            os << StringView("&");
                            break;
                        case HIRBorrowType::Unique:
                            os << StringView("&mut ");
                            break;
                        case HIRBorrowType::Owned:
                            os << StringView("&move ");
                            break;
                    }
                    os << fmt(x.wb, te.inner);
                    break;
                }
                case HIRTypeData::TAG_Pointer: {
                    auto& te = tuMatch.as_Pointer();
                    switch (te.type) {
                        case HIRBorrowType::Shared:
                            os << StringView("*const ");
                            break;
                        case HIRBorrowType::Unique:
                            os << StringView("*mut ");
                            break;
                        case HIRBorrowType::Owned:
                            os << StringView("*move ");
                            break;
                    }
                    os << fmt(x.wb, te.inner);
                    break;
                }
                case HIRTypeData::TAG_NamedFunction: {
                    auto& te = tuMatch.as_NamedFunction();
                    os << StringView("fn ") << TransMangle(x.wb, te.path);
                    break;
                }
                case HIRTypeData::TAG_Function: {
                    auto& e = tuMatch.as_Function();
                    if (e.isUnsafe) {
                        os << StringView("unsafe ");
                    }
                    if (e.abi != "") {
                        os << StringView("extern \"") << e.abi << StringView("\" ");
                    }
                    os << StringView("fn(");
                    for (const auto& t : e.argTypes) {
                        os << fmt(x.wb, t) << StringView(", ");
                    }
                    os << StringView(") -> ") << fmt(x.wb, e.rettype);
                    break;
                } break;
                case HIRTypeData::TAG_NodeType:
                    BUG(Span(), StringView("Unexpected type in trans: ") << x.e);
                    break;
            }
        }
        return;
    }

template <>
void output<ZeroCopyOutput, Fmt<MIRLValue>>(ZeroCopyOutput& os, Fmt<MIRLValue> x) {
        for (const auto& w : ::reverse(x.e.wrappers)) {
            if (w.is_Deref()) {
                os << StringView("(*");
            }
        }
        switch (x.e.root.tag()) {
            case MIRLValue::Storage::TAG_Return: {
                os << StringView("RETURN");
                break;
            }
            case MIRLValue::Storage::TAG_Local: {
                decltype(x.e.root.as_Local()) e = x.e.root.as_Local();
                os << StringView("var") << e;
                break;
            }
            case MIRLValue::Storage::TAG_Argument: {
                decltype(x.e.root.as_Argument()) e = x.e.root.as_Argument();
                os << StringView("arg") << e;
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
                    os << StringView(")");
                } break;
                    break;
                case MIRLValue::Wrapper::TAG_Field: {
                    decltype(w.as_Field()) fieldIndex = w.as_Field();
                    if (prevWasNum) {
                        os << StringView(" ");
                    }
                    os << StringView(".") << fieldIndex;
                    wasNum = true;
                } break;
                    break;
                case MIRLValue::Wrapper::TAG_Index: {
                    decltype(w.as_Index()) e = w.as_Index();
                    os << StringView("[") << fmt(x.wb, MIRLValue::newLocal(e)) << StringView("]");
                } break;
                    break;
                case MIRLValue::Wrapper::TAG_Downcast: {
                    decltype(w.as_Downcast()) variantIndex = w.as_Downcast();
                    os << StringView("@") << variantIndex;
                    wasNum = true;
                } break;
            }
        }
        return;
    }

template <>
void output<ZeroCopyOutput, Fmt<MIRConstant>>(ZeroCopyOutput& os, Fmt<MIRConstant> x) {
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
                os << (v.v < 0 ? "" : "+") << v.v << StringView(" ") << v.t;
            } break;
                break;
            case MIRConstant::TAG_Uint: {
                auto& v = e.as_Uint();
                os << v.v << StringView(" ") << v.t;
            } break;
                break;
            case MIRConstant::TAG_Float: {
                auto& v = e.as_Float();
                // TODO: Infinity/nan/...
                auto vi = H::doubleToU64(static_cast<double>(v.v));
                bool sign = (vi & (1ull << 63)) != 0;
                int exp = (vi >> 52) & 0x7FF;
                u64 frac = vi & ((1ull << 52) - 1);
                os << StringView(sign ? "-" : "+") << StringView("0x1.") << formatHex(frac, 52 / 4) << StringView("p") << (exp - 1023);
                os << StringView(" ") << v.t;
            } break;
                break;
            case MIRConstant::TAG_ItemAddr: {
                auto& v = e.as_ItemAddr();
                os << StringView("ADDROF ") << fmt(x.wb, *v);
                if (v.offset != U128(0)) {
                    os << StringView(" + ") << v.offset;
                }
            } break;
                break;
            case MIRConstant::TAG_Const: {
                BUG(Span(), StringView("Stray named constant in MIR after cleanup - ") << e);
            } break;
            default:
                os << e;
                break;
        }
        return;
    }

template <>
void output<ZeroCopyOutput, Fmt<MIRParam>>(ZeroCopyOutput& os, Fmt<MIRParam> x) {
        switch (x.e.tag()) {
            break;
            case MIRParam::TAG_LValue: {
                auto& e = x.e.as_LValue();
                os << fmt(x.wb, e);
            } break;
                break;
            case MIRParam::TAG_Borrow: {
                auto& e = x.e.as_Borrow();
                os << StringView("&");
                switch (e.type) {
                    case HIRBorrowType::Shared:
                        break;
                    case HIRBorrowType::Unique:
                        os << StringView("mut ");
                        break;
                    case HIRBorrowType::Owned:
                        os << StringView("move ");
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
        return;
    }
}
