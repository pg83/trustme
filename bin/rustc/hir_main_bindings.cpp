#include "hir_main_bindings.h"
#include "hir_hir.h"
#include "mir_mir.h"
#include "macro_rules_macro_rules.h"
#include "hir_serialise_lowlevel.h"
#include <std/mem/obj_pool.h>
#include <typeinfo>
#include "hir_visitor.h"
#include "hir_expr.h"
#include "hir_expr_state.h"
#include "hir_typeck_monomorph.h" // monomorphise_path_needed

// TODO: Have an environment variable that controls if debug is enabled here.
#define DEBUG_EXTRA_ENABLE &&des_debug_enabled()

namespace {
    bool des_debug_enabled();
}

//#define DISABLE_DEBUG   //  Disable debug for this function - too hot

namespace {
    bool des_debug_enabled() {
        static unsigned enabled = 0;
        if (enabled == 0) {
            enabled = (getenv("MRUSTC_DEBUG_DESERIALISE") ? 2 : 1);
        }
        return enabled > 1;
    }

    ::HIR::Publicity g_vis_private = ::HIR::Publicity::new_none();
}

//namespace {

template <typename T>
struct D {};

class HirDeserialiser {
    RcString crateName;
    ::std::vector<HIR::TypeRef> types;
    ::HIR::serialise::Reader& in;
    ::HIR::TypeInterner& typeInterner;

public:
    HirDeserialiser(::HIR::serialise::Reader& in, ::HIR::TypeInterner& type_interner)
        : in(in)
        , typeInterner(type_interner)
    {
    }

    RcString read_istring() {
        return in.read_istring();
    }

    ::std::string read_string() {
        return in.read_string();
    }

    bool read_bool() {
        return in.read_bool();
    }

    uint8_t read_u8() {
        return in.read_u8();
    }

    size_t deserialise_count() {
        return in.read_count();
    }

    template <typename V>
    ::std::map<::std::string, V> deserialise_strmap() {
        TRACE_FUNCTION_F("<" << typeid(V).name() << ">");
        size_t n = in.read_count();
        ::std::map<::std::string, V> rv;
        //rv.reserve(n);
        for (size_t i = 0; i < n; i++) {
            auto s = in.read_string();
            rv.insert(::std::make_pair(mv$(s), D<V>::des(*this)));
        }
        return rv;
    }

    template <typename V>
    ::std::unordered_map<::std::string, V> deserialise_strumap() {
        TRACE_FUNCTION_F("<" << typeid(V).name() << ">");
        size_t n = in.read_count();
        ::std::unordered_map<::std::string, V> rv;
        //rv.reserve(n);
        for (size_t i = 0; i < n; i++) {
            auto s = in.read_string();
            DEBUG("- " << s);
            rv.insert(::std::make_pair(mv$(s), D<V>::des(*this)));
        }
        return rv;
    }

    template <typename V>
    ::std::unordered_multimap<::std::string, V> deserialise_strummap() {
        TRACE_FUNCTION_F("<" << typeid(V).name() << ">");
        size_t n = in.read_count();
        ::std::unordered_multimap<::std::string, V> rv;
        //rv.reserve(n);
        for (size_t i = 0; i < n; i++) {
            auto s = in.read_string();
            DEBUG("- " << s);
            rv.insert(::std::make_pair(mv$(s), D<V>::des(*this)));
        }
        return rv;
    }

    template <typename V>
    ::std::map<RcString, V> deserialise_istrmap() {
        TRACE_FUNCTION_F("<" << typeid(V).name() << ">");
        size_t n = in.read_count();
        ::std::map<RcString, V> rv;
        //rv.reserve(n);
        for (size_t i = 0; i < n; i++) {
            auto s = in.read_istring();
            rv.insert(::std::make_pair(mv$(s), D<V>::des(*this)));
        }
        return rv;
    }

    template <typename V>
    ::std::unordered_map<RcString, V> deserialise_istrumap() {
        TRACE_FUNCTION_F("<" << typeid(V).name() << ">");
        size_t n = in.read_count();
        ::std::unordered_map<RcString, V> rv;
        //rv.reserve(n);
        for (size_t i = 0; i < n; i++) {
            auto s = in.read_istring();
            DEBUG("- " << s);
            rv.insert(::std::make_pair(mv$(s), D<V>::des(*this)));
        }
        return rv;
    }

    template <typename V>
    ::std::unordered_multimap<RcString, V> deserialise_istrummap() {
        TRACE_FUNCTION_F("<" << typeid(V).name() << ">");
        size_t n = in.read_count();
        ::std::unordered_multimap<RcString, V> rv;
        //rv.reserve(n);
        for (size_t i = 0; i < n; i++) {
            auto s = in.read_istring();
            DEBUG("- " << s);
            rv.insert(::std::make_pair(mv$(s), D<V>::des(*this)));
        }
        return rv;
    }

    template <typename V>
    ::std::map<::HIR::SimplePath, V> deserialise_pathmap() {
        TRACE_FUNCTION_F("<" << typeid(V).name() << ">");
        size_t n = in.read_count();
        ::std::map<::HIR::SimplePath, V> rv;
        //rv.reserve(n);
        for (size_t i = 0; i < n; i++) {
            auto s = deserialise_simplepath();
            rv.insert(::std::make_pair(mv$(s), D<V>::des(*this)));
        }
        return rv;
    }

    template <typename T>
    ::std::vector<T> deserialise_vec_c(::std::function<T()> cb) {
        TRACE_FUNCTION_FR("<" << typeid(T).name() << ">", in.get_pos());
        auto _ = in.open_object(typeid(::std::vector<T>).name());
        size_t n = in.read_count();
        DEBUG("n = " << n);
        ::std::vector<T> rv;
        rv.reserve(n);
        for (size_t i = 0; i < n; i++) {
            rv.push_back(cb());
        }
        return rv;
    }

    template <typename T>
    ::std::vector<T> deserialise_vec() {
        return deserialise_vec_c<T>([&]() {
            return D<T>::des(*this);
        });
    }

    template <typename T>
    ThinVector<T> deserialise_thinvec_c(::std::function<T()> cb) {
        TRACE_FUNCTION_FR("<" << typeid(T).name() << ">", in.get_pos());
        auto _ = in.open_object(typeid(ThinVector<T>).name());
        size_t n = in.read_count();
        DEBUG("n = " << n);
        ThinVector<T> rv;
        rv.reserve_init(n);
        for (size_t i = 0; i < n; i++) {
            rv.push_back(cb());
        }
        return rv;
    }

    template <typename T>
    ThinVector<T> deserialise_thinvec() {
        return deserialise_thinvec_c<T>([&]() {
            return D<T>::des(*this);
        });
    }

    template <typename T>
    ::std::set<T> deserialise_set() {
        TRACE_FUNCTION_FR("<" << typeid(T).name() << ">", in.get_pos());
        auto _ = in.open_object(typeid(::std::set<T>).name());
        size_t n = in.read_count();
        DEBUG("n = " << n);
        ::std::set<T> rv;
        //rv.reserve(n);
        for (size_t i = 0; i < n; i++) {
            rv.insert(D<T>::des(*this));
        }
        return rv;
    }

    ::HIR::Publicity deserialise_pub() {
        return (in.read_bool() ? ::HIR::Publicity::new_global() : g_vis_private);
    }

    template <typename T>
    ::HIR::VisEnt<T> deserialise_visent() {
        return ::HIR::VisEnt<T>{deserialise_pub(), D<T>::des(*this)};
    }

    template <typename T>
    ::std::unique_ptr<T> deserialise_ptr() {
        return box$(D<T>::des(*this));
    }

    ::HIR::LifetimeDef deserialise_lifetimedef();
    ::HIR::LifetimeRef deserialise_lifetimeref();
    ::HIR::ArraySize deserialise_arraysize();
    ::HIR::GenericRef deserialise_genericref();
    ::HIR::TypeRef deserialise_type();
    ::HIR::SimplePath deserialise_simplepath();
    ::HIR::PathParams deserialise_pathparams();
    ::HIR::GenericPath deserialise_genericpath();
    ::HIR::TraitPath deserialise_traitpath();
    ::HIR::Path deserialise_path();

    ::HIR::GenericParams deserialise_genericparams();
    ::HIR::TypeParamDef deserialise_typaramdef();
    ::HIR::ValueParamDef deserialise_valueparamdef();
    ::HIR::GenericBound deserialise_genericbound();

    void deserialise_crate(::HIR::Crate& rv);
    ::HIR::ExternLibrary deserialise_extlib();
    ::HIR::Module deserialise_module();

    ::HIR::ProcMacro deserialise_procmacro() {
        ::HIR::ProcMacro pm;
        TRACE_FUNCTION_FR("", "ProcMacro { " << pm.name << ", " << pm.path << ", [" << pm.attributes << "]}");
        switch (in.read_tag()) {
            case 0:
                pm.ty = ::HIR::ProcMacro::Ty::Function;
                break;
            case 1:
                pm.ty = ::HIR::ProcMacro::Ty::Derive;
                break;
            case 2:
                pm.ty = ::HIR::ProcMacro::Ty::Attribute;
                break;
        }
        pm.name = in.read_istring();
        pm.path = deserialise_simplepath();
        pm.attributes = deserialise_vec<::std::string>();
        DEBUG("pm = ProcMacro { " << pm.name << ", " << pm.path << ", [" << pm.attributes << "]}");
        return pm;
    }

    ::HIR::TypeImpl deserialise_typeimpl() {
        ::HIR::TypeImpl rv;
        TRACE_FUNCTION_FR("", "impl" << rv.mParams.fmt_args() << " " << rv.mType);

        rv.mParams = deserialise_genericparams();
        rv.mType = deserialise_type();

        size_t method_count = in.read_count();
        for (size_t i = 0; i < method_count; i++) {
            auto name = in.read_istring();
            rv.methods.insert(::std::make_pair(mv$(name), ::HIR::TypeImpl::VisImplEnt<::HIR::Function>{deserialise_pub(), in.read_bool(), deserialise_function()}));
        }
        size_t const_count = in.read_count();
        for (size_t i = 0; i < const_count; i++) {
            auto name = in.read_istring();
            rv.constants.insert(::std::make_pair(mv$(name), ::HIR::TypeImpl::VisImplEnt<::HIR::Constant>{deserialise_pub(), in.read_bool(), deserialise_constant()}));
        }
        size_t type_count = in.read_count();
        for (size_t i = 0; i < type_count; i++) {
            auto name = in.read_istring();
            rv.types.insert(::std::make_pair(mv$(name), ::HIR::TypeImpl::VisImplEnt<::HIR::TypeAlias>{deserialise_pub(), in.read_bool(), deserialise_typealias()}));
        }
        // m_src_module doesn't matter after typeck
        return rv;
    }

    ::HIR::TraitImpl deserialise_traitimpl() {
        ::HIR::TraitImpl rv;
        TRACE_FUNCTION_FR("", "impl" << rv.mParams.fmt_args() << " ?" << rv.traitArgs << " for " << rv.mType);

        rv.mParams = deserialise_genericparams();
        rv.traitArgs = deserialise_pathparams();
        rv.mType = deserialise_type();
        rv.isConst = in.read_bool();
        DEBUG("impl" << rv.mParams.fmt_args() << " ?" << rv.traitArgs << " for " << rv.mType);

        size_t method_count = in.read_count();
        for (size_t i = 0; i < method_count; i++) {
            auto name = in.read_istring();
            auto is_spec = in.read_bool();
            DEBUG((is_spec ? "default " : "") << "fn " << name);
            rv.methods.insert(::std::make_pair(mv$(name), ::HIR::TraitImpl::ImplEnt<::HIR::Function>{is_spec, deserialise_function()}));
        }
        size_t const_count = in.read_count();
        for (size_t i = 0; i < const_count; i++) {
            auto name = in.read_istring();
            auto is_spec = in.read_bool();
            DEBUG((is_spec ? "default " : "") << "const " << name);
            rv.constants.insert(::std::make_pair(mv$(name), ::HIR::TraitImpl::ImplEnt<::HIR::Constant>{is_spec, deserialise_constant()}));
        }
        size_t static_count = in.read_count();
        for (size_t i = 0; i < static_count; i++) {
            auto name = in.read_istring();
            auto is_spec = in.read_bool();
            DEBUG((is_spec ? "default " : "") << "static " << name);
            rv.statics.insert(::std::make_pair(mv$(name), ::HIR::TraitImpl::ImplEnt<::HIR::Static>{is_spec, deserialise_static()}));
        }
        size_t type_count = in.read_count();
        for (size_t i = 0; i < type_count; i++) {
            auto name = in.read_istring();
            auto is_spec = in.read_bool();
            DEBUG((is_spec ? "default " : "") << "type " << name);
            rv.types.insert(::std::make_pair(mv$(name), ::HIR::TraitImpl::ImplEnt<::HIR::TypeRef>{is_spec, deserialise_type()}));
        }

        // m_src_module doesn't matter after typeck
        return rv;
    }

    ::HIR::MarkerImpl deserialise_markerimpl() {
        auto generics = deserialise_genericparams();
        auto params = deserialise_pathparams();
        auto is_neg = in.read_bool();
        auto ty = deserialise_type();
        return ::HIR::MarkerImpl{mv$(generics), mv$(params), is_neg, mv$(ty)};
    }

    Ident::Hygiene deserialise_hygine() {
        auto _ = in.open_object(typeid(Ident::Hygiene).name());
        Ident::Hygiene rv;
        bool has_mod_path = in.read_bool();
        if (has_mod_path) {
            Ident::ModPath mp;
            mp.crate = in.read_istring();
            mp.ents = deserialise_vec<RcString>();

            if (mp.crate == "") {
                assert(crateName != "");
                mp.crate = crateName;
            }
            rv.set_mod_path(mv$(mp));
        }
        return rv;
    }

    ::MacroRulesPtr deserialise_macrorulesptr() {
        return ::MacroRulesPtr(new MacroRules(deserialise_macrorules()));
    }

    ::MacroRules deserialise_macrorules() {
        auto crate_name = in.read_istring();
        auto edition = static_cast<AST::Edition>(in.read_tag());
        ::MacroRules rv(crate_name, edition);
        // NOTE: This is set after loading.
        //rv.m_exported = true;
        rv.isMacroItem = in.read_bool();
        rv.rules = deserialise_vec_c<::MacroRulesArm>([&]() {
            return deserialise_macrorulesarm();
        });
        rv.mHygiene = deserialise_hygine();
        return rv;
    }

    ::SimplePatIfCheck deserialise_simplepatifcheck() {
        return ::SimplePatIfCheck{static_cast<::MacroPatEnt::Type>(in.read_tag()), deserialise_token()};
    }

    ::SimplePatEnt deserialise_simplepatent() {
        auto tag = static_cast<::SimplePatEnt::Tag>(in.read_tag());
        switch (tag) {
            case ::SimplePatEnt::TAG_End:
                return ::SimplePatEnt::make_End({});
            case ::SimplePatEnt::TAG_LoopStart:
                return ::SimplePatEnt::make_LoopStart({static_cast<unsigned>(in.read_count())});
            case ::SimplePatEnt::TAG_LoopNext:
                return ::SimplePatEnt::make_LoopNext({});
            case ::SimplePatEnt::TAG_LoopEnd:
                return ::SimplePatEnt::make_LoopEnd({});
            case ::SimplePatEnt::TAG_Jump:
                return ::SimplePatEnt::make_Jump({in.read_count()});
            case ::SimplePatEnt::TAG_ExpectTok:
                return SimplePatEnt::make_ExpectTok({deserialise_token()});
            case ::SimplePatEnt::TAG_ExpectPat:
                return SimplePatEnt::make_ExpectPat({static_cast<::MacroPatEnt::Type>(in.read_tag()), static_cast<unsigned>(in.read_count())});
            case SimplePatEnt::TAG_If:
                return SimplePatEnt::make_If({in.read_bool(), in.read_count(), deserialise_vec_c<SimplePatIfCheck>([&]() {
                    return deserialise_simplepatifcheck();
                })});
            default:
                BUG(Span(), "Bad tag for MacroPatEnt - #" << static_cast<int>(tag));
        }
    }

    ::MacroPatEnt deserialise_macropatent() {
        auto s = in.read_istring();
        auto n = static_cast<unsigned int>(in.read_count());
        auto type = static_cast<::MacroPatEnt::Type>(in.read_tag());
        ::MacroPatEnt rv(Span(), mv$(s), mv$(n), mv$(type));
        switch (rv.type) {
            case ::MacroPatEnt::PAT_TOKEN:
                rv.tok = deserialise_token();
                break;
            case ::MacroPatEnt::PAT_LOOP:
                rv.tok = deserialise_token();
                rv.subpats = deserialise_vec_c<::MacroPatEnt>([&]() {
                    return deserialise_macropatent();
                });
                break;
            case ::MacroPatEnt::PAT_TT:  // :tt
            case ::MacroPatEnt::PAT_PAT: // :pat
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

    ::MacroRulesArm deserialise_macrorulesarm() {
        ::MacroRulesArm rv;
        rv.paramNames = deserialise_vec<RcString>();
        rv.pattern = deserialise_vec_c<::SimplePatEnt>([&]() {
            return deserialise_simplepatent();
        });
        rv.contents = deserialise_vec_c<::MacroExpansionEnt>([&]() {
            return deserialise_macroexpansionent();
        });
        return rv;
    }

    ::MacroExpansionEnt deserialise_macroexpansionent() {
        switch (auto tag = in.read_tag()) {
            case 0:
                return ::MacroExpansionEnt(deserialise_token());
            case 1: {
                unsigned int v = static_cast<unsigned int>(in.read_u8()) << 24;
                return ::MacroExpansionEnt(v | in.read_count());
            }
            case 2: {
                auto entries = deserialise_vec_c<::MacroExpansionEnt>([&]() {
                    return deserialise_macroexpansionent();
                });
                auto joiner = deserialise_token();
                auto controllers = deserialise_set<unsigned int>();

                return ::MacroExpansionEnt::make_Loop({mv$(entries), mv$(joiner), mv$(controllers)});
            }
            case 3: {
                auto entries = deserialise_vec_c<::MacroExpansionConcatEnt>([&]() {
                    return deserialise_macroexpansionconcatent();
                });
                return ::MacroExpansionEnt(std::move(entries));
            }
            default:
                BUG(Span(), "Bad tag for MacroExpansionEnt - " << tag);
        }
    }

    ::MacroExpansionConcatEnt deserialise_macroexpansionconcatent() {
        switch (auto tag = in.read_tag()) {
            case ::MacroExpansionConcatEnt::TAG_Ident: {
                auto h = deserialise_hygine();
                auto n = in.read_istring();
                return ::MacroExpansionConcatEnt::make_Ident({h, n});
            }
            case ::MacroExpansionConcatEnt::TAG_Named:
                return ::MacroExpansionConcatEnt::make_Named(in.read_count());
            default:
                BUG(Span(), "Bad tag for MacroExpansionConcatEnt - " << tag);
        }
    }

    ::Token deserialise_token() {
        auto ty = static_cast<enum eTokenType>(in.read_tag());
        auto d = deserialise_tokendata();
        return ::Token(ty, ::std::move(d), {});
    }

    ::Token::Data deserialise_tokendata() {
        auto tag = static_cast<::Token::Data::Tag>(in.read_tag());
        switch (tag) {
            case ::Token::Data::TAG_None:
                return ::Token::Data::make_None({});
            case ::Token::Data::TAG_String:
                return ::Token::Data::make_String(in.read_string());
            case ::Token::Data::TAG_Ident: {
                auto hygine = deserialise_hygine();
                auto name = in.read_istring();
                return ::Token::Data::make_Ident(Ident(std::move(hygine), std::move(name)));
            }
            case ::Token::Data::TAG_Integer: {
                auto dty = static_cast<eCoreType>(in.read_tag());
                return ::Token::Data::make_Integer({dty, in.read_u128()});
            }
            case ::Token::Data::TAG_Float: {
                auto dty = static_cast<eCoreType>(in.read_tag());
                return ::Token::Data::make_Float({dty, in.read_float_value()});
            }
            default:
                BUG(Span(), "Bad tag for Token::Data - " << static_cast<int>(tag));
        }
    }

    ::HIR::ConstGenericUnevaluated deserialise_constgeneric_unevaluated();
    ::HIR::ConstGeneric deserialise_constgeneric();
    EncodedLiteral deserialise_encodedliteral();

    ::HIR::ExprPtr deserialise_exprptr() {
        ::HIR::ExprPtr rv;
        auto _ = in.open_object("HIR::ExprPtr");
        if (in.read_bool()) {
            rv.mir = deserialise_mir();
        }
        rv.erasedTypes = deserialise_vec<::HIR::TypeRef>();
        return rv;
    }

    ::MIR::FunctionPointer deserialise_mir();
    ::MIR::BasicBlock deserialise_mir_basicblock();
    ::MIR::Statement deserialise_mir_statement();
    AsmCommon::Options deserialise_asm_options();
    AsmCommon::LineFragment deserialise_asm_line_frag();
    AsmCommon::Line deserialise_asm_line();
    AsmCommon::RegisterSpec deserialise_asm_spec();
    ::MIR::AsmParam deserialise_asm_param();
    ::MIR::Terminator deserialise_mir_terminator();
    ::MIR::Terminator deserialise_mir_terminator_();
    ::MIR::UnwindAction deserialise_mir_unwind_action();
    ::MIR::SwitchValues deserialise_mir_switchvalues();
    ::MIR::CallTarget deserialise_mir_calltarget();

    ::MIR::Param deserialise_mir_param() {
        switch (auto tag = in.read_tag()) {
            case ::MIR::Param::TAG_LValue:
                return deserialise_mir_lvalue();
            case ::MIR::Param::TAG_Borrow:
                return ::MIR::Param::make_Borrow({static_cast<::HIR::BorrowType>(in.read_tag()), deserialise_mir_lvalue()});
            case ::MIR::Param::TAG_Constant:
                return deserialise_mir_constant();
            default:
                BUG(Span(), "Bad tag for MIR::Param - " << tag);
        }
    }

    ::MIR::LValue deserialise_mir_lvalue() {
        ::MIR::LValue rv;
        TRACE_FUNCTION_FR("", rv);
        rv = deserialise_mir_lvalue_();
        return rv;
    }

    ::MIR::LValue::Wrapper deserialise_mir_lvalue_wrapper() {
        return ::MIR::LValue::Wrapper::from_inner(in.read_count());
    }

    ::MIR::LValue deserialise_mir_lvalue_() {
        auto root_v = in.read_count();
        auto root = (root_v == 3 ? ::MIR::LValue::Storage::newStatic(deserialise_path()) : ::MIR::LValue::Storage::from_inner(root_v));
        return ::MIR::LValue(mv$(root), deserialise_vec<::MIR::LValue::Wrapper>());
    }

    ::MIR::RValue deserialise_mir_rvalue() {
        TRACE_FUNCTION;

        switch (auto tag = in.read_tag()) {
#define _(x, ...)                \
    case ::MIR::RValue::TAG_##x: \
        return ::MIR::RValue::make_##x(__VA_ARGS__);
            _(Use, deserialise_mir_lvalue())
            _(Constant, deserialise_mir_constant())
            _(SizedArray, {deserialise_mir_param(), deserialise_arraysize()})
            _(Borrow, {static_cast<::HIR::BorrowType>(in.read_tag()), in.read_bool(), deserialise_mir_lvalue()})
            _(Cast, {deserialise_mir_lvalue(), deserialise_type()})
            _(BinOp, {deserialise_mir_param(), static_cast<::MIR::eBinOp>(in.read_tag()), deserialise_mir_param()})
            _(UniOp, {deserialise_mir_lvalue(), static_cast<::MIR::eUniOp>(in.read_tag())})
            _(DstMeta, {deserialise_mir_lvalue()})
            _(DstPtr, {deserialise_mir_lvalue()})
            _(MakeDst, {deserialise_mir_param(), in.read_bool() ? deserialise_mir_param() : MIR::Constant::make_ItemAddr({})})
            _(Tuple, {deserialise_vec<::MIR::Param>()})
            _(Array, {deserialise_vec<::MIR::Param>()})
            _(UnionVariant, {deserialise_genericpath(), static_cast<unsigned int>(in.read_count()), deserialise_mir_param()})
            _(EnumVariant, {deserialise_genericpath(), static_cast<unsigned int>(in.read_count()), deserialise_vec<::MIR::Param>()})
            _(Struct, {deserialise_genericpath(), deserialise_vec<::MIR::Param>()})
#undef _
            default:
                BUG(Span(), "Bad tag for MIR::RValue - " << tag);
        }
    }

    ::MIR::Constant deserialise_mir_constant() {
        TRACE_FUNCTION;

        switch (auto tag = in.read_tag()) {
#define _(x, ...)                  \
    case ::MIR::Constant::TAG_##x: \
        DEBUG("- " #x);            \
        return ::MIR::Constant::make_##x(__VA_ARGS__);
            _(Int, {in.read_i128(), static_cast<::HIR::CoreType>(in.read_tag())})
            _(Uint, {in.read_u128(), static_cast<::HIR::CoreType>(in.read_tag())})
            _(Float, {in.read_float_value(), static_cast<::HIR::CoreType>(in.read_tag())})
            _(Bool, {in.read_bool()})
            case ::MIR::Constant::TAG_Bytes: {
                ::std::vector<unsigned char> bytes;
                bytes.resize(in.read_count());
                in.read(bytes.data(), bytes.size());
                return ::MIR::Constant::make_Bytes(mv$(bytes));
            }
                _(StaticString, in.read_string())
                _(Const, {box$(deserialise_path())})
                _(Generic, deserialise_genericref())
                _(Function, {box$(deserialise_path())})
                _(ItemAddr, {box$(deserialise_path()), in.read_u128()})
#undef _
            default:
                BUG(Span(), "Bad tag for MIR::Const - " << tag);
        }
    }

    ::HIR::ExternType deserialise_externtype() {
        return ::HIR::ExternType{deserialise_markings()};
    }

    ::HIR::TraitAlias deserialise_traitalias() {
        return ::HIR::TraitAlias{deserialise_genericparams(), deserialise_vec<HIR::TraitPath>()};
    }

    ::HIR::TypeItem deserialise_typeitem() {
        switch (auto tag = in.read_tag()) {
            case 0: {
                auto spath = deserialise_simplepath();
                auto is_variant = in.read_bool();
                return ::HIR::TypeItem::make_Import({mv$(spath), is_variant, static_cast<unsigned int>(in.read_count())});
            }
            case 1:
                return ::HIR::TypeItem(deserialise_module());
            case 2:
                return ::HIR::TypeItem(deserialise_typealias());
            case 3:
                return ::HIR::TypeItem(deserialise_enum());
            case 4:
                return ::HIR::TypeItem(deserialise_struct());
            case 5:
                return ::HIR::TypeItem(deserialise_trait());
            case 6:
                return ::HIR::TypeItem(deserialise_union());
            case 7:
                return ::HIR::TypeItem(deserialise_externtype());
            case 8:
                return ::HIR::TypeItem(deserialise_traitalias());
            default:
                BUG(Span(), "Bad tag for HIR::TypeItem - " << tag);
        }
    }

    ::HIR::ValueItem deserialise_valueitem() {
        switch (auto tag = in.read_tag()) {
            case 0: {
                auto spath = deserialise_simplepath();
                auto is_variant = in.read_bool();
                return ::HIR::ValueItem::make_Import({mv$(spath), is_variant, static_cast<unsigned int>(in.read_count())});
            }
            case 1:
                return ::HIR::ValueItem(deserialise_constant());
            case 2:
                return ::HIR::ValueItem(deserialise_static());
            case 3:
                return ::HIR::ValueItem::make_StructConstant({deserialise_simplepath()});
            case 4:
                return ::HIR::ValueItem(deserialise_function());
            case 5:
                return ::HIR::ValueItem::make_StructConstructor({deserialise_simplepath()});
            default:
                BUG(Span(), "Bad tag for HIR::ValueItem - " << tag);
        }
    }

    ::HIR::MacroItem deserialise_macroitem() {
        auto _ = in.open_object("HIR::MacroItem");
        auto tag = in.read_tag();
        switch (tag) {
            case HIR::MacroItem::TAG_Import:
                return HIR::MacroItem::Data_Import{deserialise_simplepath()};
            case HIR::MacroItem::TAG_MacroRules:
                return deserialise_macrorulesptr();
            case HIR::MacroItem::TAG_ProcMacro:
                return deserialise_procmacro();
        }

        TODO(Span(), "Bad tag for MacroItem - " << tag);
    }

    ::HIR::Linkage deserialise_linkage() {
        ::HIR::Linkage l;
        l.type = ::HIR::Linkage::Type::Auto;
        l.name = in.read_string();
        return l;
    }

    // - Value items
    ::HIR::Function deserialise_function() {
        TRACE_FUNCTION;
        auto _ = in.open_object("HIR::Function");

        ::HIR::Function rv;
        rv.saveCode = false;
        rv.linkage = deserialise_linkage();
        rv.receiver = static_cast<::HIR::Function::Receiver>(in.read_tag());
        auto receiver_type = deserialise_type();
        if (rv.receiver == ::HIR::Function::Receiver::Custom) {
            rv.receiverType = receiver_type;
        }
        rv.mAbi = in.read_istring();
        rv.unsafe = in.read_bool();
        rv.isConst = in.read_bool();
        rv.markings = deserialise_function_markings();
        rv.mParams = deserialise_genericparams();
        rv.mArgs = deserialise_fcnargs();
        rv.variadic = in.read_bool();
        rv.returnType = deserialise_type();
        rv.mCode = deserialise_exprptr();
        return rv;
    }

    ::HIR::Function::Markings deserialise_function_markings() {
        auto _ = in.open_object("HIR::Function::Markings");
        ::HIR::Function::Markings rv;
        rv.rustc_legacy_const_generics = deserialise_vec<unsigned>();
        rv.track_caller = in.read_bool();
        return rv;
    }

    ::std::vector<::std::pair<::HIR::Pattern, ::HIR::TypeRef>> deserialise_fcnargs() {
        size_t n = in.read_count();
        ::std::vector<::std::pair<::HIR::Pattern, ::HIR::TypeRef>> rv;
        rv.reserve(n);
        for (size_t i = 0; i < n; i++) {
            rv.push_back(::std::make_pair(::HIR::Pattern{}, deserialise_type()));
        }
        DEBUG("rv = " << rv);
        return rv;
    }

    ::HIR::Constant deserialise_constant() {
        TRACE_FUNCTION;

        ::HIR::Constant rv;
        rv.mParams = deserialise_genericparams();
        rv.mType = deserialise_type();
        rv.mValue = deserialise_exprptr();
        if (in.read_bool()) {
            rv.valueRes = deserialise_encodedliteral();
            rv.valueState = ::HIR::Constant::ValueState::Known;
        } else {
            rv.valueState = ::HIR::Constant::ValueState::Generic;
        }
        return rv;
    }

    ::HIR::Static deserialise_static() {
        TRACE_FUNCTION;

        auto linkage = deserialise_linkage();
        auto params = deserialise_genericparams();
        uint8_t bitflag_1 = in.read_u8();
#define BIT(i, fld) fld = (bitflag_1 & (1 << (i))) != 0;
        bool is_mut;
        bool save_literal;
        BIT(0, is_mut);
        BIT(1, save_literal);
#undef BIT
        auto ty = deserialise_type();
        auto rv = ::HIR::Static(mv$(linkage), is_mut, mv$(ty), {});
        if (params.is_generic()) {
            rv.mValue = deserialise_exprptr();
        }
        rv.mParams = ::std::move(params);
        if (save_literal) {
            rv.valueRes = deserialise_encodedliteral();
            rv.valueGenerated = true;
            rv.noEmitValue = true;
        }
        return rv;
    }

    // - Type items
    ::HIR::TypeAlias deserialise_typealias() {
        return ::HIR::TypeAlias{deserialise_genericparams(), deserialise_type()};
    }

    ::HIR::TraitMarkings deserialise_markings() {
        ::HIR::TraitMarkings m;
        uint8_t bitflag_1 = in.read_u8();
#define BIT(i, fld) fld = (bitflag_1 & (1 << (i))) != 0;
        BIT(0, m.has_a_deref)
        BIT(1, m.is_copy)
        BIT(2, m.has_drop_impl)
#undef BIT
        // TODO: auto_impls
        return m;
    }

    ::HIR::StructMarkings deserialise_str_markings() {
        ::HIR::StructMarkings m;
        uint8_t bitflag_1 = in.read_u8();
#define BIT(i, fld) fld = (bitflag_1 & (1 << (i))) != 0;
        BIT(0, m.can_unsize)
        BIT(1, m.is_nonzero)
        BIT(2, m.bounded_max)
        BIT(3, m.is_fundamental)
#undef BIT
        m.dst_type = static_cast<::HIR::StructMarkings::DstType>(in.read_tag());
        m.coerce_unsized = static_cast<::HIR::StructMarkings::Coerce>(in.read_tag());
        m.coerce_unsized_index = in.read_count();
        m.coerce_param = in.read_count();
        m.unsized_field = in.read_count();
        m.unsized_param = in.read_count();
        if (m.bounded_max) {
            m.bounded_max_value = in.read_u128();
        }
        // TODO: auto_impls
        return m;
    }

    ::HIR::Enum deserialise_enum();
    ::HIR::Enum::DataVariant deserialise_enumdatavariant();
    ::HIR::Enum::ValueVariant deserialise_enumvaluevariant();

    ::HIR::Struct deserialise_struct();
    ::HIR::StructField deserialise_struct_field();
    ::HIR::Union deserialise_union();
    ::HIR::Trait deserialise_trait();

    ::HIR::TraitValueItem deserialise_traitvalueitem() {
        switch (auto tag = in.read_tag()) {
#define _(x, ...)                                            \
    case ::HIR::TraitValueItem::TAG_##x:                     \
        DEBUG("- " #x);                                      \
        return ::HIR::TraitValueItem::make_##x(__VA_ARGS__); \
        break;
            _(Constant, deserialise_constant())
            _(Static, deserialise_static())
            _(Function, deserialise_function())
#undef _
            default:
                BUG(Span(), "Bad tag for HIR::TraitValueItem - " << tag);
        }
    }

    ::HIR::AssociatedType deserialise_associatedtype() {
        return ::HIR::AssociatedType{deserialise_genericparams(), in.read_bool(), deserialise_lifetimeref(), deserialise_vec<::HIR::TraitPath>(), deserialise_type()};
    }
};

#define DEF_D(ty, ...)                      \
    struct D<ty> {                          \
        static ty des(HirDeserialiser& d) { \
            __VA_ARGS__                     \
        }                                   \
    };

template <>
DEF_D(::std::string, return d.read_string(););
template <>
DEF_D(RcString, return d.read_istring(););
template <>
DEF_D(bool, return d.read_bool(););
template <>
DEF_D(uint8_t, return d.read_u8(););

template <typename T>
DEF_D(::std::unique_ptr<T>, return d.deserialise_ptr<T>();)

template <typename T>
DEF_D(::std::vector<T>, return d.deserialise_vec<T>();)
template <typename T, typename U>
struct D<::std::pair<T, U>> {
    static ::std::pair<T, U> des(HirDeserialiser& d) {
        auto a = D<T>::des(d);
        return ::std::make_pair(mv$(a), D<U>::des(d));
    }
};

template <typename T>
DEF_D(::HIR::VisEnt<T>, return d.deserialise_visent<T>();)

template <>
DEF_D(::HIR::LifetimeDef, return d.deserialise_lifetimedef();)
template <>
DEF_D(::HIR::LifetimeRef, return d.deserialise_lifetimeref();)
template <>
DEF_D(::HIR::TypeRef, return d.deserialise_type();)
template <>
DEF_D(::HIR::SimplePath, return d.deserialise_simplepath();)
template <>
DEF_D(::HIR::GenericPath, return d.deserialise_genericpath();)
template <>
DEF_D(::HIR::TraitPath, return d.deserialise_traitpath();)

template <>
DEF_D(::HIR::TypeParamDef, return d.deserialise_typaramdef();)
template <>
DEF_D(::HIR::ValueParamDef, return d.deserialise_valueparamdef();)
template <>
DEF_D(::HIR::GenericBound, return d.deserialise_genericbound();)

template <>
DEF_D(::HIR::ValueItem, return d.deserialise_valueitem();)
template <>
DEF_D(::HIR::TypeItem, return d.deserialise_typeitem();)
template <>
DEF_D(::HIR::MacroItem, return d.deserialise_macroitem();)

template <>
DEF_D(::HIR::Enum::ValueVariant, return d.deserialise_enumvaluevariant();)
template <>
DEF_D(::HIR::Enum::DataVariant, return d.deserialise_enumdatavariant();)
template <>
DEF_D(::HIR::StructField, return d.deserialise_struct_field();)
//template<> DEF_D( ::HIR::Literal, return d.deserialise_literal(); )
template <>
DEF_D(::HIR::ConstGeneric, return d.deserialise_constgeneric();)

template <>
DEF_D(::HIR::AssociatedType, return d.deserialise_associatedtype();)
template <>
DEF_D(::HIR::TraitValueItem, return d.deserialise_traitvalueitem();)

template <>
DEF_D(::MIR::Param, return d.deserialise_mir_param();)
template <>
DEF_D(::MIR::LValue::Wrapper, return d.deserialise_mir_lvalue_wrapper();)
template <>
DEF_D(::MIR::LValue, return d.deserialise_mir_lvalue();)
template <>
DEF_D(AsmCommon::LineFragment, return d.deserialise_asm_line_frag();)
template <>
DEF_D(AsmCommon::Line, return d.deserialise_asm_line();)
template <>
DEF_D(::MIR::AsmParam, return d.deserialise_asm_param();)
template <>
DEF_D(::MIR::Statement, return d.deserialise_mir_statement();)
template <>
DEF_D(::MIR::BasicBlock, return d.deserialise_mir_basicblock();)

template <>
DEF_D(::HIR::TraitPath::AtyEqual, auto src = d.deserialise_genericpath(); return ::HIR::TraitPath::AtyEqual{mv$(src), d.deserialise_pathparams(), d.deserialise_type()};)
template <>
DEF_D(::HIR::TraitPath::AtyBound, auto src = d.deserialise_genericpath(); return ::HIR::TraitPath::AtyBound{mv$(src), d.deserialise_pathparams(), d.deserialise_vec<HIR::TraitPath>()};);

template <>
DEF_D(::HIR::ProcMacro, return d.deserialise_procmacro();)
template <>
DEF_D(::HIR::TypeImpl, return d.deserialise_typeimpl();)
template <>
DEF_D(::HIR::TraitImpl, return d.deserialise_traitimpl();)
template <>
DEF_D(::HIR::MarkerImpl, return d.deserialise_markerimpl();)
template <>
DEF_D(::MacroRulesPtr, return d.deserialise_macrorulesptr();)
template <>
DEF_D(unsigned int, return static_cast<unsigned int>(d.deserialise_count());)

template <typename T>
DEF_D(::HIR::Crate::ImplGroup<std::unique_ptr<T>>, ::HIR::Crate::ImplGroup<std::unique_ptr<T>> rv; rv.named = d.deserialise_pathmap<::std::vector<::std::unique_ptr<T>>>(); rv.non_named = d.deserialise_vec<::std::unique_ptr<T>>(); rv.generic = d.deserialise_vec<::std::unique_ptr<T>>(); return rv;)
template <>
DEF_D(::HIR::ExternLibrary, return d.deserialise_extlib();)

    ::HIR::LifetimeDef HirDeserialiser::deserialise_lifetimedef() {
    ::HIR::LifetimeDef rv;
    rv.mName = in.read_istring();
    return rv;
}

::HIR::LifetimeRef HirDeserialiser::deserialise_lifetimeref() {
    ::HIR::LifetimeRef rv;
    rv.binding = static_cast<uint32_t>(in.read_count());
    return rv;
}

::HIR::GenericRef HirDeserialiser::deserialise_genericref() {
    return HIR::GenericRef{in.read_istring(), in.read_u16()};
}

::HIR::ArraySize HirDeserialiser::deserialise_arraysize() {
    switch (auto tag = in.read_tag()) {
#define _(x, ...)                   \
    case ::HIR::ArraySize::TAG_##x: \
        DEBUG("- " #x);             \
        return HIR::ArraySize::make_##x(__VA_ARGS__);
        _(Known, in.read_u64c())
        _(Unevaluated, deserialise_constgeneric())
        default:
            BUG(Span(), "Bad tag for HIR::ArraySize - " << tag);
#undef _
    }
}

::HIR::TypeRef HirDeserialiser::deserialise_type() {
    ::HIR::TypeRef rv;
    TRACE_FUNCTION_FR("", rv);

    auto idx = in.read_count();
    if (idx != ~0u) {
        DEBUG("#" << idx << "");
        rv = types.at(idx);
        return rv;
    } else {
        DEBUG("Fresh (=" << types.size() << ")");
    }
    auto _ = in.open_object("HIR::TypeData");

    switch (auto tag = in.read_tag()) {
#define _(x, ...)                                                      \
    case ::HIR::TypeData::TAG_##x:                                     \
        DEBUG("- " #x);                                                \
        rv = typeInterner.intern(::HIR::TypeData::make_##x(__VA_ARGS__)); \
        break;
        _(Infer, {~0u, HIR::InferClass::None})
        _(Diverge, {})
        _(Primitive, static_cast<::HIR::CoreType>(in.read_tag()))
        _(Path, {deserialise_path(), {}, in.read_bool() ? box$(deserialise_genericparams()) : nullptr})
        _(Generic, deserialise_genericref())
        _(TraitObject, {deserialise_traitpath(), deserialise_vec<::HIR::GenericPath>(), deserialise_lifetimeref()})
        case ::HIR::TypeData::TAG_ErasedType:
            TODO(Span(), "ErasedType");
            //_(ErasedType, {
            //    m_in.read_bool(),
            //    deserialise_vec< ::HIR::TraitPath>(),
            //    deserialise_vec< ::HIR::LifetimeRef>(),
            //    deserialise_type()
            //    })
            _(Array, {deserialise_type(), deserialise_arraysize()})
            _(Slice, {deserialise_type()})
            _(Tuple, deserialise_vec<::HIR::TypeRef>())
            _(Borrow, {deserialise_lifetimeref(), static_cast<::HIR::BorrowType>(in.read_tag()), deserialise_type()})
            _(Pointer, {static_cast<::HIR::BorrowType>(in.read_tag()), deserialise_type()})
            _(NamedFunction, {deserialise_path()})
            _(Function, {deserialise_genericparams(), in.read_bool(), in.read_bool(), in.read_istring(), deserialise_type(), deserialise_vec<::HIR::TypeRef>()})
#undef _
        default:
            BUG(Span(), "Bad tag for HIR::TypeRef - " << tag);
    }
    types.push_back(rv);
    return rv;
}

::HIR::SimplePath HirDeserialiser::deserialise_simplepath() {
    TRACE_FUNCTION;
    auto rv = ::HIR::SimplePath{deserialise_thinvec<RcString>()};
    // HACK! If the read crate name is empty, replace it with the name we're loaded with
    if (rv.crate_name() == "" && rv.components().size() > 0) {
        assert(crateName != "");
        rv.update_crate_name(crateName);
    }
    return rv;
}

::HIR::PathParams HirDeserialiser::deserialise_pathparams() {
    ::HIR::PathParams rv;
    TRACE_FUNCTION_FR("", rv);
    rv.mLifetimes = deserialise_thinvec<::HIR::LifetimeRef>();
    rv.types = deserialise_thinvec<::HIR::TypeRef>();
    rv.values = deserialise_thinvec<::HIR::ConstGeneric>();
    return rv;
}

::HIR::GenericPath HirDeserialiser::deserialise_genericpath() {
    ::HIR::GenericPath rv;
    TRACE_FUNCTION_FR("", rv);
    rv.mPath = deserialise_simplepath();
    rv.mParams = deserialise_pathparams();
    return rv;
}

::HIR::TraitPath HirDeserialiser::deserialise_traitpath() {
    auto _ = in.open_object("HIR::TraitPath");
    auto hrls = in.read_bool() ? box$(deserialise_genericparams()) : std::unique_ptr<HIR::GenericParams>();
    auto gpath = deserialise_genericpath();
    auto tys = deserialise_istrmap<::HIR::TraitPath::AtyEqual>();
    auto bounds = deserialise_istrmap<::HIR::TraitPath::AtyBound>();
    auto constness = static_cast<::HIR::BoundConstness>(in.read_u8());
    return ::HIR::TraitPath{std::move(hrls), mv$(gpath), mv$(tys), mv$(bounds), nullptr, constness};
}

::HIR::Path HirDeserialiser::deserialise_path() {
    TRACE_FUNCTION;
    switch (auto tag = in.read_tag()) {
        case 0:
            DEBUG("Generic");
            return ::HIR::Path(deserialise_genericpath());
        case 1:
            DEBUG("Inherent");
            return ::HIR::Path(::HIR::Path::Data::Data_UfcsInherent{deserialise_type(), in.read_istring(), deserialise_pathparams(), deserialise_pathparams()});
        case 2:
        case 3: {
            std::unique_ptr<HIR::GenericParams> hrtbs;
            if (tag == 3) {
                hrtbs = std::make_unique<HIR::GenericParams>(deserialise_genericparams());
            }
            DEBUG("Known");
            return ::HIR::Path(::HIR::Path::Data::Data_UfcsKnown{deserialise_type(), deserialise_genericpath(), in.read_istring(), deserialise_pathparams(), std::move(hrtbs)});
        }
        default:
            BUG(Span(), "Bad tag for HIR::Path - " << tag);
    }
}

::HIR::GenericParams HirDeserialiser::deserialise_genericparams() {
    TRACE_FUNCTION;
    ::HIR::GenericParams params;
    params.types = deserialise_vec<::HIR::TypeParamDef>();
    params.values = deserialise_vec<::HIR::ValueParamDef>();
    params.mLifetimes = deserialise_vec<::HIR::LifetimeDef>();
    params.bounds = deserialise_vec<::HIR::GenericBound>();
    DEBUG("params = " << params.fmt_args() << ", " << params.fmt_bounds());
    return params;
}

::HIR::TypeParamDef HirDeserialiser::deserialise_typaramdef() {
    auto rv = ::HIR::TypeParamDef{in.read_istring(), deserialise_type(), in.read_bool()};
    DEBUG("::HIR::TypeParamDef { " << rv.mName << ", " << rv.defaultValue << ", " << rv.isSized << "}");
    return rv;
}

::HIR::ValueParamDef HirDeserialiser::deserialise_valueparamdef() {
    auto rv = ::HIR::ValueParamDef{in.read_istring(), deserialise_type()};
    rv.defaultValue = deserialise_constgeneric();
    DEBUG("::HIR::ValueParamDef { " << rv.mName << ": " << rv.mType << " = " << rv.defaultValue << "}");
    return rv;
}

::HIR::GenericBound HirDeserialiser::deserialise_genericbound() {
    switch (auto tag = in.read_tag()) {
        case 0:
            return ::HIR::GenericBound::make_Lifetime({deserialise_lifetimeref(), deserialise_lifetimeref()});
        case 1:
            return ::HIR::GenericBound::make_TypeLifetime({deserialise_type(), deserialise_lifetimeref()});
        case 2:
        {
            auto hrtbs = in.read_bool() ? box$(deserialise_genericparams()) : nullptr;
            auto type = deserialise_type();
            auto trait = deserialise_traitpath();
            auto constness = static_cast<::HIR::BoundConstness>(in.read_u8());
            return ::HIR::GenericBound::make_TraitBound({mv$(hrtbs), mv$(type), mv$(trait), constness});
        }
        case 3:
            return ::HIR::GenericBound::make_TypeEquality({deserialise_type(), deserialise_type()});
        default:
            BUG(Span(), "Bad tag for HIR::GenericBound - " << tag);
    }
}

::HIR::Enum HirDeserialiser::deserialise_enum() {
    TRACE_FUNCTION;
    auto _ = in.open_object("HIR::Enum");

    struct H {
        static ::HIR::Enum::Class deserialise_enumclass(HirDeserialiser& des) {
            switch (auto tag = des.in.read_tag()) {
                case ::HIR::Enum::Class::TAG_Data:
                    return ::HIR::Enum::Class::make_Data(des.deserialise_vec<::HIR::Enum::DataVariant>());
                case ::HIR::Enum::Class::TAG_Value:
                    return ::HIR::Enum::Class::make_Value({
                        des.deserialise_vec<::HIR::Enum::ValueVariant>(),
                    });
                default:
                    BUG(Span(), "Bad tag for HIR::Enum::Class - " << tag);
            }
        }
    };

    return ::HIR::Enum{deserialise_genericparams(), in.read_bool(), static_cast<::HIR::Enum::Repr>(in.read_tag()), H::deserialise_enumclass(*this), true, deserialise_markings()};
}

::HIR::Enum::DataVariant HirDeserialiser::deserialise_enumdatavariant() {
    auto name = in.read_istring();
    DEBUG("Enum::DataVariant " << name);
    return ::HIR::Enum::DataVariant{mv$(name), in.read_bool(), deserialise_type(), ::HIR::ExprPtr{}, U128(in.read_u64())};
}

::HIR::Enum::ValueVariant HirDeserialiser::deserialise_enumvaluevariant() {
    auto name = in.read_istring();
    DEBUG("Enum::ValueVariant " << name);
    return ::HIR::Enum::ValueVariant{mv$(name), ::HIR::ExprPtr{}, U128(in.read_u64())};
}

::HIR::Union HirDeserialiser::deserialise_union() {
    TRACE_FUNCTION;
    auto params = deserialise_genericparams();
    auto repr = static_cast<::HIR::Union::Repr>(in.read_tag());
    auto variants = deserialise_vec<HIR::StructField>();
    auto markings = deserialise_markings();

    return ::HIR::Union{mv$(params), repr, mv$(variants), mv$(markings)};
}

::HIR::Struct HirDeserialiser::deserialise_struct() {
    TRACE_FUNCTION_FR("", in.get_pos());
    auto _ = in.open_object("HIR::Struct");
    auto params = deserialise_genericparams();
    DEBUG("params = " << params.fmt_args() << params.fmt_bounds());
    auto repr = static_cast<::HIR::Struct::Repr>(in.read_tag());

    ::HIR::Struct::Data data;
    switch (auto tag = in.read_tag()) {
        case ::HIR::Struct::Data::TAG_Unit:
            DEBUG("Unit");
            data = ::HIR::Struct::Data::make_Unit({});
            break;
        case ::HIR::Struct::Data::TAG_Tuple:
            DEBUG("Tuple");
            data = ::HIR::Struct::Data(deserialise_vec<::HIR::VisEnt<::HIR::TypeRef>>());
            break;
        case ::HIR::Struct::Data::TAG_Named:
            DEBUG("Named");
            data = ::HIR::Struct::Data(deserialise_vec<HIR::StructField>());
            break;
        default:
            BUG(Span(), "Bad tag for HIR::Struct::Data - " << tag);
    }
    unsigned forced_alignment = in.read_count();
    unsigned max_field_alignment = in.read_count();
    DEBUG("align = " << forced_alignment);
    auto markings = deserialise_markings();
    auto str_markings = deserialise_str_markings();

    auto rv = ::HIR::Struct{mv$(params), repr, mv$(data), forced_alignment, mv$(markings), mv$(str_markings)};
    rv.maxFieldAlignment = max_field_alignment;
    return rv;
}

::HIR::StructField HirDeserialiser::deserialise_struct_field() {
    return HIR::StructField{in.read_istring(), deserialise_pub(), deserialise_type(), in.read_bool() ? ::std::make_unique<HIR::GenericPath>(deserialise_genericpath()) : nullptr};
}

::HIR::Trait HirDeserialiser::deserialise_trait() {
    TRACE_FUNCTION;
    auto _ = in.open_object("HIR::Trait");

    ::HIR::Trait rv{
        deserialise_genericparams(),
        ::HIR::LifetimeRef(), // TODO: Better type for lifetime
        {}
    };
    rv.lifetime = deserialise_lifetimeref();
    const auto trait_flags = in.read_u8();
    rv.isMarker = trait_flags & 1;
    rv.isFundamental = trait_flags & 2;
    rv.isCoinductive = (trait_flags & 4) || rv.isMarker;
    rv.isConst = trait_flags & 8;
    rv.types = deserialise_istrumap<::HIR::AssociatedType>();
    rv.values = deserialise_istrumap<::HIR::TraitValueItem>();
    rv.valueIndexes = deserialise_istrummap<::std::pair<unsigned int, ::HIR::GenericPath>>();
    rv.typeIndexes = deserialise_istrumap<unsigned int>();
    rv.vtableParentTraitsStart = in.read_count();
    rv.allParentTraits = deserialise_vec<::HIR::TraitPath>();
    rv.vtablePath = deserialise_simplepath();
    return rv;
}

::HIR::ConstGenericUnevaluated HirDeserialiser::deserialise_constgeneric_unevaluated() {
    auto p_i = deserialise_pathparams();
    auto p_m = deserialise_pathparams();
    auto rv = ::HIR::ConstGenericUnevaluated(deserialise_exprptr());
    rv.params_impl = std::move(p_i);
    rv.params_item = std::move(p_m);
    return rv;
}

::HIR::ConstGeneric HirDeserialiser::deserialise_constgeneric() {
    switch (auto tag = in.read_tag()) {
#define _(x, ...)                      \
    case ::HIR::ConstGeneric::TAG_##x: \
        return ::HIR::ConstGeneric::make_##x(__VA_ARGS__);
        _(Infer, {})
        _(Unevaluated, std::make_unique<HIR::ConstGenericUnevaluated>(deserialise_constgeneric_unevaluated()))
        _(Generic, deserialise_genericref())
        _(Evaluated, HIR::EncodedLiteralPtr(deserialise_encodedliteral()))
#undef _
        default:
            BUG(Span(), "Unknown HIR::ConstGeneric tag when deserialising - " << tag);
    }
}

EncodedLiteral HirDeserialiser::deserialise_encodedliteral() {
    EncodedLiteral rv;
    rv.bytes = deserialise_vec<uint8_t>();

    auto nreloc = in.read_count();
    rv.relocations.reserve(nreloc);
    for (size_t i = 0; i < nreloc; i++) {
        auto ofs = in.read_count();
        auto len = in.read_count();
        switch (in.read_tag()) {
            case 0:
                rv.relocations.push_back(Reloc::new_named(ofs, len, deserialise_path()));
                break;
            case 1:
                rv.relocations.push_back(Reloc::new_bytes(ofs, len, in.read_string()));
                break;
            default:
                abort();
        }
    }
    return rv;
}

::MIR::FunctionPointer HirDeserialiser::deserialise_mir() {
    TRACE_FUNCTION;

    ::MIR::Function rv;

    rv.locals = deserialise_vec<::HIR::TypeRef>();
    //rv.local_names = deserialise_vec< ::std::string>( );
    rv.drop_flags = deserialise_vec<bool>();
    rv.blocks = deserialise_vec<::MIR::BasicBlock>();

    return ::MIR::FunctionPointer(new ::MIR::Function(mv$(rv)));
}

::MIR::BasicBlock HirDeserialiser::deserialise_mir_basicblock() {
    TRACE_FUNCTION;

    auto statements = deserialise_vec<::MIR::Statement>();
    auto terminator = deserialise_mir_terminator();
    const auto is_cleanup = in.read_bool();
    return ::MIR::BasicBlock{mv$(statements), mv$(terminator), is_cleanup};
}

AsmCommon::Options HirDeserialiser::deserialise_asm_options() {
    AsmCommon::Options o;
    const uint16_t bitflag_1 = in.read_u16();
#define BIT(i, fld)             \
    if (bitflag_1 & (1 << (i))) \
    fld = true
    BIT(0, o.pure);
    BIT(1, o.nomem);
    BIT(2, o.readonly);
    BIT(3, o.preserves_flags);
    BIT(4, o.noreturn);
    BIT(5, o.nostack);
    BIT(6, o.att_syntax);
#undef BIT
    return o;
}

AsmCommon::LineFragment HirDeserialiser::deserialise_asm_line_frag() {
    AsmCommon::LineFragment lf;
    lf.before = in.read_string();
    lf.index = in.read_count();
    lf.modifier = static_cast<char>(in.read_i64c());
    return lf;
}

AsmCommon::Line HirDeserialiser::deserialise_asm_line() {
    AsmCommon::Line l;
    l.frags = deserialise_vec<AsmCommon::LineFragment>();
    l.trailing = in.read_string();
    return l;
}

AsmCommon::RegisterSpec HirDeserialiser::deserialise_asm_spec() {
    switch (auto tag = in.read_tag()) {
        case AsmCommon::RegisterSpec::TAG_Class:
            return static_cast<AsmCommon::RegisterClass>(in.read_tag());
        case AsmCommon::RegisterSpec::TAG_Explicit:
            return in.read_string();
        default:
            BUG(Span(), "Bad tag for AsmCommon::RegisterSpec - " << tag);
    }
}

::MIR::AsmParam HirDeserialiser::deserialise_asm_param() {
    switch (auto tag = in.read_tag()) {
        case ::MIR::AsmParam::TAG_Sym:
            return ::MIR::AsmParam::make_Sym(deserialise_path());
        case ::MIR::AsmParam::TAG_Const:
            return ::MIR::AsmParam::make_Const(deserialise_mir_constant());
        case ::MIR::AsmParam::TAG_Reg:
            return ::MIR::AsmParam::make_Reg({static_cast<AsmCommon::Direction>(in.read_tag()), deserialise_asm_spec(), in.read_bool() ? ::std::make_unique<MIR::Param>(deserialise_mir_param()) : std::unique_ptr<MIR::Param>(), in.read_bool() ? ::std::make_unique<MIR::LValue>(deserialise_mir_lvalue()) : std::unique_ptr<MIR::LValue>()});
        default:
            BUG(Span(), "Bad tag for MIR::AsmParam - " << tag);
    }
}

::MIR::Statement HirDeserialiser::deserialise_mir_statement() {
    MIR::Statement rv;
    TRACE_FUNCTION_FR("", rv);
    auto _ = in.open_object("MIR::Statement");

    switch (auto tag = in.read_tag()) {
        case 0:
            rv = ::MIR::Statement::make_Assign({deserialise_mir_lvalue(), deserialise_mir_rvalue()});
            break;
        case 1:
            BUG(Span(), "Obsolete MIR statement Drop in metadata");
        case 2:
            rv = ::MIR::Statement::make_Asm({in.read_string(), deserialise_vec<::std::pair<::std::string, ::MIR::LValue>>(), deserialise_vec<::std::pair<::std::string, ::MIR::LValue>>(), deserialise_vec<::std::string>(), deserialise_vec<::std::string>()});
            break;
        case 3: {
            ::MIR::Statement::Data_SetDropFlag sdf;
            sdf.idx = static_cast<unsigned int>(in.read_count());
            sdf.new_val = in.read_bool();
            sdf.other = static_cast<unsigned int>(in.read_count());
            rv = ::MIR::Statement::make_SetDropFlag(sdf);
        } break;
        case 4:
            rv = ::MIR::Statement::make_ScopeEnd({deserialise_vec<unsigned int>()});
            break;
        case 5:
            rv = ::MIR::Statement::make_Asm2({deserialise_asm_options(), deserialise_vec<AsmCommon::Line>(), deserialise_vec<MIR::AsmParam>()});
            break;
        case 6:
            rv = ::MIR::Statement::make_SaveDropFlag({deserialise_mir_lvalue(), static_cast<unsigned>(in.read_count()), static_cast<unsigned>(in.read_count())});
            break;
        case 7:
            rv = ::MIR::Statement::make_LoadDropFlag({static_cast<unsigned>(in.read_count()), deserialise_mir_lvalue(), static_cast<unsigned>(in.read_count())});
            break;
        default:
            BUG(Span(), "Bad tag for MIR::Statement - " << tag);
    }
    return rv;
}

::MIR::Terminator HirDeserialiser::deserialise_mir_terminator() {
    ::MIR::Terminator rv;
    TRACE_FUNCTION_FR("", rv);
    rv = this->deserialise_mir_terminator_();
    return rv;
}

::MIR::Terminator HirDeserialiser::deserialise_mir_terminator_() {
    switch (auto tag = in.read_tag()) {
#define _(x, ...)                    \
    case ::MIR::Terminator::TAG_##x: \
        return ::MIR::Terminator::make_##x(__VA_ARGS__);
        case MIR::Terminator::TAGDEAD:
            BUG(Span(), "MIR::Terminator::TAGDEAD found");
            _(Incomplete, {})
            _(Return, {})
            _(UnwindResume, {})
            _(UnwindTerminate, {})
            _(Unreachable, {})
            _(Goto, static_cast<unsigned int>(in.read_count()))
            _(If, {deserialise_mir_lvalue(), static_cast<unsigned int>(in.read_count()), static_cast<unsigned int>(in.read_count())})
            _(Switch,
              {deserialise_mir_lvalue(),
               deserialise_vec_c<unsigned int>([&]() {
                return static_cast<unsigned int>(in.read_count());
            }),
               static_cast<unsigned int>(in.read_count()),
               static_cast<unsigned int>(in.read_count())})
            _(SwitchValue,
              {deserialise_mir_lvalue(),
               static_cast<unsigned int>(in.read_count()),
               deserialise_vec_c<unsigned int>([&]() {
                return static_cast<unsigned int>(in.read_count());
            }),
               deserialise_mir_switchvalues()})
            _(Drop, {static_cast<::MIR::eDropKind>(in.read_tag()), deserialise_mir_lvalue(), static_cast<unsigned int>(in.read_count()), static_cast<unsigned int>(in.read_count()), deserialise_mir_unwind_action()})
            _(Call, {static_cast<unsigned int>(in.read_count()), deserialise_mir_unwind_action(), deserialise_mir_lvalue(), deserialise_mir_calltarget(), deserialise_vec<::MIR::Param>()})
#undef _
        default:
            BUG(Span(), "Bad tag for MIR::Terminator - " << tag);
    }
}

::MIR::UnwindAction HirDeserialiser::deserialise_mir_unwind_action() {
    switch (auto tag = in.read_tag()) {
        case ::MIR::UnwindAction::TAG_Continue:
            return ::MIR::UnwindAction::make_Continue({});
        case ::MIR::UnwindAction::TAG_Cleanup:
            return ::MIR::UnwindAction::make_Cleanup(static_cast<unsigned int>(in.read_count()));
        case ::MIR::UnwindAction::TAG_Terminate:
            return ::MIR::UnwindAction::make_Terminate({});
        case ::MIR::UnwindAction::TAG_Unreachable:
            return ::MIR::UnwindAction::make_Unreachable({});
        default:
            BUG(Span(), "Bad tag for MIR::UnwindAction - " << tag);
    }
}

::MIR::SwitchValues HirDeserialiser::deserialise_mir_switchvalues() {
    TRACE_FUNCTION;
    switch (auto tag = in.read_tag()) {
#define _(x, ...)                      \
    case ::MIR::SwitchValues::TAG_##x: \
        return ::MIR::SwitchValues::make_##x(__VA_ARGS__);
        _(Unsigned, deserialise_vec_c<uint64_t>([&]() {
            return in.read_u64c();
        }))
        _(Signed, deserialise_vec_c<int64_t>([&]() {
            return in.read_i64c();
        }))
        _(String, deserialise_vec<::std::string>())
        _(ByteString, deserialise_vec<::std::vector<uint8_t>>())
#undef _
        default:
            BUG(Span(), "Bad tag for MIR::SwitchValues - " << tag);
    }
}

::MIR::CallTarget HirDeserialiser::deserialise_mir_calltarget() {
    switch (auto tag = in.read_tag()) {
#define _(x, ...)                    \
    case ::MIR::CallTarget::TAG_##x: \
        return ::MIR::CallTarget::make_##x(__VA_ARGS__);
        _(Value, deserialise_mir_lvalue())
        _(Path, deserialise_path())
        _(Intrinsic, {in.read_istring(), deserialise_pathparams()})
#undef _
        default:
            BUG(Span(), "Bad tag for MIR::CallTarget - " << tag);
    }
}

::HIR::Module HirDeserialiser::deserialise_module() {
    TRACE_FUNCTION;
    auto _ = in.open_object("HIR::Module");

    ::HIR::Module rv;

    // m_traits doesn't need to be serialised
    rv.valueItems = deserialise_istrumap<::std::unique_ptr<::HIR::VisEnt<::HIR::ValueItem>>>();
    rv.modItems = deserialise_istrumap<::std::unique_ptr<::HIR::VisEnt<::HIR::TypeItem>>>();
    rv.macroItems = deserialise_istrumap<::std::unique_ptr<::HIR::VisEnt<::HIR::MacroItem>>>();

    return rv;
}

::HIR::ExternLibrary HirDeserialiser::deserialise_extlib() {
    return ::HIR::ExternLibrary{in.read_string()};
}

void HirDeserialiser::deserialise_crate(::HIR::Crate& rv) {
    // NOTE: This MUST be the first item
    this->crateName = in.read_istring();
    assert(this->crateName != "" && "Empty crate name loaded from metadata");
    g_vis_private = ::HIR::Publicity::new_priv(::HIR::SimplePath(this->crateName));
    rv.crateName = this->crateName;
    rv.edition = static_cast<AST::Edition>(in.read_tag());
    rv.rootModule = deserialise_module();

    rv.typeImpls = D<::HIR::Crate::ImplGroup<std::unique_ptr<::HIR::TypeImpl>>>::des(*this);
    rv.traitImpls = deserialise_pathmap<::HIR::Crate::ImplGroup<std::unique_ptr<::HIR::TraitImpl>>>();
    rv.markerImpls = deserialise_pathmap<::HIR::Crate::ImplGroup<std::unique_ptr<::HIR::MarkerImpl>>>();

    rv.exportedMacroNames = deserialise_vec<::RcString>();
    //rv.m_exported_macros = deserialise_istrumap< ::MacroRulesPtr>();
    //rv.m_proc_macro_reexports = deserialise_istrumap< ::HIR::Crate::MacroImport>();
    rv.mLangItems = deserialise_strumap<::HIR::SimplePath>();

    {
        size_t n = in.read_count();
        for (size_t i = 0; i < n; i++) {
            auto ext_crate_name = in.read_istring();
            auto ext_crate_file = in.read_string();
            auto ext_crate = ::HIR::ExternCrate{};
            ext_crate.basename = ext_crate_file;
            ext_crate.mPath = ext_crate_file;
            rv.extCrates.insert(::std::make_pair(mv$(ext_crate_name), mv$(ext_crate)));
        }
    }

    rv.extLibs = deserialise_vec<::HIR::ExternLibrary>();
    rv.linkPaths = deserialise_vec<::std::string>();

    //rv.m_proc_macros = deserialise_vec< ::HIR::ProcMacro>();
}

//}

::HIR::Crate* HIRDeserialise(stl::ObjPool* pool, ::HIR::TypeInterner& types, const ::std::string& filename) {
    try {
        ::HIR::serialise::Reader in{filename + ".hir"}; // Callers pass the metadata basename, without its suffix.
        HirDeserialiser s{in, types};

        auto* rv = pool->make<::HIR::Crate>(pool, types);
        s.deserialise_crate(*rv);
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
        ::HIR::serialise::Reader in{filename + ".hir"}; // Callers pass the metadata basename, without its suffix.

        // NOTE: This is the first item loaded by deserialise_crate
        auto crate_name = in.read_istring();
        assert(crate_name != "" && "Empty crate name loaded from metadata");
        return crate_name;
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


#define NODE_IS(valptr, tysuf) (cast<const ::HIR::ExprNode##tysuf>(&*valptr) != nullptr)

namespace {

    class TreeVisitor: public ::HIR::Visitor, public ::HIR::ExprVisitor {
        ::std::ostream& os;
        unsigned int indentLevel;

    public:
        TreeVisitor(::HIR::TypeInterner& types, ::std::ostream& os)
            : ::HIR::Visitor(nullptr, types)
            , os(os)
            , indentLevel(0)
        {
        }

        void visit_module(::HIR::ItemPath p, ::HIR::Module& mod) override {
            if (p.get_name()[0]) {
                os << indent() << "mod " << p.get_name() << " {\n";
                inc_indent();
            }

            // TODO: Include trait list
            if (true) {
                for (const auto& t : mod.traits) {
                    os << indent() << "use " << t << ";\n";
                }
            }
            // TODO: Print publicitiy.
            ::HIR::Visitor::visit_module(p, mod);

            if (p.get_name()[0]) {
                dec_indent();
                os << indent() << "}\n";
            }
        }

        void visit_type_impl(::HIR::TypeImpl& impl) override {
            os << indent() << "impl" << impl.mParams.fmt_args() << " " << impl.mType << "\n";
            if (!impl.mParams.bounds.empty()) {
                os << indent() << " " << impl.mParams.fmt_bounds() << "\n";
            }
            os << indent() << "{\n";
            inc_indent();
            ::HIR::Visitor::visit_type_impl(impl);
            dec_indent();
            os << indent() << "}\n";
        }

        virtual void visit_trait_impl(const ::HIR::SimplePath& trait_path, ::HIR::TraitImpl& impl) override {
            os << indent() << "impl" << impl.mParams.fmt_args() << " " << trait_path << impl.traitArgs << " for " << impl.mType << "\n";
            if (!impl.mParams.bounds.empty()) {
                os << indent() << " " << impl.mParams.fmt_bounds() << "\n";
            }
            os << indent() << "{\n";
            inc_indent();
            for (auto& ent : impl.types) {
                os << indent() << "type " << ent.first << " = " << ent.second.data << "\n";
            }
            ::HIR::Visitor::visit_trait_impl(trait_path, impl);
            dec_indent();
            os << indent() << "}\n";
        }

        void visit_marker_impl(const ::HIR::SimplePath& trait_path, ::HIR::MarkerImpl& impl) override {
            os << indent() << "impl" << impl.mParams.fmt_args() << " " << (impl.is_positive ? "" : "!") << trait_path << impl.traitArgs << " for " << impl.mType << "\n";
            if (!impl.mParams.bounds.empty()) {
                os << indent() << " " << impl.mParams.fmt_bounds() << "\n";
            }
            os << indent() << "{ }\n";
        }

        // - Type Items
        void visit_type_alias(::HIR::ItemPath p, ::HIR::TypeAlias& item) override {
            os << indent() << "type " << p.get_name() << item.mParams.fmt_args() << " = " << item.mType << item.mParams.fmt_bounds() << "\n";
        }

        void visit_inherent_type(::HIR::ItemPath p, ::HIR::TypeAlias& item) override {
            this->visit_type_alias(p, item);
        }

        void visit_trait(::HIR::ItemPath p, ::HIR::Trait& item) override {
            os << indent() << "trait " << p.get_name() << item.mParams.fmt_args() << " : " << item.lifetime << "\n";
            if (!item.parentTraits.empty()) {
                os << indent() << "  " << ": ";
                bool is_first = true;
                for (auto& bound : item.parentTraits) {
                    if (!is_first) {
                        os << indent() << "  " << "+ ";
                    }
                    os << bound << "\n";
                    is_first = false;
                }
            }
            if (!item.mParams.bounds.empty()) {
                os << indent() << " " << item.mParams.fmt_bounds() << "\n";
            }
            if (!item.allParentTraits.empty()) {
                os << indent() << "/* All parent traits:\n";
                for (const auto& t : item.allParentTraits) {
                    os << indent() << t << "\n";
                }
                os << indent() << "*/\n";
            }
            os << indent() << "{\n";
            inc_indent();

            for (auto& i : item.types) {
                os << indent() << "type " << i.first;
                if (!i.second.traitBounds.empty()) {
                    os << ": ";
                    bool is_first = true;
                    for (auto& bound : i.second.traitBounds) {
                        if (!is_first) {
                            os << " + ";
                        }
                        os << bound;
                        is_first = false;
                    }
                }
                //this->visit_type(i.second.m_default);
                os << ";\n";
            }

            ::HIR::Visitor::visit_trait(p, item);

            dec_indent();
            os << indent() << "}\n";
        }

        void visit_struct(::HIR::ItemPath p, ::HIR::Struct& item) override {
            os << indent() << "struct " << p.get_name() << item.mParams.fmt_args();
            TU_MATCH_HDRA( (item.mData), {)
            TU_ARMA(Unit, flds) {
                    if (item.mParams.bounds.empty()) {
                        os << ";\n";
                    } else {
                        os << "\n";
                        os << indent() << " " << item.mParams.fmt_bounds() << "\n";
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
                        os << indent() << " " << item.mParams.fmt_bounds() << "\n";
                        os << indent() << "    ;\n";
                    }
                }
                TU_ARMA(Named, flds) {
                    os << "\n";
                    if (!item.mParams.bounds.empty()) {
                        os << indent() << " " << item.mParams.fmt_bounds() << "\n";
                    }
                    os << indent() << "{\n";
                    inc_indent();
                    for (const auto& fld : flds) {
                        os << indent() << fld.vis << " " << fld.name << ": " << fld.ty;
                        if (fld.default_value) {
                            os << " = " << *fld.default_value;
                        }
                        os << ",\n";
                    }
                    dec_indent();
                    os << indent() << "}\n";
                }
            }
        }

        void visit_enum(::HIR::ItemPath p, ::HIR::Enum& item) override {
            os << indent() << "enum " << p.get_name() << item.mParams.fmt_args() << "\n";
            if (!item.mParams.bounds.empty()) {
                os << indent() << " " << item.mParams.fmt_bounds() << "\n";
            }
            os << indent() << "{\n";
            inc_indent();
            if (const auto* e = item.mData.opt_Value()) {
                for (const auto& var : e->variants) {
                    os << indent() << var.name;
                    os << ",\n";
                }
            } else {
                for (const auto& var : item.mData.as_Data()) {
                    os << indent() << var.name;
                    if (var.type == type_interner().unit()) {
                    } else {
                        os << " " << var.type << (var.is_struct ? "/*struct*/" : "");
                    }
                    os << ",\n";
                }
            }
            dec_indent();
            os << indent() << "}\n";
        }

        // - Value Items
        void visit_function(::HIR::ItemPath p, ::HIR::Function& item) override {
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
            os << "fn " << p.get_name() << item.mParams.fmt_args() << "(";
            for (const auto& arg : item.mArgs) {
                os << arg.first << ": " << arg.second << ", ";
            }
            os << ") -> " << item.returnType << "\n";
            if (!item.mParams.bounds.empty()) {
                os << indent() << " " << item.mParams.fmt_bounds() << "\n";
            }

            if (item.mCode) {
                os << indent();
                if (cast<::HIR::ExprNodeBlock>(&*item.mCode)) {
                    item.mCode->visit(*this);
                } else {
                    os << "{\n";
                    inc_indent();
                    os << indent();

                    item.mCode->visit(*this);

                    os << "\n";
                    dec_indent();
                    os << indent();
                    os << "}";
                }
                os << "\n";
            } else {
                os << indent() << "  ;\n";
            }
        }

        void visit_static(::HIR::ItemPath p, ::HIR::Static& item) override {
            if (item.linkage.name != "") {
                os << indent() << "#[link_name=\"" << item.linkage.name << "\"]\n";
            }
            if (item.mValue) {
                os << indent() << "static " << p.get_name() << item.mParams.fmt_args() << ": " << item.mType << " = " << item.valueRes;
            } else if (item.valueGenerated) {
                os << indent() << "static " << p.get_name() << item.mParams.fmt_args() << ": " << item.mType << " = /*magic*/ " << item.valueRes;
            } else {
                os << indent() << "extern static " << p.get_name() << ": " << item.mType;
            }
            if (!item.mParams.bounds.empty()) {
                os << indent() << " " << item.mParams.fmt_bounds() << "\n";
            }
            os << ";\n";
        }

        void visit_constant(::HIR::ItemPath p, ::HIR::Constant& item) override {
            os << indent() << "const " << p.get_name() << ": " << item.mType << " = " << item.valueRes;
            if (item.mValue /*&& item.m_value_state != HIR::Constant::ValueState::Known*/) {
                os << " /*= ";
                item.mValue->visit(*this);
                os << "*/";
            }
            os << ";\n";
        }

// - Misc

        bool node_is_leaf(const ::HIR::ExprNode& node) {
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

        void visit_node_ptr(::HIR::ExprNodeP& node_ptr) override {
            HIR::ExprVisitor::visit_node_ptr(node_ptr);
            os << "/*: " << node_ptr->resType << " */";
        }

        void visit(::HIR::ExprNodeBlock& node) override {
            os << "{\n";
            inc_indent();
            for (auto& sn : node.nodes) {
                os << indent();
                this->visit_node_ptr(sn);
                os << ";\n";
            }
            if (node.valueNode) {
                os << indent();
                this->visit_node_ptr(node.valueNode);
                os << "\n";
            }
            dec_indent();
            os << indent() << "}";
        }

        void visit(::HIR::ExprNodeConstBlock& node) override {
            os << "const ";
            node.inner->visit(*this);
        }

        void visit(::HIR::ExprNodeAsm& node) override {
            os << "llvm_asm!(";
            os << ")";
        }

        void visit(::HIR::ExprNodeAsm2& node) override {
            os << "asm!(";
            os << ")";
        }

        void visit(::HIR::ExprNodeReturn& node) override {
            os << "return";
            if (node.mValue) {
                os << " ";
                this->visit_node_ptr(node.mValue);
            }
        }

        void visit(::HIR::ExprNodeYield& node) override {
            os << "yield";
            if (node.mValue) {
                os << " ";
                this->visit_node_ptr(node.mValue);
            }
        }

        void visit(::HIR::ExprNodeAWait& node) override {
            os << "(";
            this->visit_node_ptr(node.mValue);
            os << ").await";
        }

        void visit(::HIR::ExprNodeLet& node) override {
            os << "let " << node.pattern << ": " << node.mType;
            if (node.mValue) {
                os << " = ";
                this->visit_node_ptr(node.mValue);
            }
            os << ";";
        }

        void visit(::HIR::ExprNodeLoop& node) override {
            if (node.label != "") {
                os << "'" << node.label << ": ";
            }
            os << "loop ";
            this->visit_node_ptr(node.mCode);
        }

        void visit(::HIR::ExprNodeLoopControl& node) override {
            os << (node.isContinue ? "continue" : "break");
            if (node.label != "") {
                os << " '" << node.label;
            }
            if (node.mValue) {
                os << " ";
                this->visit_node_ptr(node.mValue);
            }
        }

        void visit(::HIR::ExprNodeMatch& node) override {
            os << "match ";
            this->visit_node_ptr(node.mValue);
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
                        this->visit_node_ptr(c.val);
                    }
                }
                os << " => ";
                inc_indent();
                this->visit_node_ptr(arm.mCode);
                dec_indent();
                os << ",\n";
            }
            os << indent() << "}";
        }

        void visit(::HIR::ExprNodeAssign& node) override {
            this->visit_node_ptr(node.slot);
            os << " " << ::HIR::ExprNodeAssign::opname(node.op) << "= ";
            this->visit_node_ptr(node.mValue);
        }

        void visit(::HIR::ExprNodeBinOp& node) override {
            os << "(";
            this->visit_node_ptr(node.left);
            os << ")";
            os << " " << ::HIR::ExprNodeBinOp::opname(node.op) << " ";
            os << "(";
            this->visit_node_ptr(node.right);
            os << ")";
        }

        void visit(::HIR::ExprNodeUniOp& node) override {
            switch (node.op) {
                case ::HIR::ExprNodeUniOp::Op::Invert:
                    os << "!";
                    break;
                case ::HIR::ExprNodeUniOp::Op::Negate:
                    os << "-";
                    break;
            }
            os << "(";
            this->visit_node_ptr(node.mValue);
            os << ")";
        }

        void visit(::HIR::ExprNodeBorrow& node) override {
            os << "&";
            switch (node.mType) {
                case ::HIR::BorrowType::Shared:
                    break;
                case ::HIR::BorrowType::Unique:
                    os << "mut ";
                    break;
                case ::HIR::BorrowType::Owned:
                    os << "move ";
                    break;
            }

            bool skip_parens = this->node_is_leaf(*node.mValue) || NODE_IS(node.mValue, Deref);
            if (!skip_parens) {
                os << "(";
            }
            this->visit_node_ptr(node.mValue);
            if (!skip_parens) {
                os << ")";
            }
        }

        void visit(::HIR::ExprNodeRawBorrow& node) override {
            os << "&raw ";
            switch (node.mType) {
                case ::HIR::BorrowType::Shared:
                    break;
                case ::HIR::BorrowType::Unique:
                    os << "mut ";
                    break;
                case ::HIR::BorrowType::Owned:
                    os << "move ";
                    break;
            }

            bool skip_parens = this->node_is_leaf(*node.mValue) || NODE_IS(node.mValue, Deref);
            if (!skip_parens) {
                os << "(";
            }
            this->visit_node_ptr(node.mValue);
            if (!skip_parens) {
                os << ")";
            }
        }

        void visit(::HIR::ExprNodeCast& node) override {
            this->visit_node_ptr(node.mValue);
            os << " as " << node.dstType;
        }

        void visit(::HIR::ExprNodeUnsize& node) override {
            this->visit_node_ptr(node.mValue);
            os << " : " << node.dstType;
        }

        void visit(::HIR::ExprNodeIndex& node) override {
            // TODO: Avoid parens
            os << "(";
            this->visit_node_ptr(node.mValue);
            os << ")";
            os << "[";
            this->visit_node_ptr(node.index);
            os << "]";
        }

        void visit(::HIR::ExprNodeDeref& node) override {
            os << "*";

            bool skip_parens = this->node_is_leaf(*node.mValue);
            if (!skip_parens) {
                os << "(";
            }
            this->visit_node_ptr(node.mValue);
            if (!skip_parens) {
                os << ")";
            }
        }

        void visit(::HIR::ExprNodeEmplace& node) override {
            if (node.mType == ::HIR::ExprNodeEmplace::Type::Noop) {
                return node.mValue->visit(*this);
            }
            os << "(";
            this->visit_node_ptr(node.place);
            os << " <- ";
            this->visit_node_ptr(node.mValue);
            os << ")";
            os << "/*" << (node.mType == ::HIR::ExprNodeEmplace::Type::Boxer ? "box" : "place") << "*/";
        }

        void visit(::HIR::ExprNodeTupleVariant& node) override {
            os << node.mPath;
            os << "(";
            for (/*const*/ auto& arg : node.mArgs) {
                this->visit_node_ptr(arg);
                os << ", ";
            }
            os << ")";
        }

        void visit(::HIR::ExprNodeCallPath& node) override {
            os << node.mPath;
            os << "(";
            for (/*const*/ auto& arg : node.mArgs) {
                this->visit_node_ptr(arg);
                os << ", ";
            }
            os << ")";
            os << "/* : " << node.resType << " */";
        }

        void visit(::HIR::ExprNodeCallValue& node) override {
            // TODO: Avoid brackets if not needed
            os << "(";
            this->visit_node_ptr(node.mValue);
            os << ")";
            os << "(";
            for (/*const*/ auto& arg : node.mArgs) {
                this->visit_node_ptr(arg);
                os << ", ";
            }
            os << ")";
        }

        void visit(::HIR::ExprNodeCallMethod& node) override {
            // TODO: Avoid brackets if not needed
            os << "(";
            this->visit_node_ptr(node.mValue);
            os << ")";
            os << "." << node.method << node.mParams << "(";
            for (/*const*/ auto& arg : node.mArgs) {
                this->visit_node_ptr(arg);
                os << ", ";
            }
            os << ")";
            if (!node.cache.argTypes.empty()) {
                os << "/*CACHE:" << node.cache.argTypes << "*/";
            }
        }

        void visit(::HIR::ExprNodeField& node) override {
            // TODO: Avoid brackets if not needed
            os << "(";
            this->visit_node_ptr(node.mValue);
            os << ")";
            os << "." << node.field;
        }

        void visit(::HIR::ExprNodeLiteral& node) override {
            TU_MATCH_HDRA( (node.mData), {)
            TU_ARMA(Integer, e) {
                    switch (e.mType) {
                        case ::HIR::CoreType::U8:
                            os << e.mValue << "_u8";
                            break;
                        case ::HIR::CoreType::U16:
                            os << e.mValue << "_u16";
                            break;
                        case ::HIR::CoreType::U32:
                            os << e.mValue << "_u32";
                            break;
                        case ::HIR::CoreType::U64:
                            os << e.mValue << "_u64";
                            break;
                        case ::HIR::CoreType::Usize:
                            os << e.mValue << "_usize";
                            break;
                        case ::HIR::CoreType::I8:
                            os << /*I128*/ (e.mValue) << "_i8";
                            break;
                        case ::HIR::CoreType::I16:
                            os << /*I128*/ (e.mValue) << "_i16";
                            break;
                        case ::HIR::CoreType::I32:
                            os << /*I128*/ (e.mValue) << "_i32";
                            break;
                        case ::HIR::CoreType::I64:
                            os << /*I128*/ (e.mValue) << "_i64";
                            break;
                        case ::HIR::CoreType::Isize:
                            os << /*I128*/ (e.mValue) << "_isize";
                            break;
                        case ::HIR::CoreType::Char: {
                            auto v = e.mValue.truncate_u64();
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
                        case ::HIR::CoreType::F32:
                            os << e.mValue << "_f32";
                            break;
                        case ::HIR::CoreType::F64:
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

        void visit(::HIR::ExprNodeUnitVariant& node) override {
            os << node.mPath;
        }

        void visit(::HIR::ExprNodePathValue& node) override {
            os << node.mPath;
        }

        void visit(::HIR::ExprNodeVariable& node) override {
            os << node.mName << "#" << node.slot;
        }

        void visit(::HIR::ExprNodeConstParam& node) override {
            os << node.mName << "#" << node.mBinding;
        }

        void visit(::HIR::ExprNodeStructLiteral& node) override {
            os << node.mType << " {\n";
            inc_indent();
            for (/*const*/ auto& val : node.values) {
                os << indent() << val.first << ": ";
                this->visit_node_ptr(val.second);
                os << ",\n";
            }
            if (node.baseValue) {
                os << indent() << ".. ";
                this->visit_node_ptr(node.baseValue);
                os << "\n";
            }
            os << indent() << "}";
            dec_indent();
        }

        void visit(::HIR::ExprNodeTuple& node) override {
            os << "(";
            for (/*const*/ auto& val : node.vals) {
                this->visit_node_ptr(val);
                os << ", ";
            }
            os << ")";
        }

        void visit(::HIR::ExprNodeArrayList& node) override {
            os << "[";
            for (/*const*/ auto& val : node.vals) {
                this->visit_node_ptr(val);
                os << ", ";
            }
            os << "]";
        }

        void visit(::HIR::ExprNodeArraySized& node) override {
            os << "[";
            this->visit_node_ptr(node.val);
            os << "; " << node.mSize;
            os << "]";
        }

        void visit(::HIR::ExprNodeClosure& node) override {
            if (node.mCode) {
                if (node.isMove) {
                    os << " move";
                }
                os << "|";
                for (const auto& arg : node.mArgs) {
                    os << arg.first << ": " << arg.second << ", ";
                }
                os << "| -> " << node.returnType << " ";
                this->visit_node_ptr(node.mCode);
            } else {
                os << node.objPath << "( ";
                for (/*const*/ auto& cap : node.captures) {
                    this->visit_node_ptr(cap);
                    os << ", ";
                }
                os << ")";
            }
        }

        void visit(::HIR::ExprNodeGenerator& node) override {
            if (node.mCode) {
                os << "/*gen*/";
                if (node.isPinned) {
                    os << "static ";
                }
                if (node.isMove) {
                    os << " move";
                }
                os << "|";
                //for(const auto& arg : node.m_args)
                //    m_os << arg.first << ": " << arg.second << ", ";
                os << "| -> " << node.returnType << " ";
                this->visit_node_ptr(node.mCode);
            } else {
                os << node.objPath << "( ";
                for (/*const*/ auto& cap : node.captures) {
                    this->visit_node_ptr(cap);
                    os << ", ";
                }
                os << ")";
            }
        }

        void visit(::HIR::ExprNodeGeneratorWrapper& node) override {
            os << "/*gen body*/";
            os << "|";
            //for(const auto& arg : node.m_args)
            //    m_os << arg.first << ": " << arg.second << ", ";
            os << "| -> " << node.returnType << " ";
            this->visit_node_ptr(node.mCode);
        }

        void visit(::HIR::ExprNodeAsyncBlock& node) override {
            if (node.isMove) {
                os << "move ";
            }
            os << "async {";
            if (!node.mCode) {
                os << "/* lowered: " << node.objPath << " */";
            } else {
                this->visit_node_ptr(node.mCode);
            }
            os << "}";
        }

    private:
        RepeatLitStr indent() const {
            return RepeatLitStr{"    ", static_cast<int>(indentLevel)};
        }

        void inc_indent() {
            indentLevel++;
        }

        void dec_indent() {
            indentLevel--;
        }
    };
}

void HIRDump(::std::ostream& sink, const ::HIR::Crate& crate) {
    TreeVisitor tv{crate.types, sink};

    tv.visit_crate(const_cast<::HIR::Crate&>(crate));
}

void HIRDumpExpr(::std::ostream& sink, const ::HIR::ExprPtr& expr) {
    if (!expr) {
        sink << "/*NULL*/";
        return;
    }

    assert(expr.state);
    TreeVisitor tv{expr.state->types, sink};

    const_cast<HIR::ExprPtr&>(expr)->visit(tv);
}

#undef NODE_IS


//namespace {
class HirSerialiser {
    ::std::map<std::string, size_t> types;
    ::HIR::serialise::Writer& out;
    ::HIR::TypeInterner& typeInterner;

public:
    HirSerialiser(::HIR::serialise::Writer& out, ::HIR::TypeInterner& type_interner)
        : out(out)
        , typeInterner(type_interner)
    {
    }

    void clear() {
        types.clear();
    }

    template <typename V>
    void serialise_strmap(const ::std::map<RcString, V>& map) {
        out.write_count(map.size());
        for (const auto& v : map) {
            DEBUG(v.first);
            out.write_string(v.first);
            serialise(v.second);
        }
    }

    template <typename V>
    void serialise_strmap(const ::std::map<::std::string, V>& map) {
        out.write_count(map.size());
        for (const auto& v : map) {
            out.write_string(v.first);
            serialise(v.second);
        }
    }

    template <typename V>
    void serialise_pathmap(const ::std::map<::HIR::SimplePath, V>& map) {
        out.write_count(map.size());
        for (const auto& v : map) {
            DEBUG("- " << v.first);
            serialise(v.first);
            serialise(v.second);
        }
    }

    template <typename V>
    void serialise_strmap(const ::std::unordered_map<RcString, V>& map) {
        out.write_count(map.size());
        for (const auto& v : map) {
            DEBUG("- " << v.first);
            out.write_string(v.first);
            serialise(v.second);
        }
    }

    template <typename V>
    void serialise_strmap(const ::std::unordered_map<::std::string, V>& map) {
        out.write_count(map.size());
        for (const auto& v : map) {
            DEBUG("- " << v.first);
            out.write_string(v.first);
            serialise(v.second);
        }
    }

    template <typename V>
    void serialise_strmap(const ::std::unordered_multimap<RcString, V>& map) {
        out.write_count(map.size());
        for (const auto& v : map) {
            DEBUG("- " << v.first);
            out.write_string(v.first);
            serialise(v.second);
        }
    }

    template <typename V>
    void serialise_strmap(const ::std::unordered_multimap<::std::string, V>& map) {
        out.write_count(map.size());
        for (const auto& v : map) {
            DEBUG("- " << v.first);
            out.write_string(v.first);
            serialise(v.second);
        }
    }

    template <typename T>
    void serialise_vec(const ThinVector<T>& vec) {
        TRACE_FUNCTION_F("<" << typeid(T).name() << "> size=" << vec.size());
        auto _ = out.open_object(typeid(ThinVector<T>).name());
        out.write_count(vec.size());
        for (const auto& i : vec) {
            serialise(i);
        }
    }

    template <typename T>
    void serialise_vec(const ::std::vector<T>& vec) {
        TRACE_FUNCTION_F("<" << typeid(T).name() << "> size=" << vec.size());
        auto _ = out.open_object(typeid(::std::vector<T>).name());
        out.write_count(vec.size());
        for (const auto& i : vec) {
            serialise(i);
        }
    }

    template <typename T>
    void serialise(const ::std::vector<T>& vec) {
        serialise_vec(vec);
    }

    template <typename T>
    void serialise(const ::std::set<T>& s) {
        TRACE_FUNCTION_F("size=" << s.size());
        auto _ = out.open_object(typeid(::std::set<T>).name());
        out.write_count(s.size());
        for (const auto& i : s) {
            serialise(i);
        }
    }

    void serialise(const ::HIR::Publicity& pub) {
        out.write_bool(pub.is_global());
    }

    template <typename T>
    void serialise(const ::HIR::VisEnt<T>& e) {
        serialise(e.publicity);
        serialise(e.ent);
    }

    template <typename T>
    void serialise(const ::std::unique_ptr<T>& e) {
        serialise(*e);
    }

    template <typename T>
    void serialise(const ::std::pair<::std::string, T>& e) {
        out.write_string(e.first);
        serialise(e.second);
    }

    template <typename T>
    void serialise(const ::std::pair<RcString, T>& e) {
        out.write_string(e.first);
        serialise(e.second);
    }

    template <typename T>
    void serialise(const ::std::pair<unsigned int, T>& e) {
        out.write_count(e.first);
        serialise(e.second);
    }

    //void serialise(::MIR::BasicBlockId val) {
    //    m_out.write_count(val);
    //}

    void serialise(bool v) {
        out.write_bool(v);
    };

    void serialise(unsigned int v) {
        out.write_count(v);
    };

    void serialise(uint8_t v) {
        out.write_u8(v);
    };

    void serialise(uint64_t v) {
        out.write_u64c(v);
    };

    void serialise(int64_t v) {
        out.write_i64c(v);
    };

    void serialise(const ::HIR::LifetimeDef& ld) {
        out.write_string(ld.mName);
    }

    void serialise(const ::HIR::LifetimeRef& lr) {
        out.write_count(lr.binding);
    }

    void serialise(const ::HIR::GenericRef& ge) {
        out.write_string(ge.name);
        out.write_u16(ge.binding);
    }

    void serialise_arraysize(const ::HIR::ArraySize& as) {
        out.write_tag(static_cast<int>(as.tag()));
            TU_MATCH_HDRA( (as), { )
            TU_ARMA(Unevaluated, se) {
                serialise(se);
            }
            TU_ARMA(Known, se) {
                out.write_u64c(se);
            }
            }
    }

    void serialise_type(const ::HIR::TypeData* ty) {
        // Use string comparison to ensure that lifetimes are checked
        auto ty_str = FMT(ty);
        if (ty_str[0] == '{') {
            auto p = ty_str.find('}');
            ty_str = ty_str.substr(p + 1);
        }

        auto it = types.find(ty_str);
        if (it != types.end()) {
            DEBUG("Cached " << it->second);
            out.write_count(it->second);
            return;
        }
        out.write_count(~0u);
        DEBUG("Fresh " << types.size());

        auto _ = out.open_object("HIR::TypeData");
        out.write_tag(ty->tag());
            TU_MATCH_HDRA( (*ty), {)
            TU_ARMA(Infer, e) {
                // BAAD
            }
            TU_ARMA(Diverge, e) {
            }
            TU_ARMA(Primitive, e) {
                out.write_tag(static_cast<int>(e));
            }
            TU_ARMA(Path, e) {
                serialise_path(e.path);
                out.write_bool(e.hrtbs.get() != nullptr);
                if (e.hrtbs) {
                    serialise_generics(*e.hrtbs);
                }
            }
            TU_ARMA(Generic, e) {
                serialise(e);
            }
            TU_ARMA(TraitObject, e) {
                serialise_traitpath(e.mTrait);
                serialise_vec(e.markers);
                serialise(e.lifetime);
            }
            TU_ARMA(ErasedType, e) {
                TODO(Span(), "Serialse ErasedType?");
                //serialise_path(e.m_origin);
                //m_out.write_count(e.m_index);

                out.write_bool(e.isSized);
                serialise_vec(e.traits);
                serialise_vec(e.lifetimeBounds);
                serialise_pathparams(e.use);
            }
            TU_ARMA(Array, e) {
                serialise_type(e.inner);
                serialise_arraysize(e.size);
            }
            TU_ARMA(Slice, e) {
                serialise_type(e.inner);
            }
            TU_ARMA(Tuple, e) {
                serialise_vec(e);
            }
            TU_ARMA(Borrow, e) {
                serialise(e.lifetime);
                out.write_tag(static_cast<int>(e.type));
                serialise_type(e.inner);
            }
            TU_ARMA(Pointer, e) {
                out.write_tag(static_cast<int>(e.type));
                serialise_type(e.inner);
            }
            TU_ARMA(NamedFunction, e) {
                serialise_path(e.path);
            }
            TU_ARMA(Function, e) {
                serialise_generics(e.hrls);
                out.write_bool(e.is_unsafe);
                out.write_bool(e.is_variadic);
                out.write_string(e.mAbi);
                serialise_type(e.mRettype);
                serialise_vec(e.argTypes);
            }
            break;
            case ::HIR::TypeData::TAG_NodeType:
                BUG(Span(), "Encountered invalid type when serialising - " << ty);
                break;
            }

            types.insert(std::make_pair( std::move(ty_str), types.size() ));
    }

    void serialise_simplepath(const ::HIR::SimplePath& path) {
        TRACE_FUNCTION_F(path);
        serialise_vec(path.members);
    }

    void serialise_pathparams(const ::HIR::PathParams& pp) {
        serialise_vec(pp.mLifetimes);
        serialise_vec(pp.types);
        serialise_vec(pp.values);
    }

    void serialise_genericpath(const ::HIR::GenericPath& path) {
        TRACE_FUNCTION_F(path);
        serialise_simplepath(path.mPath);
        serialise_pathparams(path.mParams);
    }

    void serialise(const ::HIR::GenericPath& path) {
        serialise_genericpath(path);
    }

    void serialise_traitpath(const ::HIR::TraitPath& path) {
        auto _ = out.open_object("HIR::TraitPath");
        assert(!path.lifetimeElision);
        out.write_bool(static_cast<bool>(path.hrtbs));
        if (path.hrtbs) {
            serialise_generics(*path.hrtbs);
        }
        serialise_genericpath(path.mPath);
        serialise_strmap(path.typeBounds);
        serialise_strmap(path.traitBounds);
        out.write_u8(static_cast<uint8_t>(path.constness));
    }

    void serialise(const ::HIR::TraitPath::AtyEqual& e) {
        serialise(e.source_trait);
        serialise_pathparams(e.aty_params);
        serialise(e.type);
    }

    void serialise(const ::HIR::TraitPath::AtyBound& e) {
        serialise(e.source_trait);
        serialise_pathparams(e.aty_params);
        serialise_vec(e.traits);
    }

    void serialise_path(const ::HIR::Path& path) {
        TRACE_FUNCTION_F("path=" << path);
            TU_MATCH_HDRA( (path.mData), {)
            TU_ARMA(Generic, e) {
                out.write_tag(0);
                serialise_genericpath(e);
            }
            TU_ARMA(UfcsInherent, e) {
                out.write_tag(1);
                serialise_type(e.type);
                out.write_string(e.item);
                serialise_pathparams(e.params);
                serialise_pathparams(e.impl_params);
            }
            TU_ARMA(UfcsKnown, e) {
                if (e.hrtbs) {
                    out.write_tag(3);
                    serialise_generics(*e.hrtbs);
                } else {
                    out.write_tag(2);
                }
                serialise_type(e.type);
                serialise_genericpath(e.trait);
                out.write_string(e.item);
                serialise_pathparams(e.params);
            }
            TU_ARMA(UfcsUnknown, e) {
                DEBUG("-- UfcsUnknown - " << path);
                assert(!"Unexpected UfcsUnknown");
            }
            }
    }

    void serialise_generics(const ::HIR::GenericParams& params) {
        DEBUG("params = " << params.fmt_args() << ", " << params.fmt_bounds());
        serialise_vec(params.types);
        serialise_vec(params.values);
        serialise_vec(params.mLifetimes);
        serialise_vec(params.bounds);
    }

    void serialise(const ::HIR::TypeParamDef& pd) {
        out.write_string(pd.mName);
        serialise_type(pd.defaultValue);
        out.write_bool(pd.isSized);
    }

    void serialise(const ::HIR::ValueParamDef& pd) {
        out.write_string(pd.mName);
        serialise_type(pd.mType);
        serialise(pd.defaultValue);
    }

    void serialise(const ::HIR::GenericBound& b) {
        TRACE_FUNCTION_F(b);
            TU_MATCH_HDRA( (b), {)
            TU_ARMA(Lifetime, e) {
                out.write_tag(0);
                serialise(e.test);
                serialise(e.valid_for);
            }
            TU_ARMA(TypeLifetime, e) {
                out.write_tag(1);
                serialise_type(e.type);
                serialise(e.valid_for);
            }
            TU_ARMA(TraitBound, e) {
                out.write_tag(2);
                out.write_bool(static_cast<bool>(e.hrtbs));
                if (e.hrtbs) {
                    serialise_generics(*e.hrtbs);
                }
                serialise_type(e.type);
                serialise_traitpath(e.trait);
                out.write_u8(static_cast<uint8_t>(e.constness));
            }
            TU_ARMA(TypeEquality, e) {
                out.write_tag(3);
                serialise_type(e.type);
                serialise_type(e.other_type);
            }
            }
    }

    void serialise(const ::HIR::ProcMacro& pm) {
        TRACE_FUNCTION_F("pm = ProcMacro { " << pm.name << ", " << pm.path << ", [" << pm.attributes << "] }");
        switch (pm.ty) {
            case ::HIR::ProcMacro::Ty::Function:
                out.write_tag(0);
                break;
            case ::HIR::ProcMacro::Ty::Derive:
                out.write_tag(1);
                break;
            case ::HIR::ProcMacro::Ty::Attribute:
                out.write_tag(2);
                break;
        }
        serialise(pm.name);
        serialise(pm.path);
        serialise_vec(pm.attributes);
    }

    template <typename T>
    void serialise(const ::HIR::Crate::ImplGroup<T>& ig) {
        serialise_pathmap(ig.named);
        serialise_vec(ig.non_named);
        serialise_vec(ig.generic);
    }

    void serialise_crate(const ::HIR::Crate& crate) {
        out.write_string(crate.crateName);
        out.write_tag(static_cast<int>(crate.edition));
        serialise_module(crate.rootModule);

        serialise(crate.typeImpls);
        serialise_pathmap(crate.traitImpls);
        serialise_pathmap(crate.markerImpls);

        serialise_vec(crate.exportedMacroNames);

        {
            decltype(crate.mLangItems) lang_items_filtered;
            for (const auto& ent : crate.mLangItems) {
                if (ent.second.crate_name() == "" || ent.second.crate_name() == crate.crateName) {
                    lang_items_filtered.insert(ent);
                }
            }
            serialise_strmap(lang_items_filtered);
        }

        out.write_count(crate.extCrates.size());
        for (const auto& ext : crate.extCrates) {
            out.write_string(ext.first);
            out.write_string(ext.second.basename);
            //m_out.write_string(ext.second.m_path);
        }
        serialise_vec(crate.extLibs);
        serialise_vec(crate.linkPaths);
    }

    void serialise(const ::HIR::ExternLibrary& lib) {
        out.write_string(lib.name);
    }

    void serialise_module(const ::HIR::Module& mod) {
        TRACE_FUNCTION;
        auto _ = out.open_object("HIR::Module");

        // m_traits doesn't need to be serialised

        serialise_strmap(mod.valueItems);
        serialise_strmap(mod.modItems);
        serialise_strmap(mod.macroItems);
    }

    void serialise_typeimpl(const ::HIR::TypeImpl& impl) {
        TRACE_FUNCTION_F("impl" << impl.mParams.fmt_args() << " " << impl.mType);
        serialise_generics(impl.mParams);
        serialise_type(impl.mType);

        out.write_count(impl.methods.size());
        for (const auto& v : impl.methods) {
            out.write_string(v.first);
            out.write_bool(v.second.publicity.is_global());
            out.write_bool(v.second.is_specialisable);
            serialise(v.second.data);
        }
        out.write_count(impl.constants.size());
        for (const auto& v : impl.constants) {
            out.write_string(v.first);
            out.write_bool(v.second.publicity.is_global());
            out.write_bool(v.second.is_specialisable);
            serialise(v.second.data);
        }
        out.write_count(impl.types.size());
        for (const auto& v : impl.types) {
            out.write_string(v.first);
            out.write_bool(v.second.publicity.is_global());
            out.write_bool(v.second.is_specialisable);
            serialise(v.second.data);
        }
        // m_src_module doesn't matter after typeck
    }

    void serialise(const ::HIR::TypeImpl& impl) {
        serialise_typeimpl(impl);
    }

    void serialise_traitimpl(const ::HIR::TraitImpl& impl) {
        TRACE_FUNCTION_F("impl" << impl.mParams.fmt_args() << " ?" << impl.traitArgs << " for " << impl.mType);
        serialise_generics(impl.mParams);
        serialise_pathparams(impl.traitArgs);
        serialise_type(impl.mType);
        out.write_bool(impl.isConst);

        out.write_count(impl.methods.size());
        for (const auto& v : impl.methods) {
            DEBUG("fn " << v.first);
            out.write_string(v.first);
            out.write_bool(v.second.is_specialisable);
            serialise(v.second.data);
        }
        out.write_count(impl.constants.size());
        for (const auto& v : impl.constants) {
            DEBUG("const " << v.first);
            out.write_string(v.first);
            out.write_bool(v.second.is_specialisable);
            serialise(v.second.data);
        }
        out.write_count(impl.statics.size());
        for (const auto& v : impl.statics) {
            DEBUG("static " << v.first);
            out.write_string(v.first);
            out.write_bool(v.second.is_specialisable);
            serialise(v.second.data);
        }
        out.write_count(impl.types.size());
        for (const auto& v : impl.types) {
            DEBUG("type " << v.first);
            out.write_string(v.first);
            out.write_bool(v.second.is_specialisable);
            serialise(v.second.data);
        }
        // m_src_module doesn't matter after typeck
    }

    void serialise(const ::HIR::TraitImpl& impl) {
        serialise_traitimpl(impl);
    }

    void serialise_markerimpl(const ::HIR::MarkerImpl& impl) {
        serialise_generics(impl.mParams);
        serialise_pathparams(impl.traitArgs);
        out.write_bool(impl.is_positive);
        serialise_type(impl.mType);
    }

    void serialise(const ::HIR::MarkerImpl& impl) {
        serialise_markerimpl(impl);
    }

    void serialise(const ::HIR::TypeData* ty) {
        serialise_type(ty);
    }

    void serialise(const ::HIR::SimplePath& p) {
        serialise_simplepath(p);
    }

    void serialise(const ::HIR::TraitPath& p) {
        serialise_traitpath(p);
    }

    void serialise(const ::std::string& v) {
        out.write_string(v);
    }

    void serialise(const RcString& v) {
        out.write_string(v);
    }

    void serialise(const Ident::Hygiene& h) {
        auto _ = out.open_object(typeid(Ident::Hygiene).name());
        out.write_bool(h.has_mod_path());
        if (h.has_mod_path()) {
            out.write_string(h.mod_path().crate);
            serialise_vec(h.mod_path().ents);
        }
    }

    void serialise(const ::MacroRulesPtr& mac) {
        serialise(*mac);
    }

    void serialise(const ::MacroRules& mac) {
        //m_exported: IGNORE, should be set
        out.write_string(mac.sourceCrate);
        out.write_tag(static_cast<unsigned int>(mac.edition));
        assert(mac.rules.size() > 0);
        out.write_bool(mac.isMacroItem);
        serialise_vec(mac.rules);
        serialise(mac.mHygiene);
    }

    void serialise(const ::MacroPatEnt& pe) {
        out.write_string(pe.name);
        out.write_count(pe.name_index);
        out.write_tag(static_cast<int>(pe.type));
        if (pe.type == ::MacroPatEnt::PAT_TOKEN) {
            serialise(pe.tok);
        } else if (pe.type == ::MacroPatEnt::PAT_LOOP) {
            serialise(pe.tok);
            serialise_vec(pe.subpats);
        }
    }

    void serialise(const ::SimplePatIfCheck& e) {
        out.write_tag(static_cast<int>(e.ty));
        serialise(e.tok);
    }

    void serialise(const ::SimplePatEnt& pe) {
        out.write_tag(pe.tag());
            TU_MATCH_HDRA( (pe), { )
            TU_ARMA(End, _e) {
            }
            TU_ARMA(LoopStart, e) {
                out.write_count(e.index);
            }
            TU_ARMA(LoopNext, _e) {
            }
            TU_ARMA(LoopEnd, _e) {
            }
            TU_ARMA(Jump, e) {
                out.write_count(e.jump_target);
            }
            TU_ARMA(ExpectTok, e) {
                serialise(e);
            }
            TU_ARMA(ExpectPat, e) {
                out.write_tag(static_cast<int>(e.type));
                out.write_count(e.idx);
            }
            TU_ARMA(If, e) {
                out.write_bool(e.is_equal);
                out.write_count(e.jump_target);
                serialise_vec(e.ents);
            }
            }
    }

    void serialise(const ::MacroRulesArm& arm) {
        serialise_vec(arm.paramNames);
        serialise_vec(arm.pattern);
        serialise_vec(arm.contents);
    }

    void serialise(const ::MacroExpansionEnt& ent) {
            TU_MATCH_HDRA( (ent), {)
            TU_ARMA(Token, e) {
                out.write_tag(0);
                serialise(e);
            }
            TU_ARMA(NamedValue, e) {
                out.write_tag(1);
                out.write_u8(e >> 24);
                out.write_count(e & 0x00FFFFFF);
            }
            TU_ARMA(Loop, e) {
                out.write_tag(2);
                serialise_vec(e.entries);
                serialise(e.joiner);
                serialise(e.controlling_input_loops);
            }
            TU_ARMA(Concat, e) {
                out.write_tag(3);
                serialise_vec(e);
            }
            }
    }

    void serialise(const ::MacroExpansionConcatEnt& e) {
        out.write_tag(e.tag());
            TU_MATCH_HDRA((e), {)
            TU_ARMA(Ident, i) {
                serialise(i.hygiene);
                out.write_string(i.name);
            }
            TU_ARMA(Named, i) {
                serialise(i);
            }
            }
    }

    void serialise(const ::Token& tok) {
        out.write_tag(tok.mType);
        serialise(tok.mData);
        // TODO: Position information.
    }

    void serialise(const ::Token::Data& td) {
        out.write_tag(td.tag());
        switch (td.tag()) {
            case ::Token::Data::TAGDEAD:
                throw "";
                TU_ARM(td, None, _e) {
                }
                break;
                TU_ARM(td, String, e) {
                    out.write_string(e);
                }
                break;
                TU_ARM(td, Ident, e) {
                    serialise(e.hygiene);
                    out.write_string(e.name);
                }
                break;
                TU_ARM(td, Integer, e) {
                    out.write_tag(e.datatype);
                    out.write_u128(e.intval);
                }
                break;
                TU_ARM(td, Float, e) {
                    out.write_tag(e.datatype);
                    out.write_float_value(e.floatval);
                }
                break;
                TU_ARM(td, Fragment, e)
                assert(!"Serialising interpolated macro fragment - should have been handled in HIR lowering");
        }
    }

    void serialise(const EncodedLiteral& lit) {
        serialise(lit.bytes);
        out.write_count(lit.relocations.size());
        for (const auto& reloc : lit.relocations) {
            out.write_count(reloc.ofs);
            out.write_count(reloc.len);
            if (reloc.p) {
                out.write_tag(0);
                serialise_path(*reloc.p);
            } else {
                out.write_tag(1);
                serialise(reloc.bytes);
            }
        }
    }

    void serialise(const ::HIR::ConstGenericUnevaluated& v) {
        ASSERT_BUG(v.expr->span(), v.expr->mir, "Encountered non-translated value in ConstGeneric: " << v);
        serialise_pathparams(v.params_impl);
        serialise_pathparams(v.params_item);
        serialise(*v.expr);
    }

    void serialise(const ::HIR::ConstGeneric& v) {
        out.write_tag(v.tag());
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

    void serialise(const ::HIR::ExprPtr& exp, bool save_mir = true) {
        auto _ = out.open_object("HIR::ExprPtr");
        save_mir &= static_cast<bool>(exp.mir);
        out.write_bool(save_mir);
        if (save_mir) {
            serialise(*exp.mir);
        }
        serialise_vec(exp.erasedTypes);
    }

    void serialise(const ::MIR::Function& mir) {
        // Write out MIR.
        serialise_vec(mir.locals);
        //serialise_vec( mir.slot_names );
        serialise_vec(mir.drop_flags);
        serialise_vec(mir.blocks);
    }

    void serialise(const ::MIR::BasicBlock& block) {
        serialise_vec(block.statements);
        serialise(block.terminator);
        out.write_bool(block.is_cleanup);
    }

    void serialise(const ::AsmCommon::LineFragment& l) {
        serialise(l.before);
        out.write_count(l.index);
        out.write_i64c(l.modifier);
    }

    void serialise(const ::AsmCommon::Line& l) {
        serialise_vec(l.frags);
        serialise(l.trailing);
    }

    void serialise(const ::AsmCommon::RegisterSpec& r) {
        out.write_tag(static_cast<unsigned>(r.tag()));
            TU_MATCH_HDRA( (r), {)
            TU_ARMA(Class, e) {
                out.write_tag(static_cast<unsigned>(e));
            }
            TU_ARMA(Explicit, e) {
                out.write_string(e);
            }
            }
    }

    void serialise(const ::MIR::AsmParam& p) {
        out.write_tag(static_cast<unsigned>(p.tag()));
            TU_MATCH_HDRA( (p), {)
            TU_ARMA(Sym, e) {
                serialise_path(e);
            }
            TU_ARMA(Const, e) {
                serialise(e);
            }
            TU_ARMA(Reg, e) {
                out.write_tag(static_cast<unsigned>(e.dir));
                serialise(e.spec);
                out.write_bool(bool(e.input));
                if (e.input) {
                    serialise(e.input);
                }
                out.write_bool(bool(e.output));
                if (e.output) {
                    serialise(e.output);
                }
            }
            }
    }

    void serialise(const ::AsmCommon::Options& o) {
        uint16_t bitflag_1 = 0;
#define BIT(i, fld) \
    if (fld)        \
        bitflag_1 |= 1 << (i);
        BIT(0, o.pure);
        BIT(1, o.nomem);
        BIT(2, o.readonly);
        BIT(3, o.preserves_flags);
        BIT(4, o.noreturn);
        BIT(5, o.nostack);
        BIT(6, o.att_syntax);
#undef BIT
        out.write_u16(bitflag_1);
    }

    void serialise(const ::MIR::Statement& stmt) {
        auto _ = out.open_object("MIR::Statement");
            TU_MATCH_HDRA( (stmt), {)
            TU_ARMA(Assign, e) {
                out.write_tag(0);
                serialise(e.dst);
                serialise(e.src);
            }
            TU_ARMA(Asm, e) {
                out.write_tag(2);
                out.write_string(e.tpl);
                serialise_vec(e.outputs);
                serialise_vec(e.inputs);
                serialise_vec(e.clobbers);
                serialise_vec(e.flags);
            }
            TU_ARMA(SetDropFlag, e) {
                out.write_tag(3);
                out.write_count(e.idx);
                out.write_bool(e.new_val);
                out.write_count(e.other);
            }
            TU_ARMA(ScopeEnd, e) {
                out.write_tag(4);
                serialise_vec(e.slots);
            }
            TU_ARMA(Asm2, e) {
                out.write_tag(5);
                serialise(e.options);
                serialise_vec(e.lines);
                serialise_vec(e.params);
            }
            TU_ARMA(SaveDropFlag, e) {
                out.write_tag(6);
                serialise(e.slot);
                out.write_count(e.bit_index);
                out.write_count(e.idx);
            }
            TU_ARMA(LoadDropFlag, e) {
                out.write_tag(7);
                out.write_count(e.idx);
                serialise(e.slot);
                out.write_count(e.bit_index);
            }
            }
    }

    void serialise(const ::MIR::Terminator& term) {
        auto serialise_unwind = [this](const ::MIR::UnwindAction& action) {
            out.write_tag(static_cast<int>(action.tag()));
            TU_IFLET(::MIR::UnwindAction, action, Cleanup, target, out.write_count(target);)
        };
        out.write_tag(static_cast<int>(term.tag()));
        TU_MATCHA(
            (term),
            (e),
            (
                Incomplete,
                // NOTE: loops that diverge (don't break) leave a dangling bb
                //assert(!"Entountered Incomplete MIR block");
            ),
            (Return, ),
            (UnwindResume, ),
            (UnwindTerminate, ),
            (Unreachable, ),
            (Goto, out.write_count(e);),
            (If, serialise(e.cond); out.write_count(e.bb_true); out.write_count(e.bb_false);),
            (Switch, serialise(e.val); serialise_vec(e.targets); out.write_count(e.valid_flag); out.write_count(e.invalid_target);),
            (SwitchValue, serialise(e.val); out.write_count(e.def_target); serialise_vec(e.targets); serialise(e.values);),
            (Drop, out.write_tag(static_cast<unsigned>(e.kind)); serialise(e.slot); out.write_count(e.flag_idx); out.write_count(e.target); serialise_unwind(e.unwind);),
            (Call, out.write_count(e.ret_block); serialise_unwind(e.unwind); serialise(e.ret_val); serialise(e.fcn); serialise_vec(e.args);)
        )
    }

    void serialise(const ::MIR::SwitchValues& sv) {
        out.write_tag(static_cast<int>(sv.tag()));
            TU_MATCH_HDRA( (sv), {)
            TU_ARMA(Unsigned, e) {
                serialise_vec(e);
            }
            TU_ARMA(Signed, e) {
                serialise_vec(e);
            }
            TU_ARMA(String, e) {
                serialise_vec(e);
            }
            TU_ARMA(ByteString, e) {
                serialise_vec(e);
            }
            }
    }

    void serialise(const ::MIR::CallTarget& ct) {
        out.write_tag(static_cast<int>(ct.tag()));
        TU_MATCHA((ct), (e), (Value, serialise(e);), (Path, serialise_path(e);), (Intrinsic, out.write_string(e.name); serialise_pathparams(e.params);))
    }

    void serialise(const ::MIR::Param& p) {
        TRACE_FUNCTION_F("Param = " << p);
        out.write_tag(static_cast<int>(p.tag()));
        TU_MATCHA((p), (e), (LValue, serialise(e);), (Borrow, out.write_tag(static_cast<int>(e.type)); serialise(e.val);), (Constant, serialise(e);))
    }

    void serialise(const ::MIR::LValue& lv) {
        TRACE_FUNCTION_F("LValue = " << lv);
        if (lv.root.is_Static()) {
            out.write_count(3);
            serialise_path(lv.root.as_Static());
        } else {
            out.write_count(lv.root.get_inner());
        }
        serialise_vec(lv.wrappers);
    }

    void serialise(const ::MIR::LValue::Wrapper& w) {
        out.write_count(w.get_inner());
    }

    void serialise(const ::MIR::RValue& val) {
        TRACE_FUNCTION_F("RValue = " << val);
        out.write_tag(val.tag());
        TU_MATCHA((val), (e), (Use, serialise(e);), (Constant, serialise(e);), (SizedArray, serialise(e.val); serialise_arraysize(e.count);), (Borrow, out.write_tag(static_cast<int>(e.type)); out.write_bool(e.is_raw); serialise(e.val);), (Cast, serialise(e.val); serialise(e.type);), (BinOp, serialise(e.val_l); out.write_tag(static_cast<int>(e.op)); serialise(e.val_r);), (UniOp, serialise(e.val); out.write_tag(static_cast<int>(e.op));), (DstMeta, serialise(e.val);), (DstPtr, serialise(e.val);), (MakeDst, serialise(e.ptr_val); auto b = !TU_TEST2(e.meta_val, Constant, , ItemAddr, .get() == nullptr); out.write_bool(b); if (b) serialise(e.meta_val);), (Tuple, serialise_vec(e.vals);), (Array, serialise_vec(e.vals);), (UnionVariant, serialise_genericpath(e.path); out.write_count(e.index); serialise(e.val);), (EnumVariant, serialise_genericpath(e.path); out.write_count(e.index); serialise_vec(e.vals);), (Struct, serialise_genericpath(e.path); serialise_vec(e.vals);))
    }

    void serialise(const ::MIR::Constant& v) {
        out.write_tag(v.tag());
        TU_MATCHA((v), (e), (Int, out.write_u128(e.v.get_inner()); out.write_tag(static_cast<unsigned>(e.t));), (Uint, out.write_u128(e.v); out.write_tag(static_cast<unsigned>(e.t));), (Float, out.write_float_value(e.v); out.write_tag(static_cast<unsigned>(e.t));), (Bool, out.write_bool(e.v);), (Bytes, out.write_count(e.size()); out.write(e.data(), e.size());), (StaticString, out.write_string(e);), (Const, ASSERT_BUG(Span(), monomorphise_path_needed(*e.p), "Unexpected Constant: " << *e.p); serialise_path(*e.p);), (Generic, serialise(e);), (Function, serialise_path(*e.p);), (ItemAddr, serialise_path(*e); out.write_u128(e.offset);))
    }

    void serialise(const ::HIR::TypeItem& item) {
        TU_MATCHA((item), (e), (Import, out.write_tag(0); serialise_simplepath(e.path); out.write_bool(e.is_variant); out.write_count(e.idx);), (Module, out.write_tag(1); serialise_module(e);), (TypeAlias, out.write_tag(2); serialise(e);), (Enum, out.write_tag(3); serialise(e);), (Struct, out.write_tag(4); serialise(e);), (Trait, out.write_tag(5); serialise(e);), (Union, out.write_tag(6); serialise(e);), (ExternType, out.write_tag(7); serialise(e);), (TraitAlias, out.write_tag(8); serialise(e);))
    }

    void serialise(const ::HIR::MacroItem& item) {
        auto _ = out.open_object("HIR::MacroItem");
        out.write_tag(item.tag());
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

    void serialise(const ::HIR::ValueItem& item) {
        TU_MATCHA((item), (e), (Import, out.write_tag(0); serialise_simplepath(e.path); out.write_bool(e.is_variant); out.write_count(e.idx);), (Constant, out.write_tag(1); serialise(e);), (Static, out.write_tag(2); serialise(e);), (StructConstant, out.write_tag(3); serialise_simplepath(e.ty);), (Function, out.write_tag(4); serialise(e);), (StructConstructor, out.write_tag(5); serialise_simplepath(e.ty);))
    }

    void serialise(const ::HIR::Linkage& linkage) {
        //m_out.write_tag( static_cast<int>(linkage.type) );
        out.write_string(linkage.name);
    }

    // - Value items
    void serialise(const ::HIR::Function& fcn) {
        TRACE_FUNCTION_F("_function:");
        auto _ = out.open_object("HIR::Function");

        serialise(fcn.linkage);

        out.write_tag(static_cast<int>(fcn.receiver));
        serialise(fcn.receiverType.value_or(typeInterner.infer()));
        out.write_string(fcn.mAbi);
        out.write_bool(fcn.unsafe);
        out.write_bool(fcn.isConst);
        serialise(fcn.markings);

        serialise_generics(fcn.mParams);
        out.write_count(fcn.mArgs.size());
        for (const auto& a : fcn.mArgs) {
            serialise(a.second);
        }
        DEBUG("m_args = " << fcn.mArgs);
        out.write_bool(fcn.variadic);
        serialise(fcn.returnType);

        serialise(fcn.mCode, fcn.saveCode || fcn.isConst);
    }

    void serialise(const ::HIR::Function::Markings& m) {
        auto _ = out.open_object("HIR::Function::Markings");
        serialise_vec(m.rustc_legacy_const_generics);
        out.write_bool(m.track_caller);
    }

    void serialise(const ::HIR::Constant& item) {
        TRACE_FUNCTION_F("_constant:");

        serialise_generics(item.mParams);
        serialise(item.mType);
        serialise(item.mValue);
        bool write_val = item.valueState == ::HIR::Constant::ValueState::Known;
        out.write_bool(write_val);
        if (write_val) {
            serialise(item.valueRes);
        }
    }

    void serialise(const ::HIR::Static& item) {
        TRACE_FUNCTION_F("_static:");

        serialise(item.linkage);
        serialise_generics(item.mParams);

        uint8_t bitflag_1 = 0;
#define BIT(i, fld) \
    if (fld)        \
        bitflag_1 |= 1 << (i);
        BIT(0, item.isMut);
        BIT(1, item.saveLiteral)
#undef BIT
        out.write_u8(bitflag_1);
        serialise(item.mType);

        if (item.mParams.is_generic()) {
            serialise(item.mValue);
        }
        // NOTE: Value not stored (What if the static is generic? It can't be.)
        // - Need to store if the item was from a const (special linkage?)
        if (item.saveLiteral) {
            serialise(item.valueRes);
        }
    }

    // - Type items
    void serialise(const ::HIR::TypeAlias& ta) {
        serialise_generics(ta.mParams);
        serialise_type(ta.mType);
    }

    void serialise(const ::HIR::TraitAlias& ta) {
        serialise_generics(ta.mParams);
        serialise_vec(ta.traits);
    }

    void serialise(const ::HIR::Enum& item) {
        auto _ = out.open_object("HIR::Enum");
        serialise_generics(item.mParams);
        out.write_bool(item.isCRepr);
        out.write_tag(static_cast<int>(item.tagRepr));
        serialise(item.mData);

        serialise(item.markings);
    }

    void serialise(const ::HIR::Enum::Class& v) {
        out.write_tag(v.tag());
        TU_MATCHA((v), (e), (Value, serialise_vec(e.variants);), (Data, serialise_vec(e);))
    }

    void serialise(const ::HIR::Enum::ValueVariant& v) {
        out.write_string(v.name);
        // NOTE: No expr, no longer needed
        out.write_u64(v.val.truncate_u64());
    }

    void serialise(const ::HIR::Enum::DataVariant& v) {
        out.write_string(v.name);
        out.write_bool(v.is_struct);
        serialise(v.type);
        out.write_u64(v.discriminant_value.truncate_u64());
    }

    void serialise(const ::HIR::TraitMarkings& m) {
        uint8_t bitflag_1 = 0;
#define BIT(i, fld) \
    if (fld)        \
        bitflag_1 |= 1 << (i);
        BIT(0, m.has_a_deref)
        BIT(1, m.is_copy)
        BIT(2, m.has_drop_impl)
#undef BIT
        out.write_u8(bitflag_1);

        // TODO: auto_impls
    }

    void serialise(const ::HIR::StructMarkings& m) {
        uint8_t bitflag_1 = 0;
#define BIT(i, fld) \
    if (fld)        \
        bitflag_1 |= 1 << (i);
        BIT(0, m.can_unsize)
        BIT(1, m.is_nonzero)
        BIT(2, m.bounded_max)
        BIT(3, m.is_fundamental)
#undef BIT
        out.write_u8(bitflag_1);

        out.write_tag(static_cast<unsigned int>(m.dst_type));
        out.write_tag(static_cast<unsigned int>(m.coerce_unsized));
        out.write_count(m.coerce_unsized_index);
        out.write_count(m.coerce_param);
        out.write_count(m.unsized_field);
        out.write_count(m.unsized_param);
        if (m.bounded_max) {
            out.write_u128(m.bounded_max_value);
        }
        // TODO: auto_impls
    }

    void serialise(const ::HIR::Struct& item) {
        TRACE_FUNCTION_F("Struct");
        auto _ = out.open_object("HIR::Struct");

        serialise_generics(item.mParams);
        out.write_tag(static_cast<int>(item.repr));

        out.write_tag(item.mData.tag());
        TU_MATCHA((item.mData), (e), (Unit, ), (Tuple, serialise_vec(e);), (Named, serialise_vec(e);))

        out.write_count(item.forcedAlignment);
        out.write_count(item.maxFieldAlignment);
        serialise(item.markings);
        serialise(item.structMarkings);
    }

    void serialise(const ::HIR::StructField& fld) {
        serialise(fld.name);
        serialise(fld.vis);
        serialise(fld.ty);
        out.write_bool(fld.default_value != nullptr);
        if (fld.default_value) {
            serialise(*fld.default_value);
        }
    }

    void serialise(const ::HIR::Union& item) {
        TRACE_FUNCTION_F("Union");

        serialise_generics(item.mParams);
        out.write_tag(static_cast<int>(item.repr));

        serialise_vec(item.mVariants);

        serialise(item.markings);
    }

    void serialise(const ::HIR::ExternType& item) {
        TRACE_FUNCTION_F("ExternType");
        serialise(item.markings);
    }

    void serialise(const ::HIR::Trait& item) {
        TRACE_FUNCTION_F("_trait:");
        auto _ = out.open_object("HIR::Trait");

        serialise_generics(item.mParams);
        serialise(item.lifetime);
        // Kept as one byte for compatibility with metadata written before
        // the fundamental bit was represented in HIR.
        out.write_u8(
            (item.isMarker ? 1u : 0u)
            | (item.isFundamental ? 2u : 0u)
            | (item.isCoinductive ? 4u : 0u)
            | (item.isConst ? 8u : 0u)
        );
        serialise_strmap(item.types);
        serialise_strmap(item.values);
        serialise_strmap(item.valueIndexes);
        serialise_strmap(item.typeIndexes);
        out.write_count(item.vtableParentTraitsStart);
        serialise_vec(item.allParentTraits);
        serialise(item.vtablePath);
    }

    void serialise(const ::HIR::TraitValueItem& tvi) {
        out.write_tag(tvi.tag());
        TU_MATCHA((tvi), (e), (Constant, DEBUG("Constant"); serialise(e);), (Static, DEBUG("Static"); serialise(e);), (Function, DEBUG("Function"); serialise(e);))
    }

    void serialise(const ::HIR::AssociatedType& at) {
        serialise_generics(at.generics);
        out.write_bool(at.is_sized);
        serialise(at.lifetimeBound);
        serialise_vec(at.traitBounds);
        serialise_type(at.defaultValue);
    }
};

//}

void HIRSerialise(const ::std::string& filename, const ::HIR::Crate& crate) {
    ::HIR::serialise::Writer out;
    HirSerialiser s{out, crate.types};
    s.serialise_crate(crate);
    s.clear();
    out.open(filename);
    s.serialise_crate(crate);
}
