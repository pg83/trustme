#include "macro_rules_macro_rules.h"
#include "macro_rules_macro_rules.h"

#include "common.h"
#include "hir_hir.h" // HIR::Crate
#include "ast_expr.h"
#include "ast_crate.h"
#include "parse_common.h"
#include "parse_ttstream.h"
#include "parse_tokentree.h"
#include "parse_parseerror.h"
#include "macro_rules_pattern_checks.h"
#include "parse_interpolated_fragment.h"

#include <limits.h>

// Map of: LoopIndex=>(Path=>Count)
typedef std::map<unsigned, std::map<std::vector<unsigned>, unsigned>> loopCountsT;

class ParameterMappings {
    /// A particular captured fragment
    struct CapturedVal {
        unsigned int numUses; // Number of times this var will be used
        unsigned int numUsed; // Number of times it has been used
        InterpolatedFragment frag;
    };

    /// A single layer of the capture set
    TAGGED_UNION(CaptureLayer, Vals, (Vals, ::std::vector<CapturedVal>), (Nested, ::std::vector<CaptureLayer>));

    /// Represents the fragments captured for a name
    struct CapturedVar {
        CaptureLayer topLayer;

        friend ::std::ostream& operator<<(::std::ostream& os, const CapturedVar& x) {
            os << "CapturedVar { " << x.topLayer << " }";
            return os;
        }
    };

    loopCountsT loopCounts;

    ::std::vector<CapturedVar> mMappings;
    unsigned mLayerCount;

public:
    ParameterMappings()
        : mLayerCount(0)
    {
    }

    ParameterMappings(ParameterMappings&&) = default;

    const ::std::vector<CapturedVar>& mappings() const {
        return mMappings;
    }

    void dump() const {
        DEBUG("m_mappings = {" << mMappings << "}");
    }

    size_t layerCount() const {
        return mLayerCount + 1;
    }

    void setLoopCounts(loopCountsT loopCounts) {
        for (const auto& e : loopCounts) {
            DEBUG(e.first << ": {" << e.second << "}");
        }
        this->loopCounts = std::move(loopCounts);
    }

    void insert(unsigned int nameIndex, const ::std::vector<unsigned int>& iterations, InterpolatedFragment data);

    /// <summary>
    /// Get the replacement fragment for a given loop iteration (or `nullptr`) if out of bounds
    /// </summary>
    InterpolatedFragment* get(const Span& sp, const ::std::vector<unsigned int>& iterations, unsigned int nameIdx);

    /// <summary>
    /// Given a current iteration and a loop index, return how many times this loop will run
    /// </summary>
    unsigned int getLoopRepeats(const Span& sp, const ::std::vector<unsigned int>& iterations, unsigned int loopIdx) const;

    /// <summary>
    /// Return the number of times this level of a given name/variable will loop
    /// </summary>
    unsigned int getVariableCount(const Span& sp, const ::std::vector<unsigned int>& iterations, unsigned int nameIdx) const;

    /// Increment the number of times a particular fragment will be used
    void incCount(const Span& sp, const ::std::vector<unsigned int>& iterations, unsigned int nameIdx);
    /// Decrement the number of times a particular fragment is used (returns true if there are still usages remaining)
    bool decCount(const Span& sp, const ::std::vector<unsigned int>& iterations, unsigned int nameIdx);

    friend ::std::ostream& operator<<(::std::ostream& os, const CapturedVal& x) {
        os << x.frag;
        return os;
    }

    friend ::std::ostream& operator<<(::std::ostream& os, const CaptureLayer& x) {
        TU_MATCH(CaptureLayer, (x), (e), (Vals, os << "[" << e << "]";), (Nested, os << "{" << e << "}";))
        return os;
    }

private:
    CapturedVal& getCap(const Span& sp, const ::std::vector<unsigned int>& iterations, unsigned int nameIdx);
};

class MacroPatternStream {
    const ::std::vector<SimplePatEnt>& simpleEnts;
    size_t mCurPos;

    bool lastWasCond;
    bool conditionMet;
    ::std::vector<bool> conditionHistory;

    const ::std::vector<bool>* conditionReplay;
    size_t conditionReplayPos;

    // Currently processed loop indexes
    ::std::vector<unsigned int> currentLoops;
    // Iteration index of each active loop level
    ::std::vector<unsigned int> loopIterations;

    loopCountsT loopCounts;

    bool peekCacheValid = false;
    const SimplePatEnt* peekCache;

public:
    MacroPatternStream(const ::std::vector<SimplePatEnt>& ents, const ::std::vector<bool>* conditionReplay = nullptr)
        : simpleEnts(ents)
        , mCurPos(0)
        , lastWasCond(false)
        , conditionReplay(conditionReplay)
        , conditionReplayPos(0)
    {
    }

    size_t curPos() const {
        return mCurPos;
    }

    /// Get the next pattern entry
    const SimplePatEnt& next();

    const SimplePatEnt& peek() {
        if (!peekCacheValid) {
            peekCache = &next();
            peekCacheValid = true;
        }
        return *peekCache;
    }

    /// Inform the stream that the `if` rule that was just returned succeeded
    void ifSucceeded();

    /// Get the current loop iteration count
    const ::std::vector<unsigned int>& getLoopIters() const {
        return loopIterations;
    }

    ::std::vector<bool> takeHistory() {
        return ::std::move(conditionHistory);
    }

    loopCountsT takeLoopCounts() {
        return ::std::move(loopCounts);
    }
};

// === Prototypes ===
unsigned int MacroInvokeRulesMatchPattern(const Span& sp, const MacroRules& rules, TokenTree input, const ASTCrate& crate, ASTModule& mod, ParameterMappings& boundTts);
void MacroInvokeRulesCountSubstUses(ParameterMappings& boundTts, const ::std::vector<MacroExpansionEnt>& contents);

// ------------------------------------
// ParameterMappings
// ------------------------------------

void ParameterMappings::insert(unsigned int nameIndex, const ::std::vector<unsigned int>& iterations, InterpolatedFragment data) {
    DEBUG("index=" << nameIndex << ", iterations=[" << iterations << "], data=" << data);
    if (nameIndex >= mMappings.size()) {
        mMappings.resize(nameIndex + 1);
    }
    auto* layer = &mMappings[nameIndex].topLayer;
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

ParameterMappings::CapturedVal& ParameterMappings::getCap(const Span& sp, const ::std::vector<unsigned int>& iterations, unsigned int nameIdx) {
    DEBUG("(iterations=[" << iterations << "], name_idx=" << nameIdx << ")");
    auto& e = mMappings.at(nameIdx);
    auto* layer = &e.topLayer;

    // - If the top layer is a 1-sized set of values, unconditionally return it
    if (auto* e = layer->opt_Vals()) {
        if (e->size() == 1) {
            return (*e)[0];
        }
        if (e->size() == 0) {
            BUG(sp, "Attempting to get binding for empty capture - #" << nameIdx);
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

    ERROR(sp, E0000, "Variable #" << nameIdx << " is still repeating at this level (" << iterations.size() << ")");
}

InterpolatedFragment* ParameterMappings::get(const Span& sp, const ::std::vector<unsigned int>& iterations, unsigned int nameIdx) {
    return &getCap(sp, iterations, nameIdx).frag;
}

unsigned int ParameterMappings::getLoopRepeats(const Span& sp, const ::std::vector<unsigned int>& iterations, unsigned int loopIdx) const {
    const auto& list = loopCounts.at(loopIdx);
    // Iterate the list, find the first prefix match of `iterations`
    // - `iterations` should always be longer or equal in length to every entry in `list`
    for (const auto& e : list) {
        ASSERT_BUG(Span(), e.first.size() <= iterations.size(), "Loop " << loopIdx << " iteration path [" << e.first << "] larger than query path [" << iterations << "]");
        if (std::equal(e.first.begin(), e.first.end(), iterations.begin())) {
            return e.second;
        }
    }
    BUG(sp, "Loop " << loopIdx << " cannot find an iteration count for path [" << iterations << "]");
}

unsigned int ParameterMappings::getVariableCount(const Span& sp, const ::std::vector<unsigned int>& iterations, unsigned int nameIdx) const {
    DEBUG("(iterations=[" << iterations << "], name_idx=" << nameIdx << ")");
    auto& e = mMappings.at(nameIdx);
    auto* layer = &e.topLayer;

    // - If the top layer is a 1-sized set of values, unconditionally return it
    if (auto* e = layer->opt_Vals()) {
        if (e->size() == 1) {
            return 1;
        }
        if (e->size() == 0) {
            BUG(sp, "Attempting to get binding for empty capture - #" << nameIdx);
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

void ParameterMappings::incCount(const Span& sp, const ::std::vector<unsigned int>& iterations, unsigned int nameIdx) {
    auto& cap = getCap(sp, iterations, nameIdx);
    assert(cap.numUsed == 0);
    cap.numUses += 1;
}

bool ParameterMappings::decCount(const Span& sp, const ::std::vector<unsigned int>& iterations, unsigned int nameIdx) {
    auto& cap = getCap(sp, iterations, nameIdx);
    assert(cap.numUsed < cap.numUses);
    cap.numUsed += 1;
    return (cap.numUsed < cap.numUses);
}

// ------------------------------------
// MacroPatternStream
// ------------------------------------

const SimplePatEnt& MacroPatternStream::next() {
    if (peekCacheValid) {
        peekCacheValid = false;
        return *peekCache;
    }

    for (;;) {
        // If not replaying, and the previous entry was a conditional, record the result of that conditional
        if (!conditionReplay && lastWasCond) {
            conditionHistory.push_back(conditionMet);
        }
        lastWasCond = false;
        // End of list? return End entry
        if (mCurPos == simpleEnts.size()) {
            static SimplePatEnt END = SimplePatEnt::make_End({});
            return END;
        }
        const auto& curEnt = simpleEnts[mCurPos];
        // If replaying, and this is a conditional
        if (conditionReplay && curEnt.is_If()) {
            // Skip the conditional (following its target or just skipping over)
            if ((*conditionReplay)[conditionReplayPos++]) {
                mCurPos = curEnt.as_If().jumpTarget;
            } else {
                mCurPos += 1;
            }
            continue;
        }
        mCurPos += 1;
        TU_MATCH_HDRA( (curEnt), {)
        default:
            if( curEnt.is_If() )
            {
                lastWasCond = true;
                conditionMet = false;
            }
            return curEnt;
            TU_ARMA(End, _e)
            BUG(Span(), "Unexpected End");
            TU_ARMA(Jump, e)
            mCurPos = e.jumpTarget;
            TU_ARMA(LoopStart, e) {
                currentLoops.push_back(e.index);
                loopIterations.push_back(0);
            }
            TU_ARMA(LoopNext, _e) {
                loopIterations.back() += 1;
            }
            TU_ARMA(LoopEnd, _e) {
                assert(!loopIterations.empty());
                assert(!currentLoops.empty());
                auto loopIndex = currentLoops.back();
                auto numIter = loopIterations.back();
                loopIterations.pop_back();
                currentLoops.pop_back();

                // Save this iteration count if replaying
                if (conditionReplay) {
                    loopCounts[loopIndex].insert(std::make_pair(loopIterations, numIter));
                }
            }
        }
    }
}

void MacroPatternStream::ifSucceeded() {
    assert(mCurPos > 0);
    assert(mCurPos <= simpleEnts.size());
    assert(lastWasCond);
    const auto& ent = simpleEnts[mCurPos - 1];
    ASSERT_BUG(Span(), ent.is_If(), "Expected If when calling `if_succeeded`, got " << ent);
    const auto& e = ent.as_If();
    ASSERT_BUG(Span(), e.jumpTarget < simpleEnts.size(), "Jump target " << e.jumpTarget << " out of range " << simpleEnts.size());
    mCurPos = e.jumpTarget;
    conditionMet = true;
}

// ----------------------------------------------------------------
/// State for MacroExpander and Macro_InvokeRules_CountSubstUses
class MacroExpandState {
    const ::std::vector<MacroExpansionEnt>& rootContents;
    const ParameterMappings& mMappings;

    struct tOffset {
        unsigned readPos;
        unsigned loopIndex;
        unsigned maxIndex;
    };

    /// Layer states : Index and Iteration
    ::std::vector<tOffset> offsets;
    ::std::vector<unsigned int> mIterations;

    /// Cached pointer to the current layer
    const ::std::vector<MacroExpansionEnt>* curEnts; // For faster lookup.

public:
    MacroExpandState(const ::std::vector<MacroExpansionEnt>& contents, const ParameterMappings& mappings)
        : rootContents(contents)
        , mMappings(mappings)
        , offsets({{0, 0, 0}})
        , curEnts(&rootContents)
    {
    }

    // Returns a pointer to the next entry to expand, or nullptr if the end is reached
    // - NOTE: When a Loop entry is returned, the separator token should be emitted
    const MacroExpansionEnt* nextEnt();

    const ::std::vector<unsigned int> iterations() const {
        return mIterations;
    }

    unsigned int topPos() const {
        if (offsets.empty()) {
            return 0;
        }
        return offsets[0].readPos;
    }

private:
    const MacroExpansionEnt& getCurLayerEnt() const;
    const ::std::vector<MacroExpansionEnt>* getCurLayer() const;
};

// ----------------------------------------------------------------
class MacroExpander: public TokenStream {
    // Used to track a specific invocation for debugging
    static unsigned sNextLogIndex;
    unsigned logIndex;

    Span thisSpan;
    const RcString crateName;
    Span invocationSpan;
    ASTEdition invocationEdition;

    ParameterMappings mMappings;
    MacroExpandState state;

    Token nextToken; // used for inserting a single token into the stream
    ::std::unique_ptr<TTStreamO> ttstream;
    ASTEdition sourceEdition;
    bool isMacroItem;
    Ident::Hygiene mHygiene;
    Ident::Hygiene lastHygiene;

public:
    MacroExpander(const MacroExpander& x) = delete;

    MacroExpander(const RcString& macroName, const Span& sp, ASTEdition edition, bool isMacroItem, unsigned int definitionId, const Ident::Hygiene& parentHygiene, const ::std::vector<MacroExpansionEnt>& contents, ParameterMappings mappings, RcString crateName, ASTEdition sourceEdition)
        : TokenStream(ParseState())
        , logIndex(sNextLogIndex++)
        , thisSpan(sp, crateName, macroName)
        , crateName(mv$(crateName))
        , invocationSpan(sp)
        , invocationEdition(edition)
        , mMappings(mv$(mappings))
        , state(contents, mMappings)
        , sourceEdition(sourceEdition)
        , isMacroItem(isMacroItem)
        , mHygiene(Ident::Hygiene::newScopeChained(parentHygiene, definitionId))
        , lastHygiene(mHygiene)
    {
    }

    Position getPosition() const override;

    Span outerSpan() const override {
        return invocationSpan;
    }

    Ident::Hygiene realGetHygiene() const override;
    ASTEdition realGetEdition() const override;
    Token realGetToken() override;
};

unsigned MacroExpander::sNextLogIndex = 0;
unsigned int MacroRules::gNextDefinitionId = 0;

void MacroInitDefaults() {
}

InterpolatedFragment MacroHandlePatternCap(TokenStream& lex, MacroPatEnt::Type type, bool stmtIsItem) {
    Token tok;
    switch (type) {
        case MacroPatEnt::PAT_TOKEN:
            BUG(lex.pointSpan(), "Encountered PAT_TOKEN when handling capture");
        case MacroPatEnt::PAT_LOOP:
            BUG(lex.pointSpan(), "Encountered PAT_LOOP when handling capture");

        case MacroPatEnt::PAT_TT:
            if (GET_TOK(tok, lex) == TOK_EOF) {
                throw ParseErrorUnexpected(lex, TOK_EOF);
            } else {
                PUTBACK(tok, lex);
            }
            return InterpolatedFragment(ParseTT(lex, false));
        case MacroPatEnt::PAT_PAT:
            // TODO: Is this edition check correct? Or should it be uncondiitonally "Yes"?
            return InterpolatedFragment(ParsePattern(lex, AllowOrPattern::Yes));
        case MacroPatEnt::PAT_TYPE:
            return InterpolatedFragment(ParseType(lex));
        case MacroPatEnt::PAT_EXPR:
            return InterpolatedFragment(InterpolatedFragment::EXPR, ParseExpr0(lex).release());
        case MacroPatEnt::PAT_STMT:
            if (stmtIsItem) {
                if (lex.lookahead(0) == TOK_INTERPOLATED_STMT_ITEM) {
                    tok = lex.getToken();
                    return InterpolatedFragment(InterpolatedFragment::STMT_ITEM, tok.takeFragStmtItem());
                }
                assert(lex.parseState().module);
                const auto& curMod = *lex.parseState().module;
                return InterpolatedFragment(InterpolatedFragment::STMT_ITEM, ParseModItemS(lex, curMod.fileInfo, curMod.path(), ASTAttributeList{}));
            }
            return InterpolatedFragment(InterpolatedFragment::STMT, ParseStmt(lex).release());
        case MacroPatEnt::PAT_PATH:
            // HACK for `rustc-1.90.0-src/vendor/icu_locid_transform_data-1.5.0/data/macros.rs::23`
            if (lex.lookahead(0) == TOK_INTERPOLATED_TYPE) {
                return InterpolatedFragment(std::move(lex.getToken().fragType()));
            }
            return InterpolatedFragment(ParsePath(lex, PATH_GENERIC_TYPE)); // non-expr mode
        case MacroPatEnt::PAT_BLOCK:
            return InterpolatedFragment(InterpolatedFragment::BLOCK, ParseExprBlockNode(lex).release());
        case MacroPatEnt::PAT_META:
            return InterpolatedFragment(ParseMetaItem(lex));
        case MacroPatEnt::PAT_ITEM: {
            assert(lex.parseState().module);
            const auto& curMod = *lex.parseState().module;
            return InterpolatedFragment(ParseModItemS(lex, curMod.fileInfo, curMod.path(), ASTAttributeList{}));
        } break;
        case MacroPatEnt::PAT_IDENT:
            // NOTE: Any reserved word is also valid as an ident
            GET_TOK(tok, lex);
            if (Token::typeIsRword(tok.type())) {
                return InterpolatedFragment(TokenTree(lex.getEdition(), lex.getHygiene(), tok));
            } else {
                CHECK_TOK(tok, TOK_IDENT);
                return InterpolatedFragment(TokenTree(lex.getEdition(), lex.getHygiene(), tok));
            }
        case MacroPatEnt::PAT_VIS:
            return InterpolatedFragment(ParsePublicity(lex, /*allow_restricted=*/true));
        case MacroPatEnt::PAT_LIFETIME:
            GET_CHECK_TOK(tok, lex, TOK_LIFETIME);
            return InterpolatedFragment(TokenTree(lex.getEdition(), lex.getHygiene(), tok));
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
                        throw ParseErrorUnexpected(lex, tok, {TOK_INTEGER, TOK_FLOAT});
                }
                GET_TOK(tok, lex);
                toks.push_back(tok);
                return InterpolatedFragment(TokenTree(lex.getEdition(), lex.getHygiene(), std::move(toks)));
            }
            switch (tok.type()) {
                case TOK_INTEGER:
                case TOK_FLOAT:
                case TOK_STRING:
                case TOK_BYTESTRING:
                case TOK_CSTRING:
                case TOK_RWORD_TRUE:
                case TOK_RWORD_FALSE:
                    break;
                default:
                    throw ParseErrorUnexpected(lex, tok, {TOK_INTEGER, TOK_FLOAT, TOK_STRING, TOK_BYTESTRING, TOK_CSTRING, TOK_RWORD_TRUE, TOK_RWORD_FALSE});
            }
            return InterpolatedFragment(TokenTree(lex.getEdition(), lex.getHygiene(), tok));
    }
    throw "";
}

/// Parse the input TokenTree according to the `macro_rules!` patterns and return a token stream of the replacement
::std::unique_ptr<TokenStream> MacroInvokeRules(const RcString& name, const MacroRules& rules, const Span& sp, TokenTree input, const ASTCrate& crate, ASTModule& mod) {
    TRACE_FUNCTION_F("'" << name << "', " << input);
    DEBUG("rules.m_source_crate = " << rules.sourceCrate);
    DEBUG("rules.m_hygiene = " << rules.mHygiene);

    ParameterMappings boundTts;
    unsigned int ruleIndex = MacroInvokeRulesMatchPattern(sp, rules, mv$(input), crate, mod, boundTts);

    const auto& rule = rules.rules.at(ruleIndex);

    DEBUG("Using macro '" << name << "' #" << ruleIndex << " - " << rule.contents.size() << " rule contents with " << boundTts.mappings().size() << " bound values");
    for (unsigned int i = 0; i < ::std::min(boundTts.mappings().size(), rule.paramNames.size()); i++) {
        DEBUG("- #" << i << " " << rule.paramNames.at(i) << " = [" << boundTts.mappings()[i] << "]");
    }

    // Run through the expansion counting the number of times each fragment is used
    MacroInvokeRulesCountSubstUses(boundTts, rule.contents);

    TokenStream* retPtr = new MacroExpander(name, sp, crate.edition, rules.isMacroItem, rules.definitionId, rules.mHygiene, rule.contents, mv$(boundTts), rules.sourceCrate == "" ? crate.crateNameReal : rules.sourceCrate, rules.edition);

    return ::std::unique_ptr<TokenStream>(retPtr);
}

// Collection of functions that consume a specific fragment type from a token stream
// - Does very loose consuming
namespace {
    // Class that provides read-only iteration over a TokenTree
    class TokenStreamRO {
        const TokenTree& tt;
        ::std::vector<size_t> offsets;
        size_t activeOffset;

        Token fakedNext;
        size_t consumeCount;

    public:
        TokenStreamRO(const TokenTree& tt)
            : tt(tt)
            , activeOffset(0)
            , consumeCount(0)
        {
            assert(!tt.isToken());
            if (tt.size() == 0) {
                activeOffset = 0;
                DEBUG("TOK_EOF");
            } else {
                const auto* curTree = &tt;
                while (!curTree->isToken()) {
                    curTree = &(*curTree)[0];
                    offsets.push_back(0);
                }
                assert(offsets.size() > 0);
                offsets.pop_back();
                activeOffset = 0;
                DEBUG(nextTok());
            }
        }

        TokenStreamRO clone() const {
            return TokenStreamRO(*this);
        }

        enum eTokenType next() const {
            return nextTok().type();
        }

        const Token& nextTok() const {
            static Token eofToken = TOK_EOF;

            if (fakedNext.type() != TOK_NULL) {
                return fakedNext;
            }

            if (offsets.empty() && activeOffset == tt.size()) {
                return eofToken;
            } else {
                const auto* curTree = &tt;
                for (auto idx : offsets) {
                    curTree = &(*curTree)[idx];
                }
                const auto& rv = (*curTree)[activeOffset].tok();
                return rv;
            }
        }

        void consume() {
            if (fakedNext.type() != TOK_NULL) {
                fakedNext = Token(TOK_NULL);
                return;
            }

            if (offsets.empty() && activeOffset == tt.size()) {
                throw ::std::runtime_error("Attempting to consume EOS");
            }
            DEBUG(consumeCount << " " << nextTok());
            consumeCount++;
            for (;;) {
                const auto* curTree = &tt;
                for (auto idx : offsets) {
                    curTree = &(*curTree)[idx];
                }

                activeOffset++;
                // If reached the end of a tree...
                if (activeOffset == curTree->size()) {
                    // If the end of the root is reached, return (leaving the state indicating EOS)
                    if (offsets.empty()) {
                        return;
                    }
                    // Pop and continue
                    activeOffset = offsets.back();
                    offsets.pop_back();
                } else {
                    // Dig into nested trees
                    while (!(*curTree)[activeOffset].isToken()) {
                        curTree = &(*curTree)[activeOffset];
                        offsets.push_back(activeOffset);
                        activeOffset = 0;
                    }
                    DEBUG("-> " << nextTok());
                    return;
                }
            }
        }

        void consumeAndPush(eTokenType ty) {
            consume();
            fakedNext = Token(ty);
        }

        // Consumes if the current token is `ty`, otherwise doesn't and returns false
        bool consumeIf(eTokenType ty) {
            if (next() == ty) {
                consume();
                return true;
            } else {
                return false;
            }
        }

        /// Returns the position in the stream (number of tokens that have been consumed)
        size_t position() const {
            return consumeCount;
        }
    };

    bool consumeType(TokenStreamRO& lex);
    enum class ItemConsumeMode {
        ItemFragment,
        StatementFragment,
    };
    bool consumeItem(TokenStreamRO& lex, ItemConsumeMode mode = ItemConsumeMode::ItemFragment);

    // Consume an entire TT
    bool consumeTt(TokenStreamRO& lex) {
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
                    consumeTt(lex);
                }
                lex.consume();
                break;
            case TOK_SQUARE_OPEN:
                lex.consume();
                while (lex.next() != TOK_SQUARE_CLOSE) {
                    consumeTt(lex);
                }
                lex.consume();
                break;
            case TOK_BRACE_OPEN:
                lex.consume();
                while (lex.next() != TOK_BRACE_CLOSE) {
                    consumeTt(lex);
                }
                lex.consume();
                break;
            default:
                lex.consume();
                break;
        }
        return true;
    }

    bool consumeTtAngle(TokenStreamRO& lex) {
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
                        lex.consumeAndPush(TOK_GT);
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
                consumeTt(lex);
            } else {
                lex.consume();
            }
        }
        // Consume closing token
        lex.consume();
        return true;
    }

    // Consume a path
    bool consumePath(TokenStreamRO& lex, bool typeMode = false) {
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
                if (typeMode && (lex.next() == TOK_LT || lex.next() == TOK_DOUBLE_LT || lex.next() == TOK_PAREN_OPEN))
                    ;
                // Allow a lone ident
                else if (lex.next() != TOK_DOUBLE_COLON) {
                    return true;
                } else
                    ;
                break;
            case TOK_LT:
            case TOK_DOUBLE_LT:
                if (!consumeTtAngle(lex)) {
                    return false;
                }
                if (lex.next() != TOK_DOUBLE_COLON) {
                    return false;
                }
                break;
            default:
                return false;
        }

        if (typeMode && (lex.next() == TOK_LT || lex.next() == TOK_DOUBLE_LT)) {
            if (!consumeTtAngle(lex)) {
                return false;
            }
        }

        while (lex.next() == TOK_DOUBLE_COLON) {
            lex.consume();
            if (lex.next() == TOK_STRING) {
                lex.consume();
            } else if (!typeMode && (lex.next() == TOK_LT || lex.next() == TOK_DOUBLE_LT)) {
                if (!consumeTtAngle(lex)) {
                    return false;
                }
            } else if (lex.next() == TOK_IDENT) {
                lex.consume();
                if (typeMode && (lex.next() == TOK_LT || lex.next() == TOK_DOUBLE_LT)) {
                    if (!consumeTtAngle(lex)) {
                        return false;
                    }
                }
            } else {
                return false;
            }
        }
        // Handles `Fn()`
        if (typeMode && lex.next() == TOK_PAREN_OPEN) {
            if (!consumeTt(lex)) {
                return false;
            }
            if (lex.consumeIf(TOK_THINARROW)) {
                if (!consumeType(lex)) {
                    return false;
                }
            }
        }
        return true;
    }

    bool consumeTypeTraitList(TokenStreamRO& lex) {
        do {
            if (lex.consumeIf(TOK_LIFETIME)) {
                continue;
            }
            if (!consumePath(lex, true)) {
                return false;
            }
        } while (lex.consumeIf(TOK_PLUS));
        return true;
    }

    bool consumeType(TokenStreamRO& lex) {
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
                return consumeTt(lex);
            case TOK_RWORD_IMPL:
            case TOK_RWORD_DYN:
                lex.consume();
                return consumeTypeTraitList(lex);
            case TOK_IDENT:
                if (lex.nextTok().ident().name == "dyn") {
                    lex.consume();
                    return consumeTypeTraitList(lex);
                }
            case TOK_RWORD_CRATE:
            case TOK_RWORD_SUPER:
            case TOK_RWORD_SELF:
            case TOK_DOUBLE_COLON:
            case TOK_INTERPOLATED_PATH:
            case TOK_LT:
            case TOK_DOUBLE_LT:
                if (!consumePath(lex, true)) {
                    return false;
                }
                // Macro invocation?
                if (lex.consumeIf(TOK_EXCLAM)) {
                    if (lex.next() != TOK_PAREN_OPEN && lex.next() != TOK_SQUARE_OPEN && lex.next() != TOK_BRACE_OPEN) {
                        return false;
                    }
                    if (!consumeTt(lex)) {
                        return false;
                    }
                }
                return true;
            case TOK_AMP:
            case TOK_DOUBLE_AMP:
                lex.consume();
                lex.consumeIf(TOK_LIFETIME);
                lex.consumeIf(TOK_RWORD_MUT);
                return consumeType(lex);
            case TOK_STAR:
                lex.consume();
                if (lex.consumeIf(TOK_RWORD_MUT))
                    ;
                else if (lex.consumeIf(TOK_RWORD_CONST))
                    ;
                else {
                    return false;
                }
                return consumeType(lex);
            case TOK_EXCLAM:
                lex.consume();
                return true;

            case TOK_RWORD_UNSAFE:
                lex.consume();
                if (lex.next() == TOK_RWORD_EXTERN) {
                    case TOK_RWORD_EXTERN:
                        lex.consume();
                        lex.consumeIf(TOK_STRING);
                }
                if (lex.next() != TOK_RWORD_FN) {
                    return false;
                }
            case TOK_RWORD_FN:
                lex.consume();
                if (lex.next() != TOK_PAREN_OPEN) {
                    return false;
                }
                if (!consumeTt(lex)) {
                    return false;
                }
                if (lex.consumeIf(TOK_THINARROW)) {
                    consumeType(lex);
                }
                return true;
            default:
                return false;
        }
    }

    bool consumePat(TokenStreamRO& lex, bool allowOr = true) {
        TRACE_FUNCTION;

        if (lex.next() == TOK_RWORD_REF || lex.next() == TOK_RWORD_MUT) {
            lex.consumeIf(TOK_RWORD_REF);
            lex.consumeIf(TOK_RWORD_MUT);
            if (!lex.consumeIf(TOK_IDENT)) {
                return false;
            }
            if (!lex.consumeIf(TOK_AT)) {
                return true;
            }
        }

        if (lex.consumeIf(TOK_INTERPOLATED_PATTERN)) {
            return true;
        }

        if (allowOr) {
            lex.consumeIf(TOK_PIPE);
        }
        for (;;) {
            switch (lex.next()) {
                case TOK_UNDERSCORE:
                    lex.consume();
                    if (allowOr && lex.consumeIf(TOK_PIPE)) {
                        continue;
                    }
                    return true;
                case TOK_IDENT:
                case TOK_RWORD_SUPER:
                case TOK_RWORD_SELF:
                case TOK_RWORD_CRATE:
                case TOK_DOUBLE_COLON:
                case TOK_INTERPOLATED_PATH:
                    consumePath(lex);
                    if (lex.next() == TOK_BRACE_OPEN) {
                        if (!consumeTt(lex)) {
                            return false;
                        }
                    } else if (lex.next() == TOK_PAREN_OPEN) {
                        if (!consumeTt(lex)) {
                            return false;
                        }
                    } else if (lex.next() == TOK_EXCLAM) {
                        lex.consume();
                        if (!consumeTt(lex)) {
                            return false;
                        }
                    } else {
                        // Fall through to the range handling
                        break;
                    }
                    if (allowOr && lex.consumeIf(TOK_PIPE)) {
                        continue;
                    }
                    return true;
                case TOK_RWORD_BOX:
                    lex.consume();
                    if (!consumePat(lex, allowOr)) {
                        return false;
                    }
                    if (allowOr && lex.consumeIf(TOK_PIPE)) {
                        continue;
                    }
                    return true;
                case TOK_AMP:
                case TOK_DOUBLE_AMP:
                    lex.consume();
                    lex.consumeIf(TOK_RWORD_MUT);
                    if (!consumePat(lex, allowOr)) {
                        return false;
                    }
                    if (allowOr && lex.consumeIf(TOK_PIPE)) {
                        continue;
                    }
                    return true;
                case TOK_PAREN_OPEN:
                case TOK_SQUARE_OPEN:
                    if (!consumeTt(lex)) {
                        return false;
                    }
                    if (allowOr && lex.consumeIf(TOK_PIPE)) {
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
            if (lex.consumeIf(TOK_AT)) {
                continue;
            }
            // ... or ..=
            if (lex.consumeIf(TOK_TRIPLE_DOT) || lex.consumeIf(TOK_DOUBLE_DOT_EQUAL)) {
                switch (lex.next()) {
                    case TOK_IDENT:
                    case TOK_RWORD_SUPER:
                    case TOK_RWORD_SELF:
                    case TOK_DOUBLE_COLON:
                    case TOK_INTERPOLATED_PATH:
                        consumePath(lex);
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
            if (allowOr && lex.consumeIf(TOK_PIPE)) {
                continue;
            }
            return true;
        }
    }

    // Consume an expression
    bool consumeExpr(TokenStreamRO& lex, bool noStructLit = false) {
        TRACE_FUNCTION;
        bool cont;

        while (lex.next() == TOK_HASH) {
            lex.consume();
            lex.consumeIf(TOK_EXCLAM);
            consumeTt(lex);
        }

        // Closures
        if (lex.next() == TOK_RWORD_MOVE || lex.next() == TOK_PIPE || lex.next() == TOK_DOUBLE_PIPE) {
            lex.consumeIf(TOK_RWORD_MOVE);
            if (lex.consumeIf(TOK_PIPE)) {
                do {
                    if (lex.next() == TOK_PIPE) {
                        break;
                    }
                    consumePat(lex, /*allow_or=*/false);
                    if (lex.consumeIf(TOK_COLON)) {
                        consumeType(lex);
                    }
                } while (lex.consumeIf(TOK_COMMA));
                if (!lex.consumeIf(TOK_PIPE)) {
                    return false;
                }
            } else {
                lex.consume();
            }
            if (lex.consumeIf(TOK_THINARROW)) {
                if (!consumeType(lex)) {
                    return false;
                }
            }
            return consumeExpr(lex);
        }

        do {
            bool innerCont;
            do {
                innerCont = true;
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
                        lex.consumeIf(TOK_RWORD_MUT);
                        break;
                    default:
                        innerCont = false;
                        break;
                }
            } while (innerCont);

            // :: -> path
            // ident -> path
            // '<' -> path
            // '(' -> tt
            // '[' -> tt
            switch (lex.next()) {
                case TOK_RWORD_CONTINUE:
                case TOK_RWORD_BREAK:
                    lex.consume();
                    lex.consumeIf(TOK_LIFETIME);
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
                            if (!consumeExpr(lex)) {
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
                    if (!consumePath(lex)) {
                        return false;
                    }
                    if (lex.next() == TOK_BRACE_OPEN && !noStructLit) {
                        consumeTt(lex);
                    } else if (lex.consumeIf(TOK_EXCLAM)) {
                        if (lex.consumeIf(TOK_IDENT)) {
                            // yay?
                        }
                        consumeTt(lex);
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
                case TOK_CSTRING:
                case TOK_RWORD_TRUE:
                case TOK_RWORD_FALSE:
                    lex.consume();
                    break;

                // Possibly a left-open (or full-open) range literal
                case TOK_DOUBLE_DOT:
                case TOK_DOUBLE_DOT_EQUAL:
                case TOK_TRIPLE_DOT:
                    break;

                case TOK_RWORD_ASYNC:
                    lex.consume();
                    lex.consumeIf(TOK_RWORD_MOVE);
                    return consumeExpr(lex, noStructLit);

                case TOK_RWORD_UNSAFE:
                    lex.consume();
                    if (lex.next() != TOK_BRACE_OPEN) {
                        return false;
                    }
                case TOK_PAREN_OPEN:
                case TOK_SQUARE_OPEN:
                case TOK_BRACE_OPEN:
                    consumeTt(lex);
                    break;

                // TODO: Do these count for "expr"?
                case TOK_RWORD_FOR:
                    lex.consume();
                    if (!consumePat(lex)) {
                        return false;
                    }
                    if (!lex.consumeIf(TOK_RWORD_IN)) {
                        return false;
                    }
                    if (!consumeExpr(lex, true)) {
                        return false;
                    }
                    if (lex.next() != TOK_BRACE_OPEN) {
                        return false;
                    }
                    if (!consumeTt(lex)) {
                        return false;
                    }
                    break;
                case TOK_RWORD_MATCH:
                    lex.consume();
                    // TODO: Parse _without_ consuming a struct literal
                    if (!consumeExpr(lex, true)) {
                        return false;
                    }
                    if (lex.next() != TOK_BRACE_OPEN) {
                        return false;
                    }
                    if (!consumeTt(lex)) {
                        return false;
                    }
                    break;
                case TOK_RWORD_WHILE:
                    lex.consume();
                    if (!consumeExpr(lex, true)) {
                        return false;
                    }
                    if (lex.next() != TOK_BRACE_OPEN) {
                        return false;
                    }
                    if (!consumeTt(lex)) {
                        return false;
                    }
                    break;
                case TOK_RWORD_LOOP:
                    lex.consume();
                    if (lex.next() != TOK_BRACE_OPEN) {
                        return false;
                    }
                    consumeTt(lex);
                    break;
                case TOK_RWORD_IF:
                    while (1) {
                        assert(lex.next() == TOK_RWORD_IF);
                        lex.consume();
                        if (lex.next() == TOK_RWORD_LET) {
                            lex.consume();
                            if (!consumePat(lex)) {
                                return false;
                            }
                            if (lex.next() != TOK_EQUAL) {
                                return false;
                            }
                            lex.consume();
                        }
                        if (!consumeExpr(lex, true)) {
                            return false;
                        }
                        if (lex.next() != TOK_BRACE_OPEN) {
                            return false;
                        }
                        consumeTt(lex);
                        if (lex.next() != TOK_RWORD_ELSE) {
                            break;
                        }
                        lex.consume();

                        if (lex.next() != TOK_RWORD_IF) {
                            if (lex.next() != TOK_BRACE_OPEN) {
                                return false;
                            }
                            consumeTt(lex);
                            break;
                        }
                    }
                    break;
                default:
                    return false;
            }

            do {
                innerCont = true;
                // '.' ident/int
                switch (lex.next()) {
                    case TOK_QMARK:
                        lex.consume();
                        break;
                    case TOK_DOT:
                        lex.consume();
                        if (lex.consumeIf(TOK_IDENT)) {
                            if (lex.consumeIf(TOK_DOUBLE_COLON)) {
                                if (!(lex.next() == TOK_LT || lex.next() == TOK_DOUBLE_LT)) {
                                    return false;
                                }
                                if (!consumeTtAngle(lex)) {
                                    return false;
                                }
                            }
                        } else if (lex.consumeIf(TOK_INTEGER))
                            ;
                        else {
                            return false;
                        }
                        break;
                    // '[' -> tt
                    case TOK_SQUARE_OPEN:
                    // '(' -> tt
                    case TOK_PAREN_OPEN:
                        consumeTt(lex);
                        break;
                    default:
                        innerCont = false;
                        break;
                }
            } while (innerCont);

            if (lex.consumeIf(TOK_COLON)) {
                consumeType(lex);
            }

            while (lex.consumeIf(TOK_RWORD_AS)) {
                consumeType(lex);
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

    bool consumeStmt(TokenStreamRO& lex, bool* outIsItem = nullptr) {
        TRACE_FUNCTION;
        if (outIsItem) {
            *outIsItem = false;
        }
        if (lex.consumeIf(TOK_INTERPOLATED_STMT)) {
            return true;
        }
        if (lex.consumeIf(TOK_INTERPOLATED_STMT_ITEM)) {
            if (outIsItem) {
                *outIsItem = true;
            }
            return true;
        }

        // A statement fragment includes item declarations.  Try the item
        // grammar on a checkpoint first; only advance the real stream when
        // the complete item matched, so expression statements retain their
        // normal interpretation.
        auto itemLex = lex.clone();
        if (consumeItem(itemLex, ItemConsumeMode::StatementFragment)) {
            while (lex.position() < itemLex.position()) {
                lex.consume();
            }
            if (outIsItem) {
                *outIsItem = true;
            }
            return true;
        }

        if (lex.consumeIf(TOK_RWORD_LET)) {
            if (!consumePat(lex)) {
                return false;
            }
            if (lex.consumeIf(TOK_COLON)) {
                if (!consumeType(lex)) {
                    return false;
                }
            }
            if (lex.consumeIf(TOK_EQUAL)) {
                if (!consumeExpr(lex)) {
                    return false;
                }
            }
            return true;
        } else {
            if (!consumeExpr(lex)) {
                return false;
            }
            return true;
        }
    }

    bool consumeVis(TokenStreamRO& lex) {
        TRACE_FUNCTION;
        if (lex.consumeIf(TOK_INTERPOLATED_VIS) || lex.consumeIf(TOK_RWORD_CRATE)) {
            return true;
        } else if (lex.consumeIf(TOK_RWORD_PUB)) {
            if (lex.next() == TOK_PAREN_OPEN) {
                return consumeTt(lex);
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

    bool consumeItem(TokenStreamRO& lex, ItemConsumeMode mode) {
        TRACE_FUNCTION;

        struct H {
            static bool maybeGenerics(TokenStreamRO& lex) {
                if (lex.next() == TOK_LT) {
                    if (!consumeTtAngle(lex)) {
                        return false;
                    }
                }
                return true;
            }

            static bool maybeWhere(TokenStreamRO& lex) {
                if (lex.consumeIf(TOK_RWORD_WHERE)) {
                    do {
                        if (lex.next() == TOK_BRACE_OPEN || lex.next() == TOK_SEMICOLON) {
                            break;
                        }
                        if (lex.consumeIf(TOK_LIFETIME)) {
                            if (!lex.consumeIf(TOK_COLON)) {
                                return false;
                            }

                            if (!lex.consumeIf(TOK_LIFETIME)) {
                                return false;
                            }
                        } else {
                            if (!consumeType(lex)) {
                                return false;
                            }
                            if (!lex.consumeIf(TOK_COLON)) {
                                return false;
                            }
                            do {
                                if (lex.consumeIf(TOK_LIFETIME)) {
                                } else {
                                    lex.consumeIf(TOK_QMARK);
                                    if (!consumePath(lex, true)) {
                                        return false;
                                    }
                                }
                            } while (lex.consumeIf(TOK_PLUS));
                        }
                    } while (lex.consumeIf(TOK_COMMA));
                }
                return true;
            }
        };

        while (lex.next() == TOK_HASH) {
            lex.consume();
            lex.consumeIf(TOK_EXCLAM);
            consumeTt(lex);
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
        if (mode == ItemConsumeMode::ItemFragment && ((lex.next() == TOK_IDENT && lex.nextTok().ident().name != "union") || lex.next() == TOK_RWORD_SELF || lex.next() == TOK_RWORD_SUPER || lex.next() == TOK_DOUBLE_COLON)) {
            if (!consumePath(lex)) {
                return false;
            }
            if (!lex.consumeIf(TOK_EXCLAM)) {
                return false;
            }
            lex.consumeIf(TOK_IDENT);
            bool needSemicolon = (lex.next() != TOK_BRACE_OPEN);
            consumeTt(lex);
            if (needSemicolon) {
                if (!lex.consumeIf(TOK_SEMICOLON)) {
                    return false;
                }
            }
            return true;
        }
        // Normal items
        if (!consumeVis(lex)) {
            return false;
        }
        if (lex.next() == TOK_RWORD_UNSAFE) {
            lex.consume();
        }
        DEBUG("Check item: " << lex.nextTok());
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
                if (!lex.consumeIf(TOK_IDENT)) {
                    return false;
                }
                if (lex.consumeIf(TOK_SEMICOLON))
                    ;
                else if (lex.next() == TOK_BRACE_OPEN) {
                    if (!consumeTt(lex)) {
                        return false;
                    }
                } else {
                    return false;
                }
                break;
            // impl [Foo for] Bar { ... }
            case TOK_RWORD_IMPL:
                lex.consume();
                if (!H::maybeGenerics(lex)) {
                    return false;
                }
                if (!consumeType(lex)) {
                    return false;
                }
                if (lex.consumeIf(TOK_RWORD_FOR)) {
                    if (!consumeType(lex)) {
                        return false;
                    }
                }
                if (!H::maybeWhere(lex)) {
                    return false;
                }
                if (lex.next() != TOK_BRACE_OPEN) {
                    return false;
                }
                return consumeTt(lex);
            // type Foo
            case TOK_RWORD_TYPE:
                lex.consume();
                if (!lex.consumeIf(TOK_IDENT)) {
                    return false;
                }
                if (!H::maybeGenerics(lex)) {
                    return false;
                }

                if (!lex.consumeIf(TOK_EQUAL)) {
                    return false;
                }
                if (!consumeType(lex)) {
                    return false;
                }
                if (!lex.consumeIf(TOK_SEMICOLON)) {
                    return false;
                }

                break;
            // static FOO
            case TOK_RWORD_STATIC:
                lex.consume();
                if (!lex.consumeIf(TOK_IDENT)) {
                    return false;
                }
                if (!lex.consumeIf(TOK_COLON)) {
                    return false;
                }
                if (!consumeType(lex)) {
                    return false;
                }
                if (!lex.consumeIf(TOK_EQUAL)) {
                    return false;
                }
                if (!consumeExpr(lex)) {
                    return false;
                }
                if (!lex.consumeIf(TOK_SEMICOLON)) {
                    return false;
                }
                break;
            case TOK_RWORD_STRUCT:
                lex.consume();
                if (!lex.consumeIf(TOK_IDENT)) {
                    return false;
                }
                if (!H::maybeGenerics(lex)) {
                    return false;
                }
                if (!H::maybeWhere(lex)) {
                    return false;
                }
                if (lex.consumeIf(TOK_SEMICOLON))
                    ;
                else if (lex.next() == TOK_PAREN_OPEN) {
                    if (!consumeTt(lex)) {
                        return false;
                    }
                    if (!lex.consumeIf(TOK_SEMICOLON)) {
                        return false;
                    }
                } else if (lex.next() == TOK_BRACE_OPEN) {
                    if (!consumeTt(lex)) {
                        return false;
                    }
                } else {
                    return false;
                }
                break;
            case TOK_RWORD_ENUM:
                lex.consume();
                if (!lex.consumeIf(TOK_IDENT)) {
                    return false;
                }
                if (!H::maybeGenerics(lex)) {
                    return false;
                }
                if (!H::maybeWhere(lex)) {
                    return false;
                }
                if (lex.next() != TOK_BRACE_OPEN) {
                    return false;
                }
                return consumeTt(lex);
            case TOK_IDENT:
                if (lex.nextTok().ident().name == "union") {
                    lex.consume();
                    if (lex.next() == TOK_EXCLAM) {
                        if (mode != ItemConsumeMode::ItemFragment) {
                            return false;
                        }
                        bool needSemicolon = (lex.next() != TOK_BRACE_OPEN);
                        consumeTt(lex);
                        if (needSemicolon) {
                            if (!lex.consumeIf(TOK_SEMICOLON)) {
                                return false;
                            }
                        }
                        return true;
                    } else {
                        if (!lex.consumeIf(TOK_IDENT)) {
                            return false;
                        }
                        if (!H::maybeGenerics(lex)) {
                            return false;
                        }
                        if (!H::maybeWhere(lex)) {
                            return false;
                        }
                        if (lex.next() != TOK_BRACE_OPEN) {
                            return false;
                        }
                        return consumeTt(lex);
                    }
                } else if (lex.nextTok().ident().name == "auto") {
                    lex.consume();
                    if (lex.consumeIf(TOK_RWORD_TRAIT)) {
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
                if (lex.consumeIf(TOK_RWORD_FN)) {
                    goto fn;
                } else {
                    if (!lex.consumeIf(TOK_IDENT)) {
                        return false;
                    }
                    if (!lex.consumeIf(TOK_COLON)) {
                        return false;
                    }
                    consumeType(lex);
                    if (!lex.consumeIf(TOK_EQUAL)) {
                        return false;
                    }
                    consumeExpr(lex);
                    if (!lex.consumeIf(TOK_SEMICOLON)) {
                        return false;
                    }
                }
                break;
            case TOK_RWORD_TRAIT:
                lex.consume();
            trait:
                if (!lex.consumeIf(TOK_IDENT)) {
                    return false;
                }

                if (!H::maybeGenerics(lex)) {
                    return false;
                }
                if (lex.consumeIf(TOK_COLON)) {
                    do {
                        if (lex.consumeIf(TOK_LIFETIME)) {
                        } else {
                            if (!consumePath(lex, true)) {
                                return false;
                            }
                        }
                    } while (lex.consumeIf(TOK_PLUS));
                }
                if (lex.next() != TOK_BRACE_OPEN) {
                    return false;
                }
                if (!consumeTt(lex)) {
                    return false;
                }
                break;
            case TOK_RWORD_EXTERN:
                lex.consume();
                if (lex.consumeIf(TOK_RWORD_CRATE)) {
                    if (!lex.consumeIf(TOK_IDENT)) {
                        return false;
                    }
                    if (lex.consumeIf(TOK_RWORD_AS)) {
                        if (!lex.consumeIf(TOK_IDENT)) {
                            return false;
                        }
                    }
                    if (!lex.consumeIf(TOK_SEMICOLON)) {
                        return false;
                    }
                    break;
                }

                lex.consumeIf(TOK_STRING);
                if (lex.next() == TOK_BRACE_OPEN) {
                    return consumeTt(lex);
                }
                if (!lex.consumeIf(TOK_RWORD_FN)) {
                    return false;
                }
                goto fn;
            case TOK_RWORD_FN:
                lex.consume();
            fn:
                if (!lex.consumeIf(TOK_IDENT)) {
                    return false;
                }

                if (!H::maybeGenerics(lex)) {
                    return false;
                }
                if (lex.next() != TOK_PAREN_OPEN) {
                    return false;
                }
                if (!consumeTt(lex)) {
                    return false;
                }

                if (lex.consumeIf(TOK_THINARROW)) {
                    if (!consumeType(lex)) {
                        return false;
                    }
                }

                if (!H::maybeWhere(lex)) {
                    return false;
                }

                if (lex.consumeIf(TOK_SEMICOLON)) {
                    // TODO: Is this actually valid?
                    break;
                } else if (lex.next() == TOK_BRACE_OPEN) {
                    if (!consumeTt(lex)) {
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

    bool consumeFromFrag(TokenStreamRO& lex, MacroPatEnt::Type type, bool* outStmtIsItem = nullptr) {
        TRACE_FUNCTION_F(type);
        switch (type) {
            case MacroPatEnt::PAT_TOKEN:
            case MacroPatEnt::PAT_LOOP:
                BUG(Span(), "Encountered " << type << " in consume_from_frag");
                ;
            case MacroPatEnt::PAT_BLOCK:
                if (lex.next() == TOK_BRACE_OPEN) {
                    return consumeTt(lex);
                } else if (lex.next() == TOK_INTERPOLATED_BLOCK) {
                    lex.consume();
                } else {
                    return false;
                }
                break;
            case MacroPatEnt::PAT_IDENT:
                if (lex.next() == TOK_IDENT || Token::typeIsRword(lex.next())) {
                    lex.consume();
                } else {
                    return false;
                }
                break;
            case MacroPatEnt::PAT_TT:
                return consumeTt(lex);
            case MacroPatEnt::PAT_PATH:
                return consumePath(lex, true);
            case MacroPatEnt::PAT_TYPE:
                return consumeType(lex);
            case MacroPatEnt::PAT_EXPR:
                return consumeExpr(lex);
            case MacroPatEnt::PAT_STMT:
                return consumeStmt(lex, outStmtIsItem);
            case MacroPatEnt::PAT_PAT:
                return consumePat(lex, true);
            case MacroPatEnt::PAT_META:
                if (lex.next() == TOK_INTERPOLATED_META) {
                    lex.consume();
                } else if (lex.next() == TOK_IDENT) {
                    lex.consume();
                    switch (lex.next()) {
                        case TOK_PAREN_OPEN:
                            return consumeTt(lex);
                        case TOK_EQUAL:
                            lex.consume();
                            return consumeExpr(lex);
                        default:
                            break;
                    }
                } else {
                    return false;
                }
                break;
            case MacroPatEnt::PAT_ITEM:
                return consumeItem(lex);
            case MacroPatEnt::PAT_VIS:
                return consumeVis(lex);
            case MacroPatEnt::PAT_LIFETIME:
                return lex.consumeIf(TOK_LIFETIME);
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
                    case TOK_CSTRING:
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

unsigned int MacroInvokeRulesMatchPattern(const Span& sp, const MacroRules& rules, TokenTree input, const ASTCrate& crate, ASTModule& mod, ParameterMappings& boundTts) {
    TRACE_FUNCTION_F(rules.rules.size() << " options");
    ASSERT_BUG(sp, rules.rules.size() > 0, "Empty macro_rules set");

    struct Match {
        size_t armIndex;
        ::std::vector<bool> conditionHistory;
        ::std::vector<bool> stmtIsItemHistory;
    };

    ::std::vector<Match> matches;
    ::std::vector<std::pair<size_t, eTokenType>> failPos;
    for (size_t i = 0; i < rules.rules.size(); i++) {
        auto lex = TokenStreamRO(input);
        auto armStream = MacroPatternStream(rules.rules[i].pattern);
        ::std::vector<bool> stmtIsItemHistory;

        bool fail = false;
        for (;;) {
            const auto pos = armStream.curPos();
            const auto& pat = armStream.next();
            // NOTE: The positions seen by this aren't fully sequential, as `next` steps over jumps/loop control ops
            DEBUG("Arm " << i << " @" << pos << " " << pat);
            if (pat.is_End()) {
                if (lex.next() != TOK_EOF) {
                    DEBUG("Expeced EOF, got " << lex.nextTok());
                    fail = true;
                }
                break;
            } else if (const auto* e = pat.opt_If()) {
                auto lc = lex.clone();
                bool rv = true;
                for (const auto& check : e->ents) {
                    if (check.ty != MacroPatEnt::PAT_TOKEN) {
                        if (!consumeFromFrag(lc, check.ty)) {
                            rv = false;
                            break;
                        }
                    } else {
                        if (lc.nextTok() != check.tok) {
                            rv = false;
                            break;
                        }
                        if (lc.nextTok() != TOK_EOF) {
                            lc.consume();
                        }
                    }
                }
                if (rv == e->isEqual) {
                    DEBUG("- Succeeded");
                    armStream.ifSucceeded();
                }
            } else if (const auto* e = pat.opt_ExpectTok()) {
                const auto& tok = lex.nextTok();
                DEBUG("Arm " << i << " @" << pos << " ExpectTok(" << *e << ") == " << tok);
                if (tok != *e) {
                    fail = true;
                    break;
                }
                lex.consume();
            } else if (const auto* e = pat.opt_ExpectPat()) {
                DEBUG("Arm " << i << " @" << pos << " ExpectPat(" << e->type << " => $" << e->idx << ")");
                bool stmtIsItem = false;
                if (!consumeFromFrag(lex, e->type, &stmtIsItem)) {
                    fail = true;
                    break;
                }
                if (e->type == MacroPatEnt::PAT_STMT) {
                    stmtIsItemHistory.push_back(stmtIsItem);
                }
            } else {
                // Unreachable.
            }
        }

        if (!fail) {
            matches.push_back(Match{i, armStream.takeHistory(), mv$(stmtIsItemHistory)});
            DEBUG(i << " MATCHED");
        } else {
            DEBUG(i << " FAILED");
            failPos.push_back(std::make_pair(lex.position(), lex.next()));
        }
    }

    if (matches.size() == 0) {
        // ERROR!
        // TODO: Keep track of where each arm failed.
        TODO(sp, "No arm matched - " << failPos);
    } else {
        // yay!

        // NOTE: There can be multiple arms active, take the first.
        auto i = matches[0].armIndex;
        const auto& history = matches[0].conditionHistory;
        const auto& stmtIsItemHistory = matches[0].stmtIsItemHistory;
        DEBUG("Evalulating arm " << i);

        auto lex = TTStreamO(sp, ParseState(), mv$(input));
        lex.parseState().crate = &crate;
        SET_MODULE(lex, mod);
        auto armStream = MacroPatternStream(rules.rules[i].pattern, &history);

        struct Capture {
            unsigned int bindingIdx;
            ::std::vector<unsigned int> iterations;
            unsigned int capIdx;
        };

        ::std::vector<InterpolatedFragment> captures;
        ::std::vector<Capture> captureInfo;
        size_t stmtCaptureIndex = 0;

        for (;;) {
            const auto& pat = armStream.next();
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

                bool stmtIsItem = false;
                if (e->type == MacroPatEnt::PAT_STMT) {
                    ASSERT_BUG(sp, stmtCaptureIndex < stmtIsItemHistory.size(), "Missing statement fragment classification");
                    stmtIsItem = stmtIsItemHistory[stmtCaptureIndex++];
                }
                auto cap = MacroHandlePatternCap(lex, e->type, stmtIsItem);

                unsigned int capIdx = captures.size();
                captures.push_back(mv$(cap));
                captureInfo.push_back(Capture{e->idx, armStream.getLoopIters(), capIdx});
            } else {
                // Unreachable.
            }
        }
        ASSERT_BUG(sp, stmtCaptureIndex == stmtIsItemHistory.size(), "Unused statement fragment classification");

        for (const auto& cap : captureInfo) {
            boundTts.insert(cap.bindingIdx, cap.iterations, mv$(captures[cap.capIdx]));
        }
        boundTts.setLoopCounts(armStream.takeLoopCounts());
        return i;
    }
}

void MacroInvokeRulesCountSubstUses(ParameterMappings& boundTts, const ::std::vector<MacroExpansionEnt>& contents) {
    TRACE_FUNCTION;
    MacroExpandState state(contents, boundTts);

    while (const auto* entPtr = state.nextEnt()) {
        DEBUG(*entPtr);
        TU_MATCH_HDRA( (*entPtr), { )
        TU_ARMA(Token, e) {
            }
            TU_ARMA(Loop, e) {
            }
            TU_ARMA(NamedValue, e) {
                switch (e & ~NAMEDVALUE_VALMASK) {
                    case 0:
                    case NAMEDVALUE_TY_IGNORE:
                        // Increment a counter in `bound_tts`
                        boundTts.incCount(Span(), state.iterations(), e & NAMEDVALUE_VALMASK);
                        break;
                    case NAMEDVALUE_TY_MAGIC:
                    default:
                        break;
                }
            }
            TU_ARMA(Concat, ccEnts) {
                for (const auto& ccEnt : ccEnts) {
                TU_MATCH_HDRA((ccEnt), {)
                TU_ARMA(Ident, e) {
                        }
                        TU_ARMA(Named, e) {
                            switch (e & ~NAMEDVALUE_VALMASK) {
                                case 0:
                                case NAMEDVALUE_TY_IGNORE:
                                    // Increment a counter in `bound_tts`
                                    boundTts.incCount(Span(), state.iterations(), e & NAMEDVALUE_VALMASK);
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
    return Position(thisSpan);
}

ASTEdition MacroExpander::realGetEdition() const {
    if (ttstream) {
        return ttstream->getEdition();
    } else {
        return sourceEdition;
    }
}

Ident::Hygiene MacroExpander::realGetHygiene() const {
    if (ttstream) {
        return ttstream->getHygiene();
    } else {
        return lastHygiene;
    }
}

Token MacroExpander::realGetToken() {
    lastHygiene = mHygiene;
    // Use m_next_token first
    if (nextToken.type() != TOK_NULL) {
        DEBUG("[" << logIndex << "] m_next_token = " << nextToken);
        return mv$(nextToken);
    }
    // Then try m_ttstream
    if (ttstream.get()) {
        Token rv = ttstream->getToken();
        DEBUG("[" << logIndex << "] TTStream present: " << rv);
        if (rv.type() != TOK_EOF) {
            return rv;
        }
        ttstream.reset();
    }

    // Loop to handle case where $crate expands to nothing
    while (const auto* nextEntPtr = state.nextEnt()) {
        const auto& ent = *nextEntPtr;
        TU_MATCH_HDRA( (ent), {)
        TU_ARMA(Token, e) {
                switch (e.type()) {
                    case TOK_IDENT:
                    case TOK_LIFETIME: {
                        auto ident = e.ident();
                        ident.hygiene = ident.hygiene.withTailScope(mHygiene, isMacroItem);
                        lastHygiene = ident.hygiene;
                        auto rv = Token(e.type(), std::move(ident));
                        DEBUG("[" << logIndex << "] Updated hygine: " << rv);
                        return rv;
                        break;
                    }
                    case TOK_BYTESTRING:
                    case TOK_STRING: {
                        auto h = e.strHygiene();
                        h = h.withTailScope(mHygiene, isMacroItem);
                        lastHygiene = h;
                        auto rv = Token(e.type(), e.str(), std::move(h));
                        DEBUG("[" << logIndex << "] Updated hygine: " << rv);
                        return rv;
                    }
                    default:
                        DEBUG("[" << logIndex << "] Raw token: " << e);
                        return e.clone();
                }
            }
            TU_ARMA(NamedValue, e) {
                switch (e & ~NAMEDVALUE_VALMASK) {
                    default:
                        BUG(this->pointSpan(), "Unknown macro metavar - 0x" << std::hex << e);
                    case NAMEDVALUE_TY_COUNT: { // `${count(VarName)}`
                        auto count = mMappings.getVariableCount(this->pointSpan(), state.iterations(), e & NAMEDVALUE_VALMASK);
                        return Token(U128(count), CORETYPE_ANY);
                        break;
                    }
                    case NAMEDVALUE_TY_IGNORE: { // `${ignore(VarName)}`
                        auto* frag = mMappings.get(this->pointSpan(), state.iterations(), e & NAMEDVALUE_VALMASK);
                        ASSERT_BUG(this->pointSpan(), frag, "Cannot find '" << (e & NAMEDVALUE_VALMASK) << "' for " << state.iterations());
                        // - Ignore
                        break;
                    }
                    case NAMEDVALUE_TY_MAGIC: // NAMEDVALUE_TY_MAGIC
                        switch (e) {
                            // - XXX: Hack for $crate special name
                            case NAMEDVALUE_MAGIC_CRATE:
                                DEBUG("[" << logIndex << "] Crate name hack");
                                if (crateName == "") {
                                    if (this->editionAfter(ASTEdition::Rust2018)) {
                                        return Token(TOK_RWORD_CRATE);
                                    }
                                } else {
                                    nextToken = Token(TOK_STRING, ::std::string(crateName.c_str()), {});
                                    return Token(TOK_DOUBLE_COLON);
                                }
                                break;
                            case NAMEDVALUE_MAGIC_INDEX:
                                ASSERT_BUG(this->pointSpan(), !state.iterations().empty(), "${index()} with no active loop");
                                return Token(U128(state.iterations().back()), CORETYPE_ANY);
                            default:
                                BUG(this->pointSpan(), "Unknown macro metavar - 0x" << std::hex << e);
                        }
                        break;
                    case 0: {
                        auto* frag = mMappings.get(this->pointSpan(), state.iterations(), e);
                        ASSERT_BUG(this->pointSpan(), frag, "Cannot find '" << e << "' for " << state.iterations());

                        bool canSteal = (mMappings.decCount(this->pointSpan(), state.iterations(), e) == false);
                        DEBUG("[" << logIndex << "] Insert replacement #" << e << " = " << *frag);
                        if (frag->mType == InterpolatedFragment::TT) {
                            auto resTt = canSteal ? mv$(frag->asTt()) : frag->asTt().clone();
                            ttstream.reset(new TTStreamO(this->outerSpan(), ParseState(), mv$(resTt)));
                            return ttstream->getToken();
                        } else {
                            if (canSteal) {
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
                std::string newIdent;
                for (const auto& ent : e) {
                TU_MATCH_HDRA( (ent), { )
                TU_ARMA(Named, v) {
                            bool canSteal = (mMappings.decCount(this->pointSpan(), state.iterations(), v) == false);
                            auto* frag = mMappings.get(this->pointSpan(), state.iterations(), v);
                            ASSERT_BUG(this->pointSpan(), frag, "Cannot find '" << v << "' for " << state.iterations());
                            Token tok;
                            if (frag->mType == InterpolatedFragment::TT) {
                                auto resTt = canSteal ? mv$(frag->asTt()) : frag->asTt().clone();
                                TTStreamO tts(this->outerSpan(), ParseState(), std::move(resTt));
                                tok = tts.getToken();
                                tts.getTokenCheck(TOK_EOF);
                            } else {
                                tok = canSteal ? Token(Token::TagTakeIP(), mv$(*frag)) : Token(*frag);
                            }
                            if (tok != TOK_IDENT) {
                                ERROR(this->pointSpan(), E0000, "concat with non-ident: " << tok);
                            }
                            newIdent += tok.ident().name.c_str();
                        }
                        TU_ARMA(Ident, v) {
                            newIdent += v.name.c_str();
                        }
                }
                }
                return Token(TOK_IDENT, Ident(realGetHygiene(), RcString::newInterned(newIdent)));
            }
            TU_ARMA(Loop, e) {
                DEBUG("[" << logIndex << "] Loop joiner " << e.joiner);
                return e.joiner;
            }
        }
    }

    DEBUG("EOF");
    return Token(TOK_EOF);
}

const MacroExpansionEnt* MacroExpandState::nextEnt() {

    // Check offset of lowest layer
    while (offsets.size() > 0) {
        unsigned int layer = offsets.size() - 1;
        const auto& ents = *curEnts;

        // Obtain current read position in layer, and increment
        size_t idx = offsets.back().readPos++;

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
                    assert(!e.controllingInputLoops.empty());
                    unsigned int numRepeats = mMappings.getLoopRepeats(Span(), mIterations, *e.controllingInputLoops.begin());
                    for (auto loopIdent : e.controllingInputLoops) {
                        if (loopIdent == *e.controllingInputLoops.begin()) {
                            continue;
                        }

                        unsigned int thisRepeats = mMappings.getLoopRepeats(Span(), mIterations, loopIdent);
                        if (thisRepeats != numRepeats) {
                            // TODO: Get the variables involved, or the pattern+output spans
                            ERROR(Span(), E0000, "Mismatch in loop iterations: " << thisRepeats << " != " << numRepeats);
                        }
                    }
                    DEBUG("Looping " << numRepeats << " times based on {" << e.controllingInputLoops << "}");
                    // 2. If it's going to repeat, start the loop
                    if (numRepeats > 0) {
                        offsets.push_back({0, 0, numRepeats});
                        mIterations.push_back(0);
                        curEnts = getCurLayer();
                    }
                }
            }
            // Fall through for loop
        } else if (layer > 0) {
            // - Otherwise, restart/end loop and fall through
            DEBUG("layer = " << layer << ", m_iterations = " << mIterations);
            auto& curOfs = offsets.back();
            DEBUG("Layer #" << layer << " Cur: " << curOfs.loopIndex << ", Max: " << curOfs.maxIndex);
            if (curOfs.loopIndex + 1 < curOfs.maxIndex) {
                mIterations.back()++;

                DEBUG("Restart layer");
                curOfs.readPos = 0;
                curOfs.loopIndex++;

                auto& loopLayer = getCurLayerEnt();
                if (loopLayer.as_Loop().joiner.type() != TOK_NULL) {
                    DEBUG("- Separator token = " << loopLayer.as_Loop().joiner);
                    return &loopLayer;
                }
                // Fall through and restart layer
            } else {
                DEBUG("Terminate layer");
                // Terminate loop, fall through to lower layers
                offsets.pop_back();
                mIterations.pop_back();
                // - Special case: End of macro, avoid issues
                if (offsets.size() == 0) {
                    break;
                }
                curEnts = getCurLayer();
            }
        } else {
            DEBUG("Terminate evaluation");
            offsets.pop_back();
            assert(offsets.size() == 0);
        }
    } // while( m_offsets NONEMPTY )

    return nullptr;
}

const MacroExpansionEnt& MacroExpandState::getCurLayerEnt() const {
    assert(offsets.size() > 1);

    const auto* ents = &rootContents;
    for (unsigned int i = 0; i < offsets.size() - 2; i++) {
        unsigned int ofs = offsets[i].readPos;
        assert(ofs > 0 && ofs <= ents->size());
        ents = &(*ents)[ofs - 1].as_Loop().entries;
    }
    return (*ents)[offsets[offsets.size() - 2].readPos - 1];
}

const ::std::vector<MacroExpansionEnt>* MacroExpandState::getCurLayer() const {
    assert(offsets.size() > 0);
    const auto* ents = &rootContents;
    for (unsigned int i = 0; i < offsets.size() - 1; i++) {
        unsigned int ofs = offsets[i].readPos;
        assert(ofs > 0 && ofs <= ents->size());
        ents = &(*ents)[ofs - 1].as_Loop().entries;
    }
    return ents;
}

bool isTokenPath(eTokenType tt) {
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

bool isTokenPat(eTokenType tt) {
    if (isTokenPath(tt)) {
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

bool isTokenType(eTokenType tt) {
    if (isTokenPath(tt)) {
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

bool isTokenExpr(eTokenType tt) {
    if (isTokenPath(tt)) {
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

bool isTokenStmt(eTokenType tt) {
    if (isTokenExpr(tt)) {
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

bool isTokenItem(eTokenType tt) {
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

bool isTokenVis(eTokenType tt) {
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
    : ptr(p)
{
    //::std::cout << "MRP new " << m_ptr << ::std::endl;
}

MacroRulesPtr::~MacroRulesPtr() {
    if (ptr) {
        //::std::cout << "MRP delete " << m_ptr << ::std::endl;
        delete ptr;
        ptr = nullptr;
    }
}

::std::ostream& operator<<(::std::ostream& os, const MacroPatEnt& x) {
    switch (x.type) {
        case MacroPatEnt::PAT_TOKEN:
            os << "=" << x.tok;
            break;
        case MacroPatEnt::PAT_LOOP:
            os << "loop #" << x.nameIndex << x.name << " w/ " << x.tok << " [" << x.subpats << "]";
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
            os << "Jump(->" << e.jumpTarget << ")";
        }
        TU_ARMA(ExpectTok, e) {
            os << "Expect(" << e << ")";
        }
        TU_ARMA(ExpectPat, e) {
            os << "Expect($" << e.idx << " = " << e.type << ")";
        }
        TU_ARMA(If, e) {
            os << "If(" << (e.isEqual ? "=" : "!=") << "[";
            for (const auto& p : e.ents) {
                if (p.ty == MacroPatEnt::PAT_TOKEN) {
                    os << p.tok;
                } else {
                    os << p.ty;
                }
                os << ", ";
            }
            os << "] ->" << e.jumpTarget << ")";
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
            os << "${" << e.controllingInputLoops << "}(" << e.entries << ") " << e.joiner;
        }
    }
    return os;
}

MacroRules::~MacroRules() {
}

MacroRulesArm::~MacroRulesArm() {
}

MacroRulesPtr ParseMacroRules(TokenStream& lex);

namespace {
    ::std::vector<SimplePatEnt> macroPatternToSimple(const Span& sp, const ::std::vector<MacroPatEnt>& pattern);
}

/// A partially-parsed rule within a macro_rules! blcok
class MacroRule {
public:
    ::std::vector<MacroPatEnt> pattern;
    Span patSpan;
    ::std::vector<MacroExpansionEnt> contents;

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
    std::map<RcString, NameState> names;

    /// Next loop identifier
    unsigned nextLoopIndex;
    // Stack of current loops (indexes)
    std::vector<unsigned> loopStack;

public:
    RuleParseState()
        : names()
        , nextLoopIndex(0)
        , loopStack()
    {
    }

    unsigned addName(const RcString& name) {
        unsigned idx = this->names.size();
        assert(this->names.count(name) == 0);
        DEBUG(name << " #" << idx << " @ [" << loopStack << "]");
        auto& e = this->names[name];
        e.idx = idx;
        e.loops = this->loopStack;
        return idx;
    }

    const NameState* findName(const RcString& name) const {
        auto it = this->names.find(name);
        if (it == this->names.end()) {
            return nullptr;
        }
        return &it->second;
    }

    unsigned openLoop() {
        auto rv = nextLoopIndex++;
        loopStack.push_back(rv);
        return rv;
    }

    void closeLoop() {
        assert(!loopStack.empty()); // Impossible given that `()` must be matched in a token tree
        loopStack.pop_back();
    }
};

/// Parse the pattern of a macro_rules! arm
::std::vector<MacroPatEnt> ParseMacroRulesPat(TokenStream& lex, enum eTokenType open, enum eTokenType close, RuleParseState& state) {
    TRACE_FUNCTION;
    Token tok;

    ::std::vector<MacroPatEnt> ret;

    int depth = 0;
    auto ps = lex.startSpan();
    while (GET_TOK(tok, lex) != close || depth > 0) {
        DEBUG("tok = " << tok);
        if (tok.type() == open) {
            depth++;
        } else if (tok.type() == close) {
            if (depth == 0) {
                throw CompileErrorGeneric(FMT("Unmatched " << Token(close) << " in macro pattern"));
            }
            depth--;
        }

        switch (tok.type()) {
            case TOK_DOLLAR:
                switch (GET_TOK(tok, lex)) {
                    case TOK_SQUARE_CLOSE:
                    case TOK_PAREN_CLOSE:
                        ret.push_back(MacroPatEnt(lex.endSpan(ps), TOK_DOLLAR));
                        PUTBACK(tok, lex);
                        break;
                    case TOK_RWORD_CRATE: // Not valid, as `$crate` already has meaning
                        throw ParseErrorUnexpected(lex, tok);
                    default:
                        // NOTE: Allow any reserved word
                        if (!Token::typeIsRword(tok.type())) {
                            throw ParseErrorUnexpected(lex, tok);
                        }
                    case TOK_UNDERSCORE:
                    case TOK_IDENT: {
                        auto name = tok.type() == TOK_IDENT ? tok.ident().name : (tok.type() == TOK_UNDERSCORE ? RcString() : RcString::newInterned(tok.toStr()));
                        GET_CHECK_TOK(tok, lex, TOK_COLON);
                        GET_CHECK_TOK(tok, lex, TOK_IDENT);
                        RcString type = tok.ident().name;

                        auto idx = state.addName(name);

                        auto sp = lex.endSpan(ps);
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
                        } else if (type == "vis") { // TODO: Should this be selective?
                            ty = MacroPatEnt::PAT_VIS;
                        } else if (type == "lifetime") { // TODO: Should this be selective?
                            ty = MacroPatEnt::PAT_LIFETIME;
                        } else if (type == "literal") { // TODO: Should this be selective?
                            ty = MacroPatEnt::PAT_LITERAL;
                        } else {
                            ERROR(lex.pointSpan(), E0000, "Unknown fragment type '" << type << "'");
                        }
                        ret.push_back(MacroPatEnt(sp, name, idx, ty));
                        break;
                    }
                    case TOK_PAREN_OPEN: {
                        auto loopIdx = state.openLoop();
                        auto subpat = ParseMacroRulesPat(lex, TOK_PAREN_OPEN, TOK_PAREN_CLOSE, state);
                        state.closeLoop();

                        enum eTokenType joiner = TOK_NULL;

                        GET_TOK(tok, lex); // Joiner or loop type
                        // If the token is a loop type, then it can't be a joiner
                        if (/*lex.edition_after(AST::Edition::Rust2018) &&*/ tok.type() == TOK_QMARK) {
                            // 2018 added `?` repetition operator
                        } else if (tok.type() == TOK_PLUS || tok.type() == TOK_STAR) {
                            // `+` and `*` were present at 1.0 (2015)
                        } else {
                            DEBUG("joiner = " << tok);
                            if (tok.hasData()) {
                                ERROR(lex.pointSpan(), E0000, "Invalid macro joiner " << tok << ", must be punctuation");
                            }
                            joiner = tok.type();
                            GET_TOK(tok, lex);
                        }
                        auto sp = lex.endSpan(ps);

                        const char* sepFlag = nullptr;
                        switch (tok.type()) {
                            case TOK_PLUS:
                                sepFlag = "+";
                                break;
                            case TOK_STAR:
                                sepFlag = "*";
                                break;
                            case TOK_QMARK:
                                sepFlag = "?";
                                // TODO: Can a `$()?` have a joiner?
                                break;
                            default:
                                if (lex.editionAfter(ASTEdition::Rust2018)) {
                                    throw ParseErrorUnexpected(lex, tok, {TOK_PLUS, TOK_STAR, TOK_QMARK});
                                } else {
                                    throw ParseErrorUnexpected(lex, tok, {TOK_PLUS, TOK_STAR});
                                }
                        }
                        assert(sepFlag);
                        DEBUG("$()" << sepFlag << " " << subpat);
                        ret.push_back(MacroPatEnt(sp, Token(joiner), sepFlag, loopIdx, ::std::move(subpat)));
                        break;
                    }
                }
                break;
            case TOK_EOF:
                throw ParseErrorUnexpected(lex, tok);
            default:
                ret.push_back(MacroPatEnt(lex.endSpan(ps), tok));
                break;
        }
        ps = lex.startSpan();
    }

    return ret;
}

struct ContentLoopVariableUse {
    std::vector<unsigned> loopStack;
    bool isOptional;

    // Constructor for when added as part of a variable
    ContentLoopVariableUse(std::vector<unsigned> loopStack)
        : loopStack(std::move(loopStack))
        , isOptional(true)
    {
    }

    friend ::std::ostream& operator<<(::std::ostream& os, const ContentLoopVariableUse& x) {
        return os << "[" << x.loopStack << "] " << (x.isOptional ? "optional" : "required");
    }
};

/// Parse the contents (replacement) of a macro_rules! arm
::std::vector<MacroExpansionEnt> ParseMacroRulesCont(TokenStream& lex, enum eTokenType open, enum eTokenType close, const RuleParseState& state, unsigned loopDepth = 0, ::std::map<unsigned int, ContentLoopVariableUse>* varUsagePtr = nullptr) {
    TRACE_FUNCTION;

    Token tok;
    ::std::vector<MacroExpansionEnt> ret;

    int depth = 0;
    while (GET_TOK(tok, lex) != close || depth > 0) {
        if (tok.type() == TOK_EOF) {
            throw ParseErrorUnexpected(lex, tok);
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
                ERROR(lex.pointSpan(), E0000, "Unmatched " << Token(close) << " in macro content");
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
                ::std::map<unsigned int, ContentLoopVariableUse> varUsage;
                auto content = ParseMacroRulesCont(lex, TOK_PAREN_OPEN, TOK_PAREN_CLOSE, state, loopDepth + 1, &varUsage);
                // ^^ The above will eat the PAREN_CLOSE
                DEBUG("var_usage = {" << varUsage << "}");

                GET_TOK(tok, lex);
                enum eTokenType joiner = TOK_NULL;
                if (lex.editionAfter(ASTEdition::Rust2018) && tok.type() == TOK_QMARK) {
                    // 2018 added `?` repetition operator
                } else if (tok.type() == TOK_PLUS || tok.type() == TOK_STAR) {
                    // `+` and `*` were present at 1.0 (2015)
                } else {
                    joiner = tok.type();
                    GET_TOK(tok, lex);
                }

                char loopType;
                switch (tok.type()) {
                    case TOK_PLUS:
                        loopType = '+';
                        break;
                    case TOK_STAR:
                        loopType = '*';
                        break;
                    case TOK_QMARK:
                        loopType = '?';
                        break;
                    default:
                        if (lex.editionAfter(ASTEdition::Rust2018)) {
                            throw ParseErrorUnexpected(lex, tok, {TOK_PLUS, TOK_STAR, TOK_QMARK});
                        } else {
                            throw ParseErrorUnexpected(lex, tok, {TOK_PLUS, TOK_STAR});
                        }
                }
                bool isOptional = (loopType != '+'); // Only '+' has to be entered

                // Look up the variables used in `var_set` and determine the controlling loop(s) for this loop
                // - Pull based on current depth
                std::set<unsigned> controllingLoops;
                for (const auto& v : varUsage) {
                    // Empty stack: Doesn't control anything
                    if (v.second.loopStack.size() == 0) {
                        DEBUG("Root variable");
                    }
                    // We're deeper than the variable's stack, take the deepest point?
                    else if (loopDepth >= v.second.loopStack.size()) {
                        DEBUG("Above this loop (" << loopDepth << " >= " << v.second.loopStack.size() << ")");
                        // Don't take anything
                    } else {
                        // Take the current point in the stack
                        controllingLoops.insert(v.second.loopStack[loopDepth]);
                    }
                }
                if (controllingLoops.empty()) {
                    // quick_error 1.2.2 has a potential typo in the arm marked with "Flush buffer on meta after ident"
                    // - This is the same as the the comment in `$foo` handling below
                    WARNING(lex.pointSpan(), W0000, "Macro loop doesn't contain any variables at this depth, omitting as it'll not run");
                    continue;
                }
                // TODO: Check that +/*/? matches for the controlling loops
                //for(const auto& loop_idx : controlling_loops)
                //{
                //}

                if (varUsagePtr) {
                    for (const auto& v : varUsage) {
                        auto it = varUsagePtr->insert(v).first;
                        // If `is_optional`: Loop might not be expanded, so propagate the non-optionality of the variable
                        if (isOptional) {
                            it->second.isOptional = true;
                        }
                    }
                }

                DEBUG("joiner = " << Token(joiner) << ", controlling_loops = {" << controllingLoops << "}, content = " << content);
                ret.push_back(MacroExpansionEnt::make_Loop({mv$(content), joiner, mv$(controllingLoops)}));
            }
            // - `${operator(args}` - Extensions
            else if (tok.type() == TOK_BRACE_OPEN) {
                auto ident = lex.getTokenCheck(TOK_IDENT).ident().name;
                if (ident == "ignore") {
                    lex.getTokenCheck(TOK_PAREN_OPEN);
                    lex.getTokenIf(TOK_DOLLAR); // 1.90 (2024 edition?) requires a $
                    GET_TOK(tok, lex);
                    if (!(tok.type() == TOK_IDENT || Token::typeIsRword(tok.type()))) {
                        CHECK_TOK(tok, TOK_IDENT);
                    }
                    auto name = tok.type() == TOK_IDENT ? tok.ident().name : RcString::newInterned(tok.toStr());
                    lex.getTokenCheck(TOK_PAREN_CLOSE);
                    const auto* ns = state.findName(name);
                    if (!ns) {
                        TODO(lex.pointSpan(), "Handle ${ignore(" << name << ")} - Missing");
                    }

                    DEBUG("$" << name << " #" << ns->idx << " [" << ns->loops << "]");

                    // If the current loop depth is smaller than the stack for this variable, then error
                    if (loopDepth < ns->loops.size()) {
                        ERROR(lex.pointSpan(), E0000, "Variable $" << name << " is still repeating at this depth (" << loopDepth << " < " << ns->loops.size() << ")");
                    }

                    if (varUsagePtr) {
                        varUsagePtr->insert(::std::make_pair(ns->idx, ContentLoopVariableUse(ns->loops)));
                    }
                    ret.push_back(MacroExpansionEnt(NAMEDVALUE_TY_IGNORE | ns->idx));
                } else if (ident == "count") {
                    lex.getTokenCheck(TOK_PAREN_OPEN);
                    lex.getTokenIf(TOK_DOLLAR); // 1.90 (2024 edition?) requires a $
                    GET_TOK(tok, lex);
                    if (!(tok.type() == TOK_IDENT || Token::typeIsRword(tok.type()))) {
                        CHECK_TOK(tok, TOK_IDENT);
                    }
                    auto name = tok.type() == TOK_IDENT ? tok.ident().name : RcString::newInterned(tok.toStr());
                    lex.getTokenCheck(TOK_PAREN_CLOSE);
                    const auto* ns = state.findName(name);
                    if (!ns) {
                        TODO(lex.pointSpan(), "Handle ${count(" << name << ")} - Missing");
                    }

                    DEBUG("$" << name << " #" << ns->idx << " [" << ns->loops << "]");

                    // Can still be repeating
                    //// If the current loop depth is smaller than the stack for this variable, then error
                    //}

                    if (varUsagePtr) {
                        varUsagePtr->insert(::std::make_pair(ns->idx, ContentLoopVariableUse(ns->loops)));
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
                                auto name = tok.type() == TOK_IDENT ? tok.ident().name : RcString::newInterned(tok.toStr());
                                const auto* ns = state.findName(name);
                                if (!ns) {
                                    TODO(lex.pointSpan(), "concat - unmapped name");
                                } else {
                                    DEBUG("CONCAT $" << name << " #" << ns->idx << " [" << ns->loops << "]");

                                    // If the current loop depth is smaller than the stack for this variable, then error
                                    if (loopDepth < ns->loops.size()) {
                                        ERROR(lex.pointSpan(), E0000, "Variable $" << name << " is still repeating at this depth (" << loopDepth << " < " << ns->loops.size() << ")");
                                    }

                                    if (varUsagePtr) {
                                        varUsagePtr->insert(::std::make_pair(ns->idx, ContentLoopVariableUse(ns->loops)));
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
                    TODO(lex.pointSpan(), "Handle ${" << ident << "...}");
                }
                lex.getTokenCheck(TOK_BRACE_CLOSE);
            } else if (tok.type() == TOK_RWORD_CRATE) {
                ret.push_back(MacroExpansionEnt(NAMEDVALUE_MAGIC_CRATE));
            } else if (tok.type() == TOK_IDENT || Token::typeIsRword(tok.type())) {
                // Look up the named parameter in the list of param names for this arm
                auto name = tok.type() == TOK_IDENT ? tok.ident().name : RcString::newInterned(tok.toStr());
                const auto* ns = state.findName(name);
                if (!ns) {
                    // NOTE: `error-chain`'s quick_error macro has an arm which refers to an undefined metavar.
                    // - Would emit a warning and use a marker index, but that's FAR too noisy

                    // Emit the literal $ <name>
                    ret.push_back(MacroExpansionEnt(Token(TOK_DOLLAR)));
                    ret.push_back(MacroExpansionEnt(mv$(tok)));
                } else {
                    DEBUG("$" << name << " #" << ns->idx << " [" << ns->loops << "]");

                    // If the current loop depth is smaller than the stack for this variable, then error
                    if (loopDepth < ns->loops.size()) {
                        ERROR(lex.pointSpan(), E0000, "Variable $" << name << " is still repeating at this depth (" << loopDepth << " < " << ns->loops.size() << ")");
                    }

                    if (varUsagePtr) {
                        varUsagePtr->insert(::std::make_pair(ns->idx, ContentLoopVariableUse(ns->loops)));
                    }
                    ret.push_back(MacroExpansionEnt(ns->idx));
                }
            } else if (tok.type() == TOK_PAREN_CLOSE || tok.type() == TOK_SQUARE_CLOSE || tok.type() == TOK_BRACE_CLOSE) {
                PUTBACK(tok, lex);
                ret.push_back(MacroExpansionEnt(Token(TOK_DOLLAR)));
            } else {
                // Expected reserved word, ident, or `(`
                throw ParseErrorUnexpected(lex, tok);
            }
        } else {
            ret.push_back(MacroExpansionEnt(mv$(tok)));
        }
    }

    return ret;
}

/// Parse a single arm of a macro_rules! block - `(foo) => (bar)`
MacroRule ParseMacroRulesVar(TokenStream& lex) {
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
            throw ParseErrorUnexpected(lex, tok);
    }
    // - Pattern entries
    RuleParseState state;
    {
        auto ps = lex.startSpan();
        rule.pattern = ParseMacroRulesPat(lex, tok.type(), close, state);
        rule.patSpan = lex.endSpan(ps);
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
            throw ParseErrorUnexpected(lex, tok);
    }
    rule.contents = ParseMacroRulesCont(lex, tok.type(), close, state);

    DEBUG("Rule - [" << rule.pattern << "] => " << rule.contents << "");

    return rule;
}

// TODO: Also count the number of times each variable is used?
void enumerateNames(const ::std::vector<MacroPatEnt>& pats, ::std::vector<RcString>& names) {
    for (const auto& pat : pats) {
        if (pat.type == MacroPatEnt::PAT_LOOP) {
            enumerateNames(pat.subpats, names);
        } else if (pat.name != "") {
            auto b = names.begin();
            auto e = names.end();
            if (::std::find(b, e, pat.name) == e) {
                names.push_back(pat.name);
            }
        }
    }
}

MacroRulesArm ParseMacroRulesMakeArm(Span patSp, ::std::vector<MacroPatEnt> pattern, ::std::vector<MacroExpansionEnt> contents) {
    // - Convert the rule into an instruction stream
    auto ruleSequence = macroPatternToSimple(patSp, pattern);
    auto arm = MacroRulesArm(mv$(ruleSequence), mv$(contents));
    enumerateNames(pattern, arm.paramNames);
    return arm;
}

namespace {
    MacroRulesPtr makeMrPtr(const TokenStream& lex) {
        auto s = lex.pointSpan();
        auto rv = MacroRulesPtr(new MacroRules(s->crateName(), lex.getEdition()));
        rv->mHygiene = lex.getHygiene();
        return rv;
    }
}

/// Parse an entire macro_rules! block into a format that exec.cpp can use
MacroRulesPtr ParseMacroRules(TokenStream& lex) {
    TRACE_FUNCTION_F("");

    Token tok;

    // Parse the patterns and replacements
    ::std::vector<MacroRule> rules;
    while (lex.lookahead(0) != TOK_EOF && lex.lookahead(0) != TOK_BRACE_CLOSE) {
        rules.push_back(ParseMacroRulesVar(lex));
        GET_TOK(tok, lex);
        // LAZY: `macro` allows comma (not `macro_rules!`) but this is strictly more permissive than rustc
        if (tok.type() != TOK_SEMICOLON && tok.type() != TOK_COMMA) {
            PUTBACK(tok, lex);
            break;
        }
    }
    GET_TOK(tok, lex);
    if (tok.type() != TOK_EOF && tok.type() != TOK_BRACE_CLOSE) {
        throw ParseErrorUnexpected(lex, tok, {TOK_EOF, TOK_BRACE_CLOSE});
    }
    DEBUG("- " << rules.size() << " rules");

    auto rv = makeMrPtr(lex);
    // Re-parse the patterns into a unified form
    for (auto& rule : rules) {
        rv->rules.push_back(ParseMacroRulesMakeArm(rule.patSpan, mv$(rule.pattern), mv$(rule.contents)));
    }

    return rv;
}

MacroRulesPtr ParseMacroRulesSingleArm(TokenStream& lex) {
    TRACE_FUNCTION_F("");
    Token tok;

    RuleParseState state;

    auto ps = lex.startSpan();
    GET_CHECK_TOK(tok, lex, TOK_PAREN_OPEN);
    auto armPat = ParseMacroRulesPat(lex, TOK_PAREN_OPEN, TOK_PAREN_CLOSE, state);
    auto patSpan = lex.endSpan(ps);
    GET_CHECK_TOK(tok, lex, TOK_BRACE_OPEN);
    // TODO: Pass a flag that annotates all idents with the current module?
    auto body = ParseMacroRulesCont(lex, TOK_BRACE_OPEN, TOK_BRACE_CLOSE, state);

    auto rv = makeMrPtr(lex);
    rv->rules.push_back(ParseMacroRulesMakeArm(patSpan, ::std::move(armPat), ::std::move(body)));
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
    PatternHeadRv macroPatternGetHeadSetInner(::std::vector<ExpTok>& rv, const ::std::vector<MacroPatEnt>& pattern, size_t directPos, const std::vector<ExpTok>& indirectPath, size_t indirectOfs) {
        TRACE_FUNCTION_F(&pattern << " #" << directPos << " [" << indirectPath << "]+" << indirectOfs);
        for (size_t idx = directPos; idx < pattern.size(); idx++) {
            const auto& ent = pattern[idx];
            DEBUG(idx << " " << ent);
            switch (ent.type) {
                case MacroPatEnt::PAT_LOOP:
                    switch (macroPatternGetHeadSetInner(rv, ent.subpats, 0, indirectPath, indirectOfs)) {
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
                            indirectOfs += ent.subpats.size();

                            // If the inner pattern didn't close the option set, then the next token can be the separator
                            if (ent.tok != TOK_NULL) {
                                // If indirect is non-zero, decrement without doing anything
                                if (indirectOfs < indirectPath.size()) {
                                    if (indirectPath[indirectOfs] != ExpTok(MacroPatEnt::PAT_TOKEN, &ent.tok)) {
                                        return PatternHeadRv::InvalidPath;
                                    }
                                    indirectOfs++;

                                    // If this is a loop (and not just an optional), attempt to repeat it
                                    if (ent.name != "?") {
                                        assert(ent.subpats.size() > 0);
                                        macroPatternGetHeadSetInner(rv, ent.subpats, 0, indirectPath, indirectOfs + ent.subpats.size());
                                    }
                                } else {
                                    rv.push_back(ExpTok(MacroPatEnt::PAT_TOKEN, &ent.tok));
                                    // Don't close the set yet, could be skipped
                                }
                            } else {
                                // If this is a loop (and not just an optional), attempt to repeat it
                                if (ent.name != "?") {
                                    assert(ent.subpats.size() > 0);
                                    macroPatternGetHeadSetInner(rv, ent.subpats, 0, indirectPath, indirectOfs + ent.subpats.size());
                                }
                            }
                            break;
                    }
                    break;
                default:
                    if (indirectOfs < indirectPath.size()) {
                        DEBUG("IP" << indirectOfs << " " << indirectPath[indirectOfs]);
                        if (indirectPath[indirectOfs] != ExpTok(ent.type, &ent.tok)) {
                            return PatternHeadRv::InvalidPath;
                        }
                        indirectOfs++;
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

    ::std::vector<ExpTok> macroPatternGetHeadSet(const ::std::vector<MacroPatEnt>& pattern, size_t directPos, const std::vector<ExpTok>& indirectPath) {
        ::std::vector<ExpTok> rv;
        TRACE_FUNCTION_FR("", rv);
        // If the pattern set isn't closed (hit something unconditional), then add `EOF` to it
        if (macroPatternGetHeadSetInner(rv, pattern, directPos, indirectPath, 0) != PatternHeadRv::Closed) {
            //if(rv.empty())
            if (!::std::any_of(rv.begin(), rv.end(), [](const ExpTok& e) {
                return e.ty == MacroPatEnt::PAT_TOKEN && *e.tok == TOK_EOF;
            })) {
                static Token tokEof = TOK_EOF;
                rv.push_back(ExpTok(MacroPatEnt::PAT_TOKEN, &tokEof));
            }
        }
        return rv;
    }

    void macroPatternToSimpleInner(const Span& sp, ::std::vector<SimplePatEnt>& rv, const ::std::vector<MacroPatEnt>& pattern) {
        size_t levelStart = rv.size();
        TRACE_FUNCTION_FR("[" << pattern << "]", "[" << FMT_CB(ss, for (auto it = rv.begin() + levelStart; it != rv.end(); ++it) { ss << *it << ", "; }) << "]");
        auto push = [&rv](SimplePatEnt spe) {
            DEBUG("[macro_pattern_to_simple_inner] rv[" << rv.size() << "] = " << spe);
            rv.push_back(::std::move(spe));
        };
        auto pushIfv = [&push](bool isEqual, ::std::vector<SimplePatIfCheck> ents, size_t tgt) {
            push(SimplePatEnt::make_If({isEqual, tgt, mv$(ents)}));
        };
        for (size_t idx = 0; idx < pattern.size(); idx++) {
            const auto& ent = pattern[idx];
            DEBUG("[" << idx << "] ent = " << ent);
            switch (ent.type) {
                case MacroPatEnt::PAT_LOOP: {
                    auto entryPats1 = macroPatternGetHeadSet(ent.subpats, 0, {});
                    DEBUG("Entry = [" << entryPats1 << "]");
                    ASSERT_BUG(ent.sp, entryPats1.size() > 0, "No entry conditions extracted from sub-pattern [" << ent.subpats << "]");
                    auto skipPats1 = macroPatternGetHeadSet(pattern, idx + 1, {});
                    DEBUG("Skip = [" << skipPats1 << "]");

                    // TODO: If EOF is in both entry and skip, then remove from entry
                    bool bodySkippable = false;
                    if (::std::find(entryPats1.begin(), entryPats1.end(), TOK_EOF) != entryPats1.end()) {
                        if (::std::find(skipPats1.begin(), skipPats1.end(), TOK_EOF) != entryPats1.end()) {
                            entryPats1.erase(::std::find(entryPats1.begin(), entryPats1.end(), TOK_EOF));
                            bodySkippable = true;
                        }
                    }

                    std::vector<std::vector<SimplePatIfCheck>> entryConds;
                    std::vector<std::vector<SimplePatIfCheck>> skipConds;
                    std::vector<std::vector<SimplePatIfCheck>> repeatConds;

                    for (const auto& ee : entryPats1) {
                        entryConds.push_back(::makeVec1<SimplePatIfCheck>({ee.ty, *ee.tok}));
                    }
                    for (const auto& ee : skipPats1) {
                        skipConds.push_back(::makeVec1<SimplePatIfCheck>({ee.ty, *ee.tok}));
                    }

                    // - Duplicates need special handling (build up a subseqent set)
                    const size_t MAX_CONDITION_ADD = 2;
                    for (size_t iterations = 0; iterations < MAX_CONDITION_ADD; iterations++) {
                        bool didExtend = false;
                        for (auto eIt = entryConds.begin(); eIt != entryConds.end(); ++eIt) {
                            auto sIt = ::std::find(skipConds.begin(), skipConds.end(), *eIt);
                            if (sIt != skipConds.end()) {
                                didExtend = true;
                                DEBUG("Entry condition is also in skip condition: " << *eIt);

                                std::vector<ExpTok> path;
                                for (auto it = eIt->begin(); it != eIt->end(); ++it) {
                                    path.push_back(ExpTok(it->ty, &it->tok));
                                }
                                auto entryPats2 = macroPatternGetHeadSet(ent.subpats, 0, path);
                                assert(entryPats2.size() > 0);
                                // Replace `TOK_EOF` in entry patterns with the first skip pattern
                                if (::std::find(entryPats2.begin(), entryPats2.end(), TOK_EOF) != entryPats2.end()) {
                                    entryPats2.erase(::std::find(entryPats2.begin(), entryPats2.end(), TOK_EOF));
                                    entryPats2.insert(entryPats2.end(), skipPats1.begin(), skipPats1.end());
                                }
                                auto skipPats2 = macroPatternGetHeadSet(pattern, idx + 1, path);
                                assert(skipPats2.size() > 0);
                                DEBUG("entry_pats2 = [" << entryPats2 << "]");
                                DEBUG("skip_pats2 = [" << skipPats2 << "]");
                                // Update the current element for both of them, and add new elements to the end of each list
                                {
                                    auto e2It = entryPats2.begin();
                                    eIt->push_back({e2It->ty, *e2It->tok});
                                    for (++e2It; e2It != entryPats2.end(); ++e2It) {
                                        eIt = entryConds.insert(eIt, *eIt);
                                        eIt->back() = SimplePatIfCheck{e2It->ty, *e2It->tok};
                                    }
                                }

                                {
                                    auto s2It = skipPats2.begin();
                                    sIt->push_back({s2It->ty, *s2It->tok});
                                    for (++s2It; s2It != skipPats2.end(); ++s2It) {
                                        sIt = skipConds.insert(sIt, *sIt);
                                        sIt->back() = SimplePatIfCheck{s2It->ty, *s2It->tok};
                                    }
                                }
                            }
                        }
                        // TODO: If any end with `:vis` then extend
                        if (!didExtend) {
                            break;
                        }
                    }
                    // - If there's three-level needed, error?
                    for (auto eIt = entryConds.begin(); eIt != entryConds.end(); ++eIt) {
                        auto sIt = ::std::find(skipConds.begin(), skipConds.end(), *eIt);
                        if (sIt != skipConds.end()) {
                            TODO(ent.sp, "Entry and skip patterns share an entry (max extend " << MAX_CONDITION_ADD << "), ambigious - " << *eIt);
                        }
                    }
                    for (const auto& e : entryConds) {
                        DEBUG("Entry += [" << e << "]");
                    }
                    for (const auto& e : skipConds) {
                        DEBUG("Skip += [" << e << "]");
                    }

                    // - Generate the repeat condition set
                    if (ent.tok != TOK_NULL) {
                        // NOTE: If the separator is also allowed after the list, then this can't just check for the separator
                        for (const auto& p : entryConds) {
                            auto v = ::makeVec1<SimplePatIfCheck>({MacroPatEnt::PAT_TOKEN, ent.tok});
                            v.insert(v.end(), p.begin(), p.end());
                            repeatConds.push_back(mv$(v));
                        }
                        // TODO: If entry indicates that it's optional (it had TOK_EOF in it) then push the skip too
                        if (bodySkippable) {
                            for (const auto& p : skipConds) {
                                auto v = ::makeVec1<SimplePatIfCheck>({MacroPatEnt::PAT_TOKEN, ent.tok});
                                v.insert(v.end(), p.begin(), p.end());
                                repeatConds.push_back(mv$(v));
                            }
                        }
                    }
                    DEBUG("Repeat = [");
                    for (const auto& e : repeatConds) {
                        DEBUG(" [" << e << "]");
                    }
                    DEBUG("]");

                    // TODO: Combine the two cases below into one?

                    // If the loop is a $()+ loop, then just recurse into it
                    if (ent.name == "+") {
                        push(SimplePatEnt::make_LoopStart({ent.nameIndex}));
                        size_t start = rv.size();
                        macroPatternToSimpleInner(sp, rv, ent.subpats);
                        push(SimplePatEnt::make_LoopNext({/*ent.name_index*/}));
                        size_t rewriteStart = rv.size();
                        if (ent.tok != TOK_NULL) {
                            if (repeatConds.size() > 1) {
                                DEBUG("Loop+ Multi-option repeat");
                                size_t expectAndJumpPos = rv.size() + repeatConds.size() + 1;
                                for (const auto& ee : repeatConds) {
                                    pushIfv(true, ee, expectAndJumpPos);
                                }
                                // If any of the above succeeded, they'll jump past this jump to the ExpectTok
                                push(SimplePatEnt::make_Jump({~0u}));
                            } else {
                                DEBUG("Loop+ Single-option repeat");
                                pushIfv(false, repeatConds.front(), ~0u);
                            }
                            push(SimplePatEnt::make_ExpectTok(ent.tok));
                            push(SimplePatEnt::make_Jump({start}));
                        } else {
                            // TODO: What if there's a collision at this level?
                            for (const auto& p : entryConds) {
                                pushIfv(true, p, start);
                            }
                        }

                        size_t postLoop = rv.size();
                        for (size_t i = rewriteStart; i < postLoop; i++) {
                            if (auto* pe = rv[i].opt_If()) {
                                if (pe->jumpTarget == ~0u) {
                                    pe->jumpTarget = postLoop;
                                }
                            }
                            if (auto* pe = rv[i].opt_Jump()) {
                                if (pe->jumpTarget == ~0u) {
                                    pe->jumpTarget = postLoop;
                                }
                            }
                        }
                        push(SimplePatEnt::make_LoopEnd({/*ent.name_index*/}));
                    } else if (ent.name == "*" || ent.name == "?") {
                        push(SimplePatEnt::make_LoopStart({ent.nameIndex}));

                        // Options:
                        // - Enter the loop (if the next token is one of the head set of the loop)
                        // - Skip the loop (the next token is the head set of the subsequent entries)
                        size_t rewriteStart = rv.size();
                        if (entryConds.size() == 1 && entryConds[0].back().tok != TOK_EOF) // HACK: if the entry ends with `EOF` then it won't be correct
                        {
                            // If not the entry pattern, skip.
                            pushIfv(false, entryConds.front(), ~0u);
                        } else if (skipConds.empty()) {
                            // No skip patterns, try all entry patterns
                            size_t start = rv.size() + entryConds.size() + 1;
                            for (const auto& p : entryConds) {
                                pushIfv(true, p, start);
                            }
                            push(SimplePatEnt::make_Jump({~0u}));
                        } else {
                            for (const auto& p : skipConds) {
                                pushIfv(true, p, ~0u);
                            }
                        }

                        macroPatternToSimpleInner(sp, rv, ent.subpats);
                        push(SimplePatEnt::make_LoopNext({/*ent.name_index*/}));

                        if (ent.name == "*") {
                            if (ent.tok != TOK_NULL) {
                                if (repeatConds.size() == 1) {
                                    DEBUG("Loop* - Single-option repeat");
                                    // If not a repeat, jump out
                                    for (const auto& ee : repeatConds) {
                                        pushIfv(/*is_equal*/ false, ee, ~0u);
                                    }
                                    push(SimplePatEnt::make_ExpectTok(ent.tok));
                                } else {
                                    DEBUG("Loop* - Multi-option repeat");
                                    // Multiple repeat conditions
                                    // - If any repeat condition matches, then jump to a consume
                                    auto checkPos = rv.size() + repeatConds.size() + 1;
                                    for (const auto& ee : repeatConds) {
                                        pushIfv(/*is_equal*/ true, ee, checkPos);
                                    }
                                    // - If none of the above matched, then jump out of the loop
                                    push(SimplePatEnt::make_Jump({~0u}));
                                    assert(rv.size() == checkPos);
                                    push(SimplePatEnt::make_ExpectTok(ent.tok));
                                }
                            }
                            // Jump back to the entry check.
                            push(SimplePatEnt::make_Jump({rewriteStart}));
                        } else if (ent.name == "?") {
                            ASSERT_BUG(sp, ent.tok == TOK_NULL, "$()? with a separator isn't valid");
                        } else {
                            BUG(sp, "");
                        }
                        size_t postLoop = rv.size();
                        for (size_t i = rewriteStart; i < postLoop; i++) {
                            if (auto* pe = rv[i].opt_If()) {
                                if (pe->jumpTarget == ~0u) {
                                    pe->jumpTarget = postLoop;
                                }
                            }
                            if (auto* pe = rv[i].opt_Jump()) {
                                if (pe->jumpTarget == ~0u) {
                                    pe->jumpTarget = postLoop;
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
                    push(SimplePatEnt::make_ExpectPat({ent.type, ent.nameIndex}));
                    break;
            }
        }

        for (size_t i = levelStart; i < rv.size(); i++) {
            TU_MATCH_HDRA( (rv[i]), { )
            default:
                // Ignore
            TU_ARMA(If, e) {
                    ASSERT_BUG(sp, e.jumpTarget < rv.size(), "If target out of bounds, " << e.jumpTarget << " >= " << rv.size());
                }
                TU_ARMA(Jump, e) {
                    ASSERT_BUG(sp, e.jumpTarget < rv.size(), "Jump target out of bounds, " << e.jumpTarget << " >= " << rv.size());
                }
            }
        }
    }

    ::std::vector<SimplePatEnt> macroPatternToSimple(const Span& sp, const ::std::vector<MacroPatEnt>& pattern) {
        ::std::vector<SimplePatEnt> rv;
        TRACE_FUNCTION_FR(pattern, rv);
        macroPatternToSimpleInner(sp, rv, pattern);
        return rv;
    }
}

MacroPatEnt::MacroPatEnt()
    : tok(TOK_NULL)
    , type(PAT_TOKEN)
{
}

// Literal token
MacroPatEnt::MacroPatEnt(Span sp, Token tok)
    : sp(mv$(sp))
    , tok(mv$(tok))
    , type(PAT_TOKEN)
{
}

// Variable reference
MacroPatEnt::MacroPatEnt(Span sp, RcString name, unsigned int nameIndex, Type type)
    : sp(mv$(sp))
    , name(mv$(name))
    , nameIndex(nameIndex)
    , tok()
    , type(type)
{
}

// Loop/optional
MacroPatEnt::MacroPatEnt(Span sp, Token sep, const char* op, unsigned index, ::std::vector<MacroPatEnt> ents)
    : sp(mv$(sp))
    , name(op)
    , nameIndex(index)
    , tok(mv$(sep))
    , subpats(mv$(ents))
    , type(PAT_LOOP)
{
}

MacroRulesArm::MacroRulesArm() {
}

MacroRulesArm::MacroRulesArm(::std::vector<SimplePatEnt> pattern, ::std::vector<MacroExpansionEnt> contents)
    : pattern(mv$(pattern))
    , contents(mv$(contents))
{
}

MacroRules::MacroRules(RcString sourceCrate, ASTEdition edition)
    : definitionId(++gNextDefinitionId)
    , sourceCrate(std::move(sourceCrate))
    , edition(edition)
{
}

::std::ostream& operator<<(::std::ostream& os, const SimplePatIfCheck& x) {
    os << x.ty;
    if (x.ty == MacroPatEnt::PAT_TOKEN) {
        os << ":" << x.tok;
    }
    return os;
}
