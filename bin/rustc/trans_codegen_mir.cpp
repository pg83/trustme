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

    ::std::ostream& operator<<(::std::ostream& os, const Fmt<HIRPath>& x) {
        return os << TransMangle(x.e);
    }

    ::std::ostream& operator<<(::std::ostream& os, const Fmt<HIRGenericPath>& x) {
        return os << TransMangle(x.e);
    }

    ::std::ostream& operator<<(::std::ostream& os, const Fmt<HIRSimplePath>& x) {
        return os << TransMangle(x.e);
    }

    ::std::ostream& operator<<(::std::ostream& os, const Fmt<HIRTypeRef>& x) {
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
                auto path = te.trait.path.clone();
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
                os << fmt(te.inner);
            }
            TU_ARMA(Pointer, te) {
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
                os << fmt(te.inner);
            }
            TU_ARMA(NamedFunction, te) {
                os << "fn " << TransMangle(te.path);
            }
            TU_ARMA(Function, e) {
                if (e.isUnsafe) {
                    os << "unsafe ";
                }
                if (e.abi != "") {
                    os << "extern \"" << e.abi << "\" ";
                }
                os << "fn(";
                for (const auto& t : e.argTypes) {
                    os << fmt(t) << ", ";
                }
                os << ") -> " << fmt(e.rettype);
            }
            break;
            case HIRTypeData::TAG_NodeType:
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
                TU_ARM(x.e, LValue, e)
                os << fmt(e);
                break;
                TU_ARM(x.e, Borrow, e) {
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

        const HIRCrate& crate;
        ::StaticTraitResolve resolve_;

        ::std::string outfilePath;
        ::std::ofstream of;
        const MIRTypeResolve* mirRes;

    public:
        CodeGeneratorMonoMir(const WireBoard& wb, const HIRCrate& crate, const ::std::string& outfile)
            : crate(crate)
            , resolve_(wb)
            , outfilePath(outfile)
            , of(outfilePath + ".mir")
        {
            for (const auto& crateName : crate.extCratesOrdered) {
                of << "crate \"" << FmtEscaped(crate.extCrates.at(crateName).path) << ".mir\";\n";
            }
        }

        void finalise(const TransOptions& opt, CodegenOutput outTy, const ::std::string& hirFile) override {
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

                // Bind `panic_impl` lang item to the item tagged with `panic_implementation`.
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

            // The requested output is a completion marker; MonoMIR is stored in the sibling `.mir` file.
            {
                ::std::ofstream of(outfilePath);
                if (!of.good()) {
                    // TODO: Error?
                }
            }
        }

        void emitType(const HIRTypeData* ty) override {
            TRACE_FUNCTION_F(ty);
            MIRFunction emptyFcn;
            MIRTypeResolve topMirRes {
                sp, resolve_, FMT_CB(ss, ss << "type " << ty;), HIRTypeRef(), {}, emptyFcn
            };
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

        // TODO: Move this to a more common location
        MetadataType metadataType(const HIRTypeData* ty) const {
            if ((ty->is_Primitive() && ty->as_Primitive() == HIRCoreType::Str) || ty->is_Slice()) {
                return MetadataType::Slice;
            } else if (ty->is_TraitObject()) {
                return MetadataType::TraitObject;
            } else if (ty->is_Path()) {
                const auto& te = ty->as_Path();
                switch (te.binding.tag()) {
                    TU_ARM(te.binding, Struct, tpb) {
                        switch (tpb->structMarkings.dstType) {
                            case HIRStructMarkings::DstType::None:
                                return MetadataType::None;
                            case HIRStructMarkings::DstType::Possible: {
                                // TODO: How to figure out? Lazy way is to check the monomorpised type of the last field (structs only)
                                const auto& path = ty->as_Path().path.data.as_Generic();
                                const auto& str = *ty->as_Path().binding.as_Struct();
                                auto monomorph = [&](const auto& tpl) {
                                    return resolve_.monomorphExpand(sp, tpl, MonomorphStatePtr(crate.types, ty, &path.params, nullptr));
                                };
                                TU_MATCHA((str.data), (se), (Unit, MIR_BUG(*mirRes, "Unit-like struct with DstType::Possible");), (Tuple, return metadataType(monomorph(se.back().ent));), (Named, return metadataType(monomorph(se.back().ty));))
                                //MIR_TODO(*m_mir_res, "Determine DST type when ::Possible - " << ty);
                                return MetadataType::None;
                            }
                            case HIRStructMarkings::DstType::Slice:
                                return MetadataType::Slice;
                            case HIRStructMarkings::DstType::TraitObject:
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

        void emitStruct(const Span& sp, const HIRGenericPath& p, const HIRStruct& item) override {
            MIRFunction emptyFcn;
            MIRTypeResolve topMirRes {
                sp, resolve_, FMT_CB(ss, ss << "struct " << p;), HIRTypeRef(), {}, emptyFcn
            };
            mirRes = &topMirRes;

            auto dropGluePath = HIRPath(crate.types.path(p.clone(), &item), "#drop_glue");

            TRACE_FUNCTION_F(p);
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

            // Generate the drop glue (and determine if there is any)
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

        void emitConstructorEnum(const Span& sp, const HIRGenericPath& varPath, const HIREnum& item, size_t varIdx) override {
            TRACE_FUNCTION_F(varPath);

            HIRTypeRef tmp;
            MonomorphStatePtr ms(crate.types, nullptr, &varPath.params, nullptr);
            auto monomorph = [&](const auto& x) {
                return resolve_.monomorphExpandOpt(sp, tmp, x, ms);
            };

            auto enumPath = varPath.clone();
            enumPath.path.popComponent();

            // Create constructor function
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

        void emitConstructorStruct(const Span& sp, const HIRGenericPath& p, const HIRStruct& item) override {
            TRACE_FUNCTION_F(p);
            HIRTypeRef tmp;
            MonomorphStatePtr ms(crate.types, nullptr, &p.params, nullptr);
            auto monomorph = [&](const auto& x) {
                return resolve_.monomorphExpandOpt(sp, tmp, x, ms);
            };
            // Create constructor function
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

        void emitUnion(const Span& sp, const HIRGenericPath& p, const HIRUnion& item) override {
            MIRFunction emptyFcn;
            MIRTypeResolve topMirRes {
                sp, resolve_, FMT_CB(ss, ss << "union " << p;), HIRTypeRef(), {}, emptyFcn
            };
            mirRes = &topMirRes;

            TRACE_FUNCTION_F(p);
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

        void emitEnum(const Span& sp, const HIRGenericPath& p, const HIREnum& item) override {
            MIRFunction emptyFcn;
            MIRTypeResolve topMirRes {
                sp, resolve_, FMT_CB(ss, ss << "enum " << p;), HIRTypeRef(), {}, emptyFcn
            };
            mirRes = &topMirRes;

            TRACE_FUNCTION_F(p);
            HIRTypeRef ty = crate.types.path(p.clone(), &item);

            // Generate the drop glue (and determine if there is any)
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
                        of << ::std::hex << "\\x0" << val << ::std::dec;
                    } else {
                        of << ::std::hex << "\\x" << val << ::std::dec;
                    }
                }
                of << "\"";
            };

            switch (repr->variants.tag()) {
                    TU_ARM(repr->variants, None, _e) {
                    }
                    TU_ARM(repr->variants, Linear, e) {
                        of << "\t@[" << e.field.index << ", " << e.field.subFields << "] = {\n";
                        for (size_t i = 0; i < e.numVariants; i++) {
                            of << "\t\t";

                            if (e.isNiche(i)) {
                                of << "*";
                            } else {
                                emitValue(e.field, U128(e.tagValue(i)));
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

        void emitStaticLocal(const HIRPath& p, const HIRStatic& item, const TransParams& params, const EncodedLiteral& encoded) override {
            MIRFunction emptyFcn;
            MIRTypeResolve topMirRes {
                sp, resolve_, FMT_CB(ss, ss << "static " << p;), HIRTypeRef(), {}, emptyFcn
            };
            mirRes = &topMirRes;

            TRACE_FUNCTION_F(p);

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

        void emitFunctionExt(const HIRPath& p, const HIRFunction& item, const TransParams& params) override {
            MIRFunction emptyFcn;
            MIRTypeResolve topMirRes {
                sp, resolve_, FMT_CB(ss, ss << "extern fn " << p;), HIRTypeRef(), {}, emptyFcn
            };
            mirRes = &topMirRes;
            TRACE_FUNCTION_F(p);

            // If the function is a C external, emit as such
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

        void emitFunctionCode(const HIRPath& p, const HIRFunction& item, const TransParams& params, bool isExternDef, const MIRFunctionPointer& code) override {
            TRACE_FUNCTION_F(p);

            MIRTypeResolve::argsT argTypes;
            for (const auto& ent : item.args) {
                argTypes.push_back(::std::make_pair(HIRPattern{}, params.monomorph(resolve_, ent.second)));
            }

            HIRTypeRef retTypeTmp;
            const auto& retType = monomorphiseFcnReturn(retTypeTmp, item, params);

            MIRTypeResolve localMirRes {
                sp, resolve_, FMT_CB(ss, ss << p;), retType, argTypes, *code
            };
            mirRes = &localMirRes;

            // - Signature
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
                            TU_ARM(stmt, Assign, se) {
                                of << "ASSIGN " << fmt(se.dst) << " = ";
                                switch (se.src.tag()) {
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
                                        TU_ARMA(Label, v) {
                                            of << "label " << v;
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
                        TU_ARM(term, Asm2, e) {
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
                        }
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
                            //}
                            of << "_ = " << e.defTarget;
                            of << " }\n";
                        }
                        break;
                        TU_ARM(term, Drop, e) {
                            of << "DROP " << fmt(e.slot);
                            if (e.kind == MIRDropKind::SHALLOW) {
                                of << " SHALLOW";
                            }
                            if (e.flagIdx != ~0u) {
                                of << " IF df" << e.flagIdx;
                            }
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
                        TU_ARM(term, TailCall, e) {
                            of << "TAILCALL ";
                            TU_MATCHA((e.fcn), (f),
                                (Intrinsic, of << "\"" << f.name << "\"";),
                                (Value, of << "(" << fmt(f) << ")";),
                                (Path, of << fmt(f);)
                            )
                            of << "(";
                            for (const auto& arg : e.args) {
                                of << fmt(arg) << ", ";
                            }
                            of << ")\n";
                        }
                        break;
                }
                of << "\t}\n";
            }

            of << "}\n";

            mirRes = nullptr;
        }

        void emitGlobalAsm(const HIRGlobalAssembly&) override {
            TODO(Span(), "global_asm! codegen");
        }

    private:
        const HIRTypeData* monomorphiseFcnReturn(HIRTypeRef& tmp, const HIRFunction& item, const TransParams& params) {
            bool hasErased = visitTyWith(item.returnType, [&](const auto& x) {
                return x->is_ErasedType();
            });

            if (hasErased || monomorphiseTypeNeeded(item.returnType)) {
                // If there's an erased type, make a copy with the erased type expanded
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
    };

    Span CodeGeneratorMonoMir::sp;
}

::std::unique_ptr<CodeGenerator> TransCodegenGetGeneratorMonoMir(const WireBoard& wb, const HIRCrate& crate, const ::std::string& outfile) {
    return ::std::unique_ptr<CodeGenerator>(new CodeGeneratorMonoMir(wb, crate, outfile));
}
