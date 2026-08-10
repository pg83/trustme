#include "hir_main_bindings.h"

// TODO: Have an environment variable that controls if debug is enabled here.
#define DEBUG_EXTRA_ENABLE &&des_debug_enabled()

namespace {
    bool des_debug_enabled();
}

//#define DISABLE_DEBUG   //  Disable debug for this function - too hot
#include "hir_hir.h"
#include "hir_main_bindings.h"
#include "mir_mir.h"
#include "macro_rules_macro_rules.h"
#include "hir_serialise_lowlevel.h"
#include <std/mem/obj_pool.h>
#include <typeinfo>

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
    RcString m_crate_name;
    ::std::vector<HIR::TypeRef> m_types;
    ::HIR::serialise::Reader& m_in;
    ::HIR::TypeInterner& m_type_interner;

public:
    HirDeserialiser(::HIR::serialise::Reader& in, ::HIR::TypeInterner& type_interner)
        : m_in(in)
        , m_type_interner(type_interner)
    {
    }

    RcString read_istring() {
        return m_in.read_istring();
    }

    ::std::string read_string() {
        return m_in.read_string();
    }

    bool read_bool() {
        return m_in.read_bool();
    }

    uint8_t read_u8() {
        return m_in.read_u8();
    }

    size_t deserialise_count() {
        return m_in.read_count();
    }

    template <typename V>
    ::std::map<::std::string, V> deserialise_strmap() {
        TRACE_FUNCTION_F("<" << typeid(V).name() << ">");
        size_t n = m_in.read_count();
        ::std::map<::std::string, V> rv;
        //rv.reserve(n);
        for (size_t i = 0; i < n; i++) {
            auto s = m_in.read_string();
            rv.insert(::std::make_pair(mv$(s), D<V>::des(*this)));
        }
        return rv;
    }

    template <typename V>
    ::std::unordered_map<::std::string, V> deserialise_strumap() {
        TRACE_FUNCTION_F("<" << typeid(V).name() << ">");
        size_t n = m_in.read_count();
        ::std::unordered_map<::std::string, V> rv;
        //rv.reserve(n);
        for (size_t i = 0; i < n; i++) {
            auto s = m_in.read_string();
            DEBUG("- " << s);
            rv.insert(::std::make_pair(mv$(s), D<V>::des(*this)));
        }
        return rv;
    }

    template <typename V>
    ::std::unordered_multimap<::std::string, V> deserialise_strummap() {
        TRACE_FUNCTION_F("<" << typeid(V).name() << ">");
        size_t n = m_in.read_count();
        ::std::unordered_multimap<::std::string, V> rv;
        //rv.reserve(n);
        for (size_t i = 0; i < n; i++) {
            auto s = m_in.read_string();
            DEBUG("- " << s);
            rv.insert(::std::make_pair(mv$(s), D<V>::des(*this)));
        }
        return rv;
    }

    template <typename V>
    ::std::map<RcString, V> deserialise_istrmap() {
        TRACE_FUNCTION_F("<" << typeid(V).name() << ">");
        size_t n = m_in.read_count();
        ::std::map<RcString, V> rv;
        //rv.reserve(n);
        for (size_t i = 0; i < n; i++) {
            auto s = m_in.read_istring();
            rv.insert(::std::make_pair(mv$(s), D<V>::des(*this)));
        }
        return rv;
    }

    template <typename V>
    ::std::unordered_map<RcString, V> deserialise_istrumap() {
        TRACE_FUNCTION_F("<" << typeid(V).name() << ">");
        size_t n = m_in.read_count();
        ::std::unordered_map<RcString, V> rv;
        //rv.reserve(n);
        for (size_t i = 0; i < n; i++) {
            auto s = m_in.read_istring();
            DEBUG("- " << s);
            rv.insert(::std::make_pair(mv$(s), D<V>::des(*this)));
        }
        return rv;
    }

    template <typename V>
    ::std::unordered_multimap<RcString, V> deserialise_istrummap() {
        TRACE_FUNCTION_F("<" << typeid(V).name() << ">");
        size_t n = m_in.read_count();
        ::std::unordered_multimap<RcString, V> rv;
        //rv.reserve(n);
        for (size_t i = 0; i < n; i++) {
            auto s = m_in.read_istring();
            DEBUG("- " << s);
            rv.insert(::std::make_pair(mv$(s), D<V>::des(*this)));
        }
        return rv;
    }

    template <typename V>
    ::std::map<::HIR::SimplePath, V> deserialise_pathmap() {
        TRACE_FUNCTION_F("<" << typeid(V).name() << ">");
        size_t n = m_in.read_count();
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
        TRACE_FUNCTION_FR("<" << typeid(T).name() << ">", m_in.get_pos());
        auto _ = m_in.open_object(typeid(::std::vector<T>).name());
        size_t n = m_in.read_count();
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
        TRACE_FUNCTION_FR("<" << typeid(T).name() << ">", m_in.get_pos());
        auto _ = m_in.open_object(typeid(ThinVector<T>).name());
        size_t n = m_in.read_count();
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
        TRACE_FUNCTION_FR("<" << typeid(T).name() << ">", m_in.get_pos());
        auto _ = m_in.open_object(typeid(::std::set<T>).name());
        size_t n = m_in.read_count();
        DEBUG("n = " << n);
        ::std::set<T> rv;
        //rv.reserve(n);
        for (size_t i = 0; i < n; i++) {
            rv.insert(D<T>::des(*this));
        }
        return rv;
    }

    ::HIR::Publicity deserialise_pub() {
        return (m_in.read_bool() ? ::HIR::Publicity::new_global() : g_vis_private);
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
        switch (m_in.read_tag()) {
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
        pm.name = m_in.read_istring();
        pm.path = deserialise_simplepath();
        pm.attributes = deserialise_vec<::std::string>();
        DEBUG("pm = ProcMacro { " << pm.name << ", " << pm.path << ", [" << pm.attributes << "]}");
        return pm;
    }

    ::HIR::TypeImpl deserialise_typeimpl() {
        ::HIR::TypeImpl rv;
        TRACE_FUNCTION_FR("", "impl" << rv.m_params.fmt_args() << " " << rv.m_type);

        rv.m_params = deserialise_genericparams();
        rv.m_type = deserialise_type();

        size_t method_count = m_in.read_count();
        for (size_t i = 0; i < method_count; i++) {
            auto name = m_in.read_istring();
            rv.m_methods.insert(::std::make_pair(mv$(name), ::HIR::TypeImpl::VisImplEnt<::HIR::Function>{deserialise_pub(), m_in.read_bool(), deserialise_function()}));
        }
        size_t const_count = m_in.read_count();
        for (size_t i = 0; i < const_count; i++) {
            auto name = m_in.read_istring();
            rv.m_constants.insert(::std::make_pair(mv$(name), ::HIR::TypeImpl::VisImplEnt<::HIR::Constant>{deserialise_pub(), m_in.read_bool(), deserialise_constant()}));
        }
        // m_src_module doesn't matter after typeck
        return rv;
    }

    ::HIR::TraitImpl deserialise_traitimpl() {
        ::HIR::TraitImpl rv;
        TRACE_FUNCTION_FR("", "impl" << rv.m_params.fmt_args() << " ?" << rv.m_trait_args << " for " << rv.m_type);

        rv.m_params = deserialise_genericparams();
        rv.m_trait_args = deserialise_pathparams();
        rv.m_type = deserialise_type();
        DEBUG("impl" << rv.m_params.fmt_args() << " ?" << rv.m_trait_args << " for " << rv.m_type);

        size_t method_count = m_in.read_count();
        for (size_t i = 0; i < method_count; i++) {
            auto name = m_in.read_istring();
            auto is_spec = m_in.read_bool();
            DEBUG((is_spec ? "default " : "") << "fn " << name);
            rv.m_methods.insert(::std::make_pair(mv$(name), ::HIR::TraitImpl::ImplEnt<::HIR::Function>{is_spec, deserialise_function()}));
        }
        size_t const_count = m_in.read_count();
        for (size_t i = 0; i < const_count; i++) {
            auto name = m_in.read_istring();
            auto is_spec = m_in.read_bool();
            DEBUG((is_spec ? "default " : "") << "const " << name);
            rv.m_constants.insert(::std::make_pair(mv$(name), ::HIR::TraitImpl::ImplEnt<::HIR::Constant>{is_spec, deserialise_constant()}));
        }
        size_t static_count = m_in.read_count();
        for (size_t i = 0; i < static_count; i++) {
            auto name = m_in.read_istring();
            auto is_spec = m_in.read_bool();
            DEBUG((is_spec ? "default " : "") << "static " << name);
            rv.m_statics.insert(::std::make_pair(mv$(name), ::HIR::TraitImpl::ImplEnt<::HIR::Static>{is_spec, deserialise_static()}));
        }
        size_t type_count = m_in.read_count();
        for (size_t i = 0; i < type_count; i++) {
            auto name = m_in.read_istring();
            auto is_spec = m_in.read_bool();
            DEBUG((is_spec ? "default " : "") << "type " << name);
            rv.m_types.insert(::std::make_pair(mv$(name), ::HIR::TraitImpl::ImplEnt<::HIR::TypeRef>{is_spec, deserialise_type()}));
        }

        // m_src_module doesn't matter after typeck
        return rv;
    }

    ::HIR::MarkerImpl deserialise_markerimpl() {
        auto generics = deserialise_genericparams();
        auto params = deserialise_pathparams();
        auto is_neg = m_in.read_bool();
        auto ty = deserialise_type();
        return ::HIR::MarkerImpl{mv$(generics), mv$(params), is_neg, mv$(ty)};
    }

    Ident::Hygiene deserialise_hygine() {
        auto _ = m_in.open_object(typeid(Ident::Hygiene).name());
        Ident::Hygiene rv;
        bool has_mod_path = m_in.read_bool();
        if (has_mod_path) {
            Ident::ModPath mp;
            mp.crate = m_in.read_istring();
            mp.ents = deserialise_vec<RcString>();

            if (mp.crate == "") {
                assert(m_crate_name != "");
                mp.crate = m_crate_name;
            }
            rv.set_mod_path(mv$(mp));
        }
        return rv;
    }

    ::MacroRulesPtr deserialise_macrorulesptr() {
        return ::MacroRulesPtr(new MacroRules(deserialise_macrorules()));
    }

    ::MacroRules deserialise_macrorules() {
        auto crate_name = m_in.read_istring();
        auto edition = static_cast<AST::Edition>(m_in.read_tag());
        ::MacroRules rv(crate_name, edition);
        // NOTE: This is set after loading.
        //rv.m_exported = true;
        rv.m_is_macro_item = m_in.read_bool();
        rv.m_rules = deserialise_vec_c<::MacroRulesArm>([&]() {
            return deserialise_macrorulesarm();
        });
        rv.m_hygiene = deserialise_hygine();
        return rv;
    }

    ::SimplePatIfCheck deserialise_simplepatifcheck() {
        return ::SimplePatIfCheck{static_cast<::MacroPatEnt::Type>(m_in.read_tag()), deserialise_token()};
    }

    ::SimplePatEnt deserialise_simplepatent() {
        auto tag = static_cast<::SimplePatEnt::Tag>(m_in.read_tag());
        switch (tag) {
            case ::SimplePatEnt::TAG_End:
                return ::SimplePatEnt::make_End({});
            case ::SimplePatEnt::TAG_LoopStart:
                return ::SimplePatEnt::make_LoopStart({static_cast<unsigned>(m_in.read_count())});
            case ::SimplePatEnt::TAG_LoopNext:
                return ::SimplePatEnt::make_LoopNext({});
            case ::SimplePatEnt::TAG_LoopEnd:
                return ::SimplePatEnt::make_LoopEnd({});
            case ::SimplePatEnt::TAG_Jump:
                return ::SimplePatEnt::make_Jump({m_in.read_count()});
            case ::SimplePatEnt::TAG_ExpectTok:
                return SimplePatEnt::make_ExpectTok({deserialise_token()});
            case ::SimplePatEnt::TAG_ExpectPat:
                return SimplePatEnt::make_ExpectPat({static_cast<::MacroPatEnt::Type>(m_in.read_tag()), static_cast<unsigned>(m_in.read_count())});
            case SimplePatEnt::TAG_If:
                return SimplePatEnt::make_If({m_in.read_bool(), m_in.read_count(), deserialise_vec_c<SimplePatIfCheck>([&]() {
                    return deserialise_simplepatifcheck();
                })});
            default:
                BUG(Span(), "Bad tag for MacroPatEnt - #" << static_cast<int>(tag));
        }
    }

    ::MacroPatEnt deserialise_macropatent() {
        auto s = m_in.read_istring();
        auto n = static_cast<unsigned int>(m_in.read_count());
        auto type = static_cast<::MacroPatEnt::Type>(m_in.read_tag());
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
        rv.m_param_names = deserialise_vec<RcString>();
        rv.m_pattern = deserialise_vec_c<::SimplePatEnt>([&]() {
            return deserialise_simplepatent();
        });
        rv.m_contents = deserialise_vec_c<::MacroExpansionEnt>([&]() {
            return deserialise_macroexpansionent();
        });
        return rv;
    }

    ::MacroExpansionEnt deserialise_macroexpansionent() {
        switch (auto tag = m_in.read_tag()) {
            case 0:
                return ::MacroExpansionEnt(deserialise_token());
            case 1: {
                unsigned int v = static_cast<unsigned int>(m_in.read_u8()) << 24;
                return ::MacroExpansionEnt(v | m_in.read_count());
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
        switch (auto tag = m_in.read_tag()) {
            case ::MacroExpansionConcatEnt::TAG_Ident: {
                auto h = deserialise_hygine();
                auto n = m_in.read_istring();
                return ::MacroExpansionConcatEnt::make_Ident({h, n});
            }
            case ::MacroExpansionConcatEnt::TAG_Named:
                return ::MacroExpansionConcatEnt::make_Named(m_in.read_count());
            default:
                BUG(Span(), "Bad tag for MacroExpansionConcatEnt - " << tag);
        }
    }

    ::Token deserialise_token() {
        auto ty = static_cast<enum eTokenType>(m_in.read_tag());
        auto d = deserialise_tokendata();
        return ::Token(ty, ::std::move(d), {});
    }

    ::Token::Data deserialise_tokendata() {
        auto tag = static_cast<::Token::Data::Tag>(m_in.read_tag());
        switch (tag) {
            case ::Token::Data::TAG_None:
                return ::Token::Data::make_None({});
            case ::Token::Data::TAG_String:
                return ::Token::Data::make_String(m_in.read_string());
            case ::Token::Data::TAG_Ident: {
                auto hygine = deserialise_hygine();
                auto name = m_in.read_istring();
                return ::Token::Data::make_Ident(Ident(std::move(hygine), std::move(name)));
            }
            case ::Token::Data::TAG_Integer: {
                auto dty = static_cast<eCoreType>(m_in.read_tag());
                return ::Token::Data::make_Integer({dty, m_in.read_u128()});
            }
            case ::Token::Data::TAG_Float: {
                auto dty = static_cast<eCoreType>(m_in.read_tag());
                return ::Token::Data::make_Float({dty, m_in.read_float_value()});
            }
            default:
                BUG(Span(), "Bad tag for Token::Data - " << static_cast<int>(tag));
        }
    }

    ::HIR::ConstGeneric_Unevaluated deserialise_constgeneric_unevaluated();
    ::HIR::ConstGeneric deserialise_constgeneric();
    EncodedLiteral deserialise_encodedliteral();

    ::HIR::ExprPtr deserialise_exprptr() {
        ::HIR::ExprPtr rv;
        auto _ = m_in.open_object("HIR::ExprPtr");
        if (m_in.read_bool()) {
            rv.m_mir = deserialise_mir();
        }
        rv.m_erased_types = deserialise_vec<::HIR::TypeRef>();
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
    ::MIR::SwitchValues deserialise_mir_switchvalues();
    ::MIR::CallTarget deserialise_mir_calltarget();

    ::MIR::Param deserialise_mir_param() {
        switch (auto tag = m_in.read_tag()) {
            case ::MIR::Param::TAG_LValue:
                return deserialise_mir_lvalue();
            case ::MIR::Param::TAG_Borrow:
                return ::MIR::Param::make_Borrow({static_cast<::HIR::BorrowType>(m_in.read_tag()), deserialise_mir_lvalue()});
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
        return ::MIR::LValue::Wrapper::from_inner(m_in.read_count());
    }

    ::MIR::LValue deserialise_mir_lvalue_() {
        auto root_v = m_in.read_count();
        auto root = (root_v == 3 ? ::MIR::LValue::Storage::new_Static(deserialise_path()) : ::MIR::LValue::Storage::from_inner(root_v));
        return ::MIR::LValue(mv$(root), deserialise_vec<::MIR::LValue::Wrapper>());
    }

    ::MIR::RValue deserialise_mir_rvalue() {
        TRACE_FUNCTION;

        switch (auto tag = m_in.read_tag()) {
#define _(x, ...)                \
    case ::MIR::RValue::TAG_##x: \
        return ::MIR::RValue::make_##x(__VA_ARGS__);
            _(Use, deserialise_mir_lvalue())
            _(Constant, deserialise_mir_constant())
            _(SizedArray, {deserialise_mir_param(), deserialise_arraysize()})
            _(Borrow, {static_cast<::HIR::BorrowType>(m_in.read_tag()), m_in.read_bool(), deserialise_mir_lvalue()})
            _(Cast, {deserialise_mir_lvalue(), deserialise_type()})
            _(BinOp, {deserialise_mir_param(), static_cast<::MIR::eBinOp>(m_in.read_tag()), deserialise_mir_param()})
            _(UniOp, {deserialise_mir_lvalue(), static_cast<::MIR::eUniOp>(m_in.read_tag())})
            _(DstMeta, {deserialise_mir_lvalue()})
            _(DstPtr, {deserialise_mir_lvalue()})
            _(MakeDst, {deserialise_mir_param(), m_in.read_bool() ? deserialise_mir_param() : MIR::Constant::make_ItemAddr({})})
            _(Tuple, {deserialise_vec<::MIR::Param>()})
            _(Array, {deserialise_vec<::MIR::Param>()})
            _(UnionVariant, {deserialise_genericpath(), static_cast<unsigned int>(m_in.read_count()), deserialise_mir_param()})
            _(EnumVariant, {deserialise_genericpath(), static_cast<unsigned int>(m_in.read_count()), deserialise_vec<::MIR::Param>()})
            _(Struct, {deserialise_genericpath(), deserialise_vec<::MIR::Param>()})
#undef _
            default:
                BUG(Span(), "Bad tag for MIR::RValue - " << tag);
        }
    }

    ::MIR::Constant deserialise_mir_constant() {
        TRACE_FUNCTION;

        switch (auto tag = m_in.read_tag()) {
#define _(x, ...)                  \
    case ::MIR::Constant::TAG_##x: \
        DEBUG("- " #x);            \
        return ::MIR::Constant::make_##x(__VA_ARGS__);
            _(Int, {m_in.read_i128(), static_cast<::HIR::CoreType>(m_in.read_tag())})
            _(Uint, {m_in.read_u128(), static_cast<::HIR::CoreType>(m_in.read_tag())})
            _(Float, {m_in.read_float_value(), static_cast<::HIR::CoreType>(m_in.read_tag())})
            _(Bool, {m_in.read_bool()})
            case ::MIR::Constant::TAG_Bytes: {
                ::std::vector<unsigned char> bytes;
                bytes.resize(m_in.read_count());
                m_in.read(bytes.data(), bytes.size());
                return ::MIR::Constant::make_Bytes(mv$(bytes));
            }
                _(StaticString, m_in.read_string())
                _(Const, {box$(deserialise_path())})
                _(Generic, deserialise_genericref())
                _(Function, {box$(deserialise_path())})
                _(ItemAddr, {box$(deserialise_path()), m_in.read_u128()})
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
        switch (auto tag = m_in.read_tag()) {
            case 0: {
                auto spath = deserialise_simplepath();
                auto is_variant = m_in.read_bool();
                return ::HIR::TypeItem::make_Import({mv$(spath), is_variant, static_cast<unsigned int>(m_in.read_count())});
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
        switch (auto tag = m_in.read_tag()) {
            case 0: {
                auto spath = deserialise_simplepath();
                auto is_variant = m_in.read_bool();
                return ::HIR::ValueItem::make_Import({mv$(spath), is_variant, static_cast<unsigned int>(m_in.read_count())});
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
        auto _ = m_in.open_object("HIR::MacroItem");
        auto tag = m_in.read_tag();
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
        l.name = m_in.read_string();
        return l;
    }

    // - Value items
    ::HIR::Function deserialise_function() {
        TRACE_FUNCTION;
        auto _ = m_in.open_object("HIR::Function");

        ::HIR::Function rv;
        rv.m_save_code = false;
        rv.m_linkage = deserialise_linkage();
        rv.m_receiver = static_cast<::HIR::Function::Receiver>(m_in.read_tag());
        auto receiver_type = deserialise_type();
        if (rv.m_receiver == ::HIR::Function::Receiver::Custom) {
            rv.m_receiver_type = receiver_type;
        }
        rv.m_abi = m_in.read_istring();
        rv.m_unsafe = m_in.read_bool();
        rv.m_const = m_in.read_bool();
        rv.m_markings = deserialise_function_markings();
        rv.m_params = deserialise_genericparams();
        rv.m_args = deserialise_fcnargs();
        rv.m_variadic = m_in.read_bool();
        rv.m_return = deserialise_type();
        rv.m_code = deserialise_exprptr();
        return rv;
    }

    ::HIR::Function::Markings deserialise_function_markings() {
        auto _ = m_in.open_object("HIR::Function::Markings");
        ::HIR::Function::Markings rv;
        rv.rustc_legacy_const_generics = deserialise_vec<unsigned>();
        rv.track_caller = m_in.read_bool();
        return rv;
    }

    ::std::vector<::std::pair<::HIR::Pattern, ::HIR::TypeRef>> deserialise_fcnargs() {
        size_t n = m_in.read_count();
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
        rv.m_params = deserialise_genericparams();
        rv.m_type = deserialise_type();
        rv.m_value = deserialise_exprptr();
        if (m_in.read_bool()) {
            rv.m_value_res = deserialise_encodedliteral();
            rv.m_value_state = ::HIR::Constant::ValueState::Known;
        } else {
            rv.m_value_state = ::HIR::Constant::ValueState::Generic;
        }
        return rv;
    }

    ::HIR::Static deserialise_static() {
        TRACE_FUNCTION;

        auto linkage = deserialise_linkage();
        auto params = deserialise_genericparams();
        uint8_t bitflag_1 = m_in.read_u8();
#define BIT(i, fld) fld = (bitflag_1 & (1 << (i))) != 0;
        bool is_mut;
        bool save_literal;
        BIT(0, is_mut);
        BIT(1, save_literal);
#undef BIT
        auto ty = deserialise_type();
        auto rv = ::HIR::Static(mv$(linkage), is_mut, mv$(ty), {});
        if (params.is_generic()) {
            rv.m_value = deserialise_exprptr();
        }
        rv.m_params = ::std::move(params);
        if (save_literal) {
            rv.m_value_res = deserialise_encodedliteral();
            rv.m_value_generated = true;
            rv.m_no_emit_value = true;
        }
        return rv;
    }

    // - Type items
    ::HIR::TypeAlias deserialise_typealias() {
        return ::HIR::TypeAlias{deserialise_genericparams(), deserialise_type()};
    }

    ::HIR::TraitMarkings deserialise_markings() {
        ::HIR::TraitMarkings m;
        uint8_t bitflag_1 = m_in.read_u8();
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
        uint8_t bitflag_1 = m_in.read_u8();
#define BIT(i, fld) fld = (bitflag_1 & (1 << (i))) != 0;
        BIT(0, m.can_unsize)
        BIT(1, m.is_nonzero)
        BIT(2, m.bounded_max)
        BIT(3, m.is_fundamental)
#undef BIT
        m.dst_type = static_cast<::HIR::StructMarkings::DstType>(m_in.read_tag());
        m.coerce_unsized = static_cast<::HIR::StructMarkings::Coerce>(m_in.read_tag());
        m.coerce_unsized_index = m_in.read_count();
        m.coerce_param = m_in.read_count();
        m.unsized_field = m_in.read_count();
        m.unsized_param = m_in.read_count();
        if (m.bounded_max) {
            m.bounded_max_value = m_in.read_u128();
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
        switch (auto tag = m_in.read_tag()) {
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
        return ::HIR::AssociatedType{deserialise_genericparams(), m_in.read_bool(), deserialise_lifetimeref(), deserialise_vec<::HIR::TraitPath>(), deserialise_type()};
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
    rv.m_name = m_in.read_istring();
    return rv;
}

::HIR::LifetimeRef HirDeserialiser::deserialise_lifetimeref() {
    ::HIR::LifetimeRef rv;
    rv.binding = static_cast<uint32_t>(m_in.read_count());
    return rv;
}

::HIR::GenericRef HirDeserialiser::deserialise_genericref() {
    return HIR::GenericRef{m_in.read_istring(), m_in.read_u16()};
}

::HIR::ArraySize HirDeserialiser::deserialise_arraysize() {
    switch (auto tag = m_in.read_tag()) {
#define _(x, ...)                   \
    case ::HIR::ArraySize::TAG_##x: \
        DEBUG("- " #x);             \
        return HIR::ArraySize::make_##x(__VA_ARGS__);
        _(Known, m_in.read_u64c())
        _(Unevaluated, deserialise_constgeneric())
        default:
            BUG(Span(), "Bad tag for HIR::ArraySize - " << tag);
#undef _
    }
}

::HIR::TypeRef HirDeserialiser::deserialise_type() {
    ::HIR::TypeRef rv;
    TRACE_FUNCTION_FR("", rv);

    auto idx = m_in.read_count();
    if (idx != ~0u) {
        DEBUG("#" << idx << "");
        rv = m_types.at(idx);
        return rv;
    } else {
        DEBUG("Fresh (=" << m_types.size() << ")");
    }
    auto _ = m_in.open_object("HIR::TypeData");

    switch (auto tag = m_in.read_tag()) {
#define _(x, ...)                                                      \
    case ::HIR::TypeData::TAG_##x:                                     \
        DEBUG("- " #x);                                                \
        rv = m_type_interner.intern(::HIR::TypeData::make_##x(__VA_ARGS__)); \
        break;
        _(Infer, {~0u, HIR::InferClass::None})
        _(Diverge, {})
        _(Primitive, static_cast<::HIR::CoreType>(m_in.read_tag()))
        _(Path, {deserialise_path(), {}, m_in.read_bool() ? box$(deserialise_genericparams()) : nullptr})
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
            _(Borrow, {deserialise_lifetimeref(), static_cast<::HIR::BorrowType>(m_in.read_tag()), deserialise_type()})
            _(Pointer, {static_cast<::HIR::BorrowType>(m_in.read_tag()), deserialise_type()})
            _(NamedFunction, {deserialise_path()})
            _(Function, {deserialise_genericparams(), m_in.read_bool(), m_in.read_bool(), m_in.read_istring(), deserialise_type(), deserialise_vec<::HIR::TypeRef>()})
#undef _
        default:
            BUG(Span(), "Bad tag for HIR::TypeRef - " << tag);
    }
    m_types.push_back(rv);
    return rv;
}

::HIR::SimplePath HirDeserialiser::deserialise_simplepath() {
    TRACE_FUNCTION;
    auto rv = ::HIR::SimplePath{deserialise_thinvec<RcString>()};
    // HACK! If the read crate name is empty, replace it with the name we're loaded with
    if (rv.crate_name() == "" && rv.components().size() > 0) {
        assert(m_crate_name != "");
        rv.update_crate_name(m_crate_name);
    }
    return rv;
}

::HIR::PathParams HirDeserialiser::deserialise_pathparams() {
    ::HIR::PathParams rv;
    TRACE_FUNCTION_FR("", rv);
    rv.m_lifetimes = deserialise_thinvec<::HIR::LifetimeRef>();
    rv.m_types = deserialise_thinvec<::HIR::TypeRef>();
    rv.m_values = deserialise_thinvec<::HIR::ConstGeneric>();
    return rv;
}

::HIR::GenericPath HirDeserialiser::deserialise_genericpath() {
    ::HIR::GenericPath rv;
    TRACE_FUNCTION_FR("", rv);
    rv.m_path = deserialise_simplepath();
    rv.m_params = deserialise_pathparams();
    return rv;
}

::HIR::TraitPath HirDeserialiser::deserialise_traitpath() {
    auto _ = m_in.open_object("HIR::TraitPath");
    auto hrls = m_in.read_bool() ? box$(deserialise_genericparams()) : std::unique_ptr<HIR::GenericParams>();
    auto gpath = deserialise_genericpath();
    auto tys = deserialise_istrmap<::HIR::TraitPath::AtyEqual>();
    auto bounds = deserialise_istrmap<::HIR::TraitPath::AtyBound>();
    return ::HIR::TraitPath{std::move(hrls), mv$(gpath), mv$(tys), mv$(bounds)};
}

::HIR::Path HirDeserialiser::deserialise_path() {
    TRACE_FUNCTION;
    switch (auto tag = m_in.read_tag()) {
        case 0:
            DEBUG("Generic");
            return ::HIR::Path(deserialise_genericpath());
        case 1:
            DEBUG("Inherent");
            return ::HIR::Path(::HIR::Path::Data::Data_UfcsInherent{deserialise_type(), m_in.read_istring(), deserialise_pathparams(), deserialise_pathparams()});
        case 2:
        case 3: {
            std::unique_ptr<HIR::GenericParams> hrtbs;
            if (tag == 3) {
                hrtbs = std::make_unique<HIR::GenericParams>(deserialise_genericparams());
            }
            DEBUG("Known");
            return ::HIR::Path(::HIR::Path::Data::Data_UfcsKnown{deserialise_type(), deserialise_genericpath(), m_in.read_istring(), deserialise_pathparams(), std::move(hrtbs)});
        }
        default:
            BUG(Span(), "Bad tag for HIR::Path - " << tag);
    }
}

::HIR::GenericParams HirDeserialiser::deserialise_genericparams() {
    TRACE_FUNCTION;
    ::HIR::GenericParams params;
    params.m_types = deserialise_vec<::HIR::TypeParamDef>();
    params.m_values = deserialise_vec<::HIR::ValueParamDef>();
    params.m_lifetimes = deserialise_vec<::HIR::LifetimeDef>();
    params.m_bounds = deserialise_vec<::HIR::GenericBound>();
    DEBUG("params = " << params.fmt_args() << ", " << params.fmt_bounds());
    return params;
}

::HIR::TypeParamDef HirDeserialiser::deserialise_typaramdef() {
    auto rv = ::HIR::TypeParamDef{m_in.read_istring(), deserialise_type(), m_in.read_bool()};
    DEBUG("::HIR::TypeParamDef { " << rv.m_name << ", " << rv.m_default << ", " << rv.m_is_sized << "}");
    return rv;
}

::HIR::ValueParamDef HirDeserialiser::deserialise_valueparamdef() {
    auto rv = ::HIR::ValueParamDef{m_in.read_istring(), deserialise_type()};
    rv.m_default = deserialise_constgeneric();
    DEBUG("::HIR::ValueParamDef { " << rv.m_name << ": " << rv.m_type << " = " << rv.m_default << "}");
    return rv;
}

::HIR::GenericBound HirDeserialiser::deserialise_genericbound() {
    switch (auto tag = m_in.read_tag()) {
        case 0:
            return ::HIR::GenericBound::make_Lifetime({deserialise_lifetimeref(), deserialise_lifetimeref()});
        case 1:
            return ::HIR::GenericBound::make_TypeLifetime({deserialise_type(), deserialise_lifetimeref()});
        case 2:
            return ::HIR::GenericBound::make_TraitBound({m_in.read_bool() ? box$(deserialise_genericparams()) : nullptr, deserialise_type(), deserialise_traitpath()});
        case 3:
            return ::HIR::GenericBound::make_TypeEquality({deserialise_type(), deserialise_type()});
        default:
            BUG(Span(), "Bad tag for HIR::GenericBound - " << tag);
    }
}

::HIR::Enum HirDeserialiser::deserialise_enum() {
    TRACE_FUNCTION;
    auto _ = m_in.open_object("HIR::Enum");

    struct H {
        static ::HIR::Enum::Class deserialise_enumclass(HirDeserialiser& des) {
            switch (auto tag = des.m_in.read_tag()) {
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

    return ::HIR::Enum{deserialise_genericparams(), m_in.read_bool(), static_cast<::HIR::Enum::Repr>(m_in.read_tag()), H::deserialise_enumclass(*this), true, deserialise_markings()};
}

::HIR::Enum::DataVariant HirDeserialiser::deserialise_enumdatavariant() {
    auto name = m_in.read_istring();
    DEBUG("Enum::DataVariant " << name);
    return ::HIR::Enum::DataVariant{mv$(name), m_in.read_bool(), deserialise_type(), ::HIR::ExprPtr{}, U128(m_in.read_u64())};
}

::HIR::Enum::ValueVariant HirDeserialiser::deserialise_enumvaluevariant() {
    auto name = m_in.read_istring();
    DEBUG("Enum::ValueVariant " << name);
    return ::HIR::Enum::ValueVariant{mv$(name), ::HIR::ExprPtr{}, U128(m_in.read_u64())};
}

::HIR::Union HirDeserialiser::deserialise_union() {
    TRACE_FUNCTION;
    auto params = deserialise_genericparams();
    auto repr = static_cast<::HIR::Union::Repr>(m_in.read_tag());
    auto variants = deserialise_vec<HIR::StructField>();
    auto markings = deserialise_markings();

    return ::HIR::Union{mv$(params), repr, mv$(variants), mv$(markings)};
}

::HIR::Struct HirDeserialiser::deserialise_struct() {
    TRACE_FUNCTION_FR("", m_in.get_pos());
    auto _ = m_in.open_object("HIR::Struct");
    auto params = deserialise_genericparams();
    DEBUG("params = " << params.fmt_args() << params.fmt_bounds());
    auto repr = static_cast<::HIR::Struct::Repr>(m_in.read_tag());

    ::HIR::Struct::Data data;
    switch (auto tag = m_in.read_tag()) {
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
    unsigned forced_alignment = m_in.read_count();
    unsigned max_field_alignment = m_in.read_count();
    DEBUG("align = " << forced_alignment);
    auto markings = deserialise_markings();
    auto str_markings = deserialise_str_markings();

    auto rv = ::HIR::Struct{mv$(params), repr, mv$(data), forced_alignment, mv$(markings), mv$(str_markings)};
    rv.m_max_field_alignment = max_field_alignment;
    return rv;
}

::HIR::StructField HirDeserialiser::deserialise_struct_field() {
    return HIR::StructField{m_in.read_istring(), deserialise_pub(), deserialise_type(), m_in.read_bool() ? ::std::make_unique<HIR::GenericPath>(deserialise_genericpath()) : nullptr};
}

::HIR::Trait HirDeserialiser::deserialise_trait() {
    TRACE_FUNCTION;
    auto _ = m_in.open_object("HIR::Trait");

    ::HIR::Trait rv{
        deserialise_genericparams(),
        ::HIR::LifetimeRef(), // TODO: Better type for lifetime
        {}
    };
    rv.m_lifetime = deserialise_lifetimeref();
    const auto trait_flags = m_in.read_u8();
    rv.m_is_marker = trait_flags & 1;
    rv.m_is_fundamental = trait_flags & 2;
    rv.m_types = deserialise_istrumap<::HIR::AssociatedType>();
    rv.m_values = deserialise_istrumap<::HIR::TraitValueItem>();
    rv.m_value_indexes = deserialise_istrummap<::std::pair<unsigned int, ::HIR::GenericPath>>();
    rv.m_type_indexes = deserialise_istrumap<unsigned int>();
    rv.m_vtable_parent_traits_start = m_in.read_count();
    rv.m_all_parent_traits = deserialise_vec<::HIR::TraitPath>();
    rv.m_vtable_path = deserialise_simplepath();
    return rv;
}

::HIR::ConstGeneric_Unevaluated HirDeserialiser::deserialise_constgeneric_unevaluated() {
    auto p_i = deserialise_pathparams();
    auto p_m = deserialise_pathparams();
    auto rv = ::HIR::ConstGeneric_Unevaluated(deserialise_exprptr());
    rv.params_impl = std::move(p_i);
    rv.params_item = std::move(p_m);
    return rv;
}

::HIR::ConstGeneric HirDeserialiser::deserialise_constgeneric() {
    switch (auto tag = m_in.read_tag()) {
#define _(x, ...)                      \
    case ::HIR::ConstGeneric::TAG_##x: \
        return ::HIR::ConstGeneric::make_##x(__VA_ARGS__);
        _(Infer, {})
        _(Unevaluated, std::make_unique<HIR::ConstGeneric_Unevaluated>(deserialise_constgeneric_unevaluated()))
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

    auto nreloc = m_in.read_count();
    rv.relocations.reserve(nreloc);
    for (size_t i = 0; i < nreloc; i++) {
        auto ofs = m_in.read_count();
        auto len = m_in.read_count();
        switch (m_in.read_tag()) {
            case 0:
                rv.relocations.push_back(Reloc::new_named(ofs, len, deserialise_path()));
                break;
            case 1:
                rv.relocations.push_back(Reloc::new_bytes(ofs, len, m_in.read_string()));
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

    return ::MIR::BasicBlock{deserialise_vec<::MIR::Statement>(), deserialise_mir_terminator()};
}

AsmCommon::Options HirDeserialiser::deserialise_asm_options() {
    AsmCommon::Options o;
    const uint16_t bitflag_1 = m_in.read_u16();
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
    lf.before = m_in.read_string();
    lf.index = m_in.read_count();
    lf.modifier = static_cast<char>(m_in.read_i64c());
    return lf;
}

AsmCommon::Line HirDeserialiser::deserialise_asm_line() {
    AsmCommon::Line l;
    l.frags = deserialise_vec<AsmCommon::LineFragment>();
    l.trailing = m_in.read_string();
    return l;
}

AsmCommon::RegisterSpec HirDeserialiser::deserialise_asm_spec() {
    switch (auto tag = m_in.read_tag()) {
        case AsmCommon::RegisterSpec::TAG_Class:
            return static_cast<AsmCommon::RegisterClass>(m_in.read_tag());
        case AsmCommon::RegisterSpec::TAG_Explicit:
            return m_in.read_string();
        default:
            BUG(Span(), "Bad tag for AsmCommon::RegisterSpec - " << tag);
    }
}

::MIR::AsmParam HirDeserialiser::deserialise_asm_param() {
    switch (auto tag = m_in.read_tag()) {
        case ::MIR::AsmParam::TAG_Sym:
            return ::MIR::AsmParam::make_Sym(deserialise_path());
        case ::MIR::AsmParam::TAG_Const:
            return ::MIR::AsmParam::make_Const(deserialise_mir_constant());
        case ::MIR::AsmParam::TAG_Reg:
            return ::MIR::AsmParam::make_Reg({static_cast<AsmCommon::Direction>(m_in.read_tag()), deserialise_asm_spec(), m_in.read_bool() ? ::std::make_unique<MIR::Param>(deserialise_mir_param()) : std::unique_ptr<MIR::Param>(), m_in.read_bool() ? ::std::make_unique<MIR::LValue>(deserialise_mir_lvalue()) : std::unique_ptr<MIR::LValue>()});
        default:
            BUG(Span(), "Bad tag for MIR::AsmParam - " << tag);
    }
}

::MIR::Statement HirDeserialiser::deserialise_mir_statement() {
    MIR::Statement rv;
    TRACE_FUNCTION_FR("", rv);
    auto _ = m_in.open_object("MIR::Statement");

    switch (auto tag = m_in.read_tag()) {
        case 0:
            rv = ::MIR::Statement::make_Assign({deserialise_mir_lvalue(), deserialise_mir_rvalue()});
            break;
        case 1:
            rv = ::MIR::Statement::make_Drop({m_in.read_bool() ? ::MIR::eDropKind::DEEP : ::MIR::eDropKind::SHALLOW, deserialise_mir_lvalue(), static_cast<unsigned int>(m_in.read_count())});
            break;
        case 2:
            rv = ::MIR::Statement::make_Asm({m_in.read_string(), deserialise_vec<::std::pair<::std::string, ::MIR::LValue>>(), deserialise_vec<::std::pair<::std::string, ::MIR::LValue>>(), deserialise_vec<::std::string>(), deserialise_vec<::std::string>()});
            break;
        case 3: {
            ::MIR::Statement::Data_SetDropFlag sdf;
            sdf.idx = static_cast<unsigned int>(m_in.read_count());
            sdf.new_val = m_in.read_bool();
            sdf.other = static_cast<unsigned int>(m_in.read_count());
            rv = ::MIR::Statement::make_SetDropFlag(sdf);
        } break;
        case 4:
            rv = ::MIR::Statement::make_ScopeEnd({deserialise_vec<unsigned int>()});
            break;
        case 5:
            rv = ::MIR::Statement::make_Asm2({deserialise_asm_options(), deserialise_vec<AsmCommon::Line>(), deserialise_vec<MIR::AsmParam>()});
            break;
        case 6:
            rv = ::MIR::Statement::make_SaveDropFlag({deserialise_mir_lvalue(), static_cast<unsigned>(m_in.read_count()), static_cast<unsigned>(m_in.read_count())});
            break;
        case 7:
            rv = ::MIR::Statement::make_LoadDropFlag({static_cast<unsigned>(m_in.read_count()), deserialise_mir_lvalue(), static_cast<unsigned>(m_in.read_count())});
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
    switch (auto tag = m_in.read_tag()) {
#define _(x, ...)                    \
    case ::MIR::Terminator::TAG_##x: \
        return ::MIR::Terminator::make_##x(__VA_ARGS__);
        case MIR::Terminator::TAGDEAD:
            BUG(Span(), "MIR::Terminator::TAGDEAD found");
            _(Incomplete, {})
            _(Return, {})
            _(Diverge, {})
            _(Goto, static_cast<unsigned int>(m_in.read_count()))
            _(Panic, {static_cast<unsigned int>(m_in.read_count())})
            _(If, {deserialise_mir_lvalue(), static_cast<unsigned int>(m_in.read_count()), static_cast<unsigned int>(m_in.read_count())})
            _(Switch,
              {deserialise_mir_lvalue(),
               deserialise_vec_c<unsigned int>([&]() {
                return static_cast<unsigned int>(m_in.read_count());
            }),
               static_cast<unsigned int>(m_in.read_count()),
               static_cast<unsigned int>(m_in.read_count())})
            _(SwitchValue,
              {deserialise_mir_lvalue(),
               static_cast<unsigned int>(m_in.read_count()),
               deserialise_vec_c<unsigned int>([&]() {
                return static_cast<unsigned int>(m_in.read_count());
            }),
               deserialise_mir_switchvalues()})
            _(Call, {static_cast<unsigned int>(m_in.read_count()), static_cast<unsigned int>(m_in.read_count()), deserialise_mir_lvalue(), deserialise_mir_calltarget(), deserialise_vec<::MIR::Param>()})
#undef _
        default:
            BUG(Span(), "Bad tag for MIR::Terminator - " << tag);
    }
}

::MIR::SwitchValues HirDeserialiser::deserialise_mir_switchvalues() {
    TRACE_FUNCTION;
    switch (auto tag = m_in.read_tag()) {
#define _(x, ...)                      \
    case ::MIR::SwitchValues::TAG_##x: \
        return ::MIR::SwitchValues::make_##x(__VA_ARGS__);
        _(Unsigned, deserialise_vec_c<uint64_t>([&]() {
            return m_in.read_u64c();
        }))
        _(Signed, deserialise_vec_c<int64_t>([&]() {
            return m_in.read_i64c();
        }))
        _(String, deserialise_vec<::std::string>())
        _(ByteString, deserialise_vec<::std::vector<uint8_t>>())
#undef _
        default:
            BUG(Span(), "Bad tag for MIR::SwitchValues - " << tag);
    }
}

::MIR::CallTarget HirDeserialiser::deserialise_mir_calltarget() {
    switch (auto tag = m_in.read_tag()) {
#define _(x, ...)                    \
    case ::MIR::CallTarget::TAG_##x: \
        return ::MIR::CallTarget::make_##x(__VA_ARGS__);
        _(Value, deserialise_mir_lvalue())
        _(Path, deserialise_path())
        _(Intrinsic, {m_in.read_istring(), deserialise_pathparams()})
#undef _
        default:
            BUG(Span(), "Bad tag for MIR::CallTarget - " << tag);
    }
}

::HIR::Module HirDeserialiser::deserialise_module() {
    TRACE_FUNCTION;
    auto _ = m_in.open_object("HIR::Module");

    ::HIR::Module rv;

    // m_traits doesn't need to be serialised
    rv.m_value_items = deserialise_istrumap<::std::unique_ptr<::HIR::VisEnt<::HIR::ValueItem>>>();
    rv.m_mod_items = deserialise_istrumap<::std::unique_ptr<::HIR::VisEnt<::HIR::TypeItem>>>();
    rv.m_macro_items = deserialise_istrumap<::std::unique_ptr<::HIR::VisEnt<::HIR::MacroItem>>>();

    return rv;
}

::HIR::ExternLibrary HirDeserialiser::deserialise_extlib() {
    return ::HIR::ExternLibrary{m_in.read_string()};
}

void HirDeserialiser::deserialise_crate(::HIR::Crate& rv) {
    // NOTE: This MUST be the first item
    this->m_crate_name = m_in.read_istring();
    assert(this->m_crate_name != "" && "Empty crate name loaded from metadata");
    g_vis_private = ::HIR::Publicity::new_priv(::HIR::SimplePath(this->m_crate_name));
    rv.m_crate_name = this->m_crate_name;
    rv.m_edition = static_cast<AST::Edition>(m_in.read_tag());
    rv.m_root_module = deserialise_module();

    rv.m_type_impls = D<::HIR::Crate::ImplGroup<std::unique_ptr<::HIR::TypeImpl>>>::des(*this);
    rv.m_trait_impls = deserialise_pathmap<::HIR::Crate::ImplGroup<std::unique_ptr<::HIR::TraitImpl>>>();
    rv.m_marker_impls = deserialise_pathmap<::HIR::Crate::ImplGroup<std::unique_ptr<::HIR::MarkerImpl>>>();

    rv.m_exported_macro_names = deserialise_vec<::RcString>();
    //rv.m_exported_macros = deserialise_istrumap< ::MacroRulesPtr>();
    //rv.m_proc_macro_reexports = deserialise_istrumap< ::HIR::Crate::MacroImport>();
    rv.m_lang_items = deserialise_strumap<::HIR::SimplePath>();

    {
        size_t n = m_in.read_count();
        for (size_t i = 0; i < n; i++) {
            auto ext_crate_name = m_in.read_istring();
            auto ext_crate_file = m_in.read_string();
            auto ext_crate = ::HIR::ExternCrate{};
            ext_crate.m_basename = ext_crate_file;
            ext_crate.m_path = ext_crate_file;
            rv.m_ext_crates.insert(::std::make_pair(mv$(ext_crate_name), mv$(ext_crate)));
        }
    }

    rv.m_ext_libs = deserialise_vec<::HIR::ExternLibrary>();
    rv.m_link_paths = deserialise_vec<::std::string>();

    //rv.m_proc_macros = deserialise_vec< ::HIR::ProcMacro>();
}

//}

::HIR::Crate* HIR_Deserialise(stl::ObjPool* pool, ::HIR::TypeInterner& types, const ::std::string& filename) {
    try {
        ::HIR::serialise::Reader in{filename + ".hir"}; // HACK!
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
#if 0
    catch(const char*)
    {
        ::std::cerr << "Unable to load crate from " << filename << ": Deserialisation failure" << ::std::endl;
        ::std::abort();
    }
#endif
}

RcString HIR_Deserialise_JustName(const ::std::string& filename) {
    try {
        ::HIR::serialise::Reader in{filename + ".hir"}; // HACK!

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
#if 0
    catch(const char*)
    {
        ::std::cerr << "Unable to load crate from " << filename << ": Deserialisation failure" << ::std::endl;
        ::std::abort();
    }
#endif
}

#undef DEBUG_EXTRA_ENABLE
#define DEBUG_EXTRA_ENABLE
#undef DEF_D

#include "hir_main_bindings.h"
#include "hir_visitor.h"
#include "hir_expr.h"
#include "hir_expr_state.h"

#define NODE_IS(valptr, tysuf) (dynamic_cast<const ::HIR::ExprNode##tysuf*>(&*valptr) != nullptr)

namespace {

    class TreeVisitor: public ::HIR::Visitor, public ::HIR::ExprVisitor {
        ::std::ostream& m_os;
        unsigned int m_indent_level;

    public:
        TreeVisitor(::HIR::TypeInterner& types, ::std::ostream& os)
            : ::HIR::Visitor(nullptr, types)
            , m_os(os)
            , m_indent_level(0)
        {
        }

        void visit_module(::HIR::ItemPath p, ::HIR::Module& mod) override {
            if (p.get_name()[0]) {
                m_os << indent() << "mod " << p.get_name() << " {\n";
                inc_indent();
            }

            // TODO: Include trait list
            if (true) {
                for (const auto& t : mod.m_traits) {
                    m_os << indent() << "use " << t << ";\n";
                }
            }
            // TODO: Print publicitiy.
            ::HIR::Visitor::visit_module(p, mod);

            if (p.get_name()[0]) {
                dec_indent();
                m_os << indent() << "}\n";
            }
        }

        void visit_type_impl(::HIR::TypeImpl& impl) override {
            m_os << indent() << "impl" << impl.m_params.fmt_args() << " " << impl.m_type << "\n";
            if (!impl.m_params.m_bounds.empty()) {
                m_os << indent() << " " << impl.m_params.fmt_bounds() << "\n";
            }
            m_os << indent() << "{\n";
            inc_indent();
            ::HIR::Visitor::visit_type_impl(impl);
            dec_indent();
            m_os << indent() << "}\n";
        }

        virtual void visit_trait_impl(const ::HIR::SimplePath& trait_path, ::HIR::TraitImpl& impl) override {
            m_os << indent() << "impl" << impl.m_params.fmt_args() << " " << trait_path << impl.m_trait_args << " for " << impl.m_type << "\n";
            if (!impl.m_params.m_bounds.empty()) {
                m_os << indent() << " " << impl.m_params.fmt_bounds() << "\n";
            }
            m_os << indent() << "{\n";
            inc_indent();
            for (auto& ent : impl.m_types) {
                m_os << indent() << "type " << ent.first << " = " << ent.second.data << "\n";
            }
            ::HIR::Visitor::visit_trait_impl(trait_path, impl);
            dec_indent();
            m_os << indent() << "}\n";
        }

        void visit_marker_impl(const ::HIR::SimplePath& trait_path, ::HIR::MarkerImpl& impl) override {
            m_os << indent() << "impl" << impl.m_params.fmt_args() << " " << (impl.is_positive ? "" : "!") << trait_path << impl.m_trait_args << " for " << impl.m_type << "\n";
            if (!impl.m_params.m_bounds.empty()) {
                m_os << indent() << " " << impl.m_params.fmt_bounds() << "\n";
            }
            m_os << indent() << "{ }\n";
        }

        // - Type Items
        void visit_type_alias(::HIR::ItemPath p, ::HIR::TypeAlias& item) override {
            m_os << indent() << "type " << p.get_name() << item.m_params.fmt_args() << " = " << item.m_type << item.m_params.fmt_bounds() << "\n";
        }

        void visit_trait(::HIR::ItemPath p, ::HIR::Trait& item) override {
            m_os << indent() << "trait " << p.get_name() << item.m_params.fmt_args() << " : " << item.m_lifetime << "\n";
            if (!item.m_parent_traits.empty()) {
                m_os << indent() << "  " << ": ";
                bool is_first = true;
                for (auto& bound : item.m_parent_traits) {
                    if (!is_first) {
                        m_os << indent() << "  " << "+ ";
                    }
                    m_os << bound << "\n";
                    is_first = false;
                }
            }
            if (!item.m_params.m_bounds.empty()) {
                m_os << indent() << " " << item.m_params.fmt_bounds() << "\n";
            }
            if (!item.m_all_parent_traits.empty()) {
                m_os << indent() << "/* All parent traits:\n";
                for (const auto& t : item.m_all_parent_traits) {
                    m_os << indent() << t << "\n";
                }
                m_os << indent() << "*/\n";
            }
            m_os << indent() << "{\n";
            inc_indent();

            for (auto& i : item.m_types) {
                m_os << indent() << "type " << i.first;
                if (!i.second.m_trait_bounds.empty()) {
                    m_os << ": ";
                    bool is_first = true;
                    for (auto& bound : i.second.m_trait_bounds) {
                        if (!is_first) {
                            m_os << " + ";
                        }
                        m_os << bound;
                        is_first = false;
                    }
                }
                //this->visit_type(i.second.m_default);
                m_os << ";\n";
            }

            ::HIR::Visitor::visit_trait(p, item);

            dec_indent();
            m_os << indent() << "}\n";
        }

        void visit_struct(::HIR::ItemPath p, ::HIR::Struct& item) override {
            m_os << indent() << "struct " << p.get_name() << item.m_params.fmt_args();
            TU_MATCH_HDRA( (item.m_data), {)
            TU_ARMA(Unit, flds) {
                    if (item.m_params.m_bounds.empty()) {
                        m_os << ";\n";
                    } else {
                        m_os << "\n";
                        m_os << indent() << " " << item.m_params.fmt_bounds() << "\n";
                        m_os << indent() << "    ;\n";
                    }
                }
                TU_ARMA(Tuple, flds) {
                    m_os << "(";
                    for (const auto& fld : flds) {
                        m_os << fld.publicity << " " << fld.ent << ", ";
                    }
                    if (item.m_params.m_bounds.empty()) {
                        m_os << ");\n";
                    } else {
                        m_os << ")\n";
                        m_os << indent() << " " << item.m_params.fmt_bounds() << "\n";
                        m_os << indent() << "    ;\n";
                    }
                }
                TU_ARMA(Named, flds) {
                    m_os << "\n";
                    if (!item.m_params.m_bounds.empty()) {
                        m_os << indent() << " " << item.m_params.fmt_bounds() << "\n";
                    }
                    m_os << indent() << "{\n";
                    inc_indent();
                    for (const auto& fld : flds) {
                        m_os << indent() << fld.vis << " " << fld.name << ": " << fld.ty;
                        if (fld.default_value) {
                            m_os << " = " << *fld.default_value;
                        }
                        m_os << ",\n";
                    }
                    dec_indent();
                    m_os << indent() << "}\n";
                }
            }
        }

        void visit_enum(::HIR::ItemPath p, ::HIR::Enum& item) override {
            m_os << indent() << "enum " << p.get_name() << item.m_params.fmt_args() << "\n";
            if (!item.m_params.m_bounds.empty()) {
                m_os << indent() << " " << item.m_params.fmt_bounds() << "\n";
            }
            m_os << indent() << "{\n";
            inc_indent();
            if (const auto* e = item.m_data.opt_Value()) {
                for (const auto& var : e->variants) {
                    m_os << indent() << var.name;
                    m_os << ",\n";
                }
            } else {
                for (const auto& var : item.m_data.as_Data()) {
                    m_os << indent() << var.name;
                    if (var.type == type_interner().unit()) {
                    } else {
                        m_os << " " << var.type << (var.is_struct ? "/*struct*/" : "");
                    }
                    m_os << ",\n";
                }
            }
            dec_indent();
            m_os << indent() << "}\n";
        }

        // - Value Items
        void visit_function(::HIR::ItemPath p, ::HIR::Function& item) override {
            m_os << indent();
            if (item.m_const) {
                m_os << "const ";
            }
            if (item.m_unsafe) {
                m_os << "unsafe ";
            }
            if (item.m_abi != ABI_RUST) {
                m_os << "extern \"" << item.m_abi << "\" ";
            }
            m_os << "fn " << p.get_name() << item.m_params.fmt_args() << "(";
            for (const auto& arg : item.m_args) {
                m_os << arg.first << ": " << arg.second << ", ";
            }
            m_os << ") -> " << item.m_return << "\n";
            if (!item.m_params.m_bounds.empty()) {
                m_os << indent() << " " << item.m_params.fmt_bounds() << "\n";
            }

            if (item.m_code) {
                m_os << indent();
                if (dynamic_cast<::HIR::ExprNode_Block*>(&*item.m_code)) {
                    item.m_code->visit(*this);
                } else {
                    m_os << "{\n";
                    inc_indent();
                    m_os << indent();

                    item.m_code->visit(*this);

                    m_os << "\n";
                    dec_indent();
                    m_os << indent();
                    m_os << "}";
                }
                m_os << "\n";
            } else {
                m_os << indent() << "  ;\n";
            }
        }

        void visit_static(::HIR::ItemPath p, ::HIR::Static& item) override {
            if (item.m_linkage.name != "") {
                m_os << indent() << "#[link_name=\"" << item.m_linkage.name << "\"]\n";
            }
            if (item.m_value) {
                m_os << indent() << "static " << p.get_name() << item.m_params.fmt_args() << ": " << item.m_type << " = " << item.m_value_res;
            } else if (item.m_value_generated) {
                m_os << indent() << "static " << p.get_name() << item.m_params.fmt_args() << ": " << item.m_type << " = /*magic*/ " << item.m_value_res;
            } else {
                m_os << indent() << "extern static " << p.get_name() << ": " << item.m_type;
            }
            if (!item.m_params.m_bounds.empty()) {
                m_os << indent() << " " << item.m_params.fmt_bounds() << "\n";
            }
            m_os << ";\n";
        }

        void visit_constant(::HIR::ItemPath p, ::HIR::Constant& item) override {
            m_os << indent() << "const " << p.get_name() << ": " << item.m_type << " = " << item.m_value_res;
            if (item.m_value /*&& item.m_value_state != HIR::Constant::ValueState::Known*/) {
                m_os << " /*= ";
                item.m_value->visit(*this);
                m_os << "*/";
            }
            m_os << ";\n";
        }

// - Misc
#if 0
        virtual void visit_params(::HIR::GenericParams& params);
        virtual void visit_pattern(::HIR::Pattern& pat);
        virtual void visit_pattern_val(::HIR::Pattern::Value& val);
        virtual void visit_type(::HIR::TypeRef& tr);

        enum class PathContext {
            TYPE,
            TRAIT,

            VALUE,
        };
        virtual void visit_trait_path(::HIR::TraitPath& p);
        virtual void visit_path(::HIR::Path& p, PathContext );
        virtual void visit_path_params(::HIR::PathParams& p);
        virtual void visit_generic_path(::HIR::GenericPath& p, PathContext );

        virtual void visit_expr(::HIR::ExprPtr& exp);
#endif

        bool node_is_leaf(const ::HIR::ExprNode& node) {
            if (NODE_IS(&node, _PathValue)) {
                return true;
            }
            if (NODE_IS(&node, _Variable)) {
                return true;
            }
            if (NODE_IS(&node, _Literal)) {
                return true;
            }
            if (NODE_IS(&node, _CallPath)) {
                return true;
            }
            if (NODE_IS(&node, _Deref)) {
                return true;
            }
            return false;
        }

        void visit_node_ptr(::HIR::ExprNodeP& node_ptr) override {
            HIR::ExprVisitor::visit_node_ptr(node_ptr);
            m_os << "/*: " << node_ptr->m_res_type << " */";
        }

        void visit(::HIR::ExprNode_Block& node) override {
            m_os << "{\n";
            inc_indent();
            for (auto& sn : node.m_nodes) {
                m_os << indent();
                this->visit_node_ptr(sn);
                m_os << ";\n";
            }
            if (node.m_value_node) {
                m_os << indent();
                this->visit_node_ptr(node.m_value_node);
                m_os << "\n";
            }
            dec_indent();
            m_os << indent() << "}";
        }

        void visit(::HIR::ExprNode_ConstBlock& node) override {
            m_os << "const ";
            node.m_inner->visit(*this);
        }

        void visit(::HIR::ExprNode_Asm& node) override {
            m_os << "llvm_asm!(";
            m_os << ")";
        }

        void visit(::HIR::ExprNode_Asm2& node) override {
            m_os << "asm!(";
            m_os << ")";
        }

        void visit(::HIR::ExprNode_Return& node) override {
            m_os << "return";
            if (node.m_value) {
                m_os << " ";
                this->visit_node_ptr(node.m_value);
            }
        }

        void visit(::HIR::ExprNode_Yield& node) override {
            m_os << "yield";
            if (node.m_value) {
                m_os << " ";
                this->visit_node_ptr(node.m_value);
            }
        }

        void visit(::HIR::ExprNode_AWait& node) override {
            m_os << "(";
            this->visit_node_ptr(node.m_value);
            m_os << ").await";
        }

        void visit(::HIR::ExprNode_Let& node) override {
            m_os << "let " << node.m_pattern << ": " << node.m_type;
            if (node.m_value) {
                m_os << " = ";
                this->visit_node_ptr(node.m_value);
            }
            m_os << ";";
        }

        void visit(::HIR::ExprNode_Loop& node) override {
            if (node.m_label != "") {
                m_os << "'" << node.m_label << ": ";
            }
            m_os << "loop ";
            this->visit_node_ptr(node.m_code);
        }

        void visit(::HIR::ExprNode_LoopControl& node) override {
            m_os << (node.m_continue ? "continue" : "break");
            if (node.m_label != "") {
                m_os << " '" << node.m_label;
            }
            if (node.m_value) {
                m_os << " ";
                this->visit_node_ptr(node.m_value);
            }
        }

        void visit(::HIR::ExprNode_Match& node) override {
            m_os << "match ";
            this->visit_node_ptr(node.m_value);
            m_os << " {\n";
            for (/*const*/ auto& arm : node.m_arms) {
                m_os << indent();
                m_os << arm.m_patterns.front();
                for (unsigned int i = 1; i < arm.m_patterns.size(); i++) {
                    m_os << " | " << arm.m_patterns[i];
                }

                if (arm.m_guards.size() > 0) {
                    m_os << " if ";
                    for (auto& c : arm.m_guards) {
                        if (&c != &arm.m_guards.front()) {
                            m_os << " && ";
                        }
                        m_os << "let " << c.pat << " = ";
                        this->visit_node_ptr(c.val);
                    }
                }
                m_os << " => ";
                inc_indent();
                this->visit_node_ptr(arm.m_code);
                dec_indent();
                m_os << ",\n";
            }
            m_os << indent() << "}";
        }

        void visit(::HIR::ExprNode_Assign& node) override {
            this->visit_node_ptr(node.m_slot);
            m_os << " " << ::HIR::ExprNode_Assign::opname(node.m_op) << "= ";
            this->visit_node_ptr(node.m_value);
        }

        void visit(::HIR::ExprNode_BinOp& node) override {
            m_os << "(";
            this->visit_node_ptr(node.m_left);
            m_os << ")";
            m_os << " " << ::HIR::ExprNode_BinOp::opname(node.m_op) << " ";
            m_os << "(";
            this->visit_node_ptr(node.m_right);
            m_os << ")";
        }

        void visit(::HIR::ExprNode_UniOp& node) override {
            switch (node.m_op) {
                case ::HIR::ExprNode_UniOp::Op::Invert:
                    m_os << "!";
                    break;
                case ::HIR::ExprNode_UniOp::Op::Negate:
                    m_os << "-";
                    break;
            }
            m_os << "(";
            this->visit_node_ptr(node.m_value);
            m_os << ")";
        }

        void visit(::HIR::ExprNode_Borrow& node) override {
            m_os << "&";
            switch (node.m_type) {
                case ::HIR::BorrowType::Shared:
                    break;
                case ::HIR::BorrowType::Unique:
                    m_os << "mut ";
                    break;
                case ::HIR::BorrowType::Owned:
                    m_os << "move ";
                    break;
            }

            bool skip_parens = this->node_is_leaf(*node.m_value) || NODE_IS(node.m_value, _Deref);
            if (!skip_parens) {
                m_os << "(";
            }
            this->visit_node_ptr(node.m_value);
            if (!skip_parens) {
                m_os << ")";
            }
        }

        void visit(::HIR::ExprNode_RawBorrow& node) override {
            m_os << "&raw ";
            switch (node.m_type) {
                case ::HIR::BorrowType::Shared:
                    break;
                case ::HIR::BorrowType::Unique:
                    m_os << "mut ";
                    break;
                case ::HIR::BorrowType::Owned:
                    m_os << "move ";
                    break;
            }

            bool skip_parens = this->node_is_leaf(*node.m_value) || NODE_IS(node.m_value, _Deref);
            if (!skip_parens) {
                m_os << "(";
            }
            this->visit_node_ptr(node.m_value);
            if (!skip_parens) {
                m_os << ")";
            }
        }

        void visit(::HIR::ExprNode_Cast& node) override {
            this->visit_node_ptr(node.m_value);
            m_os << " as " << node.m_dst_type;
        }

        void visit(::HIR::ExprNode_Unsize& node) override {
            this->visit_node_ptr(node.m_value);
            m_os << " : " << node.m_dst_type;
        }

        void visit(::HIR::ExprNode_Index& node) override {
            // TODO: Avoid parens
            m_os << "(";
            this->visit_node_ptr(node.m_value);
            m_os << ")";
            m_os << "[";
            this->visit_node_ptr(node.m_index);
            m_os << "]";
        }

        void visit(::HIR::ExprNode_Deref& node) override {
            m_os << "*";

            bool skip_parens = this->node_is_leaf(*node.m_value);
            if (!skip_parens) {
                m_os << "(";
            }
            this->visit_node_ptr(node.m_value);
            if (!skip_parens) {
                m_os << ")";
            }
        }

        void visit(::HIR::ExprNode_Emplace& node) override {
            if (node.m_type == ::HIR::ExprNode_Emplace::Type::Noop) {
                return node.m_value->visit(*this);
            }
            m_os << "(";
            this->visit_node_ptr(node.m_place);
            m_os << " <- ";
            this->visit_node_ptr(node.m_value);
            m_os << ")";
            m_os << "/*" << (node.m_type == ::HIR::ExprNode_Emplace::Type::Boxer ? "box" : "place") << "*/";
        }

        void visit(::HIR::ExprNode_TupleVariant& node) override {
            m_os << node.m_path;
            m_os << "(";
            for (/*const*/ auto& arg : node.m_args) {
                this->visit_node_ptr(arg);
                m_os << ", ";
            }
            m_os << ")";
        }

        void visit(::HIR::ExprNode_CallPath& node) override {
            m_os << node.m_path;
            m_os << "(";
            for (/*const*/ auto& arg : node.m_args) {
                this->visit_node_ptr(arg);
                m_os << ", ";
            }
            m_os << ")";
            m_os << "/* : " << node.m_res_type << " */";
        }

        void visit(::HIR::ExprNode_CallValue& node) override {
            // TODO: Avoid brackets if not needed
            m_os << "(";
            this->visit_node_ptr(node.m_value);
            m_os << ")";
            m_os << "(";
            for (/*const*/ auto& arg : node.m_args) {
                this->visit_node_ptr(arg);
                m_os << ", ";
            }
            m_os << ")";
        }

        void visit(::HIR::ExprNode_CallMethod& node) override {
            // TODO: Avoid brackets if not needed
            m_os << "(";
            this->visit_node_ptr(node.m_value);
            m_os << ")";
            m_os << "." << node.m_method << node.m_params << "(";
            for (/*const*/ auto& arg : node.m_args) {
                this->visit_node_ptr(arg);
                m_os << ", ";
            }
            m_os << ")";
            if (!node.m_cache.m_arg_types.empty()) {
                m_os << "/*CACHE:" << node.m_cache.m_arg_types << "*/";
            }
        }

        void visit(::HIR::ExprNode_Field& node) override {
            // TODO: Avoid brackets if not needed
            m_os << "(";
            this->visit_node_ptr(node.m_value);
            m_os << ")";
            m_os << "." << node.m_field;
        }

        void visit(::HIR::ExprNode_Literal& node) override {
            TU_MATCH_HDRA( (node.m_data), {)
            TU_ARMA(Integer, e) {
                    switch (e.m_type) {
                        case ::HIR::CoreType::U8:
                            m_os << e.m_value << "_u8";
                            break;
                        case ::HIR::CoreType::U16:
                            m_os << e.m_value << "_u16";
                            break;
                        case ::HIR::CoreType::U32:
                            m_os << e.m_value << "_u32";
                            break;
                        case ::HIR::CoreType::U64:
                            m_os << e.m_value << "_u64";
                            break;
                        case ::HIR::CoreType::Usize:
                            m_os << e.m_value << "_usize";
                            break;
                        case ::HIR::CoreType::I8:
                            m_os << /*I128*/ (e.m_value) << "_i8";
                            break;
                        case ::HIR::CoreType::I16:
                            m_os << /*I128*/ (e.m_value) << "_i16";
                            break;
                        case ::HIR::CoreType::I32:
                            m_os << /*I128*/ (e.m_value) << "_i32";
                            break;
                        case ::HIR::CoreType::I64:
                            m_os << /*I128*/ (e.m_value) << "_i64";
                            break;
                        case ::HIR::CoreType::Isize:
                            m_os << /*I128*/ (e.m_value) << "_isize";
                            break;
                        case ::HIR::CoreType::Char: {
                            auto v = e.m_value.truncate_u64();
                            if (v == '\\' || v == '\'') {
                                m_os << "'\\" << static_cast<char>(v) << "'";
                            } else if (' ' <= v && v <= 0x7F) {
                                m_os << "'" << static_cast<char>(v) << "'";
                            } else {
                                m_os << "'\\u{" << ::std::hex << v << ::std::dec << "}'";
                            }
                        } break;
                        default:
                            m_os << e.m_value << "_unk";
                            break;
                    }
                }
                TU_ARMA(Float, e) {
                    switch (e.m_type) {
                        case ::HIR::CoreType::F32:
                            m_os << e.m_value << "_f32";
                            break;
                        case ::HIR::CoreType::F64:
                            m_os << e.m_value << "_f64";
                            break;
                        default:
                            m_os << e.m_value << "_unk";
                            break;
                    }
                }
                TU_ARMA(Boolean, e) {
                    m_os << (e ? "true" : "false");
                }
                TU_ARMA(String, e) {
                    m_os << "\"" << FmtEscaped(e) << "\"";
                }
                TU_ARMA(CString, e) {
                    m_os << "c\"" << FmtEscaped(e.v) << "\"";
                }
                TU_ARMA(ByteString, e) {
                    m_os << "b\"";
                    for (auto b : e) {
                        if (b == '\\' || b == '\"') {
                            m_os << "\\" << b;
                        } else if (' ' <= b && b <= 0x7F) {
                            m_os << b;
                        } else {
                            char buf[3];
                            sprintf(buf, "%02x", static_cast<uint8_t>(b));
                            m_os << "\\x" << buf;
                        }
                    }
                    m_os << "\"";
                }
            }
        }

        void visit(::HIR::ExprNode_UnitVariant& node) override {
            m_os << node.m_path;
        }

        void visit(::HIR::ExprNode_PathValue& node) override {
            m_os << node.m_path;
        }

        void visit(::HIR::ExprNode_Variable& node) override {
            m_os << node.m_name << "#" << node.m_slot;
        }

        void visit(::HIR::ExprNode_ConstParam& node) override {
            m_os << node.m_name << "#" << node.m_binding;
        }

        void visit(::HIR::ExprNode_StructLiteral& node) override {
            m_os << node.m_type << " {\n";
            inc_indent();
            for (/*const*/ auto& val : node.m_values) {
                m_os << indent() << val.first << ": ";
                this->visit_node_ptr(val.second);
                m_os << ",\n";
            }
            if (node.m_base_value) {
                m_os << indent() << ".. ";
                this->visit_node_ptr(node.m_base_value);
                m_os << "\n";
            }
            m_os << indent() << "}";
            dec_indent();
        }

        void visit(::HIR::ExprNode_Tuple& node) override {
            m_os << "(";
            for (/*const*/ auto& val : node.m_vals) {
                this->visit_node_ptr(val);
                m_os << ", ";
            }
            m_os << ")";
        }

        void visit(::HIR::ExprNode_ArrayList& node) override {
            m_os << "[";
            for (/*const*/ auto& val : node.m_vals) {
                this->visit_node_ptr(val);
                m_os << ", ";
            }
            m_os << "]";
        }

        void visit(::HIR::ExprNode_ArraySized& node) override {
            m_os << "[";
            this->visit_node_ptr(node.m_val);
            m_os << "; " << node.m_size;
            m_os << "]";
        }

        void visit(::HIR::ExprNode_Closure& node) override {
            if (node.m_code) {
                if (node.m_is_move) {
                    m_os << " move";
                }
                m_os << "|";
                for (const auto& arg : node.m_args) {
                    m_os << arg.first << ": " << arg.second << ", ";
                }
                m_os << "| -> " << node.m_return << " ";
                this->visit_node_ptr(node.m_code);
            } else {
                m_os << node.m_obj_path << "( ";
                for (/*const*/ auto& cap : node.m_captures) {
                    this->visit_node_ptr(cap);
                    m_os << ", ";
                }
                m_os << ")";
            }
        }

        void visit(::HIR::ExprNode_Generator& node) override {
            if (node.m_code) {
                m_os << "/*gen*/";
                if (node.m_is_pinned) {
                    m_os << "static ";
                }
                if (node.m_is_move) {
                    m_os << " move";
                }
                m_os << "|";
                //for(const auto& arg : node.m_args)
                //    m_os << arg.first << ": " << arg.second << ", ";
                m_os << "| -> " << node.m_return << " ";
                this->visit_node_ptr(node.m_code);
            } else {
                m_os << node.m_obj_path << "( ";
                for (/*const*/ auto& cap : node.m_captures) {
                    this->visit_node_ptr(cap);
                    m_os << ", ";
                }
                m_os << ")";
            }
        }

        void visit(::HIR::ExprNode_GeneratorWrapper& node) override {
            m_os << "/*gen body*/";
            m_os << "|";
            //for(const auto& arg : node.m_args)
            //    m_os << arg.first << ": " << arg.second << ", ";
            m_os << "| -> " << node.m_return << " ";
            this->visit_node_ptr(node.m_code);
        }

        void visit(::HIR::ExprNode_AsyncBlock& node) override {
            if (node.m_is_move) {
                m_os << "move ";
            }
            m_os << "async {";
            if (!node.m_code) {
                m_os << "/* lowered: " << node.m_obj_path << " */";
            } else {
                this->visit_node_ptr(node.m_code);
            }
            m_os << "}";
        }

    private:
        RepeatLitStr indent() const {
            return RepeatLitStr{"    ", static_cast<int>(m_indent_level)};
        }

        void inc_indent() {
            m_indent_level++;
        }

        void dec_indent() {
            m_indent_level--;
        }
    };
}

void HIR_Dump(::std::ostream& sink, const ::HIR::Crate& crate) {
    TreeVisitor tv{crate.m_types, sink};

    tv.visit_crate(const_cast<::HIR::Crate&>(crate));
}

void HIR_DumpExpr(::std::ostream& sink, const ::HIR::ExprPtr& expr) {
    if (!expr) {
        sink << "/*NULL*/";
        return;
    }

    assert(expr.m_state);
    TreeVisitor tv{expr.m_state->m_types, sink};

    const_cast<HIR::ExprPtr&>(expr)->visit(tv);
}

#undef NODE_IS

#include "hir_hir.h"
#include "hir_main_bindings.h"
#include "macro_rules_macro_rules.h"
#include "mir_mir.h"
#include "hir_serialise_lowlevel.h"
#include "hir_typeck_monomorph.h" // monomorphise_path_needed

//namespace {
class HirSerialiser {
    ::std::map<std::string, size_t> m_types;
    ::HIR::serialise::Writer& m_out;
    ::HIR::TypeInterner& m_type_interner;

public:
    HirSerialiser(::HIR::serialise::Writer& out, ::HIR::TypeInterner& type_interner)
        : m_out(out)
        , m_type_interner(type_interner)
    {
    }

    void clear() {
        m_types.clear();
    }

    template <typename V>
    void serialise_strmap(const ::std::map<RcString, V>& map) {
        m_out.write_count(map.size());
        for (const auto& v : map) {
            DEBUG(v.first);
            m_out.write_string(v.first);
            serialise(v.second);
        }
    }

    template <typename V>
    void serialise_strmap(const ::std::map<::std::string, V>& map) {
        m_out.write_count(map.size());
        for (const auto& v : map) {
            m_out.write_string(v.first);
            serialise(v.second);
        }
    }

    template <typename V>
    void serialise_pathmap(const ::std::map<::HIR::SimplePath, V>& map) {
        m_out.write_count(map.size());
        for (const auto& v : map) {
            DEBUG("- " << v.first);
            serialise(v.first);
            serialise(v.second);
        }
    }

    template <typename V>
    void serialise_strmap(const ::std::unordered_map<RcString, V>& map) {
        m_out.write_count(map.size());
        for (const auto& v : map) {
            DEBUG("- " << v.first);
            m_out.write_string(v.first);
            serialise(v.second);
        }
    }

    template <typename V>
    void serialise_strmap(const ::std::unordered_map<::std::string, V>& map) {
        m_out.write_count(map.size());
        for (const auto& v : map) {
            DEBUG("- " << v.first);
            m_out.write_string(v.first);
            serialise(v.second);
        }
    }

    template <typename V>
    void serialise_strmap(const ::std::unordered_multimap<RcString, V>& map) {
        m_out.write_count(map.size());
        for (const auto& v : map) {
            DEBUG("- " << v.first);
            m_out.write_string(v.first);
            serialise(v.second);
        }
    }

    template <typename V>
    void serialise_strmap(const ::std::unordered_multimap<::std::string, V>& map) {
        m_out.write_count(map.size());
        for (const auto& v : map) {
            DEBUG("- " << v.first);
            m_out.write_string(v.first);
            serialise(v.second);
        }
    }

    template <typename T>
    void serialise_vec(const ThinVector<T>& vec) {
        TRACE_FUNCTION_F("<" << typeid(T).name() << "> size=" << vec.size());
        auto _ = m_out.open_object(typeid(ThinVector<T>).name());
        m_out.write_count(vec.size());
        for (const auto& i : vec) {
            serialise(i);
        }
    }

    template <typename T>
    void serialise_vec(const ::std::vector<T>& vec) {
        TRACE_FUNCTION_F("<" << typeid(T).name() << "> size=" << vec.size());
        auto _ = m_out.open_object(typeid(::std::vector<T>).name());
        m_out.write_count(vec.size());
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
        auto _ = m_out.open_object(typeid(::std::set<T>).name());
        m_out.write_count(s.size());
        for (const auto& i : s) {
            serialise(i);
        }
    }

    void serialise(const ::HIR::Publicity& pub) {
        m_out.write_bool(pub.is_global());
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
        m_out.write_string(e.first);
        serialise(e.second);
    }

    template <typename T>
    void serialise(const ::std::pair<RcString, T>& e) {
        m_out.write_string(e.first);
        serialise(e.second);
    }

    template <typename T>
    void serialise(const ::std::pair<unsigned int, T>& e) {
        m_out.write_count(e.first);
        serialise(e.second);
    }

    //void serialise(::MIR::BasicBlockId val) {
    //    m_out.write_count(val);
    //}

    void serialise(bool v) {
        m_out.write_bool(v);
    };

    void serialise(unsigned int v) {
        m_out.write_count(v);
    };

    void serialise(uint8_t v) {
        m_out.write_u8(v);
    };

    void serialise(uint64_t v) {
        m_out.write_u64c(v);
    };

    void serialise(int64_t v) {
        m_out.write_i64c(v);
    };

    void serialise(const ::HIR::LifetimeDef& ld) {
        m_out.write_string(ld.m_name);
    }

    void serialise(const ::HIR::LifetimeRef& lr) {
        m_out.write_count(lr.binding);
    }

    void serialise(const ::HIR::GenericRef& ge) {
        m_out.write_string(ge.name);
        m_out.write_u16(ge.binding);
    }

    void serialise_arraysize(const ::HIR::ArraySize& as) {
        m_out.write_tag(static_cast<int>(as.tag()));
            TU_MATCH_HDRA( (as), { )
            TU_ARMA(Unevaluated, se) {
                serialise(se);
            }
            TU_ARMA(Known, se) {
                m_out.write_u64c(se);
            }
            }
    }

    void serialise_type(const ::HIR::TypeRef& ty) {
        // Use string comparison to ensure that lifetimes are checked
        auto ty_str = FMT(ty);
        if (ty_str[0] == '{') {
            auto p = ty_str.find('}');
            ty_str = ty_str.substr(p + 1);
        }

        auto it = m_types.find(ty_str);
        if (it != m_types.end()) {
            DEBUG("Cached " << it->second);
            m_out.write_count(it->second);
            return;
        }
        m_out.write_count(~0u);
        DEBUG("Fresh " << m_types.size());

        auto _ = m_out.open_object("HIR::TypeData");
        m_out.write_tag(ty->tag());
            TU_MATCH_HDRA( (*ty), {)
            TU_ARMA(Infer, e) {
                // BAAD
            }
            TU_ARMA(Diverge, e) {
            }
            TU_ARMA(Primitive, e) {
                m_out.write_tag(static_cast<int>(e));
            }
            TU_ARMA(Path, e) {
                serialise_path(e.path);
                m_out.write_bool(e.hrtbs.get() != nullptr);
                if (e.hrtbs) {
                    serialise_generics(*e.hrtbs);
                }
            }
            TU_ARMA(Generic, e) {
                serialise(e);
            }
            TU_ARMA(TraitObject, e) {
                serialise_traitpath(e.m_trait);
                serialise_vec(e.m_markers);
                serialise(e.m_lifetime);
            }
            TU_ARMA(ErasedType, e) {
                TODO(Span(), "Serialse ErasedType?");
                //serialise_path(e.m_origin);
                //m_out.write_count(e.m_index);

                m_out.write_bool(e.m_is_sized);
                serialise_vec(e.m_traits);
                serialise_vec(e.m_lifetime_bounds);
                serialise_pathparams(e.m_use);
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
                m_out.write_tag(static_cast<int>(e.type));
                serialise_type(e.inner);
            }
            TU_ARMA(Pointer, e) {
                m_out.write_tag(static_cast<int>(e.type));
                serialise_type(e.inner);
            }
            TU_ARMA(NamedFunction, e) {
                serialise_path(e.path);
            }
            TU_ARMA(Function, e) {
                serialise_generics(e.hrls);
                m_out.write_bool(e.is_unsafe);
                m_out.write_bool(e.is_variadic);
                m_out.write_string(e.m_abi);
                serialise_type(e.m_rettype);
                serialise_vec(e.m_arg_types);
            }
            break;
            case ::HIR::TypeData::TAG_NodeType:
                BUG(Span(), "Encountered invalid type when serialising - " << ty);
                break;
            }

            m_types.insert(std::make_pair( std::move(ty_str), m_types.size() ));
    }

    void serialise_simplepath(const ::HIR::SimplePath& path) {
        TRACE_FUNCTION_F(path);
        serialise_vec(path.m_members);
    }

    void serialise_pathparams(const ::HIR::PathParams& pp) {
        serialise_vec(pp.m_lifetimes);
        serialise_vec(pp.m_types);
        serialise_vec(pp.m_values);
    }

    void serialise_genericpath(const ::HIR::GenericPath& path) {
        TRACE_FUNCTION_F(path);
        serialise_simplepath(path.m_path);
        serialise_pathparams(path.m_params);
    }

    void serialise(const ::HIR::GenericPath& path) {
        serialise_genericpath(path);
    }

    void serialise_traitpath(const ::HIR::TraitPath& path) {
        auto _ = m_out.open_object("HIR::TraitPath");
        m_out.write_bool(static_cast<bool>(path.m_hrtbs));
        if (path.m_hrtbs) {
            serialise_generics(*path.m_hrtbs);
        }
        serialise_genericpath(path.m_path);
        serialise_strmap(path.m_type_bounds);
        serialise_strmap(path.m_trait_bounds);
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
            TU_MATCH_HDRA( (path.m_data), {)
            TU_ARMA(Generic, e) {
                m_out.write_tag(0);
                serialise_genericpath(e);
            }
            TU_ARMA(UfcsInherent, e) {
                m_out.write_tag(1);
                serialise_type(e.type);
                m_out.write_string(e.item);
                serialise_pathparams(e.params);
                serialise_pathparams(e.impl_params);
            }
            TU_ARMA(UfcsKnown, e) {
                if (e.hrtbs) {
                    m_out.write_tag(3);
                    serialise_generics(*e.hrtbs);
                } else {
                    m_out.write_tag(2);
                }
                serialise_type(e.type);
                serialise_genericpath(e.trait);
                m_out.write_string(e.item);
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
        serialise_vec(params.m_types);
        serialise_vec(params.m_values);
        serialise_vec(params.m_lifetimes);
        serialise_vec(params.m_bounds);
    }

    void serialise(const ::HIR::TypeParamDef& pd) {
        m_out.write_string(pd.m_name);
        serialise_type(pd.m_default);
        m_out.write_bool(pd.m_is_sized);
    }

    void serialise(const ::HIR::ValueParamDef& pd) {
        m_out.write_string(pd.m_name);
        serialise_type(pd.m_type);
        serialise(pd.m_default);
    }

    void serialise(const ::HIR::GenericBound& b) {
        TRACE_FUNCTION_F(b);
            TU_MATCH_HDRA( (b), {)
            TU_ARMA(Lifetime, e) {
                m_out.write_tag(0);
                serialise(e.test);
                serialise(e.valid_for);
            }
            TU_ARMA(TypeLifetime, e) {
                m_out.write_tag(1);
                serialise_type(e.type);
                serialise(e.valid_for);
            }
            TU_ARMA(TraitBound, e) {
                m_out.write_tag(2);
                m_out.write_bool(static_cast<bool>(e.hrtbs));
                if (e.hrtbs) {
                    serialise_generics(*e.hrtbs);
                }
                serialise_type(e.type);
                serialise_traitpath(e.trait);
            }
            TU_ARMA(TypeEquality, e) {
                m_out.write_tag(3);
                serialise_type(e.type);
                serialise_type(e.other_type);
            }
            }
    }

    void serialise(const ::HIR::ProcMacro& pm) {
        TRACE_FUNCTION_F("pm = ProcMacro { " << pm.name << ", " << pm.path << ", [" << pm.attributes << "] }");
        switch (pm.ty) {
            case ::HIR::ProcMacro::Ty::Function:
                m_out.write_tag(0);
                break;
            case ::HIR::ProcMacro::Ty::Derive:
                m_out.write_tag(1);
                break;
            case ::HIR::ProcMacro::Ty::Attribute:
                m_out.write_tag(2);
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
        m_out.write_string(crate.m_crate_name);
        m_out.write_tag(static_cast<int>(crate.m_edition));
        serialise_module(crate.m_root_module);

        serialise(crate.m_type_impls);
        serialise_pathmap(crate.m_trait_impls);
        serialise_pathmap(crate.m_marker_impls);

        serialise_vec(crate.m_exported_macro_names);

        {
            decltype(crate.m_lang_items) lang_items_filtered;
            for (const auto& ent : crate.m_lang_items) {
                if (ent.second.crate_name() == "" || ent.second.crate_name() == crate.m_crate_name) {
                    lang_items_filtered.insert(ent);
                }
            }
            serialise_strmap(lang_items_filtered);
        }

        m_out.write_count(crate.m_ext_crates.size());
        for (const auto& ext : crate.m_ext_crates) {
            m_out.write_string(ext.first);
            m_out.write_string(ext.second.m_basename);
            //m_out.write_string(ext.second.m_path);
        }
        serialise_vec(crate.m_ext_libs);
        serialise_vec(crate.m_link_paths);
    }

    void serialise(const ::HIR::ExternLibrary& lib) {
        m_out.write_string(lib.name);
    }

    void serialise_module(const ::HIR::Module& mod) {
        TRACE_FUNCTION;
        auto _ = m_out.open_object("HIR::Module");

        // m_traits doesn't need to be serialised

        serialise_strmap(mod.m_value_items);
        serialise_strmap(mod.m_mod_items);
        serialise_strmap(mod.m_macro_items);
    }

    void serialise_typeimpl(const ::HIR::TypeImpl& impl) {
        TRACE_FUNCTION_F("impl" << impl.m_params.fmt_args() << " " << impl.m_type);
        serialise_generics(impl.m_params);
        serialise_type(impl.m_type);

        m_out.write_count(impl.m_methods.size());
        for (const auto& v : impl.m_methods) {
            m_out.write_string(v.first);
            m_out.write_bool(v.second.publicity.is_global());
            m_out.write_bool(v.second.is_specialisable);
            serialise(v.second.data);
        }
        m_out.write_count(impl.m_constants.size());
        for (const auto& v : impl.m_constants) {
            m_out.write_string(v.first);
            m_out.write_bool(v.second.publicity.is_global());
            m_out.write_bool(v.second.is_specialisable);
            serialise(v.second.data);
        }
        // m_src_module doesn't matter after typeck
    }

    void serialise(const ::HIR::TypeImpl& impl) {
        serialise_typeimpl(impl);
    }

    void serialise_traitimpl(const ::HIR::TraitImpl& impl) {
        TRACE_FUNCTION_F("impl" << impl.m_params.fmt_args() << " ?" << impl.m_trait_args << " for " << impl.m_type);
        serialise_generics(impl.m_params);
        serialise_pathparams(impl.m_trait_args);
        serialise_type(impl.m_type);

        m_out.write_count(impl.m_methods.size());
        for (const auto& v : impl.m_methods) {
            DEBUG("fn " << v.first);
            m_out.write_string(v.first);
            m_out.write_bool(v.second.is_specialisable);
            serialise(v.second.data);
        }
        m_out.write_count(impl.m_constants.size());
        for (const auto& v : impl.m_constants) {
            DEBUG("const " << v.first);
            m_out.write_string(v.first);
            m_out.write_bool(v.second.is_specialisable);
            serialise(v.second.data);
        }
        m_out.write_count(impl.m_statics.size());
        for (const auto& v : impl.m_statics) {
            DEBUG("static " << v.first);
            m_out.write_string(v.first);
            m_out.write_bool(v.second.is_specialisable);
            serialise(v.second.data);
        }
        m_out.write_count(impl.m_types.size());
        for (const auto& v : impl.m_types) {
            DEBUG("type " << v.first);
            m_out.write_string(v.first);
            m_out.write_bool(v.second.is_specialisable);
            serialise(v.second.data);
        }
        // m_src_module doesn't matter after typeck
    }

    void serialise(const ::HIR::TraitImpl& impl) {
        serialise_traitimpl(impl);
    }

    void serialise_markerimpl(const ::HIR::MarkerImpl& impl) {
        serialise_generics(impl.m_params);
        serialise_pathparams(impl.m_trait_args);
        m_out.write_bool(impl.is_positive);
        serialise_type(impl.m_type);
    }

    void serialise(const ::HIR::MarkerImpl& impl) {
        serialise_markerimpl(impl);
    }

    void serialise(const ::HIR::TypeRef& ty) {
        serialise_type(ty);
    }

    void serialise(const ::HIR::SimplePath& p) {
        serialise_simplepath(p);
    }

    void serialise(const ::HIR::TraitPath& p) {
        serialise_traitpath(p);
    }

    void serialise(const ::std::string& v) {
        m_out.write_string(v);
    }

    void serialise(const RcString& v) {
        m_out.write_string(v);
    }

    void serialise(const Ident::Hygiene& h) {
        auto _ = m_out.open_object(typeid(Ident::Hygiene).name());
        m_out.write_bool(h.has_mod_path());
        if (h.has_mod_path()) {
            m_out.write_string(h.mod_path().crate);
            serialise_vec(h.mod_path().ents);
        }
    }

    void serialise(const ::MacroRulesPtr& mac) {
        serialise(*mac);
    }

    void serialise(const ::MacroRules& mac) {
        //m_exported: IGNORE, should be set
        m_out.write_string(mac.m_source_crate);
        m_out.write_tag(static_cast<unsigned int>(mac.m_edition));
        assert(mac.m_rules.size() > 0);
        m_out.write_bool(mac.m_is_macro_item);
        serialise_vec(mac.m_rules);
        serialise(mac.m_hygiene);
    }

    void serialise(const ::MacroPatEnt& pe) {
        m_out.write_string(pe.name);
        m_out.write_count(pe.name_index);
        m_out.write_tag(static_cast<int>(pe.type));
        if (pe.type == ::MacroPatEnt::PAT_TOKEN) {
            serialise(pe.tok);
        } else if (pe.type == ::MacroPatEnt::PAT_LOOP) {
            serialise(pe.tok);
            serialise_vec(pe.subpats);
        }
    }

    void serialise(const ::SimplePatIfCheck& e) {
        m_out.write_tag(static_cast<int>(e.ty));
        serialise(e.tok);
    }

    void serialise(const ::SimplePatEnt& pe) {
        m_out.write_tag(pe.tag());
            TU_MATCH_HDRA( (pe), { )
            TU_ARMA(End, _e) {
            }
            TU_ARMA(LoopStart, e) {
                m_out.write_count(e.index);
            }
            TU_ARMA(LoopNext, _e) {
            }
            TU_ARMA(LoopEnd, _e) {
            }
            TU_ARMA(Jump, e) {
                m_out.write_count(e.jump_target);
            }
            TU_ARMA(ExpectTok, e) {
                serialise(e);
            }
            TU_ARMA(ExpectPat, e) {
                m_out.write_tag(static_cast<int>(e.type));
                m_out.write_count(e.idx);
            }
            TU_ARMA(If, e) {
                m_out.write_bool(e.is_equal);
                m_out.write_count(e.jump_target);
                serialise_vec(e.ents);
            }
            }
    }

    void serialise(const ::MacroRulesArm& arm) {
        serialise_vec(arm.m_param_names);
        serialise_vec(arm.m_pattern);
        serialise_vec(arm.m_contents);
    }

    void serialise(const ::MacroExpansionEnt& ent) {
            TU_MATCH_HDRA( (ent), {)
            TU_ARMA(Token, e) {
                m_out.write_tag(0);
                serialise(e);
            }
            TU_ARMA(NamedValue, e) {
                m_out.write_tag(1);
                m_out.write_u8(e >> 24);
                m_out.write_count(e & 0x00FFFFFF);
            }
            TU_ARMA(Loop, e) {
                m_out.write_tag(2);
                serialise_vec(e.entries);
                serialise(e.joiner);
                serialise(e.controlling_input_loops);
            }
            TU_ARMA(Concat, e) {
                m_out.write_tag(3);
                serialise_vec(e);
            }
            }
    }

    void serialise(const ::MacroExpansionConcatEnt& e) {
        m_out.write_tag(e.tag());
            TU_MATCH_HDRA((e), {)
            TU_ARMA(Ident, i) {
                serialise(i.hygiene);
                m_out.write_string(i.name);
            }
            TU_ARMA(Named, i) {
                serialise(i);
            }
            }
    }

    void serialise(const ::Token& tok) {
        m_out.write_tag(tok.m_type);
        serialise(tok.m_data);
        // TODO: Position information.
    }

    void serialise(const ::Token::Data& td) {
        m_out.write_tag(td.tag());
        switch (td.tag()) {
            case ::Token::Data::TAGDEAD:
                throw "";
                TU_ARM(td, None, _e) {
                }
                break;
                TU_ARM(td, String, e) {
                    m_out.write_string(e);
                }
                break;
                TU_ARM(td, Ident, e) {
                    serialise(e.hygiene);
                    m_out.write_string(e.name);
                }
                break;
                TU_ARM(td, Integer, e) {
                    m_out.write_tag(e.m_datatype);
                    m_out.write_u128(e.m_intval);
                }
                break;
                TU_ARM(td, Float, e) {
                    m_out.write_tag(e.m_datatype);
                    m_out.write_float_value(e.m_floatval);
                }
                break;
                TU_ARM(td, Fragment, e)
                assert(!"Serialising interpolated macro fragment - should have been handled in HIR lowering");
        }
    }

    void serialise(const EncodedLiteral& lit) {
        serialise(lit.bytes);
        m_out.write_count(lit.relocations.size());
        for (const auto& reloc : lit.relocations) {
            m_out.write_count(reloc.ofs);
            m_out.write_count(reloc.len);
            if (reloc.p) {
                m_out.write_tag(0);
                serialise_path(*reloc.p);
            } else {
                m_out.write_tag(1);
                serialise(reloc.bytes);
            }
        }
    }

    void serialise(const ::HIR::ConstGeneric_Unevaluated& v) {
        ASSERT_BUG(v.expr->span(), v.expr->m_mir, "Encountered non-translated value in ConstGeneric: " << v);
        serialise_pathparams(v.params_impl);
        serialise_pathparams(v.params_item);
        serialise(*v.expr);
    }

    void serialise(const ::HIR::ConstGeneric& v) {
        m_out.write_tag(v.tag());
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
        auto _ = m_out.open_object("HIR::ExprPtr");
        save_mir &= static_cast<bool>(exp.m_mir);
        m_out.write_bool(save_mir);
        if (save_mir) {
            serialise(*exp.m_mir);
        }
        serialise_vec(exp.m_erased_types);
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
    }

    void serialise(const ::AsmCommon::LineFragment& l) {
        serialise(l.before);
        m_out.write_count(l.index);
        m_out.write_i64c(l.modifier);
    }

    void serialise(const ::AsmCommon::Line& l) {
        serialise_vec(l.frags);
        serialise(l.trailing);
    }

    void serialise(const ::AsmCommon::RegisterSpec& r) {
        m_out.write_tag(static_cast<unsigned>(r.tag()));
            TU_MATCH_HDRA( (r), {)
            TU_ARMA(Class, e) {
                m_out.write_tag(static_cast<unsigned>(e));
            }
            TU_ARMA(Explicit, e) {
                m_out.write_string(e);
            }
            }
    }

    void serialise(const ::MIR::AsmParam& p) {
        m_out.write_tag(static_cast<unsigned>(p.tag()));
            TU_MATCH_HDRA( (p), {)
            TU_ARMA(Sym, e) {
                serialise_path(e);
            }
            TU_ARMA(Const, e) {
                serialise(e);
            }
            TU_ARMA(Reg, e) {
                m_out.write_tag(static_cast<unsigned>(e.dir));
                serialise(e.spec);
                m_out.write_bool(bool(e.input));
                if (e.input) {
                    serialise(e.input);
                }
                m_out.write_bool(bool(e.output));
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
        m_out.write_u16(bitflag_1);
    }

    void serialise(const ::MIR::Statement& stmt) {
        auto _ = m_out.open_object("MIR::Statement");
            TU_MATCH_HDRA( (stmt), {)
            TU_ARMA(Assign, e) {
                m_out.write_tag(0);
                serialise(e.dst);
                serialise(e.src);
            }
            TU_ARMA(Drop, e) {
                m_out.write_tag(1);
                assert(e.kind == ::MIR::eDropKind::DEEP || e.kind == ::MIR::eDropKind::SHALLOW);
                m_out.write_bool(e.kind == ::MIR::eDropKind::DEEP);
                serialise(e.slot);
                m_out.write_count(e.flag_idx);
            }
            TU_ARMA(Asm, e) {
                m_out.write_tag(2);
                m_out.write_string(e.tpl);
                serialise_vec(e.outputs);
                serialise_vec(e.inputs);
                serialise_vec(e.clobbers);
                serialise_vec(e.flags);
            }
            TU_ARMA(SetDropFlag, e) {
                m_out.write_tag(3);
                m_out.write_count(e.idx);
                m_out.write_bool(e.new_val);
                m_out.write_count(e.other);
            }
            TU_ARMA(ScopeEnd, e) {
                m_out.write_tag(4);
                serialise_vec(e.slots);
            }
            TU_ARMA(Asm2, e) {
                m_out.write_tag(5);
                serialise(e.options);
                serialise_vec(e.lines);
                serialise_vec(e.params);
            }
            TU_ARMA(SaveDropFlag, e) {
                m_out.write_tag(6);
                serialise(e.slot);
                m_out.write_count(e.bit_index);
                m_out.write_count(e.idx);
            }
            TU_ARMA(LoadDropFlag, e) {
                m_out.write_tag(7);
                m_out.write_count(e.idx);
                serialise(e.slot);
                m_out.write_count(e.bit_index);
            }
            }
    }

    void serialise(const ::MIR::Terminator& term) {
        m_out.write_tag(static_cast<int>(term.tag()));
        TU_MATCHA(
            (term),
            (e),
            (
                Incomplete,
                // NOTE: loops that diverge (don't break) leave a dangling bb
                //assert(!"Entountered Incomplete MIR block");
            ),
            (Return, ),
            (Diverge, ),
            (Goto, m_out.write_count(e);),
            (Panic, m_out.write_count(e.dst);),
            (If, serialise(e.cond); m_out.write_count(e.bb_true); m_out.write_count(e.bb_false);),
            (Switch, serialise(e.val); serialise_vec(e.targets); m_out.write_count(e.valid_flag); m_out.write_count(e.invalid_target);),
            (SwitchValue, serialise(e.val); m_out.write_count(e.def_target); serialise_vec(e.targets); serialise(e.values);),
            (Call, m_out.write_count(e.ret_block); m_out.write_count(e.panic_block); serialise(e.ret_val); serialise(e.fcn); serialise_vec(e.args);)
        )
    }

    void serialise(const ::MIR::SwitchValues& sv) {
        m_out.write_tag(static_cast<int>(sv.tag()));
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
        m_out.write_tag(static_cast<int>(ct.tag()));
        TU_MATCHA((ct), (e), (Value, serialise(e);), (Path, serialise_path(e);), (Intrinsic, m_out.write_string(e.name); serialise_pathparams(e.params);))
    }

    void serialise(const ::MIR::Param& p) {
        TRACE_FUNCTION_F("Param = " << p);
        m_out.write_tag(static_cast<int>(p.tag()));
        TU_MATCHA((p), (e), (LValue, serialise(e);), (Borrow, m_out.write_tag(static_cast<int>(e.type)); serialise(e.val);), (Constant, serialise(e);))
    }

    void serialise(const ::MIR::LValue& lv) {
        TRACE_FUNCTION_F("LValue = " << lv);
        if (lv.m_root.is_Static()) {
            m_out.write_count(3);
            serialise_path(lv.m_root.as_Static());
        } else {
            m_out.write_count(lv.m_root.get_inner());
        }
        serialise_vec(lv.m_wrappers);
    }

    void serialise(const ::MIR::LValue::Wrapper& w) {
        m_out.write_count(w.get_inner());
    }

    void serialise(const ::MIR::RValue& val) {
        TRACE_FUNCTION_F("RValue = " << val);
        m_out.write_tag(val.tag());
        TU_MATCHA((val), (e), (Use, serialise(e);), (Constant, serialise(e);), (SizedArray, serialise(e.val); serialise_arraysize(e.count);), (Borrow, m_out.write_tag(static_cast<int>(e.type)); m_out.write_bool(e.is_raw); serialise(e.val);), (Cast, serialise(e.val); serialise(e.type);), (BinOp, serialise(e.val_l); m_out.write_tag(static_cast<int>(e.op)); serialise(e.val_r);), (UniOp, serialise(e.val); m_out.write_tag(static_cast<int>(e.op));), (DstMeta, serialise(e.val);), (DstPtr, serialise(e.val);), (MakeDst, serialise(e.ptr_val); auto b = !TU_TEST2(e.meta_val, Constant, , ItemAddr, .get() == nullptr); m_out.write_bool(b); if (b) serialise(e.meta_val);), (Tuple, serialise_vec(e.vals);), (Array, serialise_vec(e.vals);), (UnionVariant, serialise_genericpath(e.path); m_out.write_count(e.index); serialise(e.val);), (EnumVariant, serialise_genericpath(e.path); m_out.write_count(e.index); serialise_vec(e.vals);), (Struct, serialise_genericpath(e.path); serialise_vec(e.vals);))
    }

    void serialise(const ::MIR::Constant& v) {
        m_out.write_tag(v.tag());
        TU_MATCHA((v), (e), (Int, m_out.write_u128(e.v.get_inner()); m_out.write_tag(static_cast<unsigned>(e.t));), (Uint, m_out.write_u128(e.v); m_out.write_tag(static_cast<unsigned>(e.t));), (Float, m_out.write_float_value(e.v); m_out.write_tag(static_cast<unsigned>(e.t));), (Bool, m_out.write_bool(e.v);), (Bytes, m_out.write_count(e.size()); m_out.write(e.data(), e.size());), (StaticString, m_out.write_string(e);), (Const, ASSERT_BUG(Span(), monomorphise_path_needed(*e.p), "Unexpected Constant: " << *e.p); serialise_path(*e.p);), (Generic, serialise(e);), (Function, serialise_path(*e.p);), (ItemAddr, serialise_path(*e); m_out.write_u128(e.offset);))
    }

    void serialise(const ::HIR::TypeItem& item) {
        TU_MATCHA((item), (e), (Import, m_out.write_tag(0); serialise_simplepath(e.path); m_out.write_bool(e.is_variant); m_out.write_count(e.idx);), (Module, m_out.write_tag(1); serialise_module(e);), (TypeAlias, m_out.write_tag(2); serialise(e);), (Enum, m_out.write_tag(3); serialise(e);), (Struct, m_out.write_tag(4); serialise(e);), (Trait, m_out.write_tag(5); serialise(e);), (Union, m_out.write_tag(6); serialise(e);), (ExternType, m_out.write_tag(7); serialise(e);), (TraitAlias, m_out.write_tag(8); serialise(e);))
    }

    void serialise(const ::HIR::MacroItem& item) {
        auto _ = m_out.open_object("HIR::MacroItem");
        m_out.write_tag(item.tag());
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
        TU_MATCHA((item), (e), (Import, m_out.write_tag(0); serialise_simplepath(e.path); m_out.write_bool(e.is_variant); m_out.write_count(e.idx);), (Constant, m_out.write_tag(1); serialise(e);), (Static, m_out.write_tag(2); serialise(e);), (StructConstant, m_out.write_tag(3); serialise_simplepath(e.ty);), (Function, m_out.write_tag(4); serialise(e);), (StructConstructor, m_out.write_tag(5); serialise_simplepath(e.ty);))
    }

    void serialise(const ::HIR::Linkage& linkage) {
        //m_out.write_tag( static_cast<int>(linkage.type) );
        m_out.write_string(linkage.name);
    }

    // - Value items
    void serialise(const ::HIR::Function& fcn) {
        TRACE_FUNCTION_F("_function:");
        auto _ = m_out.open_object("HIR::Function");

        serialise(fcn.m_linkage);

        m_out.write_tag(static_cast<int>(fcn.m_receiver));
        serialise(fcn.m_receiver_type.value_or(m_type_interner.infer()));
        m_out.write_string(fcn.m_abi);
        m_out.write_bool(fcn.m_unsafe);
        m_out.write_bool(fcn.m_const);
        serialise(fcn.m_markings);

        serialise_generics(fcn.m_params);
        m_out.write_count(fcn.m_args.size());
        for (const auto& a : fcn.m_args) {
            serialise(a.second);
        }
        DEBUG("m_args = " << fcn.m_args);
        m_out.write_bool(fcn.m_variadic);
        serialise(fcn.m_return);

        serialise(fcn.m_code, fcn.m_save_code || fcn.m_const);
    }

    void serialise(const ::HIR::Function::Markings& m) {
        auto _ = m_out.open_object("HIR::Function::Markings");
        serialise_vec(m.rustc_legacy_const_generics);
        m_out.write_bool(m.track_caller);
    }

    void serialise(const ::HIR::Constant& item) {
        TRACE_FUNCTION_F("_constant:");

        serialise_generics(item.m_params);
        serialise(item.m_type);
        serialise(item.m_value);
        bool write_val = item.m_value_state == ::HIR::Constant::ValueState::Known;
        m_out.write_bool(write_val);
        if (write_val) {
            serialise(item.m_value_res);
        }
    }

    void serialise(const ::HIR::Static& item) {
        TRACE_FUNCTION_F("_static:");

        serialise(item.m_linkage);
        serialise_generics(item.m_params);

        uint8_t bitflag_1 = 0;
#define BIT(i, fld) \
    if (fld)        \
        bitflag_1 |= 1 << (i);
        BIT(0, item.m_is_mut);
        BIT(1, item.m_save_literal)
#undef BIT
        m_out.write_u8(bitflag_1);
        serialise(item.m_type);

        if (item.m_params.is_generic()) {
            serialise(item.m_value);
        }
        // NOTE: Value not stored (What if the static is generic? It can't be.)
        // - Need to store if the item was from a const (special linkage?)
        if (item.m_save_literal) {
            serialise(item.m_value_res);
        }
    }

    // - Type items
    void serialise(const ::HIR::TypeAlias& ta) {
        serialise_generics(ta.m_params);
        serialise_type(ta.m_type);
    }

    void serialise(const ::HIR::TraitAlias& ta) {
        serialise_generics(ta.m_params);
        serialise_vec(ta.m_traits);
    }

    void serialise(const ::HIR::Enum& item) {
        auto _ = m_out.open_object("HIR::Enum");
        serialise_generics(item.m_params);
        m_out.write_bool(item.m_is_c_repr);
        m_out.write_tag(static_cast<int>(item.m_tag_repr));
        serialise(item.m_data);

        serialise(item.m_markings);
    }

    void serialise(const ::HIR::Enum::Class& v) {
        m_out.write_tag(v.tag());
        TU_MATCHA((v), (e), (Value, serialise_vec(e.variants);), (Data, serialise_vec(e);))
    }

    void serialise(const ::HIR::Enum::ValueVariant& v) {
        m_out.write_string(v.name);
        // NOTE: No expr, no longer needed
        m_out.write_u64(v.val.truncate_u64());
    }

    void serialise(const ::HIR::Enum::DataVariant& v) {
        m_out.write_string(v.name);
        m_out.write_bool(v.is_struct);
        serialise(v.type);
        m_out.write_u64(v.discriminant_value.truncate_u64());
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
        m_out.write_u8(bitflag_1);

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
        m_out.write_u8(bitflag_1);

        m_out.write_tag(static_cast<unsigned int>(m.dst_type));
        m_out.write_tag(static_cast<unsigned int>(m.coerce_unsized));
        m_out.write_count(m.coerce_unsized_index);
        m_out.write_count(m.coerce_param);
        m_out.write_count(m.unsized_field);
        m_out.write_count(m.unsized_param);
        if (m.bounded_max) {
            m_out.write_u128(m.bounded_max_value);
        }
        // TODO: auto_impls
    }

    void serialise(const ::HIR::Struct& item) {
        TRACE_FUNCTION_F("Struct");
        auto _ = m_out.open_object("HIR::Struct");

        serialise_generics(item.m_params);
        m_out.write_tag(static_cast<int>(item.m_repr));

        m_out.write_tag(item.m_data.tag());
        TU_MATCHA((item.m_data), (e), (Unit, ), (Tuple, serialise_vec(e);), (Named, serialise_vec(e);))

        m_out.write_count(item.m_forced_alignment);
        m_out.write_count(item.m_max_field_alignment);
        serialise(item.m_markings);
        serialise(item.m_struct_markings);
    }

    void serialise(const ::HIR::StructField& fld) {
        serialise(fld.name);
        serialise(fld.vis);
        serialise(fld.ty);
        m_out.write_bool(fld.default_value != nullptr);
        if (fld.default_value) {
            serialise(*fld.default_value);
        }
    }

    void serialise(const ::HIR::Union& item) {
        TRACE_FUNCTION_F("Union");

        serialise_generics(item.m_params);
        m_out.write_tag(static_cast<int>(item.m_repr));

        serialise_vec(item.m_variants);

        serialise(item.m_markings);
    }

    void serialise(const ::HIR::ExternType& item) {
        TRACE_FUNCTION_F("ExternType");
        serialise(item.m_markings);
    }

    void serialise(const ::HIR::Trait& item) {
        TRACE_FUNCTION_F("_trait:");
        auto _ = m_out.open_object("HIR::Trait");

        serialise_generics(item.m_params);
        serialise(item.m_lifetime);
        // Kept as one byte for compatibility with metadata written before
        // the fundamental bit was represented in HIR.
        m_out.write_u8(
            (item.m_is_marker ? 1u : 0u)
            | (item.m_is_fundamental ? 2u : 0u)
        );
        serialise_strmap(item.m_types);
        serialise_strmap(item.m_values);
        serialise_strmap(item.m_value_indexes);
        serialise_strmap(item.m_type_indexes);
        m_out.write_count(item.m_vtable_parent_traits_start);
        serialise_vec(item.m_all_parent_traits);
        serialise(item.m_vtable_path);
    }

    void serialise(const ::HIR::TraitValueItem& tvi) {
        m_out.write_tag(tvi.tag());
        TU_MATCHA((tvi), (e), (Constant, DEBUG("Constant"); serialise(e);), (Static, DEBUG("Static"); serialise(e);), (Function, DEBUG("Function"); serialise(e);))
    }

    void serialise(const ::HIR::AssociatedType& at) {
        serialise_generics(at.m_generics);
        m_out.write_bool(at.is_sized);
        serialise(at.m_lifetime_bound);
        serialise_vec(at.m_trait_bounds);
        serialise_type(at.m_default);
    }
};

//}

void HIR_Serialise(const ::std::string& filename, const ::HIR::Crate& crate) {
    ::HIR::serialise::Writer out;
    HirSerialiser s{out, crate.m_types};
    s.serialise_crate(crate);
    s.clear();
    out.open(filename);
    s.serialise_crate(crate);
}
