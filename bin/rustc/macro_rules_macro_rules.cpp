#include "macro_rules_macro_rules.h"

#include "common.h"
#include "macro_rules_macro_rules.h"
#include "parse_parseerror.h"
#include "parse_ttstream.h"
#include "parse_common.h"
#include <limits.h>
#include "macro_rules_pattern_checks.h"
#include "parse_interpolated_fragment.h"
#include "ast_expr.h"
#include "ast_crate.h"
#include "hir_hir.h" // HIR::Crate

// Map of: LoopIndex=>(Path=>Count)
typedef std::map<unsigned, std::map<std::vector<unsigned>, unsigned>> loop_counts_t;

class ParameterMappings {
    /// A particular captured fragment
    struct CapturedVal {
        unsigned int num_uses; // Number of times this var will be used
        unsigned int num_used; // Number of times it has been used
        InterpolatedFragment frag;
    };

    /// A single layer of the capture set
    TAGGED_UNION(CaptureLayer, Vals, (Vals, ::std::vector<CapturedVal>), (Nested, ::std::vector<CaptureLayer>));

    /// Represents the fragments captured for a name
    struct CapturedVar {
        CaptureLayer top_layer;

        friend ::std::ostream& operator<<(::std::ostream& os, const CapturedVar& x) {
            os << "CapturedVar { " << x.top_layer << " }";
            return os;
        }
    };

    loop_counts_t m_loop_counts;

    ::std::vector<CapturedVar> m_mappings;
    unsigned m_layer_count;

public:
    ParameterMappings()
        : m_layer_count(0)
    {
    }

    ParameterMappings(ParameterMappings&&) = default;

    const ::std::vector<CapturedVar>& mappings() const {
        return m_mappings;
    }

    void dump() const {
        DEBUG("m_mappings = {" << m_mappings << "}");
    }

    size_t layer_count() const {
        return m_layer_count + 1;
    }

    void set_loop_counts(loop_counts_t loop_counts) {
        for (const auto& e : loop_counts) {
            DEBUG(e.first << ": {" << e.second << "}");
        }
        m_loop_counts = std::move(loop_counts);
    }

    void insert(unsigned int name_index, const ::std::vector<unsigned int>& iterations, InterpolatedFragment data);

    /// <summary>
    /// Get the replacement fragment for a given loop iteration (or `nullptr`) if out of bounds
    /// </summary>
    InterpolatedFragment* get(const Span& sp, const ::std::vector<unsigned int>& iterations, unsigned int name_idx);

    /// <summary>
    /// Given a current iteration and a loop index, return how many times this loop will run
    /// </summary>
    unsigned int get_loop_repeats(const Span& sp, const ::std::vector<unsigned int>& iterations, unsigned int loop_idx) const;

    /// <summary>
    /// Return the number of times this level of a given name/variable will loop
    /// </summary>
    unsigned int get_variable_count(const Span& sp, const ::std::vector<unsigned int>& iterations, unsigned int name_idx) const;

    /// Increment the number of times a particular fragment will be used
    void inc_count(const Span& sp, const ::std::vector<unsigned int>& iterations, unsigned int name_idx);
    /// Decrement the number of times a particular fragment is used (returns true if there are still usages remaining)
    bool dec_count(const Span& sp, const ::std::vector<unsigned int>& iterations, unsigned int name_idx);

    friend ::std::ostream& operator<<(::std::ostream& os, const CapturedVal& x) {
        os << x.frag;
        return os;
    }

    friend ::std::ostream& operator<<(::std::ostream& os, const CaptureLayer& x) {
        TU_MATCH(CaptureLayer, (x), (e), (Vals, os << "[" << e << "]";), (Nested, os << "{" << e << "}";))
        return os;
    }

private:
    CapturedVal& get_cap(const Span& sp, const ::std::vector<unsigned int>& iterations, unsigned int name_idx);
};

class MacroPatternStream {
    const ::std::vector<SimplePatEnt>& m_simple_ents;
    size_t m_cur_pos;

    bool m_last_was_cond;
    bool m_condition_met;
    ::std::vector<bool> m_condition_history;

    const ::std::vector<bool>* m_condition_replay;
    size_t m_condition_replay_pos;

    // Currently processed loop indexes
    ::std::vector<unsigned int> m_current_loops;
    // Iteration index of each active loop level
    ::std::vector<unsigned int> m_loop_iterations;

    loop_counts_t m_loop_counts;

    bool m_peek_cache_valid = false;
    const SimplePatEnt* m_peek_cache;

public:
    MacroPatternStream(const ::std::vector<SimplePatEnt>& ents, const ::std::vector<bool>* condition_replay = nullptr)
        : m_simple_ents(ents)
        , m_cur_pos(0)
        , m_last_was_cond(false)
        , m_condition_replay(condition_replay)
        , m_condition_replay_pos(0)
    {
    }

    size_t cur_pos() const {
        return m_cur_pos;
    }

    /// Get the next pattern entry
    const SimplePatEnt& next();

    const SimplePatEnt& peek() {
        if (!m_peek_cache_valid) {
            m_peek_cache = &next();
            m_peek_cache_valid = true;
        }
        return *m_peek_cache;
    }

    /// Inform the stream that the `if` rule that was just returned succeeded
    void if_succeeded();

    /// Get the current loop iteration count
    const ::std::vector<unsigned int>& get_loop_iters() const {
        return m_loop_iterations;
    }

    ::std::vector<bool> take_history() {
        return ::std::move(m_condition_history);
    }

    loop_counts_t take_loop_counts() {
        return ::std::move(m_loop_counts);
    }
};

// === Prototypes ===
unsigned int Macro_InvokeRules_MatchPattern(const Span& sp, const MacroRules& rules, TokenTree input, const AST::Crate& crate, AST::Module& mod, ParameterMappings& bound_tts);
void Macro_InvokeRules_CountSubstUses(ParameterMappings& bound_tts, const ::std::vector<MacroExpansionEnt>& contents);

// ------------------------------------
// ParameterMappings
// ------------------------------------

void ParameterMappings::insert(unsigned int name_index, const ::std::vector<unsigned int>& iterations, InterpolatedFragment data) {
    DEBUG("index=" << name_index << ", iterations=[" << iterations << "], data=" << data);
    if (name_index >= m_mappings.size()) {
        m_mappings.resize(name_index + 1);
    }
    auto* layer = &m_mappings[name_index].top_layer;
    if (iterations.size() > 0) {
        for (unsigned int i = 0; i < iterations.size() - 1; i++) {
            auto iter = iterations[i];

            if (layer->is_Vals()) {
                assert(layer->as_Vals().size() == 0);
                *layer = CaptureLayer::make_Nested({});
            }
            auto& e = layer->as_Nested();
            while (e.size() < iter) {
                DEBUG("- Skipped iteration " << e.size());
                e.push_back(CaptureLayer::make_Nested({}));
            }

            if (e.size() == iter) {
                e.push_back(CaptureLayer::make_Vals({}));
            } else {
                if (e.size() > iter) {
                    DEBUG("ERROR: Iterations ran backwards? - " << e.size() << " > " << iter);
                }
            }
            layer = &e[iter];
        }
        ASSERT_BUG(Span(), layer->as_Vals().size() == iterations.back(), "Capture count mismatch with iteration index - iterations=[" << iterations << "]");
    } else {
        assert(layer->as_Vals().size() == 0);
    }
    layer->as_Vals().push_back(CapturedVal{0, 0, mv$(data)});
}

ParameterMappings::CapturedVal& ParameterMappings::get_cap(const Span& sp, const ::std::vector<unsigned int>& iterations, unsigned int name_idx) {
    DEBUG("(iterations=[" << iterations << "], name_idx=" << name_idx << ")");
    auto& e = m_mappings.at(name_idx);
    //DEBUG("- e = " << e);
    auto* layer = &e.top_layer;

    // - If the top layer is a 1-sized set of values, unconditionally return it
    if (auto* e = layer->opt_Vals()) {
        if (e->size() == 1) {
            return (*e)[0];
        }
        if (e->size() == 0) {
            BUG(sp, "Attempting to get binding for empty capture - #" << name_idx);
        }
    }

    for (const auto iter : iterations) {
        TU_MATCH_HDRA( (*layer), { )
        TU_ARMA(Vals, e) {
                ASSERT_BUG(sp, iter < e.size(), "Iteration index " << iter << " outside of range " << e.size() << " (values)");
                return e.at(iter);
            }
            TU_ARMA(Nested, e) {
                ASSERT_BUG(sp, iter < e.size(), "Iteration index " << iter << " outside of range " << e.size() << " (nest)");
                layer = &e.at(iter);
            }
        }
    }

    ERROR(sp, E0000, "Variable #" << name_idx << " is still repeating at this level (" << iterations.size() << ")");
}

InterpolatedFragment* ParameterMappings::get(const Span& sp, const ::std::vector<unsigned int>& iterations, unsigned int name_idx) {
    return &get_cap(sp, iterations, name_idx).frag;
}

unsigned int ParameterMappings::get_loop_repeats(const Span& sp, const ::std::vector<unsigned int>& iterations, unsigned int loop_idx) const {
    const auto& list = m_loop_counts.at(loop_idx);
    // Iterate the list, find the first prefix match of `iterations`
    // - `iterations` should always be longer or equal in length to every entry in `list`
    //auto ranges = list.equal_range(iterations);
    for (const auto& e : list) {
        ASSERT_BUG(Span(), e.first.size() <= iterations.size(), "Loop " << loop_idx << " iteration path [" << e.first << "] larger than query path [" << iterations << "]");
        if (std::equal(e.first.begin(), e.first.end(), iterations.begin())) {
            return e.second;
        }
    }
    BUG(sp, "Loop " << loop_idx << " cannot find an iteration count for path [" << iterations << "]");
}

unsigned int ParameterMappings::get_variable_count(const Span& sp, const ::std::vector<unsigned int>& iterations, unsigned int name_idx) const {
    DEBUG("(iterations=[" << iterations << "], name_idx=" << name_idx << ")");
    auto& e = m_mappings.at(name_idx);
    //DEBUG("- e = " << e);
    auto* layer = &e.top_layer;

    // - If the top layer is a 1-sized set of values, unconditionally return it
    if (auto* e = layer->opt_Vals()) {
        if (e->size() == 1) {
            return 1;
        }
        if (e->size() == 0) {
            BUG(sp, "Attempting to get binding for empty capture - #" << name_idx);
        }
    }

    for (const auto iter : iterations) {
        TU_MATCH_HDRA( (*layer), { )
        TU_ARMA(Vals, e) {
                ASSERT_BUG(sp, iter < e.size(), "Iteration index " << iter << " outside of range " << e.size() << " (values)");
                return 1;
            }
            TU_ARMA(Nested, e) {
                ASSERT_BUG(sp, iter < e.size(), "Iteration index " << iter << " outside of range " << e.size() << " (nest)");
                layer = &e.at(iter);
            }
        }
    }
    TU_MATCH_HDRA( (*layer), { )
    TU_ARMA(Vals, e) {
            return e.size();
        }
        TU_ARMA(Nested, e) {
            return e.size();
        }
    }
    throw "";
}

void ParameterMappings::inc_count(const Span& sp, const ::std::vector<unsigned int>& iterations, unsigned int name_idx) {
    auto& cap = get_cap(sp, iterations, name_idx);
    assert(cap.num_used == 0);
    cap.num_uses += 1;
}

bool ParameterMappings::dec_count(const Span& sp, const ::std::vector<unsigned int>& iterations, unsigned int name_idx) {
    auto& cap = get_cap(sp, iterations, name_idx);
    assert(cap.num_used < cap.num_uses);
    cap.num_used += 1;
    return (cap.num_used < cap.num_uses);
}

// ------------------------------------
// MacroPatternStream
// ------------------------------------

const SimplePatEnt& MacroPatternStream::next() {
    if (m_peek_cache_valid) {
        m_peek_cache_valid = false;
        return *m_peek_cache;
    }

    for (;;) {
        // If not replaying, and the previous entry was a conditional, record the result of that conditional
        if (!m_condition_replay && m_last_was_cond) {
            m_condition_history.push_back(m_condition_met);
        }
        m_last_was_cond = false;
        // End of list? return End entry
        if (m_cur_pos == m_simple_ents.size()) {
            static SimplePatEnt END = SimplePatEnt::make_End({});
            return END;
        }
        const auto& cur_ent = m_simple_ents[m_cur_pos];
        // If replaying, and this is a conditional
        if (m_condition_replay && cur_ent.is_If()) {
            // Skip the conditional (following its target or just skipping over)
            if ((*m_condition_replay)[m_condition_replay_pos++]) {
                m_cur_pos = cur_ent.as_If().jump_target;
            } else {
                m_cur_pos += 1;
            }
            continue;
        }
        m_cur_pos += 1;
        TU_MATCH_HDRA( (cur_ent), {)
        default:
            if( cur_ent.is_If() )
            {
                m_last_was_cond = true;
                m_condition_met = false;
            }
            return cur_ent;
            TU_ARMA(End, _e)
            BUG(Span(), "Unexpected End");
            TU_ARMA(Jump, e)
            m_cur_pos = e.jump_target;
            TU_ARMA(LoopStart, e) {
                m_current_loops.push_back(e.index);
                m_loop_iterations.push_back(0);
            }
            TU_ARMA(LoopNext, _e) {
                m_loop_iterations.back() += 1;
            }
            TU_ARMA(LoopEnd, _e) {
                assert(!m_loop_iterations.empty());
                assert(!m_current_loops.empty());
                auto loop_index = m_current_loops.back();
                auto num_iter = m_loop_iterations.back();
                m_loop_iterations.pop_back();
                m_current_loops.pop_back();

                // Save this iteration count if replaying
                if (m_condition_replay) {
                    m_loop_counts[loop_index].insert(std::make_pair(m_loop_iterations, num_iter));
                }
            }
        }
    }
}

void MacroPatternStream::if_succeeded() {
    assert(m_cur_pos > 0);
    assert(m_cur_pos <= m_simple_ents.size());
    assert(m_last_was_cond);
    const auto& ent = m_simple_ents[m_cur_pos - 1];
    ASSERT_BUG(Span(), ent.is_If(), "Expected If when calling `if_succeeded`, got " << ent);
    const auto& e = ent.as_If();
    ASSERT_BUG(Span(), e.jump_target < m_simple_ents.size(), "Jump target " << e.jump_target << " out of range " << m_simple_ents.size());
    m_cur_pos = e.jump_target;
    m_condition_met = true;
}

// ----------------------------------------------------------------
/// State for MacroExpander and Macro_InvokeRules_CountSubstUses
class MacroExpandState {
    const ::std::vector<MacroExpansionEnt>& m_root_contents;
    const ParameterMappings& m_mappings;

    struct t_offset {
        unsigned read_pos;
        unsigned loop_index;
        unsigned max_index;
    };

    /// Layer states : Index and Iteration
    ::std::vector<t_offset> m_offsets;
    ::std::vector<unsigned int> m_iterations;

    /// Cached pointer to the current layer
    const ::std::vector<MacroExpansionEnt>* m_cur_ents; // For faster lookup.

public:
    MacroExpandState(const ::std::vector<MacroExpansionEnt>& contents, const ParameterMappings& mappings)
        : m_root_contents(contents)
        , m_mappings(mappings)
        , m_offsets({{0, 0, 0}})
        , m_cur_ents(&m_root_contents)
    {
    }

    // Returns a pointer to the next entry to expand, or nullptr if the end is reached
    // - NOTE: When a Loop entry is returned, the separator token should be emitted
    const MacroExpansionEnt* next_ent();

    const ::std::vector<unsigned int> iterations() const {
        return m_iterations;
    }

    unsigned int top_pos() const {
        if (m_offsets.empty()) {
            return 0;
        }
        return m_offsets[0].read_pos;
    }

private:
    const MacroExpansionEnt& getCurLayerEnt() const;
    const ::std::vector<MacroExpansionEnt>* getCurLayer() const;
};

// ----------------------------------------------------------------
class MacroExpander: public TokenStream {
    // Used to track a specific invocation for debugging
    static unsigned s_next_log_index;
    unsigned m_log_index;

    Span m_this_span;
    const RcString m_crate_name;
    Span m_invocation_span;
    AST::Edition m_invocation_edition;

    ParameterMappings m_mappings;
    MacroExpandState m_state;

    Token m_next_token; // used for inserting a single token into the stream
    ::std::unique_ptr<TTStreamO> m_ttstream;
    AST::Edition m_source_edition;
    bool m_is_macro_item;
    Ident::Hygiene m_hygiene;

public:
    MacroExpander(const MacroExpander& x) = delete;

    MacroExpander(const RcString& macro_name, const Span& sp, AST::Edition edition, bool is_macro_item, const Ident::Hygiene& parent_hygiene, const ::std::vector<MacroExpansionEnt>& contents, ParameterMappings mappings, RcString crate_name, AST::Edition source_edition)
        : TokenStream(ParseState())
        , m_log_index(s_next_log_index++)
        , m_this_span(sp, crate_name, macro_name)
        , m_crate_name(mv$(crate_name))
        , m_invocation_span(sp)
        , m_invocation_edition(edition)
        , m_mappings(mv$(mappings))
        , m_state(contents, m_mappings)
        , m_source_edition(source_edition)
        , m_is_macro_item(is_macro_item)
        , m_hygiene(Ident::Hygiene::new_scope_chained(parent_hygiene))
    {
    }

    Position getPosition() const override;

    Span outerSpan() const override {
        return m_invocation_span;
    }

    Ident::Hygiene realGetHygiene() const override;
    AST::Edition realGetEdition() const override;
    Token realGetToken() override;
};

unsigned MacroExpander::s_next_log_index = 0;

void Macro_InitDefaults() {
}

InterpolatedFragment Macro_HandlePatternCap(TokenStream& lex, MacroPatEnt::Type type, bool stmt_is_item) {
    Token tok;
    switch (type) {
        case MacroPatEnt::PAT_TOKEN:
            BUG(lex.point_span(), "Encountered PAT_TOKEN when handling capture");
        case MacroPatEnt::PAT_LOOP:
            BUG(lex.point_span(), "Encountered PAT_LOOP when handling capture");

        case MacroPatEnt::PAT_TT:
            if (GET_TOK(tok, lex) == TOK_EOF) {
                throw ParseError::Unexpected(lex, TOK_EOF);
            } else {
                PUTBACK(tok, lex);
            }
            return InterpolatedFragment(Parse_TT(lex, false));
        case MacroPatEnt::PAT_PAT:
            // TODO: Is this edition check correct? Or should it be uncondiitonally "Yes"?
            //return InterpolatedFragment( Parse_Pattern(lex, lex.edition_after(AST::Edition::Rust2021) ? AllowOrPattern::Yes : AllowOrPattern::No) );
            return InterpolatedFragment(Parse_Pattern(lex, AllowOrPattern::Yes));
        case MacroPatEnt::PAT_TYPE:
            return InterpolatedFragment(Parse_Type(lex));
        case MacroPatEnt::PAT_EXPR:
            return InterpolatedFragment(InterpolatedFragment::EXPR, Parse_Expr0(lex).release());
        case MacroPatEnt::PAT_STMT:
            if (stmt_is_item) {
                if (lex.lookahead(0) == TOK_INTERPOLATED_STMT_ITEM) {
                    tok = lex.getToken();
                    return InterpolatedFragment(InterpolatedFragment::STMT_ITEM, tok.take_frag_stmt_item());
                }
                assert(lex.parse_state().module);
                const auto& cur_mod = *lex.parse_state().module;
                return InterpolatedFragment(InterpolatedFragment::STMT_ITEM, Parse_Mod_Item_S(lex, cur_mod.m_file_info, cur_mod.path(), AST::AttributeList{}));
            }
            return InterpolatedFragment(InterpolatedFragment::STMT, Parse_Stmt(lex).release());
        case MacroPatEnt::PAT_PATH:
            // HACK for `rustc-1.90.0-src/vendor/icu_locid_transform_data-1.5.0/data/macros.rs::23`
            if (lex.lookahead(0) == TOK_INTERPOLATED_TYPE) {
                return InterpolatedFragment(std::move(lex.getToken().frag_type()));
            }
            return InterpolatedFragment(Parse_Path(lex, PATH_GENERIC_TYPE)); // non-expr mode
        case MacroPatEnt::PAT_BLOCK:
            return InterpolatedFragment(InterpolatedFragment::BLOCK, Parse_ExprBlockNode(lex).release());
        case MacroPatEnt::PAT_META:
            return InterpolatedFragment(Parse_MetaItem(lex));
        case MacroPatEnt::PAT_ITEM: {
            assert(lex.parse_state().module);
            const auto& cur_mod = *lex.parse_state().module;
            return InterpolatedFragment(Parse_Mod_Item_S(lex, cur_mod.m_file_info, cur_mod.path(), AST::AttributeList{}));
        } break;
        case MacroPatEnt::PAT_IDENT:
            // NOTE: Any reserved word is also valid as an ident
            GET_TOK(tok, lex);
            if (Token::type_is_rword(tok.type())) {
                return InterpolatedFragment(TokenTree(lex.get_edition(), lex.get_hygiene(), tok));
            } else {
                CHECK_TOK(tok, TOK_IDENT);
                return InterpolatedFragment(TokenTree(lex.get_edition(), lex.get_hygiene(), tok));
            }
        case MacroPatEnt::PAT_VIS:
            return InterpolatedFragment(Parse_Publicity(lex, /*allow_restricted=*/true));
        case MacroPatEnt::PAT_LIFETIME:
            GET_CHECK_TOK(tok, lex, TOK_LIFETIME);
            return InterpolatedFragment(TokenTree(lex.get_edition(), lex.get_hygiene(), tok));
        case MacroPatEnt::PAT_LITERAL:
            GET_TOK(tok, lex);
            if (tok.type() == TOK_DASH) {
                std::vector<TokenTree> toks;
                switch (lex.lookahead(0)) {
                    case TOK_INTEGER:
                    case TOK_FLOAT:
                        toks.push_back(tok);
                        break;
                    default:
                        throw ParseError::Unexpected(lex, tok, {TOK_INTEGER, TOK_FLOAT});
                }
                GET_TOK(tok, lex);
                toks.push_back(tok);
                return InterpolatedFragment(TokenTree(lex.get_edition(), lex.get_hygiene(), std::move(toks)));
            }
            switch (tok.type()) {
                case TOK_INTEGER:
                case TOK_FLOAT:
                case TOK_STRING:
                case TOK_BYTESTRING:
                case TOK_RWORD_TRUE:
                case TOK_RWORD_FALSE:
                    break;
                default:
                    throw ParseError::Unexpected(lex, tok, {TOK_INTEGER, TOK_FLOAT, TOK_STRING, TOK_BYTESTRING, TOK_RWORD_TRUE, TOK_RWORD_FALSE});
            }
            return InterpolatedFragment(TokenTree(lex.get_edition(), lex.get_hygiene(), tok));
    }
    throw "";
}

/// Parse the input TokenTree according to the `macro_rules!` patterns and return a token stream of the replacement
::std::unique_ptr<TokenStream> Macro_InvokeRules(const RcString& name, const MacroRules& rules, const Span& sp, TokenTree input, const AST::Crate& crate, AST::Module& mod) {
    TRACE_FUNCTION_F("'" << name << "', " << input);
    DEBUG("rules.m_source_crate = " << rules.m_source_crate);
    DEBUG("rules.m_hygiene = " << rules.m_hygiene);

    ParameterMappings bound_tts;
    unsigned int rule_index = Macro_InvokeRules_MatchPattern(sp, rules, mv$(input), crate, mod, bound_tts);

    const auto& rule = rules.m_rules.at(rule_index);

    DEBUG("Using macro '" << name << "' #" << rule_index << " - " << rule.m_contents.size() << " rule contents with " << bound_tts.mappings().size() << " bound values");
    for (unsigned int i = 0; i < ::std::min(bound_tts.mappings().size(), rule.m_param_names.size()); i++) {
        DEBUG("- #" << i << " " << rule.m_param_names.at(i) << " = [" << bound_tts.mappings()[i] << "]");
    }
    //bound_tts.dump();

    // Run through the expansion counting the number of times each fragment is used
    Macro_InvokeRules_CountSubstUses(bound_tts, rule.m_contents);

    TokenStream* ret_ptr = new MacroExpander(name, sp, crate.m_edition, rules.m_is_macro_item, rules.m_hygiene, rule.m_contents, mv$(bound_tts), rules.m_source_crate == "" ? crate.m_crate_name_real : rules.m_source_crate, rules.m_edition);

    return ::std::unique_ptr<TokenStream>(ret_ptr);
}

// Collection of functions that consume a specific fragment type from a token stream
// - Does very loose consuming
namespace {
    // Class that provides read-only iteration over a TokenTree
    class TokenStreamRO {
        const TokenTree& m_tt;
        ::std::vector<size_t> m_offsets;
        size_t m_active_offset;

        Token m_faked_next;
        size_t m_consume_count;

    public:
        TokenStreamRO(const TokenTree& tt)
            : m_tt(tt)
            , m_active_offset(0)
            , m_consume_count(0)
        {
            assert(!m_tt.is_token());
            if (m_tt.size() == 0) {
                m_active_offset = 0;
                DEBUG("TOK_EOF");
            } else {
                const auto* cur_tree = &m_tt;
                while (!cur_tree->is_token()) {
                    cur_tree = &(*cur_tree)[0];
                    m_offsets.push_back(0);
                }
                assert(m_offsets.size() > 0);
                m_offsets.pop_back();
                m_active_offset = 0;
                DEBUG(next_tok());
            }
        }

        TokenStreamRO clone() const {
            return TokenStreamRO(*this);
        }

        enum eTokenType next() const {
            return next_tok().type();
        }

        const Token& next_tok() const {
            static Token eof_token = TOK_EOF;

            if (m_faked_next.type() != TOK_NULL) {
                return m_faked_next;
            }

            if (m_offsets.empty() && m_active_offset == m_tt.size()) {
                //DEBUG(m_consume_count << " " << eof_token << "(EOF)");
                return eof_token;
            } else {
                const auto* cur_tree = &m_tt;
                for (auto idx : m_offsets) {
                    cur_tree = &(*cur_tree)[idx];
                }
                const auto& rv = (*cur_tree)[m_active_offset].tok();
                //DEBUG(m_consume_count << " " << rv);
                return rv;
            }
        }

        void consume() {
            if (m_faked_next.type() != TOK_NULL) {
                m_faked_next = Token(TOK_NULL);
                return;
            }

            if (m_offsets.empty() && m_active_offset == m_tt.size()) {
                throw ::std::runtime_error("Attempting to consume EOS");
            }
            DEBUG(m_consume_count << " " << next_tok());
            m_consume_count++;
            for (;;) {
                const auto* cur_tree = &m_tt;
                for (auto idx : m_offsets) {
                    cur_tree = &(*cur_tree)[idx];
                }

                m_active_offset++;
                // If reached the end of a tree...
                if (m_active_offset == cur_tree->size()) {
                    // If the end of the root is reached, return (leaving the state indicating EOS)
                    if (m_offsets.empty()) {
                        return;
                    }
                    // Pop and continue
                    m_active_offset = m_offsets.back();
                    m_offsets.pop_back();
                } else {
                    // Dig into nested trees
                    while (!(*cur_tree)[m_active_offset].is_token()) {
                        cur_tree = &(*cur_tree)[m_active_offset];
                        m_offsets.push_back(m_active_offset);
                        m_active_offset = 0;
                    }
                    DEBUG("-> " << next_tok());
                    return;
                }
            }
        }

        void consume_and_push(eTokenType ty) {
            consume();
            m_faked_next = Token(ty);
        }

        // Consumes if the current token is `ty`, otherwise doesn't and returns false
        bool consume_if(eTokenType ty) {
            if (next() == ty) {
                consume();
                return true;
            } else {
                return false;
            }
        }

        /// Returns the position in the stream (number of tokens that have been consumed)
        size_t position() const {
            return m_consume_count;
        }
    };

    bool consume_type(TokenStreamRO& lex);
    enum class ItemConsumeMode {
        ItemFragment,
        StatementFragment,
    };
    bool consume_item(TokenStreamRO& lex, ItemConsumeMode mode = ItemConsumeMode::ItemFragment);

    // Consume an entire TT
    bool consume_tt(TokenStreamRO& lex) {
        TRACE_FUNCTION;
        switch (lex.next()) {
            case TOK_EOF:
            case TOK_PAREN_CLOSE:
            case TOK_BRACE_CLOSE:
            case TOK_SQUARE_CLOSE:
                return false;
            case TOK_PAREN_OPEN:
                lex.consume();
                while (lex.next() != TOK_PAREN_CLOSE) {
                    consume_tt(lex);
                }
                lex.consume();
                break;
            case TOK_SQUARE_OPEN:
                lex.consume();
                while (lex.next() != TOK_SQUARE_CLOSE) {
                    consume_tt(lex);
                }
                lex.consume();
                break;
            case TOK_BRACE_OPEN:
                lex.consume();
                while (lex.next() != TOK_BRACE_CLOSE) {
                    consume_tt(lex);
                }
                lex.consume();
                break;
            default:
                lex.consume();
                break;
        }
        return true;
    }

    bool consume_tt_angle(TokenStreamRO& lex) {
        TRACE_FUNCTION;
        unsigned int level = (lex.next() == TOK_DOUBLE_LT ? 2 : 1);
        // Seek until enouh matching '>'s are seen
        // TODO: Can expressions show up on this context?
        lex.consume();
        for (;;) {
            if (lex.next() == TOK_LT || lex.next() == TOK_DOUBLE_LT) {
                level += (lex.next() == TOK_DOUBLE_LT ? 2 : 1);
            } else if (lex.next() == TOK_GT || lex.next() == TOK_DOUBLE_GT) {
                assert(level > 0);
                if (lex.next() == TOK_DOUBLE_GT) {
                    if (level == 1) {
                        lex.consume_and_push(TOK_GT);
                        return true;
                    }
                    level -= 2;
                } else {
                    level -= 1;
                }
                if (level == 0) {
                    break;
                }
            } else if (lex.next() == TOK_EOF) {
                return false;
            } else {
            }

            // Consume TTs separately
            if (lex.next() == TOK_PAREN_OPEN) {
                consume_tt(lex);
            } else {
                lex.consume();
            }
        }
        // Consume closing token
        lex.consume();
        return true;
    }

    // Consume a path
    bool consume_path(TokenStreamRO& lex, bool type_mode = false) {
        TRACE_FUNCTION;
        switch (lex.next()) {
            case TOK_INTERPOLATED_PATH:
            case TOK_INTERPOLATED_TYPE: // HACK!
                lex.consume();
                return true;
            case TOK_RWORD_SELF:
                lex.consume();
                // Allow a lone `self` (it's referring to the current object)
                if (lex.next() != TOK_DOUBLE_COLON) {
                    return true;
                }
                break;
            case TOK_RWORD_CRATE:
                lex.consume();
                // Require `::` after `crate`
                break;
            case TOK_RWORD_SUPER:
                lex.consume();
                if (lex.next() != TOK_DOUBLE_COLON) {
                    return false;
                }
                break;
            case TOK_DOUBLE_COLON:
                break;
            case TOK_IDENT:
                lex.consume();
                if (type_mode && (lex.next() == TOK_LT || lex.next() == TOK_DOUBLE_LT || lex.next() == TOK_PAREN_OPEN))
                    ;
                // Allow a lone ident
                else if (lex.next() != TOK_DOUBLE_COLON) {
                    return true;
                } else
                    ;
                break;
            case TOK_LT:
            case TOK_DOUBLE_LT:
                if (!consume_tt_angle(lex)) {
                    return false;
                }
                if (lex.next() != TOK_DOUBLE_COLON) {
                    return false;
                }
                break;
            default:
                return false;
        }

        if (type_mode && (lex.next() == TOK_LT || lex.next() == TOK_DOUBLE_LT)) {
            if (!consume_tt_angle(lex)) {
                return false;
            }
        }

        while (lex.next() == TOK_DOUBLE_COLON) {
            lex.consume();
            if (lex.next() == TOK_STRING) {
                lex.consume();
            } else if (!type_mode && (lex.next() == TOK_LT || lex.next() == TOK_DOUBLE_LT)) {
                if (!consume_tt_angle(lex)) {
                    return false;
                }
            } else if (lex.next() == TOK_IDENT) {
                lex.consume();
                if (type_mode && (lex.next() == TOK_LT || lex.next() == TOK_DOUBLE_LT)) {
                    if (!consume_tt_angle(lex)) {
                        return false;
                    }
                }
            } else {
                return false;
            }
        }
        // Handles `Fn()`
        if (type_mode && lex.next() == TOK_PAREN_OPEN) {
            if (!consume_tt(lex)) {
                return false;
            }
            if (lex.consume_if(TOK_THINARROW)) {
                if (!consume_type(lex)) {
                    return false;
                }
            }
        }
        return true;
    }

    bool consume_type_TraitList(TokenStreamRO& lex) {
        do {
            if (lex.consume_if(TOK_LIFETIME)) {
                continue;
            }
            if (!consume_path(lex, true)) {
                return false;
            }
        } while (lex.consume_if(TOK_PLUS));
        return true;
    }

    bool consume_type(TokenStreamRO& lex) {
        TRACE_FUNCTION;
        switch (lex.next()) {
            case TOK_UNDERSCORE:
                lex.consume();
                return true;
            case TOK_INTERPOLATED_TYPE:
                lex.consume();
                return true;
            case TOK_PAREN_OPEN:
            case TOK_SQUARE_OPEN:
                return consume_tt(lex);
            case TOK_RWORD_IMPL:
            case TOK_RWORD_DYN:
                lex.consume();
                return consume_type_TraitList(lex);
            case TOK_IDENT:
                if (TARGETVER_LEAST_1_29 && lex.next_tok().ident().name == "dyn") {
                    lex.consume();
                    return consume_type_TraitList(lex);
                }
            case TOK_RWORD_CRATE:
            case TOK_RWORD_SUPER:
            case TOK_RWORD_SELF:
            case TOK_DOUBLE_COLON:
            case TOK_INTERPOLATED_PATH:
            case TOK_LT:
            case TOK_DOUBLE_LT:
                if (!consume_path(lex, true)) {
                    return false;
                }
                // Macro invocation?
                if (lex.consume_if(TOK_EXCLAM)) {
                    if (lex.next() != TOK_PAREN_OPEN && lex.next() != TOK_SQUARE_OPEN && lex.next() != TOK_BRACE_OPEN) {
                        return false;
                    }
                    if (!consume_tt(lex)) {
                        return false;
                    }
                }
                return true;
            case TOK_AMP:
            case TOK_DOUBLE_AMP:
                lex.consume();
                lex.consume_if(TOK_LIFETIME);
                lex.consume_if(TOK_RWORD_MUT);
                return consume_type(lex);
            case TOK_STAR:
                lex.consume();
                if (lex.consume_if(TOK_RWORD_MUT))
                    ;
                else if (lex.consume_if(TOK_RWORD_CONST))
                    ;
                else {
                    return false;
                }
                return consume_type(lex);
            case TOK_EXCLAM:
                lex.consume();
                return true;

            case TOK_RWORD_UNSAFE:
                lex.consume();
                if (lex.next() == TOK_RWORD_EXTERN) {
                    case TOK_RWORD_EXTERN:
                        lex.consume();
                        lex.consume_if(TOK_STRING);
                }
                if (lex.next() != TOK_RWORD_FN) {
                    return false;
                }
            case TOK_RWORD_FN:
                lex.consume();
                if (lex.next() != TOK_PAREN_OPEN) {
                    return false;
                }
                if (!consume_tt(lex)) {
                    return false;
                }
                if (lex.consume_if(TOK_THINARROW)) {
                    consume_type(lex);
                }
                return true;
            default:
                return false;
        }
    }

    bool consume_pat(TokenStreamRO& lex, bool allow_or = true) {
        TRACE_FUNCTION;

        if (lex.next() == TOK_RWORD_REF || lex.next() == TOK_RWORD_MUT) {
            lex.consume_if(TOK_RWORD_REF);
            lex.consume_if(TOK_RWORD_MUT);
            if (!lex.consume_if(TOK_IDENT)) {
                return false;
            }
            if (!lex.consume_if(TOK_AT)) {
                return true;
            }
        }

        if (lex.consume_if(TOK_INTERPOLATED_PATTERN)) {
            return true;
        }

        if (allow_or) {
            lex.consume_if(TOK_PIPE);
        }
        for (;;) {
            switch (lex.next()) {
                case TOK_UNDERSCORE:
                    lex.consume();
                    if (allow_or && lex.consume_if(TOK_PIPE)) {
                        continue;
                    }
                    return true;
                case TOK_IDENT:
                case TOK_RWORD_SUPER:
                case TOK_RWORD_SELF:
                case TOK_RWORD_CRATE:
                case TOK_DOUBLE_COLON:
                case TOK_INTERPOLATED_PATH:
                    consume_path(lex);
                    if (lex.next() == TOK_BRACE_OPEN) {
                        if (!consume_tt(lex)) {
                            return false;
                        }
                    } else if (lex.next() == TOK_PAREN_OPEN) {
                        if (!consume_tt(lex)) {
                            return false;
                        }
                    } else if (lex.next() == TOK_EXCLAM) {
                        lex.consume();
                        if (!consume_tt(lex)) {
                            return false;
                        }
                    } else {
                        // Fall through to the range handling
                        break;
                    }
                    if (allow_or && lex.consume_if(TOK_PIPE)) {
                        continue;
                    }
                    return true;
                case TOK_RWORD_BOX:
                    lex.consume();
                    if (!consume_pat(lex, allow_or)) {
                        return false;
                    }
                    if (allow_or && lex.consume_if(TOK_PIPE)) {
                        continue;
                    }
                    return true;
                case TOK_AMP:
                case TOK_DOUBLE_AMP:
                    lex.consume();
                    lex.consume_if(TOK_RWORD_MUT);
                    if (!consume_pat(lex, allow_or)) {
                        return false;
                    }
                    if (allow_or && lex.consume_if(TOK_PIPE)) {
                        continue;
                    }
                    return true;
                case TOK_PAREN_OPEN:
                case TOK_SQUARE_OPEN:
                    if (!consume_tt(lex)) {
                        return false;
                    }
                    if (allow_or && lex.consume_if(TOK_PIPE)) {
                        continue;
                    }
                    return true;
                case TOK_BYTESTRING:
                case TOK_STRING:
                case TOK_INTEGER:
                case TOK_FLOAT:
                    lex.consume();
                    break;
                default:
                    return false;
            }
            if (lex.consume_if(TOK_AT)) {
                continue;
            }
            // ... or ..=
            if (lex.consume_if(TOK_TRIPLE_DOT) || lex.consume_if(TOK_DOUBLE_DOT_EQUAL)) {
                switch (lex.next()) {
                    case TOK_IDENT:
                    case TOK_RWORD_SUPER:
                    case TOK_RWORD_SELF:
                    case TOK_DOUBLE_COLON:
                    case TOK_INTERPOLATED_PATH:
                        consume_path(lex);
                        break;
                    case TOK_STRING:
                    case TOK_INTEGER:
                    case TOK_FLOAT:
                        lex.consume();
                        break;
                    default:
                        return false;
                }
            }
            if (allow_or && lex.consume_if(TOK_PIPE)) {
                continue;
            }
            return true;
        }
    }

    // Consume an expression
    bool consume_expr(TokenStreamRO& lex, bool no_struct_lit = false) {
        TRACE_FUNCTION;
        bool cont;

        while (lex.next() == TOK_HASH) {
            lex.consume();
            lex.consume_if(TOK_EXCLAM);
            consume_tt(lex);
        }

        // Closures
        if (lex.next() == TOK_RWORD_MOVE || lex.next() == TOK_PIPE || lex.next() == TOK_DOUBLE_PIPE) {
            lex.consume_if(TOK_RWORD_MOVE);
            if (lex.consume_if(TOK_PIPE)) {
                do {
                    if (lex.next() == TOK_PIPE) {
                        break;
                    }
                    consume_pat(lex, /*allow_or=*/false);
                    if (lex.consume_if(TOK_COLON)) {
                        consume_type(lex);
                    }
                } while (lex.consume_if(TOK_COMMA));
                if (!lex.consume_if(TOK_PIPE)) {
                    return false;
                }
            } else {
                lex.consume();
            }
            if (lex.consume_if(TOK_THINARROW)) {
                if (!consume_type(lex)) {
                    return false;
                }
            }
            return consume_expr(lex);
        }

        do {
            bool inner_cont;
            do {
                inner_cont = true;
                switch (lex.next()) {
                    case TOK_STAR:      // Deref
                    case TOK_DASH:      // Negate
                    case TOK_EXCLAM:    // Invert
                    case TOK_RWORD_BOX: // Box
                        lex.consume();
                        break;
                    case TOK_DOUBLE_AMP:
                    case TOK_AMP:
                        lex.consume();
                        lex.consume_if(TOK_RWORD_MUT);
                        break;
                    default:
                        inner_cont = false;
                        break;
                }
            } while (inner_cont);

            // :: -> path
            // ident -> path
            // '<' -> path
            // '(' -> tt
            // '[' -> tt
            switch (lex.next()) {
                case TOK_RWORD_CONTINUE:
                case TOK_RWORD_BREAK:
                    lex.consume();
                    lex.consume_if(TOK_LIFETIME);
                    if (0) {
                        case TOK_RWORD_RETURN:
                            lex.consume();
                    }
                    switch (lex.next()) {
                        case TOK_EOF:
                        case TOK_SEMICOLON:
                        case TOK_COMMA:
                        case TOK_PAREN_CLOSE:
                        case TOK_BRACE_CLOSE:
                        case TOK_SQUARE_CLOSE:
                            break;
                        default:
                            if (!consume_expr(lex)) {
                                return false;
                            }
                            break;
                    }
                    break;
                case TOK_IDENT:
                case TOK_INTERPOLATED_PATH:
                case TOK_DOUBLE_COLON:
                case TOK_RWORD_SELF:
                case TOK_RWORD_SUPER:
                case TOK_RWORD_CRATE:
                case TOK_LT:
                case TOK_DOUBLE_LT:
                    if (!consume_path(lex)) {
                        return false;
                    }
                    if (lex.next() == TOK_BRACE_OPEN && !no_struct_lit) {
                        consume_tt(lex);
                    } else if (lex.consume_if(TOK_EXCLAM)) {
                        if (lex.consume_if(TOK_IDENT)) {
                            // yay?
                        }
                        consume_tt(lex);
                    }
                    break;

                case TOK_INTERPOLATED_EXPR:
                case TOK_INTERPOLATED_BLOCK:
                    lex.consume();
                    break;
                case TOK_INTEGER:
                case TOK_FLOAT:
                case TOK_STRING:
                case TOK_BYTESTRING:
                case TOK_RWORD_TRUE:
                case TOK_RWORD_FALSE:
                    lex.consume();
                    break;

                // Possibly a left-open (or full-open) range literal
                case TOK_DOUBLE_DOT:
                case TOK_DOUBLE_DOT_EQUAL:
                case TOK_TRIPLE_DOT:
                    break;

                case TOK_RWORD_UNSAFE:
                    lex.consume();
                    if (lex.next() != TOK_BRACE_OPEN) {
                        return false;
                    }
                case TOK_PAREN_OPEN:
                case TOK_SQUARE_OPEN:
                case TOK_BRACE_OPEN:
                    consume_tt(lex);
                    break;

                // TODO: Do these count for "expr"?
                case TOK_RWORD_FOR:
                    lex.consume();
                    if (!consume_pat(lex)) {
                        return false;
                    }
                    if (!lex.consume_if(TOK_RWORD_IN)) {
                        return false;
                    }
                    if (!consume_expr(lex, true)) {
                        return false;
                    }
                    if (lex.next() != TOK_BRACE_OPEN) {
                        return false;
                    }
                    if (!consume_tt(lex)) {
                        return false;
                    }
                    break;
                case TOK_RWORD_MATCH:
                    lex.consume();
                    // TODO: Parse _without_ consuming a struct literal
                    if (!consume_expr(lex, true)) {
                        return false;
                    }
                    if (lex.next() != TOK_BRACE_OPEN) {
                        return false;
                    }
                    if (!consume_tt(lex)) {
                        return false;
                    }
                    break;
                case TOK_RWORD_WHILE:
                    lex.consume();
                    if (!consume_expr(lex, true)) {
                        return false;
                    }
                    if (lex.next() != TOK_BRACE_OPEN) {
                        return false;
                    }
                    if (!consume_tt(lex)) {
                        return false;
                    }
                    break;
                case TOK_RWORD_LOOP:
                    lex.consume();
                    if (lex.next() != TOK_BRACE_OPEN) {
                        return false;
                    }
                    consume_tt(lex);
                    break;
                case TOK_RWORD_IF:
                    while (1) {
                        assert(lex.next() == TOK_RWORD_IF);
                        lex.consume();
                        if (lex.next() == TOK_RWORD_LET) {
                            lex.consume();
                            if (!consume_pat(lex)) {
                                return false;
                            }
                            if (lex.next() != TOK_EQUAL) {
                                return false;
                            }
                            lex.consume();
                        }
                        if (!consume_expr(lex, true)) {
                            return false;
                        }
                        if (lex.next() != TOK_BRACE_OPEN) {
                            return false;
                        }
                        consume_tt(lex);
                        if (lex.next() != TOK_RWORD_ELSE) {
                            break;
                        }
                        lex.consume();

                        if (lex.next() != TOK_RWORD_IF) {
                            if (lex.next() != TOK_BRACE_OPEN) {
                                return false;
                            }
                            consume_tt(lex);
                            break;
                        }
                    }
                    break;
                default:
                    return false;
            }

            do {
                inner_cont = true;
                // '.' ident/int
                switch (lex.next()) {
                    case TOK_QMARK:
                        lex.consume();
                        break;
                    case TOK_DOT:
                        lex.consume();
                        if (lex.consume_if(TOK_IDENT)) {
                            if (lex.consume_if(TOK_DOUBLE_COLON)) {
                                if (!(lex.next() == TOK_LT || lex.next() == TOK_DOUBLE_LT)) {
                                    return false;
                                }
                                if (!consume_tt_angle(lex)) {
                                    return false;
                                }
                            }
                        } else if (lex.consume_if(TOK_INTEGER))
                            ;
                        else {
                            return false;
                        }
                        break;
                    // '[' -> tt
                    case TOK_SQUARE_OPEN:
                    // '(' -> tt
                    case TOK_PAREN_OPEN:
                        consume_tt(lex);
                        break;
                    default:
                        inner_cont = false;
                        break;
                }
            } while (inner_cont);

            if (lex.consume_if(TOK_COLON)) {
                consume_type(lex);
            }

            while (lex.consume_if(TOK_RWORD_AS)) {
                consume_type(lex);
            }

            cont = true;
            switch (lex.next()) {
                case TOK_PLUS:
                case TOK_DASH:
                case TOK_SLASH:
                case TOK_STAR:
                case TOK_PERCENT:
                case TOK_DOUBLE_LT:
                case TOK_DOUBLE_GT:
                case TOK_PIPE:
                case TOK_AMP:
                case TOK_CARET:
                case TOK_LT:
                case TOK_GT:
                case TOK_LTE:
                case TOK_GTE:
                case TOK_DOUBLE_EQUAL:
                case TOK_EXCLAM_EQUAL:
                case TOK_DOUBLE_AMP:
                case TOK_DOUBLE_PIPE:
                case TOK_DOUBLE_DOT_EQUAL:
                case TOK_TRIPLE_DOT:
                    lex.consume();
                    break;
                case TOK_DOUBLE_DOT:
                    lex.consume();
                    DEBUG("TOK_DOUBLE_DOT => " << lex.next());
                    switch (lex.next()) {
                        case TOK_EOF:
                            return true;
                        case TOK_COMMA:
                        case TOK_SEMICOLON:
                        case TOK_BRACE_CLOSE:
                        case TOK_PAREN_CLOSE:
                        case TOK_SQUARE_CLOSE:
                            cont = false;
                            break;
                        default:
                            break;
                    }
                    break;
                case TOK_EQUAL:
                case TOK_PLUS_EQUAL:
                case TOK_DASH_EQUAL:
                case TOK_SLASH_EQUAL:
                case TOK_STAR_EQUAL:
                case TOK_PERCENT_EQUAL:
                case TOK_AMP_EQUAL:
                case TOK_PIPE_EQUAL:
                    lex.consume();
                    break;
                default:
                    cont = false;
                    break;
            }
        } while (cont);
        return true;
    }

    bool consume_stmt(TokenStreamRO& lex, bool* out_is_item = nullptr) {
        TRACE_FUNCTION;
        if (out_is_item) {
            *out_is_item = false;
        }
        if (lex.consume_if(TOK_INTERPOLATED_STMT)) {
            return true;
        }
        if (lex.consume_if(TOK_INTERPOLATED_STMT_ITEM)) {
            if (out_is_item) {
                *out_is_item = true;
            }
            return true;
        }

        // A statement fragment includes item declarations.  Try the item
        // grammar on a checkpoint first; only advance the real stream when
        // the complete item matched, so expression statements retain their
        // normal interpretation.
        auto item_lex = lex.clone();
        if (consume_item(item_lex, ItemConsumeMode::StatementFragment)) {
            while (lex.position() < item_lex.position()) {
                lex.consume();
            }
            if (out_is_item) {
                *out_is_item = true;
            }
            return true;
        }

        if (lex.consume_if(TOK_RWORD_LET)) {
            if (!consume_pat(lex)) {
                return false;
            }
            if (lex.consume_if(TOK_COLON)) {
                if (!consume_type(lex)) {
                    return false;
                }
            }
            if (lex.consume_if(TOK_EQUAL)) {
                if (!consume_expr(lex)) {
                    return false;
                }
            }
            return true;
        } else {
            if (!consume_expr(lex)) {
                return false;
            }
            return true;
        }
    }

    bool consume_vis(TokenStreamRO& lex) {
        TRACE_FUNCTION;
        if (lex.consume_if(TOK_INTERPOLATED_VIS) || lex.consume_if(TOK_RWORD_CRATE)) {
            return true;
        } else if (lex.consume_if(TOK_RWORD_PUB)) {
            if (lex.next() == TOK_PAREN_OPEN) {
                return consume_tt(lex);
            }
            return true;
        } else {
            // HACK: If the next character is nothing interesting, then force no match?
            // - TODO: Instead, have `:vis` force a deepeer check
            if (lex.next() == TOK_EOF || lex.next() == TOK_PAREN_CLOSE || lex.next() == TOK_BRACE_CLOSE || lex.next() == TOK_SQUARE_CLOSE) {
                return false;
            }
            // NOTE: This is kinda true?
            return true;
        }
    }

    bool consume_item(TokenStreamRO& lex, ItemConsumeMode mode) {
        TRACE_FUNCTION;

        struct H {
            static bool maybe_generics(TokenStreamRO& lex) {
                if (lex.next() == TOK_LT) {
                    if (!consume_tt_angle(lex)) {
                        return false;
                    }
                }
                return true;
            }

            static bool maybe_where(TokenStreamRO& lex) {
                if (lex.consume_if(TOK_RWORD_WHERE)) {
                    do {
                        if (lex.next() == TOK_BRACE_OPEN || lex.next() == TOK_SEMICOLON) {
                            break;
                        }
                        if (lex.consume_if(TOK_LIFETIME)) {
                            if (!lex.consume_if(TOK_COLON)) {
                                return false;
                            }

                            if (!lex.consume_if(TOK_LIFETIME)) {
                                return false;
                            }
                        } else {
                            if (!consume_type(lex)) {
                                return false;
                            }
                            if (!lex.consume_if(TOK_COLON)) {
                                return false;
                            }
                            do {
                                if (lex.consume_if(TOK_LIFETIME)) {
                                } else {
                                    lex.consume_if(TOK_QMARK);
                                    if (!consume_path(lex, true)) {
                                        return false;
                                    }
                                }
                            } while (lex.consume_if(TOK_PLUS));
                        }
                    } while (lex.consume_if(TOK_COMMA));
                }
                return true;
            }
        };

        while (lex.next() == TOK_HASH) {
            lex.consume();
            lex.consume_if(TOK_EXCLAM);
            consume_tt(lex);
        }
        // Interpolated items
        if (lex.next() == TOK_INTERPOLATED_ITEM) {
            if (mode != ItemConsumeMode::ItemFragment) {
                return false;
            }
            lex.consume();
            return true;
        }
        // Macro invocation
        // TODO: What about `union!` as a macro? Needs to be handled below
        if (mode == ItemConsumeMode::ItemFragment && ((lex.next() == TOK_IDENT && lex.next_tok().ident().name != "union") || lex.next() == TOK_RWORD_SELF || lex.next() == TOK_RWORD_SUPER || lex.next() == TOK_DOUBLE_COLON)) {
            if (!consume_path(lex)) {
                return false;
            }
            if (!lex.consume_if(TOK_EXCLAM)) {
                return false;
            }
            lex.consume_if(TOK_IDENT);
            bool need_semicolon = (lex.next() != TOK_BRACE_OPEN);
            consume_tt(lex);
            if (need_semicolon) {
                if (!lex.consume_if(TOK_SEMICOLON)) {
                    return false;
                }
            }
            return true;
        }
        // Normal items
        if (!consume_vis(lex)) {
            return false;
        }
        if (lex.next() == TOK_RWORD_UNSAFE) {
            lex.consume();
        }
        DEBUG("Check item: " << lex.next_tok());
        switch (lex.next()) {
            case TOK_RWORD_USE:
                // Lazy mode
                while (lex.next() != TOK_SEMICOLON) {
                    lex.consume();
                }
                lex.consume();
                break;
            case TOK_RWORD_MOD:
                lex.consume();
                if (!lex.consume_if(TOK_IDENT)) {
                    return false;
                }
                if (lex.consume_if(TOK_SEMICOLON))
                    ;
                else if (lex.next() == TOK_BRACE_OPEN) {
                    if (!consume_tt(lex)) {
                        return false;
                    }
                } else {
                    return false;
                }
                break;
            // impl [Foo for] Bar { ... }
            case TOK_RWORD_IMPL:
                lex.consume();
                if (!H::maybe_generics(lex)) {
                    return false;
                }
                if (!consume_type(lex)) {
                    return false;
                }
                if (lex.consume_if(TOK_RWORD_FOR)) {
                    if (!consume_type(lex)) {
                        return false;
                    }
                }
                if (!H::maybe_where(lex)) {
                    return false;
                }
                if (lex.next() != TOK_BRACE_OPEN) {
                    return false;
                }
                return consume_tt(lex);
            // type Foo
            case TOK_RWORD_TYPE:
                lex.consume();
                if (!lex.consume_if(TOK_IDENT)) {
                    return false;
                }
                if (!H::maybe_generics(lex)) {
                    return false;
                }

                if (!lex.consume_if(TOK_EQUAL)) {
                    return false;
                }
                if (!consume_type(lex)) {
                    return false;
                }
                if (!lex.consume_if(TOK_SEMICOLON)) {
                    return false;
                }

                break;
            // static FOO
            case TOK_RWORD_STATIC:
                lex.consume();
                if (!lex.consume_if(TOK_IDENT)) {
                    return false;
                }
                if (!lex.consume_if(TOK_COLON)) {
                    return false;
                }
                if (!consume_type(lex)) {
                    return false;
                }
                if (!lex.consume_if(TOK_EQUAL)) {
                    return false;
                }
                if (!consume_expr(lex)) {
                    return false;
                }
                if (!lex.consume_if(TOK_SEMICOLON)) {
                    return false;
                }
                break;
            case TOK_RWORD_STRUCT:
                lex.consume();
                if (!lex.consume_if(TOK_IDENT)) {
                    return false;
                }
                if (!H::maybe_generics(lex)) {
                    return false;
                }
                if (!H::maybe_where(lex)) {
                    return false;
                }
                if (lex.consume_if(TOK_SEMICOLON))
                    ;
                else if (lex.next() == TOK_PAREN_OPEN) {
                    if (!consume_tt(lex)) {
                        return false;
                    }
                    if (!lex.consume_if(TOK_SEMICOLON)) {
                        return false;
                    }
                } else if (lex.next() == TOK_BRACE_OPEN) {
                    if (!consume_tt(lex)) {
                        return false;
                    }
                } else {
                    return false;
                }
                break;
            case TOK_RWORD_ENUM:
                lex.consume();
                if (!lex.consume_if(TOK_IDENT)) {
                    return false;
                }
                if (!H::maybe_generics(lex)) {
                    return false;
                }
                if (!H::maybe_where(lex)) {
                    return false;
                }
                if (lex.next() != TOK_BRACE_OPEN) {
                    return false;
                }
                return consume_tt(lex);
            case TOK_IDENT:
                if (lex.next_tok().ident().name == "union") {
                    lex.consume();
                    if (lex.next() == TOK_EXCLAM) {
                        if (mode != ItemConsumeMode::ItemFragment) {
                            return false;
                        }
                        bool need_semicolon = (lex.next() != TOK_BRACE_OPEN);
                        consume_tt(lex);
                        if (need_semicolon) {
                            if (!lex.consume_if(TOK_SEMICOLON)) {
                                return false;
                            }
                        }
                        return true;
                    } else {
                        if (!lex.consume_if(TOK_IDENT)) {
                            return false;
                        }
                        if (!H::maybe_generics(lex)) {
                            return false;
                        }
                        if (!H::maybe_where(lex)) {
                            return false;
                        }
                        if (lex.next() != TOK_BRACE_OPEN) {
                            return false;
                        }
                        return consume_tt(lex);
                    }
                } else if (lex.next_tok().ident().name == "auto") {
                    lex.consume();
                    if (lex.consume_if(TOK_RWORD_TRAIT)) {
                        goto trait;
                    } else {
                        return false;
                    }
                } else {
                    return false;
                }
                break;
            // const [unsafe] [extern] fn
            // const FOO
            case TOK_RWORD_CONST:
                lex.consume();
                if (lex.next() == TOK_RWORD_UNSAFE) {
                    lex.consume();
                }
                if (lex.next() == TOK_RWORD_EXTERN) {
                    lex.consume();
                }
                if (lex.consume_if(TOK_RWORD_FN)) {
                    goto fn;
                } else {
                    if (!lex.consume_if(TOK_IDENT)) {
                        return false;
                    }
                    if (!lex.consume_if(TOK_COLON)) {
                        return false;
                    }
                    consume_type(lex);
                    if (!lex.consume_if(TOK_EQUAL)) {
                        return false;
                    }
                    consume_expr(lex);
                    if (!lex.consume_if(TOK_SEMICOLON)) {
                        return false;
                    }
                }
                break;
            case TOK_RWORD_TRAIT:
                lex.consume();
            trait:
                if (!lex.consume_if(TOK_IDENT)) {
                    return false;
                }

                if (!H::maybe_generics(lex)) {
                    return false;
                }
                if (lex.consume_if(TOK_COLON)) {
                    do {
                        if (lex.consume_if(TOK_LIFETIME)) {
                        } else {
                            if (!consume_path(lex, true)) {
                                return false;
                            }
                        }
                    } while (lex.consume_if(TOK_PLUS));
                }
                if (lex.next() != TOK_BRACE_OPEN) {
                    return false;
                }
                if (!consume_tt(lex)) {
                    return false;
                }
                break;
            case TOK_RWORD_EXTERN:
                lex.consume();
                if (lex.consume_if(TOK_RWORD_CRATE)) {
                    if (!lex.consume_if(TOK_IDENT)) {
                        return false;
                    }
                    if (lex.consume_if(TOK_RWORD_AS)) {
                        if (!lex.consume_if(TOK_IDENT)) {
                            return false;
                        }
                    }
                    if (!lex.consume_if(TOK_SEMICOLON)) {
                        return false;
                    }
                    break;
                }

                lex.consume_if(TOK_STRING);
                if (lex.next() == TOK_BRACE_OPEN) {
                    return consume_tt(lex);
                }
                if (!lex.consume_if(TOK_RWORD_FN)) {
                    return false;
                }
                goto fn;
            case TOK_RWORD_FN:
                lex.consume();
            fn:
                if (!lex.consume_if(TOK_IDENT)) {
                    return false;
                }

                if (!H::maybe_generics(lex)) {
                    return false;
                }
                if (lex.next() != TOK_PAREN_OPEN) {
                    return false;
                }
                if (!consume_tt(lex)) {
                    return false;
                }

                if (lex.consume_if(TOK_THINARROW)) {
                    if (!consume_type(lex)) {
                        return false;
                    }
                }

                if (!H::maybe_where(lex)) {
                    return false;
                }

                if (lex.consume_if(TOK_SEMICOLON)) {
                    // TODO: Is this actually valid?
                    break;
                } else if (lex.next() == TOK_BRACE_OPEN) {
                    if (!consume_tt(lex)) {
                        return false;
                    }
                } else {
                    return false;
                }
                break;
            default:
                return false;
        }
        return true;
    }

    bool consume_from_frag(TokenStreamRO& lex, MacroPatEnt::Type type, bool* out_stmt_is_item = nullptr) {
        TRACE_FUNCTION_F(type);
        switch (type) {
            case MacroPatEnt::PAT_TOKEN:
            case MacroPatEnt::PAT_LOOP:
                BUG(Span(), "Encountered " << type << " in consume_from_frag");
                ;
            case MacroPatEnt::PAT_BLOCK:
                if (lex.next() == TOK_BRACE_OPEN) {
                    return consume_tt(lex);
                } else if (lex.next() == TOK_INTERPOLATED_BLOCK) {
                    lex.consume();
                } else {
                    return false;
                }
                break;
            case MacroPatEnt::PAT_IDENT:
                if (lex.next() == TOK_IDENT || Token::type_is_rword(lex.next())) {
                    lex.consume();
                } else {
                    return false;
                }
                break;
            case MacroPatEnt::PAT_TT:
                return consume_tt(lex);
            case MacroPatEnt::PAT_PATH:
                return consume_path(lex, true);
            case MacroPatEnt::PAT_TYPE:
                return consume_type(lex);
            case MacroPatEnt::PAT_EXPR:
                return consume_expr(lex);
            case MacroPatEnt::PAT_STMT:
                return consume_stmt(lex, out_stmt_is_item);
            case MacroPatEnt::PAT_PAT:
                //return consume_pat(lex, lex.edition_after(AST::Edition::Rust2021));
                return consume_pat(lex, true);
            case MacroPatEnt::PAT_META:
                if (lex.next() == TOK_INTERPOLATED_META) {
                    lex.consume();
                } else if (lex.next() == TOK_IDENT) {
                    lex.consume();
                    switch (lex.next()) {
                        case TOK_PAREN_OPEN:
                            return consume_tt(lex);
                        case TOK_EQUAL:
                            lex.consume();
                            return consume_expr(lex);
                        default:
                            break;
                    }
                } else {
                    return false;
                }
                break;
            case MacroPatEnt::PAT_ITEM:
                return consume_item(lex);
            case MacroPatEnt::PAT_VIS:
                return consume_vis(lex);
            case MacroPatEnt::PAT_LIFETIME:
                return lex.consume_if(TOK_LIFETIME);
            case MacroPatEnt::PAT_LITERAL:
                switch (lex.next()) {
                    case TOK_DASH: {
                        auto tmp = lex.clone();
                        tmp.consume();
                        switch (tmp.next()) {
                            case TOK_INTEGER:
                            case TOK_FLOAT:
                                lex.consume();
                                lex.consume();
                                return true;
                            default:
                                return false;
                        }
                    } break;
                    case TOK_INTEGER:
                    case TOK_FLOAT:
                    case TOK_STRING:
                    case TOK_BYTESTRING:
                    case TOK_RWORD_TRUE:
                    case TOK_RWORD_FALSE:
                        lex.consume();
                        return true;
                    default:
                        return false;
                }
        }
        return true;
    }
}

unsigned int Macro_InvokeRules_MatchPattern(const Span& sp, const MacroRules& rules, TokenTree input, const AST::Crate& crate, AST::Module& mod, ParameterMappings& bound_tts) {
    TRACE_FUNCTION_F(rules.m_rules.size() << " options");
    ASSERT_BUG(sp, rules.m_rules.size() > 0, "Empty macro_rules set");

    struct Match {
        size_t arm_index;
        ::std::vector<bool> condition_history;
        ::std::vector<bool> stmt_is_item_history;
    };
    ::std::vector<Match> matches;
    ::std::vector<std::pair<size_t, eTokenType>> fail_pos;
    for (size_t i = 0; i < rules.m_rules.size(); i++) {
        auto lex = TokenStreamRO(input);
        auto arm_stream = MacroPatternStream(rules.m_rules[i].m_pattern);
        ::std::vector<bool> stmt_is_item_history;

        bool fail = false;
        for (;;) {
            const auto pos = arm_stream.cur_pos();
            const auto& pat = arm_stream.next();
            // NOTE: The positions seen by this aren't fully sequential, as `next` steps over jumps/loop control ops
            DEBUG("Arm " << i << " @" << pos << " " << pat);
            if (pat.is_End()) {
                if (lex.next() != TOK_EOF) {
                    DEBUG("Expeced EOF, got " << lex.next_tok());
                    fail = true;
                }
                break;
            } else if (const auto* e = pat.opt_If()) {
                auto lc = lex.clone();
                bool rv = true;
                for (const auto& check : e->ents) {
                    if (check.ty != MacroPatEnt::PAT_TOKEN) {
                        if (!consume_from_frag(lc, check.ty)) {
                            rv = false;
                            break;
                        }
                    } else {
                        if (lc.next_tok() != check.tok) {
                            rv = false;
                            break;
                        }
                        if (lc.next_tok() != TOK_EOF) {
                            lc.consume();
                        }
                    }
                }
                if (rv == e->is_equal) {
                    DEBUG("- Succeeded");
                    arm_stream.if_succeeded();
                }
            } else if (const auto* e = pat.opt_ExpectTok()) {
                const auto& tok = lex.next_tok();
                DEBUG("Arm " << i << " @" << pos << " ExpectTok(" << *e << ") == " << tok);
                if (tok != *e) {
                    fail = true;
                    break;
                }
                lex.consume();
            } else if (const auto* e = pat.opt_ExpectPat()) {
                DEBUG("Arm " << i << " @" << pos << " ExpectPat(" << e->type << " => $" << e->idx << ")");
                bool stmt_is_item = false;
                if (!consume_from_frag(lex, e->type, &stmt_is_item)) {
                    fail = true;
                    break;
                }
                if (e->type == MacroPatEnt::PAT_STMT) {
                    stmt_is_item_history.push_back(stmt_is_item);
                }
            } else {
                // Unreachable.
            }
        }

        if (!fail) {
            matches.push_back(Match{i, arm_stream.take_history(), mv$(stmt_is_item_history)});
            DEBUG(i << " MATCHED");
        } else {
            DEBUG(i << " FAILED");
            fail_pos.push_back(std::make_pair(lex.position(), lex.next()));
        }
    }

    if (matches.size() == 0) {
        // ERROR!
        // TODO: Keep track of where each arm failed.
        TODO(sp, "No arm matched - " << fail_pos);
    } else {
        // yay!

        // NOTE: There can be multiple arms active, take the first.
        auto i = matches[0].arm_index;
        const auto& history = matches[0].condition_history;
        const auto& stmt_is_item_history = matches[0].stmt_is_item_history;
        DEBUG("Evalulating arm " << i);

        auto lex = TTStreamO(sp, ParseState(), mv$(input));
        lex.parse_state().crate = &crate;
        SET_MODULE(lex, mod);
        auto arm_stream = MacroPatternStream(rules.m_rules[i].m_pattern, &history);

        struct Capture {
            unsigned int binding_idx;
            ::std::vector<unsigned int> iterations;
            unsigned int cap_idx;
        };

        ::std::vector<InterpolatedFragment> captures;
        ::std::vector<Capture> capture_info;
        size_t stmt_capture_index = 0;

        for (;;) {
            const auto& pat = arm_stream.next();
            DEBUG(i << " " << pat);
            if (pat.is_End()) {
                break;
            } else if (pat.is_If()) {
                BUG(sp, "Unexpected If pattern during final matching - " << pat);
            } else if (const auto* e = pat.opt_ExpectTok()) {
                auto tok = lex.getToken();
                DEBUG(i << " ExpectTok(" << *e << ") == " << tok);
                if (tok != *e) {
                    ERROR(sp, E0000, "Expected token " << *e << " in macro arm, got " << tok);
                    break;
                }
            } else if (const auto* e = pat.opt_ExpectPat()) {
                DEBUG(i << " ExpectPat(" << e->type << " => $" << e->idx << ")");

                bool stmt_is_item = false;
                if (e->type == MacroPatEnt::PAT_STMT) {
                    ASSERT_BUG(sp, stmt_capture_index < stmt_is_item_history.size(), "Missing statement fragment classification");
                    stmt_is_item = stmt_is_item_history[stmt_capture_index++];
                }
                auto cap = Macro_HandlePatternCap(lex, e->type, stmt_is_item);

                unsigned int cap_idx = captures.size();
                captures.push_back(mv$(cap));
                capture_info.push_back(Capture{e->idx, arm_stream.get_loop_iters(), cap_idx});
            } else {
                // Unreachable.
            }
        }
        ASSERT_BUG(sp, stmt_capture_index == stmt_is_item_history.size(), "Unused statement fragment classification");

        for (const auto& cap : capture_info) {
            bound_tts.insert(cap.binding_idx, cap.iterations, mv$(captures[cap.cap_idx]));
        }
        bound_tts.set_loop_counts(arm_stream.take_loop_counts());
        return i;
    }
}

void Macro_InvokeRules_CountSubstUses(ParameterMappings& bound_tts, const ::std::vector<MacroExpansionEnt>& contents) {
    TRACE_FUNCTION;
    MacroExpandState state(contents, bound_tts);

    while (const auto* ent_ptr = state.next_ent()) {
        DEBUG(*ent_ptr);
        TU_MATCH_HDRA( (*ent_ptr), { )
        TU_ARMA(Token, e) {
            }
            TU_ARMA(Loop, e) {
            }
            TU_ARMA(NamedValue, e) {
                switch (e & ~NAMEDVALUE_VALMASK) {
                    case 0:
                    case NAMEDVALUE_TY_IGNORE:
                        // Increment a counter in `bound_tts`
                        bound_tts.inc_count(Span(), state.iterations(), e & NAMEDVALUE_VALMASK);
                        break;
                    case NAMEDVALUE_TY_MAGIC:
                    default:
                        break;
                }
            }
            TU_ARMA(Concat, cc_ents) {
                for (const auto& cc_ent : cc_ents) {
                TU_MATCH_HDRA((cc_ent), {)
                TU_ARMA(Ident, e) {
                        }
                        TU_ARMA(Named, e) {
                            switch (e & ~NAMEDVALUE_VALMASK) {
                                case 0:
                                case NAMEDVALUE_TY_IGNORE:
                                    // Increment a counter in `bound_tts`
                                    bound_tts.inc_count(Span(), state.iterations(), e & NAMEDVALUE_VALMASK);
                                    break;
                                case NAMEDVALUE_TY_MAGIC:
                                default:
                                    break;
                            }
                        }
                }
                }
            }
        }
    }
}

Position MacroExpander::getPosition() const {
    // TODO: Return the attached position of the last fetched token
    return Position(m_this_span);
}

AST::Edition MacroExpander::realGetEdition() const {
    if (m_ttstream) {
        return m_ttstream->get_edition();
    } else {
        return m_source_edition;
    }
}

Ident::Hygiene MacroExpander::realGetHygiene() const {
    if (m_ttstream) {
        return m_ttstream->get_hygiene();
    } else {
        return m_hygiene;
    }
}

Token MacroExpander::realGetToken() {
    // Use m_next_token first
    if (m_next_token.type() != TOK_NULL) {
        DEBUG("[" << m_log_index << "] m_next_token = " << m_next_token);
        return mv$(m_next_token);
    }
    // Then try m_ttstream
    if (m_ttstream.get()) {
        Token rv = m_ttstream->getToken();
        DEBUG("[" << m_log_index << "] TTStream present: " << rv);
        if (rv.type() != TOK_EOF) {
            return rv;
        }
        m_ttstream.reset();
    }

    // Loop to handle case where $crate expands to nothing
    while (const auto* next_ent_ptr = m_state.next_ent()) {
        const auto& ent = *next_ent_ptr;
        TU_MATCH_HDRA( (ent), {)
        TU_ARMA(Token, e) {
                switch (e.type()) {
                    case TOK_IDENT:
                    case TOK_LIFETIME: {
                        // Rewrite the hygiene of an ident such that idents in the macro explicitly are unique for each expansion
                        // - Appears to be a valid option.
                        auto ident = e.ident();
                        if (ident.hygiene == m_hygiene.get_parent() || m_is_macro_item) {
                            ident.hygiene = m_hygiene;
                        }
                        auto rv = Token(e.type(), std::move(ident));
                        DEBUG("[" << m_log_index << "] Updated hygine: " << rv);
                        return rv;
                        break;
                    }
                    case TOK_BYTESTRING:
                    case TOK_STRING: {
                        auto h = e.str_hygiene();
                        if (h == m_hygiene.get_parent() || m_is_macro_item) {
                            h = m_hygiene;
                        }
                        auto rv = Token(e.type(), e.str(), std::move(h));
                        DEBUG("[" << m_log_index << "] Updated hygine: " << rv);
                        return rv;
                    }
                    default:
                        DEBUG("[" << m_log_index << "] Raw token: " << e);
                        return e.clone();
                }
            }
            TU_ARMA(NamedValue, e) {
                switch (e & ~NAMEDVALUE_VALMASK) {
                    default:
                        BUG(this->point_span(), "Unknown macro metavar - 0x" << std::hex << e);
                    case NAMEDVALUE_TY_COUNT: { // `${count(VarName)}`
                        auto count = m_mappings.get_variable_count(this->point_span(), m_state.iterations(), e & NAMEDVALUE_VALMASK);
                        return Token(U128(count), CORETYPE_ANY);
                        break;
                    }
                    case NAMEDVALUE_TY_IGNORE: { // `${ignore(VarName)}`
                        auto* frag = m_mappings.get(this->point_span(), m_state.iterations(), e & NAMEDVALUE_VALMASK);
                        ASSERT_BUG(this->point_span(), frag, "Cannot find '" << (e & NAMEDVALUE_VALMASK) << "' for " << m_state.iterations());
                        // - Ignore
                        break;
                    }
                    case NAMEDVALUE_TY_MAGIC: // NAMEDVALUE_TY_MAGIC
                        switch (e) {
                            // - XXX: Hack for $crate special name
                            case NAMEDVALUE_MAGIC_CRATE:
                                DEBUG("[" << m_log_index << "] Crate name hack");
                                if (m_crate_name == "") {
                                    if (this->edition_after(AST::Edition::Rust2018)) {
                                        return Token(TOK_RWORD_CRATE);
                                    }
                                } else {
                                    m_next_token = Token(TOK_STRING, ::std::string(m_crate_name.c_str()), {});
                                    return Token(TOK_DOUBLE_COLON);
                                }
                                break;
                            case NAMEDVALUE_MAGIC_INDEX:
                                ASSERT_BUG(this->point_span(), !m_state.iterations().empty(), "${index()} with no active loop");
                                return Token(U128(m_state.iterations().back()), CORETYPE_ANY);
                            default:
                                BUG(this->point_span(), "Unknown macro metavar - 0x" << std::hex << e);
                        }
                        break;
                    case 0: {
                        auto* frag = m_mappings.get(this->point_span(), m_state.iterations(), e);
                        ASSERT_BUG(this->point_span(), frag, "Cannot find '" << e << "' for " << m_state.iterations());

                        bool can_steal = (m_mappings.dec_count(this->point_span(), m_state.iterations(), e) == false);
                        DEBUG("[" << m_log_index << "] Insert replacement #" << e << " = " << *frag);
                        if (frag->m_type == InterpolatedFragment::TT) {
                            auto res_tt = can_steal ? mv$(frag->as_tt()) : frag->as_tt().clone();
                            m_ttstream.reset(new TTStreamO(this->outerSpan(), ParseState(), mv$(res_tt)));
                            return m_ttstream->getToken();
                        } else {
                            if (can_steal) {
                                return Token(Token::TagTakeIP(), mv$(*frag));
                            } else {
                                // Clones
                                return Token(*frag);
                            }
                        }
                    } break;
                }
            }
            TU_ARMA(Concat, e) {
                std::string new_ident;
                for (const auto& ent : e) {
                TU_MATCH_HDRA( (ent), { )
                TU_ARMA(Named, v) {
                            bool can_steal = (m_mappings.dec_count(this->point_span(), m_state.iterations(), v) == false);
                            auto* frag = m_mappings.get(this->point_span(), m_state.iterations(), v);
                            ASSERT_BUG(this->point_span(), frag, "Cannot find '" << v << "' for " << m_state.iterations());
                            Token tok;
                            if (frag->m_type == InterpolatedFragment::TT) {
                                auto res_tt = can_steal ? mv$(frag->as_tt()) : frag->as_tt().clone();
                                TTStreamO tts(this->outerSpan(), ParseState(), std::move(res_tt));
                                tok = tts.getToken();
                                tts.getTokenCheck(TOK_EOF);
                            } else {
                                tok = can_steal ? Token(Token::TagTakeIP(), mv$(*frag)) : Token(*frag);
                            }
                            if (tok != TOK_IDENT) {
                                ERROR(this->point_span(), E0000, "concat with non-ident: " << tok);
                            }
                            new_ident += tok.ident().name.c_str();
                        }
                        TU_ARMA(Ident, v) {
                            new_ident += v.name.c_str();
                        }
                }
                }
                return Token(TOK_IDENT, Ident(realGetHygiene(), RcString::new_interned(new_ident)));
            }
            TU_ARMA(Loop, e) {
                //assert( e.joiner.tok() != TOK_NULL );
                DEBUG("[" << m_log_index << "] Loop joiner " << e.joiner);
                return e.joiner;
            }
        }
    }

    DEBUG("EOF");
    return Token(TOK_EOF);
}

const MacroExpansionEnt* MacroExpandState::next_ent() {
    //DEBUG("ofs " << m_offsets << " < " << m_root_contents.size());

    // Check offset of lowest layer
    while (m_offsets.size() > 0) {
        unsigned int layer = m_offsets.size() - 1;
        const auto& ents = *m_cur_ents;

        // Obtain current read position in layer, and increment
        size_t idx = m_offsets.back().read_pos++;

        // Check if limit has been reached
        if (idx < ents.size()) {
            // - If not, just handle the next entry
            const auto& ent = ents[idx];
            TU_MATCH_HDRA( (ent), {)
            TU_ARMA(Token, e) {
                    return &ent;
                }
                TU_ARMA(NamedValue, e) {
                    return &ent;
                }
                TU_ARMA(Concat, e) {
                    return &ent;
                }
                TU_ARMA(Loop, e) {
                    assert(!e.controlling_input_loops.empty());
                    unsigned int num_repeats = m_mappings.get_loop_repeats(Span(), m_iterations, *e.controlling_input_loops.begin());
                    for (auto loop_ident : e.controlling_input_loops) {
                        if (loop_ident == *e.controlling_input_loops.begin()) {
                            continue;
                        }

                        unsigned int this_repeats = m_mappings.get_loop_repeats(Span(), m_iterations, loop_ident);
                        if (this_repeats != num_repeats) {
                            // TODO: Get the variables involved, or the pattern+output spans
                            ERROR(Span(), E0000, "Mismatch in loop iterations: " << this_repeats << " != " << num_repeats);
                        }
                    }
                    DEBUG("Looping " << num_repeats << " times based on {" << e.controlling_input_loops << "}");
                    // 2. If it's going to repeat, start the loop
                    if (num_repeats > 0) {
                        m_offsets.push_back({0, 0, num_repeats});
                        m_iterations.push_back(0);
                        m_cur_ents = getCurLayer();
                    }
                }
            }
            // Fall through for loop
        } else if (layer > 0) {
            // - Otherwise, restart/end loop and fall through
            DEBUG("layer = " << layer << ", m_iterations = " << m_iterations);
            auto& cur_ofs = m_offsets.back();
            DEBUG("Layer #" << layer << " Cur: " << cur_ofs.loop_index << ", Max: " << cur_ofs.max_index);
            if (cur_ofs.loop_index + 1 < cur_ofs.max_index) {
                m_iterations.back()++;

                DEBUG("Restart layer");
                cur_ofs.read_pos = 0;
                cur_ofs.loop_index++;

                auto& loop_layer = getCurLayerEnt();
                if (loop_layer.as_Loop().joiner.type() != TOK_NULL) {
                    DEBUG("- Separator token = " << loop_layer.as_Loop().joiner);
                    return &loop_layer;
                }
                // Fall through and restart layer
            } else {
                DEBUG("Terminate layer");
                // Terminate loop, fall through to lower layers
                m_offsets.pop_back();
                m_iterations.pop_back();
                // - Special case: End of macro, avoid issues
                if (m_offsets.size() == 0) {
                    break;
                }
                m_cur_ents = getCurLayer();
            }
        } else {
            DEBUG("Terminate evaluation");
            m_offsets.pop_back();
            assert(m_offsets.size() == 0);
        }
    } // while( m_offsets NONEMPTY )

    return nullptr;
}

const MacroExpansionEnt& MacroExpandState::getCurLayerEnt() const {
    assert(m_offsets.size() > 1);

    const auto* ents = &m_root_contents;
    for (unsigned int i = 0; i < m_offsets.size() - 2; i++) {
        unsigned int ofs = m_offsets[i].read_pos;
        assert(ofs > 0 && ofs <= ents->size());
        ents = &(*ents)[ofs - 1].as_Loop().entries;
    }
    return (*ents)[m_offsets[m_offsets.size() - 2].read_pos - 1];
}

const ::std::vector<MacroExpansionEnt>* MacroExpandState::getCurLayer() const {
    assert(m_offsets.size() > 0);
    const auto* ents = &m_root_contents;
    for (unsigned int i = 0; i < m_offsets.size() - 1; i++) {
        unsigned int ofs = m_offsets[i].read_pos;
        //DEBUG(i << " ofs=" << ofs << " / " << ents->size());
        assert(ofs > 0 && ofs <= ents->size());
        ents = &(*ents)[ofs - 1].as_Loop().entries;
        //DEBUG("ents = " << ents);
    }
    return ents;
}

#include "common.h"
#include "macro_rules_macro_rules.h"
#include "parse_parseerror.h"
#include "parse_tokentree.h"
#include "parse_common.h"
#include <limits.h>

#include "macro_rules_pattern_checks.h"

bool is_token_path(eTokenType tt) {
    switch (tt) {
        case TOK_IDENT:
        case TOK_DOUBLE_COLON:
        case TOK_LT:
        case TOK_DOUBLE_LT:
        case TOK_RWORD_SELF:
        case TOK_RWORD_SUPER:
        case TOK_INTERPOLATED_PATH:
            return true;
        default:
            return false;
    }
}

bool is_token_pat(eTokenType tt) {
    if (is_token_path(tt)) {
        return true;
    }
    switch (tt) {
        case TOK_PAREN_OPEN:
        case TOK_SQUARE_OPEN:

        case TOK_UNDERSCORE:
        case TOK_AMP:
        case TOK_RWORD_BOX:
        case TOK_RWORD_REF:
        case TOK_RWORD_MUT:
        case TOK_STRING:
        case TOK_INTEGER:
        case TOK_CHAR:
        case TOK_INTERPOLATED_PATTERN:
            return true;
        default:
            return false;
    }
}

bool is_token_type(eTokenType tt) {
    if (is_token_path(tt)) {
        return true;
    }
    switch (tt) {
        case TOK_PAREN_OPEN:
        case TOK_SQUARE_OPEN:
        case TOK_STAR:
        case TOK_AMP:
        case TOK_RWORD_EXTERN:
        case TOK_RWORD_UNSAFE:
        case TOK_RWORD_FN:
        case TOK_INTERPOLATED_TYPE:
            return true;
        default:
            return false;
    }
}

bool is_token_expr(eTokenType tt) {
    if (is_token_path(tt)) {
        return true;
    }
    switch (tt) {
        // Leading unary operators
        case TOK_AMP:       // Borrow
        case TOK_STAR:      // Deref
        case TOK_DASH:      // Negate
        case TOK_EXCLAM:    // Invert
        case TOK_RWORD_BOX: // Box
        // Composite values
        case TOK_PAREN_OPEN:  // Parenthesised
        case TOK_SQUARE_OPEN: // Array

        // Flow
        case TOK_RWORD_RETURN:
        case TOK_RWORD_BREAK:
        case TOK_RWORD_CONTINUE:

        // Blocks
        case TOK_BRACE_OPEN:
        case TOK_RWORD_MATCH:
        case TOK_RWORD_IF:
        case TOK_RWORD_FOR:
        case TOK_RWORD_WHILE:
        case TOK_RWORD_LOOP:
        case TOK_RWORD_UNSAFE:

        // Closures
        case TOK_RWORD_MOVE:
        case TOK_PIPE:
        case TOK_DOUBLE_PIPE:

        // Literal tokens
        case TOK_INTEGER:
        case TOK_FLOAT:
        case TOK_STRING:
        case TOK_BYTESTRING:
        case TOK_RWORD_TRUE:
        case TOK_RWORD_FALSE:

        case TOK_INTERPOLATED_EXPR:
            return true;
        default:
            return false;
    }
}

bool is_token_stmt(eTokenType tt) {
    if (is_token_expr(tt)) {
        return true;
    }
    switch (tt) {
        case TOK_BRACE_OPEN:
        case TOK_RWORD_LET:
        case TOK_INTERPOLATED_STMT:
        case TOK_INTERPOLATED_STMT_ITEM:
            return true;
        default:
            return false;
    }
}

bool is_token_item(eTokenType tt) {
    switch (tt) {
        case TOK_HASH:

        case TOK_RWORD_PUB:
        case TOK_RWORD_UNSAFE:
        case TOK_RWORD_TYPE:
        case TOK_RWORD_CONST:
        case TOK_RWORD_STATIC:
        case TOK_RWORD_FN:
        case TOK_RWORD_STRUCT:
        case TOK_RWORD_ENUM:
        case TOK_RWORD_TRAIT:
        case TOK_RWORD_MOD:
        case TOK_RWORD_USE:
        case TOK_RWORD_EXTERN:
        case TOK_RWORD_IMPL:
        // TODO: more?
        case TOK_INTERPOLATED_ITEM:
            return true;
        default:
            return false;
    }
}

bool is_token_vis(eTokenType tt) {
    switch (tt) {
        case TOK_RWORD_PUB:
        case TOK_RWORD_CRATE:
        case TOK_INTERPOLATED_VIS:
            return true;
        default:
            return true; // TODO: Is this true? it can capture just nothing
    }
}

MacroRulesPtr::MacroRulesPtr(MacroRules* p)
    : m_ptr(p)
{
    //::std::cout << "MRP new " << m_ptr << ::std::endl;
}

MacroRulesPtr::~MacroRulesPtr() {
    if (m_ptr) {
        //::std::cout << "MRP delete " << m_ptr << ::std::endl;
        delete m_ptr;
        m_ptr = nullptr;
    }
}

::std::ostream& operator<<(::std::ostream& os, const MacroPatEnt& x) {
    switch (x.type) {
        case MacroPatEnt::PAT_TOKEN:
            os << "=" << x.tok;
            break;
        case MacroPatEnt::PAT_LOOP:
            os << "loop #" << x.name_index << x.name << " w/ " << x.tok << " [" << x.subpats << "]";
            break;
        default:
            os << "$" << x.name << ":";
            switch (x.type) {
                case MacroPatEnt::PAT_TOKEN:
                    throw "";
                case MacroPatEnt::PAT_LOOP:
                    throw "";
                case MacroPatEnt::PAT_TT:
                    os << "tt";
                    break;
                case MacroPatEnt::PAT_PAT:
                    os << "pat";
                    break;
                case MacroPatEnt::PAT_IDENT:
                    os << "ident";
                    break;
                case MacroPatEnt::PAT_PATH:
                    os << "path";
                    break;
                case MacroPatEnt::PAT_TYPE:
                    os << "type";
                    break;
                case MacroPatEnt::PAT_EXPR:
                    os << "expr";
                    break;
                case MacroPatEnt::PAT_STMT:
                    os << "stmt";
                    break;
                case MacroPatEnt::PAT_BLOCK:
                    os << "block";
                    break;
                case MacroPatEnt::PAT_META:
                    os << "meta";
                    break;
                case MacroPatEnt::PAT_ITEM:
                    os << "item";
                    break;
                case MacroPatEnt::PAT_VIS:
                    os << "vis";
                    break;
                case MacroPatEnt::PAT_LIFETIME:
                    os << "lifetime";
                    break;
                case MacroPatEnt::PAT_LITERAL:
                    os << "literal";
                    break;
            }
            break;
    }
    return os;
}

::std::ostream& operator<<(::std::ostream& os, const MacroPatEnt::Type& x) {
    switch (x) {
        case MacroPatEnt::PAT_TOKEN:
            os << "PAT_TOKEN";
            break;
        case MacroPatEnt::PAT_LOOP:
            os << "PAT_LOOP";
            break;
        case MacroPatEnt::PAT_TT:
            os << "PAT_TT";
            break;
        case MacroPatEnt::PAT_PAT:
            os << "PAT_PAT";
            break;
        case MacroPatEnt::PAT_IDENT:
            os << "PAT_IDENT";
            break;
        case MacroPatEnt::PAT_PATH:
            os << "PAT_PATH";
            break;
        case MacroPatEnt::PAT_TYPE:
            os << "PAT_TYPE";
            break;
        case MacroPatEnt::PAT_EXPR:
            os << "PAT_EXPR";
            break;
        case MacroPatEnt::PAT_STMT:
            os << "PAT_STMT";
            break;
        case MacroPatEnt::PAT_BLOCK:
            os << "PAT_BLOCK";
            break;
        case MacroPatEnt::PAT_META:
            os << "PAT_META";
            break;
        case MacroPatEnt::PAT_ITEM:
            os << "PAT_ITEM";
            break;
        case MacroPatEnt::PAT_VIS:
            os << "PAT_VIS";
            break;
        case MacroPatEnt::PAT_LIFETIME:
            os << "PAT_LIFETIME";
            break;
        case MacroPatEnt::PAT_LITERAL:
            os << "PAT_LITERAL";
            break;
    }
    return os;
}

::std::ostream& operator<<(::std::ostream& os, const SimplePatEnt& x) {
    TU_MATCH_HDRA( (x), { )
    TU_ARMA(End, _e) os << "End";
        TU_ARMA(LoopStart, e) os << "LoopStart(" << e.index << ")";
        TU_ARMA(LoopNext, _e) os << "LoopNext";
        TU_ARMA(LoopEnd, _e) os << "LoopEnd";
        TU_ARMA(Jump, e) {
            os << "Jump(->" << e.jump_target << ")";
        }
        TU_ARMA(ExpectTok, e) {
            os << "Expect(" << e << ")";
        }
        TU_ARMA(ExpectPat, e) {
            os << "Expect($" << e.idx << " = " << e.type << ")";
        }
        TU_ARMA(If, e) {
            os << "If(" << (e.is_equal ? "=" : "!=") << "[";
            for (const auto& p : e.ents) {
                if (p.ty == MacroPatEnt::PAT_TOKEN) {
                    os << p.tok;
                } else {
                    os << p.ty;
                }
                os << ", ";
            }
            os << "] ->" << e.jump_target << ")";
        }
    }
    return os;
}

::std::ostream& operator<<(::std::ostream& os, const MacroExpansionEnt& x) {
    TU_MATCH_HDRA( (x), {)
    TU_ARMA(Token, e) {
            os << "=" << e;
        }
        TU_ARMA(NamedValue, e) {
            switch (e & ~NAMEDVALUE_VALMASK) {
                case 0:
                    os << "$" << e;
                    break;
                case NAMEDVALUE_TY_COUNT:
                    os << "${count(...)}";
                    break;
                default:
                    os << "$?" << e;
            }
        }
        TU_ARMA(Concat, e) {
            os << "${concat(...)}";
        }
        TU_ARMA(Loop, e) {
            os << "${" << e.controlling_input_loops << "}(" << e.entries << ") " << e.joiner;
        }
    }
    return os;
}

MacroRules::~MacroRules() {
}

MacroRulesArm::~MacroRulesArm() {
}

#include "common.h"
#include "parse_common.h"
#include "parse_parseerror.h"
#include "macro_rules_macro_rules.h"
#include "macro_rules_pattern_checks.h"
#include "ast_crate.h" // for editions

MacroRulesPtr Parse_MacroRules(TokenStream& lex);

namespace {
    ::std::vector<SimplePatEnt> macro_pattern_to_simple(const Span& sp, const ::std::vector<MacroPatEnt>& pattern);
}

/// A partially-parsed rule within a macro_rules! blcok
class MacroRule {
public:
    ::std::vector<MacroPatEnt> m_pattern;
    Span m_pat_span;
    ::std::vector<MacroExpansionEnt> m_contents;

    MacroRule() {
    }

    MacroRule(MacroRule&&) = default;
    MacroRule(const MacroRule&) = delete;
};

/// <summary>
/// State used when parsing a rule pattern (passed to contents/expansion)
/// </summary>
struct RuleParseState {
    struct NameState {
        unsigned idx;
        std::vector<unsigned> loops;
    };

private:
    std::map<RcString, NameState> m_names;

    /// Next loop identifier
    unsigned next_loop_index;
    // Stack of current loops (indexes)
    std::vector<unsigned> loop_stack;

public:
    RuleParseState()
        : m_names()
        , next_loop_index(0)
        , loop_stack()
    {
    }

    unsigned add_name(const RcString& name) {
        unsigned idx = this->m_names.size();
        assert(this->m_names.count(name) == 0);
        DEBUG(name << " #" << idx << " @ [" << loop_stack << "]");
        auto& e = this->m_names[name];
        e.idx = idx;
        e.loops = this->loop_stack;
        return idx;
    }

    const NameState* find_name(const RcString& name) const {
        auto it = this->m_names.find(name);
        if (it == this->m_names.end()) {
            return nullptr;
        }
        return &it->second;
    }

    unsigned open_loop() {
        auto rv = next_loop_index++;
        loop_stack.push_back(rv);
        return rv;
    }

    void close_loop() {
        assert(!loop_stack.empty()); // Impossible given that `()` must be matched in a token tree
        loop_stack.pop_back();
    }
};

/// Parse the pattern of a macro_rules! arm
::std::vector<MacroPatEnt> Parse_MacroRules_Pat(TokenStream& lex, enum eTokenType open, enum eTokenType close, RuleParseState& state) {
    TRACE_FUNCTION;
    Token tok;

    ::std::vector<MacroPatEnt> ret;

    int depth = 0;
    auto ps = lex.start_span();
    while (GET_TOK(tok, lex) != close || depth > 0) {
        DEBUG("tok = " << tok);
        if (tok.type() == open) {
            depth++;
        } else if (tok.type() == close) {
            if (depth == 0) {
                throw ParseError::Generic(FMT("Unmatched " << Token(close) << " in macro pattern"));
            }
            depth--;
        }

        switch (tok.type()) {
            case TOK_DOLLAR:
                switch (GET_TOK(tok, lex)) {
                    case TOK_SQUARE_CLOSE:
                    case TOK_PAREN_CLOSE:
                        ret.push_back(MacroPatEnt(lex.end_span(ps), TOK_DOLLAR));
                        PUTBACK(tok, lex);
                        break;
                    case TOK_RWORD_CRATE: // Not valid, as `$crate` already has meaning
                        throw ParseError::Unexpected(lex, tok);
                    default:
                        // NOTE: Allow any reserved word
                        if (!Token::type_is_rword(tok.type())) {
                            throw ParseError::Unexpected(lex, tok);
                        }
                    case TOK_UNDERSCORE:
                    case TOK_IDENT: {
                        auto name = tok.type() == TOK_IDENT ? tok.ident().name : (tok.type() == TOK_UNDERSCORE ? RcString() : RcString::new_interned(tok.to_str()));
                        GET_CHECK_TOK(tok, lex, TOK_COLON);
                        GET_CHECK_TOK(tok, lex, TOK_IDENT);
                        RcString type = tok.ident().name;

                        auto idx = state.add_name(name);

                        auto sp = lex.end_span(ps);
                        MacroPatEnt::Type ty;
                        if (0)
                            ;
                        else if (type == "tt") {
                            ty = MacroPatEnt::PAT_TT;
                        } else if (type == "pat") { //
                            ty = MacroPatEnt::PAT_PAT;
                        } else if (type == "pat_param") { // Added between 39 and 54, explicitly excludes or-patterns
                            ty = MacroPatEnt::PAT_PAT;
                        } else if (type == "ident") {
                            ty = MacroPatEnt::PAT_IDENT;
                        } else if (type == "path") {
                            ty = MacroPatEnt::PAT_PATH;
                        } else if (type == "expr") {
                            ty = MacroPatEnt::PAT_EXPR;
                        } else if (type == "stmt") {
                            ty = MacroPatEnt::PAT_STMT;
                        } else if (type == "ty") {
                            ty = MacroPatEnt::PAT_TYPE;
                        } else if (type == "meta") {
                            ty = MacroPatEnt::PAT_META;
                        } else if (type == "block") {
                            ty = MacroPatEnt::PAT_BLOCK;
                        } else if (type == "item") {
                            ty = MacroPatEnt::PAT_ITEM;
                        } else if (/*TARGETVER_1_29 && */ type == "vis") { // TODO: Should this be selective?
                            ty = MacroPatEnt::PAT_VIS;
                        } else if (/*TARGETVER_1_29 && */ type == "lifetime") { // TODO: Should this be selective?
                            ty = MacroPatEnt::PAT_LIFETIME;
                        } else if (/*TARGETVER_1_39 && */ type == "literal") { // TODO: Should this be selective?
                            ty = MacroPatEnt::PAT_LITERAL;
                        } else {
                            ERROR(lex.point_span(), E0000, "Unknown fragment type '" << type << "'");
                        }
                        ret.push_back(MacroPatEnt(sp, name, idx, ty));
                        break;
                    }
                    case TOK_PAREN_OPEN: {
                        auto loop_idx = state.open_loop();
                        auto subpat = Parse_MacroRules_Pat(lex, TOK_PAREN_OPEN, TOK_PAREN_CLOSE, state);
                        state.close_loop();

                        enum eTokenType joiner = TOK_NULL;

                        GET_TOK(tok, lex); // Joiner or loop type
                        // If the token is a loop type, then it can't be a joiner
                        if (/*lex.edition_after(AST::Edition::Rust2018) &&*/ tok.type() == TOK_QMARK) {
                            // 2018 added `?` repetition operator
                        } else if (tok.type() == TOK_PLUS || tok.type() == TOK_STAR) {
                            // `+` and `*` were present at 1.0 (2015)
                        } else {
                            DEBUG("joiner = " << tok);
                            if (tok.has_data()) {
                                ERROR(lex.point_span(), E0000, "Invalid macro joiner " << tok << ", must be punctuation");
                            }
                            joiner = tok.type();
                            GET_TOK(tok, lex);
                        }
                        auto sp = lex.end_span(ps);

                        const char* sep_flag = nullptr;
                        switch (tok.type()) {
                            case TOK_PLUS:
                                sep_flag = "+";
                                break;
                            case TOK_STAR:
                                sep_flag = "*";
                                break;
                            case TOK_QMARK:
                                sep_flag = "?";
                                // TODO: Can a `$()?` have a joiner?
                                break;
                            default:
                                if (lex.edition_after(AST::Edition::Rust2018)) {
                                    throw ParseError::Unexpected(lex, tok, {TOK_PLUS, TOK_STAR, TOK_QMARK});
                                } else {
                                    throw ParseError::Unexpected(lex, tok, {TOK_PLUS, TOK_STAR});
                                }
                        }
                        assert(sep_flag);
                        DEBUG("$()" << sep_flag << " " << subpat);
                        ret.push_back(MacroPatEnt(sp, Token(joiner), sep_flag, loop_idx, ::std::move(subpat)));
                        break;
                    }
                }
                break;
            case TOK_EOF:
                throw ParseError::Unexpected(lex, tok);
            default:
                ret.push_back(MacroPatEnt(lex.end_span(ps), tok));
                break;
        }
        ps = lex.start_span();
    }

    return ret;
}

struct ContentLoopVariableUse {
    std::vector<unsigned> loop_stack;
    bool is_optional;

    // Constructor for when added as part of a variable
    ContentLoopVariableUse(std::vector<unsigned> loop_stack)
        : loop_stack(std::move(loop_stack))
        , is_optional(true)
    {
    }

    friend ::std::ostream& operator<<(::std::ostream& os, const ContentLoopVariableUse& x) {
        return os << "[" << x.loop_stack << "] " << (x.is_optional ? "optional" : "required");
    }
};

/// Parse the contents (replacement) of a macro_rules! arm
::std::vector<MacroExpansionEnt> Parse_MacroRules_Cont(TokenStream& lex, enum eTokenType open, enum eTokenType close, const RuleParseState& state, unsigned loop_depth = 0, ::std::map<unsigned int, ContentLoopVariableUse>* var_usage_ptr = nullptr) {
    TRACE_FUNCTION;

    Token tok;
    ::std::vector<MacroExpansionEnt> ret;

    int depth = 0;
    while (GET_TOK(tok, lex) != close || depth > 0) {
        if (tok.type() == TOK_EOF) {
            throw ParseError::Unexpected(lex, tok);
        }
        if (tok.type() == TOK_NULL) {
            continue;
        }

        if (tok.type() == open) {
            DEBUG("depth++");
            depth++;
        } else if (tok.type() == close) {
            DEBUG("depth--");
            if (depth == 0) {
                ERROR(lex.point_span(), E0000, "Unmatched " << Token(close) << " in macro content");
            }
            depth--;
        } else {
            // Not a change to the depth
            // NOTE: Not chained, still want to push open/close tokens to the output
        }

        // `$` - Macro metavars
        if (tok.type() == TOK_DOLLAR) {
            GET_TOK(tok, lex);

            // `$(`
            if (tok.type() == TOK_PAREN_OPEN) {
                ::std::map<unsigned int, ContentLoopVariableUse> var_usage;
                auto content = Parse_MacroRules_Cont(lex, TOK_PAREN_OPEN, TOK_PAREN_CLOSE, state, loop_depth + 1, &var_usage);
                // ^^ The above will eat the PAREN_CLOSE
                DEBUG("var_usage = {" << var_usage << "}");

                GET_TOK(tok, lex);
                enum eTokenType joiner = TOK_NULL;
                if (lex.edition_after(AST::Edition::Rust2018) && tok.type() == TOK_QMARK) {
                    // 2018 added `?` repetition operator
                } else if (tok.type() == TOK_PLUS || tok.type() == TOK_STAR) {
                    // `+` and `*` were present at 1.0 (2015)
                } else {
                    joiner = tok.type();
                    GET_TOK(tok, lex);
                }

                char loop_type;
                switch (tok.type()) {
                    case TOK_PLUS:
                        loop_type = '+';
                        break;
                    case TOK_STAR:
                        loop_type = '*';
                        break;
                    case TOK_QMARK:
                        loop_type = '?';
                        break;
                    default:
                        if (lex.edition_after(AST::Edition::Rust2018)) {
                            throw ParseError::Unexpected(lex, tok, {TOK_PLUS, TOK_STAR, TOK_QMARK});
                        } else {
                            throw ParseError::Unexpected(lex, tok, {TOK_PLUS, TOK_STAR});
                        }
                }
                bool is_optional = (loop_type != '+'); // Only '+' has to be entered

                // Look up the variables used in `var_set` and determine the controlling loop(s) for this loop
                // - Pull based on current depth
                std::set<unsigned> controlling_loops;
                for (const auto& v : var_usage) {
                    // Empty stack: Doesn't control anything
                    if (v.second.loop_stack.size() == 0) {
                        DEBUG("Root variable");
                    }
                    // We're deeper than the variable's stack, take the deepest point?
                    else if (loop_depth >= v.second.loop_stack.size()) {
                        DEBUG("Above this loop (" << loop_depth << " >= " << v.second.loop_stack.size() << ")");
                        // Don't take anything
                        //controlling_loops.insert( v.second.loop_stack.back() );
                    } else {
                        // Take the current point in the stack
                        controlling_loops.insert(v.second.loop_stack[loop_depth]);
                    }
                }
                if (controlling_loops.empty()) {
                    // quick_error 1.2.2 has a potential typo in the arm marked with "Flush buffer on meta after ident"
                    // - This is the same as the the comment in `$foo` handling below
                    WARNING(lex.point_span(), W0000, "Macro loop doesn't contain any variables at this depth, omitting as it'll not run");
                    continue;
                }
                // TODO: Check that +/*/? matches for the controlling loops
                //for(const auto& loop_idx : controlling_loops)
                //{
                //}

                if (var_usage_ptr) {
                    for (const auto& v : var_usage) {
                        auto it = var_usage_ptr->insert(v).first;
                        // If `is_optional`: Loop might not be expanded, so propagate the non-optionality of the variable
                        if (is_optional) {
                            it->second.is_optional = true;
                        }
                    }
                }

                DEBUG("joiner = " << Token(joiner) << ", controlling_loops = {" << controlling_loops << "}, content = " << content);
                ret.push_back(MacroExpansionEnt::make_Loop({mv$(content), joiner, mv$(controlling_loops)}));
            }
            // - `${operator(args}` - Extensions
            else if (tok.type() == TOK_BRACE_OPEN) {
                auto ident = lex.getTokenCheck(TOK_IDENT).ident().name;
                if (ident == "ignore") {
                    lex.getTokenCheck(TOK_PAREN_OPEN);
                    lex.getTokenIf(TOK_DOLLAR); // 1.90 (2024 edition?) requires a $
                    GET_TOK(tok, lex);
                    if (!(tok.type() == TOK_IDENT || Token::type_is_rword(tok.type()))) {
                        CHECK_TOK(tok, TOK_IDENT);
                    }
                    auto name = tok.type() == TOK_IDENT ? tok.ident().name : RcString::new_interned(tok.to_str());
                    lex.getTokenCheck(TOK_PAREN_CLOSE);
                    const auto* ns = state.find_name(name);
                    if (!ns) {
                        TODO(lex.point_span(), "Handle ${ignore(" << name << ")} - Missing");
                    }

                    DEBUG("$" << name << " #" << ns->idx << " [" << ns->loops << "]");

                    // If the current loop depth is smaller than the stack for this variable, then error
                    if (loop_depth < ns->loops.size()) {
                        ERROR(lex.point_span(), E0000, "Variable $" << name << " is still repeating at this depth (" << loop_depth << " < " << ns->loops.size() << ")");
                    }

                    if (var_usage_ptr) {
                        var_usage_ptr->insert(::std::make_pair(ns->idx, ContentLoopVariableUse(ns->loops)));
                    }
                    ret.push_back(MacroExpansionEnt(NAMEDVALUE_TY_IGNORE | ns->idx));
                } else if (ident == "count") {
                    lex.getTokenCheck(TOK_PAREN_OPEN);
                    lex.getTokenIf(TOK_DOLLAR); // 1.90 (2024 edition?) requires a $
                    GET_TOK(tok, lex);
                    if (!(tok.type() == TOK_IDENT || Token::type_is_rword(tok.type()))) {
                        CHECK_TOK(tok, TOK_IDENT);
                    }
                    auto name = tok.type() == TOK_IDENT ? tok.ident().name : RcString::new_interned(tok.to_str());
                    lex.getTokenCheck(TOK_PAREN_CLOSE);
                    const auto* ns = state.find_name(name);
                    if (!ns) {
                        TODO(lex.point_span(), "Handle ${count(" << name << ")} - Missing");
                    }

                    DEBUG("$" << name << " #" << ns->idx << " [" << ns->loops << "]");

                    // Can still be repeating
                    //// If the current loop depth is smaller than the stack for this variable, then error
                    //if( loop_depth < ns->loops.size() ) {
                    //    ERROR(lex.point_span(), E0000, "Variable $" << name << " is still repeating at this depth (" << loop_depth << " < " << ns->loops.size() << ")");
                    //}

                    if (var_usage_ptr) {
                        var_usage_ptr->insert(::std::make_pair(ns->idx, ContentLoopVariableUse(ns->loops)));
                    }
                    ret.push_back(MacroExpansionEnt(NAMEDVALUE_TY_COUNT | ns->idx));
                } else if (ident == "index") {
                    lex.getTokenCheck(TOK_PAREN_OPEN);
                    lex.getTokenCheck(TOK_PAREN_CLOSE);
                    ret.push_back(MacroExpansionEnt(NAMEDVALUE_MAGIC_INDEX));
                } else if (ident == "concat") {
                    ::std::vector<MacroExpansionConcatEnt> ents;
                    lex.getTokenCheck(TOK_PAREN_OPEN);
                    // A list of identifiers (which could be expansion entries)
                    while (true) {
                        if (lex.getTokenIf(TOK_DOLLAR)) {
                            if (lex.getTokenIf(TOK_RWORD_CRATE)) {
                                ents.push_back(MacroExpansionConcatEnt(NAMEDVALUE_MAGIC_CRATE));
                            } else {
                                GET_CHECK_TOK(tok, lex, TOK_IDENT);
                                auto name = tok.type() == TOK_IDENT ? tok.ident().name : RcString::new_interned(tok.to_str());
                                const auto* ns = state.find_name(name);
                                if (!ns) {
                                    TODO(lex.point_span(), "concat - unmapped name");
                                } else {
                                    DEBUG("CONCAT $" << name << " #" << ns->idx << " [" << ns->loops << "]");

                                    // If the current loop depth is smaller than the stack for this variable, then error
                                    if (loop_depth < ns->loops.size()) {
                                        ERROR(lex.point_span(), E0000, "Variable $" << name << " is still repeating at this depth (" << loop_depth << " < " << ns->loops.size() << ")");
                                    }

                                    if (var_usage_ptr) {
                                        var_usage_ptr->insert(::std::make_pair(ns->idx, ContentLoopVariableUse(ns->loops)));
                                    }
                                    ents.push_back(MacroExpansionConcatEnt(ns->idx));
                                }
                            }
                        } else {
                            GET_CHECK_TOK(tok, lex, TOK_IDENT);
                            ents.push_back(MacroExpansionConcatEnt(tok.ident()));
                        }
                        if (!lex.getTokenIf(TOK_COMMA)) {
                            break;
                        }
                        if (lex.lookahead(0) == TOK_PAREN_CLOSE) {
                            break;
                        }
                    }
                    lex.getTokenCheck(TOK_PAREN_CLOSE);
                    ret.push_back(MacroExpansionEnt(std::move(ents)));
                } else {
                    TODO(lex.point_span(), "Handle ${" << ident << "...}");
                }
                lex.getTokenCheck(TOK_BRACE_CLOSE);
            } else if (tok.type() == TOK_RWORD_CRATE) {
                ret.push_back(MacroExpansionEnt(NAMEDVALUE_MAGIC_CRATE));
            } else if (tok.type() == TOK_IDENT || Token::type_is_rword(tok.type())) {
                // Look up the named parameter in the list of param names for this arm
                auto name = tok.type() == TOK_IDENT ? tok.ident().name : RcString::new_interned(tok.to_str());
                const auto* ns = state.find_name(name);
                if (!ns) {
                    // NOTE: `error-chain`'s quick_error macro has an arm which refers to an undefined metavar.
                    // - Would emit a warning and use a marker index, but that's FAR too noisy

                    // Emit the literal $ <name>
                    ret.push_back(MacroExpansionEnt(Token(TOK_DOLLAR)));
                    ret.push_back(MacroExpansionEnt(mv$(tok)));
                } else {
                    DEBUG("$" << name << " #" << ns->idx << " [" << ns->loops << "]");

                    // If the current loop depth is smaller than the stack for this variable, then error
                    if (loop_depth < ns->loops.size()) {
                        ERROR(lex.point_span(), E0000, "Variable $" << name << " is still repeating at this depth (" << loop_depth << " < " << ns->loops.size() << ")");
                    }

                    if (var_usage_ptr) {
                        var_usage_ptr->insert(::std::make_pair(ns->idx, ContentLoopVariableUse(ns->loops)));
                    }
                    ret.push_back(MacroExpansionEnt(ns->idx));
                }
            } else if (tok.type() == TOK_PAREN_CLOSE || tok.type() == TOK_SQUARE_CLOSE || tok.type() == TOK_BRACE_CLOSE) {
                PUTBACK(tok, lex);
                ret.push_back(MacroExpansionEnt(Token(TOK_DOLLAR)));
            } else {
                // Expected reserved word, ident, or `(`
                throw ParseError::Unexpected(lex, tok);
            }
        } else {
            ret.push_back(MacroExpansionEnt(mv$(tok)));
        }
    }

    return ret;
}

/// Parse a single arm of a macro_rules! block - `(foo) => (bar)`
MacroRule Parse_MacroRules_Var(TokenStream& lex) {
    TRACE_FUNCTION;
    Token tok;

    MacroRule rule;

    // Pattern
    enum eTokenType close;
    switch (GET_TOK(tok, lex)) {
        case TOK_BRACE_OPEN:
            close = TOK_BRACE_CLOSE;
            break;
        case TOK_PAREN_OPEN:
            close = TOK_PAREN_CLOSE;
            break;
        case TOK_SQUARE_OPEN:
            close = TOK_SQUARE_CLOSE;
            break;
        default:
            throw ParseError::Unexpected(lex, tok);
    }
    // - Pattern entries
    RuleParseState state;
    {
        auto ps = lex.start_span();
        rule.m_pattern = Parse_MacroRules_Pat(lex, tok.type(), close, state);
        rule.m_pat_span = lex.end_span(ps);
    }

    GET_CHECK_TOK(tok, lex, TOK_FATARROW);

    // Replacement
    switch (GET_TOK(tok, lex)) {
        case TOK_BRACE_OPEN:
            close = TOK_BRACE_CLOSE;
            break;
        case TOK_PAREN_OPEN:
            close = TOK_PAREN_CLOSE;
            break;
        default:
            throw ParseError::Unexpected(lex, tok);
    }
    rule.m_contents = Parse_MacroRules_Cont(lex, tok.type(), close, state);

    DEBUG("Rule - [" << rule.m_pattern << "] => " << rule.m_contents << "");

    return rule;
}

// TODO: Also count the number of times each variable is used?
void enumerate_names(const ::std::vector<MacroPatEnt>& pats, ::std::vector<RcString>& names) {
    for (const auto& pat : pats) {
        if (pat.type == MacroPatEnt::PAT_LOOP) {
            enumerate_names(pat.subpats, names);
        } else if (pat.name != "") {
            auto b = names.begin();
            auto e = names.end();
            if (::std::find(b, e, pat.name) == e) {
                names.push_back(pat.name);
            }
        }
    }
}

MacroRulesArm Parse_MacroRules_MakeArm(Span pat_sp, ::std::vector<MacroPatEnt> pattern, ::std::vector<MacroExpansionEnt> contents) {
    // - Convert the rule into an instruction stream
    auto rule_sequence = macro_pattern_to_simple(pat_sp, pattern);
    auto arm = MacroRulesArm(mv$(rule_sequence), mv$(contents));
    enumerate_names(pattern, arm.m_param_names);
    return arm;
}

namespace {
    MacroRulesPtr make_mr_ptr(const TokenStream& lex) {
        auto s = lex.point_span();
        auto rv = MacroRulesPtr(new MacroRules(s->crate_name(), lex.get_edition()));
        rv->m_hygiene = lex.get_hygiene();
        return rv;
    }
}

/// Parse an entire macro_rules! block into a format that exec.cpp can use
MacroRulesPtr Parse_MacroRules(TokenStream& lex) {
    TRACE_FUNCTION_F("");

    Token tok;

    // Parse the patterns and replacements
    ::std::vector<MacroRule> rules;
    while (lex.lookahead(0) != TOK_EOF && lex.lookahead(0) != TOK_BRACE_CLOSE) {
        rules.push_back(Parse_MacroRules_Var(lex));
        GET_TOK(tok, lex);
        // LAZY: `macro` allows comma (not `macro_rules!`) but this is strictly more permissive than rustc
        if (tok.type() != TOK_SEMICOLON && tok.type() != TOK_COMMA) {
            PUTBACK(tok, lex);
            break;
        }
    }
    GET_TOK(tok, lex);
    if (tok.type() != TOK_EOF && tok.type() != TOK_BRACE_CLOSE) {
        throw ParseError::Unexpected(lex, tok, {TOK_EOF, TOK_BRACE_CLOSE});
    }
    DEBUG("- " << rules.size() << " rules");

    auto rv = make_mr_ptr(lex);
    // Re-parse the patterns into a unified form
    for (auto& rule : rules) {
        rv->m_rules.push_back(Parse_MacroRules_MakeArm(rule.m_pat_span, mv$(rule.m_pattern), mv$(rule.m_contents)));
    }

    return rv;
}

MacroRulesPtr Parse_MacroRulesSingleArm(TokenStream& lex) {
    TRACE_FUNCTION_F("");
    Token tok;

    RuleParseState state;

    auto ps = lex.start_span();
    GET_CHECK_TOK(tok, lex, TOK_PAREN_OPEN);
    auto arm_pat = Parse_MacroRules_Pat(lex, TOK_PAREN_OPEN, TOK_PAREN_CLOSE, state);
    auto pat_span = lex.end_span(ps);
    GET_CHECK_TOK(tok, lex, TOK_BRACE_OPEN);
    // TODO: Pass a flag that annotates all idents with the current module?
    auto body = Parse_MacroRules_Cont(lex, TOK_BRACE_OPEN, TOK_BRACE_CLOSE, state);

    auto rv = make_mr_ptr(lex);
    rv->m_rules.push_back(Parse_MacroRules_MakeArm(pat_span, ::std::move(arm_pat), ::std::move(body)));
    return rv;
}

namespace {

    struct ExpTok {
        MacroPatEnt::Type ty;
        const Token* tok;

        ExpTok(MacroPatEnt::Type ty, const Token* tok)
            : ty(ty)
            , tok(tok)
        {
        }

        bool operator==(const ExpTok& t) const {
            return this->ty == t.ty && *this->tok == *t.tok;
        }

        bool operator!=(const ExpTok& t) const {
            return !(*this == t);
        }

        bool operator==(eTokenType tt) const {
            return this->ty == MacroPatEnt::PAT_TOKEN && *this->tok == tt;
        }
    };

    ::std::ostream& operator<<(::std::ostream& os, const ExpTok& t) {
        os << "ExpTok(" << t.ty << " " << *t.tok << ")";
        return os;
    }

    enum class PatternHeadRv {
        /// An item has been added to the output
        Closed,
        /// Nothing was found (i.e. ran out of data)
        NotFound,
        /// The path didn't match
        InvalidPath,
    };

    // Yields all possible ExpectTok/ExpectPat entries from a pattern position
    // Returns `true` if there's no fall-through
    /// rv: Output vector for tokens
    /// pattern: Input pattern (match arm or body of a repeat)
    /// direct_pos: Position in `pattern` at which to start the search
    /// indirect_path: Token/check path to follow before returning an item
    /// indirect_ofs: Current offset into `indirect_path`
    PatternHeadRv macro_pattern_get_head_set_inner(::std::vector<ExpTok>& rv, const ::std::vector<MacroPatEnt>& pattern, size_t direct_pos, const std::vector<ExpTok>& indirect_path, size_t indirect_ofs) {
        TRACE_FUNCTION_F(&pattern << " #" << direct_pos << " [" << indirect_path << "]+" << indirect_ofs);
        for (size_t idx = direct_pos; idx < pattern.size(); idx++) {
            const auto& ent = pattern[idx];
            DEBUG(idx << " " << ent);
            switch (ent.type) {
                case MacroPatEnt::PAT_LOOP:
                    switch (macro_pattern_get_head_set_inner(rv, ent.subpats, 0, indirect_path, indirect_ofs)) {
                        case PatternHeadRv::InvalidPath:
                            if (ent.name == "+") {
                                return PatternHeadRv::InvalidPath;
                            } else {
                                // The path didn't match going into the loop, so consider the next token.
                            }
                            break;
                        case PatternHeadRv::Closed:
                            // + loops have to iterate at least once, so if the set is closed by the sub-patterns, close us too
                            if (ent.name == "+") {
                                return PatternHeadRv::Closed;
                            } else if (ent.name == "*" || ent.name == "?") {
                                // for * and ? loops, they can be skipped entirely.
                                // - Don't add the separator, this arm is to capture the case where the arm isn't taken.
                            } else {
                                BUG(Span(), "Unknown loop type " << ent.name);
                            }
                            break;
                        case PatternHeadRv::NotFound:
                            // Reached the end of the loop without finding a token
                            indirect_ofs += ent.subpats.size();

                            // If the inner pattern didn't close the option set, then the next token can be the separator
                            if (ent.tok != TOK_NULL) {
                                // If indirect is non-zero, decrement without doing anything
                                if (indirect_ofs < indirect_path.size()) {
                                    if (indirect_path[indirect_ofs] != ExpTok(MacroPatEnt::PAT_TOKEN, &ent.tok)) {
                                        return PatternHeadRv::InvalidPath;
                                    }
                                    indirect_ofs++;

                                    // If this is a loop (and not just an optional), attempt to repeat it
                                    if (ent.name != "?") {
                                        assert(ent.subpats.size() > 0);
                                        macro_pattern_get_head_set_inner(rv, ent.subpats, 0, indirect_path, indirect_ofs + ent.subpats.size());
                                    }
                                } else {
                                    rv.push_back(ExpTok(MacroPatEnt::PAT_TOKEN, &ent.tok));
                                    // Don't close the set yet, could be skipped
                                }
                            } else {
                                // If this is a loop (and not just an optional), attempt to repeat it
                                if (ent.name != "?") {
                                    assert(ent.subpats.size() > 0);
                                    macro_pattern_get_head_set_inner(rv, ent.subpats, 0, indirect_path, indirect_ofs + ent.subpats.size());
                                }
                            }
                            break;
                    }
                    break;
                default:
                    if (indirect_ofs < indirect_path.size()) {
                        DEBUG("IP" << indirect_ofs << " " << indirect_path[indirect_ofs]);
                        if (indirect_path[indirect_ofs] != ExpTok(ent.type, &ent.tok)) {
                            return PatternHeadRv::InvalidPath;
                        }
                        indirect_ofs++;
                    } else {
                        DEBUG("Found");
                        rv.push_back(ExpTok(ent.type, &ent.tok));
                        return PatternHeadRv::Closed;
                    }
                    break;
            }
        }
        DEBUG("Hit end");
        return PatternHeadRv::NotFound;
    }

    ::std::vector<ExpTok> macro_pattern_get_head_set(const ::std::vector<MacroPatEnt>& pattern, size_t direct_pos, const std::vector<ExpTok>& indirect_path) {
        ::std::vector<ExpTok> rv;
        TRACE_FUNCTION_FR("", rv);
        // If the pattern set isn't closed (hit something unconditional), then add `EOF` to it
        if (macro_pattern_get_head_set_inner(rv, pattern, direct_pos, indirect_path, 0) != PatternHeadRv::Closed) {
            //if(rv.empty())
            if (!::std::any_of(rv.begin(), rv.end(), [](const ExpTok& e) {
                return e.ty == MacroPatEnt::PAT_TOKEN && *e.tok == TOK_EOF;
            })) {
                static Token tok_eof = TOK_EOF;
                rv.push_back(ExpTok(MacroPatEnt::PAT_TOKEN, &tok_eof));
            }
        }
        return rv;
    }

    void macro_pattern_to_simple_inner(const Span& sp, ::std::vector<SimplePatEnt>& rv, const ::std::vector<MacroPatEnt>& pattern) {
        size_t level_start = rv.size();
        TRACE_FUNCTION_FR("[" << pattern << "]", "[" << FMT_CB(ss, for (auto it = rv.begin() + level_start; it != rv.end(); ++it) { ss << *it << ", "; }) << "]");
        auto push = [&rv](SimplePatEnt spe) {
            DEBUG("[macro_pattern_to_simple_inner] rv[" << rv.size() << "] = " << spe);
            rv.push_back(::std::move(spe));
        };
        auto push_ifv = [&push](bool is_equal, ::std::vector<SimplePatIfCheck> ents, size_t tgt) {
            push(SimplePatEnt::make_If({is_equal, tgt, mv$(ents)}));
        };
        for (size_t idx = 0; idx < pattern.size(); idx++) {
            const auto& ent = pattern[idx];
            DEBUG("[" << idx << "] ent = " << ent);
            switch (ent.type) {
                case MacroPatEnt::PAT_LOOP: {
                    auto entry_pats1 = macro_pattern_get_head_set(ent.subpats, 0, {});
                    DEBUG("Entry = [" << entry_pats1 << "]");
                    ASSERT_BUG(ent.sp, entry_pats1.size() > 0, "No entry conditions extracted from sub-pattern [" << ent.subpats << "]");
                    auto skip_pats1 = macro_pattern_get_head_set(pattern, idx + 1, {});
                    DEBUG("Skip = [" << skip_pats1 << "]");

                    // TODO: If EOF is in both entry and skip, then remove from entry
                    bool body_skippable = false;
                    if (::std::find(entry_pats1.begin(), entry_pats1.end(), TOK_EOF) != entry_pats1.end()) {
                        if (::std::find(skip_pats1.begin(), skip_pats1.end(), TOK_EOF) != entry_pats1.end()) {
                            entry_pats1.erase(::std::find(entry_pats1.begin(), entry_pats1.end(), TOK_EOF));
                            body_skippable = true;
                        }
                    }

                    std::vector<std::vector<SimplePatIfCheck>> entry_conds;
                    std::vector<std::vector<SimplePatIfCheck>> skip_conds;
                    std::vector<std::vector<SimplePatIfCheck>> repeat_conds;

                    for (const auto& ee : entry_pats1) {
                        entry_conds.push_back(::make_vec1<SimplePatIfCheck>({ee.ty, *ee.tok}));
                    }
                    for (const auto& ee : skip_pats1) {
                        skip_conds.push_back(::make_vec1<SimplePatIfCheck>({ee.ty, *ee.tok}));
                    }

                    // - Duplicates need special handling (build up a subseqent set)
                    const size_t MAX_CONDITION_ADD = 2;
                    for (size_t iterations = 0; iterations < MAX_CONDITION_ADD; iterations++) {
                        bool did_extend = false;
                        for (auto e_it = entry_conds.begin(); e_it != entry_conds.end(); ++e_it) {
                            auto s_it = ::std::find(skip_conds.begin(), skip_conds.end(), *e_it);
                            if (s_it != skip_conds.end()) {
                                did_extend = true;
                                DEBUG("Entry condition is also in skip condition: " << *e_it);

                                std::vector<ExpTok> path;
                                for (auto it = e_it->begin(); it != e_it->end(); ++it) {
                                    path.push_back(ExpTok(it->ty, &it->tok));
                                }
                                auto entry_pats2 = macro_pattern_get_head_set(ent.subpats, 0, path);
                                assert(entry_pats2.size() > 0);
                                // Replace `TOK_EOF` in entry patterns with the first skip pattern
                                if (::std::find(entry_pats2.begin(), entry_pats2.end(), TOK_EOF) != entry_pats2.end()) {
                                    entry_pats2.erase(::std::find(entry_pats2.begin(), entry_pats2.end(), TOK_EOF));
                                    entry_pats2.insert(entry_pats2.end(), skip_pats1.begin(), skip_pats1.end());
                                }
                                auto skip_pats2 = macro_pattern_get_head_set(pattern, idx + 1, path);
                                assert(skip_pats2.size() > 0);
                                DEBUG("entry_pats2 = [" << entry_pats2 << "]");
                                DEBUG("skip_pats2 = [" << skip_pats2 << "]");
                                // Update the current element for both of them, and add new elements to the end of each list
                                {
                                    auto e2_it = entry_pats2.begin();
                                    e_it->push_back({e2_it->ty, *e2_it->tok});
                                    for (++e2_it; e2_it != entry_pats2.end(); ++e2_it) {
                                        e_it = entry_conds.insert(e_it, *e_it);
                                        e_it->back() = SimplePatIfCheck{e2_it->ty, *e2_it->tok};
                                    }
                                }

                                {
                                    auto s2_it = skip_pats2.begin();
                                    s_it->push_back({s2_it->ty, *s2_it->tok});
                                    for (++s2_it; s2_it != skip_pats2.end(); ++s2_it) {
                                        s_it = skip_conds.insert(s_it, *s_it);
                                        s_it->back() = SimplePatIfCheck{s2_it->ty, *s2_it->tok};
                                    }
                                }
                            }
                        }
                        // TODO: If any end with `:vis` then extend
                        if (!did_extend) {
                            break;
                        }
                    }
                    // - If there's three-level needed, error?
                    for (auto e_it = entry_conds.begin(); e_it != entry_conds.end(); ++e_it) {
                        auto s_it = ::std::find(skip_conds.begin(), skip_conds.end(), *e_it);
                        if (s_it != skip_conds.end()) {
                            TODO(ent.sp, "Entry and skip patterns share an entry (max extend " << MAX_CONDITION_ADD << "), ambigious - " << *e_it);
                        }
                    }
                    for (const auto& e : entry_conds) {
                        DEBUG("Entry += [" << e << "]");
                    }
                    for (const auto& e : skip_conds) {
                        DEBUG("Skip += [" << e << "]");
                    }

                    // - Generate the repeat condition set
                    if (ent.tok != TOK_NULL) {
                        // NOTE: If the separator is also allowed after the list, then this can't just check for the separator
                        for (const auto& p : entry_conds) {
                            auto v = ::make_vec1<SimplePatIfCheck>({MacroPatEnt::PAT_TOKEN, ent.tok});
                            v.insert(v.end(), p.begin(), p.end());
                            repeat_conds.push_back(mv$(v));
                        }
                        // TODO: If entry indicates that it's optional (it had TOK_EOF in it) then push the skip too
                        if (body_skippable) {
                            for (const auto& p : skip_conds) {
                                auto v = ::make_vec1<SimplePatIfCheck>({MacroPatEnt::PAT_TOKEN, ent.tok});
                                v.insert(v.end(), p.begin(), p.end());
                                repeat_conds.push_back(mv$(v));
                            }
                        }
                    }
                    DEBUG("Repeat = [");
                    for (const auto& e : repeat_conds) {
                        DEBUG(" [" << e << "]");
                    }
                    DEBUG("]");

                    // TODO: Combine the two cases below into one?

                    // If the loop is a $()+ loop, then just recurse into it
                    if (ent.name == "+") {
                        push(SimplePatEnt::make_LoopStart({ent.name_index}));
                        size_t start = rv.size();
                        macro_pattern_to_simple_inner(sp, rv, ent.subpats);
                        push(SimplePatEnt::make_LoopNext({/*ent.name_index*/}));
                        size_t rewrite_start = rv.size();
                        if (ent.tok != TOK_NULL) {
                            if (repeat_conds.size() > 1) {
                                DEBUG("Loop+ Multi-option repeat");
                                size_t expect_and_jump_pos = rv.size() + repeat_conds.size() + 1;
                                for (const auto& ee : repeat_conds) {
                                    push_ifv(true, ee, expect_and_jump_pos);
                                }
                                // If any of the above succeeded, they'll jump past this jump to the ExpectTok
                                push(SimplePatEnt::make_Jump({~0u}));
                            } else {
                                DEBUG("Loop+ Single-option repeat");
                                push_ifv(false, repeat_conds.front(), ~0u);
                            }
                            push(SimplePatEnt::make_ExpectTok(ent.tok));
                            push(SimplePatEnt::make_Jump({start}));
                        } else {
                            // TODO: What if there's a collision at this level?
                            for (const auto& p : entry_conds) {
                                push_ifv(true, p, start);
                            }
                        }

                        size_t post_loop = rv.size();
                        for (size_t i = rewrite_start; i < post_loop; i++) {
                            if (auto* pe = rv[i].opt_If()) {
                                if (pe->jump_target == ~0u) {
                                    pe->jump_target = post_loop;
                                }
                            }
                            if (auto* pe = rv[i].opt_Jump()) {
                                if (pe->jump_target == ~0u) {
                                    pe->jump_target = post_loop;
                                }
                            }
                        }
                        push(SimplePatEnt::make_LoopEnd({/*ent.name_index*/}));
                    } else if (ent.name == "*" || ent.name == "?") {
                        push(SimplePatEnt::make_LoopStart({ent.name_index}));

                        // Options:
                        // - Enter the loop (if the next token is one of the head set of the loop)
                        // - Skip the loop (the next token is the head set of the subsequent entries)
                        size_t rewrite_start = rv.size();
                        if (entry_conds.size() == 1 && entry_conds[0].back().tok != TOK_EOF) // HACK: if the entry ends with `EOF` then it won't be correct
                        {
                            // If not the entry pattern, skip.
                            push_ifv(false, entry_conds.front(), ~0u);
                        } else if (skip_conds.empty()) {
                            // No skip patterns, try all entry patterns
                            size_t start = rv.size() + entry_conds.size() + 1;
                            for (const auto& p : entry_conds) {
                                push_ifv(true, p, start);
                            }
                            push(SimplePatEnt::make_Jump({~0u}));
                        } else {
                            for (const auto& p : skip_conds) {
                                push_ifv(true, p, ~0u);
                            }
                        }

                        macro_pattern_to_simple_inner(sp, rv, ent.subpats);
                        push(SimplePatEnt::make_LoopNext({/*ent.name_index*/}));

                        if (ent.name == "*") {
                            if (ent.tok != TOK_NULL) {
                                if (repeat_conds.size() == 1) {
                                    DEBUG("Loop* - Single-option repeat");
                                    // If not a repeat, jump out
                                    for (const auto& ee : repeat_conds) {
                                        push_ifv(/*is_equal*/ false, ee, ~0u);
                                    }
                                    push(SimplePatEnt::make_ExpectTok(ent.tok));
                                } else {
                                    DEBUG("Loop* - Multi-option repeat");
                                    // Multiple repeat conditions
                                    // - If any repeat condition matches, then jump to a consume
                                    auto check_pos = rv.size() + repeat_conds.size() + 1;
                                    for (const auto& ee : repeat_conds) {
                                        push_ifv(/*is_equal*/ true, ee, check_pos);
                                    }
                                    // - If none of the above matched, then jump out of the loop
                                    push(SimplePatEnt::make_Jump({~0u}));
                                    assert(rv.size() == check_pos);
                                    push(SimplePatEnt::make_ExpectTok(ent.tok));
                                }
                            }
                            // Jump back to the entry check.
                            push(SimplePatEnt::make_Jump({rewrite_start}));
                        } else if (ent.name == "?") {
                            ASSERT_BUG(sp, ent.tok == TOK_NULL, "$()? with a separator isn't valid");
                        } else {
                            BUG(sp, "");
                        }
                        size_t post_loop = rv.size();
                        for (size_t i = rewrite_start; i < post_loop; i++) {
                            if (auto* pe = rv[i].opt_If()) {
                                if (pe->jump_target == ~0u) {
                                    pe->jump_target = post_loop;
                                }
                            }
                            if (auto* pe = rv[i].opt_Jump()) {
                                if (pe->jump_target == ~0u) {
                                    pe->jump_target = post_loop;
                                }
                            }
                        }
                        push(SimplePatEnt::make_LoopEnd({/*ent.name_index*/}));
                    } else {
                        TODO(sp, "Handle loop type '" << ent.name << "'");
                    }
                } break;
                case MacroPatEnt::PAT_TOKEN:
                    push(SimplePatEnt::make_ExpectTok(ent.tok));
                    break;
                default:
                    push(SimplePatEnt::make_ExpectPat({ent.type, ent.name_index}));
                    break;
            }
        }

        for (size_t i = level_start; i < rv.size(); i++) {
            TU_MATCH_HDRA( (rv[i]), { )
            default:
                // Ignore
            TU_ARMA(If, e) {
                    ASSERT_BUG(sp, e.jump_target < rv.size(), "If target out of bounds, " << e.jump_target << " >= " << rv.size());
                }
                TU_ARMA(Jump, e) {
                    ASSERT_BUG(sp, e.jump_target < rv.size(), "Jump target out of bounds, " << e.jump_target << " >= " << rv.size());
                }
            }
        }
    }

    ::std::vector<SimplePatEnt> macro_pattern_to_simple(const Span& sp, const ::std::vector<MacroPatEnt>& pattern) {
        ::std::vector<SimplePatEnt> rv;
        TRACE_FUNCTION_FR(pattern, rv);
        macro_pattern_to_simple_inner(sp, rv, pattern);
        return rv;
    }
}
