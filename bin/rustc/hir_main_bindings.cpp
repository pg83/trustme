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

// TODO: Have an environment variable that controls if debug is enabled here.
#define DEBUG_EXTRA_ENABLE &&desDebugEnabled()

namespace {
    bool desDebugEnabled();
}

//#define DISABLE_DEBUG   //  Disable debug for this function - too hot

namespace {
    bool desDebugEnabled() {
        static unsigned enabled = 0;
        if (enabled == 0) {
            enabled = (getenv("MRUSTC_DEBUG_DESERIALISE") ? 2 : 1);
        }
        return enabled > 1;
    }

    HIRPublicity gVisPrivate = HIRPublicity::newNone();
}

//namespace {

template <typename T>
struct D {};

class HirDeserialiser {
    RcString crateName;
    ::std::vector<HIRTypeRef> types;
    HIRSerialiseReader& in;
    HIRTypeInterner& typeInterner;

public:
    stl::ObjPool& mPool;

    HirDeserialiser(stl::ObjPool& pool, HIRSerialiseReader& in, HIRTypeInterner& typeInterner)
        : mPool(pool)
        , in(in)
        , typeInterner(typeInterner)
    {
    }

    RcString readIstring() {
        return in.readIstring();
    }

    ::std::string readString() {
        return in.readString();
    }

    bool readBool() {
        return in.readBool();
    }

    uint8_t readU8() {
        return in.readU8();
    }

    size_t deserialiseCount() {
        return in.readCount();
    }

    template <typename V>
    ::std::map<::std::string, V> deserialiseStrmap() {
        TRACE_FUNCTION_F("<" << typeid(V).name() << ">");
        size_t n = in.readCount();
        ::std::map<::std::string, V> rv;
        for (size_t i = 0; i < n; i++) {
            auto s = in.readString();
            rv.insert(::std::make_pair(mv$(s), D<V>::des(*this)));
        }
        return rv;
    }

    template <typename V>
    ::std::unordered_map<::std::string, V> deserialiseStrumap() {
        TRACE_FUNCTION_F("<" << typeid(V).name() << ">");
        size_t n = in.readCount();
        ::std::unordered_map<::std::string, V> rv;
        for (size_t i = 0; i < n; i++) {
            auto s = in.readString();
            DEBUG("- " << s);
            rv.insert(::std::make_pair(mv$(s), D<V>::des(*this)));
        }
        return rv;
    }

    template <typename V>
    ::std::unordered_multimap<::std::string, V> deserialiseStrummap() {
        TRACE_FUNCTION_F("<" << typeid(V).name() << ">");
        size_t n = in.readCount();
        ::std::unordered_multimap<::std::string, V> rv;
        for (size_t i = 0; i < n; i++) {
            auto s = in.readString();
            DEBUG("- " << s);
            rv.insert(::std::make_pair(mv$(s), D<V>::des(*this)));
        }
        return rv;
    }

    template <typename V>
    ::std::map<RcString, V> deserialiseIstrmap() {
        TRACE_FUNCTION_F("<" << typeid(V).name() << ">");
        size_t n = in.readCount();
        ::std::map<RcString, V> rv;
        for (size_t i = 0; i < n; i++) {
            auto s = in.readIstring();
            rv.insert(::std::make_pair(mv$(s), D<V>::des(*this)));
        }
        return rv;
    }

    // Pool-allocated variant: module item tables are the single biggest heap
    // consumer when loading extern crates, and they live for the whole
    // compilation, so they belong in the arena rather than in unique_ptrs.
    template <typename T>
    ::std::unordered_map<RcString, T*> deserialiseIstrumapPooled() {
        size_t n = in.readCount();
        ::std::unordered_map<RcString, T*> rv;
        for (size_t i = 0; i < n; i++) {
            auto s = in.readIstring();
            rv.insert(::std::make_pair(mv$(s), mPool.make<T>(D<T>::des(*this))));
        }
        return rv;
    }

    template <typename V>
    ::std::unordered_map<RcString, V> deserialiseIstrumap() {
        TRACE_FUNCTION_F("<" << typeid(V).name() << ">");
        size_t n = in.readCount();
        ::std::unordered_map<RcString, V> rv;
        for (size_t i = 0; i < n; i++) {
            auto s = in.readIstring();
            DEBUG("- " << s);
            rv.insert(::std::make_pair(mv$(s), D<V>::des(*this)));
        }
        return rv;
    }

    template <typename V>
    ::std::unordered_multimap<RcString, V> deserialiseIstrummap() {
        TRACE_FUNCTION_F("<" << typeid(V).name() << ">");
        size_t n = in.readCount();
        ::std::unordered_multimap<RcString, V> rv;
        for (size_t i = 0; i < n; i++) {
            auto s = in.readIstring();
            DEBUG("- " << s);
            rv.insert(::std::make_pair(mv$(s), D<V>::des(*this)));
        }
        return rv;
    }

    template <typename V>
    ::std::map<HIRSimplePath, V> deserialisePathmap() {
        TRACE_FUNCTION_F("<" << typeid(V).name() << ">");
        size_t n = in.readCount();
        ::std::map<HIRSimplePath, V> rv;
        for (size_t i = 0; i < n; i++) {
            auto s = deserialiseSimplepath();
            rv.insert(::std::make_pair(mv$(s), D<V>::des(*this)));
        }
        return rv;
    }

    template <typename T>
    ::std::vector<T> deserialiseVecC(::std::function<T()> cb) {
        TRACE_FUNCTION_FR("<" << typeid(T).name() << ">", in.getPos());
        auto _ = in.openObject(typeid(::std::vector<T>).name());
        size_t n = in.readCount();
        DEBUG("n = " << n);
        ::std::vector<T> rv;
        rv.reserve(n);
        for (size_t i = 0; i < n; i++) {
            rv.push_back(cb());
        }
        return rv;
    }

    template <typename T>
    ::std::vector<T> deserialiseVec() {
        return deserialiseVecC<T>([&]() {
            return D<T>::des(*this);
        });
    }

    template <typename T>
    ThinVector<T> deserialiseThinvecC(::std::function<T()> cb) {
        TRACE_FUNCTION_FR("<" << typeid(T).name() << ">", in.getPos());
        auto _ = in.openObject(typeid(ThinVector<T>).name());
        size_t n = in.readCount();
        DEBUG("n = " << n);
        ThinVector<T> rv;
        rv.reserveInit(n);
        for (size_t i = 0; i < n; i++) {
            rv.push_back(cb());
        }
        return rv;
    }

    template <typename T>
    ThinVector<T> deserialiseThinvec() {
        return deserialiseThinvecC<T>([&]() {
            return D<T>::des(*this);
        });
    }

    template <typename T>
    ::std::set<T> deserialiseSet() {
        TRACE_FUNCTION_FR("<" << typeid(T).name() << ">", in.getPos());
        auto _ = in.openObject(typeid(::std::set<T>).name());
        size_t n = in.readCount();
        DEBUG("n = " << n);
        ::std::set<T> rv;
        for (size_t i = 0; i < n; i++) {
            rv.insert(D<T>::des(*this));
        }
        return rv;
    }

    HIRPublicity deserialisePub() {
        return (in.readBool() ? HIRPublicity::newGlobal() : gVisPrivate);
    }

    template <typename T>
    HIRVisEnt<T> deserialiseVisent() {
        return HIRVisEnt<T>{deserialisePub(), D<T>::des(*this)};
    }

    template <typename T>
    ::std::unique_ptr<T> deserialisePtr() {
        return box$(D<T>::des(*this));
    }

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

    HIRProcMacro deserialiseProcmacro() {
        HIRProcMacro pm;
        TRACE_FUNCTION_FR("", "ProcMacro { " << pm.name << ", " << pm.path << ", [" << pm.attributes << "]}");
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
        DEBUG("pm = ProcMacro { " << pm.name << ", " << pm.path << ", [" << pm.attributes << "]}");
        return pm;
    }

    HIRTypeImpl deserialiseTypeimpl() {
        HIRTypeImpl rv;
        TRACE_FUNCTION_FR("", "impl" << rv.mParams.fmtArgs() << " " << rv.mType);

        rv.mParams = deserialiseGenericparams();
        rv.mType = deserialiseType();

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

    HIRTraitImpl deserialiseTraitimpl() {
        HIRTraitImpl rv;
        TRACE_FUNCTION_FR("", "impl" << rv.mParams.fmtArgs() << " ?" << rv.traitArgs << " for " << rv.mType);

        rv.mParams = deserialiseGenericparams();
        rv.traitArgs = deserialisePathparams();
        rv.mType = deserialiseType();
        rv.isConst = in.readBool();
        DEBUG("impl" << rv.mParams.fmtArgs() << " ?" << rv.traitArgs << " for " << rv.mType);

        size_t methodCount = in.readCount();
        for (size_t i = 0; i < methodCount; i++) {
            auto name = in.readIstring();
            auto isSpec = in.readBool();
            DEBUG((isSpec ? "default " : "") << "fn " << name);
            rv.methods.insert(::std::make_pair(mv$(name), HIRTraitImpl::ImplEnt<HIRFunction>{isSpec, deserialiseFunction()}));
        }
        size_t constCount = in.readCount();
        for (size_t i = 0; i < constCount; i++) {
            auto name = in.readIstring();
            auto isSpec = in.readBool();
            DEBUG((isSpec ? "default " : "") << "const " << name);
            rv.constants.insert(::std::make_pair(mv$(name), HIRTraitImpl::ImplEnt<HIRConstant>{isSpec, deserialiseConstant()}));
        }
        size_t staticCount = in.readCount();
        for (size_t i = 0; i < staticCount; i++) {
            auto name = in.readIstring();
            auto isSpec = in.readBool();
            DEBUG((isSpec ? "default " : "") << "static " << name);
            rv.statics.insert(::std::make_pair(mv$(name), HIRTraitImpl::ImplEnt<HIRStatic>{isSpec, deserialiseStatic()}));
        }
        size_t typeCount = in.readCount();
        for (size_t i = 0; i < typeCount; i++) {
            auto name = in.readIstring();
            auto isSpec = in.readBool();
            DEBUG((isSpec ? "default " : "") << "type " << name);
            rv.types.insert(::std::make_pair(mv$(name), HIRTraitImpl::ImplEnt<HIRTypeRef>{isSpec, deserialiseType()}));
        }

        // m_src_module doesn't matter after typeck
        return rv;
    }

    HIRMarkerImpl deserialiseMarkerimpl() {
        auto generics = deserialiseGenericparams();
        auto params = deserialisePathparams();
        auto isNeg = in.readBool();
        auto ty = deserialiseType();
        return HIRMarkerImpl{mv$(generics), mv$(params), isNeg, mv$(ty)};
    }

    Ident::Hygiene deserialiseHygine() {
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
            rv.setModPath(mPool, mv$(mp));
        }
        return rv;
    }

    ::MacroRulesPtr deserialiseMacrorulesptr() {
        return ::MacroRulesPtr(new MacroRules(deserialiseMacrorules()));
    }

    ::MacroRules deserialiseMacrorules() {
        auto crateName = in.readIstring();
        auto edition = static_cast<ASTEdition>(in.readTag());
        ::MacroRules rv(crateName, edition);
        // NOTE: This is set after loading.
        rv.isMacroItem = in.readBool();
        rv.rules = deserialiseVecC<::MacroRulesArm>([&]() {
            return deserialiseMacrorulesarm();
        });
        rv.mHygiene = deserialiseHygine();
        return rv;
    }

    ::SimplePatIfCheck deserialiseSimplepatifcheck() {
        return ::SimplePatIfCheck{static_cast<::MacroPatEnt::Type>(in.readTag()), deserialiseToken()};
    }

    ::SimplePatEnt deserialiseSimplepatent() {
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

    ::MacroPatEnt deserialiseMacropatent() {
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

    ::MacroRulesArm deserialiseMacrorulesarm() {
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

    ::MacroExpansionEnt deserialiseMacroexpansionent() {
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

    ::MacroExpansionConcatEnt deserialiseMacroexpansionconcatent() {
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

    ::Token deserialiseToken() {
        auto ty = static_cast<enum eTokenType>(in.readTag());
        auto d = deserialiseTokendata();
        return ::Token(ty, ::std::move(d), {});
    }

    ::Token::Data deserialiseTokendata() {
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

    HIRConstGenericUnevaluated deserialiseConstgenericUnevaluated();
    HIRConstGeneric deserialiseConstgeneric();
    EncodedLiteral deserialiseEncodedliteral();

    HIRExprPtr deserialiseExprptr() {
        HIRExprPtr rv;
        auto _ = in.openObject("HIR::ExprPtr");
        if (in.readBool()) {
            rv.mir = deserialiseMir();
        }
        rv.erasedTypes = deserialiseVec<HIRTypeRef>();
        return rv;
    }

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

    MIRParam deserialiseMirParam() {
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

    MIRLValue deserialiseMirLvalue() {
        MIRLValue rv;
        TRACE_FUNCTION_FR("", rv);
        rv = deserialise_mir_lvalue_();
        return rv;
    }

    MIRLValue::Wrapper deserialiseMirLvalueWrapper() {
        return MIRLValue::Wrapper::fromInner(in.readCount());
    }

    MIRLValue deserialise_mir_lvalue_() {
        auto rootV = in.readCount();
        auto root = (rootV == 3 ? MIRLValue::Storage::newStatic(deserialisePath()) : MIRLValue::Storage::fromInner(rootV));
        return MIRLValue(mv$(root), deserialiseVec<MIRLValue::Wrapper>());
    }

    MIRRValue deserialiseMirRvalue() {
        TRACE_FUNCTION;

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

    MIRConstant deserialiseMirConstant() {
        TRACE_FUNCTION;

        switch (auto tag = in.readTag()) {
#define _(x, ...)              \
    case MIRConstant::TAG_##x: \
        DEBUG("- " #x);        \
        return MIRConstant::make_##x(__VA_ARGS__);
            _(Int, {in.readI128(), static_cast<HIRCoreType>(in.readTag())})
            _(Uint, {in.readU128(), static_cast<HIRCoreType>(in.readTag())})
            _(Float, {in.readFloatValue(), static_cast<HIRCoreType>(in.readTag())})
            _(Bool, {in.readBool()})
            case MIRConstant::TAG_Bytes: {
                ::std::vector<unsigned char> bytes;
                bytes.resize(in.readCount());
                in.read(bytes.data(), bytes.size());
                return MIRConstant::make_Bytes(mv$(bytes));
            }
                _(StaticString, in.readString())
                _(Const, {box$(deserialisePath())})
                _(Generic, deserialiseGenericref())
                _(Function, {box$(deserialisePath())})
                _(ItemAddr, {box$(deserialisePath()), in.readU128()})
#undef _
            default:
                BUG(Span(), "Bad tag for MIR::Const - " << tag);
        }
    }

    HIRExternType deserialiseExterntype() {
        return HIRExternType{deserialiseMarkings()};
    }

    HIRTraitAlias deserialiseTraitalias() {
        return HIRTraitAlias{deserialiseGenericparams(), deserialiseVec<HIRTraitPath>()};
    }

    HIRTypeItem deserialiseTypeitem() {
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

    HIRValueItem deserialiseValueitem() {
        switch (auto tag = in.readTag()) {
            case 0: {
                auto spath = deserialiseSimplepath();
                auto isVariant = in.readBool();
                return HIRValueItem::make_Import({mv$(spath), isVariant, static_cast<unsigned int>(in.readCount())});
            }
            case 1:
                return HIRValueItem(deserialiseConstant());
            case 2:
                return HIRValueItem(deserialiseStatic());
            case 3:
                return HIRValueItem::make_StructConstant({deserialiseSimplepath()});
            case 4:
                return HIRValueItem(deserialiseFunction());
            case 5:
                return HIRValueItem::make_StructConstructor({deserialiseSimplepath()});
            default:
                BUG(Span(), "Bad tag for HIR::ValueItem - " << tag);
        }
    }

    HIRMacroItem deserialiseMacroitem() {
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

    HIRLinkage deserialiseLinkage() {
        HIRLinkage l;
        l.type = HIRLinkage::Type::Auto;
        l.name = in.readString();
        return l;
    }

    // - Value items
    HIRFunction deserialiseFunction() {
        TRACE_FUNCTION;
        auto _ = in.openObject("HIR::Function");

        HIRFunction rv;
        rv.saveCode = false;
        rv.linkage = deserialiseLinkage();
        rv.receiver = static_cast<HIRFunction::Receiver>(in.readTag());
        auto receiverType = deserialiseType();
        if (rv.receiver == HIRFunction::Receiver::Custom) {
            rv.receiverType = receiverType;
        }
        rv.mAbi = in.readIstring();
        rv.unsafe = in.readBool();
        rv.isConst = in.readBool();
        rv.markings = deserialiseFunctionMarkings();
        rv.mParams = deserialiseGenericparams();
        rv.mArgs = deserialiseFcnargs();
        rv.variadic = in.readBool();
        rv.returnType = deserialiseType();
        rv.mCode = deserialiseExprptr();
        return rv;
    }

    HIRFunction::Markings deserialiseFunctionMarkings() {
        auto _ = in.openObject("HIR::Function::Markings");
        HIRFunction::Markings rv;
        rv.rustcLegacyConstGenerics = deserialiseVec<unsigned>();
        rv.trackCaller = in.readBool();
        rv.isRustcIntrinsic = in.readBool();
        rv.isRustcPromotable = in.readBool();
        return rv;
    }

    ::std::vector<::std::pair<HIRPattern, HIRTypeRef>> deserialiseFcnargs() {
        size_t n = in.readCount();
        ::std::vector<::std::pair<HIRPattern, HIRTypeRef>> rv;
        rv.reserve(n);
        for (size_t i = 0; i < n; i++) {
            rv.push_back(::std::make_pair(HIRPattern{}, deserialiseType()));
        }
        DEBUG("rv = " << rv);
        return rv;
    }

    HIRConstant deserialiseConstant() {
        TRACE_FUNCTION;

        HIRConstant rv;
        rv.mParams = deserialiseGenericparams();
        rv.mType = deserialiseType();
        rv.mValue = deserialiseExprptr();
        if (in.readBool()) {
            rv.valueRes = deserialiseEncodedliteral();
            rv.valueState = HIRConstant::ValueState::Known;
        } else {
            rv.valueState = HIRConstant::ValueState::Generic;
        }
        return rv;
    }

    HIRStatic deserialiseStatic() {
        TRACE_FUNCTION;

        auto linkage = deserialiseLinkage();
        auto params = deserialiseGenericparams();
        uint8_t bitflag1 = in.readU8();
#define BIT(i, fld) fld = (bitflag1 & (1 << (i))) != 0;
        bool isMut;
        bool saveLiteral;
        bool hasExplicitAlignment;
        BIT(0, isMut);
        BIT(1, saveLiteral);
        BIT(2, hasExplicitAlignment);
#undef BIT
        auto explicitAlignment = hasExplicitAlignment ? in.readCount() : 0;
        auto ty = deserialiseType();
        auto rv = HIRStatic(mv$(linkage), isMut, mv$(ty), {});
        rv.explicitAlignment = explicitAlignment;
        if (params.isGeneric()) {
            rv.mValue = deserialiseExprptr();
        }
        rv.mParams = ::std::move(params);
        if (saveLiteral) {
            rv.valueRes = deserialiseEncodedliteral();
            rv.valueGenerated = true;
            rv.noEmitValue = true;
        }
        return rv;
    }

    // - Type items
    HIRTypeAlias deserialiseTypealias() {
        return HIRTypeAlias{deserialiseGenericparams(), deserialiseType()};
    }

    HIRTraitMarkings deserialiseMarkings() {
        HIRTraitMarkings m;
        uint8_t bitflag1 = in.readU8();
#define BIT(i, fld) fld = (bitflag1 & (1 << (i))) != 0;
        BIT(0, m.hasADeref)
        BIT(1, m.isCopy)
        BIT(2, m.hasDropImpl)
#undef BIT
        // TODO: auto_impls
        return m;
    }

    HIRStructMarkings deserialiseStrMarkings() {
        HIRStructMarkings m;
        uint8_t bitflag1 = in.readU8();
#define BIT(i, fld) fld = (bitflag1 & (1 << (i))) != 0;
        BIT(0, m.canUnsize)
        BIT(1, m.isNonzero)
        BIT(2, m.boundedMax)
        BIT(3, m.isFundamental)
        BIT(4, m.isNoNiche)
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

    HIREnum deserialiseEnum();
    HIREnum::DataVariant deserialiseEnumdatavariant();
    HIREnum::ValueVariant deserialiseEnumvaluevariant();

    HIRStruct deserialiseStruct();
    HIRStructField deserialiseStructField();
    HIRUnion deserialiseUnion();
    HIRTrait deserialiseTrait();

    HIRTraitValueItem deserialiseTraitvalueitem() {
        switch (auto tag = in.readTag()) {
#define _(x, ...)                                        \
    case HIRTraitValueItem::TAG_##x:                     \
        DEBUG("- " #x);                                  \
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

    HIRAssociatedType deserialiseAssociatedtype() {
        return HIRAssociatedType{deserialiseGenericparams(), in.readBool(), deserialiseVec<HIRTraitPath>(), deserialiseType()};
    }
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
DEF_D(uint8_t, return d.readU8(););

template <typename T>
DEF_D(::std::unique_ptr<T>, return d.deserialisePtr<T>();)

template <typename T>
DEF_D(::std::vector<T>, return d.deserialiseVec<T>();)
template <typename T, typename U>
struct D<::std::pair<T, U>> {
    static ::std::pair<T, U> des(HirDeserialiser& d) {
        auto a = D<T>::des(d);
        return ::std::make_pair(mv$(a), D<U>::des(d));
    }
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
        DEBUG("- " #x);         \
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
    TRACE_FUNCTION_FR("", rv);

    auto idx = in.readCount();
    if (idx != ~0u) {
        DEBUG("#" << idx << "");
        rv = types.at(idx);
        return rv;
    } else {
        DEBUG("Fresh (=" << types.size() << ")");
    }
    auto _ = in.openObject("HIR::TypeData");

    switch (auto tag = in.readTag()) {
#define _(x, ...)                                                     \
    case HIRTypeData::TAG_##x:                                        \
        DEBUG("- " #x);                                               \
        rv = typeInterner.intern(HIRTypeData::make_##x(__VA_ARGS__)); \
        break;
        _(Infer, {~0u, HIRInferClass::None})
        _(Diverge, {})
        _(Primitive, static_cast<HIRCoreType>(in.readTag()))
        _(Path, {deserialisePath(), {}})
        _(Generic, deserialiseGenericref())
        _(TraitObject, {deserialiseTraitpath(), deserialiseVec<HIRGenericPath>()})
        case HIRTypeData::TAG_ErasedType:
            TODO(Span(), "ErasedType");
            _(Array, {deserialiseType(), deserialiseArraysize()})
            _(Slice, {deserialiseType()})
            _(Tuple, deserialiseVec<HIRTypeRef>())
            _(Borrow, {static_cast<HIRBorrowType>(in.readTag()), deserialiseType()})
            _(Pointer, {static_cast<HIRBorrowType>(in.readTag()), deserialiseType()})
            _(NamedFunction, {deserialisePath()})
            _(Function, {in.readBool(), in.readBool(), in.readIstring(), deserialiseType(), deserialiseVec<HIRTypeRef>()})
#undef _
        default:
            BUG(Span(), "Bad tag for HIR::ASTType* - " << tag);
    }
    types.push_back(rv);
    return rv;
}

HIRSimplePath HirDeserialiser::deserialiseSimplepath() {
    TRACE_FUNCTION;
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
    TRACE_FUNCTION_FR("", rv);
    rv.types = deserialiseThinvec<HIRTypeRef>();
    rv.values = deserialiseThinvec<HIRConstGeneric>();
    return rv;
}

HIRGenericPath HirDeserialiser::deserialiseGenericpath() {
    HIRGenericPath rv;
    TRACE_FUNCTION_FR("", rv);
    rv.mPath = deserialiseSimplepath();
    rv.mParams = deserialisePathparams();
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
    TRACE_FUNCTION;
    switch (auto tag = in.readTag()) {
        case 0:
            DEBUG("Generic");
            return HIRPath(deserialiseGenericpath());
        case 1:
            DEBUG("Inherent");
            return HIRPath(HIRPath::Data::Data_UfcsInherent{deserialiseType(), in.readIstring(), deserialisePathparams(), deserialisePathparams()});
        case 2: {
            DEBUG("Known");
            return HIRPath(HIRPath::Data::Data_UfcsKnown{deserialiseType(), deserialiseGenericpath(), in.readIstring(), deserialisePathparams()});
        }
        default:
            BUG(Span(), "Bad tag for HIR::Path - " << tag);
    }
}

HIRGenericParams HirDeserialiser::deserialiseGenericparams() {
    TRACE_FUNCTION;
    HIRGenericParams params;
    params.types = deserialiseVec<HIRTypeParamDef>();
    params.values = deserialiseVec<HIRValueParamDef>();
    params.bounds = deserialiseVec<HIRGenericBound>();
    DEBUG("params = " << params.fmtArgs() << ", " << params.fmtBounds());
    return params;
}

HIRTypeParamDef HirDeserialiser::deserialiseTyparamdef() {
    auto rv = HIRTypeParamDef{in.readIstring(), deserialiseType(), in.readBool()};
    DEBUG("::HIR::TypeParamDef { " << rv.mName << ", " << rv.defaultValue << ", " << rv.isSized << "}");
    return rv;
}

HIRValueParamDef HirDeserialiser::deserialiseValueparamdef() {
    auto rv = HIRValueParamDef{in.readIstring(), deserialiseType()};
    rv.defaultValue = deserialiseConstgeneric();
    DEBUG("::HIR::ValueParamDef { " << rv.mName << ": " << rv.mType << " = " << rv.defaultValue << "}");
    return rv;
}

HIRGenericBound HirDeserialiser::deserialiseGenericbound() {
    switch (auto tag = in.readTag()) {
        case 2: {
            auto type = deserialiseType();
            auto trait = deserialiseTraitpath();
            auto constness = static_cast<HIRBoundConstness>(in.readU8());
            return HIRGenericBound::make_TraitBound({mv$(type), mv$(trait), constness});
        }
        case 3:
            return HIRGenericBound::make_TypeEquality({deserialiseType(), deserialiseType()});
        default:
            BUG(Span(), "Bad tag for HIR::GenericBound - " << tag);
    }
}

HIREnum HirDeserialiser::deserialiseEnum() {
    TRACE_FUNCTION;
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

    return HIREnum{deserialiseGenericparams(), in.readBool(), static_cast<HIREnum::Repr>(in.readTag()), H::deserialiseEnumclass(*this), true, deserialiseMarkings()};
}

HIREnum::DataVariant HirDeserialiser::deserialiseEnumdatavariant() {
    auto name = in.readIstring();
    DEBUG("Enum::DataVariant " << name);
    return HIREnum::DataVariant{mv$(name), in.readBool(), deserialiseType(), HIRExprPtr{}, U128(in.readU64())};
}

HIREnum::ValueVariant HirDeserialiser::deserialiseEnumvaluevariant() {
    auto name = in.readIstring();
    DEBUG("Enum::ValueVariant " << name);
    return HIREnum::ValueVariant{mv$(name), HIRExprPtr{}, U128(in.readU64())};
}

HIRUnion HirDeserialiser::deserialiseUnion() {
    TRACE_FUNCTION;
    auto params = deserialiseGenericparams();
    auto repr = static_cast<HIRUnion::Repr>(in.readTag());
    auto variants = deserialiseVec<HIRStructField>();
    auto markings = deserialiseMarkings();

    return HIRUnion{mv$(params), repr, mv$(variants), mv$(markings)};
}

HIRStruct HirDeserialiser::deserialiseStruct() {
    TRACE_FUNCTION_FR("", in.getPos());
    auto _ = in.openObject("HIR::Struct");
    auto params = deserialiseGenericparams();
    DEBUG("params = " << params.fmtArgs() << params.fmtBounds());
    auto repr = static_cast<HIRStruct::Repr>(in.readTag());

    HIRStruct::Data data;
    switch (auto tag = in.readTag()) {
        case HIRStruct::Data::TAG_Unit:
            DEBUG("Unit");
            data = HIRStruct::Data::make_Unit({});
            break;
        case HIRStruct::Data::TAG_Tuple:
            DEBUG("Tuple");
            data = HIRStruct::Data(deserialiseVec<HIRVisEnt<HIRTypeRef>>());
            break;
        case HIRStruct::Data::TAG_Named:
            DEBUG("Named");
            data = HIRStruct::Data(deserialiseVec<HIRStructField>());
            break;
        default:
            BUG(Span(), "Bad tag for HIR::Struct::Data - " << tag);
    }
    unsigned forcedAlignment = in.readCount();
    unsigned maxFieldAlignment = in.readCount();
    DEBUG("align = " << forcedAlignment);
    auto markings = deserialiseMarkings();
    auto strMarkings = deserialiseStrMarkings();

    auto rv = HIRStruct{mv$(params), repr, mv$(data), forcedAlignment, mv$(markings), mv$(strMarkings)};
    rv.maxFieldAlignment = maxFieldAlignment;
    return rv;
}

HIRStructField HirDeserialiser::deserialiseStructField() {
    return HIRStructField{in.readIstring(), deserialisePub(), deserialiseType(), in.readBool() ? ::std::make_unique<HIRGenericPath>(deserialiseGenericpath()) : nullptr};
}

HIRTrait HirDeserialiser::deserialiseTrait() {
    TRACE_FUNCTION;
    auto _ = in.openObject("HIR::Trait");

    HIRTrait rv{deserialiseGenericparams(), {}};
    const auto traitFlags = in.readU8();
    rv.mIsMarker = traitFlags & 1;
    rv.isFundamental = traitFlags & 2;
    rv.isCoinductive = (traitFlags & 4) || rv.mIsMarker;
    rv.isConst = traitFlags & 8;
    rv.skipArrayDuringMethodDispatch = traitFlags & 16;
    rv.skipBoxedSliceDuringMethodDispatch = traitFlags & 32;
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
    auto pI = deserialisePathparams();
    auto pM = deserialisePathparams();
    auto rv = HIRConstGenericUnevaluated(deserialiseExprptr());
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
        _(Evaluated, HIREncodedLiteralPtr(deserialiseEncodedliteral()))
#undef _
        default:
            BUG(Span(), "Unknown HIR::ConstGeneric tag when deserialising - " << tag);
    }
}

EncodedLiteral HirDeserialiser::deserialiseEncodedliteral() {
    EncodedLiteral rv;
    rv.bytes = deserialiseVec<uint8_t>();

    auto nreloc = in.readCount();
    rv.relocations.reserve(nreloc);
    for (size_t i = 0; i < nreloc; i++) {
        auto ofs = in.readCount();
        auto len = in.readCount();
        switch (in.readTag()) {
            case 0:
                rv.relocations.push_back(Reloc::newNamed(ofs, len, deserialisePath()));
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
    TRACE_FUNCTION;

    MIRFunction rv;

    rv.locals = deserialiseVec<HIRTypeRef>();
    rv.dropFlags = deserialiseVec<bool>();
    rv.blocks = deserialiseVec<MIRBasicBlock>();

    return MIRFunctionPointer(new MIRFunction(mv$(rv)));
}

MIRBasicBlock HirDeserialiser::deserialiseMirBasicblock() {
    TRACE_FUNCTION;

    auto statements = deserialiseVec<MIRStatement>();
    auto terminator = deserialiseMirTerminator();
    const auto isCleanup = in.readBool();
    return MIRBasicBlock{mv$(statements), mv$(terminator), isCleanup};
}

AsmOptions HirDeserialiser::deserialiseAsmOptions() {
    AsmOptions o;
    const uint16_t bitflag1 = in.readU16();
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
        default:
            BUG(Span(), "Bad tag for MIR::AsmParam - " << tag);
    }
}

MIRStatement HirDeserialiser::deserialiseMirStatement() {
    MIRStatement rv;
    TRACE_FUNCTION_FR("", rv);
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
    TRACE_FUNCTION_FR("", rv);
    rv = this->deserialise_mir_terminator_();
    return rv;
}

MIRTerminator HirDeserialiser::deserialise_mir_terminator_() {
    switch (auto tag = in.readTag()) {
#define _(x, ...)                \
    case MIRTerminator::TAG_##x: \
        return MIRTerminator::make_##x(__VA_ARGS__);
        case MIRTerminator::TAGDEAD:
            BUG(Span(), "MIR::Terminator::TAGDEAD found");
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
            _(Call, {static_cast<unsigned int>(in.readCount()), deserialiseMirUnwindAction(), deserialiseMirLvalue(), deserialiseMirCalltarget(), deserialiseVec<MIRParam>()})
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
    TRACE_FUNCTION;
    switch (auto tag = in.readTag()) {
#define _(x, ...)                  \
    case MIRSwitchValues::TAG_##x: \
        return MIRSwitchValues::make_##x(__VA_ARGS__);
        _(Unsigned, deserialiseVecC<uint64_t>([&]() {
            return in.readU64c();
        }))
        _(Signed, deserialiseVecC<int64_t>([&]() {
            return in.readI64c();
        }))
        _(String, deserialiseVec<::std::string>())
        _(ByteString, deserialiseVec<::std::vector<uint8_t>>())
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
    TRACE_FUNCTION;
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
    gVisPrivate = HIRPublicity::newPriv(HIRSimplePath(this->crateName));
    rv.crateName = this->crateName;
    rv.edition = static_cast<ASTEdition>(in.readTag());
    rv.mRootModule = deserialiseModule();

    rv.typeImpls = D<HIRCrate::ImplGroup<std::unique_ptr<HIRTypeImpl>>>::des(*this);
    rv.traitImpls = deserialisePathmap<HIRCrate::ImplGroup<std::unique_ptr<HIRTraitImpl>>>();
    rv.markerImpls = deserialisePathmap<HIRCrate::ImplGroup<std::unique_ptr<HIRMarkerImpl>>>();

    rv.exportedMacroNames = deserialiseVec<::RcString>();
    rv.mLangItems = deserialiseStrumap<HIRSimplePath>();

    {
        size_t n = in.readCount();
        for (size_t i = 0; i < n; i++) {
            auto extCrateName = in.readIstring();
            auto extCrateFile = in.readString();
            auto extCrate = HIRExternCrate{};
            extCrate.basename = extCrateFile;
            extCrate.mPath = extCrateFile;
            rv.extCrates.insert(::std::make_pair(mv$(extCrateName), mv$(extCrate)));
        }
    }

    rv.extLibs = deserialiseVec<HIRExternLibrary>();
    rv.linkPaths = deserialiseVec<::std::string>();
}

//}

HIRCrate* HIRDeserialise(stl::ObjPool* pool, HIRTypeInterner& types, const ::std::string& filename) {
    try {
        HIRSerialiseReader in{filename + ".hir"}; // Callers pass the metadata basename, without its suffix.
        HirDeserialiser s{*pool, in, types};

        auto* rv = pool->make<HIRCrate>(pool, types);
        s.deserialiseCrate(*rv);
        return rv;
    } catch (int) {
        ::std::abort();
    } catch (const ::std::runtime_error& e) {
        ::std::cerr << "Unable to deserialise crate metadata from " << filename << ": " << e.what() << ::std::endl;
        ::std::abort();
    }
}

RcString HIRDeserialiseJustName(const ::std::string& filename) {
    try {
        HIRSerialiseReader in{filename + ".hir"}; // Callers pass the metadata basename, without its suffix.

        // NOTE: This is the first item loaded by deserialise_crate
        auto crateName = in.readIstring();
        assert(crateName != "" && "Empty crate name loaded from metadata");
        return crateName;
    } catch (int) {
        ::std::abort();
    } catch (const ::std::runtime_error& e) {
        ::std::cerr << "Unable to deserialise crate metadata from " << filename << ": " << e.what() << ::std::endl;
        ::std::abort();
    }
}

#undef DEBUG_EXTRA_ENABLE
#define DEBUG_EXTRA_ENABLE
#undef DEF_D

#define NODE_IS(valptr, tysuf) (cast<const HIRExprNode##tysuf>(&*valptr) != nullptr)

namespace {

    class TreeVisitor: public HIRVisitor, public HIRExprVisitor {
        ::std::ostream& os;
        unsigned int indentLevel;

    public:
        TreeVisitor(HIRTypeInterner& types, ::std::ostream& os)
            : HIRVisitor(nullptr, types)
            , os(os)
            , indentLevel(0)
        {
        }

        void visitModule(HIRItemPath p, HIRModule& mod) override {
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

        void visitTypeImpl(HIRTypeImpl& impl) override {
            os << indent() << "impl" << impl.mParams.fmtArgs() << " " << impl.mType << "\n";
            if (!impl.mParams.bounds.empty()) {
                os << indent() << " " << impl.mParams.fmtBounds() << "\n";
            }
            os << indent() << "{\n";
            incIndent();
            HIRVisitor::visitTypeImpl(impl);
            decIndent();
            os << indent() << "}\n";
        }

        virtual void visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) override {
            os << indent() << "impl" << impl.mParams.fmtArgs() << " " << traitPath << impl.traitArgs << " for " << impl.mType << "\n";
            if (!impl.mParams.bounds.empty()) {
                os << indent() << " " << impl.mParams.fmtBounds() << "\n";
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

        void visitMarkerImpl(const HIRSimplePath& traitPath, HIRMarkerImpl& impl) override {
            os << indent() << "impl" << impl.mParams.fmtArgs() << " " << (impl.isPositive ? "" : "!") << traitPath << impl.traitArgs << " for " << impl.mType << "\n";
            if (!impl.mParams.bounds.empty()) {
                os << indent() << " " << impl.mParams.fmtBounds() << "\n";
            }
            os << indent() << "{ }\n";
        }

        // - Type Items
        void visitTypeAlias(HIRItemPath p, HIRTypeAlias& item) override {
            os << indent() << "type " << p.getName() << item.mParams.fmtArgs() << " = " << item.mType << item.mParams.fmtBounds() << "\n";
        }

        void visitInherentType(HIRItemPath p, HIRTypeAlias& item) override {
            this->visitTypeAlias(p, item);
        }

        void visitTrait(HIRItemPath p, HIRTrait& item) override {
            os << indent() << "trait " << p.getName() << item.mParams.fmtArgs() << "\n";
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
            if (!item.mParams.bounds.empty()) {
                os << indent() << " " << item.mParams.fmtBounds() << "\n";
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

        void visitStruct(HIRItemPath p, HIRStruct& item) override {
            os << indent() << "struct " << p.getName() << item.mParams.fmtArgs();
            TU_MATCH_HDRA( (item.mData), {)
            TU_ARMA(Unit, flds) {
                    if (item.mParams.bounds.empty()) {
                        os << ";\n";
                    } else {
                        os << "\n";
                        os << indent() << " " << item.mParams.fmtBounds() << "\n";
                        os << indent() << "    ;\n";
                    }
                }
                TU_ARMA(Tuple, flds) {
                    os << "(";
                    for (const auto& fld : flds) {
                        os << fld.publicity << " " << fld.ent << ", ";
                    }
                    if (item.mParams.bounds.empty()) {
                        os << ");\n";
                    } else {
                        os << ")\n";
                        os << indent() << " " << item.mParams.fmtBounds() << "\n";
                        os << indent() << "    ;\n";
                    }
                }
                TU_ARMA(Named, flds) {
                    os << "\n";
                    if (!item.mParams.bounds.empty()) {
                        os << indent() << " " << item.mParams.fmtBounds() << "\n";
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
                }
            }
        }

        void visitEnum(HIRItemPath p, HIREnum& item) override {
            os << indent() << "enum " << p.getName() << item.mParams.fmtArgs() << "\n";
            if (!item.mParams.bounds.empty()) {
                os << indent() << " " << item.mParams.fmtBounds() << "\n";
            }
            os << indent() << "{\n";
            incIndent();
            if (const auto* e = item.mData.opt_Value()) {
                for (const auto& var : e->variants) {
                    os << indent() << var.name;
                    os << ",\n";
                }
            } else {
                for (const auto& var : item.mData.as_Data()) {
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

        // - Value Items
        void visitFunction(HIRItemPath p, HIRFunction& item) override {
            os << indent();
            if (item.isConst) {
                os << "const ";
            }
            if (item.unsafe) {
                os << "unsafe ";
            }
            if (item.mAbi != ABI_RUST) {
                os << "extern \"" << item.mAbi << "\" ";
            }
            os << "fn " << p.getName() << item.mParams.fmtArgs() << "(";
            for (const auto& arg : item.mArgs) {
                os << arg.first << ": " << arg.second << ", ";
            }
            os << ") -> " << item.returnType << "\n";
            if (!item.mParams.bounds.empty()) {
                os << indent() << " " << item.mParams.fmtBounds() << "\n";
            }

            if (item.mCode) {
                os << indent();
                if (cast<HIRExprNodeBlock>(&*item.mCode)) {
                    item.mCode->visit(*this);
                } else {
                    os << "{\n";
                    incIndent();
                    os << indent();

                    item.mCode->visit(*this);

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

        void visitStatic(HIRItemPath p, HIRStatic& item) override {
            if (item.linkage.name != "") {
                os << indent() << "#[link_name=\"" << item.linkage.name << "\"]\n";
            }
            if (item.mValue) {
                os << indent() << "static " << p.getName() << item.mParams.fmtArgs() << ": " << item.mType << " = " << item.valueRes;
            } else if (item.valueGenerated) {
                os << indent() << "static " << p.getName() << item.mParams.fmtArgs() << ": " << item.mType << " = /*magic*/ " << item.valueRes;
            } else {
                os << indent() << "extern static " << p.getName() << ": " << item.mType;
            }
            if (!item.mParams.bounds.empty()) {
                os << indent() << " " << item.mParams.fmtBounds() << "\n";
            }
            os << ";\n";
        }

        void visitConstant(HIRItemPath p, HIRConstant& item) override {
            os << indent() << "const " << p.getName() << ": " << item.mType << " = " << item.valueRes;
            if (item.mValue /*&& item.m_value_state != HIR::Constant::ValueState::Known*/) {
                os << " /*= ";
                item.mValue->visit(*this);
                os << "*/";
            }
            os << ";\n";
        }

        // - Misc

        bool nodeIsLeaf(const HIRExprNode& node) {
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

        void visitNodePtr(HIRExprNodeP& nodePtr) override {
            HIRExprVisitor::visitNodePtr(nodePtr);
            os << "/*: " << nodePtr->resType << " */";
        }

        void visit(HIRExprNodeBlock& node) override {
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

        void visit(HIRExprNodeConstBlock& node) override {
            os << "const ";
            node.inner->visit(*this);
        }

        void visit(HIRExprNodeAsm& node) override {
            os << "llvm_asm!(";
            os << ")";
        }

        void visit(HIRExprNodeAsm2& node) override {
            os << "asm!(";
            os << ")";
        }

        void visit(HIRExprNodeReturn& node) override {
            os << "return";
            if (node.mValue) {
                os << " ";
                this->visitNodePtr(node.mValue);
            }
        }

        void visit(HIRExprNodeYield& node) override {
            os << "yield";
            if (node.mValue) {
                os << " ";
                this->visitNodePtr(node.mValue);
            }
        }

        void visit(HIRExprNodeAWait& node) override {
            os << "(";
            this->visitNodePtr(node.mValue);
            os << ").await";
        }

        void visit(HIRExprNodeLet& node) override {
            os << "let " << node.pattern << ": " << node.mType;
            if (node.mValue) {
                os << " = ";
                this->visitNodePtr(node.mValue);
            }
            os << ";";
        }

        void visit(HIRExprNodeLoop& node) override {
            if (node.label != "") {
                os << "'" << node.label << ": ";
            }
            os << "loop ";
            this->visitNodePtr(node.mCode);
        }

        void visit(HIRExprNodeLoopControl& node) override {
            os << (node.isContinue ? "continue" : "break");
            if (node.label != "") {
                os << " '" << node.label;
            }
            if (node.mValue) {
                os << " ";
                this->visitNodePtr(node.mValue);
            }
        }

        void visit(HIRExprNodeMatch& node) override {
            os << "match ";
            this->visitNodePtr(node.mValue);
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
                this->visitNodePtr(arm.mCode);
                decIndent();
                os << ",\n";
            }
            os << indent() << "}";
        }

        void visit(HIRExprNodeAssign& node) override {
            this->visitNodePtr(node.slot);
            os << " " << HIRExprNodeAssign::opname(node.op) << "= ";
            this->visitNodePtr(node.mValue);
        }

        void visit(HIRExprNodeBinOp& node) override {
            os << "(";
            this->visitNodePtr(node.left);
            os << ")";
            os << " " << HIRExprNodeBinOp::opname(node.op) << " ";
            os << "(";
            this->visitNodePtr(node.right);
            os << ")";
        }

        void visit(HIRExprNodeUniOp& node) override {
            switch (node.op) {
                case HIRExprNodeUniOp::Op::Invert:
                    os << "!";
                    break;
                case HIRExprNodeUniOp::Op::Negate:
                    os << "-";
                    break;
            }
            os << "(";
            this->visitNodePtr(node.mValue);
            os << ")";
        }

        void visit(HIRExprNodeBorrow& node) override {
            os << "&";
            switch (node.mType) {
                case HIRBorrowType::Shared:
                    break;
                case HIRBorrowType::Unique:
                    os << "mut ";
                    break;
                case HIRBorrowType::Owned:
                    os << "move ";
                    break;
            }

            bool skipParens = this->nodeIsLeaf(*node.mValue) || NODE_IS(node.mValue, Deref);
            if (!skipParens) {
                os << "(";
            }
            this->visitNodePtr(node.mValue);
            if (!skipParens) {
                os << ")";
            }
        }

        void visit(HIRExprNodeRawBorrow& node) override {
            os << "&raw ";
            switch (node.mType) {
                case HIRBorrowType::Shared:
                    break;
                case HIRBorrowType::Unique:
                    os << "mut ";
                    break;
                case HIRBorrowType::Owned:
                    os << "move ";
                    break;
            }

            bool skipParens = this->nodeIsLeaf(*node.mValue) || NODE_IS(node.mValue, Deref);
            if (!skipParens) {
                os << "(";
            }
            this->visitNodePtr(node.mValue);
            if (!skipParens) {
                os << ")";
            }
        }

        void visit(HIRExprNodeCast& node) override {
            this->visitNodePtr(node.mValue);
            os << " as " << node.dstType;
        }

        void visit(HIRExprNodeUnsize& node) override {
            this->visitNodePtr(node.mValue);
            os << " : " << node.dstType;
        }

        void visit(HIRExprNodeIndex& node) override {
            // TODO: Avoid parens
            os << "(";
            this->visitNodePtr(node.mValue);
            os << ")";
            os << "[";
            this->visitNodePtr(node.index);
            os << "]";
        }

        void visit(HIRExprNodeDeref& node) override {
            os << "*";

            bool skipParens = this->nodeIsLeaf(*node.mValue);
            if (!skipParens) {
                os << "(";
            }
            this->visitNodePtr(node.mValue);
            if (!skipParens) {
                os << ")";
            }
        }

        void visit(HIRExprNodeEmplace& node) override {
            if (node.mType == HIRExprNodeEmplace::Type::Noop) {
                return node.mValue->visit(*this);
            }
            os << "(";
            this->visitNodePtr(node.place);
            os << " <- ";
            this->visitNodePtr(node.mValue);
            os << ")";
            os << "/*" << (node.mType == HIRExprNodeEmplace::Type::Boxer ? "box" : "place") << "*/";
        }

        void visit(HIRExprNodeTupleVariant& node) override {
            os << node.mPath;
            os << "(";
            for (/*const*/ auto& arg : node.mArgs) {
                this->visitNodePtr(arg);
                os << ", ";
            }
            os << ")";
        }

        void visit(HIRExprNodeCallPath& node) override {
            os << node.mPath;
            os << "(";
            for (/*const*/ auto& arg : node.mArgs) {
                this->visitNodePtr(arg);
                os << ", ";
            }
            os << ")";
            os << "/* : " << node.resType << " */";
        }

        void visit(HIRExprNodeCallValue& node) override {
            // TODO: Avoid brackets if not needed
            os << "(";
            this->visitNodePtr(node.mValue);
            os << ")";
            os << "(";
            for (/*const*/ auto& arg : node.mArgs) {
                this->visitNodePtr(arg);
                os << ", ";
            }
            os << ")";
        }

        void visit(HIRExprNodeCallMethod& node) override {
            // TODO: Avoid brackets if not needed
            os << "(";
            this->visitNodePtr(node.mValue);
            os << ")";
            os << "." << node.method << node.mParams << "(";
            for (/*const*/ auto& arg : node.mArgs) {
                this->visitNodePtr(arg);
                os << ", ";
            }
            os << ")";
            if (!node.cache.argTypes.empty()) {
                os << "/*CACHE:" << node.cache.argTypes << "*/";
            }
        }

        void visit(HIRExprNodeField& node) override {
            // TODO: Avoid brackets if not needed
            os << "(";
            this->visitNodePtr(node.mValue);
            os << ")";
            os << "." << node.field;
        }

        void visit(HIRExprNodeLiteral& node) override {
            TU_MATCH_HDRA( (node.mData), {)
            TU_ARMA(Integer, e) {
                    switch (e.mType) {
                        case HIRCoreType::U8:
                            os << e.mValue << "_u8";
                            break;
                        case HIRCoreType::U16:
                            os << e.mValue << "_u16";
                            break;
                        case HIRCoreType::U32:
                            os << e.mValue << "_u32";
                            break;
                        case HIRCoreType::U64:
                            os << e.mValue << "_u64";
                            break;
                        case HIRCoreType::Usize:
                            os << e.mValue << "_usize";
                            break;
                        case HIRCoreType::I8:
                            os << /*I128*/ (e.mValue) << "_i8";
                            break;
                        case HIRCoreType::I16:
                            os << /*I128*/ (e.mValue) << "_i16";
                            break;
                        case HIRCoreType::I32:
                            os << /*I128*/ (e.mValue) << "_i32";
                            break;
                        case HIRCoreType::I64:
                            os << /*I128*/ (e.mValue) << "_i64";
                            break;
                        case HIRCoreType::Isize:
                            os << /*I128*/ (e.mValue) << "_isize";
                            break;
                        case HIRCoreType::Char: {
                            auto v = e.mValue.truncateU64();
                            if (v == '\\' || v == '\'') {
                                os << "'\\" << static_cast<char>(v) << "'";
                            } else if (' ' <= v && v <= 0x7F) {
                                os << "'" << static_cast<char>(v) << "'";
                            } else {
                                os << "'\\u{" << ::std::hex << v << ::std::dec << "}'";
                            }
                        } break;
                        default:
                            os << e.mValue << "_unk";
                            break;
                    }
                }
                TU_ARMA(Float, e) {
                    switch (e.mType) {
                        case HIRCoreType::F32:
                            os << e.mValue << "_f32";
                            break;
                        case HIRCoreType::F64:
                            os << e.mValue << "_f64";
                            break;
                        default:
                            os << e.mValue << "_unk";
                            break;
                    }
                }
                TU_ARMA(Boolean, e) {
                    os << (e ? "true" : "false");
                }
                TU_ARMA(String, e) {
                    os << "\"" << FmtEscaped(e) << "\"";
                }
                TU_ARMA(CString, e) {
                    os << "c\"" << FmtEscaped(e.v) << "\"";
                }
                TU_ARMA(ByteString, e) {
                    os << "b\"";
                    for (auto b : e) {
                        if (b == '\\' || b == '\"') {
                            os << "\\" << b;
                        } else if (' ' <= b && b <= 0x7F) {
                            os << b;
                        } else {
                            char buf[3];
                            sprintf(buf, "%02x", static_cast<uint8_t>(b));
                            os << "\\x" << buf;
                        }
                    }
                    os << "\"";
                }
            }
        }

        void visit(HIRExprNodeUnitVariant& node) override {
            os << node.mPath;
        }

        void visit(HIRExprNodePathValue& node) override {
            os << node.mPath;
        }

        void visit(HIRExprNodeVariable& node) override {
            os << node.mName << "#" << node.slot;
        }

        void visit(HIRExprNodeConstParam& node) override {
            os << node.mName << "#" << node.mBinding;
        }

        void visit(HIRExprNodeStructLiteral& node) override {
            os << node.mType << " {\n";
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

        void visit(HIRExprNodeTuple& node) override {
            os << "(";
            for (/*const*/ auto& val : node.vals) {
                this->visitNodePtr(val);
                os << ", ";
            }
            os << ")";
        }

        void visit(HIRExprNodeArrayList& node) override {
            os << "[";
            for (/*const*/ auto& val : node.vals) {
                this->visitNodePtr(val);
                os << ", ";
            }
            os << "]";
        }

        void visit(HIRExprNodeArraySized& node) override {
            os << "[";
            this->visitNodePtr(node.val);
            os << "; " << node.mSize;
            os << "]";
        }

        void visit(HIRExprNodeClosure& node) override {
            if (node.mCode) {
                if (node.isMove) {
                    os << " move";
                }
                os << "|";
                for (const auto& arg : node.mArgs) {
                    os << arg.first << ": " << arg.second << ", ";
                }
                os << "| -> " << node.returnType << " ";
                this->visitNodePtr(node.mCode);
            } else {
                os << node.objPath << "( ";
                for (/*const*/ auto& cap : node.captures) {
                    this->visitNodePtr(cap);
                    os << ", ";
                }
                os << ")";
            }
        }

        void visit(HIRExprNodeGenerator& node) override {
            if (node.mCode) {
                os << "/*gen*/";
                if (node.isPinned) {
                    os << "static ";
                }
                if (node.isMove) {
                    os << " move";
                }
                os << "|";
                os << "| -> " << node.returnType << " ";
                this->visitNodePtr(node.mCode);
            } else {
                os << node.objPath << "( ";
                for (/*const*/ auto& cap : node.captures) {
                    this->visitNodePtr(cap);
                    os << ", ";
                }
                os << ")";
            }
        }

        void visit(HIRExprNodeGeneratorWrapper& node) override {
            os << "/*gen body*/";
            os << "|";
            os << "| -> " << node.returnType << " ";
            this->visitNodePtr(node.mCode);
        }

        void visit(HIRExprNodeAsyncBlock& node) override {
            if (node.isMove) {
                os << "move ";
            }
            os << "async {";
            if (!node.mCode) {
                os << "/* lowered: " << node.objPath << " */";
            } else {
                this->visitNodePtr(node.mCode);
            }
            os << "}";
        }

    private:
        RepeatLitStr indent() const {
            return RepeatLitStr{"    ", static_cast<int>(indentLevel)};
        }

        void incIndent() {
            indentLevel++;
        }

        void decIndent() {
            indentLevel--;
        }
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
    HirSerialiser(HIRSerialiseWriter& out, HIRTypeInterner& typeInterner)
        : out(out)
        , typeInterner(typeInterner)
    {
    }

    void clear() {
        types.clear();
    }

    template <typename V>
    void serialiseStrmap(const ::std::map<RcString, V>& map) {
        out.writeCount(map.size());
        for (const auto& v : map) {
            DEBUG(v.first);
            out.writeString(v.first);
            serialise(v.second);
        }
    }

    template <typename V>
    void serialiseStrmap(const ::std::map<::std::string, V>& map) {
        out.writeCount(map.size());
        for (const auto& v : map) {
            out.writeString(v.first);
            serialise(v.second);
        }
    }

    template <typename V>
    void serialisePathmap(const ::std::map<HIRSimplePath, V>& map) {
        out.writeCount(map.size());
        for (const auto& v : map) {
            DEBUG("- " << v.first);
            serialise(v.first);
            serialise(v.second);
        }
    }

    template <typename V>
    void serialiseStrmap(const ::std::unordered_map<RcString, V>& map) {
        out.writeCount(map.size());
        for (const auto& v : map) {
            DEBUG("- " << v.first);
            out.writeString(v.first);
            serialise(v.second);
        }
    }

    template <typename V>
    void serialiseStrmap(const ::std::unordered_map<::std::string, V>& map) {
        out.writeCount(map.size());
        for (const auto& v : map) {
            DEBUG("- " << v.first);
            out.writeString(v.first);
            serialise(v.second);
        }
    }

    template <typename V>
    void serialiseStrmap(const ::std::unordered_multimap<RcString, V>& map) {
        out.writeCount(map.size());
        for (const auto& v : map) {
            DEBUG("- " << v.first);
            out.writeString(v.first);
            serialise(v.second);
        }
    }

    template <typename V>
    void serialiseStrmap(const ::std::unordered_multimap<::std::string, V>& map) {
        out.writeCount(map.size());
        for (const auto& v : map) {
            DEBUG("- " << v.first);
            out.writeString(v.first);
            serialise(v.second);
        }
    }

    template <typename T>
    void serialiseVec(const ThinVector<T>& vec) {
        TRACE_FUNCTION_F("<" << typeid(T).name() << "> size=" << vec.size());
        auto _ = out.openObject(typeid(ThinVector<T>).name());
        out.writeCount(vec.size());
        for (const auto& i : vec) {
            serialise(i);
        }
    }

    template <typename T>
    void serialiseVec(const ::std::vector<T>& vec) {
        TRACE_FUNCTION_F("<" << typeid(T).name() << "> size=" << vec.size());
        auto _ = out.openObject(typeid(::std::vector<T>).name());
        out.writeCount(vec.size());
        for (const auto& i : vec) {
            serialise(i);
        }
    }

    template <typename T>
    void serialise(const ::std::vector<T>& vec) {
        serialiseVec(vec);
    }

    template <typename T>
    void serialise(const ::std::set<T>& s) {
        TRACE_FUNCTION_F("size=" << s.size());
        auto _ = out.openObject(typeid(::std::set<T>).name());
        out.writeCount(s.size());
        for (const auto& i : s) {
            serialise(i);
        }
    }

    void serialise(const HIRPublicity& pub) {
        out.writeBool(pub.isGlobal());
    }

    template <typename T>
    void serialise(const HIRVisEnt<T>& e) {
        serialise(e.publicity);
        serialise(e.ent);
    }

    template <typename T>
    void serialise(const HIRVisEnt<T>* e) {
        assert(e);
        serialise(*e);
    }

    template <typename T>
    void serialise(const ::std::unique_ptr<T>& e) {
        serialise(*e);
    }

    template <typename T>
    void serialise(const ::std::pair<::std::string, T>& e) {
        out.writeString(e.first);
        serialise(e.second);
    }

    template <typename T>
    void serialise(const ::std::pair<RcString, T>& e) {
        out.writeString(e.first);
        serialise(e.second);
    }

    template <typename T>
    void serialise(const ::std::pair<unsigned int, T>& e) {
        out.writeCount(e.first);
        serialise(e.second);
    }

    //}

    void serialise(bool v) {
        out.writeBool(v);
    };

    void serialise(unsigned int v) {
        out.writeCount(v);
    };

    void serialise(uint8_t v) {
        out.writeU8(v);
    };

    void serialise(uint64_t v) {
        out.writeU64c(v);
    };

    void serialise(int64_t v) {
        out.writeI64c(v);
    };

    void serialise(const HIRGenericRef& ge) {
        out.writeString(ge.name);
        out.writeU16(ge.binding);
    }

    void serialiseArraysize(const HIRArraySize& as) {
        out.writeTag(static_cast<int>(as.tag()));
            TU_MATCH_HDRA( (as), { )
            TU_ARMA(Unevaluated, se) {
                serialise(se);
            }
            TU_ARMA(Known, se) {
                out.writeU64c(se);
            }
            }
    }

    void serialiseType(const HIRTypeData* ty) {
        // Use string comparison to ensure that lifetimes are checked
        auto tyStr = FMT(ty);
        if (tyStr[0] == '{') {
            auto p = tyStr.find('}');
            tyStr = tyStr.substr(p + 1);
        }

        auto it = types.find(tyStr);
        if (it != types.end()) {
            DEBUG("Cached " << it->second);
            out.writeCount(it->second);
            return;
        }
        out.writeCount(~0u);
        DEBUG("Fresh " << types.size());

        auto _ = out.openObject("HIR::TypeData");
        out.writeTag(ty->tag());
            TU_MATCH_HDRA( (*ty), {)
            TU_ARMA(Infer, e) {
                // BAAD
            }
            TU_ARMA(Diverge, e) {
            }
            TU_ARMA(Primitive, e) {
                out.writeTag(static_cast<int>(e));
            }
            TU_ARMA(Path, e) {
                serialisePath(e.path);
            }
            TU_ARMA(Generic, e) {
                serialise(e);
            }
            TU_ARMA(TraitObject, e) {
                serialiseTraitpath(e.mTrait);
                serialiseVec(e.markers);
            }
            TU_ARMA(ErasedType, e) {
                TODO(Span(), "Serialse ErasedType?");

                out.writeBool(e.isSized);
                serialiseVec(e.traits);
                serialisePathparams(e.use);
            }
            TU_ARMA(Array, e) {
                serialiseType(e.inner);
                serialiseArraysize(e.size);
            }
            TU_ARMA(Slice, e) {
                serialiseType(e.inner);
            }
            TU_ARMA(Tuple, e) {
                serialiseVec(e);
            }
            TU_ARMA(Borrow, e) {
                out.writeTag(static_cast<int>(e.type));
                serialiseType(e.inner);
            }
            TU_ARMA(Pointer, e) {
                out.writeTag(static_cast<int>(e.type));
                serialiseType(e.inner);
            }
            TU_ARMA(NamedFunction, e) {
                serialisePath(e.path);
            }
            TU_ARMA(Function, e) {
                out.writeBool(e.isUnsafe);
                out.writeBool(e.isVariadic);
                out.writeString(e.mAbi);
                serialiseType(e.mRettype);
                serialiseVec(e.argTypes);
            }
            break;
            case HIRTypeData::TAG_NodeType:
                BUG(Span(), "Encountered invalid type when serialising - " << ty);
                break;
            }

            types.insert(std::make_pair( std::move(tyStr), types.size() ));
    }

    void serialiseSimplepath(const HIRSimplePath& path) {
        TRACE_FUNCTION_F(path);
        serialiseVec(path.members);
    }

    void serialisePathparams(const HIRPathParams& pp) {
        serialiseVec(pp.types);
        serialiseVec(pp.values);
    }

    void serialiseGenericpath(const HIRGenericPath& path) {
        TRACE_FUNCTION_F(path);
        serialiseSimplepath(path.mPath);
        serialisePathparams(path.mParams);
    }

    void serialise(const HIRGenericPath& path) {
        serialiseGenericpath(path);
    }

    void serialiseTraitpath(const HIRTraitPath& path) {
        auto _ = out.openObject("HIR::TraitPath");
        serialiseGenericpath(path.mPath);
        serialiseStrmap(path.typeBounds);
        serialiseStrmap(path.traitBounds);
        out.writeU8(static_cast<uint8_t>(path.constness));
    }

    void serialise(const HIRTraitPath::AtyEqual& e) {
        serialise(e.sourceTrait);
        serialisePathparams(e.atyParams);
        serialise(e.type);
    }

    void serialise(const HIRTraitPath::AtyBound& e) {
        serialise(e.sourceTrait);
        serialisePathparams(e.atyParams);
        serialiseVec(e.traits);
    }

    void serialisePath(const HIRPath& path) {
        TRACE_FUNCTION_F("path=" << path);
            TU_MATCH_HDRA( (path.mData), {)
            TU_ARMA(Generic, e) {
                out.writeTag(0);
                serialiseGenericpath(e);
            }
            TU_ARMA(UfcsInherent, e) {
                out.writeTag(1);
                serialiseType(e.type);
                out.writeString(e.item);
                serialisePathparams(e.params);
                serialisePathparams(e.implParams);
            }
            TU_ARMA(UfcsKnown, e) {
                out.writeTag(2);
                serialiseType(e.type);
                serialiseGenericpath(e.trait);
                out.writeString(e.item);
                serialisePathparams(e.params);
            }
            TU_ARMA(UfcsUnknown, e) {
                DEBUG("-- UfcsUnknown - " << path);
                assert(!"Unexpected UfcsUnknown");
            }
            }
    }

    void serialiseGenerics(const HIRGenericParams& params) {
        DEBUG("params = " << params.fmtArgs() << ", " << params.fmtBounds());
        serialiseVec(params.types);
        serialiseVec(params.values);
        serialiseVec(params.bounds);
    }

    void serialise(const HIRTypeParamDef& pd) {
        out.writeString(pd.mName);
        serialiseType(pd.defaultValue);
        out.writeBool(pd.isSized);
    }

    void serialise(const HIRValueParamDef& pd) {
        out.writeString(pd.mName);
        serialiseType(pd.mType);
        serialise(pd.defaultValue);
    }

    void serialise(const HIRGenericBound& b) {
        TRACE_FUNCTION_F(b);
            TU_MATCH_HDRA( (b), {)
            TU_ARMA(TraitBound, e) {
                out.writeTag(2);
                serialiseType(e.type);
                serialiseTraitpath(e.trait);
                out.writeU8(static_cast<uint8_t>(e.constness));
            }
            TU_ARMA(TypeEquality, e) {
                out.writeTag(3);
                serialiseType(e.type);
                serialiseType(e.otherType);
            }
            }
    }

    void serialise(const HIRProcMacro& pm) {
        TRACE_FUNCTION_F("pm = ProcMacro { " << pm.name << ", " << pm.path << ", [" << pm.attributes << "] }");
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
    void serialise(const HIRCrate::ImplGroup<T>& ig) {
        serialisePathmap(ig.named);
        serialiseVec(ig.nonNamed);
        serialiseVec(ig.generic);
    }

    void serialiseCrate(const HIRCrate& crate) {
        out.writeString(crate.crateName);
        out.writeTag(static_cast<int>(crate.edition));
        serialiseModule(crate.mRootModule);

        serialise(crate.typeImpls);
        serialisePathmap(crate.traitImpls);
        serialisePathmap(crate.markerImpls);

        serialiseVec(crate.exportedMacroNames);

        {
            decltype(crate.mLangItems) langItemsFiltered;
            for (const auto& ent : crate.mLangItems) {
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

    void serialise(const HIRExternLibrary& lib) {
        out.writeString(lib.name);
    }

    void serialiseModule(const HIRModule& mod) {
        TRACE_FUNCTION;
        auto _ = out.openObject("HIR::Module");

        // m_traits doesn't need to be serialised

        serialiseStrmap(mod.valueItems);
        serialiseStrmap(mod.modItems);
        serialiseStrmap(mod.macroItems);
    }

    void serialiseTypeimpl(const HIRTypeImpl& impl) {
        TRACE_FUNCTION_F("impl" << impl.mParams.fmtArgs() << " " << impl.mType);
        serialiseGenerics(impl.mParams);
        serialiseType(impl.mType);

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

    void serialise(const HIRTypeImpl& impl) {
        serialiseTypeimpl(impl);
    }

    void serialiseTraitimpl(const HIRTraitImpl& impl) {
        TRACE_FUNCTION_F("impl" << impl.mParams.fmtArgs() << " ?" << impl.traitArgs << " for " << impl.mType);
        serialiseGenerics(impl.mParams);
        serialisePathparams(impl.traitArgs);
        serialiseType(impl.mType);
        out.writeBool(impl.isConst);

        out.writeCount(impl.methods.size());
        for (const auto& v : impl.methods) {
            DEBUG("fn " << v.first);
            out.writeString(v.first);
            out.writeBool(v.second.isSpecialisable);
            serialise(v.second.data);
        }
        out.writeCount(impl.constants.size());
        for (const auto& v : impl.constants) {
            DEBUG("const " << v.first);
            out.writeString(v.first);
            out.writeBool(v.second.isSpecialisable);
            serialise(v.second.data);
        }
        out.writeCount(impl.statics.size());
        for (const auto& v : impl.statics) {
            DEBUG("static " << v.first);
            out.writeString(v.first);
            out.writeBool(v.second.isSpecialisable);
            serialise(v.second.data);
        }
        out.writeCount(impl.types.size());
        for (const auto& v : impl.types) {
            DEBUG("type " << v.first);
            out.writeString(v.first);
            out.writeBool(v.second.isSpecialisable);
            serialise(v.second.data);
        }
        // m_src_module doesn't matter after typeck
    }

    void serialise(const HIRTraitImpl& impl) {
        serialiseTraitimpl(impl);
    }

    void serialiseMarkerimpl(const HIRMarkerImpl& impl) {
        serialiseGenerics(impl.mParams);
        serialisePathparams(impl.traitArgs);
        out.writeBool(impl.isPositive);
        serialiseType(impl.mType);
    }

    void serialise(const HIRMarkerImpl& impl) {
        serialiseMarkerimpl(impl);
    }

    void serialise(const HIRTypeData* ty) {
        serialiseType(ty);
    }

    void serialise(const HIRSimplePath& p) {
        serialiseSimplepath(p);
    }

    void serialise(const HIRTraitPath& p) {
        serialiseTraitpath(p);
    }

    void serialise(const ::std::string& v) {
        out.writeString(v);
    }

    void serialise(const RcString& v) {
        out.writeString(v);
    }

    void serialise(const Ident::Hygiene& h) {
        auto _ = out.openObject(typeid(Ident::Hygiene).name());
        out.writeBool(h.hasModPath());
        if (h.hasModPath()) {
            out.writeString(h.modPath().crate);
            serialiseVec(h.modPath().ents);
        }
    }

    void serialise(const ::MacroRulesPtr& mac) {
        serialise(*mac);
    }

    void serialise(const ::MacroRules& mac) {
        //m_exported: IGNORE, should be set
        out.writeString(mac.sourceCrate);
        out.writeTag(static_cast<unsigned int>(mac.edition));
        assert(mac.rules.size() > 0);
        out.writeBool(mac.isMacroItem);
        serialiseVec(mac.rules);
        serialise(mac.mHygiene);
    }

    void serialise(const ::MacroPatEnt& pe) {
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

    void serialise(const ::SimplePatIfCheck& e) {
        out.writeTag(static_cast<int>(e.ty));
        serialise(e.tok);
    }

    void serialise(const ::SimplePatEnt& pe) {
        out.writeTag(pe.tag());
            TU_MATCH_HDRA( (pe), { )
            TU_ARMA(End, _e) {
            }
            TU_ARMA(LoopStart, e) {
                out.writeCount(e.index);
            }
            TU_ARMA(LoopNext, _e) {
            }
            TU_ARMA(LoopEnd, _e) {
            }
            TU_ARMA(Jump, e) {
                out.writeCount(e.jumpTarget);
            }
            TU_ARMA(ExpectTok, e) {
                serialise(e);
            }
            TU_ARMA(ExpectPat, e) {
                out.writeTag(static_cast<int>(e.type));
                out.writeCount(e.idx);
            }
            TU_ARMA(If, e) {
                out.writeBool(e.isEqual);
                out.writeCount(e.jumpTarget);
                serialiseVec(e.ents);
            }
            }
    }

    void serialise(const ::MacroRulesArm& arm) {
        serialiseVec(arm.paramNames);
        serialiseVec(arm.pattern);
        serialiseVec(arm.contents);
    }

    void serialise(const ::MacroExpansionEnt& ent) {
            TU_MATCH_HDRA( (ent), {)
            TU_ARMA(Token, e) {
                out.writeTag(0);
                serialise(e);
            }
            TU_ARMA(NamedValue, e) {
                out.writeTag(1);
                out.writeU8(e >> 24);
                out.writeCount(e & 0x00FFFFFF);
            }
            TU_ARMA(Loop, e) {
                out.writeTag(2);
                serialiseVec(e.entries);
                serialise(e.joiner);
                serialise(e.controllingInputLoops);
            }
            TU_ARMA(Concat, e) {
                out.writeTag(3);
                serialiseVec(e);
            }
            }
    }

    void serialise(const ::MacroExpansionConcatEnt& e) {
        out.writeTag(e.tag());
            TU_MATCH_HDRA((e), {)
            TU_ARMA(Ident, i) {
                serialise(i.hygiene);
                out.writeString(i.name);
            }
            TU_ARMA(Named, i) {
                serialise(i);
            }
            }
    }

    void serialise(const ::Token& tok) {
        out.writeTag(tok.mType);
        serialise(tok.mData);
        // TODO: Position information.
    }

    void serialise(const ::Token::Data& td) {
        out.writeTag(td.tag());
        switch (td.tag()) {
            case ::Token::Data::TAGDEAD:
                throw "";
                TU_ARM(td, None, _e) {
                }
                break;
                TU_ARM(td, String, e) {
                    out.writeString(e);
                }
                break;
                TU_ARM(td, Ident, e) {
                    serialise(e.hygiene);
                    out.writeString(e.name);
                }
                break;
                TU_ARM(td, Integer, e) {
                    out.writeTag(e.datatype);
                    out.writeU128(e.intval);
                }
                break;
                TU_ARM(td, Float, e) {
                    out.writeTag(e.datatype);
                    out.writeFloatValue(e.floatval);
                }
                break;
                TU_ARM(td, Fragment, e)
                assert(!"Serialising interpolated macro fragment - should have been handled in HIR lowering");
        }
    }

    void serialise(const EncodedLiteral& lit) {
        serialise(lit.bytes);
        out.writeCount(lit.relocations.size());
        for (const auto& reloc : lit.relocations) {
            out.writeCount(reloc.ofs);
            out.writeCount(reloc.len);
            if (reloc.p) {
                out.writeTag(0);
                serialisePath(*reloc.p);
            } else {
                out.writeTag(1);
                serialise(reloc.bytes);
            }
        }
    }

    void serialise(const HIRConstGenericUnevaluated& v) {
        ASSERT_BUG(v.expr->span(), v.expr->mir, "Encountered non-translated value in ConstGeneric: " << v);
        serialisePathparams(v.paramsImpl);
        serialisePathparams(v.paramsItem);
        serialise(*v.expr);
    }

    void serialise(const HIRConstGeneric& v) {
        out.writeTag(v.tag());
            TU_MATCH_HDRA( (v), {)
            TU_ARMA(Infer, e) {
            }
            TU_ARMA(Unevaluated, e) {
                serialise(*e);
            }
            TU_ARMA(Generic, e)
            serialise(e);
            TU_ARMA(Evaluated, e)
            serialise(*e);
            }
    }

    void serialise(const HIRExprPtr& exp, bool saveMir = true) {
        auto _ = out.openObject("HIR::ExprPtr");
        saveMir &= static_cast<bool>(exp.mir);
        out.writeBool(saveMir);
        if (saveMir) {
            serialise(*exp.mir);
        }
        serialiseVec(exp.erasedTypes);
    }

    void serialise(const MIRFunction& mir) {
        // Write out MIR.
        serialiseVec(mir.locals);
        serialiseVec(mir.dropFlags);
        serialiseVec(mir.blocks);
    }

    void serialise(const MIRBasicBlock& block) {
        serialiseVec(block.statements);
        serialise(block.terminator);
        out.writeBool(block.isCleanup);
    }

    void serialise(const AsmLineFragment& l) {
        serialise(l.before);
        out.writeCount(l.index);
        out.writeI64c(l.modifier);
    }

    void serialise(const AsmLine& l) {
        serialiseVec(l.frags);
        serialise(l.trailing);
    }

    void serialise(const AsmRegisterSpec& r) {
        out.writeTag(static_cast<unsigned>(r.tag()));
            TU_MATCH_HDRA( (r), {)
            TU_ARMA(Class, e) {
                out.writeTag(static_cast<unsigned>(e));
            }
            TU_ARMA(Explicit, e) {
                out.writeString(e);
            }
            }
    }

    void serialise(const MIRAsmParam& p) {
        out.writeTag(static_cast<unsigned>(p.tag()));
            TU_MATCH_HDRA( (p), {)
            TU_ARMA(Sym, e) {
                serialisePath(e);
            }
            TU_ARMA(Const, e) {
                serialise(e);
            }
            TU_ARMA(Reg, e) {
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
            }
            }
    }

    void serialise(const AsmOptions& o) {
        uint16_t bitflag1 = 0;
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
#undef BIT
        out.writeU16(bitflag1);
    }

    void serialise(const MIRStatement& stmt) {
        auto _ = out.openObject("MIR::Statement");
            TU_MATCH_HDRA( (stmt), {)
            TU_ARMA(Assign, e) {
                out.writeTag(0);
                serialise(e.dst);
                serialise(e.src);
            }
            TU_ARMA(Asm, e) {
                out.writeTag(2);
                out.writeString(e.tpl);
                serialiseVec(e.outputs);
                serialiseVec(e.inputs);
                serialiseVec(e.clobbers);
                serialiseVec(e.flags);
            }
            TU_ARMA(SetDropFlag, e) {
                out.writeTag(3);
                out.writeCount(e.idx);
                out.writeBool(e.newVal);
                out.writeCount(e.other);
            }
            TU_ARMA(ScopeEnd, e) {
                out.writeTag(4);
                serialiseVec(e.slots);
            }
            TU_ARMA(Asm2, e) {
                out.writeTag(5);
                serialise(e.options);
                serialiseVec(e.lines);
                serialiseVec(e.params);
            }
            TU_ARMA(SaveDropFlag, e) {
                out.writeTag(6);
                serialise(e.slot);
                out.writeCount(e.bitIndex);
                out.writeCount(e.idx);
            }
            TU_ARMA(LoadDropFlag, e) {
                out.writeTag(7);
                out.writeCount(e.idx);
                serialise(e.slot);
                out.writeCount(e.bitIndex);
            }
            }
    }

    void serialise(const MIRTerminator& term) {
        auto serialiseUnwind = [this](const MIRUnwindAction& action) {
            out.writeTag(static_cast<int>(action.tag()));
            TU_IFLET(MIRUnwindAction, action, Cleanup, target, out.writeCount(target);)
        };
        out.writeTag(static_cast<int>(term.tag()));
        TU_MATCHA(
            (term),
            (e),
            (
                Incomplete,
                // NOTE: loops that diverge (don't break) leave a dangling bb
            ),
            (Return, ),
            (UnwindResume, ),
            (UnwindTerminate, ),
            (Unreachable, ),
            (Goto, out.writeCount(e);),
            (If, serialise(e.cond); out.writeCount(e.bbTrue); out.writeCount(e.bbFalse);),
            (Switch, serialise(e.val); serialiseVec(e.targets); out.writeCount(e.validFlag); out.writeCount(e.invalidTarget);),
            (SwitchValue, serialise(e.val); out.writeCount(e.defTarget); serialiseVec(e.targets); serialise(e.values);),
            (Drop, out.writeTag(static_cast<unsigned>(e.kind)); serialise(e.slot); out.writeCount(e.flagIdx); out.writeCount(e.target); serialiseUnwind(e.unwind);),
            (Call, out.writeCount(e.retBlock); serialiseUnwind(e.unwind); serialise(e.retVal); serialise(e.fcn); serialiseVec(e.args);)
        )
    }

    void serialise(const MIRSwitchValues& sv) {
        out.writeTag(static_cast<int>(sv.tag()));
            TU_MATCH_HDRA( (sv), {)
            TU_ARMA(Unsigned, e) {
                serialiseVec(e);
            }
            TU_ARMA(Signed, e) {
                serialiseVec(e);
            }
            TU_ARMA(String, e) {
                serialiseVec(e);
            }
            TU_ARMA(ByteString, e) {
                serialiseVec(e);
            }
            }
    }

    void serialise(const MIRCallTarget& ct) {
        out.writeTag(static_cast<int>(ct.tag()));
        TU_MATCHA((ct), (e), (Value, serialise(e);), (Path, serialisePath(e);), (Intrinsic, out.writeString(e.name); serialisePathparams(e.params);))
    }

    void serialise(const MIRParam& p) {
        TRACE_FUNCTION_F("Param = " << p);
        out.writeTag(static_cast<int>(p.tag()));
        TU_MATCHA((p), (e), (LValue, serialise(e);), (Borrow, out.writeTag(static_cast<int>(e.type)); serialise(e.val);), (Constant, serialise(e);))
    }

    void serialise(const MIRLValue& lv) {
        TRACE_FUNCTION_F("LValue = " << lv);
        if (lv.root.is_Static()) {
            out.writeCount(3);
            serialisePath(lv.root.as_Static());
        } else {
            out.writeCount(lv.root.getInner());
        }
        serialiseVec(lv.wrappers);
    }

    void serialise(const MIRLValue::Wrapper& w) {
        out.writeCount(w.getInner());
    }

    void serialise(const MIRRValue& val) {
        TRACE_FUNCTION_F("RValue = " << val);
        out.writeTag(val.tag());
        TU_MATCHA((val), (e), (Use, serialise(e);), (Constant, serialise(e);), (SizedArray, serialise(e.val); serialiseArraysize(e.count);), (Borrow, out.writeTag(static_cast<int>(e.type)); out.writeBool(e.isRaw); serialise(e.val);), (Cast, serialise(e.val); serialise(e.type);), (BinOp, serialise(e.valL); out.writeTag(static_cast<int>(e.op)); serialise(e.valR);), (UniOp, serialise(e.val); out.writeTag(static_cast<int>(e.op));), (DstMeta, serialise(e.val);), (DstPtr, serialise(e.val);), (MakeDst, serialise(e.ptrVal); auto b = !TU_TEST2(e.metaVal, Constant, , ItemAddr, .get() == nullptr); out.writeBool(b); if (b) serialise(e.metaVal);), (Tuple, serialiseVec(e.vals);), (Array, serialiseVec(e.vals);), (UnionVariant, serialiseGenericpath(e.path); out.writeCount(e.index); serialise(e.val);), (EnumVariant, serialiseGenericpath(e.path); out.writeCount(e.index); serialiseVec(e.vals);), (Struct, serialiseGenericpath(e.path); serialiseVec(e.vals);))
    }

    void serialise(const MIRConstant& v) {
        out.writeTag(v.tag());
        TU_MATCHA((v), (e), (Int, out.writeU128(e.v.getInner()); out.writeTag(static_cast<unsigned>(e.t));), (Uint, out.writeU128(e.v); out.writeTag(static_cast<unsigned>(e.t));), (Float, out.writeFloatValue(e.v); out.writeTag(static_cast<unsigned>(e.t));), (Bool, out.writeBool(e.v);), (Bytes, out.writeCount(e.size()); out.write(e.data(), e.size());), (StaticString, out.writeString(e);), (Const, ASSERT_BUG(Span(), monomorphisePathNeeded(*e.p), "Unexpected Constant: " << *e.p); serialisePath(*e.p);), (Generic, serialise(e);), (Function, serialisePath(*e.p);), (ItemAddr, serialisePath(*e); out.writeU128(e.offset);))
    }

    void serialise(const HIRTypeItem& item) {
        TU_MATCHA((item), (e), (Import, out.writeTag(0); serialiseSimplepath(e.path); out.writeBool(e.isVariant); out.writeCount(e.idx);), (Module, out.writeTag(1); serialiseModule(e);), (TypeAlias, out.writeTag(2); serialise(e);), (Enum, out.writeTag(3); serialise(e);), (Struct, out.writeTag(4); serialise(e);), (Trait, out.writeTag(5); serialise(e);), (Union, out.writeTag(6); serialise(e);), (ExternType, out.writeTag(7); serialise(e);), (TraitAlias, out.writeTag(8); serialise(e);))
    }

    void serialise(const HIRMacroItem& item) {
        auto _ = out.openObject("HIR::MacroItem");
        out.writeTag(item.tag());
            TU_MATCH_HDRA( (item), {)
            TU_ARMA(Import, e) {
                serialise(e.path);
            }
            TU_ARMA(MacroRules, e) {
                serialise(e);
            }
            TU_ARMA(ProcMacro, e) {
                serialise(e);
            }
            }
    }

    void serialise(const HIRValueItem& item) {
        TU_MATCHA((item), (e), (Import, out.writeTag(0); serialiseSimplepath(e.path); out.writeBool(e.isVariant); out.writeCount(e.idx);), (Constant, out.writeTag(1); serialise(e);), (Static, out.writeTag(2); serialise(e);), (StructConstant, out.writeTag(3); serialiseSimplepath(e.ty);), (Function, out.writeTag(4); serialise(e);), (StructConstructor, out.writeTag(5); serialiseSimplepath(e.ty);))
    }

    void serialise(const HIRLinkage& linkage) {
        out.writeString(linkage.name);
    }

    // - Value items
    void serialise(const HIRFunction& fcn) {
        TRACE_FUNCTION_F("_function:");
        auto _ = out.openObject("HIR::Function");

        serialise(fcn.linkage);

        out.writeTag(static_cast<int>(fcn.receiver));
        serialise(fcn.receiverType.value_or(typeInterner.infer()));
        out.writeString(fcn.mAbi);
        out.writeBool(fcn.unsafe);
        out.writeBool(fcn.isConst);
        serialise(fcn.markings);

        serialiseGenerics(fcn.mParams);
        out.writeCount(fcn.mArgs.size());
        for (const auto& a : fcn.mArgs) {
            serialise(a.second);
        }
        DEBUG("m_args = " << fcn.mArgs);
        out.writeBool(fcn.variadic);
        serialise(fcn.returnType);

        serialise(fcn.mCode, fcn.saveCode || fcn.isConst);
    }

    void serialise(const HIRFunction::Markings& m) {
        auto _ = out.openObject("HIR::Function::Markings");
        serialiseVec(m.rustcLegacyConstGenerics);
        out.writeBool(m.trackCaller);
        out.writeBool(m.isRustcIntrinsic);
        out.writeBool(m.isRustcPromotable);
    }

    void serialise(const HIRConstant& item) {
        TRACE_FUNCTION_F("_constant:");

        serialiseGenerics(item.mParams);
        serialise(item.mType);
        serialise(item.mValue);
        bool writeVal = item.valueState == HIRConstant::ValueState::Known;
        out.writeBool(writeVal);
        if (writeVal) {
            serialise(item.valueRes);
        }
    }

    void serialise(const HIRStatic& item) {
        TRACE_FUNCTION_F("_static:");

        serialise(item.linkage);
        serialiseGenerics(item.mParams);

        uint8_t bitflag1 = 0;
#define BIT(i, fld) \
    if (fld)        \
        bitflag1 |= 1 << (i);
        BIT(0, item.isMut);
        BIT(1, item.saveLiteral)
        BIT(2, item.explicitAlignment != 0)
#undef BIT
        out.writeU8(bitflag1);
        if (item.explicitAlignment != 0) {
            out.writeCount(item.explicitAlignment);
        }
        serialise(item.mType);

        if (item.mParams.isGeneric()) {
            serialise(item.mValue);
        }
        // NOTE: Value not stored (What if the static is generic? It can't be.)
        // - Need to store if the item was from a const (special linkage?)
        if (item.saveLiteral) {
            serialise(item.valueRes);
        }
    }

    // - Type items
    void serialise(const HIRTypeAlias& ta) {
        serialiseGenerics(ta.mParams);
        serialiseType(ta.mType);
    }

    void serialise(const HIRTraitAlias& ta) {
        serialiseGenerics(ta.mParams);
        serialiseVec(ta.traits);
    }

    void serialise(const HIREnum& item) {
        auto _ = out.openObject("HIR::Enum");
        serialiseGenerics(item.mParams);
        out.writeBool(item.isCRepr);
        out.writeTag(static_cast<int>(item.tagRepr));
        serialise(item.mData);

        serialise(item.markings);
    }

    void serialise(const HIREnum::Class& v) {
        out.writeTag(v.tag());
        TU_MATCHA((v), (e), (Value, serialiseVec(e.variants);), (Data, serialiseVec(e);))
    }

    void serialise(const HIREnum::ValueVariant& v) {
        out.writeString(v.name);
        // NOTE: No expr, no longer needed
        out.writeU64(v.val.truncateU64());
    }

    void serialise(const HIREnum::DataVariant& v) {
        out.writeString(v.name);
        out.writeBool(v.isStruct);
        serialise(v.type);
        out.writeU64(v.discriminantValue.truncateU64());
    }

    void serialise(const HIRTraitMarkings& m) {
        uint8_t bitflag1 = 0;
#define BIT(i, fld) \
    if (fld)        \
        bitflag1 |= 1 << (i);
        BIT(0, m.hasADeref)
        BIT(1, m.isCopy)
        BIT(2, m.hasDropImpl)
#undef BIT
        out.writeU8(bitflag1);

        // TODO: auto_impls
    }

    void serialise(const HIRStructMarkings& m) {
        uint8_t bitflag1 = 0;
#define BIT(i, fld) \
    if (fld)        \
        bitflag1 |= 1 << (i);
        BIT(0, m.canUnsize)
        BIT(1, m.isNonzero)
        BIT(2, m.boundedMax)
        BIT(3, m.isFundamental)
        BIT(4, m.isNoNiche)
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

    void serialise(const HIRStruct& item) {
        TRACE_FUNCTION_F("Struct");
        auto _ = out.openObject("HIR::Struct");

        serialiseGenerics(item.mParams);
        out.writeTag(static_cast<int>(item.repr));

        out.writeTag(item.mData.tag());
        TU_MATCHA((item.mData), (e), (Unit, ), (Tuple, serialiseVec(e);), (Named, serialiseVec(e);))

        out.writeCount(item.forcedAlignment);
        out.writeCount(item.maxFieldAlignment);
        serialise(item.markings);
        serialise(item.structMarkings);
    }

    void serialise(const HIRStructField& fld) {
        serialise(fld.name);
        serialise(fld.vis);
        serialise(fld.ty);
        out.writeBool(fld.defaultValue != nullptr);
        if (fld.defaultValue) {
            serialise(*fld.defaultValue);
        }
    }

    void serialise(const HIRUnion& item) {
        TRACE_FUNCTION_F("Union");

        serialiseGenerics(item.mParams);
        out.writeTag(static_cast<int>(item.repr));

        serialiseVec(item.mVariants);

        serialise(item.markings);
    }

    void serialise(const HIRExternType& item) {
        TRACE_FUNCTION_F("ExternType");
        serialise(item.markings);
    }

    void serialise(const HIRTrait& item) {
        TRACE_FUNCTION_F("_trait:");
        auto _ = out.openObject("HIR::Trait");

        serialiseGenerics(item.mParams);
        // Kept as one byte for compatibility with metadata written before
        // the fundamental bit was represented in HIR.
        out.writeU8((item.mIsMarker ? 1u : 0u) | (item.isFundamental ? 2u : 0u) | (item.isCoinductive ? 4u : 0u) | (item.isConst ? 8u : 0u) | (item.skipArrayDuringMethodDispatch ? 16u : 0u) | (item.skipBoxedSliceDuringMethodDispatch ? 32u : 0u));
        serialiseStrmap(item.types);
        serialiseStrmap(item.values);
        serialiseStrmap(item.valueIndexes);
        serialiseStrmap(item.typeIndexes);
        out.writeCount(item.vtableParentTraitsStart);
        serialiseVec(item.allParentTraits);
        serialise(item.vtablePath);
    }

    void serialise(const HIRTraitValueItem& tvi) {
        out.writeTag(tvi.tag());
        TU_MATCHA((tvi), (e), (Constant, DEBUG("Constant"); serialise(e);), (Static, DEBUG("Static"); serialise(e);), (Function, DEBUG("Function"); serialise(e);))
    }

    void serialise(const HIRAssociatedType& at) {
        serialiseGenerics(at.generics);
        out.writeBool(at.isSized);
        serialiseVec(at.traitBounds);
        serialiseType(at.defaultValue);
    }
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
