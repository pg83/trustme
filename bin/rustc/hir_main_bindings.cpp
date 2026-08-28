#include "hir_main_bindings.h"

#include "hir_hir.h"
#include "mir_mir.h"
#include "hir_expr.h"
#include "hir_visitor.h"
#include "hir_expr_state.h"
#include "hir_typeck_monomorph.h" // monomorphise_path_needed
#include "hir_serialise_lowlevel.h"
#include "macro_rules_macro_rules.h"

#include <std/mem/obj_pool.h>

#include <typeinfo>

using namespace stl;

namespace {
    bool isMetadataFile(const auto& filename) {
        ::std::ifstream direct(filename, ::std::ios_base::in | ::std::ios_base::binary);
        unsigned char header[2] = {};
        if (direct.read(reinterpret_cast<char*>(header), sizeof(header))) {
            const unsigned word = static_cast<unsigned>(header[0]) * 256 + header[1];
            if ((header[0] & 0x0f) == 8 && word % 31 == 0) {
                return true;
            }
        }
        return false;
    }

    auto metadataFilename(const auto& filename) {
        // New rlibs are the compressed HIR stream itself. Dylibs and proc
        // macros keep that stream in a `.rlib` companion. Old artifacts used
        // a sibling `.hir`; recognise all three layouts during the transition.
        if (isMetadataFile(filename)) {
            return filename;
        }
        auto rlib = filename + ".rlib";
        if (isMetadataFile(rlib)) {
            return rlib;
        }
        return filename + ".hir";
    }
}

//namespace {

template <typename T>
struct D {};

class HirDeserialiser {
    RcString crateName;
    ::std::vector<HIRTypeRef> types;
    HIRSerialiseReader& in;
    HIRTypeInterner& typeInterner;
    u32& id;
    HIRPublicity privateVisibility = HIRPublicity::newNone();

public:
    ObjPool& pool;

    HirDeserialiser(u32& id, ObjPool& pool, HIRSerialiseReader& in, HIRTypeInterner& typeInterner);

    RcString readIstring();

    ::std::string readString();

    bool readBool();

    u8 readU8();

    size_t deserialiseCount();

    template <typename V>
    ::std::map<::std::string, V> deserialiseStrmap();

    template <typename V>
    ::std::unordered_map<::std::string, V> deserialiseStrumap();

    template <typename V>
    ::std::unordered_multimap<::std::string, V> deserialiseStrummap();

    template <typename V>
    ::std::map<RcString, V> deserialiseIstrmap();

    // Pool-allocated variant: module item tables are the single biggest heap
    // consumer when loading extern crates, and they live for the whole
    // compilation, so they belong in the arena rather than in unique_ptrs.
    template <typename T>
    ::std::unordered_map<RcString, T*> deserialiseIstrumapPooled();

    template <typename V>
    ::std::unordered_map<RcString, V> deserialiseIstrumap();

    template <typename V>
    ::std::unordered_multimap<RcString, V> deserialiseIstrummap();

    template <typename V>
    ::std::map<HIRSimplePath, V> deserialisePathmap();

    template <typename T, typename F>
    ::std::vector<T> deserialiseVecC(F cb);

    template <typename T>
    ::std::vector<T> deserialiseVec();

    template <typename T, typename F>
    ThinVector<T> deserialiseThinvecC(F cb);

    template <typename T>
    ThinVector<T> deserialiseThinvec();

    template <typename T>
    ::std::set<T> deserialiseSet();

    HIRPublicity deserialisePub();

    template <typename T>
    HIRVisEnt<T> deserialiseVisent();

    template <typename T>
    ::std::unique_ptr<T> deserialisePtr();

    HIRArraySize deserialiseArraysize();
    HIRGenericRef deserialiseGenericref();
    HIRTypeRef deserialiseType();
    HIRSimplePath deserialiseSimplepath();
    HIRPathParams deserialisePathparams();
    HIRGenericPath deserialiseGenericpath();
    HIRTraitPath deserialiseTraitpath();
    HIRPath deserialisePath();

    HIRGenericParams deserialiseGenericparams();
    HIRTypeParamDef deserialiseTyparamdef();
    HIRValueParamDef deserialiseValueparamdef();
    HIRGenericBound deserialiseGenericbound();

    void deserialiseCrate(HIRCrate& rv);
    HIRExternLibrary deserialiseExtlib();
    HIRModule deserialiseModule();

    HIRProcMacro deserialiseProcmacro();

    HIRTypeImpl deserialiseTypeimpl();

    HIRTraitImpl deserialiseTraitimpl();

    HIRMarkerImpl deserialiseMarkerimpl();

    Ident::Hygiene deserialiseHygine();

    ::MacroRulesPtr deserialiseMacrorulesptr();

    ::MacroRules deserialiseMacrorules();

    ::SimplePatIfCheck deserialiseSimplepatifcheck();

    ::SimplePatEnt deserialiseSimplepatent();

    ::MacroPatEnt deserialiseMacropatent();

    ::MacroRulesArm deserialiseMacrorulesarm();

    ::MacroExpansionEnt deserialiseMacroexpansionent();

    ::MacroExpansionConcatEnt deserialiseMacroexpansionconcatent();

    ::Token deserialiseToken();

    ::Token::Data deserialiseTokendata();

    HIRConstGenericUnevaluated deserialiseConstgenericUnevaluated();
    HIRConstGeneric deserialiseConstgeneric();
    EncodedLiteral deserialiseEncodedliteral();

    HIRExprPtr deserialiseExprptr();

    MIRFunctionPointer deserialiseMir();
    MIRBasicBlock deserialiseMirBasicblock();
    MIRStatement deserialiseMirStatement();
    AsmOptions deserialiseAsmOptions();
    AsmLineFragment deserialiseAsmLineFrag();
    AsmLine deserialiseAsmLine();
    AsmRegisterSpec deserialiseAsmSpec();
    MIRAsmParam deserialiseAsmParam();
    MIRTerminator deserialiseMirTerminator();
    MIRTerminator deserialise_mir_terminator_();
    MIRUnwindAction deserialiseMirUnwindAction();
    MIRSwitchValues deserialiseMirSwitchvalues();
    MIRCallTarget deserialiseMirCalltarget();

    MIRParam deserialiseMirParam();

    MIRLValue deserialiseMirLvalue();

    MIRLValue::Wrapper deserialiseMirLvalueWrapper();

    MIRLValue deserialise_mir_lvalue_();

    MIRRValue deserialiseMirRvalue();

    MIRConstant deserialiseMirConstant();

    HIRExternType deserialiseExterntype();

    HIRTraitAlias deserialiseTraitalias();

    HIRTypeItem deserialiseTypeitem();

    HIRValueItem deserialiseValueitem();

    HIRMacroItem deserialiseMacroitem();

    HIRLinkage deserialiseLinkage();

    // - Value items
    HIRFunction deserialiseFunction();

    HIRFunction::Markings deserialiseFunctionMarkings();

    ::std::vector<::std::pair<HIRPattern, HIRTypeRef>> deserialiseFcnargs();

    HIRConstant deserialiseConstant();

    HIRStatic deserialiseStatic();

    // - Type items
    HIRTypeAlias deserialiseTypealias();

    HIRTraitMarkings deserialiseMarkings();

    HIRStructMarkings deserialiseStrMarkings();

    HIREnum deserialiseEnum();
    HIREnum::DataVariant deserialiseEnumdatavariant();
    HIREnum::ValueVariant deserialiseEnumvaluevariant();

    HIRStruct deserialiseStruct();
    HIRStructField deserialiseStructField();
    HIRUnion deserialiseUnion();
    HIRTrait deserialiseTrait();

    HIRTraitValueItem deserialiseTraitvalueitem();

    HIRAssociatedType deserialiseAssociatedtype();
};

#define DEF_D(ty, ...)                      \
    struct D<ty> {                          \
        static ty des(HirDeserialiser& d) { \
            __VA_ARGS__                     \
        }                                   \
    };

template <>
DEF_D(::std::string, return d.readString(););
template <>
DEF_D(RcString, return d.readIstring(););
template <>
DEF_D(bool, return d.readBool(););
template <>
DEF_D(u8, return d.readU8(););

template <typename T>
DEF_D(::std::unique_ptr<T>, return d.deserialisePtr<T>();)

template <typename T>
DEF_D(::std::vector<T>, return d.deserialiseVec<T>();)
template <typename T, typename U>
struct D<::std::pair<T, U>> {
    static ::std::pair<T, U> des(HirDeserialiser& d);
};

template <typename T>
DEF_D(HIRVisEnt<T>, return d.deserialiseVisent<T>();)

template <>
DEF_D(HIRTypeRef, return d.deserialiseType();)
template <>
DEF_D(HIRSimplePath, return d.deserialiseSimplepath();)
template <>
DEF_D(HIRGenericPath, return d.deserialiseGenericpath();)
template <>
DEF_D(HIRTraitPath, return d.deserialiseTraitpath();)

template <>
DEF_D(HIRTypeParamDef, return d.deserialiseTyparamdef();)
template <>
DEF_D(HIRValueParamDef, return d.deserialiseValueparamdef();)
template <>
DEF_D(HIRGenericBound, return d.deserialiseGenericbound();)

template <>
DEF_D(HIRValueItem, return d.deserialiseValueitem();)
template <>
DEF_D(HIRTypeItem, return d.deserialiseTypeitem();)
template <>
DEF_D(HIRMacroItem, return d.deserialiseMacroitem();)

template <>
DEF_D(HIREnum::ValueVariant, return d.deserialiseEnumvaluevariant();)
template <>
DEF_D(HIREnum::DataVariant, return d.deserialiseEnumdatavariant();)
template <>
DEF_D(HIRStructField, return d.deserialiseStructField();)
//template<> DEF_D( ::HIR::Literal, return d.deserialise_literal(); )
template <>
DEF_D(HIRConstGeneric, return d.deserialiseConstgeneric();)

template <>
DEF_D(HIRAssociatedType, return d.deserialiseAssociatedtype();)
template <>
DEF_D(HIRTraitValueItem, return d.deserialiseTraitvalueitem();)

template <>
DEF_D(MIRParam, return d.deserialiseMirParam();)
template <>
DEF_D(MIRLValue::Wrapper, return d.deserialiseMirLvalueWrapper();)
template <>
DEF_D(MIRLValue, return d.deserialiseMirLvalue();)
template <>
DEF_D(AsmLineFragment, return d.deserialiseAsmLineFrag();)
template <>
DEF_D(AsmLine, return d.deserialiseAsmLine();)
template <>
DEF_D(MIRAsmParam, return d.deserialiseAsmParam();)
template <>
DEF_D(MIRStatement, return d.deserialiseMirStatement();)
template <>
DEF_D(MIRBasicBlock, return d.deserialiseMirBasicblock();)

template <>
DEF_D(HIRTraitPath::AtyEqual, auto src = d.deserialiseGenericpath(); return HIRTraitPath::AtyEqual{mv$(src), d.deserialisePathparams(), d.deserialiseType()};)
template <>
DEF_D(HIRTraitPath::AtyBound, auto src = d.deserialiseGenericpath(); return HIRTraitPath::AtyBound{mv$(src), d.deserialisePathparams(), d.deserialiseVec<HIRTraitPath>()};);

template <>
DEF_D(HIRProcMacro, return d.deserialiseProcmacro();)
template <>
DEF_D(HIRTypeImpl, return d.deserialiseTypeimpl();)
template <>
DEF_D(HIRTraitImpl, return d.deserialiseTraitimpl();)
template <>
DEF_D(HIRMarkerImpl, return d.deserialiseMarkerimpl();)
template <>
DEF_D(::MacroRulesPtr, return d.deserialiseMacrorulesptr();)
template <>
DEF_D(unsigned int, return static_cast<unsigned int>(d.deserialiseCount());)

template <typename T>
DEF_D(HIRCrate::ImplGroup<std::unique_ptr<T>>, HIRCrate::ImplGroup<std::unique_ptr<T>> rv; rv.named = d.deserialisePathmap<::std::vector<::std::unique_ptr<T>>>(); rv.nonNamed = d.deserialiseVec<::std::unique_ptr<T>>(); rv.generic = d.deserialiseVec<::std::unique_ptr<T>>(); return rv;)
template <>
DEF_D(HIRExternLibrary, return d.deserialiseExtlib();)

HIRGenericRef HirDeserialiser::deserialiseGenericref() {
    return HIRGenericRef{in.readIstring(), in.readU16()};
}

HIRArraySize HirDeserialiser::deserialiseArraysize() {
    switch (auto tag = in.readTag()) {
#define _(x, ...)               \
    case HIRArraySize::TAG_##x: \
        return HIRArraySize::make_##x(__VA_ARGS__);
        _(Known, in.readU64c())
        _(Unevaluated, deserialiseConstgeneric())
        default:
            BUG(Span(), "Bad tag for HIR::ArraySize - " << tag);
#undef _
    }
}

HIRTypeRef HirDeserialiser::deserialiseType() {
    HIRTypeRef rv;

    auto idx = in.readCount();
    if (idx != ~0u) {
        rv = types.at(idx);
        return rv;
    }
    auto _ = in.openObject("HIR::TypeData");

    switch (auto tag = in.readTag()) {
#define _(x, ...)                                                     \
    case HIRTypeData::TAG_##x:                                        \
        rv = typeInterner.intern(HIRTypeData::make_##x(__VA_ARGS__)); \
        break;
        _(Infer, {~0u, HIRInferClass::None})
        _(Diverge, {})
        _(Primitive, static_cast<HIRCoreType>(in.readTag()))
        _(Path, {deserialisePath(), {}})
        _(Generic, deserialiseGenericref())
        _(TraitObject, {deserialiseTraitpath(), deserialiseVec<HIRGenericPath>(), in.readIstring(), in.readBool()})
        case HIRTypeData::TAG_ErasedType:
            TODO(Span(), "ErasedType");
            _(Array, {deserialiseType(), deserialiseArraysize()})
            _(Slice, {deserialiseType()})
            _(Tuple, deserialiseVec<HIRTypeRef>())
            _(Borrow, {static_cast<HIRBorrowType>(in.readTag()), deserialiseType()})
            _(Pointer, {static_cast<HIRBorrowType>(in.readTag()), deserialiseType()})
            _(NamedFunction, {deserialisePath()})
            _(Function, {in.readBool(), in.readBool(), in.readIstring(), deserialiseType(), deserialiseVec<HIRTypeRef>(), in.readBool(), in.readIstring(), in.readBool()})
        case HIRTypeData::TAG_Pattern: {
            auto inner = deserialiseType();
            HIRTypePattern pattern;
            auto count = in.readCount();
            pattern.alternatives.reserve(count);
            while (count--) {
                HIRTypePatternRange range;
                range.hasStart = in.readBool();
                if (range.hasStart) {
                    range.start = deserialiseConstgeneric();
                }
                range.hasEnd = in.readBool();
                if (range.hasEnd) {
                    range.end = deserialiseConstgeneric();
                }
                range.endInclusive = in.readBool();
                pattern.alternatives.push_back(mv$(range));
            }
            rv = typeInterner.intern(HIRTypeData::make_Pattern({inner, mv$(pattern)}));
            break;
        }
#undef _
        default:
            BUG(Span(), "Bad tag for HIR::ASTType* - " << tag);
    }
    types.push_back(rv);
    return rv;
}

HIRSimplePath HirDeserialiser::deserialiseSimplepath() {
    auto rv = HIRSimplePath{deserialiseThinvec<RcString>()};
    // HACK! If the read crate name is empty, replace it with the name we're loaded with
    if (rv.crateName() == "" && rv.components().size() > 0) {
        assert(crateName != "");
        rv.updateCrateName(crateName);
    }
    return rv;
}

HIRPathParams HirDeserialiser::deserialisePathparams() {
    HIRPathParams rv;
    rv.types = deserialiseThinvec<HIRTypeRef>();
    rv.values = deserialiseThinvec<HIRConstGeneric>();
    return rv;
}

HIRGenericPath HirDeserialiser::deserialiseGenericpath() {
    HIRGenericPath rv;
    rv.path = deserialiseSimplepath();
    rv.params = deserialisePathparams();
    return rv;
}

HIRTraitPath HirDeserialiser::deserialiseTraitpath() {
    auto _ = in.openObject("HIR::TraitPath");
    auto gpath = deserialiseGenericpath();
    auto tys = deserialiseIstrmap<HIRTraitPath::AtyEqual>();
    auto bounds = deserialiseIstrmap<HIRTraitPath::AtyBound>();
    auto constness = static_cast<HIRBoundConstness>(in.readU8());
    return HIRTraitPath{mv$(gpath), mv$(tys), mv$(bounds), nullptr, constness};
}

HIRPath HirDeserialiser::deserialisePath() {
    switch (auto tag = in.readTag()) {
        case 0:
            return HIRPath(deserialiseGenericpath());
        case 1:
            return HIRPath(HIRPath::Data::Data_UfcsInherent{deserialiseType(), in.readIstring(), deserialisePathparams(), deserialisePathparams()});
        case 2: {
            return HIRPath(HIRPath::Data::Data_UfcsKnown{deserialiseType(), deserialiseGenericpath(), in.readIstring(), deserialisePathparams()});
        }
        default:
            BUG(Span(), "Bad tag for HIR::Path - " << tag);
    }
}

HIRGenericParams HirDeserialiser::deserialiseGenericparams() {
    HIRGenericParams params;
    auto paramKindCount = in.readCount();
    params.paramKinds.grow(paramKindCount);
    for (size_t i = 0; i < paramKindCount; i++) {
        auto kind = static_cast<HIRGenericParamKind>(in.readU8());
        ASSERT_BUG(Span(), kind == HIRGenericParamKind::Type || kind == HIRGenericParamKind::Value, "Invalid generic parameter kind");
        params.paramKinds.pushBack(kind);
    }
    params.types = deserialiseVec<HIRTypeParamDef>();
    params.values = deserialiseVec<HIRValueParamDef>();
    params.bounds = deserialiseVec<HIRGenericBound>();
    return params;
}

HIRTypeParamDef HirDeserialiser::deserialiseTyparamdef() {
    auto rv = HIRTypeParamDef{in.readIstring(), deserialiseType(), in.readBool()};
    return rv;
}

HIRValueParamDef HirDeserialiser::deserialiseValueparamdef() {
    auto rv = HIRValueParamDef{in.readIstring(), deserialiseType()};
    rv.defaultValue = deserialiseConstgeneric();
    return rv;
}

HIRGenericBound HirDeserialiser::deserialiseGenericbound() {
    switch (auto tag = in.readTag()) {
        case 2: {
            auto type = deserialiseType();
            auto trait = deserialiseTraitpath();
            auto constness = static_cast<HIRBoundConstness>(in.readU8());
            auto isTrivial = in.readBool();
            return HIRGenericBound::make_TraitBound({mv$(type), mv$(trait), constness, isTrivial});
        }
        case 3:
            return HIRGenericBound::make_TypeEquality({deserialiseType(), deserialiseType()});
        default:
            BUG(Span(), "Bad tag for HIR::GenericBound - " << tag);
    }
}

HIREnum HirDeserialiser::deserialiseEnum() {
    auto _ = in.openObject("HIR::Enum");

    struct H {
        static HIREnum::Class deserialiseEnumclass(HirDeserialiser& des) {
            switch (auto tag = des.in.readTag()) {
                case HIREnum::Class::TAG_Data:
                    return HIREnum::Class::make_Data(des.deserialiseVec<HIREnum::DataVariant>());
                case HIREnum::Class::TAG_Value:
                    return HIREnum::Class::make_Value({
                        des.deserialiseVec<HIREnum::ValueVariant>(),
                    });
                default:
                    BUG(Span(), "Bad tag for HIR::Enum::Class - " << tag);
            }
        }
    };

    auto params = deserialiseGenericparams();
    auto isCRepr = in.readBool();
    auto tagRepr = static_cast<HIREnum::Repr>(in.readTag());
    auto forcedAlignment = static_cast<unsigned>(in.readCount());
    const bool mustUse = in.readBool();
    auto rv = HIREnum{mv$(params), isCRepr, tagRepr, H::deserialiseEnumclass(*this), true, deserialiseMarkings()};
    rv.forcedAlignment = forcedAlignment;
    rv.mustUse = mustUse;
    return rv;
}

HIREnum::DataVariant HirDeserialiser::deserialiseEnumdatavariant() {
    auto name = in.readIstring();
    return HIREnum::DataVariant{mv$(name), in.readBool(), deserialiseType(), HIRExprPtr{}, U128(in.readU64())};
}

HIREnum::ValueVariant HirDeserialiser::deserialiseEnumvaluevariant() {
    auto name = in.readIstring();
    return HIREnum::ValueVariant{mv$(name), HIRExprPtr{}, U128(in.readU64())};
}

HIRUnion HirDeserialiser::deserialiseUnion() {
    auto params = deserialiseGenericparams();
    auto repr = static_cast<HIRUnion::Repr>(in.readTag());
    auto variants = deserialiseVec<HIRStructField>();
    auto forcedAlignment = static_cast<unsigned>(in.readCount());
    auto maxFieldAlignment = static_cast<unsigned>(in.readCount());
    const bool mustUse = in.readBool();
    auto markings = deserialiseMarkings();

    auto rv = HIRUnion{mv$(params), repr, mv$(variants), mv$(markings)};
    rv.forcedAlignment = forcedAlignment;
    rv.maxFieldAlignment = maxFieldAlignment;
    rv.mustUse = mustUse;
    return rv;
}

HIRStruct HirDeserialiser::deserialiseStruct() {
    auto _ = in.openObject("HIR::Struct");
    auto params = deserialiseGenericparams();
    auto repr = static_cast<HIRStruct::Repr>(in.readTag());

    HIRStruct::Data data;
    switch (auto tag = in.readTag()) {
        case HIRStruct::Data::TAG_Unit:
            data = HIRStruct::Data::make_Unit({});
            break;
        case HIRStruct::Data::TAG_Tuple:
            data = HIRStruct::Data(deserialiseVec<HIRVisEnt<HIRTypeRef>>());
            break;
        case HIRStruct::Data::TAG_Named:
            data = HIRStruct::Data(deserialiseVec<HIRStructField>());
            break;
        default:
            BUG(Span(), "Bad tag for HIR::Struct::Data - " << tag);
    }
    unsigned forcedAlignment = in.readCount();
    unsigned maxFieldAlignment = in.readCount();
    const bool mustUse = in.readBool();
    auto markings = deserialiseMarkings();
    auto strMarkings = deserialiseStrMarkings();

    auto rv = HIRStruct{mv$(params), repr, mv$(data), forcedAlignment, mv$(markings), mv$(strMarkings)};
    rv.maxFieldAlignment = maxFieldAlignment;
    rv.mustUse = mustUse;
    return rv;
}

HIRStructField HirDeserialiser::deserialiseStructField() {
    return HIRStructField{in.readIstring(), deserialisePub(), deserialiseType(), in.readBool() ? ::std::make_unique<HIRGenericPath>(deserialiseGenericpath()) : nullptr};
}

HIRTrait HirDeserialiser::deserialiseTrait() {
    auto _ = in.openObject("HIR::Trait");

    HIRTrait rv{deserialiseGenericparams(), {}};
    const auto traitFlags = in.readU8();
    rv.isMarker = traitFlags & 1;
    rv.isFundamental = traitFlags & 2;
    rv.isCoinductive = (traitFlags & 4) || rv.isMarker;
    rv.isConst = traitFlags & 8;
    rv.skipArrayDuringMethodDispatch = traitFlags & 16;
    rv.skipBoxedSliceDuringMethodDispatch = traitFlags & 32;
    rv.mustUse = traitFlags & 64;
    rv.types = deserialiseIstrumap<HIRAssociatedType>();
    rv.values = deserialiseIstrumap<HIRTraitValueItem>();
    rv.valueIndexes = deserialiseIstrummap<::std::pair<unsigned int, HIRGenericPath>>();
    rv.typeIndexes = deserialiseIstrumap<unsigned int>();
    rv.vtableParentTraitsStart = in.readCount();
    rv.allParentTraits = deserialiseVec<HIRTraitPath>();
    rv.vtablePath = deserialiseSimplepath();
    return rv;
}

HIRConstGenericUnevaluated HirDeserialiser::deserialiseConstgenericUnevaluated() {
    auto selfType = in.readBool() ? deserialiseType() : nullptr;
    auto pI = deserialisePathparams();
    auto pM = deserialisePathparams();
    auto rv = HIRConstGenericUnevaluated(deserialiseExprptr());
    rv.selfType = selfType;
    rv.paramsImpl = std::move(pI);
    rv.paramsItem = std::move(pM);
    return rv;
}

HIRConstGeneric HirDeserialiser::deserialiseConstgeneric() {
    switch (auto tag = in.readTag()) {
#define _(x, ...)                  \
    case HIRConstGeneric::TAG_##x: \
        return HIRConstGeneric::make_##x(__VA_ARGS__);
        _(Infer, {})
        _(Unevaluated, std::make_unique<HIRConstGenericUnevaluated>(deserialiseConstgenericUnevaluated()))
        _(Generic, deserialiseGenericref())
        _(Evaluated, freezeEncodedLiteral(pool, deserialiseEncodedliteral()))
#undef _
        default:
            BUG(Span(), "Unknown HIR::ConstGeneric tag when deserialising - " << tag);
    }
}

EncodedLiteral HirDeserialiser::deserialiseEncodedliteral() {
    EncodedLiteral rv;
    rv.bytes = deserialiseVec<u8>();

    auto nreloc = in.readCount();
    rv.relocations.reserve(nreloc);
    for (size_t i = 0; i < nreloc; i++) {
        auto ofs = in.readCount();
        auto len = in.readCount();
        const bool preserveTrackCaller = in.readBool();
        switch (in.readTag()) {
            case 0:
                rv.relocations.push_back(Reloc::newNamed(ofs, len, deserialisePath(), preserveTrackCaller));
                break;
            case 1:
                rv.relocations.push_back(Reloc::newBytes(ofs, len, in.readString()));
                break;
            default:
                abort();
        }
    }
    return rv;
}

MIRFunctionPointer HirDeserialiser::deserialiseMir() {
    MIRFunction rv;

    rv.locals = deserialiseVec<HIRTypeRef>();
    rv.dropFlags = deserialiseVec<bool>();
    rv.blocks = deserialiseVec<MIRBasicBlock>();

    return MIRFunctionPointer(new MIRFunction(mv$(rv)));
}

MIRBasicBlock HirDeserialiser::deserialiseMirBasicblock() {
    auto statements = deserialiseVec<MIRStatement>();
    auto terminator = deserialiseMirTerminator();
    const auto isCleanup = in.readBool();
    return MIRBasicBlock{mv$(statements), mv$(terminator), isCleanup};
}

AsmOptions HirDeserialiser::deserialiseAsmOptions() {
    AsmOptions o;
    const u16 bitflag1 = in.readU16();
#define BIT(i, fld)            \
    if (bitflag1 & (1 << (i))) \
    fld = true
    BIT(0, o.pure);
    BIT(1, o.nomem);
    BIT(2, o.readonly);
    BIT(3, o.preservesFlags);
    BIT(4, o.noreturn);
    BIT(5, o.nostack);
    BIT(6, o.attSyntax);
    BIT(7, o.raw);
#undef BIT
    return o;
}

AsmLineFragment HirDeserialiser::deserialiseAsmLineFrag() {
    AsmLineFragment lf;
    lf.before = in.readString();
    lf.index = in.readCount();
    lf.modifier = static_cast<char>(in.readI64c());
    return lf;
}

AsmLine HirDeserialiser::deserialiseAsmLine() {
    AsmLine l;
    l.frags = deserialiseVec<AsmLineFragment>();
    l.trailing = in.readString();
    return l;
}

AsmRegisterSpec HirDeserialiser::deserialiseAsmSpec() {
    switch (auto tag = in.readTag()) {
        case AsmRegisterSpec::TAG_Class:
            return static_cast<AsmRegisterClass>(in.readTag());
        case AsmRegisterSpec::TAG_Explicit:
            return in.readString();
        default:
            BUG(Span(), "Bad tag for AsmCommon::RegisterSpec - " << tag);
    }
}

MIRAsmParam HirDeserialiser::deserialiseAsmParam() {
    switch (auto tag = in.readTag()) {
        case MIRAsmParam::TAG_Sym:
            return MIRAsmParam::make_Sym(deserialisePath());
        case MIRAsmParam::TAG_Const:
            return MIRAsmParam::make_Const(deserialiseMirConstant());
        case MIRAsmParam::TAG_Reg:
            return MIRAsmParam::make_Reg({static_cast<AsmDirection>(in.readTag()), deserialiseAsmSpec(), in.readBool() ? ::std::make_unique<MIRParam>(deserialiseMirParam()) : std::unique_ptr<MIRParam>(), in.readBool() ? ::std::make_unique<MIRLValue>(deserialiseMirLvalue()) : std::unique_ptr<MIRLValue>()});
        case MIRAsmParam::TAG_Label:
            return MIRAsmParam::make_Label(static_cast<unsigned int>(in.readCount()));
        default:
            BUG(Span(), "Bad tag for MIR::AsmParam - " << tag);
    }
}

MIRStatement HirDeserialiser::deserialiseMirStatement() {
    MIRStatement rv;
    auto _ = in.openObject("MIR::Statement");

    switch (auto tag = in.readTag()) {
        case 0:
            rv = MIRStatement::make_Assign({deserialiseMirLvalue(), deserialiseMirRvalue()});
            break;
        case 1:
            BUG(Span(), "Obsolete MIR statement Drop in metadata");
        case 2:
            rv = MIRStatement::make_Asm({in.readString(), deserialiseVec<::std::pair<::std::string, MIRLValue>>(), deserialiseVec<::std::pair<::std::string, MIRLValue>>(), deserialiseVec<::std::string>(), deserialiseVec<::std::string>()});
            break;
        case 3: {
            MIRStatement::Data_SetDropFlag sdf;
            sdf.idx = static_cast<unsigned int>(in.readCount());
            sdf.newVal = in.readBool();
            sdf.other = static_cast<unsigned int>(in.readCount());
            rv = MIRStatement::make_SetDropFlag(sdf);
        } break;
        case 4:
            rv = MIRStatement::make_ScopeEnd({deserialiseVec<unsigned int>()});
            break;
        case 5:
            rv = MIRStatement::make_Asm2({deserialiseAsmOptions(), deserialiseVec<AsmLine>(), deserialiseVec<MIRAsmParam>()});
            break;
        case 6:
            rv = MIRStatement::make_SaveDropFlag({deserialiseMirLvalue(), static_cast<unsigned>(in.readCount()), static_cast<unsigned>(in.readCount())});
            break;
        case 7:
            rv = MIRStatement::make_LoadDropFlag({static_cast<unsigned>(in.readCount()), deserialiseMirLvalue(), static_cast<unsigned>(in.readCount())});
            break;
        default:
            BUG(Span(), "Bad tag for MIR::Statement - " << tag);
    }
    return rv;
}

MIRTerminator HirDeserialiser::deserialiseMirTerminator() {
    MIRTerminator rv;
    rv = this->deserialise_mir_terminator_();
    return rv;
}

MIRTerminator HirDeserialiser::deserialise_mir_terminator_() {
    switch (auto tag = in.readTag()) {
#define _(x, ...)                \
    case MIRTerminator::TAG_##x: \
        return MIRTerminator::make_##x(__VA_ARGS__);
        _(Incomplete, {})
        _(Return, {})
        _(UnwindResume, {})
        _(UnwindTerminate, {})
        _(Unreachable, {})
        _(Goto, static_cast<unsigned int>(in.readCount()))
        _(If, {deserialiseMirLvalue(), static_cast<unsigned int>(in.readCount()), static_cast<unsigned int>(in.readCount())})
        _(Switch,
          {deserialiseMirLvalue(),
           deserialiseVecC<unsigned int>([&]() {
            return static_cast<unsigned int>(in.readCount());
        }),
           static_cast<unsigned int>(in.readCount()),
           static_cast<unsigned int>(in.readCount())})
        _(SwitchValue,
          {deserialiseMirLvalue(),
           static_cast<unsigned int>(in.readCount()),
           deserialiseVecC<unsigned int>([&]() {
            return static_cast<unsigned int>(in.readCount());
        }),
           deserialiseMirSwitchvalues()})
        _(Drop, {static_cast<MIRDropKind>(in.readTag()), deserialiseMirLvalue(), static_cast<unsigned int>(in.readCount()), static_cast<unsigned int>(in.readCount()), deserialiseMirUnwindAction()})
        _(Call, {static_cast<unsigned int>(in.readCount()), deserialiseMirUnwindAction(), deserialiseMirLvalue(), deserialiseMirCalltarget(), deserialiseVec<MIRParam>(), {in.readIstring(), static_cast<unsigned int>(in.readCount()), static_cast<unsigned int>(in.readCount())}, in.readBool()})
        _(TailCall, {deserialiseMirCalltarget(), deserialiseVec<MIRParam>(), {in.readIstring(), static_cast<unsigned int>(in.readCount()), static_cast<unsigned int>(in.readCount())}, in.readBool()})
        _(Asm2, {deserialiseAsmOptions(), deserialiseVec<AsmLine>(), deserialiseVec<MIRAsmParam>(), static_cast<unsigned int>(in.readCount())})
#undef _
        default:
            BUG(Span(), "Bad tag for MIR::Terminator - " << tag);
    }
}

MIRUnwindAction HirDeserialiser::deserialiseMirUnwindAction() {
    switch (auto tag = in.readTag()) {
        case MIRUnwindAction::TAG_Continue:
            return MIRUnwindAction::make_Continue({});
        case MIRUnwindAction::TAG_Cleanup:
            return MIRUnwindAction::make_Cleanup(static_cast<unsigned int>(in.readCount()));
        case MIRUnwindAction::TAG_Terminate:
            return MIRUnwindAction::make_Terminate({});
        case MIRUnwindAction::TAG_Unreachable:
            return MIRUnwindAction::make_Unreachable({});
        default:
            BUG(Span(), "Bad tag for MIR::UnwindAction - " << tag);
    }
}

MIRSwitchValues HirDeserialiser::deserialiseMirSwitchvalues() {
    switch (auto tag = in.readTag()) {
#define _(x, ...)                  \
    case MIRSwitchValues::TAG_##x: \
        return MIRSwitchValues::make_##x(__VA_ARGS__);
        _(Unsigned, deserialiseVecC<u64>([&]() {
            return in.readU64c();
        }))
        _(Signed, deserialiseVecC<i64>([&]() {
            return in.readI64c();
        }))
        _(String, deserialiseVec<::std::string>())
        _(ByteString, deserialiseVec<::std::vector<u8>>())
#undef _
        default:
            BUG(Span(), "Bad tag for MIR::SwitchValues - " << tag);
    }
}

MIRCallTarget HirDeserialiser::deserialiseMirCalltarget() {
    switch (auto tag = in.readTag()) {
#define _(x, ...)                \
    case MIRCallTarget::TAG_##x: \
        return MIRCallTarget::make_##x(__VA_ARGS__);
        _(Value, deserialiseMirLvalue())
        _(Path, deserialisePath())
        _(Intrinsic, {in.readIstring(), deserialisePathparams()})
#undef _
        default:
            BUG(Span(), "Bad tag for MIR::CallTarget - " << tag);
    }
}

HIRModule HirDeserialiser::deserialiseModule() {
    auto _ = in.openObject("HIR::Module");

    HIRModule rv;

    // m_traits doesn't need to be serialised
    rv.valueItems = deserialiseIstrumapPooled<HIRVisEnt<HIRValueItem>>();
    rv.modItems = deserialiseIstrumapPooled<HIRVisEnt<HIRTypeItem>>();
    rv.macroItems = deserialiseIstrumapPooled<HIRVisEnt<HIRMacroItem>>();

    return rv;
}

HIRExternLibrary HirDeserialiser::deserialiseExtlib() {
    return HIRExternLibrary{in.readString()};
}

void HirDeserialiser::deserialiseCrate(HIRCrate& rv) {
    // NOTE: This MUST be the first item
    this->crateName = in.readIstring();
    assert(this->crateName != "" && "Empty crate name loaded from metadata");
    privateVisibility = HIRPublicity::newPriv(HIRSimplePath(this->crateName));
    rv.crateName = this->crateName;
    rv.edition = static_cast<ASTEdition>(in.readTag());
    rv.rootModule = deserialiseModule();

    size_t localItemTypeNamePathCount = in.readCount();
    for (size_t i = 0; i < localItemTypeNamePathCount; i++) {
        auto modulePath = deserialiseSimplepath();
        auto* ownerPath = rv.pool->make<HIRPath>(deserialisePath());
        rv.localItemTypeNamePaths = rv.pool->make<HIRLocalItemTypeNamePath>(modulePath, ownerPath, rv.localItemTypeNamePaths);
    }

    rv.typeImpls = D<HIRCrate::ImplGroup<std::unique_ptr<HIRTypeImpl>>>::des(*this);
    rv.traitImpls = deserialisePathmap<HIRCrate::ImplGroup<std::unique_ptr<HIRTraitImpl>>>();
    rv.markerImpls = deserialisePathmap<HIRCrate::ImplGroup<std::unique_ptr<HIRMarkerImpl>>>();

    rv.exportedMacroNames = deserialiseVec<::RcString>();
    rv.langItems = deserialiseStrumap<HIRSimplePath>();

    {
        size_t n = in.readCount();
        for (size_t i = 0; i < n; i++) {
            auto extCrateName = in.readIstring();
            auto extCrateFile = in.readString();
            auto extCrate = HIRExternCrate{};
            extCrate.basename = extCrateFile;
            extCrate.path = extCrateFile;
            rv.extCrates.insert(::std::make_pair(mv$(extCrateName), mv$(extCrate)));
        }
    }

    rv.extLibs = deserialiseVec<HIRExternLibrary>();
    rv.linkPaths = deserialiseVec<::std::string>();
}

//}

HIRCrate* HIRDeserialise(u32& id, ObjPool* pool, HIRTypeInterner& types, const ::std::string& filename) {
    {
        HIRSerialiseReader in{metadataFilename(filename)};
        HirDeserialiser s{id, *pool, in, types};

        auto* rv = pool->make<HIRCrate>(pool, types);
        s.deserialiseCrate(*rv);
        return rv;
    }
}

RcString HIRDeserialiseJustName(const ::std::string& filename) {
    {
        HIRSerialiseReader in{metadataFilename(filename)};

        // NOTE: This is the first item loaded by deserialise_crate
        auto crateName = in.readIstring();
        assert(crateName != "" && "Empty crate name loaded from metadata");
        return crateName;
    }
}

#undef DEF_D

#define NODE_IS(valptr, tysuf) (cast<const HIRExprNode##tysuf>(&*valptr) != nullptr)

namespace {

    class TreeVisitor: public HIRVisitor, public HIRExprVisitor {
        ::std::ostream& os;
        unsigned int indentLevel;

    public:
        TreeVisitor(HIRTypeInterner& types, ::std::ostream& os);

        void visitModule(HIRItemPath p, HIRModule& mod) override;

        void visitTypeImpl(HIRTypeImpl& impl) override;

        virtual void visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) override;

        void visitMarkerImpl(const HIRSimplePath& traitPath, HIRMarkerImpl& impl) override;

        // - Type Items
        void visitTypeAlias(HIRItemPath p, HIRTypeAlias& item) override;

        void visitInherentType(HIRItemPath p, HIRTypeAlias& item) override;

        void visitTrait(HIRItemPath p, HIRTrait& item) override;

        void visitStruct(HIRItemPath p, HIRStruct& item) override;

        void visitEnum(HIRItemPath p, HIREnum& item) override;

        // - Value Items
        void visitFunction(HIRItemPath p, HIRFunction& item) override;

        void visitStatic(HIRItemPath p, HIRStatic& item) override;

        void visitConstant(HIRItemPath p, HIRConstant& item) override;

        // - Misc

        bool nodeIsLeaf(const HIRExprNode& node);

        void visitNodePtr(HIRExprNodeP& nodePtr) override;

        void visit(HIRExprNodeBlock& node) override;

        void visit(HIRExprNodeConstBlock& node) override;

        void visit(HIRExprNodeAsm& node) override;

        void visit(HIRExprNodeAsm2& node) override;

        void visit(HIRExprNodeReturn& node) override;

        void visit(HIRExprNodeYield& node) override;

        void visit(HIRExprNodeAWait& node) override;

        void visit(HIRExprNodeUse& node) override;

        void visit(HIRExprNodeLet& node) override;

        void visit(HIRExprNodeLoop& node) override;

        void visit(HIRExprNodeLoopControl& node) override;

        void visit(HIRExprNodeMatch& node) override;

        void visit(HIRExprNodeAssign& node) override;

        void visit(HIRExprNodeBinOp& node) override;

        void visit(HIRExprNodeUniOp& node) override;

        void visit(HIRExprNodeBorrow& node) override;

        void visit(HIRExprNodeRawBorrow& node) override;

        void visit(HIRExprNodeCast& node) override;

        void visit(HIRExprNodeUnsize& node) override;

        void visit(HIRExprNodeIndex& node) override;

        void visit(HIRExprNodeDeref& node) override;

        void visit(HIRExprNodeEmplace& node) override;

        void visit(HIRExprNodeTupleVariant& node) override;

        void visit(HIRExprNodeCallPath& node) override;

        void visit(HIRExprNodeCallValue& node) override;

        void visit(HIRExprNodeCallMethod& node) override;

        void visit(HIRExprNodeField& node) override;

        void visit(HIRExprNodeLiteral& node) override;

        void visit(HIRExprNodeUnitVariant& node) override;

        void visit(HIRExprNodePathValue& node) override;

        void visit(HIRExprNodeVariable& node) override;

        void visit(HIRExprNodeConstParam& node) override;

        void visit(HIRExprNodeStructLiteral& node) override;

        void visit(HIRExprNodeTuple& node) override;

        void visit(HIRExprNodeArrayList& node) override;

        void visit(HIRExprNodeArraySized& node) override;

        void visit(HIRExprNodeClosure& node) override;

        void visit(HIRExprNodeGenerator& node) override;

        void visit(HIRExprNodeGeneratorWrapper& node) override;

        void visit(HIRExprNodeAsyncBlock& node) override;

    private:
        RepeatLitStr indent() const;

        void incIndent();

        void decIndent();
    };
}

void HIRDump(::std::ostream& sink, const HIRCrate& crate) {
    TreeVisitor tv{crate.types, sink};

    tv.visitCrate(const_cast<HIRCrate&>(crate));
}

void HIRDumpExpr(::std::ostream& sink, const HIRExprPtr& expr) {
    if (!expr) {
        sink << "/*NULL*/";
        return;
    }

    assert(expr.state);
    TreeVisitor tv{expr.state->types, sink};

    const_cast<HIRExprPtr&>(expr)->visit(tv);
}

#undef NODE_IS

//namespace {
class HirSerialiser {
    ::std::map<std::string, size_t> types;
    HIRSerialiseWriter& out;
    HIRTypeInterner& typeInterner;

public:
    HirSerialiser(HIRSerialiseWriter& out, HIRTypeInterner& typeInterner);

    void clear();

    template <typename V>
    void serialiseStrmap(const ::std::map<RcString, V>& map);

    template <typename V>
    void serialiseStrmap(const ::std::map<::std::string, V>& map);

    template <typename V>
    void serialisePathmap(const ::std::map<HIRSimplePath, V>& map);

    template <typename V>
    void serialiseStrmap(const ::std::unordered_map<RcString, V>& map);

    template <typename V>
    void serialiseStrmap(const ::std::unordered_map<::std::string, V>& map);

    template <typename V>
    void serialiseStrmap(const ::std::unordered_multimap<RcString, V>& map);

    template <typename V>
    void serialiseStrmap(const ::std::unordered_multimap<::std::string, V>& map);

    template <typename T>
    void serialiseVec(const ThinVector<T>& vec);

    template <typename T>
    void serialiseVec(const ::std::vector<T>& vec);

    template <typename T>
    void serialise(const ::std::vector<T>& vec);

    template <typename T>
    void serialise(const ::std::set<T>& s);

    void serialise(const HIRPublicity& pub);

    template <typename T>
    void serialise(const HIRVisEnt<T>& e);

    template <typename T>
    void serialise(const HIRVisEnt<T>* e);

    template <typename T>
    void serialise(const ::std::unique_ptr<T>& e);

    template <typename T>
    void serialise(const ::std::pair<::std::string, T>& e);

    template <typename T>
    void serialise(const ::std::pair<RcString, T>& e);

    template <typename T>
    void serialise(const ::std::pair<unsigned int, T>& e);

    //}

    void serialise(bool v);
    ;

    void serialise(unsigned int v);
    ;

    void serialise(u8 v);
    ;

    void serialise(u64 v);
    ;

    void serialise(i64 v);
    ;

    void serialise(const HIRGenericRef& ge);

    void serialiseArraysize(const HIRArraySize& as);

    void serialiseType(const HIRTypeData* ty);

    void serialiseSimplepath(const HIRSimplePath& path);

    void serialisePathparams(const HIRPathParams& pp);

    void serialiseGenericpath(const HIRGenericPath& path);

    void serialise(const HIRGenericPath& path);

    void serialiseTraitpath(const HIRTraitPath& path);

    void serialise(const HIRTraitPath::AtyEqual& e);

    void serialise(const HIRTraitPath::AtyBound& e);

    void serialisePath(const HIRPath& path);

    void serialiseGenerics(const HIRGenericParams& params);

    void serialise(const HIRTypeParamDef& pd);

    void serialise(const HIRValueParamDef& pd);

    void serialise(const HIRGenericBound& b);

    void serialise(const HIRProcMacro& pm);

    template <typename T>
    void serialise(const HIRCrate::ImplGroup<T>& ig);

    void serialiseCrate(const HIRCrate& crate);

    void serialise(const HIRExternLibrary& lib);

    void serialiseModule(const HIRModule& mod);

    void serialiseTypeimpl(const HIRTypeImpl& impl);

    void serialise(const HIRTypeImpl& impl);

    void serialiseTraitimpl(const HIRTraitImpl& impl);

    void serialise(const HIRTraitImpl& impl);

    void serialiseMarkerimpl(const HIRMarkerImpl& impl);

    void serialise(const HIRMarkerImpl& impl);

    void serialise(const HIRTypeData* ty);

    void serialise(const HIRSimplePath& p);

    void serialise(const HIRTraitPath& p);

    void serialise(const ::std::string& v);

    void serialise(const RcString& v);

    void serialise(const Ident::Hygiene& h);

    void serialise(const ::MacroRulesPtr& mac);

    void serialise(const ::MacroRules& mac);

    void serialise(const ::MacroPatEnt& pe);

    void serialise(const ::SimplePatIfCheck& e);

    void serialise(const ::SimplePatEnt& pe);

    void serialise(const ::MacroRulesArm& arm);

    void serialise(const ::MacroExpansionEnt& ent);

    void serialise(const ::MacroExpansionConcatEnt& e);

    void serialise(const ::Token& tok);

    void serialise(const ::Token::Data& td);

    void serialise(const EncodedLiteral& lit);

    void serialise(const HIRConstGenericUnevaluated& v);

    void serialise(const HIRConstGeneric& v);

    void serialise(const HIRExprPtr& exp, bool saveMir = true);

    void serialise(const MIRFunction& mir);

    void serialise(const MIRBasicBlock& block);

    void serialise(const AsmLineFragment& l);

    void serialise(const AsmLine& l);

    void serialise(const AsmRegisterSpec& r);

    void serialise(const MIRAsmParam& p);

    void serialise(const AsmOptions& o);

    void serialise(const MIRStatement& stmt);

    void serialise(const MIRTerminator& term);

    void serialise(const MIRSwitchValues& sv);

    void serialise(const MIRCallTarget& ct);

    void serialise(const MIRParam& p);

    void serialise(const MIRLValue& lv);

    void serialise(const MIRLValue::Wrapper& w);

    void serialise(const MIRRValue& val);

    void serialise(const MIRConstant& v);

    void serialise(const HIRTypeItem& item);

    void serialise(const HIRMacroItem& item);

    void serialise(const HIRValueItem& item);

    void serialise(const HIRLinkage& linkage);

    // - Value items
    void serialise(const HIRFunction& fcn);

    void serialise(const HIRFunction::Markings& m);

    void serialise(const HIRConstant& item);

    void serialise(const HIRStatic& item);

    // - Type items
    void serialise(const HIRTypeAlias& ta);

    void serialise(const HIRTraitAlias& ta);

    void serialise(const HIREnum& item);

    void serialise(const HIREnum::Class& v);

    void serialise(const HIREnum::ValueVariant& v);

    void serialise(const HIREnum::DataVariant& v);

    void serialise(const HIRTraitMarkings& m);

    void serialise(const HIRStructMarkings& m);

    void serialise(const HIRStruct& item);

    void serialise(const HIRStructField& fld);

    void serialise(const HIRUnion& item);

    void serialise(const HIRExternType& item);

    void serialise(const HIRTrait& item);

    void serialise(const HIRTraitValueItem& tvi);

    void serialise(const HIRAssociatedType& at);
};

//}

void HIRSerialise(const ::std::string& filename, const HIRCrate& crate) {
    HIRSerialiseWriter out;
    HirSerialiser s{out, crate.types};
    s.serialiseCrate(crate);
    s.clear();
    out.open(filename);
    s.serialiseCrate(crate);
}

HirDeserialiser::HirDeserialiser(u32& id, ObjPool& pool, HIRSerialiseReader& in, HIRTypeInterner& typeInterner)
    : in(in)
    , typeInterner(typeInterner)
    , id(id)
    , pool(pool)
{
}

auto HirDeserialiser::readIstring() -> RcString {
    return in.readIstring();
}

auto HirDeserialiser::readString() -> ::std::string {
    return in.readString();
}

auto HirDeserialiser::readBool() -> bool {
    return in.readBool();
}

auto HirDeserialiser::readU8() -> u8 {
    return in.readU8();
}

auto HirDeserialiser::deserialiseCount() -> size_t {
    return in.readCount();
}

template <typename V>
auto HirDeserialiser::deserialiseStrmap() -> ::std::map<::std::string, V> {
    size_t n = in.readCount();
    ::std::map<::std::string, V> rv;
    for (size_t i = 0; i < n; i++) {
        auto s = in.readString();
        rv.insert(::std::make_pair(mv$(s), D<V>::des(*this)));
    }
    return rv;
}

template <typename V>
auto HirDeserialiser::deserialiseStrumap() -> ::std::unordered_map<::std::string, V> {
    size_t n = in.readCount();
    ::std::unordered_map<::std::string, V> rv;
    for (size_t i = 0; i < n; i++) {
        auto s = in.readString();
        rv.insert(::std::make_pair(mv$(s), D<V>::des(*this)));
    }
    return rv;
}

template <typename V>
auto HirDeserialiser::deserialiseStrummap() -> ::std::unordered_multimap<::std::string, V> {
    size_t n = in.readCount();
    ::std::unordered_multimap<::std::string, V> rv;
    for (size_t i = 0; i < n; i++) {
        auto s = in.readString();
        rv.insert(::std::make_pair(mv$(s), D<V>::des(*this)));
    }
    return rv;
}

template <typename V>
auto HirDeserialiser::deserialiseIstrmap() -> ::std::map<RcString, V> {
    size_t n = in.readCount();
    ::std::map<RcString, V> rv;
    for (size_t i = 0; i < n; i++) {
        auto s = in.readIstring();
        rv.insert(::std::make_pair(mv$(s), D<V>::des(*this)));
    }
    return rv;
}

template <typename T>
auto HirDeserialiser::deserialiseIstrumapPooled() -> ::std::unordered_map<RcString, T*> {
    size_t n = in.readCount();
    ::std::unordered_map<RcString, T*> rv;
    for (size_t i = 0; i < n; i++) {
        auto s = in.readIstring();
        rv.insert(::std::make_pair(mv$(s), pool.make<T>(D<T>::des(*this))));
    }
    return rv;
}

template <typename V>
auto HirDeserialiser::deserialiseIstrumap() -> ::std::unordered_map<RcString, V> {
    size_t n = in.readCount();
    ::std::unordered_map<RcString, V> rv;
    for (size_t i = 0; i < n; i++) {
        auto s = in.readIstring();
        rv.insert(::std::make_pair(mv$(s), D<V>::des(*this)));
    }
    return rv;
}

template <typename V>
auto HirDeserialiser::deserialiseIstrummap() -> ::std::unordered_multimap<RcString, V> {
    size_t n = in.readCount();
    ::std::unordered_multimap<RcString, V> rv;
    for (size_t i = 0; i < n; i++) {
        auto s = in.readIstring();
        rv.insert(::std::make_pair(mv$(s), D<V>::des(*this)));
    }
    return rv;
}

template <typename V>
auto HirDeserialiser::deserialisePathmap() -> ::std::map<HIRSimplePath, V> {
    size_t n = in.readCount();
    ::std::map<HIRSimplePath, V> rv;
    for (size_t i = 0; i < n; i++) {
        auto s = deserialiseSimplepath();
        rv.insert(::std::make_pair(mv$(s), D<V>::des(*this)));
    }
    return rv;
}

template <typename T, typename F>
auto HirDeserialiser::deserialiseVecC(F cb) -> ::std::vector<T> {
    auto _ = in.openObject(typeid(::std::vector<T>).name());
    size_t n = in.readCount();
    ::std::vector<T> rv;
    rv.reserve(n);
    for (size_t i = 0; i < n; i++) {
        rv.push_back(cb());
    }
    return rv;
}

template <typename T>
auto HirDeserialiser::deserialiseVec() -> ::std::vector<T> {
    return deserialiseVecC<T>([&]() {
        return D<T>::des(*this);
    });
}

template <typename T, typename F>
auto HirDeserialiser::deserialiseThinvecC(F cb) -> ThinVector<T> {
    auto _ = in.openObject(typeid(ThinVector<T>).name());
    size_t n = in.readCount();
    ThinVector<T> rv;
    rv.reserveInit(n);
    for (size_t i = 0; i < n; i++) {
        rv.push_back(cb());
    }
    return rv;
}

template <typename T>
auto HirDeserialiser::deserialiseThinvec() -> ThinVector<T> {
    return deserialiseThinvecC<T>([&]() {
        return D<T>::des(*this);
    });
}

template <typename T>
auto HirDeserialiser::deserialiseSet() -> ::std::set<T> {
    auto _ = in.openObject(typeid(::std::set<T>).name());
    size_t n = in.readCount();
    ::std::set<T> rv;
    for (size_t i = 0; i < n; i++) {
        rv.insert(D<T>::des(*this));
    }
    return rv;
}

auto HirDeserialiser::deserialisePub() -> HIRPublicity {
    return (in.readBool() ? HIRPublicity::newGlobal() : privateVisibility);
}

template <typename T>
auto HirDeserialiser::deserialiseVisent() -> HIRVisEnt<T> {
    return HIRVisEnt<T>{deserialisePub(), D<T>::des(*this)};
}

template <typename T>
auto HirDeserialiser::deserialisePtr() -> ::std::unique_ptr<T> {
    return box$(D<T>::des(*this));
}

auto HirDeserialiser::deserialiseProcmacro() -> HIRProcMacro {
    HIRProcMacro pm;
    switch (in.readTag()) {
        case 0:
            pm.ty = HIRProcMacro::Ty::Function;
            break;
        case 1:
            pm.ty = HIRProcMacro::Ty::Derive;
            break;
        case 2:
            pm.ty = HIRProcMacro::Ty::Attribute;
            break;
    }
    pm.name = in.readIstring();
    pm.path = deserialiseSimplepath();
    pm.attributes = deserialiseVec<::std::string>();
    return pm;
}

auto HirDeserialiser::deserialiseTypeimpl() -> HIRTypeImpl {
    HIRTypeImpl rv;

    rv.params = deserialiseGenericparams();
    rv.type = deserialiseType();

    size_t methodCount = in.readCount();
    for (size_t i = 0; i < methodCount; i++) {
        auto name = in.readIstring();
        rv.methods.insert(::std::make_pair(mv$(name), HIRTypeImpl::VisImplEnt<HIRFunction>{deserialisePub(), in.readBool(), deserialiseFunction()}));
    }
    size_t constCount = in.readCount();
    for (size_t i = 0; i < constCount; i++) {
        auto name = in.readIstring();
        rv.constants.insert(::std::make_pair(mv$(name), HIRTypeImpl::VisImplEnt<HIRConstant>{deserialisePub(), in.readBool(), deserialiseConstant()}));
    }
    size_t typeCount = in.readCount();
    for (size_t i = 0; i < typeCount; i++) {
        auto name = in.readIstring();
        rv.types.insert(::std::make_pair(mv$(name), HIRTypeImpl::VisImplEnt<HIRTypeAlias>{deserialisePub(), in.readBool(), deserialiseTypealias()}));
    }
    // m_src_module doesn't matter after typeck
    return rv;
}

auto HirDeserialiser::deserialiseTraitimpl() -> HIRTraitImpl {
    HIRTraitImpl rv;

    rv.params = deserialiseGenericparams();
    rv.traitArgs = deserialisePathparams();
    rv.type = deserialiseType();
    rv.isConst = in.readBool();
    rv.isReservation = in.readBool();

    size_t methodCount = in.readCount();
    for (size_t i = 0; i < methodCount; i++) {
        auto name = in.readIstring();
        auto isSpec = in.readBool();
        rv.methods.insert(::std::make_pair(mv$(name), HIRTraitImpl::ImplEnt<HIRFunction>{isSpec, deserialiseFunction()}));
    }
    size_t constCount = in.readCount();
    for (size_t i = 0; i < constCount; i++) {
        auto name = in.readIstring();
        auto isSpec = in.readBool();
        rv.constants.insert(::std::make_pair(mv$(name), HIRTraitImpl::ImplEnt<HIRConstant>{isSpec, deserialiseConstant()}));
    }
    size_t staticCount = in.readCount();
    for (size_t i = 0; i < staticCount; i++) {
        auto name = in.readIstring();
        auto isSpec = in.readBool();
        rv.statics.insert(::std::make_pair(mv$(name), HIRTraitImpl::ImplEnt<HIRStatic>{isSpec, deserialiseStatic()}));
    }
    size_t typeCount = in.readCount();
    for (size_t i = 0; i < typeCount; i++) {
        auto name = in.readIstring();
        auto isSpec = in.readBool();
        rv.types.insert(::std::make_pair(mv$(name), HIRTraitImpl::ImplEnt<HIRTypeRef>{isSpec, deserialiseType()}));
    }

    // m_src_module doesn't matter after typeck
    return rv;
}

auto HirDeserialiser::deserialiseMarkerimpl() -> HIRMarkerImpl {
    auto generics = deserialiseGenericparams();
    auto params = deserialisePathparams();
    auto isNeg = in.readBool();
    auto ty = deserialiseType();
    return HIRMarkerImpl{mv$(generics), mv$(params), isNeg, mv$(ty)};
}

auto HirDeserialiser::deserialiseHygine() -> Ident::Hygiene {
    auto _ = in.openObject(typeid(Ident::Hygiene).name());
    Ident::Hygiene rv;
    bool hasModPath = in.readBool();
    if (hasModPath) {
        Ident::ModPath mp;
        mp.crate = in.readIstring();
        mp.ents = deserialiseVec<RcString>();

        if (mp.crate == "") {
            assert(crateName != "");
            mp.crate = crateName;
        }
        rv.setModPath(pool, mv$(mp));
    }
    return rv;
}

auto HirDeserialiser::deserialiseMacrorulesptr() -> ::MacroRulesPtr {
    return ::MacroRulesPtr(new MacroRules(deserialiseMacrorules()));
}

auto HirDeserialiser::deserialiseMacrorules() -> ::MacroRules {
    auto crateName = in.readIstring();
    auto edition = static_cast<ASTEdition>(in.readTag());
    ::MacroRules rv(id, crateName, edition);
    // NOTE: This is set after loading.
    rv.isMacroItem = in.readBool();
    rv.transparent = in.readBool();
    rv.rules = deserialiseVecC<::MacroRulesArm>([&]() {
        return deserialiseMacrorulesarm();
    });
    rv.hygiene = deserialiseHygine();
    return rv;
}

auto HirDeserialiser::deserialiseSimplepatifcheck() -> ::SimplePatIfCheck {
    return ::SimplePatIfCheck{static_cast<::MacroPatEnt::Type>(in.readTag()), deserialiseToken()};
}

auto HirDeserialiser::deserialiseSimplepatent() -> ::SimplePatEnt {
    auto tag = static_cast<::SimplePatEnt::Tag>(in.readTag());
    switch (tag) {
        case ::SimplePatEnt::TAG_End:
            return ::SimplePatEnt::make_End({});
        case ::SimplePatEnt::TAG_LoopStart:
            return ::SimplePatEnt::make_LoopStart({static_cast<unsigned>(in.readCount())});
        case ::SimplePatEnt::TAG_LoopNext:
            return ::SimplePatEnt::make_LoopNext({});
        case ::SimplePatEnt::TAG_LoopEnd:
            return ::SimplePatEnt::make_LoopEnd({});
        case ::SimplePatEnt::TAG_Jump:
            return ::SimplePatEnt::make_Jump({in.readCount()});
        case ::SimplePatEnt::TAG_ExpectTok:
            return SimplePatEnt::make_ExpectTok({deserialiseToken()});
        case ::SimplePatEnt::TAG_ExpectPat:
            return SimplePatEnt::make_ExpectPat({static_cast<::MacroPatEnt::Type>(in.readTag()), static_cast<unsigned>(in.readCount())});
        case SimplePatEnt::TAG_If:
            return SimplePatEnt::make_If({in.readBool(), in.readCount(), deserialiseVecC<SimplePatIfCheck>([&]() {
                return deserialiseSimplepatifcheck();
            })});
        default:
            BUG(Span(), "Bad tag for MacroPatEnt - #" << static_cast<int>(tag));
    }
}

auto HirDeserialiser::deserialiseMacropatent() -> ::MacroPatEnt {
    auto s = in.readIstring();
    auto n = static_cast<unsigned int>(in.readCount());
    auto type = static_cast<::MacroPatEnt::Type>(in.readTag());
    ::MacroPatEnt rv(Span(), mv$(s), mv$(n), mv$(type));
    switch (rv.type) {
        case ::MacroPatEnt::PAT_TOKEN:
            rv.tok = deserialiseToken();
            break;
        case ::MacroPatEnt::PAT_LOOP:
            rv.tok = deserialiseToken();
            rv.subpats = deserialiseVecC<::MacroPatEnt>([&]() {
                return deserialiseMacropatent();
            });
            break;
        case ::MacroPatEnt::PAT_TT:  // :tt
        case ::MacroPatEnt::PAT_PAT: // :pat
        case ::MacroPatEnt::PAT_PAT_PARAM:
        case ::MacroPatEnt::PAT_IDENT:
        case ::MacroPatEnt::PAT_PATH:
        case ::MacroPatEnt::PAT_TYPE:
        case ::MacroPatEnt::PAT_EXPR:
        case ::MacroPatEnt::PAT_STMT:
        case ::MacroPatEnt::PAT_BLOCK:
        case ::MacroPatEnt::PAT_META:
        case ::MacroPatEnt::PAT_ITEM:
        case ::MacroPatEnt::PAT_VIS:
            break;
        default:
            BUG(Span(), "Bad tag for MacroPatEnt - #" << static_cast<int>(rv.type) << " " << rv.type);
    }
    return rv;
}

auto HirDeserialiser::deserialiseMacrorulesarm() -> ::MacroRulesArm {
    ::MacroRulesArm rv;
    rv.paramNames = deserialiseVec<RcString>();
    rv.pattern = deserialiseVecC<::SimplePatEnt>([&]() {
        return deserialiseSimplepatent();
    });
    rv.contents = deserialiseVecC<::MacroExpansionEnt>([&]() {
        return deserialiseMacroexpansionent();
    });
    return rv;
}

auto HirDeserialiser::deserialiseMacroexpansionent() -> ::MacroExpansionEnt {
    switch (auto tag = in.readTag()) {
        case 0:
            return ::MacroExpansionEnt(deserialiseToken());
        case 1: {
            unsigned int v = static_cast<unsigned int>(in.readU8()) << 24;
            return ::MacroExpansionEnt(v | in.readCount());
        }
        case 2: {
            auto entries = deserialiseVecC<::MacroExpansionEnt>([&]() {
                return deserialiseMacroexpansionent();
            });
            auto joiner = deserialiseToken();
            auto controllers = deserialiseSet<unsigned int>();

            return ::MacroExpansionEnt::make_Loop({mv$(entries), mv$(joiner), mv$(controllers)});
        }
        case 3: {
            auto entries = deserialiseVecC<::MacroExpansionConcatEnt>([&]() {
                return deserialiseMacroexpansionconcatent();
            });
            return ::MacroExpansionEnt(std::move(entries));
        }
        default:
            BUG(Span(), "Bad tag for MacroExpansionEnt - " << tag);
    }
}

auto HirDeserialiser::deserialiseMacroexpansionconcatent() -> ::MacroExpansionConcatEnt {
    switch (auto tag = in.readTag()) {
        case ::MacroExpansionConcatEnt::TAG_Ident: {
            auto h = deserialiseHygine();
            auto n = in.readIstring();
            return ::MacroExpansionConcatEnt::make_Ident({h, n});
        }
        case ::MacroExpansionConcatEnt::TAG_Named:
            return ::MacroExpansionConcatEnt::make_Named(in.readCount());
        default:
            BUG(Span(), "Bad tag for MacroExpansionConcatEnt - " << tag);
    }
}

auto HirDeserialiser::deserialiseToken() -> ::Token {
    auto ty = static_cast<enum eTokenType>(in.readTag());
    auto d = deserialiseTokendata();
    return ::Token(ty, ::std::move(d), {});
}

auto HirDeserialiser::deserialiseTokendata() -> ::Token::Data {
    auto tag = static_cast<::Token::Data::Tag>(in.readTag());
    switch (tag) {
        case ::Token::Data::TAG_None:
            return ::Token::Data::make_None({});
        case ::Token::Data::TAG_String:
            return ::Token::Data::make_String(in.readString());
        case ::Token::Data::TAG_Ident: {
            auto hygine = deserialiseHygine();
            auto name = in.readIstring();
            return ::Token::Data::make_Ident(Ident(std::move(hygine), std::move(name)));
        }
        case ::Token::Data::TAG_Integer: {
            auto dty = static_cast<eCoreType>(in.readTag());
            return ::Token::Data::make_Integer({dty, in.readU128()});
        }
        case ::Token::Data::TAG_Float: {
            auto dty = static_cast<eCoreType>(in.readTag());
            return ::Token::Data::make_Float({dty, in.readFloatValue()});
        }
        default:
            BUG(Span(), "Bad tag for Token::Data - " << static_cast<int>(tag));
    }
}

auto HirDeserialiser::deserialiseExprptr() -> HIRExprPtr {
    HIRExprPtr rv;
    auto _ = in.openObject("HIR::ExprPtr");
    if (in.readBool()) {
        rv.mir = deserialiseMir();
    }
    rv.erasedTypes = deserialiseVec<HIRTypeRef>();
    return rv;
}

auto HirDeserialiser::deserialiseMirParam() -> MIRParam {
    switch (auto tag = in.readTag()) {
        case MIRParam::TAG_LValue:
            return deserialiseMirLvalue();
        case MIRParam::TAG_Borrow:
            return MIRParam::make_Borrow({static_cast<HIRBorrowType>(in.readTag()), deserialiseMirLvalue()});
        case MIRParam::TAG_Constant:
            return deserialiseMirConstant();
        default:
            BUG(Span(), "Bad tag for MIR::Param - " << tag);
    }
}

auto HirDeserialiser::deserialiseMirLvalue() -> MIRLValue {
    MIRLValue rv;
    rv = deserialise_mir_lvalue_();
    return rv;
}

auto HirDeserialiser::deserialiseMirLvalueWrapper() -> MIRLValue::Wrapper {
    return MIRLValue::Wrapper::fromInner(in.readCount());
}

auto HirDeserialiser::deserialise_mir_lvalue_() -> MIRLValue {
    auto rootV = in.readCount();
    auto root = (rootV == 3 ? MIRLValue::Storage::newStatic(deserialisePath()) : MIRLValue::Storage::fromInner(rootV));
    return MIRLValue(mv$(root), deserialiseVec<MIRLValue::Wrapper>());
}

auto HirDeserialiser::deserialiseMirRvalue() -> MIRRValue {
    switch (auto tag = in.readTag()) {
#define _(x, ...)            \
    case MIRRValue::TAG_##x: \
        return MIRRValue::make_##x(__VA_ARGS__);
        _(Use, deserialiseMirLvalue())
        _(Constant, deserialiseMirConstant())
        _(SizedArray, {deserialiseMirParam(), deserialiseArraysize()})
        _(Borrow, {static_cast<HIRBorrowType>(in.readTag()), in.readBool(), deserialiseMirLvalue()})
        _(Cast, {deserialiseMirLvalue(), deserialiseType()})
        _(BinOp, {deserialiseMirParam(), static_cast<MIRBinOp>(in.readTag()), deserialiseMirParam()})
        _(UniOp, {deserialiseMirLvalue(), static_cast<MIRUniOp>(in.readTag())})
        _(DstMeta, {deserialiseMirLvalue()})
        _(DstPtr, {deserialiseMirLvalue()})
        _(MakeDst, {deserialiseMirParam(), in.readBool() ? deserialiseMirParam() : MIRConstant::make_ItemAddr({})})
        _(Tuple, {deserialiseVec<MIRParam>()})
        _(Array, {deserialiseVec<MIRParam>()})
        _(UnionVariant, {deserialiseGenericpath(), static_cast<unsigned int>(in.readCount()), deserialiseMirParam()})
        _(EnumVariant, {deserialiseGenericpath(), static_cast<unsigned int>(in.readCount()), deserialiseVec<MIRParam>()})
        _(Struct, {deserialiseGenericpath(), deserialiseVec<MIRParam>()})
#undef _
        default:
            BUG(Span(), "Bad tag for MIR::RValue - " << tag);
    }
}

auto HirDeserialiser::deserialiseMirConstant() -> MIRConstant {
    switch (auto tag = in.readTag()) {
#define _(x, ...)              \
    case MIRConstant::TAG_##x: \
        return MIRConstant::make_##x(__VA_ARGS__);
        _(Int, {in.readI128(), static_cast<HIRCoreType>(in.readTag())})
        _(Uint, {in.readU128(), static_cast<HIRCoreType>(in.readTag())})
        _(Float, {in.readFloatValue(), static_cast<HIRCoreType>(in.readTag())})
        _(Bool, {in.readBool()})
        case MIRConstant::TAG_Bytes: {
            ::std::vector<u8> bytes;
            bytes.resize(in.readCount());
            in.read(bytes.data(), bytes.size());
            return MIRConstant::make_Bytes(mv$(bytes));
        }
            _(StaticString, in.readString())
            _(Encoded, {deserialiseType(), deserialiseEncodedliteral()})
            _(Const, {box$(deserialisePath())})
            _(Generic, deserialiseGenericref())
            _(Function, {box$(deserialisePath())})
            _(ItemAddr, {box$(deserialisePath()), in.readU128()})
#undef _
        default:
            BUG(Span(), "Bad tag for MIR::Const - " << tag);
    }
}

auto HirDeserialiser::deserialiseExterntype() -> HIRExternType {
    return HIRExternType{deserialiseMarkings()};
}

auto HirDeserialiser::deserialiseTraitalias() -> HIRTraitAlias {
    return HIRTraitAlias{deserialiseGenericparams(), deserialiseVec<HIRTraitPath>()};
}

auto HirDeserialiser::deserialiseTypeitem() -> HIRTypeItem {
    switch (auto tag = in.readTag()) {
        case 0: {
            auto spath = deserialiseSimplepath();
            auto isVariant = in.readBool();
            return HIRTypeItem::make_Import({mv$(spath), isVariant, static_cast<unsigned int>(in.readCount())});
        }
        case 1:
            return HIRTypeItem(deserialiseModule());
        case 2:
            return HIRTypeItem(deserialiseTypealias());
        case 3:
            return HIRTypeItem(deserialiseEnum());
        case 4:
            return HIRTypeItem(deserialiseStruct());
        case 5:
            return HIRTypeItem(deserialiseTrait());
        case 6:
            return HIRTypeItem(deserialiseUnion());
        case 7:
            return HIRTypeItem(deserialiseExterntype());
        case 8:
            return HIRTypeItem(deserialiseTraitalias());
        default:
            BUG(Span(), "Bad tag for HIR::TypeItem - " << tag);
    }
}

auto HirDeserialiser::deserialiseValueitem() -> HIRValueItem {
    switch (auto tag = in.readTag()) {
        case 0: {
            auto spath = deserialiseSimplepath();
            auto isVariant = in.readBool();
            return HIRValueItem::make_Import({mv$(spath), isVariant, static_cast<unsigned int>(in.readCount())});
        }
        case 1:
            return HIRValueItem(pool.make<HIRConstant>(deserialiseConstant()));
        case 2:
            return HIRValueItem(pool.make<HIRStatic>(deserialiseStatic()));
        case 3:
            return HIRValueItem::make_StructConstant({deserialiseSimplepath()});
        case 4:
            return HIRValueItem(pool.make<HIRFunction>(deserialiseFunction()));
        case 5:
            return HIRValueItem::make_StructConstructor({deserialiseSimplepath()});
        default:
            BUG(Span(), "Bad tag for HIR::ValueItem - " << tag);
    }
}

auto HirDeserialiser::deserialiseMacroitem() -> HIRMacroItem {
    auto _ = in.openObject("HIR::MacroItem");
    auto tag = in.readTag();
    switch (tag) {
        case HIRMacroItem::TAG_Import:
            return HIRMacroItem::Data_Import{deserialiseSimplepath()};
        case HIRMacroItem::TAG_MacroRules:
            return deserialiseMacrorulesptr();
        case HIRMacroItem::TAG_ProcMacro:
            return deserialiseProcmacro();
    }

    TODO(Span(), "Bad tag for MacroItem - " << tag);
}

auto HirDeserialiser::deserialiseLinkage() -> HIRLinkage {
    HIRLinkage l;
    l.type = HIRLinkage::Type::Auto;
    l.name = in.readString();
    return l;
}

auto HirDeserialiser::deserialiseFunction() -> HIRFunction {
    auto _ = in.openObject("HIR::Function");

    HIRFunction rv;
    rv.saveCode = false;
    rv.linkage = deserialiseLinkage();
    rv.receiver = static_cast<HIRFunction::Receiver>(in.readTag());
    auto receiverType = deserialiseType();
    if (rv.receiver == HIRFunction::Receiver::Custom) {
        rv.receiverType = receiverType;
    }
    rv.abi = in.readIstring();
    rv.unsafe = in.readBool();
    rv.isConst = in.readBool();
    rv.markings = deserialiseFunctionMarkings();
    rv.params = deserialiseGenericparams();
    rv.args = deserialiseFcnargs();
    rv.variadic = in.readBool();
    rv.hasNamedVariadic = in.readBool();
    rv.returnType = deserialiseType();
    rv.source.filename = in.readIstring();
    rv.source.line = static_cast<unsigned int>(in.readCount());
    rv.source.column = static_cast<unsigned int>(in.readCount());
    rv.code = deserialiseExprptr();
    return rv;
}

auto HirDeserialiser::deserialiseFunctionMarkings() -> HIRFunction::Markings {
    auto _ = in.openObject("HIR::Function::Markings");
    HIRFunction::Markings rv;
    rv.rustcLegacyConstGenerics = deserialiseVec<unsigned>();
    rv.trackCaller = in.readBool();
    rv.isRustcIntrinsic = in.readBool();
    rv.isRustcPromotable = in.readBool();
    rv.mustUse = in.readBool();
    rv.alignment = in.readCount();
    rv.inlineType = static_cast<HIRFunction::Markings::Inline>(in.readTag());
    return rv;
}

auto HirDeserialiser::deserialiseFcnargs() -> ::std::vector<::std::pair<HIRPattern, HIRTypeRef>> {
    size_t n = in.readCount();
    ::std::vector<::std::pair<HIRPattern, HIRTypeRef>> rv;
    rv.reserve(n);
    for (size_t i = 0; i < n; i++) {
        rv.push_back(::std::make_pair(HIRPattern{}, deserialiseType()));
    }
    return rv;
}

auto HirDeserialiser::deserialiseConstant() -> HIRConstant {
    HIRConstant rv;
    rv.params = deserialiseGenericparams();
    rv.type = deserialiseType();
    rv.value = deserialiseExprptr();
    if (in.readBool()) {
        rv.valueRes = deserialiseEncodedliteral();
        rv.valueState = HIRConstant::ValueState::Known;
    } else {
        rv.valueState = HIRConstant::ValueState::Generic;
    }
    return rv;
}

auto HirDeserialiser::deserialiseStatic() -> HIRStatic {
    auto linkage = deserialiseLinkage();
    auto params = deserialiseGenericparams();
    u8 bitflag1 = in.readU8();
#define BIT(i, fld) fld = (bitflag1 & (1 << (i))) != 0;
    bool isMut;
    bool saveLiteral;
    bool hasExplicitAlignment;
    bool isPromoted;
    BIT(0, isMut);
    BIT(1, saveLiteral);
    BIT(2, hasExplicitAlignment);
    BIT(3, isPromoted);
#undef BIT
    auto explicitAlignment = hasExplicitAlignment ? in.readCount() : 0;
    auto ty = deserialiseType();
    auto rv = HIRStatic(mv$(linkage), isMut, mv$(ty), {});
    rv.explicitAlignment = explicitAlignment;
    rv.isPromoted = isPromoted;
    if (params.isGeneric()) {
        rv.value = deserialiseExprptr();
    }
    rv.params = ::std::move(params);
    if (saveLiteral) {
        rv.valueRes = deserialiseEncodedliteral();
        rv.valueGenerated = true;
        rv.noEmitValue = true;
    }
    return rv;
}

auto HirDeserialiser::deserialiseTypealias() -> HIRTypeAlias {
    return HIRTypeAlias{deserialiseGenericparams(), deserialiseType()};
}

auto HirDeserialiser::deserialiseMarkings() -> HIRTraitMarkings {
    HIRTraitMarkings m;
    u8 bitflag1 = in.readU8();
#define BIT(i, fld) fld = (bitflag1 & (1 << (i))) != 0;
    BIT(0, m.hasADeref)
    BIT(1, m.isCopy)
    BIT(2, m.hasDropImpl)
    BIT(3, m.hasConstDropImpl)
#undef BIT
    // TODO: auto_impls
    return m;
}

auto HirDeserialiser::deserialiseStrMarkings() -> HIRStructMarkings {
    HIRStructMarkings m;
    u8 bitflag1 = in.readU8();
#define BIT(i, fld) fld = (bitflag1 & (1 << (i))) != 0;
    BIT(0, m.canUnsize)
    BIT(1, m.isNonzero)
    BIT(2, m.boundedMax)
    BIT(3, m.isFundamental)
    BIT(4, m.isNoNiche)
    BIT(5, m.isAsyncDropGlue)
#undef BIT
    m.dstType = static_cast<HIRStructMarkings::DstType>(in.readTag());
    m.coerceUnsized = static_cast<HIRStructMarkings::Coerce>(in.readTag());
    m.coerceUnsizedIndex = in.readCount();
    m.coerceParam = in.readCount();
    m.unsizedField = in.readCount();
    m.unsizedParam = in.readCount();
    if (m.boundedMax) {
        m.boundedMaxValue = in.readU128();
    }
    // TODO: auto_impls
    return m;
}

auto HirDeserialiser::deserialiseTraitvalueitem() -> HIRTraitValueItem {
    switch (auto tag = in.readTag()) {
#define _(x, ...)                                        \
    case HIRTraitValueItem::TAG_##x:                     \
        return HIRTraitValueItem::make_##x(__VA_ARGS__); \
        break;
        _(Constant, deserialiseConstant())
        _(Static, deserialiseStatic())
        _(Function, deserialiseFunction())
#undef _
        default:
            BUG(Span(), "Bad tag for HIR::TraitValueItem - " << tag);
    }
}

auto HirDeserialiser::deserialiseAssociatedtype() -> HIRAssociatedType {
    return HIRAssociatedType{deserialiseGenericparams(), in.readBool(), deserialiseVec<HIRTraitPath>(), deserialiseType()};
}

template <typename T, typename U>
auto D<::std::pair<T, U>>::des(HirDeserialiser& d) -> ::std::pair<T, U> {
    auto a = D<T>::des(d);
    return ::std::make_pair(mv$(a), D<U>::des(d));
}

TreeVisitor::TreeVisitor(HIRTypeInterner& types, ::std::ostream& os)
    : HIRVisitor(nullptr, types)
    , os(os)
    , indentLevel(0)
{
}

auto TreeVisitor::visitModule(HIRItemPath p, HIRModule& mod) -> void {
    if (p.getName()[0]) {
        os << indent() << "mod " << p.getName() << " {\n";
        incIndent();
    }

    // TODO: Include trait list
    if (true) {
        for (const auto& t : mod.traits) {
            os << indent() << "use " << t << ";\n";
        }
    }
    // TODO: Print publicitiy.
    HIRVisitor::visitModule(p, mod);

    if (p.getName()[0]) {
        decIndent();
        os << indent() << "}\n";
    }
}

auto TreeVisitor::visitTypeImpl(HIRTypeImpl& impl) -> void {
    os << indent() << "impl" << impl.params.fmtArgs() << " " << impl.type << "\n";
    if (!impl.params.bounds.empty()) {
        os << indent() << " " << impl.params.fmtBounds() << "\n";
    }
    os << indent() << "{\n";
    incIndent();
    HIRVisitor::visitTypeImpl(impl);
    decIndent();
    os << indent() << "}\n";
}

auto TreeVisitor::visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) -> void {
    os << indent() << "impl" << impl.params.fmtArgs() << " " << traitPath << impl.traitArgs << " for " << impl.type << "\n";
    if (!impl.params.bounds.empty()) {
        os << indent() << " " << impl.params.fmtBounds() << "\n";
    }
    os << indent() << "{\n";
    incIndent();
    for (auto& ent : impl.types) {
        os << indent() << "type " << ent.first << " = " << ent.second.data << "\n";
    }
    HIRVisitor::visitTraitImpl(traitPath, impl);
    decIndent();
    os << indent() << "}\n";
}

auto TreeVisitor::visitMarkerImpl(const HIRSimplePath& traitPath, HIRMarkerImpl& impl) -> void {
    os << indent() << "impl" << impl.params.fmtArgs() << " " << (impl.isPositive ? "" : "!") << traitPath << impl.traitArgs << " for " << impl.type << "\n";
    if (!impl.params.bounds.empty()) {
        os << indent() << " " << impl.params.fmtBounds() << "\n";
    }
    os << indent() << "{ }\n";
}

auto TreeVisitor::visitTypeAlias(HIRItemPath p, HIRTypeAlias& item) -> void {
    os << indent() << "type " << p.getName() << item.params.fmtArgs() << " = " << item.type << item.params.fmtBounds() << "\n";
}

auto TreeVisitor::visitInherentType(HIRItemPath p, HIRTypeAlias& item) -> void {
    this->visitTypeAlias(p, item);
}

auto TreeVisitor::visitTrait(HIRItemPath p, HIRTrait& item) -> void {
    os << indent() << "trait " << p.getName() << item.params.fmtArgs() << "\n";
    if (!item.parentTraits.empty()) {
        os << indent() << "  " << ": ";
        bool isFirst = true;
        for (auto& bound : item.parentTraits) {
            if (!isFirst) {
                os << indent() << "  " << "+ ";
            }
            os << bound << "\n";
            isFirst = false;
        }
    }
    if (!item.params.bounds.empty()) {
        os << indent() << " " << item.params.fmtBounds() << "\n";
    }
    if (!item.allParentTraits.empty()) {
        os << indent() << "/* All parent traits:\n";
        for (const auto& t : item.allParentTraits) {
            os << indent() << t << "\n";
        }
        os << indent() << "*/\n";
    }
    os << indent() << "{\n";
    incIndent();

    for (auto& i : item.types) {
        os << indent() << "type " << i.first;
        if (!i.second.traitBounds.empty()) {
            os << ": ";
            bool isFirst = true;
            for (auto& bound : i.second.traitBounds) {
                if (!isFirst) {
                    os << " + ";
                }
                os << bound;
                isFirst = false;
            }
        }
        os << ";\n";
    }

    HIRVisitor::visitTrait(p, item);

    decIndent();
    os << indent() << "}\n";
}

auto TreeVisitor::visitStruct(HIRItemPath p, HIRStruct& item) -> void {
    os << indent() << "struct " << p.getName() << item.params.fmtArgs();
    switch (item.data.tag()) {
        case HIRStructData::TAG_Unit: {
            if (item.params.bounds.empty()) {
                os << ";\n";
            } else {
                os << "\n";
                os << indent() << " " << item.params.fmtBounds() << "\n";
                os << indent() << "    ;\n";
            }
            break;
        }
        case HIRStructData::TAG_Tuple: {
            auto& flds = item.data.as_Tuple();
            os << "(";
            for (const auto& fld : flds) {
                os << fld.publicity << " " << fld.ent << ", ";
            }
            if (item.params.bounds.empty()) {
                os << ");\n";
            } else {
                os << ")\n";
                os << indent() << " " << item.params.fmtBounds() << "\n";
                os << indent() << "    ;\n";
            }
            break;
        }
        case HIRStructData::TAG_Named: {
            auto& flds = item.data.as_Named();
            os << "\n";
            if (!item.params.bounds.empty()) {
                os << indent() << " " << item.params.fmtBounds() << "\n";
            }
            os << indent() << "{\n";
            incIndent();
            for (const auto& fld : flds) {
                os << indent() << fld.vis << " " << fld.name << ": " << fld.ty;
                if (fld.defaultValue) {
                    os << " = " << *fld.defaultValue;
                }
                os << ",\n";
            }
            decIndent();
            os << indent() << "}\n";
            break;
        }
    }
}

auto TreeVisitor::visitEnum(HIRItemPath p, HIREnum& item) -> void {
    os << indent() << "enum " << p.getName() << item.params.fmtArgs() << "\n";
    if (!item.params.bounds.empty()) {
        os << indent() << " " << item.params.fmtBounds() << "\n";
    }
    os << indent() << "{\n";
    incIndent();
    if (const auto* e = item.data.opt_Value()) {
        for (const auto& var : e->variants) {
            os << indent() << var.name;
            os << ",\n";
        }
    } else {
        for (const auto& var : item.data.as_Data()) {
            os << indent() << var.name;
            if (var.type == typeInterner().unit()) {
            } else {
                os << " " << var.type << (var.isStruct ? "/*struct*/" : "");
            }
            os << ",\n";
        }
    }
    decIndent();
    os << indent() << "}\n";
}

auto TreeVisitor::visitFunction(HIRItemPath p, HIRFunction& item) -> void {
    os << indent();
    if (item.isConst) {
        os << "const ";
    }
    if (item.unsafe) {
        os << "unsafe ";
    }
    if (item.abi != ABI_RUST) {
        os << "extern \"" << item.abi << "\" ";
    }
    os << "fn " << p.getName() << item.params.fmtArgs() << "(";
    for (const auto& arg : item.args) {
        os << arg.first << ": " << arg.second << ", ";
    }
    os << ") -> " << item.returnType << "\n";
    if (!item.params.bounds.empty()) {
        os << indent() << " " << item.params.fmtBounds() << "\n";
    }

    if (item.code) {
        os << indent();
        if (cast<HIRExprNodeBlock>(&*item.code)) {
            item.code->visit(*this);
        } else {
            os << "{\n";
            incIndent();
            os << indent();

            item.code->visit(*this);

            os << "\n";
            decIndent();
            os << indent();
            os << "}";
        }
        os << "\n";
    } else {
        os << indent() << "  ;\n";
    }
}

auto TreeVisitor::visitStatic(HIRItemPath p, HIRStatic& item) -> void {
    if (item.linkage.name != "") {
        os << indent() << "#[link_name=\"" << item.linkage.name << "\"]\n";
    }
    if (item.value) {
        os << indent() << "static " << p.getName() << item.params.fmtArgs() << ": " << item.type << " = " << item.valueRes;
    } else if (item.valueGenerated) {
        os << indent() << "static " << p.getName() << item.params.fmtArgs() << ": " << item.type << " = /*magic*/ " << item.valueRes;
    } else {
        os << indent() << "extern static " << p.getName() << ": " << item.type;
    }
    if (!item.params.bounds.empty()) {
        os << indent() << " " << item.params.fmtBounds() << "\n";
    }
    os << ";\n";
}

auto TreeVisitor::visitConstant(HIRItemPath p, HIRConstant& item) -> void {
    os << indent() << "const " << p.getName() << ": " << item.type << " = " << item.valueRes;
    if (item.value /*&& item.m_value_state != HIR::Constant::ValueState::Known*/) {
        os << " /*= ";
        item.value->visit(*this);
        os << "*/";
    }
    os << ";\n";
}

#ifdef NODE_IS
    #undef NODE_IS
#endif
#define NODE_IS(valptr, tysuf) (cast<const HIRExprNode##tysuf>(&*valptr) != nullptr)

auto TreeVisitor::nodeIsLeaf(const HIRExprNode& node) -> bool {
    if (NODE_IS(&node, PathValue)) {
        return true;
    }
    if (NODE_IS(&node, Variable)) {
        return true;
    }
    if (NODE_IS(&node, Literal)) {
        return true;
    }
    if (NODE_IS(&node, CallPath)) {
        return true;
    }
    if (NODE_IS(&node, Deref)) {
        return true;
    }
    return false;
}

#undef NODE_IS

auto TreeVisitor::visitNodePtr(HIRExprNodeP& nodePtr) -> void {
    HIRExprVisitor::visitNodePtr(nodePtr);
    os << "/*: " << nodePtr->resType << " */";
}

auto TreeVisitor::visit(HIRExprNodeBlock& node) -> void {
    os << "{\n";
    incIndent();
    for (auto& sn : node.nodes) {
        os << indent();
        this->visitNodePtr(sn);
        os << ";\n";
    }
    if (node.valueNode) {
        os << indent();
        this->visitNodePtr(node.valueNode);
        os << "\n";
    }
    decIndent();
    os << indent() << "}";
}

auto TreeVisitor::visit(HIRExprNodeConstBlock& node) -> void {
    os << "const ";
    node.inner->visit(*this);
}

auto TreeVisitor::visit(HIRExprNodeAsm& node) -> void {
    os << "llvm_asm!(";
    os << ")";
}

auto TreeVisitor::visit(HIRExprNodeAsm2& node) -> void {
    os << "asm!(";
    os << ")";
}

auto TreeVisitor::visit(HIRExprNodeReturn& node) -> void {
    os << (node.isTailCall ? "become" : "return");
    if (node.value) {
        os << " ";
        this->visitNodePtr(node.value);
    }
}

auto TreeVisitor::visit(HIRExprNodeYield& node) -> void {
    os << "yield";
    if (node.value) {
        os << " ";
        this->visitNodePtr(node.value);
    }
}

auto TreeVisitor::visit(HIRExprNodeAWait& node) -> void {
    os << "(";
    this->visitNodePtr(node.value);
    os << ").await";
}

auto TreeVisitor::visit(HIRExprNodeUse& node) -> void {
    os << "(";
    this->visitNodePtr(node.value);
    os << ").use";
}

auto TreeVisitor::visit(HIRExprNodeLet& node) -> void {
    os << "let " << node.pattern << ": " << node.type;
    if (node.value) {
        os << " = ";
        this->visitNodePtr(node.value);
    }
    os << ";";
}

auto TreeVisitor::visit(HIRExprNodeLoop& node) -> void {
    if (node.label != "") {
        os << "'" << node.label << ": ";
    }
    os << "loop ";
    this->visitNodePtr(node.code);
}

auto TreeVisitor::visit(HIRExprNodeLoopControl& node) -> void {
    os << (node.isContinue ? "continue" : "break");
    if (node.label != "") {
        os << " '" << node.label;
    }
    if (node.value) {
        os << " ";
        this->visitNodePtr(node.value);
    }
}

auto TreeVisitor::visit(HIRExprNodeMatch& node) -> void {
    os << "match ";
    this->visitNodePtr(node.value);
    os << " {\n";
    for (/*const*/ auto& arm : node.arms) {
        os << indent();
        os << arm.patterns.front();
        for (unsigned int i = 1; i < arm.patterns.size(); i++) {
            os << " | " << arm.patterns[i];
        }

        if (arm.guards.size() > 0) {
            os << " if ";
            for (auto& c : arm.guards) {
                if (&c != &arm.guards.front()) {
                    os << " && ";
                }
                os << "let " << c.pat << " = ";
                this->visitNodePtr(c.val);
            }
        }
        os << " => ";
        incIndent();
        this->visitNodePtr(arm.code);
        decIndent();
        os << ",\n";
    }
    os << indent() << "}";
}

auto TreeVisitor::visit(HIRExprNodeAssign& node) -> void {
    this->visitNodePtr(node.slot);
    os << " " << HIRExprNodeAssign::opname(node.op) << "= ";
    this->visitNodePtr(node.value);
}

auto TreeVisitor::visit(HIRExprNodeBinOp& node) -> void {
    os << "(";
    this->visitNodePtr(node.left);
    os << ")";
    os << " " << HIRExprNodeBinOp::opname(node.op) << " ";
    os << "(";
    this->visitNodePtr(node.right);
    os << ")";
}

auto TreeVisitor::visit(HIRExprNodeUniOp& node) -> void {
    switch (node.op) {
        case HIRExprNodeUniOp::Op::Invert:
            os << "!";
            break;
        case HIRExprNodeUniOp::Op::Negate:
            os << "-";
            break;
    }
    os << "(";
    this->visitNodePtr(node.value);
    os << ")";
}

#ifdef NODE_IS
    #undef NODE_IS
#endif
#define NODE_IS(valptr, tysuf) (cast<const HIRExprNode##tysuf>(&*valptr) != nullptr)

auto TreeVisitor::visit(HIRExprNodeBorrow& node) -> void {
    os << "&";
    switch (node.type) {
        case HIRBorrowType::Shared:
            break;
        case HIRBorrowType::Unique:
            os << "mut ";
            break;
        case HIRBorrowType::Owned:
            os << "move ";
            break;
    }

    bool skipParens = this->nodeIsLeaf(*node.value) || NODE_IS(node.value, Deref);
    if (!skipParens) {
        os << "(";
    }
    this->visitNodePtr(node.value);
    if (!skipParens) {
        os << ")";
    }
}

#undef NODE_IS

#ifdef NODE_IS
    #undef NODE_IS
#endif
#define NODE_IS(valptr, tysuf) (cast<const HIRExprNode##tysuf>(&*valptr) != nullptr)

auto TreeVisitor::visit(HIRExprNodeRawBorrow& node) -> void {
    os << "&raw ";
    switch (node.type) {
        case HIRBorrowType::Shared:
            break;
        case HIRBorrowType::Unique:
            os << "mut ";
            break;
        case HIRBorrowType::Owned:
            os << "move ";
            break;
    }

    bool skipParens = this->nodeIsLeaf(*node.value) || NODE_IS(node.value, Deref);
    if (!skipParens) {
        os << "(";
    }
    this->visitNodePtr(node.value);
    if (!skipParens) {
        os << ")";
    }
}

#undef NODE_IS

auto TreeVisitor::visit(HIRExprNodeCast& node) -> void {
    this->visitNodePtr(node.value);
    os << " as " << node.dstType;
}

auto TreeVisitor::visit(HIRExprNodeUnsize& node) -> void {
    this->visitNodePtr(node.value);
    os << " : " << node.dstType;
}

auto TreeVisitor::visit(HIRExprNodeIndex& node) -> void {
    // TODO: Avoid parens
    os << "(";
    this->visitNodePtr(node.value);
    os << ")";
    os << "[";
    this->visitNodePtr(node.index);
    os << "]";
}

auto TreeVisitor::visit(HIRExprNodeDeref& node) -> void {
    os << "*";

    bool skipParens = this->nodeIsLeaf(*node.value);
    if (!skipParens) {
        os << "(";
    }
    this->visitNodePtr(node.value);
    if (!skipParens) {
        os << ")";
    }
}

auto TreeVisitor::visit(HIRExprNodeEmplace& node) -> void {
    if (node.type == HIRExprNodeEmplace::Type::Noop) {
        return node.value->visit(*this);
    }
    os << "(";
    this->visitNodePtr(node.place);
    os << " <- ";
    this->visitNodePtr(node.value);
    os << ")";
    os << "/*" << (node.type == HIRExprNodeEmplace::Type::Boxer ? "box" : "place") << "*/";
}

auto TreeVisitor::visit(HIRExprNodeTupleVariant& node) -> void {
    os << node.path;
    os << "(";
    for (/*const*/ auto& arg : node.args) {
        this->visitNodePtr(arg);
        os << ", ";
    }
    os << ")";
}

auto TreeVisitor::visit(HIRExprNodeCallPath& node) -> void {
    os << node.path;
    os << "(";
    for (/*const*/ auto& arg : node.args) {
        this->visitNodePtr(arg);
        os << ", ";
    }
    os << ")";
    os << "/* : " << node.resType << " */";
}

auto TreeVisitor::visit(HIRExprNodeCallValue& node) -> void {
    // TODO: Avoid brackets if not needed
    os << "(";
    this->visitNodePtr(node.value);
    os << ")";
    os << "(";
    for (/*const*/ auto& arg : node.args) {
        this->visitNodePtr(arg);
        os << ", ";
    }
    os << ")";
}

auto TreeVisitor::visit(HIRExprNodeCallMethod& node) -> void {
    // TODO: Avoid brackets if not needed
    os << "(";
    this->visitNodePtr(node.value);
    os << ")";
    os << "." << node.method << node.params << "(";
    for (/*const*/ auto& arg : node.args) {
        this->visitNodePtr(arg);
        os << ", ";
    }
    os << ")";
    if (!node.cache.argTypes.empty()) {
        os << "/*CACHE:" << node.cache.argTypes << "*/";
    }
}

auto TreeVisitor::visit(HIRExprNodeField& node) -> void {
    // TODO: Avoid brackets if not needed
    os << "(";
    this->visitNodePtr(node.value);
    os << ")";
    os << "." << node.field;
}

auto TreeVisitor::visit(HIRExprNodeLiteral& node) -> void {
    switch (node.data.tag()) {
        case HIRExprLiteral::TAG_Integer: {
            auto& e = node.data.as_Integer();
            switch (e.type) {
                case HIRCoreType::U8:
                    os << e.value << "_u8";
                    break;
                case HIRCoreType::U16:
                    os << e.value << "_u16";
                    break;
                case HIRCoreType::U32:
                    os << e.value << "_u32";
                    break;
                case HIRCoreType::U64:
                    os << e.value << "_u64";
                    break;
                case HIRCoreType::Usize:
                    os << e.value << "_usize";
                    break;
                case HIRCoreType::I8:
                    os << /*I128*/ (e.value) << "_i8";
                    break;
                case HIRCoreType::I16:
                    os << /*I128*/ (e.value) << "_i16";
                    break;
                case HIRCoreType::I32:
                    os << /*I128*/ (e.value) << "_i32";
                    break;
                case HIRCoreType::I64:
                    os << /*I128*/ (e.value) << "_i64";
                    break;
                case HIRCoreType::Isize:
                    os << /*I128*/ (e.value) << "_isize";
                    break;
                case HIRCoreType::Char: {
                    auto v = e.value.truncateU64();
                    if (v == '\\' || v == '\'') {
                        os << "'\\" << static_cast<char>(v) << "'";
                    } else if (' ' <= v && v <= 0x7F) {
                        os << "'" << static_cast<char>(v) << "'";
                    } else {
                        os << "'\\u{" << ::std::hex << v << ::std::dec << "}'";
                    }
                } break;
                default:
                    os << e.value << "_unk";
                    break;
            }
            break;
        }
        case HIRExprLiteral::TAG_Float: {
            auto& e = node.data.as_Float();
            switch (e.type) {
                case HIRCoreType::F32:
                    os << e.value << "_f32";
                    break;
                case HIRCoreType::F64:
                    os << e.value << "_f64";
                    break;
                default:
                    os << e.value << "_unk";
                    break;
            }
            break;
        }
        case HIRExprLiteral::TAG_Boolean: {
            auto& e = node.data.as_Boolean();
            os << (e ? "true" : "false");
            break;
        }
        case HIRExprLiteral::TAG_String: {
            auto& e = node.data.as_String();
            os << "\"" << FmtEscaped(e) << "\"";
            break;
        }
        case HIRExprLiteral::TAG_CString: {
            auto& e = node.data.as_CString();
            os << "c\"" << FmtEscaped(e.v) << "\"";
            break;
        }
        case HIRExprLiteral::TAG_ByteString: {
            auto& e = node.data.as_ByteString();
            os << "b\"";
            for (auto b : e) {
                if (b == '\\' || b == '\"') {
                    os << "\\" << b;
                } else if (' ' <= b && b <= 0x7F) {
                    os << b;
                } else {
                    char buf[3];
                    sprintf(buf, "%02x", static_cast<u8>(b));
                    os << "\\x" << buf;
                }
            }
            os << "\"";
            break;
        }
    }
}

auto TreeVisitor::visit(HIRExprNodeUnitVariant& node) -> void {
    os << node.path;
}

auto TreeVisitor::visit(HIRExprNodePathValue& node) -> void {
    os << node.path;
}

auto TreeVisitor::visit(HIRExprNodeVariable& node) -> void {
    os << node.name << "#" << node.slot;
}

auto TreeVisitor::visit(HIRExprNodeConstParam& node) -> void {
    os << node.name << "#" << node.binding;
}

auto TreeVisitor::visit(HIRExprNodeStructLiteral& node) -> void {
    os << node.type << " {\n";
    incIndent();
    for (/*const*/ auto& val : node.values) {
        os << indent() << val.first << ": ";
        this->visitNodePtr(val.second);
        os << ",\n";
    }
    if (node.baseValue) {
        os << indent() << ".. ";
        this->visitNodePtr(node.baseValue);
        os << "\n";
    }
    os << indent() << "}";
    decIndent();
}

auto TreeVisitor::visit(HIRExprNodeTuple& node) -> void {
    os << "(";
    for (/*const*/ auto& val : node.vals) {
        this->visitNodePtr(val);
        os << ", ";
    }
    os << ")";
}

auto TreeVisitor::visit(HIRExprNodeArrayList& node) -> void {
    os << "[";
    for (/*const*/ auto& val : node.vals) {
        this->visitNodePtr(val);
        os << ", ";
    }
    os << "]";
}

auto TreeVisitor::visit(HIRExprNodeArraySized& node) -> void {
    os << "[";
    this->visitNodePtr(node.val);
    os << "; " << node.size;
    os << "]";
}

auto TreeVisitor::visit(HIRExprNodeClosure& node) -> void {
    if (node.code) {
        if (node.isMove) {
            os << " move";
        }
        if (node.isUse) {
            os << " use";
        }
        os << "|";
        for (const auto& arg : node.args) {
            os << arg.first << ": " << arg.second << ", ";
        }
        os << "| -> " << node.returnType << " ";
        this->visitNodePtr(node.code);
    } else {
        os << node.objPath << "( ";
        for (/*const*/ auto& cap : node.captures) {
            this->visitNodePtr(cap);
            os << ", ";
        }
        os << ")";
    }
}

auto TreeVisitor::visit(HIRExprNodeGenerator& node) -> void {
    if (node.code) {
        os << "/*gen*/";
        if (node.isPinned) {
            os << "static ";
        }
        if (node.isMove) {
            os << " move";
        }
        os << "|";
        os << "| -> " << node.returnType << " ";
        this->visitNodePtr(node.code);
    } else {
        os << node.objPath << "( ";
        for (/*const*/ auto& cap : node.captures) {
            this->visitNodePtr(cap);
            os << ", ";
        }
        os << ")";
    }
}

auto TreeVisitor::visit(HIRExprNodeGeneratorWrapper& node) -> void {
    os << "/*gen body*/";
    os << "|";
    os << "| -> " << node.returnType << " ";
    this->visitNodePtr(node.code);
}

auto TreeVisitor::visit(HIRExprNodeAsyncBlock& node) -> void {
    if (node.isMove) {
        os << "move ";
    }
    os << "async {";
    if (!node.code) {
        os << "/* lowered: " << node.objPath << " */";
    } else {
        this->visitNodePtr(node.code);
    }
    os << "}";
}

auto TreeVisitor::indent() const -> RepeatLitStr {
    return RepeatLitStr{"    ", static_cast<int>(indentLevel)};
}

auto TreeVisitor::incIndent() -> void {
    indentLevel++;
}

auto TreeVisitor::decIndent() -> void {
    indentLevel--;
}

HirSerialiser::HirSerialiser(HIRSerialiseWriter& out, HIRTypeInterner& typeInterner)
    : out(out)
    , typeInterner(typeInterner)
{
}

auto HirSerialiser::clear() -> void {
    types.clear();
}

template <typename V>
auto HirSerialiser::serialiseStrmap(const ::std::map<RcString, V>& map) -> void {
    out.writeCount(map.size());
    for (const auto& v : map) {
        out.writeString(v.first);
        serialise(v.second);
    }
}

template <typename V>
auto HirSerialiser::serialiseStrmap(const ::std::map<::std::string, V>& map) -> void {
    out.writeCount(map.size());
    for (const auto& v : map) {
        out.writeString(v.first);
        serialise(v.second);
    }
}

template <typename V>
auto HirSerialiser::serialisePathmap(const ::std::map<HIRSimplePath, V>& map) -> void {
    out.writeCount(map.size());
    for (const auto& v : map) {
        serialise(v.first);
        serialise(v.second);
    }
}

template <typename V>
auto HirSerialiser::serialiseStrmap(const ::std::unordered_map<RcString, V>& map) -> void {
    out.writeCount(map.size());
    for (const auto& v : map) {
        out.writeString(v.first);
        serialise(v.second);
    }
}

template <typename V>
auto HirSerialiser::serialiseStrmap(const ::std::unordered_map<::std::string, V>& map) -> void {
    out.writeCount(map.size());
    for (const auto& v : map) {
        out.writeString(v.first);
        serialise(v.second);
    }
}

template <typename V>
auto HirSerialiser::serialiseStrmap(const ::std::unordered_multimap<RcString, V>& map) -> void {
    out.writeCount(map.size());
    for (const auto& v : map) {
        out.writeString(v.first);
        serialise(v.second);
    }
}

template <typename V>
auto HirSerialiser::serialiseStrmap(const ::std::unordered_multimap<::std::string, V>& map) -> void {
    out.writeCount(map.size());
    for (const auto& v : map) {
        out.writeString(v.first);
        serialise(v.second);
    }
}

template <typename T>
auto HirSerialiser::serialiseVec(const ThinVector<T>& vec) -> void {
    auto _ = out.openObject(typeid(ThinVector<T>).name());
    out.writeCount(vec.size());
    for (const auto& i : vec) {
        serialise(i);
    }
}

template <typename T>
auto HirSerialiser::serialiseVec(const ::std::vector<T>& vec) -> void {
    auto _ = out.openObject(typeid(::std::vector<T>).name());
    out.writeCount(vec.size());
    for (const auto& i : vec) {
        serialise(i);
    }
}

template <typename T>
auto HirSerialiser::serialise(const ::std::vector<T>& vec) -> void {
    serialiseVec(vec);
}

template <typename T>
auto HirSerialiser::serialise(const ::std::set<T>& s) -> void {
    auto _ = out.openObject(typeid(::std::set<T>).name());
    out.writeCount(s.size());
    for (const auto& i : s) {
        serialise(i);
    }
}

auto HirSerialiser::serialise(const HIRPublicity& pub) -> void {
    out.writeBool(pub.isGlobal());
}

template <typename T>
auto HirSerialiser::serialise(const HIRVisEnt<T>& e) -> void {
    serialise(e.publicity);
    serialise(e.ent);
}

template <typename T>
auto HirSerialiser::serialise(const HIRVisEnt<T>* e) -> void {
    assert(e);
    serialise(*e);
}

template <typename T>
auto HirSerialiser::serialise(const ::std::unique_ptr<T>& e) -> void {
    serialise(*e);
}

template <typename T>
auto HirSerialiser::serialise(const ::std::pair<::std::string, T>& e) -> void {
    out.writeString(e.first);
    serialise(e.second);
}

template <typename T>
auto HirSerialiser::serialise(const ::std::pair<RcString, T>& e) -> void {
    out.writeString(e.first);
    serialise(e.second);
}

template <typename T>
auto HirSerialiser::serialise(const ::std::pair<unsigned int, T>& e) -> void {
    out.writeCount(e.first);
    serialise(e.second);
}

auto HirSerialiser::serialise(bool v) -> void {
    out.writeBool(v);
}

auto HirSerialiser::serialise(unsigned int v) -> void {
    out.writeCount(v);
}

auto HirSerialiser::serialise(u8 v) -> void {
    out.writeU8(v);
}

auto HirSerialiser::serialise(u64 v) -> void {
    out.writeU64c(v);
}

auto HirSerialiser::serialise(i64 v) -> void {
    out.writeI64c(v);
}

auto HirSerialiser::serialise(const HIRGenericRef& ge) -> void {
    ASSERT_BUG(Span(), !ge.isSolverExistential(), "solver existential escaped into serialised HIR: " << ge);
    out.writeString(ge.name);
    out.writeU16(ge.binding);
}

auto HirSerialiser::serialiseArraysize(const HIRArraySize& as) -> void {
    out.writeTag(static_cast<int>(as.tag()));
    switch (as.tag()) {
        case HIRArraySize::TAG_Unevaluated: {
            auto& se = as.as_Unevaluated();
            serialise(se);
            break;
        }
        case HIRArraySize::TAG_Known: {
            auto& se = as.as_Known();
            out.writeU64c(se);
            break;
        }
    }
}

auto HirSerialiser::serialiseType(const HIRTypeData* ty) -> void {
    // Use string comparison to ensure that lifetimes are checked
    auto tyStr = FMT(ty);
    if (tyStr[0] == '{') {
        auto p = tyStr.find('}');
        tyStr = tyStr.substr(p + 1);
    }

    auto it = types.find(tyStr);
    if (it != types.end()) {
        out.writeCount(it->second);
        return;
    }
    out.writeCount(~0u);

    auto _ = out.openObject("HIR::TypeData");
    out.writeTag(ty->tag());
    switch ((*ty).tag()) {
        case HIRTypeData::TAG_Infer: {
            // BAAD
            break;
        }
        case HIRTypeData::TAG_Diverge: {
            break;
        }
        case HIRTypeData::TAG_Primitive: {
            auto& e = (*ty).as_Primitive();
            out.writeTag(static_cast<int>(e));
            break;
        }
        case HIRTypeData::TAG_Path: {
            auto& e = (*ty).as_Path();
            serialisePath(e.path);
            break;
        }
        case HIRTypeData::TAG_Generic: {
            auto& e = (*ty).as_Generic();
            serialise(e);
            break;
        }
        case HIRTypeData::TAG_TraitObject: {
            auto& e = (*ty).as_TraitObject();
            serialiseTraitpath(e.trait);
            serialiseVec(e.markers);
            out.writeString(e.lifetimeIdentity);
            out.writeBool(e.lifetimeIdentityHasFree);
            break;
        }
        case HIRTypeData::TAG_ErasedType: {
            auto& e = (*ty).as_ErasedType();
            TODO(Span(), "Serialse ErasedType?");

            out.writeBool(e.isSized);
            serialiseVec(e.traits);
            serialisePathparams(e.use);
            break;
        }
        case HIRTypeData::TAG_Array: {
            auto& e = (*ty).as_Array();
            serialiseType(e.inner);
            serialiseArraysize(e.size);
            break;
        }
        case HIRTypeData::TAG_Slice: {
            auto& e = (*ty).as_Slice();
            serialiseType(e.inner);
            break;
        }
        case HIRTypeData::TAG_Tuple: {
            auto& e = (*ty).as_Tuple();
            serialiseVec(e);
            break;
        }
        case HIRTypeData::TAG_Borrow: {
            auto& e = (*ty).as_Borrow();
            out.writeTag(static_cast<int>(e.type));
            serialiseType(e.inner);
            break;
        }
        case HIRTypeData::TAG_Pointer: {
            auto& e = (*ty).as_Pointer();
            out.writeTag(static_cast<int>(e.type));
            serialiseType(e.inner);
            break;
        }
        case HIRTypeData::TAG_NamedFunction: {
            auto& e = (*ty).as_NamedFunction();
            serialisePath(e.path);
            break;
        }
        case HIRTypeData::TAG_Function: {
            auto& e = (*ty).as_Function();
            out.writeBool(e.isUnsafe);
            out.writeBool(e.isVariadic);
            out.writeString(e.abi);
            serialiseType(e.rettype);
            serialiseVec(e.argTypes);
            out.writeBool(e.trackCaller);
            out.writeString(e.lifetimeIdentity);
            out.writeBool(e.lifetimeIdentityHasFree);
            break;
        }
        case HIRTypeData::TAG_Pattern: {
            auto& e = (*ty).as_Pattern();
            serialiseType(e.inner);
            out.writeCount(e.pattern.alternatives.size());
            for (const auto& range : e.pattern.alternatives) {
                out.writeBool(range.hasStart);
                if (range.hasStart) {
                    serialise(range.start);
                }
                out.writeBool(range.hasEnd);
                if (range.hasEnd) {
                    serialise(range.end);
                }
                out.writeBool(range.endInclusive);
            }
            break;
        } break;
        case HIRTypeData::TAG_NodeType:
            BUG(Span(), "Encountered invalid type when serialising - " << ty);
            break;
    }

    types.insert(std::make_pair(std::move(tyStr), types.size()));
}

auto HirSerialiser::serialiseSimplepath(const HIRSimplePath& path) -> void {
    if (!path.p) {
        auto _ = out.openObject(typeid(ThinVector<RcString>).name());
        out.writeCount(0);
        return;
    }
    serialiseVec(path.p->members);
}

auto HirSerialiser::serialisePathparams(const HIRPathParams& pp) -> void {
    serialiseVec(pp.types);
    serialiseVec(pp.values);
}

auto HirSerialiser::serialiseGenericpath(const HIRGenericPath& path) -> void {
    serialiseSimplepath(path.path);
    serialisePathparams(path.params);
}

auto HirSerialiser::serialise(const HIRGenericPath& path) -> void {
    serialiseGenericpath(path);
}

auto HirSerialiser::serialiseTraitpath(const HIRTraitPath& path) -> void {
    auto _ = out.openObject("HIR::TraitPath");
    serialiseGenericpath(path.path);
    serialiseStrmap(path.typeBounds);
    serialiseStrmap(path.traitBounds);
    out.writeU8(static_cast<u8>(path.constness));
}

auto HirSerialiser::serialise(const HIRTraitPath::AtyEqual& e) -> void {
    serialise(e.sourceTrait);
    serialisePathparams(e.atyParams);
    serialise(e.type);
}

auto HirSerialiser::serialise(const HIRTraitPath::AtyBound& e) -> void {
    serialise(e.sourceTrait);
    serialisePathparams(e.atyParams);
    serialiseVec(e.traits);
}

auto HirSerialiser::serialisePath(const HIRPath& path) -> void {
    switch (path.data.tag()) {
        case HIRPathData::TAG_Generic: {
            auto& e = path.data.as_Generic();
            out.writeTag(0);
            serialiseGenericpath(e);
            break;
        }
        case HIRPathData::TAG_UfcsInherent: {
            auto& e = path.data.as_UfcsInherent();
            out.writeTag(1);
            serialiseType(e.type);
            out.writeString(e.item);
            serialisePathparams(e.params);
            serialisePathparams(e.implParams);
            break;
        }
        case HIRPathData::TAG_UfcsKnown: {
            auto& e = path.data.as_UfcsKnown();
            out.writeTag(2);
            serialiseType(e.type);
            serialiseGenericpath(e.trait);
            out.writeString(e.item);
            serialisePathparams(e.params);
            break;
        }
        case HIRPathData::TAG_UfcsUnknown: {
            assert(!"Unexpected UfcsUnknown");
            break;
        }
    }
}

auto HirSerialiser::serialiseGenerics(const HIRGenericParams& params) -> void {
    ASSERT_BUG(Span(), params.paramKinds.empty() || params.hasParamOrder(), "Incomplete generic parameter order: " << params.paramKinds.length() << " entries for " << params.paramCount() << " parameters");
    out.writeCount(params.paramKinds.length());
    for (const auto kind : params.paramKinds) {
        out.writeU8(static_cast<u8>(kind));
    }
    serialiseVec(params.types);
    serialiseVec(params.values);
    serialiseVec(params.bounds);
}

auto HirSerialiser::serialise(const HIRTypeParamDef& pd) -> void {
    out.writeString(pd.name);
    serialiseType(pd.defaultValue);
    out.writeBool(pd.isSized);
}

auto HirSerialiser::serialise(const HIRValueParamDef& pd) -> void {
    out.writeString(pd.name);
    serialiseType(pd.type);
    serialise(pd.defaultValue);
}

auto HirSerialiser::serialise(const HIRGenericBound& b) -> void {
    switch (b.tag()) {
        case HIRGenericBound::TAG_TraitBound: {
            auto& e = b.as_TraitBound();
            out.writeTag(2);
            serialiseType(e.type);
            serialiseTraitpath(e.trait);
            out.writeU8(static_cast<u8>(e.constness));
            out.writeBool(e.isTrivial);
            break;
        }
        case HIRGenericBound::TAG_TypeEquality: {
            auto& e = b.as_TypeEquality();
            out.writeTag(3);
            serialiseType(e.type);
            serialiseType(e.otherType);
            break;
        }
    }
}

auto HirSerialiser::serialise(const HIRProcMacro& pm) -> void {
    switch (pm.ty) {
        case HIRProcMacro::Ty::Function:
            out.writeTag(0);
            break;
        case HIRProcMacro::Ty::Derive:
            out.writeTag(1);
            break;
        case HIRProcMacro::Ty::Attribute:
            out.writeTag(2);
            break;
    }
    serialise(pm.name);
    serialise(pm.path);
    serialiseVec(pm.attributes);
}

template <typename T>
auto HirSerialiser::serialise(const HIRCrate::ImplGroup<T>& ig) -> void {
    serialisePathmap(ig.named);
    serialiseVec(ig.nonNamed);
    serialiseVec(ig.generic);
}

auto HirSerialiser::serialiseCrate(const HIRCrate& crate) -> void {
    out.writeString(crate.crateName);
    out.writeTag(static_cast<int>(crate.edition));
    serialiseModule(crate.rootModule);

    size_t localItemTypeNamePathCount = 0;
    for (const auto* path = crate.localItemTypeNamePaths; path; path = path->next) {
        localItemTypeNamePathCount++;
    }
    out.writeCount(localItemTypeNamePathCount);
    for (const auto* path = crate.localItemTypeNamePaths; path; path = path->next) {
        serialiseSimplepath(path->modulePath);
        serialisePath(*path->ownerPath);
    }

    serialise(crate.typeImpls);
    serialisePathmap(crate.traitImpls);
    serialisePathmap(crate.markerImpls);

    serialiseVec(crate.exportedMacroNames);

    {
        decltype(crate.langItems) langItemsFiltered;
        for (const auto& ent : crate.langItems) {
            if (ent.second.crateName() == "" || ent.second.crateName() == crate.crateName) {
                langItemsFiltered.insert(ent);
            }
        }
        serialiseStrmap(langItemsFiltered);
    }

    out.writeCount(crate.extCrates.size());
    for (const auto& ext : crate.extCrates) {
        out.writeString(ext.first);
        out.writeString(ext.second.basename);
    }
    serialiseVec(crate.extLibs);
    serialiseVec(crate.linkPaths);
}

auto HirSerialiser::serialise(const HIRExternLibrary& lib) -> void {
    out.writeString(lib.name);
}

auto HirSerialiser::serialiseModule(const HIRModule& mod) -> void {
    auto _ = out.openObject("HIR::Module");

    // m_traits doesn't need to be serialised

    serialiseStrmap(mod.valueItems);
    serialiseStrmap(mod.modItems);
    serialiseStrmap(mod.macroItems);
}

auto HirSerialiser::serialiseTypeimpl(const HIRTypeImpl& impl) -> void {
    serialiseGenerics(impl.params);
    serialiseType(impl.type);

    out.writeCount(impl.methods.size());
    for (const auto& v : impl.methods) {
        out.writeString(v.first);
        out.writeBool(v.second.publicity.isGlobal());
        out.writeBool(v.second.isSpecialisable);
        serialise(v.second.data);
    }
    out.writeCount(impl.constants.size());
    for (const auto& v : impl.constants) {
        out.writeString(v.first);
        out.writeBool(v.second.publicity.isGlobal());
        out.writeBool(v.second.isSpecialisable);
        serialise(v.second.data);
    }
    out.writeCount(impl.types.size());
    for (const auto& v : impl.types) {
        out.writeString(v.first);
        out.writeBool(v.second.publicity.isGlobal());
        out.writeBool(v.second.isSpecialisable);
        serialise(v.second.data);
    }
    // m_src_module doesn't matter after typeck
}

auto HirSerialiser::serialise(const HIRTypeImpl& impl) -> void {
    serialiseTypeimpl(impl);
}

auto HirSerialiser::serialiseTraitimpl(const HIRTraitImpl& impl) -> void {
    serialiseGenerics(impl.params);
    serialisePathparams(impl.traitArgs);
    serialiseType(impl.type);
    out.writeBool(impl.isConst);
    out.writeBool(impl.isReservation);

    out.writeCount(impl.methods.size());
    for (const auto& v : impl.methods) {
        out.writeString(v.first);
        out.writeBool(v.second.isSpecialisable);
        serialise(v.second.data);
    }
    out.writeCount(impl.constants.size());
    for (const auto& v : impl.constants) {
        out.writeString(v.first);
        out.writeBool(v.second.isSpecialisable);
        serialise(v.second.data);
    }
    out.writeCount(impl.statics.size());
    for (const auto& v : impl.statics) {
        out.writeString(v.first);
        out.writeBool(v.second.isSpecialisable);
        serialise(v.second.data);
    }
    out.writeCount(impl.types.size());
    for (const auto& v : impl.types) {
        out.writeString(v.first);
        out.writeBool(v.second.isSpecialisable);
        serialise(v.second.data);
    }
    // m_src_module doesn't matter after typeck
}

auto HirSerialiser::serialise(const HIRTraitImpl& impl) -> void {
    serialiseTraitimpl(impl);
}

auto HirSerialiser::serialiseMarkerimpl(const HIRMarkerImpl& impl) -> void {
    serialiseGenerics(impl.params);
    serialisePathparams(impl.traitArgs);
    out.writeBool(impl.isPositive);
    serialiseType(impl.type);
}

auto HirSerialiser::serialise(const HIRMarkerImpl& impl) -> void {
    serialiseMarkerimpl(impl);
}

auto HirSerialiser::serialise(const HIRTypeData* ty) -> void {
    serialiseType(ty);
}

auto HirSerialiser::serialise(const HIRSimplePath& p) -> void {
    serialiseSimplepath(p);
}

auto HirSerialiser::serialise(const HIRTraitPath& p) -> void {
    serialiseTraitpath(p);
}

auto HirSerialiser::serialise(const ::std::string& v) -> void {
    out.writeString(v);
}

auto HirSerialiser::serialise(const RcString& v) -> void {
    out.writeString(v);
}

auto HirSerialiser::serialise(const Ident::Hygiene& h) -> void {
    auto _ = out.openObject(typeid(Ident::Hygiene).name());
    out.writeBool(h.hasModPath());
    if (h.hasModPath()) {
        out.writeString(h.modPath().crate);
        serialiseVec(h.modPath().ents);
    }
}

auto HirSerialiser::serialise(const ::MacroRulesPtr& mac) -> void {
    serialise(*mac);
}

auto HirSerialiser::serialise(const ::MacroRules& mac) -> void {
    //m_exported: IGNORE, should be set
    out.writeString(mac.sourceCrate);
    out.writeTag(static_cast<unsigned int>(mac.edition));
    assert(mac.rules.size() > 0);
    out.writeBool(mac.isMacroItem);
    out.writeBool(mac.transparent);
    serialiseVec(mac.rules);
    serialise(mac.hygiene);
}

auto HirSerialiser::serialise(const ::MacroPatEnt& pe) -> void {
    out.writeString(pe.name);
    out.writeCount(pe.nameIndex);
    out.writeTag(static_cast<int>(pe.type));
    if (pe.type == ::MacroPatEnt::PAT_TOKEN) {
        serialise(pe.tok);
    } else if (pe.type == ::MacroPatEnt::PAT_LOOP) {
        serialise(pe.tok);
        serialiseVec(pe.subpats);
    }
}

auto HirSerialiser::serialise(const ::SimplePatIfCheck& e) -> void {
    out.writeTag(static_cast<int>(e.ty));
    serialise(e.tok);
}

auto HirSerialiser::serialise(const ::SimplePatEnt& pe) -> void {
    out.writeTag(pe.tag());
    switch (pe.tag()) {
        case SimplePatEnt::TAG_End: {
            break;
        }
        case SimplePatEnt::TAG_LoopStart: {
            auto& e = pe.as_LoopStart();
            out.writeCount(e.index);
            break;
        }
        case SimplePatEnt::TAG_LoopNext: {
            break;
        }
        case SimplePatEnt::TAG_LoopEnd: {
            break;
        }
        case SimplePatEnt::TAG_Jump: {
            auto& e = pe.as_Jump();
            out.writeCount(e.jumpTarget);
            break;
        }
        case SimplePatEnt::TAG_ExpectTok: {
            auto& e = pe.as_ExpectTok();
            serialise(e);
            break;
        }
        case SimplePatEnt::TAG_ExpectPat: {
            auto& e = pe.as_ExpectPat();
            out.writeTag(static_cast<int>(e.type));
            out.writeCount(e.idx);
            break;
        }
        case SimplePatEnt::TAG_If: {
            auto& e = pe.as_If();
            out.writeBool(e.isEqual);
            out.writeCount(e.jumpTarget);
            serialiseVec(e.ents);
            break;
        }
    }
}

auto HirSerialiser::serialise(const ::MacroRulesArm& arm) -> void {
    serialiseVec(arm.paramNames);
    serialiseVec(arm.pattern);
    serialiseVec(arm.contents);
}

auto HirSerialiser::serialise(const ::MacroExpansionEnt& ent) -> void {
    switch (ent.tag()) {
        case MacroExpansionEnt::TAG_Token: {
            auto& e = ent.as_Token();
            out.writeTag(0);
            serialise(e);
            break;
        }
        case MacroExpansionEnt::TAG_NamedValue: {
            auto& e = ent.as_NamedValue();
            out.writeTag(1);
            out.writeU8(e >> 24);
            out.writeCount(e & 0x00FFFFFF);
            break;
        }
        case MacroExpansionEnt::TAG_Loop: {
            auto& e = ent.as_Loop();
            out.writeTag(2);
            serialiseVec(e.entries);
            serialise(e.joiner);
            serialise(e.controllingInputLoops);
            break;
        }
        case MacroExpansionEnt::TAG_Concat: {
            auto& e = ent.as_Concat();
            out.writeTag(3);
            serialiseVec(e);
            break;
        }
    }
}

auto HirSerialiser::serialise(const ::MacroExpansionConcatEnt& e) -> void {
    out.writeTag(e.tag());
    switch (e.tag()) {
        case MacroExpansionConcatEnt::TAG_Ident: {
            auto& i = e.as_Ident();
            serialise(i.hygiene);
            out.writeString(i.name);
            break;
        }
        case MacroExpansionConcatEnt::TAG_Named: {
            auto& i = e.as_Named();
            serialise(i);
            break;
        }
    }
}

auto HirSerialiser::serialise(const ::Token& tok) -> void {
    out.writeTag(tok.type_);
    serialise(tok.data_);
    // TODO: Position information.
}

auto HirSerialiser::serialise(const ::Token::Data& td) -> void {
    out.writeTag(td.tag());
    switch (td.tag()) {
        break;
        case TokenData::TAG_None: {
        } break;
            break;
        case TokenData::TAG_String: {
            auto& e = td.as_String();
            out.writeString(e);

        } break;
            break;
        case TokenData::TAG_Ident: {
            auto& e = td.as_Ident();
            serialise(e.hygiene);
            out.writeString(e.name);

        } break;
            break;
        case TokenData::TAG_Integer: {
            auto& e = td.as_Integer();
            out.writeTag(e.datatype);
            out.writeU128(e.intval);

        } break;
            break;
        case TokenData::TAG_Float: {
            auto& e = td.as_Float();
            out.writeTag(e.datatype);
            out.writeFloatValue(e.floatval);

        } break;
            break;
        case TokenData::TAG_Fragment: {
            assert(!"Serialising interpolated macro fragment - should have been handled in HIR lowering");
        }
    }
}

auto HirSerialiser::serialise(const EncodedLiteral& lit) -> void {
    serialise(lit.bytes);
    out.writeCount(lit.relocations.size());
    for (const auto& reloc : lit.relocations) {
        out.writeCount(reloc.ofs);
        out.writeCount(reloc.len);
        out.writeBool(reloc.preserveTrackCaller);
        if (reloc.p) {
            out.writeTag(0);
            serialisePath(*reloc.p);
        } else {
            out.writeTag(1);
            serialise(reloc.bytes);
        }
    }
}

auto HirSerialiser::serialise(const HIRConstGenericUnevaluated& v) -> void {
    ASSERT_BUG(v.expr->span(), v.expr->mir, "Encountered non-translated value in ConstGeneric: " << v);
    out.writeBool(v.selfType != nullptr);
    if (v.selfType) {
        serialiseType(v.selfType);
    }
    serialisePathparams(v.paramsImpl);
    serialisePathparams(v.paramsItem);
    serialise(*v.expr);
}

auto HirSerialiser::serialise(const HIRConstGeneric& v) -> void {
    out.writeTag(v.tag());
    switch (v.tag()) {
        case HIRConstGeneric::TAG_Infer: {
            break;
        }
        case HIRConstGeneric::TAG_Unevaluated: {
            auto& e = v.as_Unevaluated();
            serialise(*e);
            break;
        }
        case HIRConstGeneric::TAG_Generic: {
            auto& e = v.as_Generic();
            serialise(e);
            break;
        }
        case HIRConstGeneric::TAG_Evaluated: {
            auto& e = v.as_Evaluated();
            serialise(*e);
            break;
        }
    }
}

auto HirSerialiser::serialise(const HIRExprPtr& exp, bool saveMir) -> void {
    auto _ = out.openObject("HIR::ExprPtr");
    saveMir &= static_cast<bool>(exp.mir);
    out.writeBool(saveMir);
    if (saveMir) {
        serialise(*exp.mir);
    }
    serialiseVec(exp.erasedTypes);
}

auto HirSerialiser::serialise(const MIRFunction& mir) -> void {
    // Write out MIR.
    serialiseVec(mir.locals);
    serialiseVec(mir.dropFlags);
    serialiseVec(mir.blocks);
}

auto HirSerialiser::serialise(const MIRBasicBlock& block) -> void {
    serialiseVec(block.statements);
    serialise(block.terminator);
    out.writeBool(block.isCleanup);
}

auto HirSerialiser::serialise(const AsmLineFragment& l) -> void {
    serialise(l.before);
    out.writeCount(l.index);
    out.writeI64c(l.modifier);
}

auto HirSerialiser::serialise(const AsmLine& l) -> void {
    serialiseVec(l.frags);
    serialise(l.trailing);
}

auto HirSerialiser::serialise(const AsmRegisterSpec& r) -> void {
    out.writeTag(static_cast<unsigned>(r.tag()));
    switch (r.tag()) {
        case AsmRegisterSpec::TAG_Class: {
            auto& e = r.as_Class();
            out.writeTag(static_cast<unsigned>(e));
            break;
        }
        case AsmRegisterSpec::TAG_Explicit: {
            auto& e = r.as_Explicit();
            out.writeString(e);
            break;
        }
    }
}

auto HirSerialiser::serialise(const MIRAsmParam& p) -> void {
    out.writeTag(static_cast<unsigned>(p.tag()));
    switch (p.tag()) {
        case MIRAsmParam::TAG_Sym: {
            auto& e = p.as_Sym();
            serialisePath(e);
            break;
        }
        case MIRAsmParam::TAG_Const: {
            auto& e = p.as_Const();
            serialise(e);
            break;
        }
        case MIRAsmParam::TAG_Reg: {
            auto& e = p.as_Reg();
            out.writeTag(static_cast<unsigned>(e.dir));
            serialise(e.spec);
            out.writeBool(bool(e.input));
            if (e.input) {
                serialise(e.input);
            }
            out.writeBool(bool(e.output));
            if (e.output) {
                serialise(e.output);
            }
            break;
        }
        case MIRAsmParam::TAG_Label: {
            auto& e = p.as_Label();
            out.writeCount(e);
            break;
        }
    }
}

auto HirSerialiser::serialise(const AsmOptions& o) -> void {
    u16 bitflag1 = 0;
#define BIT(i, fld) \
    if (fld)        \
        bitflag1 |= 1 << (i);
    BIT(0, o.pure);
    BIT(1, o.nomem);
    BIT(2, o.readonly);
    BIT(3, o.preservesFlags);
    BIT(4, o.noreturn);
    BIT(5, o.nostack);
    BIT(6, o.attSyntax);
    BIT(7, o.raw);
#undef BIT
    out.writeU16(bitflag1);
}

auto HirSerialiser::serialise(const MIRStatement& stmt) -> void {
    auto _ = out.openObject("MIR::Statement");
    switch (stmt.tag()) {
        case MIRStatement::TAG_Assign: {
            auto& e = stmt.as_Assign();
            out.writeTag(0);
            serialise(e.dst);
            serialise(e.src);
            break;
        }
        case MIRStatement::TAG_Asm: {
            auto& e = stmt.as_Asm();
            out.writeTag(2);
            out.writeString(e.tpl);
            serialiseVec(e.outputs);
            serialiseVec(e.inputs);
            serialiseVec(e.clobbers);
            serialiseVec(e.flags);
            break;
        }
        case MIRStatement::TAG_SetDropFlag: {
            auto& e = stmt.as_SetDropFlag();
            out.writeTag(3);
            out.writeCount(e.idx);
            out.writeBool(e.newVal);
            out.writeCount(e.other);
            break;
        }
        case MIRStatement::TAG_ScopeEnd: {
            auto& e = stmt.as_ScopeEnd();
            out.writeTag(4);
            serialiseVec(e.slots);
            break;
        }
        case MIRStatement::TAG_Asm2: {
            auto& e = stmt.as_Asm2();
            out.writeTag(5);
            serialise(e.options);
            serialiseVec(e.lines);
            serialiseVec(e.params);
            break;
        }
        case MIRStatement::TAG_SaveDropFlag: {
            auto& e = stmt.as_SaveDropFlag();
            out.writeTag(6);
            serialise(e.slot);
            out.writeCount(e.bitIndex);
            out.writeCount(e.idx);
            break;
        }
        case MIRStatement::TAG_LoadDropFlag: {
            auto& e = stmt.as_LoadDropFlag();
            out.writeTag(7);
            out.writeCount(e.idx);
            serialise(e.slot);
            out.writeCount(e.bitIndex);
            break;
        }
    }
}

auto HirSerialiser::serialise(const MIRTerminator& term) -> void {
    auto serialiseUnwind = [this](const MIRUnwindAction& action) {
        out.writeTag(static_cast<int>(action.tag()));
        if (action.is_Cleanup()) {
            auto& target = action.as_Cleanup();
            out.writeCount(target);
        }
    };
    out.writeTag(static_cast<int>(term.tag()));
    switch (term.tag()) {
        case MIRTerminator::TAG_Incomplete: {
            // NOTE: loops that diverge (don't break) leave a dangling bb
            break;
        }
        case MIRTerminator::TAG_Return: {
            break;
        }
        case MIRTerminator::TAG_UnwindResume: {
            break;
        }
        case MIRTerminator::TAG_UnwindTerminate: {
            break;
        }
        case MIRTerminator::TAG_Unreachable: {
            break;
        }
        case MIRTerminator::TAG_Goto: {
            auto& e = term.as_Goto();
            out.writeCount(e);
            break;
        }
        case MIRTerminator::TAG_If: {
            auto& e = term.as_If();
            serialise(e.cond);
            out.writeCount(e.bbTrue);
            out.writeCount(e.bbFalse);
            break;
        }
        case MIRTerminator::TAG_Switch: {
            auto& e = term.as_Switch();
            serialise(e.val);
            serialiseVec(e.targets);
            out.writeCount(e.validFlag);
            out.writeCount(e.invalidTarget);
            break;
        }
        case MIRTerminator::TAG_SwitchValue: {
            auto& e = term.as_SwitchValue();
            serialise(e.val);
            out.writeCount(e.defTarget);
            serialiseVec(e.targets);
            serialise(e.values);
            break;
        }
        case MIRTerminator::TAG_Drop: {
            auto& e = term.as_Drop();
            out.writeTag(static_cast<unsigned>(e.kind));
            serialise(e.slot);
            out.writeCount(e.flagIdx);
            out.writeCount(e.target);
            serialiseUnwind(e.unwind);
            break;
        }
        case MIRTerminator::TAG_Call: {
            auto& e = term.as_Call();
            out.writeCount(e.retBlock);
            serialiseUnwind(e.unwind);
            serialise(e.retVal);
            serialise(e.fcn);
            serialiseVec(e.args);
            out.writeString(e.source.filename);
            out.writeCount(e.source.line);
            out.writeCount(e.source.column);
            out.writeBool(e.tracksCaller);
            break;
        }
        case MIRTerminator::TAG_TailCall: {
            auto& e = term.as_TailCall();
            serialise(e.fcn);
            serialiseVec(e.args);
            out.writeString(e.source.filename);
            out.writeCount(e.source.line);
            out.writeCount(e.source.column);
            out.writeBool(e.tracksCaller);
            break;
        }
        case MIRTerminator::TAG_Asm2: {
            auto& e = term.as_Asm2();
            serialise(e.options);
            serialiseVec(e.lines);
            serialiseVec(e.params);
            out.writeCount(e.retBlock);
            break;
        }
    }
}

auto HirSerialiser::serialise(const MIRSwitchValues& sv) -> void {
    out.writeTag(static_cast<int>(sv.tag()));
    switch (sv.tag()) {
        case MIRSwitchValues::TAG_Unsigned: {
            auto& e = sv.as_Unsigned();
            serialiseVec(e);
            break;
        }
        case MIRSwitchValues::TAG_Signed: {
            auto& e = sv.as_Signed();
            serialiseVec(e);
            break;
        }
        case MIRSwitchValues::TAG_String: {
            auto& e = sv.as_String();
            serialiseVec(e);
            break;
        }
        case MIRSwitchValues::TAG_ByteString: {
            auto& e = sv.as_ByteString();
            serialiseVec(e);
            break;
        }
    }
}

auto HirSerialiser::serialise(const MIRCallTarget& ct) -> void {
    out.writeTag(static_cast<int>(ct.tag()));
    switch (ct.tag()) {
        case MIRCallTarget::TAG_Value: {
            auto& e = ct.as_Value();
            serialise(e);
            break;
        }
        case MIRCallTarget::TAG_Path: {
            auto& e = ct.as_Path();
            serialisePath(e);
            break;
        }
        case MIRCallTarget::TAG_Intrinsic: {
            auto& e = ct.as_Intrinsic();
            out.writeString(e.name);
            serialisePathparams(e.params);
            break;
        }
    }
}

auto HirSerialiser::serialise(const MIRParam& p) -> void {
    out.writeTag(static_cast<int>(p.tag()));
    switch (p.tag()) {
        case MIRParam::TAG_LValue: {
            auto& e = p.as_LValue();
            serialise(e);
            break;
        }
        case MIRParam::TAG_Borrow: {
            auto& e = p.as_Borrow();
            out.writeTag(static_cast<int>(e.type));
            serialise(e.val);
            break;
        }
        case MIRParam::TAG_Constant: {
            auto& e = p.as_Constant();
            serialise(e);
            break;
        }
    }
}

auto HirSerialiser::serialise(const MIRLValue& lv) -> void {
    if (lv.root.is_Static()) {
        out.writeCount(3);
        serialisePath(lv.root.as_Static());
    } else {
        out.writeCount(lv.root.getInner());
    }
    serialiseVec(lv.wrappers);
}

auto HirSerialiser::serialise(const MIRLValue::Wrapper& w) -> void {
    out.writeCount(w.getInner());
}

auto HirSerialiser::serialise(const MIRRValue& val) -> void {
    out.writeTag(val.tag());
    switch (val.tag()) {
        case MIRRValue::TAG_Use: {
            auto& e = val.as_Use();
            serialise(e);
            break;
        }
        case MIRRValue::TAG_Constant: {
            auto& e = val.as_Constant();
            serialise(e);
            break;
        }
        case MIRRValue::TAG_SizedArray: {
            auto& e = val.as_SizedArray();
            serialise(e.val);
            serialiseArraysize(e.count);
            break;
        }
        case MIRRValue::TAG_Borrow: {
            auto& e = val.as_Borrow();
            out.writeTag(static_cast<int>(e.type));
            out.writeBool(e.isRaw);
            serialise(e.val);
            break;
        }
        case MIRRValue::TAG_Cast: {
            auto& e = val.as_Cast();
            serialise(e.val);
            serialise(e.type);
            break;
        }
        case MIRRValue::TAG_BinOp: {
            auto& e = val.as_BinOp();
            serialise(e.valL);
            out.writeTag(static_cast<int>(e.op));
            serialise(e.valR);
            break;
        }
        case MIRRValue::TAG_UniOp: {
            auto& e = val.as_UniOp();
            serialise(e.val);
            out.writeTag(static_cast<int>(e.op));
            break;
        }
        case MIRRValue::TAG_DstMeta: {
            auto& e = val.as_DstMeta();
            serialise(e.val);
            break;
        }
        case MIRRValue::TAG_DstPtr: {
            auto& e = val.as_DstPtr();
            serialise(e.val);
            break;
        }
        case MIRRValue::TAG_MakeDst: {
            auto& e = val.as_MakeDst();
            serialise(e.ptrVal);
            auto b = !(e.metaVal.is_Constant() && e.metaVal.as_Constant().is_ItemAddr() && e.metaVal.as_Constant().as_ItemAddr().get() == nullptr);
            out.writeBool(b);
            if (b) {
                serialise(e.metaVal);
            }
            break;
        }
        case MIRRValue::TAG_Tuple: {
            auto& e = val.as_Tuple();
            serialiseVec(e.vals);
            break;
        }
        case MIRRValue::TAG_Array: {
            auto& e = val.as_Array();
            serialiseVec(e.vals);
            break;
        }
        case MIRRValue::TAG_UnionVariant: {
            auto& e = val.as_UnionVariant();
            serialiseGenericpath(e.path);
            out.writeCount(e.index);
            serialise(e.val);
            break;
        }
        case MIRRValue::TAG_EnumVariant: {
            auto& e = val.as_EnumVariant();
            serialiseGenericpath(e.path);
            out.writeCount(e.index);
            serialiseVec(e.vals);
            break;
        }
        case MIRRValue::TAG_Struct: {
            auto& e = val.as_Struct();
            serialiseGenericpath(e.path);
            serialiseVec(e.vals);
            break;
        }
    }
}

auto HirSerialiser::serialise(const MIRConstant& v) -> void {
    out.writeTag(v.tag());
    switch (v.tag()) {
        case MIRConstant::TAG_Int: {
            auto& e = v.as_Int();
            out.writeU128(e.v.getInner());
            out.writeTag(static_cast<unsigned>(e.t));
            break;
        }
        case MIRConstant::TAG_Uint: {
            auto& e = v.as_Uint();
            out.writeU128(e.v);
            out.writeTag(static_cast<unsigned>(e.t));
            break;
        }
        case MIRConstant::TAG_Float: {
            auto& e = v.as_Float();
            out.writeFloatValue(e.v);
            out.writeTag(static_cast<unsigned>(e.t));
            break;
        }
        case MIRConstant::TAG_Bool: {
            auto& e = v.as_Bool();
            out.writeBool(e.v);
            break;
        }
        case MIRConstant::TAG_Bytes: {
            auto& e = v.as_Bytes();
            out.writeCount(e.size());
            out.write(e.data(), e.size());
            break;
        }
        case MIRConstant::TAG_StaticString: {
            auto& e = v.as_StaticString();
            out.writeString(e);
            break;
        }
        case MIRConstant::TAG_Encoded: {
            auto& e = v.as_Encoded();
            serialise(e.type);
            serialise(e.value);
            break;
        }
        case MIRConstant::TAG_Const: {
            auto& e = v.as_Const();
            ASSERT_BUG(Span(), monomorphisePathNeeded(*e.p), "Unexpected Constant: " << *e.p);
            serialisePath(*e.p);
            break;
        }
        case MIRConstant::TAG_Generic: {
            auto& e = v.as_Generic();
            serialise(e);
            break;
        }
        case MIRConstant::TAG_Function: {
            auto& e = v.as_Function();
            serialisePath(*e.p);
            break;
        }
        case MIRConstant::TAG_ItemAddr: {
            auto& e = v.as_ItemAddr();
            serialisePath(*e);
            out.writeU128(e.offset);
            break;
        }
    }
}

auto HirSerialiser::serialise(const HIRTypeItem& item) -> void {
    switch (item.tag()) {
        case HIRTypeItem::TAG_Import: {
            auto& e = item.as_Import();
            out.writeTag(0);
            serialiseSimplepath(e.path);
            out.writeBool(e.isVariant);
            out.writeCount(e.idx);
            break;
        }
        case HIRTypeItem::TAG_Module: {
            auto& e = item.as_Module();
            out.writeTag(1);
            serialiseModule(e);
            break;
        }
        case HIRTypeItem::TAG_TypeAlias: {
            auto& e = item.as_TypeAlias();
            out.writeTag(2);
            serialise(e);
            break;
        }
        case HIRTypeItem::TAG_Enum: {
            auto& e = item.as_Enum();
            out.writeTag(3);
            serialise(e);
            break;
        }
        case HIRTypeItem::TAG_Struct: {
            auto& e = item.as_Struct();
            out.writeTag(4);
            serialise(e);
            break;
        }
        case HIRTypeItem::TAG_Trait: {
            auto& e = item.as_Trait();
            out.writeTag(5);
            serialise(e);
            break;
        }
        case HIRTypeItem::TAG_Union: {
            auto& e = item.as_Union();
            out.writeTag(6);
            serialise(e);
            break;
        }
        case HIRTypeItem::TAG_ExternType: {
            auto& e = item.as_ExternType();
            out.writeTag(7);
            serialise(e);
            break;
        }
        case HIRTypeItem::TAG_TraitAlias: {
            auto& e = item.as_TraitAlias();
            out.writeTag(8);
            serialise(e);
            break;
        }
    }
}

auto HirSerialiser::serialise(const HIRMacroItem& item) -> void {
    auto _ = out.openObject("HIR::MacroItem");
    out.writeTag(item.tag());
    switch (item.tag()) {
        case HIRMacroItem::TAG_Import: {
            auto& e = item.as_Import();
            serialise(e.path);
            break;
        }
        case HIRMacroItem::TAG_MacroRules: {
            auto& e = item.as_MacroRules();
            serialise(e);
            break;
        }
        case HIRMacroItem::TAG_ProcMacro: {
            auto& e = item.as_ProcMacro();
            serialise(e);
            break;
        }
    }
}

auto HirSerialiser::serialise(const HIRValueItem& item) -> void {
    switch (item.tag()) {
        case HIRValueItem::TAG_Import: {
            auto& e = item.as_Import();
            out.writeTag(0);
            serialiseSimplepath(e.path);
            out.writeBool(e.isVariant);
            out.writeCount(e.idx);
            break;
        }
        case HIRValueItem::TAG_Constant: {
            const auto& e = *item.as_Constant();
            out.writeTag(1);
            serialise(e);
            break;
        }
        case HIRValueItem::TAG_Static: {
            const auto& e = *item.as_Static();
            out.writeTag(2);
            serialise(e);
            break;
        }
        case HIRValueItem::TAG_StructConstant: {
            auto& e = item.as_StructConstant();
            out.writeTag(3);
            serialiseSimplepath(e.ty);
            break;
        }
        case HIRValueItem::TAG_Function: {
            const auto& e = *item.as_Function();
            out.writeTag(4);
            serialise(e);
            break;
        }
        case HIRValueItem::TAG_StructConstructor: {
            auto& e = item.as_StructConstructor();
            out.writeTag(5);
            serialiseSimplepath(e.ty);
            break;
        }
    }
}

auto HirSerialiser::serialise(const HIRLinkage& linkage) -> void {
    out.writeString(linkage.name);
}

auto HirSerialiser::serialise(const HIRFunction& fcn) -> void {
    assert(!fcn.traitReturnType);
    auto _ = out.openObject("HIR::Function");

    serialise(fcn.linkage);

    out.writeTag(static_cast<int>(fcn.receiver));
    serialise(fcn.receiverType.value_or(typeInterner.infer()));
    out.writeString(fcn.abi);
    out.writeBool(fcn.unsafe);
    out.writeBool(fcn.isConst);
    serialise(fcn.markings);

    serialiseGenerics(fcn.params);
    out.writeCount(fcn.args.size());
    for (const auto& a : fcn.args) {
        serialise(a.second);
    }
    out.writeBool(fcn.variadic);
    out.writeBool(fcn.hasNamedVariadic);
    serialise(fcn.returnType);
    out.writeString(fcn.source.filename);
    out.writeCount(fcn.source.line);
    out.writeCount(fcn.source.column);

    serialise(fcn.code, fcn.saveCode || fcn.isConst);
}

auto HirSerialiser::serialise(const HIRFunction::Markings& m) -> void {
    auto _ = out.openObject("HIR::Function::Markings");
    serialiseVec(m.rustcLegacyConstGenerics);
    out.writeBool(m.trackCaller);
    out.writeBool(m.isRustcIntrinsic);
    out.writeBool(m.isRustcPromotable);
    // `#[must_use]` is reported at the call site, which may be in another
    // crate, so it has to travel with the function.
    out.writeBool(m.mustUse);
    out.writeCount(m.alignment);
    out.writeTag(static_cast<unsigned int>(m.inlineType));
}

auto HirSerialiser::serialise(const HIRConstant& item) -> void {
    serialiseGenerics(item.params);
    serialise(item.type);
    serialise(item.value);
    bool writeVal = item.valueState == HIRConstant::ValueState::Known;
    out.writeBool(writeVal);
    if (writeVal) {
        serialise(item.valueRes);
    }
}

auto HirSerialiser::serialise(const HIRStatic& item) -> void {
    serialise(item.linkage);
    serialiseGenerics(item.params);

    u8 bitflag1 = 0;
#define BIT(i, fld) \
    if (fld)        \
        bitflag1 |= 1 << (i);
    BIT(0, item.isMut);
    BIT(1, item.saveLiteral)
    BIT(2, item.explicitAlignment != 0)
    BIT(3, item.isPromoted)
#undef BIT
    out.writeU8(bitflag1);
    if (item.explicitAlignment != 0) {
        out.writeCount(item.explicitAlignment);
    }
    serialise(item.type);

    if (item.params.isGeneric()) {
        serialise(item.value);
    }
    // NOTE: Value not stored (What if the static is generic? It can't be.)
    // - Need to store if the item was from a const (special linkage?)
    if (item.saveLiteral) {
        serialise(item.valueRes);
    }
}

auto HirSerialiser::serialise(const HIRTypeAlias& ta) -> void {
    serialiseGenerics(ta.params);
    serialiseType(ta.type);
}

auto HirSerialiser::serialise(const HIRTraitAlias& ta) -> void {
    serialiseGenerics(ta.params);
    serialiseVec(ta.traits);
}

auto HirSerialiser::serialise(const HIREnum& item) -> void {
    auto _ = out.openObject("HIR::Enum");
    serialiseGenerics(item.params);
    out.writeBool(item.isCRepr);
    out.writeTag(static_cast<int>(item.tagRepr));
    out.writeCount(item.forcedAlignment);
    out.writeBool(item.mustUse);
    serialise(item.data);

    serialise(item.markings);
}

auto HirSerialiser::serialise(const HIREnum::Class& v) -> void {
    out.writeTag(v.tag());
    switch (v.tag()) {
        case HIREnumClass::TAG_Value: {
            auto& e = v.as_Value();
            serialiseVec(e.variants);
            break;
        }
        case HIREnumClass::TAG_Data: {
            auto& e = v.as_Data();
            serialiseVec(e);
            break;
        }
    }
}

auto HirSerialiser::serialise(const HIREnum::ValueVariant& v) -> void {
    out.writeString(v.name);
    // NOTE: No expr, no longer needed
    out.writeU64(v.val.truncateU64());
}

auto HirSerialiser::serialise(const HIREnum::DataVariant& v) -> void {
    out.writeString(v.name);
    out.writeBool(v.isStruct);
    serialise(v.type);
    out.writeU64(v.discriminantValue.truncateU64());
}

auto HirSerialiser::serialise(const HIRTraitMarkings& m) -> void {
    u8 bitflag1 = 0;
#define BIT(i, fld) \
    if (fld)        \
        bitflag1 |= 1 << (i);
    BIT(0, m.hasADeref)
    BIT(1, m.isCopy)
    BIT(2, m.hasDropImpl)
    BIT(3, m.hasConstDropImpl)
#undef BIT
    out.writeU8(bitflag1);

    // TODO: auto_impls
}

auto HirSerialiser::serialise(const HIRStructMarkings& m) -> void {
    u8 bitflag1 = 0;
#define BIT(i, fld) \
    if (fld)        \
        bitflag1 |= 1 << (i);
    BIT(0, m.canUnsize)
    BIT(1, m.isNonzero)
    BIT(2, m.boundedMax)
    BIT(3, m.isFundamental)
    BIT(4, m.isNoNiche)
    BIT(5, m.isAsyncDropGlue)
#undef BIT
    out.writeU8(bitflag1);

    out.writeTag(static_cast<unsigned int>(m.dstType));
    out.writeTag(static_cast<unsigned int>(m.coerceUnsized));
    out.writeCount(m.coerceUnsizedIndex);
    out.writeCount(m.coerceParam);
    out.writeCount(m.unsizedField);
    out.writeCount(m.unsizedParam);
    if (m.boundedMax) {
        out.writeU128(m.boundedMaxValue);
    }
    // TODO: auto_impls
}

auto HirSerialiser::serialise(const HIRStruct& item) -> void {
    auto _ = out.openObject("HIR::Struct");

    serialiseGenerics(item.params);
    out.writeTag(static_cast<int>(item.repr));

    out.writeTag(item.data.tag());
    switch (item.data.tag()) {
        case HIRStructData::TAG_Unit: {
            break;
        }
        case HIRStructData::TAG_Tuple: {
            auto& e = item.data.as_Tuple();
            serialiseVec(e);
            break;
        }
        case HIRStructData::TAG_Named: {
            auto& e = item.data.as_Named();
            serialiseVec(e);
            break;
        }
    }

    out.writeCount(item.forcedAlignment);
    out.writeCount(item.maxFieldAlignment);
    out.writeBool(item.mustUse);
    serialise(item.markings);
    serialise(item.structMarkings);
}

auto HirSerialiser::serialise(const HIRStructField& fld) -> void {
    serialise(fld.name);
    serialise(fld.vis);
    serialise(fld.ty);
    out.writeBool(fld.defaultValue != nullptr);
    if (fld.defaultValue) {
        serialise(*fld.defaultValue);
    }
}

auto HirSerialiser::serialise(const HIRUnion& item) -> void {
    serialiseGenerics(item.params);
    out.writeTag(static_cast<int>(item.repr));

    serialiseVec(item.variants);
    out.writeCount(item.forcedAlignment);
    out.writeCount(item.maxFieldAlignment);
    out.writeBool(item.mustUse);

    serialise(item.markings);
}

auto HirSerialiser::serialise(const HIRExternType& item) -> void {
    serialise(item.markings);
}

auto HirSerialiser::serialise(const HIRTrait& item) -> void {
    auto _ = out.openObject("HIR::Trait");

    serialiseGenerics(item.params);
    // Kept as one byte for compatibility with metadata written before
    // the fundamental bit was represented in HIR.
    out.writeU8((item.isMarker ? 1u : 0u) | (item.isFundamental ? 2u : 0u) | (item.isCoinductive ? 4u : 0u) | (item.isConst ? 8u : 0u) | (item.skipArrayDuringMethodDispatch ? 16u : 0u) | (item.skipBoxedSliceDuringMethodDispatch ? 32u : 0u) | (item.mustUse ? 64u : 0u));
    serialiseStrmap(item.types);
    serialiseStrmap(item.values);
    serialiseStrmap(item.valueIndexes);
    serialiseStrmap(item.typeIndexes);
    out.writeCount(item.vtableParentTraitsStart);
    serialiseVec(item.allParentTraits);
    serialise(item.vtablePath);
}

auto HirSerialiser::serialise(const HIRTraitValueItem& tvi) -> void {
    out.writeTag(tvi.tag());
    switch (tvi.tag()) {
        case HIRTraitValueItem::TAG_Constant: {
            auto& e = tvi.as_Constant();
            serialise(e);
            break;
        }
        case HIRTraitValueItem::TAG_Static: {
            auto& e = tvi.as_Static();
            serialise(e);
            break;
        }
        case HIRTraitValueItem::TAG_Function: {
            auto& e = tvi.as_Function();
            serialise(e);
            break;
        }
    }
}

auto HirSerialiser::serialise(const HIRAssociatedType& at) -> void {
    serialiseGenerics(at.generics);
    out.writeBool(at.isSized);
    serialiseVec(at.traitBounds);
    serialiseType(at.defaultValue);
}
