#include "hir_path.h"
#include "hir_type.h"
#include "hir_expr.h"
#include <algorithm>

namespace {
    bool g_compare_hrls = false;
}

namespace HIR {
    TraitPath::TraitPath()
        : m_trait_ptr(nullptr)
    {
    }

    TraitPath::TraitPath(::std::unique_ptr<GenericParams> hrtbs, GenericPath path)
        : m_hrtbs(::std::move(hrtbs))
        , m_path(::std::move(path))
        , m_trait_ptr(nullptr)
    {
    }

    TraitPath::TraitPath(::std::unique_ptr<GenericParams> hrtbs, GenericPath path, assoc_list_t type_bounds, ::std::map<RcString, AtyBound> trait_bounds, const ::HIR::Trait* trait_ptr, BoundConstness constness)
        : m_hrtbs(::std::move(hrtbs))
        , m_path(::std::move(path))
        , m_type_bounds(::std::move(type_bounds))
        , m_trait_bounds(::std::move(trait_bounds))
        , m_constness(constness)
        , m_trait_ptr(trait_ptr)
    {
    }

    TraitPath::~TraitPath() = default;
    TraitPath::TraitPath(TraitPath&&) = default;
    TraitPath& TraitPath::operator=(TraitPath&&) = default;

    ::std::ostream& operator<<(::std::ostream& os, const ::HIR::SimplePath& x) {
        if (x.crate_name() != "") {
            os << "::\"" << x.crate_name() << "\"";
        } else if (x.components().size() == 0) {
            os << "::";
        } else {
        }
        for (const auto& n : x.components()) {
            os << "::" << n;
        }
        return os;
    }

    ::std::ostream& operator<<(::std::ostream& os, const PathParams& x) {
        bool has_args = (x.m_lifetimes.size() > 0 || x.m_types.size() > 0 || x.m_values.size() > 0);

        if (has_args) {
            os << "<";
        }
        for (const auto& lft : x.m_lifetimes) {
            os << lft << ",";
        }
        for (const auto& ty : x.m_types) {
            os << ty << ",";
        }
        for (const auto& v : x.m_values) {
            os << "{" << v << "},";
        }
        if (has_args) {
            os << ">";
        }
        return os;
    }

    ::std::ostream& operator<<(::std::ostream& os, const GenericPath& x) {
        os << x.m_path << x.m_params;
        return os;
    }

    ::std::ostream& operator<<(::std::ostream& os, const TraitPath& x) {
        if (x.m_constness == BoundConstness::Always) {
            os << "const ";
        } else if (x.m_constness == BoundConstness::Maybe) {
            os << "[const] ";
        }
        if (x.m_hrtbs) {
            os << "for" << x.m_hrtbs->fmt_args() << " ";
        }
        os << x.m_path.m_path;
        bool has_args = (x.m_path.m_params.m_lifetimes.size() > 0 || x.m_path.m_params.m_types.size() > 0 || x.m_type_bounds.size() > 0 || x.m_trait_bounds.size() > 0);

        if (has_args) {
            os << "<";
        }
        for (const auto& lft : x.m_path.m_params.m_lifetimes) {
            os << lft << ",";
        }
        for (const auto& ty : x.m_path.m_params.m_types) {
            os << ty << ",";
        }
        for (const auto& v : x.m_path.m_params.m_values) {
            os << v << ",";
        }
        for (const auto& assoc : x.m_type_bounds) {
            os << assoc.first << "{" << assoc.second.source_trait << "}=" << assoc.second << ",";
        }
        for (const auto& assoc : x.m_trait_bounds) {
            for (const auto& trait : assoc.second.traits) {
                os << assoc.first << "{" << assoc.second.source_trait << "}: " << trait << ",";
            }
        }
        if (has_args) {
            os << ">";
        }
        return os;
    }

    ::std::ostream& operator<<(::std::ostream& os, const Path& x) {
        TU_MATCH(::HIR::Path::Data, (x.m_data), (e), (Generic, return os << e;), (UfcsInherent, return os << "<" << e.type << " /*- " << e.impl_params << "*/>::" << e.item << e.params;), (UfcsKnown, os << "<" << e.type << " as "; if (e.hrtbs) { os << "for" << e.hrtbs->fmt_args() << " "; } os << e.trait << ">::" << e.item << e.params; return os;), (UfcsUnknown, return os << "<" << e.type << " as _>::" << e.item << e.params;))
        return os;
    }
}

::HIR::SimplePath HIR::SimplePath::clone() const {
    return SimplePath(m_members);
}

::HIR::SimplePath HIR::SimplePath::parent() const {
    if (m_members.size() > 1) {
        return SimplePath(ThinVector<RcString>(m_members.begin(), m_members.end() - 1));
    } else {
        return this->clone();
    }
}

::HIR::SimplePath HIR::SimplePath::operator+(const RcString& s) const {
    if (m_members.empty()) {
        return ThinVector<RcString>({RcString(), s});
    } else {
        SimplePath rv;
        rv.m_members.reserve(m_members.size());
        for (const auto& v : m_members) {
            rv.m_members.push_back(v);
        }
        rv.m_members.push_back(s);
        return rv;
    }
}

void HIR::SimplePath::operator+=(const RcString& s) {
    if (m_members.empty()) {
        m_members = ThinVector<RcString>({RcString(), s});
    } else {
        m_members.push_back(s);
    }
}

RcString HIR::SimplePath::pop_component() {
    if (m_members.size() <= 1) {
        return RcString();
    } else {
        auto rv = m_members.back();
        m_members.pop_back();
        if (m_members.size() == 1 && m_members[0] == RcString()) {
            m_members = ThinVector<RcString>();
        }
        return rv;
    }
}

void HIR::SimplePath::update_crate_name(RcString v) {
    if (m_members.empty()) {
        m_members.push_back(v);
    } else if (v.c_str()[0] == '\0' && m_members.size() == 1) {
        m_members = ThinVector<RcString>();
    } else {
        m_members[0] = std::move(v);
    }
}

void HIR::SimplePath::update_last_component(RcString v) {
    assert(m_members.size() >= 2);
    m_members.back() = std::move(v);
}

bool HIR::SimplePath::starts_with(const HIR::SimplePath& p, bool skip_last /*=false*/) const {
    if (p.m_members.empty()) {
        return crate_name() == RcString();
    }
    // This path can't start with `p` if it's shorter than `p`
    if (m_members.size() < p.m_members.size() - (skip_last ? 1 : 0)) {
        return false;
    }
    for (size_t i = 0; i < p.m_members.size() - (skip_last ? 1 : 0); i++) {
        if (p.m_members[i] != this->m_members[i]) {
            return false;
        }
    }
    return true;
}

::HIR::PathParams::PathParams() {
}

::HIR::PathParams::PathParams(::HIR::TypeRef ty0) {
    m_types = ThinVector<HIR::TypeRef>(1);
    m_types[0] = std::move(ty0);
}

HIR::PathParams::PathParams(::HIR::LifetimeRef lft) {
    m_lifetimes = ThinVector<HIR::LifetimeRef>(1);
    m_lifetimes[0] = std::move(lft);
}

::HIR::PathParams HIR::PathParams::clone() const {
    PathParams rv;
    rv.m_lifetimes = this->m_lifetimes;
    rv.m_types.reserve(m_types.size());
    for (const auto& t : m_types) {
        rv.m_types.push_back(t);
    }
    rv.m_values.reserve(m_values.size());
    for (const auto& t : m_values) {
        rv.m_values.push_back(t.clone());
    }
    return rv;
}

::HIR::GenericPath::GenericPath() {
}

::HIR::GenericPath::GenericPath(::HIR::SimplePath sp)
    : m_path(mv$(sp))
{
}

::HIR::GenericPath::GenericPath(::HIR::SimplePath sp, ::HIR::PathParams params)
    : m_path(mv$(sp))
    , m_params(mv$(params))
{
}

::HIR::GenericPath::GenericPath(::HIR::GenericParams hrls, ::HIR::SimplePath sp, ::HIR::PathParams params)
    : m_path(mv$(sp))
    , m_params(mv$(params))
{
}

::HIR::GenericPath HIR::GenericPath::clone() const {
    return GenericPath(m_path.clone(), m_params.clone());
}

bool HIR::PathParams::equals_ignoring_regions(const HIR::PathParams& x) const {
    if (m_types.size() != x.m_types.size() || m_values.size() != x.m_values.size()) {
        return false;
    }
    for (size_t i = 0; i < m_types.size(); i++) {
        if (m_types[i] != x.m_types[i] && !m_types[i]->equals_ignoring_regions(x.m_types[i])) {
            return false;
        }
    }
    for (size_t i = 0; i < m_values.size(); i++) {
        if (m_values[i] != x.m_values[i]) {
            return false;
        }
    }
    return true;
}

bool HIR::GenericPath::equals_ignoring_regions(const HIR::GenericPath& x) const {
    return m_path == x.m_path && m_params.equals_ignoring_regions(x.m_params);
}

Ordering HIR::GenericPath::ord(const HIR::GenericPath& x) const {
    ORD(m_path, x.m_path);
    //DEBUG("\n  " << *this << "\n  " << x);
    ORD(m_params, x.m_params);

    return OrdEqual;
}

::HIR::TraitPath HIR::TraitPath::clone() const {
    ::HIR::TraitPath rv{m_hrtbs ? box$(m_hrtbs->clone()) : nullptr, m_path.clone(), {}, {}, m_trait_ptr, m_constness};
    rv.m_lifetime_elision = m_lifetime_elision;

    for (const auto& assoc : m_type_bounds) {
        rv.m_type_bounds.insert(::std::make_pair(assoc.first, assoc.second.clone()));
    }
    for (const auto& assoc : m_trait_bounds) {
        rv.m_trait_bounds.insert(::std::make_pair(assoc.first, assoc.second.clone()));
    }

    return rv;
}

bool HIR::TraitPath::equals_ignoring_regions(const TraitPath& x) const {
    if (!m_path.equals_ignoring_regions(x.m_path)
        || m_type_bounds.size() != x.m_type_bounds.size()
        || m_trait_bounds.size() != x.m_trait_bounds.size()) {
        return false;
    }

    auto lhs_type = m_type_bounds.begin();
    auto rhs_type = x.m_type_bounds.begin();
    for (; lhs_type != m_type_bounds.end(); ++lhs_type, ++rhs_type) {
        const auto& lhs = lhs_type->second;
        const auto& rhs = rhs_type->second;
        if (lhs_type->first != rhs_type->first
            || !lhs.source_trait.equals_ignoring_regions(rhs.source_trait)
            || !lhs.aty_params.equals_ignoring_regions(rhs.aty_params)
            || (lhs.type != rhs.type && !lhs.type->equals_ignoring_regions(rhs.type))) {
            return false;
        }
    }

    auto lhs_bound = m_trait_bounds.begin();
    auto rhs_bound = x.m_trait_bounds.begin();
    for (; lhs_bound != m_trait_bounds.end(); ++lhs_bound, ++rhs_bound) {
        const auto& lhs = lhs_bound->second;
        const auto& rhs = rhs_bound->second;
        if (lhs_bound->first != rhs_bound->first
            || !lhs.source_trait.equals_ignoring_regions(rhs.source_trait)
            || !lhs.aty_params.equals_ignoring_regions(rhs.aty_params)
            || lhs.traits.size() != rhs.traits.size()) {
            return false;
        }
        for (size_t i = 0; i < lhs.traits.size(); i++) {
            if (!lhs.traits[i].equals_ignoring_regions(rhs.traits[i])) {
                return false;
            }
        }
    }
    return true;
}

Ordering HIR::TraitPath::ord(const TraitPath& x) const {
    // NOTE: An empty set is treated as the same as none
    if (g_compare_hrls) {
        ORD(m_hrtbs.get() && !m_hrtbs->is_empty(), x.m_hrtbs.get() && !x.m_hrtbs->is_empty());
        if (m_hrtbs && x.m_hrtbs) {
            ORD(m_hrtbs->m_lifetimes.size(), x.m_hrtbs->m_lifetimes.size());
            ORD(m_hrtbs->m_bounds, x.m_hrtbs->m_bounds);
        }
    }

    ORD(m_path, x.m_path);
    ORD(m_trait_bounds, x.m_trait_bounds);
    ORD(m_type_bounds, x.m_type_bounds);
    return OrdEqual;
}

::HIR::Path::Path(::HIR::GenericPath gp)
    : m_data(::HIR::Path::Data::make_Generic(mv$(gp)))
{
}

::HIR::Path::Path(::HIR::SimplePath sp)
    : m_data(::HIR::Path::Data::make_Generic(::HIR::GenericPath(mv$(sp))))
{
}

::HIR::Path::Path(TypeRef ty, RcString item, PathParams item_params)
    : m_data(Data::make_UfcsInherent({mv$(ty), mv$(item), mv$(item_params)}))
{
}

::HIR::Path::Path(TypeRef ty, GenericPath trait, RcString item, PathParams item_params)
    : m_data(Data::make_UfcsKnown({mv$(ty), mv$(trait), mv$(item), mv$(item_params)}))
{
}

::HIR::Path::Path(TypeRef ty, GenericParams hrtbs, GenericPath trait, RcString item, PathParams item_params)
    : m_data(Data::make_UfcsKnown({mv$(ty), mv$(trait), mv$(item), mv$(item_params), box$(hrtbs)}))
{
}

::HIR::Path HIR::Path::clone() const {
    TU_MATCH_HDRA((m_data), {)
    TU_ARMA(Generic, e) {
            return Path(Data::make_Generic(e.clone()));
        }
        TU_ARMA(UfcsInherent, e) {
            return Path(Data::make_UfcsInherent({e.type, e.item, e.params.clone(), e.impl_params.clone()}));
        }
        TU_ARMA(UfcsKnown, e) {
            return Path(
                Data::make_UfcsKnown({
                    e.type,
                    e.trait.clone(),
                    e.item,
                    e.params.clone(),
                    e.hrtbs ? box$(e.hrtbs->clone()) : nullptr,
                })
            );
        }
        TU_ARMA(UfcsUnknown, e) {
            return Path(Data::make_UfcsUnknown({e.type, e.item, e.params.clone()}));
        }
    }
    throw "";
}

::HIR::Compare HIR::PathParams::compare_with_placeholders(const Span& sp, const ::HIR::PathParams& x, ::HIR::t_cb_resolve_type resolve_placeholder) const {
    using ::HIR::Compare;

    auto rv = Compare::Equal;
    if (this->m_types.size() > 0 || x.m_types.size() > 0) {
        if (this->m_types.size() != x.m_types.size()) {
            return Compare::Unequal;
        }
        for (unsigned int i = 0; i < x.m_types.size(); i++) {
            auto rv2 = this->m_types[i]->compare_with_placeholders(sp, x.m_types[i], resolve_placeholder);
            if (rv2 == Compare::Unequal) {
                return Compare::Unequal;
            }
            if (rv2 == Compare::Fuzzy) {
                rv = Compare::Fuzzy;
            }
        }
    }
#if 1
    if (this->m_values.size() > 0 || x.m_values.size() > 0) {
        if (this->m_values.size() != x.m_values.size()) {
            return Compare::Unequal;
        }
        for (unsigned int i = 0; i < x.m_values.size(); i++) {
            const auto& val_t = resolve_placeholder.get_val(sp, this->m_values[i]);
            const auto& val_x = resolve_placeholder.get_val(sp, x.m_values[i]);
            /*if( const auto* ge = val_t.opt_Generic() ) {
                rv &= match.match_val(*ge, val_x);
                if(rv == Compare::Unequal)
                    return Compare::Unequal;
            }
            else*/
            {
                // TODO: Look up the the ivars?
                if (val_t.is_Infer() || val_x.is_Infer()) {
                    //return Compare::Fuzzy;
                    rv = Compare::Fuzzy;
                } else if (val_t != val_x) {
                    if (val_t.is_Unevaluated() || val_x.is_Unevaluated()) {
                        //return Compare::Fuzzy;
                        rv = Compare::Fuzzy;
                    } else {
                        return Compare::Unequal;
                    }
                }
            }
        }
    }
#endif
    return rv;
}

::HIR::Compare HIR::PathParams::match_test_generics_fuzz(const Span& sp, const PathParams& x, t_cb_resolve_type resolve_placeholder, ::HIR::MatchGenerics& match) const {
    using ::HIR::Compare;
    auto rv = Compare::Equal;
    TRACE_FUNCTION_F("(PathParams) " << *this << " with " << x);

    if (this->m_types.size() != x.m_types.size()) {
        return Compare::Unequal;
    }
    for (unsigned int i = 0; i < x.m_types.size(); i++) {
        rv &= this->m_types[i]->match_test_generics_fuzz(sp, x.m_types[i], resolve_placeholder, match);
        if (rv == Compare::Unequal) {
            return Compare::Unequal;
        }
    }

    if (this->m_values.size() != x.m_values.size()) {
        return Compare::Unequal;
    }
    for (unsigned int i = 0; i < x.m_values.size(); i++) {
        const auto& val_t = resolve_placeholder.get_val(sp, this->m_values[i]);
        const auto& val_x = resolve_placeholder.get_val(sp, x.m_values[i]);
        if (const auto* ge = val_t.opt_Generic()) {
            rv &= match.match_val(*ge, val_x);
            if (rv == Compare::Unequal) {
                return Compare::Unequal;
            }
        } else {
            // TODO: Look up the the ivars?
            if (val_t.is_Infer() || val_x.is_Infer()) {
                return Compare::Fuzzy;
            }

            // An unevaluated value that is a plain integer literal can still be compared
            // exactly; treating it as fuzzy made impl selection on const generics pick the
            // first candidate (harfrust's `SelectAtomic<8/16/32>`).
            struct H2 {
                static bool get_literal(const ::HIR::ConstGeneric& v, U128& out) {
                    if (const auto* ev = v.opt_Evaluated()) {
                        auto sl = EncodedLiteralSlice(**ev);
                        if (sl.m_size == 0 || sl.m_size > 16) {
                            return false;
                        }
                        out = sl.read_uint(sl.m_size);
                        return true;
                    }
                    if (const auto* uev = v.opt_Unevaluated()) {
                        if (!(*uev)->expr || !*(*uev)->expr) {
                            return false;
                        }
                        const auto& node = **(*uev)->expr;
                        if (const auto* lit = cast<const ::HIR::ExprNodeLiteral>(&node)) {
                            if (const auto* i = lit->m_data.opt_Integer()) {
                                out = i->m_value;
                                return true;
                            }
                        }
                        return false;
                    }
                    return false;
                }
            };

            U128 lit_t, lit_x;
            if (H2::get_literal(val_t, lit_t) && H2::get_literal(val_x, lit_x)) {
                if (lit_t != lit_x) {
                    return Compare::Unequal;
                }
                // Equal literals: continue (leaves `rv` as-is)
            } else if (val_t != val_x) {
                if (val_t.is_Unevaluated() || val_x.is_Unevaluated()) {
                    return Compare::Fuzzy;
                }
                return Compare::Unequal;
            }
        }
    }

#if 1
    if (this->m_lifetimes.size() != x.m_lifetimes.size()) {
        //return Compare::Unequal;
    }
    for (unsigned int i = 0; i < std::min(this->m_lifetimes.size(), x.m_lifetimes.size()); i++) {
        if (this->m_lifetimes[i].is_param()) {
            /*rv &=*/match.match_lft(this->m_lifetimes[i].as_param(), x.m_lifetimes[i]);
            //if(rv == Compare::Unequal)
            //    return Compare::Unequal;
        } else {
            //if( this->m_lifetimes[i] != x.m_lifetimes[i] ) {
            //    return Compare::Unequal;
            //}
        }
    }
#endif

    return rv;
}

::HIR::Compare HIR::GenericPath::compare_with_placeholders(const Span& sp, const ::HIR::GenericPath& x, ::HIR::t_cb_resolve_type resolve_placeholder) const {
    if (this->m_path != x.m_path) {
        return ::HIR::Compare::Unequal;
    }

    return this->m_params.compare_with_placeholders(sp, x.m_params, resolve_placeholder);
}

namespace {
    ::HIR::Compare compare_with_placeholders(const Span& sp, const ::HIR::PathParams& l, const ::HIR::PathParams& r, ::HIR::t_cb_resolve_type resolve_placeholder) {
        return l.compare_with_placeholders(sp, r, resolve_placeholder);
    }

    ::HIR::Compare compare_with_placeholders(const Span& sp, const ::HIR::GenericPath& l, const ::HIR::GenericPath& r, ::HIR::t_cb_resolve_type resolve_placeholder) {
        return l.compare_with_placeholders(sp, r, resolve_placeholder);
    }
}

#define CMP(rv, cmp)                            \
    do {                                        \
        switch (cmp) {                          \
            case ::HIR::Compare::Unequal:       \
                return ::HIR::Compare::Unequal; \
            case ::HIR::Compare::Fuzzy:         \
                rv = ::HIR::Compare::Fuzzy;     \
                break;                          \
            case ::HIR::Compare::Equal:         \
                break;                          \
        }                                       \
    } while (0)

::HIR::Compare HIR::TraitPath::compare_with_placeholders(const Span& sp, const TraitPath& x, t_cb_resolve_type resolve_placeholder) const {
    auto rv = m_path.compare_with_placeholders(sp, x.m_path, resolve_placeholder);
    if (rv == Compare::Unequal) {
        return rv;
    }

    // TODO: HRLs

#if 1
    if (g_compare_hrls) {
        if ((this->m_hrtbs && !this->m_hrtbs->is_empty()) != (x.m_hrtbs && !x.m_hrtbs->is_empty())) {
            return Compare::Unequal;
        }
        if (this->m_hrtbs && x.m_hrtbs) {
            if (this->m_hrtbs->m_lifetimes.size() != x.m_hrtbs->m_lifetimes.size()) {
                return Compare::Unequal;
            }
        }
    }
#endif

    auto it_l = m_type_bounds.begin();
    auto it_r = x.m_type_bounds.begin();
    while (it_l != m_type_bounds.end() && it_r != x.m_type_bounds.end()) {
        if (it_l->first != it_r->first) {
            return Compare::Unequal;
        }
        CMP(rv, it_l->second.type->compare_with_placeholders(sp, it_r->second.type, resolve_placeholder));
        ++it_l;
        ++it_r;
    }

    if (it_l != m_type_bounds.end() || it_r != x.m_type_bounds.end()) {
        return Compare::Unequal;
    }

    return rv;
}

::HIR::Compare HIR::Path::compare_with_placeholders(const Span& sp, const Path& x, t_cb_resolve_type resolve_placeholder) const {
    if (this->m_data.tag() != x.m_data.tag()) {
        return Compare::Unequal;
    }
    TU_MATCH_HDRA( (this->m_data, x.m_data), {)
    TU_ARMA(Generic, ple, pre) {
            return ::compare_with_placeholders(sp, ple, pre, resolve_placeholder);
        }
        TU_ARMA(UfcsUnknown, ple, pre) {
            if (ple.item != pre.item) {
                return Compare::Unequal;
            }

            TODO(sp, "Path::compare_with_placeholders - UfcsUnknown");
        }
        TU_ARMA(UfcsInherent, ple, pre) {
            if (ple.item != pre.item) {
                return Compare::Unequal;
            }
            ::HIR::Compare rv = ::HIR::Compare::Equal;
            CMP(rv, ple.type->compare_with_placeholders(sp, pre.type, resolve_placeholder));
            CMP(rv, ::compare_with_placeholders(sp, ple.params, pre.params, resolve_placeholder));
            return rv;
        }
        TU_ARMA(UfcsKnown, ple, pre) {
            if (ple.item != pre.item) {
                return Compare::Unequal;
            }

            ::HIR::Compare rv = ::HIR::Compare::Equal;
            CMP(rv, ple.type->compare_with_placeholders(sp, pre.type, resolve_placeholder));
            CMP(rv, ::compare_with_placeholders(sp, ple.trait, pre.trait, resolve_placeholder));
            CMP(rv, ::compare_with_placeholders(sp, ple.params, pre.params, resolve_placeholder));
            return rv;
        }
    }
    throw "";
}

Ordering HIR::Path::ord(const ::HIR::Path& x) const {
    ORD((unsigned)m_data.tag(), (unsigned)x.m_data.tag());
    TU_MATCH(::HIR::Path::Data, (this->m_data, x.m_data), (tpe, xpe), (Generic, return ::ord(tpe, xpe);), (UfcsInherent, ORD(tpe.type, xpe.type); ORD(tpe.item, xpe.item); return ::ord(tpe.params, xpe.params);), (UfcsKnown, ORD(tpe.type, xpe.type); ORD(tpe.trait, xpe.trait); ORD(tpe.item, xpe.item); return ::ord(tpe.params, xpe.params);), (UfcsUnknown, ORD(tpe.type, xpe.type); ORD(tpe.item, xpe.item); return ::ord(tpe.params, xpe.params);))
    throw "";
}

bool HIR::Path::equals_ignoring_regions(const Path& x) const {
    if (m_data.tag() != x.m_data.tag()) {
        return false;
    }
    TU_MATCH_HDRA((m_data, x.m_data), {)
    TU_ARMA(Generic, lhs, rhs) {
        return lhs.equals_ignoring_regions(rhs);
    }
    TU_ARMA(UfcsInherent, lhs, rhs) {
        return lhs.item == rhs.item
            && (lhs.type == rhs.type || lhs.type->equals_ignoring_regions(rhs.type))
            && lhs.params.equals_ignoring_regions(rhs.params)
            && lhs.impl_params.equals_ignoring_regions(rhs.impl_params);
    }
    TU_ARMA(UfcsKnown, lhs, rhs) {
        return lhs.item == rhs.item
            && (lhs.type == rhs.type || lhs.type->equals_ignoring_regions(rhs.type))
            && lhs.trait.equals_ignoring_regions(rhs.trait)
            && lhs.params.equals_ignoring_regions(rhs.params);
    }
    TU_ARMA(UfcsUnknown, lhs, rhs) {
        return lhs.item == rhs.item
            && (lhs.type == rhs.type || lhs.type->equals_ignoring_regions(rhs.type))
            && lhs.params.equals_ignoring_regions(rhs.params);
    }
    }
    throw "";
}

bool ::HIR::Path::operator==(const Path & x) const {
    return this->ord(x) == ::OrdEqual;
}

namespace HIR {

EncodedLiteralPtr::EncodedLiteralPtr()
    : p(nullptr) {
}
EncodedLiteralPtr::EncodedLiteralPtr(EncodedLiteralPtr&& x)
    : p(x.p) {
    x.p = nullptr;
}
EncodedLiteralPtr& EncodedLiteralPtr::operator=(EncodedLiteralPtr&& x) {
    this->~EncodedLiteralPtr();
    this->p = x.p;
    x.p = nullptr;
    return *this;
}
EncodedLiteral& EncodedLiteralPtr::operator*() {
    assert(p);
    return *p;
}
const EncodedLiteral& EncodedLiteralPtr::operator*() const {
    assert(p);
    return *p;
}
EncodedLiteral* EncodedLiteralPtr::operator->() {
    assert(p);
    return p;
}
const EncodedLiteral* EncodedLiteralPtr::operator->() const {
    assert(p);
    return p;
}
SimplePath::SimplePath(ThinVector<RcString> members)
    : m_members(std::move(members)) {
}
SimplePath::SimplePath() {
}
SimplePath::SimplePath(RcString crate)
    : SimplePath(crate, ::std::span<RcString>()) {
}
SimplePath::SimplePath(RcString crate, ::std::vector<RcString> components)
    : SimplePath(crate, ::std::span<RcString>(components)) {
}
SimplePath::SimplePath(RcString crate, ::std::span<RcString> components) {
    // NOTE: Ensure that it's impossible for the crate name to be empty with only one value in `m_members`, simplifies comparison logic
    if (crate.c_str()[0] != '\0' || !components.empty()) {
        m_members.reserve(1 + components.size());
        m_members.push_back(std::move(crate));
        for (auto& n : components) {
            m_members.push_back(std::move(n));
        }
    }
}
SimplePath::SimplePath(RcString crate, ::std::span<const RcString> components) {
    if (crate.c_str()[0] != '\0' || !components.empty()) {
        m_members.reserve(1 + components.size());
        m_members.push_back(std::move(crate));
        for (const auto& n : components) {
            m_members.push_back(n);
        }
    }
}
SimplePath::SimplePath(RcString crate, ::std::initializer_list<RcString> components)
    : SimplePath(std::move(crate), ::std::span<const RcString>(components.begin(), components.end())) {
}
const RcString& SimplePath::crate_name() const {
    static RcString empty;
    return m_members.empty() ? empty : m_members.front();
}
::std::vector<RcString> SimplePath::components_vec() const {
    const auto values = components();
    return {values.begin(), values.end()};
}
Ordering PathParams::ord(const PathParams& x) const {
    //if(auto cmp = ::ord(m_lifetimes, x.m_lifetimes)) return cmp;
    if (auto cmp = ::ord(m_types, x.m_types)) {
        return cmp;
    }
    if (auto cmp = ::ord(m_values, x.m_values)) {
        return cmp;
    }
    return OrdEqual;
}
Ordering TraitPath::AtyEqual::ord(const AtyEqual& x) const {
    ORD(source_trait, x.source_trait);
    ORD(aty_params, x.aty_params);
    ORD(type, x.type);
    return OrdEqual;
}
Ordering TraitPath::AtyBound::ord(const AtyBound& x) const {
    ORD(source_trait, x.source_trait);
    ORD(aty_params, x.aty_params);
    ORD(traits, x.traits);
    return OrdEqual;
}
TraitPath::AtyBound TraitPath::AtyBound::clone() const {
    std::vector<::HIR::TraitPath> new_traits;
    new_traits.reserve(traits.size());
    for (const auto& t : traits) {
        new_traits.push_back(t.clone());
    }
    return AtyBound{source_trait.clone(), aty_params.clone(), ::std::move(new_traits)};
}
Path::Path(Data data)
    : m_data(mv$(data)) {
}
ConstGeneric_Unevaluated::ConstGeneric_Unevaluated() {
}
}

namespace HIR {

::std::ostream& operator<<(::std::ostream& os, const Compare& x) {
    switch (x) {
        case Compare::Equal:
            os << "Equal";
            break;
        case Compare::Fuzzy:
            os << "Fuzzy";
            break;
        case Compare::Unequal:
            os << "Unequal";
            break;
    }
    return os;
}
Compare& operator&=(Compare& x, const Compare& y) {
    if (x == Compare::Unequal) {
    } else if (y == Compare::Unequal) {
        x = Compare::Unequal;
    } else if (y == Compare::Fuzzy) {
        x = Compare::Fuzzy;
    } else {
        // keep as-is
    }
    return x;
}
::std::ostream& operator<<(::std::ostream& os, const TraitPath::AtyEqual& x) {
    os << x.type;
    return os;
}
}
