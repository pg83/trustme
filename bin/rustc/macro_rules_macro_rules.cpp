#include "macro_rules_macro_rules.h"
#include "output.h"
#include "macro_rules_macro_rules.h"

#include "common.h"
#include "hir_hir.h"
#include "ast_expr.h"
#include "ast_crate.h"
#include "wire_board.h"
#include "parse_common.h"
#include "parse_ttstream.h"
#include "parse_tokentree.h"
#include "parse_parseerror.h"
#include "macro_rules_pattern_checks.h"
#include "parse_interpolated_fragment.h"

#include <limits.h>
#include <sstream>

using namespace stl;

namespace {
    typedef std::map<unsigned, std::map<std::vector<unsigned>, unsigned>> loopCountsT;

    struct CapturedVal {
        unsigned int numUses;
        unsigned int numUsed;
        InterpolatedFragment frag;
    };

#include "macro_rules_capture_tu.h"

    inline ZeroCopyOutput& operator<<(ZeroCopyOutput& os, const CapturedVal& x);
    inline ZeroCopyOutput& operator<<(ZeroCopyOutput& os, const CaptureLayer& x);

    struct ParameterMappings {
        struct CapturedVar {
            CaptureLayer topLayer;

            friend ZeroCopyOutput& operator<<(ZeroCopyOutput& os, const CapturedVar& x) {
                os << StringView("CapturedVar { ") << x.topLayer << StringView(" }");
                return os;
            }
        };

        loopCountsT loopCounts;

        std::vector<CapturedVar> mappings_;
        unsigned layerCount_;

        ParameterMappings();

        ParameterMappings(ParameterMappings&&) = default;

        const std::vector<CapturedVar>& mappings() const;

        size_t layerCount() const;

        void setLoopCounts(loopCountsT loopCounts);

        void insert(unsigned int nameIndex, const std::vector<unsigned int>& iterations, InterpolatedFragment data);

        InterpolatedFragment* get(const Span& sp, const std::vector<unsigned int>& iterations, unsigned int nameIdx);

        unsigned int getLoopRepeats(const Span& sp, const std::vector<unsigned int>& iterations, unsigned int loopIdx) const;

        unsigned int getVariableCount(const Span& sp, const std::vector<unsigned int>& iterations, unsigned int nameIdx, unsigned int depth = 0) const;

        void incCount(const Span& sp, const std::vector<unsigned int>& iterations, unsigned int nameIdx);
        bool decCount(const Span& sp, const std::vector<unsigned int>& iterations, unsigned int nameIdx);

        CapturedVal& getCap(const Span& sp, const std::vector<unsigned int>& iterations, unsigned int nameIdx);
    };

    struct MacroPatternStream {
        const std::vector<SimplePatEnt>& simpleEnts;
        SimplePatEnt endEnt;
        size_t curPos_;

        bool lastWasCond;
        bool conditionMet;
        std::vector<bool> conditionHistory;

        const std::vector<bool>* conditionReplay;
        size_t conditionReplayPos;

        std::vector<unsigned int> currentLoops;
        std::vector<unsigned int> loopIterations;

        loopCountsT loopCounts;

        bool peekCacheValid = false;
        const SimplePatEnt* peekCache;

        MacroPatternStream(const std::vector<SimplePatEnt>& ents, const std::vector<bool>* conditionReplay = nullptr);

        size_t curPos() const;

        const SimplePatEnt& next();

        const SimplePatEnt& peek();

        void ifSucceeded();

        const std::vector<unsigned int>& getLoopIters() const;

        std::vector<bool> takeHistory();

        loopCountsT takeLoopCounts();
    };

    struct MacroExpandState {
        const std::vector<MacroExpansionEnt>& rootContents;
        const ParameterMappings& mappings_;

        struct tOffset {
            unsigned readPos;
            unsigned loopIndex;
            unsigned maxIndex;
        };

        std::vector<tOffset> offsets;
        std::vector<unsigned int> iterations_;

        const std::vector<MacroExpansionEnt>* curEnts;

        MacroExpandState(const std::vector<MacroExpansionEnt>& contents, const ParameterMappings& mappings);

        const MacroExpansionEnt* nextEnt();

        const std::vector<unsigned int> iterations() const;

        unsigned int loopIndexAt(unsigned int depth) const;

        unsigned int loopLengthAt(unsigned int depth) const;

        unsigned int topPos() const;

        const MacroExpansionEnt& getCurLayerEnt() const;
        const std::vector<MacroExpansionEnt>* getCurLayer() const;
    };

    struct MacroExpander: public TokenStream {
        Span thisSpan;
        const RcString crateName;
        Span invocationSpan;
        ASTEdition invocationEdition;

        ParameterMappings mappings_;
        MacroExpandState state;

        Token nextToken;
        std::unique_ptr<TTStreamO> ttstream;
        ASTEdition sourceEdition;
        bool isMacroItem;
        bool transparent;
        Ident::Hygiene hygiene_;
        Ident::Hygiene lastHygiene;

        ObjPool& pool;

        MacroExpander(const MacroExpander& x) = delete;

        MacroExpander(u32& id, ObjPool& pool, const RcString& macroName, const Span& sp, ASTEdition edition, bool isMacroItem, bool transparent, unsigned int definitionId, const Ident::Hygiene& parentHygiene, const std::vector<MacroExpansionEnt>& contents, ParameterMappings mappings, RcString crateName, ASTEdition sourceEdition);

        Position getPosition() const override;

        Span outerSpan() const override;

        Ident::Hygiene realGetHygiene() const override;
        ASTEdition realGetEdition() const override;
        Token realGetToken() override;
    };

    struct MacroRule {
        std::vector<MacroPatEnt> pattern;
        Span patSpan;
        std::vector<MacroExpansionEnt> contents;

        MacroRule();

        MacroRule(MacroRule&&) = default;
        MacroRule(const MacroRule&) = delete;
    };

    struct RuleParseState {
        struct NameState {
            unsigned idx;
            std::vector<unsigned> loops;
            Ident::Hygiene hygiene;
        };

        std::map<RcString, std::vector<NameState>> names;
        unsigned nextNameIndex;

        unsigned nextLoopIndex;
        std::vector<unsigned> loopStack;

        RuleParseState();

        unsigned addName(const Ident& ident);

        const NameState* findName(const Ident& ident) const;

        unsigned openLoop();

        void closeLoop();
    };

    struct ContentLoopVariableUse {
        std::vector<unsigned> loopStack;
        bool isOptional;

        ContentLoopVariableUse(std::vector<unsigned> loopStack);

        friend ZeroCopyOutput& operator<<(ZeroCopyOutput& os, const ContentLoopVariableUse& x) {
            return os << StringView("[") << x.loopStack << StringView("] ") << StringView(x.isOptional ? "optional" : "required");
        }
    };

    struct TokenStreamRO {
        const TokenTree& tt;
        std::vector<size_t> offsets;
        size_t activeOffset;

        Token eofToken;
        Token fakedNext;
        size_t consumeCount;

        TokenStreamRO(const TokenTree& tt);

        TokenStreamRO clone() const;

        enum eTokenType next() const;

        const Token& nextTok() const;

        void consume();

        void consumeAndPush(eTokenType ty);

        bool consumeIf(eTokenType ty);

        size_t position() const;
    };

    struct ExpTok {
        MacroPatEnt::Type ty;
        Token tok;

        ExpTok(MacroPatEnt::Type ty, const Token& tok);

        bool operator==(const ExpTok& t) const;

        bool operator!=(const ExpTok& t) const;

        bool operator==(eTokenType tt) const;
    };

    inline ZeroCopyOutput& operator<<(ZeroCopyOutput& os, const CapturedVal& x) {
        os << x.frag;
        return os;
    }

    inline ZeroCopyOutput& operator<<(ZeroCopyOutput& os, const CaptureLayer& x) {
        switch (x.tag()) {
            case CaptureLayer::TAG_Vals: {
                auto& e = x.as_Vals();
                os << StringView("[") << e << StringView("]");
                break;
            }
            case CaptureLayer::TAG_Nested: {
                auto& e = x.as_Nested();
                os << StringView("{") << e << StringView("}");
                break;
            }
        }
        return os;
    }

    unsigned int MacroInvokeRulesMatchPattern(const Span& sp, const WireBoard& wb, const MacroRules& rules, TokenTree input, const ASTCrate& crate, ASTModule& mod, ParameterMappings& boundTts);
    void MacroInvokeRulesCountSubstUses(ParameterMappings& boundTts, const std::vector<MacroExpansionEnt>& contents);

    template <typename Layer>
    unsigned int captureLayerDepth(const Layer& layer) {
        unsigned int rv = 1;
        if (const auto* nested = layer.opt_Nested()) {
            unsigned int deepest = 0;
            for (const auto& child : *nested) {
                deepest = std::max(deepest, captureLayerDepth(child));
            }
            rv += deepest;
        }
        return rv;
    }

    template <typename Layer>
    unsigned int captureLayerNodesAt(const Layer& layer, unsigned int level) {
        if (const auto* vals = layer.opt_Vals()) {
            return level == 1 ? static_cast<unsigned int>(vals->size()) : 0;
        }
        const auto& nested = layer.as_Nested();
        if (level == 1) {
            return static_cast<unsigned int>(nested.size());
        }
        unsigned int rv = 0;
        for (const auto& child : nested) {
            rv += captureLayerNodesAt(child, level - 1);
        }
        return rv;
    }

    void MacroInitDefaults() {
    }

    InterpolatedFragment MacroHandlePatternCap(TokenStream& lex, MacroPatEnt::Type type, bool stmtIsItem) {
        Token tok;
        switch (type) {
            case MacroPatEnt::PAT_TOKEN:
                BUG(lex.pointSpan(), StringView("Encountered PAT_TOKEN when handling capture"));
            case MacroPatEnt::PAT_LOOP:
                BUG(lex.pointSpan(), StringView("Encountered PAT_LOOP when handling capture"));

            case MacroPatEnt::PAT_TT:
                if (GET_TOK(tok, lex) == TOK_EOF) {
                    parseErrorUnexpected(lex, TOK_EOF);
                } else {
                    PUTBACK(tok, lex);
                }
                return InterpolatedFragment(ParseTT(lex, false));
            case MacroPatEnt::PAT_PAT:
                return InterpolatedFragment(ParsePattern(lex, AllowOrPattern::Yes));
            case MacroPatEnt::PAT_PAT_PARAM:
                return InterpolatedFragment(ParsePattern(lex, AllowOrPattern::No));
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
                    BUG_ASSERT(lex.parseState().module);
                    const auto& curMod = *lex.parseState().module;
                    return InterpolatedFragment(InterpolatedFragment::STMT_ITEM, ParseModItemS(lex, curMod.fileInfo, curMod.path(), ASTAttributeList{}));
                }
                {
                    auto attrs = ParseItemAttrs(lex);
                    auto stmt = ParseStmt(lex);
                    stmt->setAttrs(mv$(attrs));
                    return InterpolatedFragment(InterpolatedFragment::STMT, stmt.release());
                }
            case MacroPatEnt::PAT_PATH:
                // HACK for `rustc-1.90.0-src/vendor/icu_locid_transform_data-1.5.0/data/macros.rs::23`
                if (lex.lookahead(0) == TOK_INTERPOLATED_TYPE) {
                    return InterpolatedFragment(std::move(lex.getToken().fragType()));
                }
                GET_TOK(tok, lex);
                {
                    auto span = lex.tokenStartSpan(tok);
                    PUTBACK(tok, lex);
                    return InterpolatedFragment(ParsePath(lex, PATH_GENERIC_TYPE), std::move(span));
                }
            case MacroPatEnt::PAT_BLOCK:
                return InterpolatedFragment(InterpolatedFragment::BLOCK, ParseExprBlockNode(lex).release());
            case MacroPatEnt::PAT_META:
                return InterpolatedFragment(ParseMetaItem(lex));
            case MacroPatEnt::PAT_ITEM: {
                BUG_ASSERT(lex.parseState().module);
                const auto& curMod = *lex.parseState().module;
                return InterpolatedFragment(ParseModItemS(lex, curMod.fileInfo, curMod.path(), ASTAttributeList{}));
            } break;
            case MacroPatEnt::PAT_IDENT:
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
                            parseErrorUnexpected(lex, tok, {TOK_INTEGER, TOK_FLOAT});
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
                    case TOK_LITERAL_SUFFIXED:
                    case TOK_RWORD_TRUE:
                    case TOK_RWORD_FALSE:
                        break;
                    default:
                        parseErrorUnexpected(lex, tok, {TOK_INTEGER, TOK_FLOAT, TOK_STRING, TOK_BYTESTRING, TOK_CSTRING, TOK_RWORD_TRUE, TOK_RWORD_FALSE});
                }
                return InterpolatedFragment(TokenTree(lex.getEdition(), lex.getHygiene(), tok));
        }
        UNREACHABLE();
    }

    bool consumeType(TokenStreamRO& lex);
    enum class ItemConsumeMode {
        ItemFragment,
        StatementFragment,
    };
    bool consumeItem(TokenStreamRO& lex, ItemConsumeMode mode = ItemConsumeMode::ItemFragment);

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

        // TODO: Can expressions show up on this context?
        lex.consume();
        for (;;) {
            if (lex.next() == TOK_LT || lex.next() == TOK_DOUBLE_LT) {
                level += (lex.next() == TOK_DOUBLE_LT ? 2 : 1);
            } else if (lex.next() == TOK_DOUBLE_GT_EQUAL) {
                BUG_ASSERT(level > 0);
                if (level == 1) {
                    lex.consumeAndPush(TOK_GTE);
                    return true;
                }
                level -= 2;
                lex.consumeAndPush(TOK_EQUAL);
                if (level == 0) {
                    return true;
                }
            } else if (lex.next() == TOK_GT || lex.next() == TOK_DOUBLE_GT) {
                BUG_ASSERT(level > 0);
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

            if (lex.next() == TOK_PAREN_OPEN || lex.next() == TOK_SQUARE_OPEN || lex.next() == TOK_BRACE_OPEN) {
                consumeTt(lex);
            } else {
                lex.consume();
            }
        }
        lex.consume();
        return true;
    }

    bool consumePath(TokenStreamRO& lex, bool typeMode = false) {
        TRACE_FUNCTION;
        switch (lex.next()) {
            case TOK_INTERPOLATED_PATH:
            case TOK_INTERPOLATED_TYPE: // HACK!
                lex.consume();
                return true;
            case TOK_RWORD_SELF:
                lex.consume();
                if (lex.next() != TOK_DOUBLE_COLON) {
                    return true;
                }
                break;
            case TOK_RWORD_CRATE:
                lex.consume();
                break;
            case TOK_RWORD_SUPER:
                lex.consume();
                if (lex.next() != TOK_DOUBLE_COLON) {
                    return true;
                }
                break;
            case TOK_DOUBLE_COLON:
                break;
            case TOK_IDENT:
                lex.consume();
                if (typeMode && (lex.next() == TOK_LT || lex.next() == TOK_DOUBLE_LT || lex.next() == TOK_PAREN_OPEN))
                    ;
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
            } else if (lex.next() == TOK_LT || lex.next() == TOK_DOUBLE_LT) {
                if (!consumeTtAngle(lex)) {
                    return false;
                }
            } else if (lex.next() == TOK_IDENT || lex.next() == TOK_RWORD_SELF || lex.next() == TOK_RWORD_SUPER || lex.next() == TOK_RWORD_CRATE) {
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
            if (lex.next() == TOK_INTERPOLATED_TYPE) {
                return false;
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
            case TOK_QMARK:
                lex.consume();
                return consumeTypeTraitList(lex);
            case TOK_RWORD_FOR:
                lex.consume();
                if (lex.next() != TOK_LT || !consumeTtAngle(lex)) {
                    return false;
                }
                return consumeType(lex);
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
                case TOK_LT:
                case TOK_DOUBLE_LT:
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
                case TOK_DASH:
                    lex.consume();
                    if (lex.next() != TOK_INTEGER && lex.next() != TOK_FLOAT) {
                        return false;
                    }
                    lex.consume();
                    break;
                case TOK_DOUBLE_DOT:
                case TOK_DOUBLE_DOT_EQUAL:
                    lex.consume();
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
                        case TOK_DASH:
                            lex.consume();
                            if (lex.next() != TOK_INTEGER && lex.next() != TOK_FLOAT) {
                                return false;
                            }
                            lex.consume();
                            break;
                        default:
                            break;
                    }
                    if (allowOr && lex.consumeIf(TOK_PIPE)) {
                        continue;
                    }
                    return true;
                default:
                    return false;
            }
            if (lex.consumeIf(TOK_AT)) {
                continue;
            }
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
                    case TOK_DASH:
                        lex.consume();
                        if (lex.next() != TOK_INTEGER && lex.next() != TOK_FLOAT) {
                            return false;
                        }
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

    bool consumeExpr(TokenStreamRO& lex, bool noStructLit = false, bool statementExpr = false) {
        TRACE_FUNCTION;
        bool cont;

        while (lex.next() == TOK_HASH) {
            lex.consume();
            lex.consumeIf(TOK_EXCLAM);
            consumeTt(lex);
        }

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
            bool hasPrefix = false;
            bool exprIsComplete = false;
            bool innerCont;
            do {
                innerCont = true;
                switch (lex.next()) {
                    case TOK_STAR:
                    case TOK_DASH:
                    case TOK_EXCLAM:
                    case TOK_RWORD_BOX:
                        lex.consume();
                        hasPrefix = true;
                        break;
                    case TOK_DOUBLE_AMP:
                    case TOK_AMP:
                        lex.consume();
                        if (lex.next() == TOK_IDENT && lex.nextTok().ident().name == "raw") {
                            lex.consume();
                            if (!lex.consumeIf(TOK_RWORD_CONST) && !lex.consumeIf(TOK_RWORD_MUT)) {
                                return false;
                            }
                        } else {
                            lex.consumeIf(TOK_RWORD_MUT);
                        }
                        hasPrefix = true;
                        break;
                    default:
                        innerCont = false;
                        break;
                }
            } while (innerCont);

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
                        }
                        exprIsComplete = !hasPrefix && lex.next() == TOK_BRACE_OPEN;
                        consumeTt(lex);
                    }
                    break;

                case TOK_INTERPOLATED_EXPR:
                    lex.consume();
                    break;
                case TOK_INTERPOLATED_BLOCK:
                    lex.consume();
                    exprIsComplete = !hasPrefix;
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

                case TOK_DOUBLE_DOT:
                case TOK_DOUBLE_DOT_EQUAL:
                case TOK_TRIPLE_DOT:
                    break;

                case TOK_RWORD_ASYNC:
                    lex.consume();
                    lex.consumeIf(TOK_RWORD_MOVE);
                    return consumeExpr(lex, noStructLit);

                case TOK_RWORD_CONST:
                    lex.consume();
                    if (lex.next() != TOK_BRACE_OPEN) {
                        return false;
                    }
                    consumeTt(lex);
                    exprIsComplete = !hasPrefix;
                    break;

                case TOK_RWORD_UNSAFE:
                case TOK_RWORD_TRY:
                    lex.consume();
                    if (lex.next() != TOK_BRACE_OPEN) {
                        return false;
                    }
                    consumeTt(lex);
                    exprIsComplete = !hasPrefix;
                    break;
                case TOK_LIFETIME:
                    lex.consume();
                    if (!lex.consumeIf(TOK_COLON)) {
                        return false;
                    }
                    switch (lex.next()) {
                        case TOK_RWORD_LOOP:
                        case TOK_RWORD_WHILE:
                        case TOK_RWORD_FOR:
                        case TOK_BRACE_OPEN:
                            break;
                        default:
                            return false;
                    }
                    return consumeExpr(lex, noStructLit, statementExpr);
                case TOK_PAREN_OPEN:
                case TOK_SQUARE_OPEN:
                    consumeTt(lex);
                    break;
                case TOK_BRACE_OPEN:
                    consumeTt(lex);
                    exprIsComplete = !hasPrefix;
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
                    exprIsComplete = !hasPrefix;
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
                    exprIsComplete = !hasPrefix;
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
                    exprIsComplete = !hasPrefix;
                    break;
                case TOK_RWORD_LOOP:
                    lex.consume();
                    if (lex.next() != TOK_BRACE_OPEN) {
                        return false;
                    }
                    consumeTt(lex);
                    exprIsComplete = !hasPrefix;
                    break;
                case TOK_RWORD_IF:
                    while (1) {
                        BUG_ASSERT(lex.next() == TOK_RWORD_IF);
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
                    exprIsComplete = !hasPrefix;
                    break;
                default:
                    return false;
            }

            do {
                innerCont = true;
                switch (lex.next()) {
                    case TOK_QMARK:
                        lex.consume();
                        exprIsComplete = false;
                        break;
                    case TOK_DOT:
                        lex.consume();
                        exprIsComplete = false;
                        if (lex.consumeIf(TOK_IDENT)) {
                            if (lex.consumeIf(TOK_DOUBLE_COLON)) {
                                if (!(lex.next() == TOK_LT || lex.next() == TOK_DOUBLE_LT)) {
                                    return false;
                                }
                                if (!consumeTtAngle(lex)) {
                                    return false;
                                }
                            }
                        } else if (lex.consumeIf(TOK_RWORD_AWAIT)) {
                        } else if (lex.consumeIf(TOK_INTEGER) || lex.consumeIf(TOK_FLOAT))
                            ;
                        else {
                            return false;
                        }
                        break;
                    case TOK_SQUARE_OPEN:
                    case TOK_PAREN_OPEN:
                        if (statementExpr && exprIsComplete) {
                            innerCont = false;
                            break;
                        }
                        consumeTt(lex);
                        exprIsComplete = false;
                        break;
                    default:
                        innerCont = false;
                        break;
                }
            } while (innerCont);

            if (statementExpr && exprIsComplete) {
                return true;
            }

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
                    DEBUG(StringView("TOK_DOUBLE_DOT => ") << lex.next());
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

        while (lex.next() == TOK_HASH) {
            lex.consume();
            if (lex.consumeIf(TOK_EXCLAM) || !consumeTt(lex)) {
                return false;
            }
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
            if (!consumeExpr(lex, false, true)) {
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
        if (lex.next() == TOK_INTERPOLATED_ITEM) {
            if (mode != ItemConsumeMode::ItemFragment) {
                return false;
            }
            lex.consume();
            return true;
        }

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
        if (!consumeVis(lex)) {
            return false;
        }
        if (lex.next() == TOK_RWORD_UNSAFE) {
            lex.consume();
        }
        DEBUG(StringView("Check item: ") << lex.nextTok());
        switch (lex.next()) {
            case TOK_RWORD_USE:
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
                    if (!lex.consumeIf(TOK_IDENT) && !lex.consumeIf(TOK_UNDERSCORE)) {
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
                    if (!lex.consumeIf(TOK_IDENT) && !lex.consumeIf(TOK_RWORD_SELF)) {
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
                BUG(Span(), StringView("Encountered ") << type << StringView(" in consume_from_frag"));
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
            case MacroPatEnt::PAT_PAT_PARAM:
                return consumePat(lex, false);
            case MacroPatEnt::PAT_META:
                if (lex.next() == TOK_INTERPOLATED_META) {
                    lex.consume();
                } else if (lex.next() == TOK_IDENT) {
                    lex.consume();
                    switch (lex.next()) {
                        case TOK_PAREN_OPEN:
                        case TOK_SQUARE_OPEN:
                        case TOK_BRACE_OPEN:
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
                    case TOK_LITERAL_SUFFIXED:
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

    unsigned int MacroInvokeRulesMatchPattern(const Span& sp, const WireBoard& wb, const MacroRules& rules, TokenTree input, const ASTCrate& crate, ASTModule& mod, ParameterMappings& boundTts) {
        TRACE_FUNCTION_F(rules.rules.size() << StringView(" options"));
        ASSERT_BUG(sp, rules.rules.size() > 0, StringView("Empty macro_rules set"));

        struct Match {
            size_t armIndex;
            std::vector<bool> conditionHistory;
            std::vector<bool> stmtIsItemHistory;
        };

        std::vector<Match> matches;
        std::vector<std::pair<size_t, eTokenType>> failPos;
        for (size_t i = 0; i < rules.rules.size(); i++) {
            auto lex = TokenStreamRO(input);
            auto armStream = MacroPatternStream(rules.rules[i].pattern);
            std::vector<bool> stmtIsItemHistory;

            bool fail = false;
            for (;;) {
                const auto pos = armStream.curPos();
                const auto& pat = armStream.next();
                DEBUG(StringView("Arm ") << i << StringView(" @") << pos << StringView(" ") << pat);
                if (pat.is_End()) {
                    if (lex.next() != TOK_EOF) {
                        DEBUG(StringView("Expeced EOF, got ") << lex.nextTok());
                        fail = true;
                    }
                    break;
                } else if (const auto* e = pat.opt_If()) {
                    auto lc = lex.clone();
                    bool rv = true;
                    for (const auto& check : e->ents) {
                        const bool atConditionStart = lc.position() == lex.position();
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
                        if (atConditionStart && check.isLocalAmbiguity()) {
                            ERROR(sp, E0000, StringView("local ambiguity when calling macro: multiple parsing options"));
                        }
                    }
                    if (rv == e->isEqual) {
                        DEBUG(StringView("- Succeeded"));
                        armStream.ifSucceeded();
                    }
                } else if (const auto* e = pat.opt_ExpectTok()) {
                    const auto& tok = lex.nextTok();
                    DEBUG(StringView("Arm ") << i << StringView(" @") << pos << StringView(" ExpectTok(") << *e << StringView(") == ") << tok);
                    if (tok != *e) {
                        fail = true;
                        break;
                    }
                    lex.consume();
                } else if (const auto* e = pat.opt_ExpectPat()) {
                    DEBUG(StringView("Arm ") << i << StringView(" @") << pos << StringView(" ExpectPat(") << e->type << StringView(" => $") << e->idx << StringView(")"));
                    bool stmtIsItem = false;
                    if (!consumeFromFrag(lex, e->type, &stmtIsItem)) {
                        fail = true;
                        break;
                    }
                    if (e->type == MacroPatEnt::PAT_STMT) {
                        stmtIsItemHistory.push_back(stmtIsItem);
                    }
                } else {
                }
            }

            if (!fail) {
                matches.push_back(Match{i, armStream.takeHistory(), mv$(stmtIsItemHistory)});
                DEBUG(i << StringView(" MATCHED"));
            } else {
                DEBUG(i << StringView(" FAILED"));
                failPos.push_back(std::make_pair(lex.position(), lex.next()));
            }
        }

        if (matches.size() == 0) {
            // TODO: Keep track of where each arm failed.
            TODO(sp, StringView("No arm matched - ") << failPos);
        } else {
            auto i = matches[0].armIndex;
            const auto& history = matches[0].conditionHistory;
            const auto& stmtIsItemHistory = matches[0].stmtIsItemHistory;

            DEBUG(StringView("Evalulating arm ") << i);
            auto lex = TTStreamO(sp, ParseState(), mv$(input));
            lex.parseState().crate = &crate;
            lex.parseState().wb = &wb;
            SET_MODULE(lex, mod);
            auto armStream = MacroPatternStream(rules.rules[i].pattern, &history);

            struct Capture {
                unsigned int bindingIdx;
                std::vector<unsigned int> iterations;
                unsigned int capIdx;
            };

            std::vector<InterpolatedFragment> captures;
            std::vector<Capture> captureInfo;
            size_t stmtCaptureIndex = 0;

            for (;;) {
                const auto& pat = armStream.next();
                DEBUG(i << StringView(" ") << pat);
                if (pat.is_End()) {
                    break;
                } else if (pat.is_If()) {
                    BUG(sp, StringView("Unexpected If pattern during final matching - ") << pat);
                } else if (const auto* e = pat.opt_ExpectTok()) {
                    auto tok = lex.getToken();
                    DEBUG(i << StringView(" ExpectTok(") << *e << StringView(") == ") << tok);
                    if (tok != *e) {
                        ERROR(sp, E0000, StringView("Expected token ") << *e << StringView(" in macro arm, got ") << tok);
                        break;
                    }
                } else if (const auto* e = pat.opt_ExpectPat()) {
                    DEBUG(i << StringView(" ExpectPat(") << e->type << StringView(" => $") << e->idx << StringView(")"));
                    bool stmtIsItem = false;
                    if (e->type == MacroPatEnt::PAT_STMT) {
                        ASSERT_BUG(sp, stmtCaptureIndex < stmtIsItemHistory.size(), StringView("Missing statement fragment classification"));
                        stmtIsItem = stmtIsItemHistory[stmtCaptureIndex++];
                    }
                    auto cap = MacroHandlePatternCap(lex, e->type, stmtIsItem);

                    unsigned int capIdx = captures.size();
                    captures.push_back(mv$(cap));
                    captureInfo.push_back(Capture{e->idx, armStream.getLoopIters(), capIdx});
                } else {
                }
            }
            ASSERT_BUG(sp, stmtCaptureIndex == stmtIsItemHistory.size(), StringView("Unused statement fragment classification"));

            for (const auto& cap : captureInfo) {
                boundTts.insert(cap.bindingIdx, cap.iterations, mv$(captures[cap.capIdx]));
            }
            boundTts.setLoopCounts(armStream.takeLoopCounts());
            return i;
        }
    }

    void MacroInvokeRulesCountSubstUses(ParameterMappings& boundTts, const std::vector<MacroExpansionEnt>& contents) {
        TRACE_FUNCTION;
        MacroExpandState state(contents, boundTts);

        while (const auto* entPtr = state.nextEnt()) {
            DEBUG(*entPtr);
            switch ((*entPtr).tag()) {
                case MacroExpansionEnt::TAG_Token: {
                    break;
                }
                case MacroExpansionEnt::TAG_Loop: {
                    break;
                }
                case MacroExpansionEnt::TAG_NamedValue: {
                    auto& e = (*entPtr).as_NamedValue();
                    switch (e & ~NAMEDVALUE_VALMASK) {
                        case 0:
                            boundTts.incCount(Span(), state.iterations(), e & NAMEDVALUE_VALMASK);
                            break;
                        case NAMEDVALUE_TY_IGNORE:
                        case NAMEDVALUE_TY_MAGIC:
                        default:
                            break;
                    }
                    break;
                }
                case MacroExpansionEnt::TAG_Concat: {
                    auto& ccEnts = (*entPtr).as_Concat();
                    for (const auto& ccEnt : ccEnts) {
                        switch (ccEnt.tag()) {
                            case MacroExpansionConcatEnt::TAG_Ident: {
                                break;
                            }
                            case MacroExpansionConcatEnt::TAG_Named: {
                                auto& e = ccEnt.as_Named();
                                switch (e & ~NAMEDVALUE_VALMASK) {
                                    case 0:
                                    case NAMEDVALUE_TY_IGNORE:
                                        boundTts.incCount(Span(), state.iterations(), e & NAMEDVALUE_VALMASK);
                                        break;
                                    case NAMEDVALUE_TY_MAGIC:
                                    default:
                                        break;
                                }
                                break;
                            }
                        }
                    }
                    break;
                }
            }
        }
    }

    std::vector<SimplePatEnt> macroPatternToSimple(const Span& sp, const std::vector<MacroPatEnt>& pattern);

    std::vector<MacroPatEnt> ParseMacroRulesPat(TokenStream& lex, enum eTokenType open, enum eTokenType close, RuleParseState& state) {
        TRACE_FUNCTION;
        Token tok;

        std::vector<MacroPatEnt> ret;

        int depth = 0;
        auto ps = lex.startSpan();
        while (GET_TOK(tok, lex) != close || depth > 0) {
            DEBUG(StringView("tok = ") << tok);
            if (tok.isDocComment()) {
                lex.getTokenIf(TOK_EXCLAM);
                GET_CHECK_TOK(tok, lex, TOK_SQUARE_OPEN);
                GET_CHECK_TOK(tok, lex, TOK_IDENT);
                GET_CHECK_TOK(tok, lex, TOK_EQUAL);
                GET_CHECK_TOK(tok, lex, TOK_STRING);
                GET_CHECK_TOK(tok, lex, TOK_SQUARE_CLOSE);
                continue;
            }
            if (tok.type() == open) {
                depth++;
            } else if (tok.type() == close) {
                if (depth == 0) {
                    compileErrorGeneric(FMT(StringView("Unmatched ") << Token(close) << StringView(" in macro pattern")).c_str());
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
                        case TOK_RWORD_CRATE:
                            parseErrorUnexpected(lex, tok);
                        default:
                            if (!Token::typeIsRword(tok.type())) {
                                parseErrorUnexpected(lex, tok);
                            }
                        case TOK_UNDERSCORE:
                        case TOK_IDENT: {
                            auto nameIdent = tok.type() == TOK_IDENT ? tok.ident() : Ident(tok.type() == TOK_UNDERSCORE ? RcString() : RcString::newInterned(tok.toStr()));
                            const auto& name = nameIdent.name;
                            GET_CHECK_TOK(tok, lex, TOK_COLON);
                            GET_CHECK_TOK(tok, lex, TOK_IDENT);
                            RcString type = tok.ident().name;

                            auto idx = state.addName(nameIdent);

                            auto sp = lex.endSpan(ps);
                            MacroPatEnt::Type ty;
                            if (0)
                                ;
                            else if (type == "tt") {
                                ty = MacroPatEnt::PAT_TT;
                            } else if (type == "pat") {
                                ty = lex.editionAfter(ASTEdition::Rust2021) ? MacroPatEnt::PAT_PAT : MacroPatEnt::PAT_PAT_PARAM;
                            } else if (type == "pat_param") {
                                ty = MacroPatEnt::PAT_PAT_PARAM;
                            } else if (type == "ident") {
                                ty = MacroPatEnt::PAT_IDENT;
                            } else if (type == "path") {
                                ty = MacroPatEnt::PAT_PATH;
                            } else if (type == "expr") {
                                ty = MacroPatEnt::PAT_EXPR;
                            } else if (type == "expr_2021") {
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
                                ERROR(lex.pointSpan(), E0000, StringView("Unknown fragment type '") << type << StringView("'"));
                            }
                            ret.push_back(MacroPatEnt(sp, name, idx, ty));
                            break;
                        }
                        case TOK_PAREN_OPEN: {
                            auto loopIdx = state.openLoop();
                            auto subpat = ParseMacroRulesPat(lex, TOK_PAREN_OPEN, TOK_PAREN_CLOSE, state);
                            state.closeLoop();

                            enum eTokenType joiner = TOK_NULL;

                            GET_TOK(tok, lex);
                            if (/*lex.edition_after(AST::Edition::Rust2018) &&*/ tok.type() == TOK_QMARK) {
                            } else if (tok.type() == TOK_PLUS || tok.type() == TOK_STAR) {
                            } else {
                                DEBUG(StringView("joiner = ") << tok);
                                if (tok.hasData()) {
                                    ERROR(lex.pointSpan(), E0000, StringView("Invalid macro joiner ") << tok << StringView(", must be punctuation"));
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
                                        parseErrorUnexpected(lex, tok, {TOK_PLUS, TOK_STAR, TOK_QMARK});
                                    } else {
                                        parseErrorUnexpected(lex, tok, {TOK_PLUS, TOK_STAR});
                                    }
                            }
                            BUG_ASSERT(sepFlag);
                            DEBUG(StringView("$()") << sepFlag << StringView(" ") << subpat);
                            ret.push_back(MacroPatEnt(sp, Token(joiner), sepFlag, loopIdx, std::move(subpat)));
                            break;
                        }
                    }
                    break;
                case TOK_EOF:
                    parseErrorUnexpected(lex, tok);
                default:
                    ret.push_back(MacroPatEnt(lex.endSpan(ps), tok));
                    break;
            }
            ps = lex.startSpan();
        }

        return ret;
    }

    std::vector<MacroExpansionEnt> ParseMacroRulesCont(TokenStream& lex, enum eTokenType open, enum eTokenType close, const RuleParseState& state, unsigned loopDepth = 0, std::map<unsigned int, ContentLoopVariableUse>* varUsagePtr = nullptr) {
        TRACE_FUNCTION;
        Token tok;
        std::vector<MacroExpansionEnt> ret;

        int depth = 0;
        while (GET_TOK(tok, lex) != close || depth > 0) {
            if (tok.type() == TOK_EOF) {
                parseErrorUnexpected(lex, tok);
            }
            if (tok.type() == TOK_NULL) {
                continue;
            }

            if (tok.type() == open) {
                DEBUG(StringView("depth++"));
                depth++;
            } else if (tok.type() == close) {
                DEBUG(StringView("depth--"));
                if (depth == 0) {
                    ERROR(lex.pointSpan(), E0000, StringView("Unmatched ") << Token(close) << StringView(" in macro content"));
                }
                depth--;
            } else {
            }

            if (tok.type() == TOK_DOLLAR) {
                GET_TOK(tok, lex);

                if (tok.type() == TOK_DOLLAR) {
                    ret.push_back(MacroExpansionEnt(Token(TOK_DOLLAR)));
                    continue;
                }

                if (tok.type() == TOK_PAREN_OPEN) {
                    std::map<unsigned int, ContentLoopVariableUse> varUsage;
                    auto content = ParseMacroRulesCont(lex, TOK_PAREN_OPEN, TOK_PAREN_CLOSE, state, loopDepth + 1, &varUsage);

                    DEBUG(StringView("var_usage = {") << varUsage << StringView("}"));
                    GET_TOK(tok, lex);
                    enum eTokenType joiner = TOK_NULL;
                    if (tok.type() == TOK_QMARK) {
                    } else if (tok.type() == TOK_PLUS || tok.type() == TOK_STAR) {
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
                                parseErrorUnexpected(lex, tok, {TOK_PLUS, TOK_STAR, TOK_QMARK});
                            } else {
                                parseErrorUnexpected(lex, tok, {TOK_PLUS, TOK_STAR});
                            }
                    }
                    bool isOptional = (loopType != '+');

                    std::set<unsigned> controllingLoops;
                    for (const auto& v : varUsage) {
                        if (v.second.loopStack.size() != 0 && loopDepth < v.second.loopStack.size()) {
                            controllingLoops.insert(v.second.loopStack[loopDepth]);
                        }
                    }
                    if (controllingLoops.empty()) {
                        WARNING(lex.pointSpan(), W0000, StringView("Macro loop doesn't contain any variables at this depth, omitting as it'll not run"));
                        continue;
                    }
                    // TODO: Check that +/*/? matches for the controlling loops

                    if (varUsagePtr) {
                        for (const auto& v : varUsage) {
                            auto it = varUsagePtr->insert(v).first;
                            if (isOptional) {
                                it->second.isOptional = true;
                            }
                        }
                    }

                    DEBUG(StringView("joiner = ") << Token(joiner) << StringView(", controlling_loops = {") << controllingLoops << StringView("}, content = ") << content);
                    ret.push_back(MacroExpansionEnt::make_Loop({mv$(content), joiner, mv$(controllingLoops)}));
                } else if (tok.type() == TOK_BRACE_OPEN) {
                    auto ident = lex.getTokenCheck(TOK_IDENT).ident().name;
                    if (ident == "ignore") {
                        lex.getTokenCheck(TOK_PAREN_OPEN);
                        lex.getTokenIf(TOK_DOLLAR);
                        GET_TOK(tok, lex);
                        if (!(tok.type() == TOK_IDENT || Token::typeIsRword(tok.type()))) {
                            CHECK_TOK(tok, TOK_IDENT);
                        }
                        auto nameIdent = tok.type() == TOK_IDENT ? tok.ident() : Ident(RcString::newInterned(tok.toStr()));
                        const auto& name = nameIdent.name;
                        lex.getTokenCheck(TOK_PAREN_CLOSE);
                        const auto* ns = state.findName(nameIdent);
                        if (!ns) {
                            TODO(lex.pointSpan(), StringView("Handle ${ignore(") << name << StringView(")} - Missing"));
                        }

                        DEBUG(StringView("$") << name << StringView(" #") << ns->idx << StringView(" [") << ns->loops << StringView("]"));
                        if (varUsagePtr) {
                            varUsagePtr->insert(std::make_pair(ns->idx, ContentLoopVariableUse(ns->loops)));
                        }
                        ret.push_back(MacroExpansionEnt(NAMEDVALUE_TY_IGNORE | ns->idx));
                    } else if (ident == "count") {
                        lex.getTokenCheck(TOK_PAREN_OPEN);
                        lex.getTokenIf(TOK_DOLLAR);
                        GET_TOK(tok, lex);
                        if (!(tok.type() == TOK_IDENT || Token::typeIsRword(tok.type()))) {
                            CHECK_TOK(tok, TOK_IDENT);
                        }
                        auto nameIdent = tok.type() == TOK_IDENT ? tok.ident() : Ident(RcString::newInterned(tok.toStr()));
                        const auto& name = nameIdent.name;
                        unsigned int countDepth = 0;
                        if (lex.getTokenIf(TOK_COMMA)) {
                            countDepth = static_cast<unsigned int>(lex.getTokenCheck(TOK_INTEGER).intval().truncateU64());
                        }
                        lex.getTokenCheck(TOK_PAREN_CLOSE);
                        const auto* ns = state.findName(nameIdent);
                        if (!ns) {
                            TODO(lex.pointSpan(), StringView("Handle ${count(") << name << StringView(")} - Missing"));
                        }

                        DEBUG(StringView("$") << name << StringView(" #") << ns->idx << StringView(" [") << ns->loops << StringView("]"));
                        if (varUsagePtr) {
                            varUsagePtr->insert(std::make_pair(ns->idx, ContentLoopVariableUse(ns->loops)));
                        }
                        ret.push_back(MacroExpansionEnt(NAMEDVALUE_TY_COUNT | (countDepth << NAMEDVALUE_COUNT_DEPTHSHIFT) | ns->idx));
                    } else if (ident == "index" || ident == "len") {
                        lex.getTokenCheck(TOK_PAREN_OPEN);
                        unsigned int loopDepthArg = 0;
                        if (!lex.getTokenIf(TOK_PAREN_CLOSE)) {
                            loopDepthArg = static_cast<unsigned int>(lex.getTokenCheck(TOK_INTEGER).intval().truncateU64());
                            lex.getTokenCheck(TOK_PAREN_CLOSE);
                        }
                        ret.push_back(MacroExpansionEnt((ident == "index" ? NAMEDVALUE_MAGIC_INDEX_AT : NAMEDVALUE_MAGIC_LEN_AT) + loopDepthArg));
                    } else if (ident == "concat") {
                        std::vector<MacroExpansionConcatEnt> ents;
                        lex.getTokenCheck(TOK_PAREN_OPEN);
                        while (true) {
                            if (lex.getTokenIf(TOK_DOLLAR)) {
                                if (lex.getTokenIf(TOK_RWORD_CRATE)) {
                                    ents.push_back(MacroExpansionConcatEnt(NAMEDVALUE_MAGIC_CRATE));
                                } else {
                                    GET_CHECK_TOK(tok, lex, TOK_IDENT);
                                    auto nameIdent = tok.type() == TOK_IDENT ? tok.ident() : Ident(RcString::newInterned(tok.toStr()));
                                    const auto& name = nameIdent.name;
                                    const auto* ns = state.findName(nameIdent);
                                    if (!ns) {
                                        TODO(lex.pointSpan(), StringView("concat - unmapped name"));
                                    } else {
                                        DEBUG(StringView("CONCAT $") << name << StringView(" #") << ns->idx << StringView(" [") << ns->loops << StringView("]"));
                                        if (loopDepth < ns->loops.size()) {
                                            ERROR(lex.pointSpan(), E0000, StringView("Variable $") << name << StringView(" is still repeating at this depth (") << loopDepth << StringView(" < ") << ns->loops.size() << StringView(")"));
                                        }

                                        if (varUsagePtr) {
                                            varUsagePtr->insert(std::make_pair(ns->idx, ContentLoopVariableUse(ns->loops)));
                                        }
                                        ents.push_back(MacroExpansionConcatEnt(ns->idx));
                                    }
                                }
                            } else if (lex.lookahead(0) == TOK_STRING) {
                                GET_CHECK_TOK(tok, lex, TOK_STRING);
                                ents.push_back(MacroExpansionConcatEnt(Ident(RcString::newInterned(tok.str()))));
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
                        TODO(lex.pointSpan(), StringView("Handle ${") << ident << StringView("...}"));
                    }
                    lex.getTokenCheck(TOK_BRACE_CLOSE);
                } else if (tok.type() == TOK_RWORD_CRATE) {
                    ret.push_back(MacroExpansionEnt(NAMEDVALUE_MAGIC_CRATE));
                } else if (tok.type() == TOK_IDENT || Token::typeIsRword(tok.type())) {
                    auto nameIdent = tok.type() == TOK_IDENT ? tok.ident() : Ident(RcString::newInterned(tok.toStr()));
                    const auto& name = nameIdent.name;
                    const auto* ns = state.findName(nameIdent);
                    if (!ns) {
                        ret.push_back(MacroExpansionEnt(Token(TOK_DOLLAR)));
                        ret.push_back(MacroExpansionEnt(mv$(tok)));
                    } else {
                        DEBUG(StringView("$") << name << StringView(" #") << ns->idx << StringView(" [") << ns->loops << StringView("]"));
                        if (loopDepth < ns->loops.size()) {
                            ERROR(lex.pointSpan(), E0000, StringView("Variable $") << name << StringView(" is still repeating at this depth (") << loopDepth << StringView(" < ") << ns->loops.size() << StringView(")"));
                        }

                        if (varUsagePtr) {
                            varUsagePtr->insert(std::make_pair(ns->idx, ContentLoopVariableUse(ns->loops)));
                        }
                        ret.push_back(MacroExpansionEnt(ns->idx));
                    }
                } else if (tok.type() == TOK_PAREN_CLOSE || tok.type() == TOK_SQUARE_CLOSE || tok.type() == TOK_BRACE_CLOSE) {
                    PUTBACK(tok, lex);
                    ret.push_back(MacroExpansionEnt(Token(TOK_DOLLAR)));
                } else {
                    parseErrorUnexpected(lex, tok);
                }
            } else {
                ret.push_back(MacroExpansionEnt(mv$(tok)));
            }
        }

        return ret;
    }

    MacroRule ParseMacroRulesVar(TokenStream& lex) {
        TRACE_FUNCTION;
        Token tok;

        MacroRule rule;

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
                parseErrorUnexpected(lex, tok);
        }
        RuleParseState state;
        {
            auto ps = lex.startSpan();
            rule.pattern = ParseMacroRulesPat(lex, tok.type(), close, state);
            rule.patSpan = lex.endSpan(ps);
        }

        GET_CHECK_TOK(tok, lex, TOK_FATARROW);

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
                parseErrorUnexpected(lex, tok);
        }
        rule.contents = ParseMacroRulesCont(lex, tok.type(), close, state);
        ASSERT_BUG(lex.pointSpan(), lex.parseState().wb, StringView("Macro parser has no WireBoard"));
        MacroRulesNormaliseFragments(*lex.parseState().wb, rule.contents);

        DEBUG(StringView("Rule - [") << rule.pattern << StringView("] => ") << rule.contents << StringView(""));
        return rule;
    }

    void enumerateNames(const std::vector<MacroPatEnt>& pats, std::vector<RcString>& names) {
        for (const auto& pat : pats) {
            if (pat.type == MacroPatEnt::PAT_LOOP) {
                enumerateNames(pat.subpats, names);
            } else if (pat.name != "") {
                auto b = names.begin();
                auto e = names.end();
                if (std::find(b, e, pat.name) == e) {
                    names.push_back(pat.name);
                }
            }
        }
    }

    MacroRulesArm ParseMacroRulesMakeArm(Span patSp, std::vector<MacroPatEnt> pattern, std::vector<MacroExpansionEnt> contents) {
        MacroRulesCheckFollowSets(pattern.data(), pattern.size());
        auto ruleSequence = macroPatternToSimple(patSp, pattern);
        auto arm = MacroRulesArm(mv$(ruleSequence), mv$(contents));
        enumerateNames(pattern, arm.paramNames);
        return arm;
    }

    MacroRulesPtr makeMrPtr(TokenStream& lex) {
        auto s = lex.pointSpan();
        auto rv = MacroRulesPtr(new MacroRules(lex.parseState().wb->id, s->crateName(), lex.getEdition()));
        rv->hygiene = lex.getHygiene();
        return rv;
    }



    enum class PatternHeadRv {
        Closed,
        NotFound,
        InvalidPath,
    };

    PatternHeadRv macroPatternGetHeadSetInner(std::vector<ExpTok>& rv, const std::vector<MacroPatEnt>& pattern, size_t directPos, const std::vector<ExpTok>& indirectPath, size_t indirectOfs) {
        TRACE_FUNCTION_F(static_cast<const void*>(&pattern) << StringView(" #") << directPos << StringView(" [") << indirectPath << StringView("]+") << indirectOfs);
        for (size_t idx = directPos; idx < pattern.size(); idx++) {
            const auto& ent = pattern[idx];
            DEBUG(idx << StringView(" ") << ent);
            switch (ent.type) {
                case MacroPatEnt::PAT_LOOP:
                    switch (macroPatternGetHeadSetInner(rv, ent.subpats, 0, indirectPath, indirectOfs)) {
                        case PatternHeadRv::InvalidPath:
                            if (ent.name == "+") {
                                return PatternHeadRv::InvalidPath;
                            } else {
                            }
                            break;
                        case PatternHeadRv::Closed:
                            if (ent.name == "+") {
                                return PatternHeadRv::Closed;
                            } else if (ent.name == "*" || ent.name == "?") {
                            } else {
                                BUG(Span(), StringView("Unknown loop type ") << ent.name);
                            }
                            break;
                        case PatternHeadRv::NotFound:
                            indirectOfs += ent.subpats.size();

                            if (ent.tok != TOK_NULL) {
                                if (indirectOfs < indirectPath.size()) {
                                    if (indirectPath[indirectOfs] != ExpTok(MacroPatEnt::PAT_TOKEN, ent.tok)) {
                                        return PatternHeadRv::InvalidPath;
                                    }
                                    indirectOfs++;

                                    if (ent.name != "?") {
                                        BUG_ASSERT(ent.subpats.size() > 0);
                                        macroPatternGetHeadSetInner(rv, ent.subpats, 0, indirectPath, indirectOfs + ent.subpats.size());
                                    }
                                } else {
                                    rv.push_back(ExpTok(MacroPatEnt::PAT_TOKEN, ent.tok));
                                }
                            } else {
                                if (ent.name != "?") {
                                    BUG_ASSERT(ent.subpats.size() > 0);
                                    macroPatternGetHeadSetInner(rv, ent.subpats, 0, indirectPath, indirectOfs + ent.subpats.size());
                                }
                            }
                            break;
                    }
                    break;
                default:
                    if (indirectOfs < indirectPath.size()) {
                        DEBUG(StringView("IP") << indirectOfs << StringView(" ") << indirectPath[indirectOfs]);
                        if (indirectPath[indirectOfs] != ExpTok(ent.type, ent.tok)) {
                            return PatternHeadRv::InvalidPath;
                        }
                        indirectOfs++;
                    } else {
                        DEBUG(StringView("Found"));
                        rv.push_back(ExpTok(ent.type, ent.tok));
                        return PatternHeadRv::Closed;
                    }
                    break;
            }
        }
        DEBUG(StringView("Hit end"));
        return PatternHeadRv::NotFound;
    }

    std::vector<ExpTok> macroPatternGetHeadSet(const std::vector<MacroPatEnt>& pattern, size_t directPos, const std::vector<ExpTok>& indirectPath) {
        std::vector<ExpTok> rv;
        TRACE_FUNCTION_FR(StringView(""), rv);
        if (macroPatternGetHeadSetInner(rv, pattern, directPos, indirectPath, 0) != PatternHeadRv::Closed) {
            if (!std::any_of(rv.begin(), rv.end(), [](const ExpTok& e) {
                return e.ty == MacroPatEnt::PAT_TOKEN && e.tok == TOK_EOF;
            })) {
                rv.push_back(ExpTok(MacroPatEnt::PAT_TOKEN, Token(TOK_EOF)));
            }
        }
        return rv;
    }

    void macroPatternToSimpleInner(const Span& sp, std::vector<SimplePatEnt>& rv, const std::vector<MacroPatEnt>& pattern) {
        size_t levelStart = rv.size();
        TRACE_FUNCTION_FR(StringView("[") << pattern << StringView("]"), StringView("[") << FMT_CB(ss, for (auto it = rv.begin() + levelStart; it != rv.end(); ++it) { ss << *it << StringView(", "); }) << StringView("]"));
        auto push = [&rv](SimplePatEnt spe) {
            DEBUG(StringView("[macro_pattern_to_simple_inner] rv[") << rv.size() << StringView("] = ") << spe);
            rv.push_back(std::move(spe));
        };
        auto pushIfv = [&push](bool isEqual, std::vector<SimplePatIfCheck> ents, size_t tgt) {
            push(SimplePatEnt::make_If({isEqual, tgt, mv$(ents)}));
        };
        for (size_t idx = 0; idx < pattern.size(); idx++) {
            const auto& ent = pattern[idx];
            DEBUG(StringView("[") << idx << StringView("] ent = ") << ent);
            switch (ent.type) {
                case MacroPatEnt::PAT_LOOP: {
                    auto entryPats1 = macroPatternGetHeadSet(ent.subpats, 0, {});
                    DEBUG(StringView("Entry = [") << entryPats1 << StringView("]"));
                    ASSERT_BUG(ent.sp, entryPats1.size() > 0, StringView("No entry conditions extracted from sub-pattern [") << ent.subpats << StringView("]"));
                    auto skipPats1 = macroPatternGetHeadSet(pattern, idx + 1, {});

                    DEBUG(StringView("Skip = [") << skipPats1 << StringView("]"));
                    // TODO: If EOF is in both entry and skip, then remove from entry
                    bool bodySkippable = false;
                    if (std::find(entryPats1.begin(), entryPats1.end(), TOK_EOF) != entryPats1.end()) {
                        if (std::find(skipPats1.begin(), skipPats1.end(), TOK_EOF) != entryPats1.end()) {
                            entryPats1.erase(std::find(entryPats1.begin(), entryPats1.end(), TOK_EOF));
                            bodySkippable = true;
                        }
                    }

                    std::vector<std::vector<SimplePatIfCheck>> entryConds;
                    std::vector<std::vector<SimplePatIfCheck>> skipConds;
                    std::vector<std::vector<SimplePatIfCheck>> repeatConds;

                    for (const auto& ee : entryPats1) {
                        entryConds.push_back(::makeVec1<SimplePatIfCheck>({ee.ty, ee.tok}));
                    }
                    for (const auto& ee : skipPats1) {
                        skipConds.push_back(::makeVec1<SimplePatIfCheck>({ee.ty, ee.tok}));
                    }

                    const size_t MAX_CONDITION_ADD = 2;
                    for (size_t iterations = 0; iterations < MAX_CONDITION_ADD; iterations++) {
                        bool didExtend = false;
                        for (auto eIt = entryConds.begin(); eIt != entryConds.end(); ++eIt) {
                            auto sIt = std::find(skipConds.begin(), skipConds.end(), *eIt);
                            if (sIt != skipConds.end()) {
                                didExtend = true;

                                DEBUG(StringView("Entry condition is also in skip condition: ") << *eIt);
                                if (eIt->front().ty != MacroPatEnt::PAT_TOKEN && !eIt->front().isLocalAmbiguity()) {
                                    eIt->front().markLocalAmbiguity();
                                    sIt->front().markLocalAmbiguity();
                                }

                                std::vector<ExpTok> path;
                                for (auto it = eIt->begin(); it != eIt->end(); ++it) {
                                    path.push_back(ExpTok(it->ty, it->tok));
                                }
                                auto entryPats2 = macroPatternGetHeadSet(ent.subpats, 0, path);
                                BUG_ASSERT(entryPats2.size() > 0);
                                if (std::find(entryPats2.begin(), entryPats2.end(), TOK_EOF) != entryPats2.end()) {
                                    entryPats2.erase(std::find(entryPats2.begin(), entryPats2.end(), TOK_EOF));
                                    entryPats2.insert(entryPats2.end(), skipPats1.begin(), skipPats1.end());
                                }
                                auto skipPats2 = macroPatternGetHeadSet(pattern, idx + 1, path);
                                BUG_ASSERT(skipPats2.size() > 0);
                                DEBUG(StringView("entry_pats2 = [") << entryPats2 << StringView("]"));
                                DEBUG(StringView("skip_pats2 = [") << skipPats2 << StringView("]"));
                                {
                                    auto e2It = entryPats2.begin();
                                    eIt->push_back({e2It->ty, e2It->tok});
                                    for (++e2It; e2It != entryPats2.end(); ++e2It) {
                                        eIt = entryConds.insert(eIt, *eIt);
                                        eIt->back() = SimplePatIfCheck{e2It->ty, e2It->tok};
                                    }
                                }

                                {
                                    auto s2It = skipPats2.begin();
                                    sIt->push_back({s2It->ty, s2It->tok});
                                    for (++s2It; s2It != skipPats2.end(); ++s2It) {
                                        sIt = skipConds.insert(sIt, *sIt);
                                        sIt->back() = SimplePatIfCheck{s2It->ty, s2It->tok};
                                    }
                                }
                            }
                        }
                        // TODO: If any end with `:vis` then extend
                        if (!didExtend) {
                            break;
                        }
                    }
                    for (auto eIt = entryConds.begin(); eIt != entryConds.end(); ++eIt) {
                        auto sIt = std::find(skipConds.begin(), skipConds.end(), *eIt);
                        if (sIt != skipConds.end()) {
                            TODO(ent.sp, StringView("Entry and skip patterns share an entry (max extend ") << MAX_CONDITION_ADD << StringView("), ambigious - ") << *eIt);
                        }
                    }
                    for (const auto& e : entryConds) {
                        DEBUG(StringView("Entry += [") << e << StringView("]"));
                    }
                    for (const auto& e : skipConds) {
                        DEBUG(StringView("Skip += [") << e << StringView("]"));
                    }
                    if (ent.tok != TOK_NULL) {
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
                    DEBUG(StringView("Repeat = ["));
                    for (const auto& e : repeatConds) {
                        DEBUG(StringView(" [") << e << StringView("]"));
                    }
                    DEBUG(StringView("]"));
                    // TODO: Combine the two cases below into one?

                    if (ent.name == "+") {
                        push(SimplePatEnt::make_LoopStart({ent.nameIndex}));
                        size_t start = rv.size();
                        macroPatternToSimpleInner(sp, rv, ent.subpats);
                        push(SimplePatEnt::make_LoopNext({/*ent.name_index*/}));
                        size_t rewriteStart = rv.size();
                        if (ent.tok != TOK_NULL) {
                            if (repeatConds.size() > 1) {
                                DEBUG(StringView("Loop+ Multi-option repeat"));
                                size_t expectAndJumpPos = rv.size() + repeatConds.size() + 1;
                                for (const auto& ee : repeatConds) {
                                    pushIfv(true, ee, expectAndJumpPos);
                                }
                                push(SimplePatEnt::make_Jump({~0u}));
                            } else {
                                DEBUG(StringView("Loop+ Single-option repeat"));
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

                        size_t rewriteStart = rv.size();
                        if (entryConds.size() == 1 && entryConds[0].back().tok != TOK_EOF) // HACK: if the entry ends with `EOF` then it won't be correct
                        {
                            pushIfv(false, entryConds.front(), ~0u);
                        } else if (skipConds.empty()) {
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
                                    DEBUG(StringView("Loop* - Single-option repeat"));
                                    for (const auto& ee : repeatConds) {
                                        pushIfv(/*is_equal*/ false, ee, ~0u);
                                    }
                                    push(SimplePatEnt::make_ExpectTok(ent.tok));
                                } else {
                                    DEBUG(StringView("Loop* - Multi-option repeat"));
                                    auto checkPos = rv.size() + repeatConds.size() + 1;
                                    for (const auto& ee : repeatConds) {
                                        pushIfv(/*is_equal*/ true, ee, checkPos);
                                    }
                                    push(SimplePatEnt::make_Jump({~0u}));
                                    BUG_ASSERT(rv.size() == checkPos);
                                    push(SimplePatEnt::make_ExpectTok(ent.tok));
                                }
                            }
                            push(SimplePatEnt::make_Jump({rewriteStart}));
                        } else if (ent.name == "?") {
                            ASSERT_BUG(sp, ent.tok == TOK_NULL, StringView("$()? with a separator isn't valid"));
                        } else {
                            BUG(sp, StringView(""));
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
                        TODO(sp, StringView("Handle loop type '") << ent.name << StringView("'"));
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
            {
                auto& tuMatch = rv[i];
                switch (tuMatch.tag()) {
                    default:
                        break;
                    case SimplePatEnt::TAG_If: {
                        auto& e = tuMatch.as_If();
                        ASSERT_BUG(sp, e.jumpTarget < rv.size(), StringView("If target out of bounds, ") << e.jumpTarget << StringView(" >= ") << rv.size());
                        break;
                    }
                    case SimplePatEnt::TAG_Jump: {
                        auto& e = tuMatch.as_Jump();
                        ASSERT_BUG(sp, e.jumpTarget < rv.size(), StringView("Jump target out of bounds, ") << e.jumpTarget << StringView(" >= ") << rv.size());
                        break;
                    }
                }
            }
        }
    }

    std::vector<SimplePatEnt> macroPatternToSimple(const Span& sp, const std::vector<MacroPatEnt>& pattern) {
        std::vector<SimplePatEnt> rv;
        TRACE_FUNCTION_FR(pattern, rv);
        macroPatternToSimpleInner(sp, rv, pattern);
        return rv;
    }
}

void ParameterMappings::insert(unsigned int nameIndex, const std::vector<unsigned int>& iterations, InterpolatedFragment data) {
    DEBUG(StringView("index=") << nameIndex << StringView(", iterations=[") << iterations << StringView("], data=") << data);
    if (nameIndex >= mappings_.size()) {
        mappings_.resize(nameIndex + 1);
    }
    auto* layer = &mappings_[nameIndex].topLayer;
    if (iterations.size() > 0) {
        for (unsigned int i = 0; i < iterations.size() - 1; i++) {
            auto iter = iterations[i];

            if (layer->is_Vals()) {
                BUG_ASSERT(layer->as_Vals().size() == 0);
                *layer = CaptureLayer::make_Nested({});
            }
            auto& e = layer->as_Nested();
            while (e.size() < iter) {
                DEBUG(StringView("- Skipped iteration ") << e.size());
                e.push_back(CaptureLayer::make_Nested({}));
            }

            if (e.size() == iter) {
                e.push_back(CaptureLayer::make_Vals({}));
            }
            layer = &e[iter];
        }
        ASSERT_BUG(Span(), layer->as_Vals().size() == iterations.back(), StringView("Capture count mismatch with iteration index - iterations=[") << iterations << StringView("]"));
    } else {
        BUG_ASSERT(layer->as_Vals().size() == 0);
    }
    layer->as_Vals().push_back(CapturedVal{0, 0, mv$(data)});
}

CapturedVal& ParameterMappings::getCap(const Span& sp, const std::vector<unsigned int>& iterations, unsigned int nameIdx) {
    DEBUG(StringView("(iterations=[") << iterations << StringView("], name_idx=") << nameIdx << StringView(")"));
    auto& e = mappings_.at(nameIdx);
    auto* layer = &e.topLayer;

    if (auto* e = layer->opt_Vals()) {
        if (e->size() == 1) {
            return (*e)[0];
        }
        if (e->size() == 0) {
            BUG(sp, StringView("Attempting to get binding for empty capture - #") << nameIdx);
        }
    }

    for (const auto iter : iterations) {
        switch ((*layer).tag()) {
            case CaptureLayer::TAG_Vals: {
                auto& e = (*layer).as_Vals();
                ASSERT_BUG(sp, iter < e.size(), StringView("Iteration index ") << iter << StringView(" outside of range ") << e.size() << StringView(" (values)"));
                return e.at(iter);
            }
            case CaptureLayer::TAG_Nested: {
                auto& e = (*layer).as_Nested();
                ASSERT_BUG(sp, iter < e.size(), StringView("Iteration index ") << iter << StringView(" outside of range ") << e.size() << StringView(" (nest)"));
                layer = &e.at(iter);
                break;
            }
        }
    }

    ERROR(sp, E0000, StringView("Variable #") << nameIdx << StringView(" is still repeating at this level (") << iterations.size() << StringView(")"));
}

InterpolatedFragment* ParameterMappings::get(const Span& sp, const std::vector<unsigned int>& iterations, unsigned int nameIdx) {
    return &getCap(sp, iterations, nameIdx).frag;
}

unsigned int ParameterMappings::getLoopRepeats(const Span& sp, const std::vector<unsigned int>& iterations, unsigned int loopIdx) const {
    const auto& list = loopCounts.at(loopIdx);
    for (const auto& e : list) {
        ASSERT_BUG(Span(), e.first.size() <= iterations.size(), StringView("Loop ") << loopIdx << StringView(" iteration path [") << e.first << StringView("] larger than query path [") << iterations << StringView("]"));
        if (std::equal(e.first.begin(), e.first.end(), iterations.begin())) {
            return e.second;
        }
    }
    BUG(sp, StringView("Loop ") << loopIdx << StringView(" cannot find an iteration count for path [") << iterations << StringView("]"));
}

unsigned int ParameterMappings::getVariableCount(const Span& sp, const std::vector<unsigned int>& iterations, unsigned int nameIdx, unsigned int depth) const {
    DEBUG(StringView("(iterations=[") << iterations << StringView("], name_idx=") << nameIdx << StringView(")"));
    auto& e = mappings_.at(nameIdx);
    auto* layer = &e.topLayer;

    if (auto* e = layer->opt_Vals()) {
        if (e->size() == 1) {
            return 1;
        }
        if (e->size() == 0) {
            BUG(sp, StringView("Attempting to get binding for empty capture - #") << nameIdx);
        }
    }

    for (const auto iter : iterations) {
        switch ((*layer).tag()) {
            case CaptureLayer::TAG_Vals: {
                auto& e = (*layer).as_Vals();
                ASSERT_BUG(sp, iter < e.size(), StringView("Iteration index ") << iter << StringView(" outside of range ") << e.size() << StringView(" (values)"));
                return 1;
            }
            case CaptureLayer::TAG_Nested: {
                auto& e = (*layer).as_Nested();
                ASSERT_BUG(sp, iter < e.size(), StringView("Iteration index ") << iter << StringView(" outside of range ") << e.size() << StringView(" (nest)"));
                layer = &e.at(iter);
                break;
            }
        }
    }
    const unsigned int levels = captureLayerDepth(*layer);
    if (depth >= levels) {
        depth = levels - 1;
    }
    return captureLayerNodesAt(*layer, levels - depth);
}

void ParameterMappings::incCount(const Span& sp, const std::vector<unsigned int>& iterations, unsigned int nameIdx) {
    auto& cap = getCap(sp, iterations, nameIdx);
    BUG_ASSERT(cap.numUsed == 0);
    cap.numUses += 1;
}

bool ParameterMappings::decCount(const Span& sp, const std::vector<unsigned int>& iterations, unsigned int nameIdx) {
    auto& cap = getCap(sp, iterations, nameIdx);
    BUG_ASSERT(cap.numUsed < cap.numUses);
    cap.numUsed += 1;
    return (cap.numUsed < cap.numUses);
}

const SimplePatEnt& MacroPatternStream::next() {
    if (peekCacheValid) {
        peekCacheValid = false;
        return *peekCache;
    }

    for (;;) {
        if (!conditionReplay && lastWasCond) {
            conditionHistory.push_back(conditionMet);
        }
        lastWasCond = false;
        if (curPos_ == simpleEnts.size()) {
            return endEnt;
        }
        const auto& curEnt = simpleEnts[curPos_];
        if (conditionReplay && curEnt.is_If()) {
            if ((*conditionReplay)[conditionReplayPos++]) {
                curPos_ = curEnt.as_If().jumpTarget;
            } else {
                curPos_ += 1;
            }
            continue;
        }
        curPos_ += 1;
        switch (curEnt.tag()) {
            default:
                if (curEnt.is_If()) {
                    lastWasCond = true;
                    conditionMet = false;
                }
                return curEnt;
            case SimplePatEnt::TAG_End: {
                BUG(Span(), StringView("Unexpected End"));
                break;
            }
            case SimplePatEnt::TAG_Jump: {
                auto& e = curEnt.as_Jump();
                curPos_ = e.jumpTarget;
                break;
            }
            case SimplePatEnt::TAG_LoopStart: {
                auto& e = curEnt.as_LoopStart();
                currentLoops.push_back(e.index);
                loopIterations.push_back(0);
                break;
            }
            case SimplePatEnt::TAG_LoopNext: {
                loopIterations.back() += 1;
                break;
            }
            case SimplePatEnt::TAG_LoopEnd: {
                BUG_ASSERT(!loopIterations.empty());
                BUG_ASSERT(!currentLoops.empty());
                auto loopIndex = currentLoops.back();
                auto numIter = loopIterations.back();
                loopIterations.pop_back();
                currentLoops.pop_back();

                if (conditionReplay) {
                    loopCounts[loopIndex].insert(std::make_pair(loopIterations, numIter));
                }
                break;
            }
        }
    }
}

void MacroPatternStream::ifSucceeded() {
    BUG_ASSERT(curPos_ > 0);
    BUG_ASSERT(curPos_ <= simpleEnts.size());
    BUG_ASSERT(lastWasCond);
    const auto& ent = simpleEnts[curPos_ - 1];
    ASSERT_BUG(Span(), ent.is_If(), StringView("Expected If when calling `if_succeeded`, got ") << ent);
    const auto& e = ent.as_If();
    ASSERT_BUG(Span(), e.jumpTarget < simpleEnts.size(), StringView("Jump target ") << e.jumpTarget << StringView(" out of range ") << simpleEnts.size());
    curPos_ = e.jumpTarget;
    conditionMet = true;
}

std::unique_ptr<TokenStream> MacroInvokeRules(const RcString& name, const MacroRules& rules, const Span& sp, const WireBoard& wb, TokenTree input, const ASTCrate& crate, ASTModule& mod) {
    TRACE_FUNCTION_F(StringView("'") << name << StringView("', ") << input);
    DEBUG(StringView("rules.m_source_crate = ") << rules.sourceCrate);
    DEBUG(StringView("rules.m_hygiene = ") << rules.hygiene);
    ParameterMappings boundTts;
    unsigned int ruleIndex = MacroInvokeRulesMatchPattern(sp, wb, rules, mv$(input), crate, mod, boundTts);

    const auto& rule = rules.rules.at(ruleIndex);

    DEBUG(StringView("Using macro '") << name << StringView("' #") << ruleIndex << StringView(" - ") << rule.contents.size() << StringView(" rule contents with ") << boundTts.mappings().size() << StringView(" bound values"));
    MacroInvokeRulesCountSubstUses(boundTts, rule.contents);

    TokenStream* retPtr = new MacroExpander(wb.id, *crate.hirPool, name, sp, crate.edition, rules.isMacroItem, rules.transparent, rules.definitionId, rules.hygiene, rule.contents, mv$(boundTts), rules.sourceCrate == "" ? crate.crateNameReal : rules.sourceCrate, rules.edition);

    return std::unique_ptr<TokenStream>(retPtr);
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
    lastHygiene = hygiene_;
    if (nextToken.type() != TOK_NULL) {
        DEBUG(StringView("m_next_token = ") << nextToken);
        return mv$(nextToken);
    }
    if (ttstream.get()) {
        Token rv = ttstream->getToken();
        DEBUG(StringView("TTStream present: ") << rv);
        if (rv.type() != TOK_EOF) {
            return rv;
        }
        ttstream.reset();
    }

    while (const auto* nextEntPtr = state.nextEnt()) {
        const auto& ent = *nextEntPtr;
        switch (ent.tag()) {
            case MacroExpansionEnt::TAG_Token: {
                auto& e = ent.as_Token();
                switch (e.type()) {
                    case TOK_IDENT:
                    case TOK_LIFETIME: {
                        auto ident = e.ident();
                        if (!transparent) {
                            ident.hygiene = ident.hygiene.withTailScope(pool, hygiene_, isMacroItem);
                        }
                        lastHygiene = ident.hygiene;
                        auto rv = Token(e.type(), std::move(ident));
                        DEBUG(StringView("Updated hygine: ") << rv);
                        return rv;
                        break;
                    }
                    case TOK_BYTESTRING:
                    case TOK_STRING: {
                        auto h = e.strHygiene();
                        h = h.withTailScope(pool, hygiene_, isMacroItem);
                        lastHygiene = h;
                        auto rv = Token(e.type(), e.str(), std::move(h));
                        DEBUG(StringView("Updated hygine: ") << rv);
                        return rv;
                    }
                    default:
                        DEBUG(StringView("Raw token: ") << e);
                        return e.clone();
                }
                break;
            }
            case MacroExpansionEnt::TAG_NamedValue: {
                auto& e = ent.as_NamedValue();
                switch (e & ~NAMEDVALUE_VALMASK) {
                    default:
                        BUG(this->pointSpan(), StringView("Unknown macro metavar - 0x") << formatHex(e));
                    case NAMEDVALUE_TY_COUNT: {
                        const auto value = e & NAMEDVALUE_VALMASK;
                        auto count = mappings_.getVariableCount(this->pointSpan(), state.iterations(), value & NAMEDVALUE_COUNT_IDXMASK, value >> NAMEDVALUE_COUNT_DEPTHSHIFT);
                        return Token(U128(count), CORETYPE_ANY);
                        break;
                    }
                    case NAMEDVALUE_TY_IGNORE: {
                        break;
                    }
                    case NAMEDVALUE_TY_MAGIC:
                        switch (e) {
                            case NAMEDVALUE_MAGIC_CRATE:
                                DEBUG(StringView("Crate name hack"));
                                if (crateName == "") {
                                    if (sourceEdition >= ASTEdition::Rust2018) {
                                        return Token(TOK_RWORD_CRATE);
                                    }
                                } else {
                                    nextToken = Token(TOK_STRING, std::string(crateName.c_str()), {});
                                    return Token(TOK_DOUBLE_COLON);
                                }
                                break;
                            case NAMEDVALUE_MAGIC_INDEX:
                                ASSERT_BUG(this->pointSpan(), !state.iterations().empty(), StringView("${index()} with no active loop"));
                                return Token(U128(state.iterations().back()), CORETYPE_ANY);
                            default: {
                                const auto kind = e & ~NAMEDVALUE_MAGIC_DEPTHMASK;
                                const auto depth = e & NAMEDVALUE_MAGIC_DEPTHMASK;
                                if (kind == NAMEDVALUE_MAGIC_INDEX_AT) {
                                    auto idx = state.loopIndexAt(depth);
                                    ASSERT_BUG(this->pointSpan(), idx != ~0u, StringView("${index(") << depth << StringView(")} with no such loop"));
                                    return Token(U128(idx), CORETYPE_ANY);
                                }
                                if (kind == NAMEDVALUE_MAGIC_LEN_AT) {
                                    auto len = state.loopLengthAt(depth);
                                    ASSERT_BUG(this->pointSpan(), len != ~0u, StringView("${len(") << depth << StringView(")} with no such loop"));
                                    return Token(U128(len), CORETYPE_ANY);
                                }
                                BUG(this->pointSpan(), StringView("Unknown macro metavar - 0x") << formatHex(e));
                            }
                        }
                        break;
                    case 0: {
                        auto* frag = mappings_.get(this->pointSpan(), state.iterations(), e);
                        ASSERT_BUG(this->pointSpan(), frag, StringView("Cannot find '") << e << StringView("' for ") << state.iterations());

                        bool canSteal = (mappings_.decCount(this->pointSpan(), state.iterations(), e) == false);
                        DEBUG(StringView("Insert replacement #") << e << StringView(" = ") << *frag);
                        if (frag->type == InterpolatedFragment::TT) {
                            auto resTt = canSteal ? mv$(frag->asTt()) : frag->asTt().clone();
                            ttstream.reset(new TTStreamO(this->outerSpan(), ParseState(), mv$(resTt)));
                            return ttstream->getToken();
                        } else {
                            if (canSteal) {
                                return Token(Token::TagTakeIP(), mv$(*frag));
                            } else {
                                return Token(*frag);
                            }
                        }
                    } break;
                }
                break;
            }
            case MacroExpansionEnt::TAG_Concat: {
                auto& e = ent.as_Concat();
                std::string newIdent;
                for (const auto& ent : e) {
                    switch (ent.tag()) {
                        case MacroExpansionConcatEnt::TAG_Named: {
                            auto& v = ent.as_Named();
                            bool canSteal = (mappings_.decCount(this->pointSpan(), state.iterations(), v) == false);
                            auto* frag = mappings_.get(this->pointSpan(), state.iterations(), v);
                            ASSERT_BUG(this->pointSpan(), frag, StringView("Cannot find '") << v << StringView("' for ") << state.iterations());
                            Token tok;
                            if (frag->type == InterpolatedFragment::TT) {
                                auto resTt = canSteal ? mv$(frag->asTt()) : frag->asTt().clone();
                                TTStreamO tts(this->outerSpan(), ParseState(), std::move(resTt));
                                tok = tts.getToken();
                                tts.getTokenCheck(TOK_EOF);
                            } else {
                                tok = canSteal ? Token(Token::TagTakeIP(), mv$(*frag)) : Token(*frag);
                            }
                            if (tok == TOK_STRING) {
                                newIdent += tok.str();
                            } else if (tok == TOK_IDENT) {
                                newIdent += tok.ident().name.c_str();
                            } else {
                                ERROR(this->pointSpan(), E0000, StringView("concat with non-ident: ") << tok);
                            }
                            break;
                        }
                        case MacroExpansionConcatEnt::TAG_Ident: {
                            auto& v = ent.as_Ident();
                            newIdent += v.name.c_str();
                            break;
                        }
                    }
                }
                return Token(TOK_IDENT, Ident(realGetHygiene(), RcString::newInterned(newIdent)));
            }
            case MacroExpansionEnt::TAG_Loop: {
                auto& e = ent.as_Loop();
                DEBUG(StringView("Loop joiner ") << e.joiner);
                return e.joiner;
            }
        }
    }

    DEBUG(StringView("EOF"));
    return Token(TOK_EOF);
}

const MacroExpansionEnt* MacroExpandState::nextEnt() {
    while (offsets.size() > 0) {
        unsigned int layer = offsets.size() - 1;
        const auto& ents = *curEnts;

        size_t idx = offsets.back().readPos++;

        if (idx < ents.size()) {
            const auto& ent = ents[idx];
            switch (ent.tag()) {
                case MacroExpansionEnt::TAG_Token: {
                    return &ent;
                }
                case MacroExpansionEnt::TAG_NamedValue: {
                    return &ent;
                }
                case MacroExpansionEnt::TAG_Concat: {
                    return &ent;
                }
                case MacroExpansionEnt::TAG_Loop: {
                    auto& e = ent.as_Loop();
                    BUG_ASSERT(!e.controllingInputLoops.empty());
                    unsigned int numRepeats = mappings_.getLoopRepeats(Span(), iterations_, *e.controllingInputLoops.begin());
                    for (auto loopIdent : e.controllingInputLoops) {
                        if (loopIdent == *e.controllingInputLoops.begin()) {
                            continue;
                        }

                        unsigned int thisRepeats = mappings_.getLoopRepeats(Span(), iterations_, loopIdent);
                        if (thisRepeats != numRepeats) {
                            // TODO: Get the variables involved, or the pattern+output spans
                            ERROR(Span(), E0000, StringView("Mismatch in loop iterations: ") << thisRepeats << StringView(" != ") << numRepeats);
                        }
                    }
                    DEBUG(StringView("Looping ") << numRepeats << StringView(" times based on {") << e.controllingInputLoops << StringView("}"));
                    if (numRepeats > 0) {
                        offsets.push_back({0, 0, numRepeats});
                        iterations_.push_back(0);
                        curEnts = getCurLayer();
                    }
                    break;
                }
            }
        } else if (layer > 0) {
            DEBUG(StringView("layer = ") << layer << StringView(", m_iterations = ") << iterations_);
            auto& curOfs = offsets.back();
            DEBUG(StringView("Layer #") << layer << StringView(" Cur: ") << curOfs.loopIndex << StringView(", Max: ") << curOfs.maxIndex);
            if (curOfs.loopIndex + 1 < curOfs.maxIndex) {
                iterations_.back()++;

                DEBUG(StringView("Restart layer"));
                curOfs.readPos = 0;
                curOfs.loopIndex++;

                auto& loopLayer = getCurLayerEnt();
                if (loopLayer.as_Loop().joiner.type() != TOK_NULL) {
                    DEBUG(StringView("- Separator token = ") << loopLayer.as_Loop().joiner);
                    return &loopLayer;
                }
            } else {
                offsets.pop_back();
                iterations_.pop_back();
                if (offsets.size() == 0) {
                    break;
                }
                curEnts = getCurLayer();
            }
        } else {
            DEBUG(StringView("Terminate evaluation"));
            offsets.pop_back();
            BUG_ASSERT(offsets.size() == 0);
        }
    }

    return nullptr;
}

const MacroExpansionEnt& MacroExpandState::getCurLayerEnt() const {
    BUG_ASSERT(offsets.size() > 1);

    const auto* ents = &rootContents;
    for (unsigned int i = 0; i < offsets.size() - 2; i++) {
        unsigned int ofs = offsets[i].readPos;
        BUG_ASSERT(ofs > 0 && ofs <= ents->size());
        ents = &(*ents)[ofs - 1].as_Loop().entries;
    }
    return (*ents)[offsets[offsets.size() - 2].readPos - 1];
}

const std::vector<MacroExpansionEnt>* MacroExpandState::getCurLayer() const {
    BUG_ASSERT(offsets.size() > 0);
    const auto* ents = &rootContents;
    for (unsigned int i = 0; i < offsets.size() - 1; i++) {
        unsigned int ofs = offsets[i].readPos;
        BUG_ASSERT(ofs > 0 && ofs <= ents->size());
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
        case TOK_AMP:
        case TOK_STAR:
        case TOK_DASH:
        case TOK_EXCLAM:
        case TOK_RWORD_BOX:
        case TOK_PAREN_OPEN:
        case TOK_SQUARE_OPEN:

        case TOK_RWORD_RETURN:
        case TOK_RWORD_BREAK:
        case TOK_RWORD_CONTINUE:

        case TOK_BRACE_OPEN:
        case TOK_RWORD_MATCH:
        case TOK_RWORD_IF:
        case TOK_RWORD_FOR:
        case TOK_RWORD_WHILE:
        case TOK_RWORD_LOOP:
        case TOK_RWORD_UNSAFE:
        case TOK_RWORD_TRY:
        case TOK_LIFETIME:

        case TOK_RWORD_MOVE:
        case TOK_PIPE:
        case TOK_DOUBLE_PIPE:

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
}

MacroRulesPtr::~MacroRulesPtr() {
    if (ptr) {
        delete ptr;
        ptr = nullptr;
    }
}









MacroRules::~MacroRules() {
}

MacroRulesArm::~MacroRulesArm() {
}

MacroRulesPtr ParseMacroRules(TokenStream& lex);

void MacroRulesNormaliseFragments(const WireBoard& wb, std::vector<MacroExpansionEnt>& contents) {
    const auto isInterpolatedFragment = [](eTokenType type) {
        switch (type) {
            case TOK_INTERPOLATED_PATH:
            case TOK_INTERPOLATED_TYPE:
            case TOK_INTERPOLATED_PATTERN:
            case TOK_INTERPOLATED_EXPR:
            case TOK_INTERPOLATED_STMT:
            case TOK_INTERPOLATED_STMT_ITEM:
            case TOK_INTERPOLATED_BLOCK:
            case TOK_INTERPOLATED_META:
            case TOK_INTERPOLATED_ITEM:
            case TOK_INTERPOLATED_VIS:
                return true;
            default:
                return false;
        }
    };

    struct Emitter {
        const WireBoard& wb;
        std::vector<MacroExpansionEnt> out;

        void emitFromString(const std::string& s) {
            std::istringstream iss{s};
            Lexer lex{wb.id, *wb.pool, iss, ASTEdition::Rust2021, {}};
            for (;;) {
                auto tok = lex.getToken();
                if (tok == TOK_EOF) {
                    break;
                }
                out.push_back(std::move(tok));
            }
        }

        void emitPath(const ASTPath& path) {
            if (const auto* e = path.cls.opt_Local()) {
                out.push_back(Token(TOK_IDENT, e->name));
                return;
            }
            if (const auto* e = path.cls.opt_Relative()) {
                bool simple = true;
                for (const auto& node : e->nodes) {
                    simple &= node.args().isEmpty();
                }
                if (simple) {
                    for (const auto& node : e->nodes) {
                        if (&node != &e->nodes.front()) {
                            out.push_back(Token(TOK_DOUBLE_COLON));
                        }
                        out.push_back(Token(TOK_IDENT, Ident(e->hygiene, node.name())));
                    }
                    return;
                }
            }
            StringBuilder ss;
            path.printPretty(ss, false);
            emitFromString(std::string(static_cast<const char*>(ss.data()), ss.length()));
        }

        void emitTokenTree(TokenTree& tt) {
            if (tt.isToken()) {
                emitToken(tt.tok());
            } else {
                for (size_t i = 0; i < tt.size(); i++) {
                    emitTokenTree(tt[i]);
                }
            }
        }

        void emitAst(const ASTExprNode& node) {
            if (const auto* e = cast<const ASTExprNodeInteger>(&node)) {
                out.push_back(Token(e->value, e->datatype));
            } else if (const auto* e = cast<const ASTExprNodeBool>(&node)) {
                out.push_back(Token(e->value ? TOK_RWORD_TRUE : TOK_RWORD_FALSE));
            } else if (const auto* e = cast<const ASTExprNodeNamedValue>(&node)) {
                emitPath(e->path);
            } else if (const auto* e = cast<const ASTExprNodeMacro>(&node)) {
                emitPath(e->path);
                out.push_back(Token(TOK_EXCLAM));
                if (e->ident != "") {
                    out.push_back(Token(TOK_IDENT, e->ident));
                }
                out.push_back(Token(e->isBraced ? TOK_BRACE_OPEN : TOK_PAREN_OPEN));
                auto tokens = e->tokens.clone();
                emitTokenTree(tokens);
                out.push_back(Token(e->isBraced ? TOK_BRACE_CLOSE : TOK_PAREN_CLOSE));
            } else {
                BUG(Span(), StringView("Unknown node type: ") << typeid(node).name());
            }
        }

        void emitType(ASTType*& type) {
            switch (type->data.tag()) {
                default:
                    TODO(Span(), StringView("Convert interpolated macro fragment: ") << type);
                case TypeData::TAG_Path: {
                    auto& p = type->data.as_Path();
                    emitPath(*p);
                    break;
                }
            }
        }

        void emitToken(Token& tok) {
            switch (tok.type()) {
                case TOK_INTERPOLATED_PATH:
                case TOK_INTERPOLATED_PATTERN:
                case TOK_INTERPOLATED_STMT:
                case TOK_INTERPOLATED_STMT_ITEM:
                case TOK_INTERPOLATED_BLOCK:
                case TOK_INTERPOLATED_ITEM:
                case TOK_INTERPOLATED_VIS:
                    TODO(Span(), StringView("Convert interpolated macro fragment: ") << tok);
                case TOK_INTERPOLATED_TYPE:
                    emitType(tok.fragType());
                    break;
                case TOK_INTERPOLATED_META: {
                    auto& meta = tok.fragMeta();
                    for (const auto& e : meta.name().elems) {
                        if (&e != &meta.name().elems.front()) {
                            out.push_back(Token(TOK_DOUBLE_COLON));
                        }
                        out.push_back(Token(TOK_IDENT, e));
                    }
                    emitTokenTree(meta.dataMut());
                    break;
                }
                case TOK_INTERPOLATED_EXPR:
                    emitAst(tok.fragNode());
                    break;
                default:
                    out.push_back(std::move(tok));
                    break;
            }
        }
    };

    for (auto it = contents.begin(); it != contents.end();) {
        if (auto* loop = it->opt_Loop()) {
            MacroRulesNormaliseFragments(wb, loop->entries);
            ++it;
            continue;
        }
        auto* tok = it->opt_Token();
        if (!tok || !isInterpolatedFragment(tok->type())) {
            ++it;
            continue;
        }

        Emitter emitter{wb};
        emitter.emitToken(*tok);
        ASSERT_BUG(Span(), !emitter.out.empty(), StringView("Interpolated macro fragment emitted no tokens"));
        const auto replacementCount = emitter.out.size();
        *it = std::move(emitter.out.front());
        ++it;
        if (replacementCount > 1) {
            it = contents.insert(it, std::make_move_iterator(emitter.out.begin() + 1), std::make_move_iterator(emitter.out.end()));
            it += replacementCount - 1;
        }
    }
}

// TODO: Also count the number of times each variable is used?

MacroRulesPtr ParseMacroRules(TokenStream& lex) {
    TRACE_FUNCTION_F(StringView(""));
    Token tok;

    std::vector<MacroRule> rules;
    while (lex.lookahead(0) != TOK_EOF && lex.lookahead(0) != TOK_BRACE_CLOSE) {
        rules.push_back(ParseMacroRulesVar(lex));
        GET_TOK(tok, lex);
        if (tok.type() != TOK_SEMICOLON && tok.type() != TOK_COMMA) {
            PUTBACK(tok, lex);
            break;
        }
    }
    GET_TOK(tok, lex);
    if (tok.type() != TOK_EOF && tok.type() != TOK_BRACE_CLOSE) {
        parseErrorUnexpected(lex, tok, {TOK_EOF, TOK_BRACE_CLOSE});
    }

    DEBUG(StringView("- ") << rules.size() << StringView(" rules"));
    auto rv = makeMrPtr(lex);
    for (auto& rule : rules) {
        rv->rules.push_back(ParseMacroRulesMakeArm(rule.patSpan, mv$(rule.pattern), mv$(rule.contents)));
    }

    return rv;
}

MacroRulesPtr ParseMacroRulesSingleArm(TokenStream& lex) {
    TRACE_FUNCTION_F(StringView(""));
    Token tok;

    RuleParseState state;

    auto ps = lex.startSpan();
    GET_CHECK_TOK(tok, lex, TOK_PAREN_OPEN);
    auto armPat = ParseMacroRulesPat(lex, TOK_PAREN_OPEN, TOK_PAREN_CLOSE, state);
    auto patSpan = lex.endSpan(ps);
    GET_CHECK_TOK(tok, lex, TOK_BRACE_OPEN);
    // TODO: Pass a flag that annotates all idents with the current module?
    auto body = ParseMacroRulesCont(lex, TOK_BRACE_OPEN, TOK_BRACE_CLOSE, state);
    ASSERT_BUG(lex.pointSpan(), lex.parseState().wb, StringView("Macro parser has no WireBoard"));
    MacroRulesNormaliseFragments(*lex.parseState().wb, body);

    auto rv = makeMrPtr(lex);
    rv->rules.push_back(ParseMacroRulesMakeArm(patSpan, std::move(armPat), std::move(body)));
    return rv;
}

MacroPatEnt::MacroPatEnt()
    : tok(TOK_NULL)
    , type(PAT_TOKEN)
{
}

MacroPatEnt::MacroPatEnt(Span sp, Token tok)
    : sp(mv$(sp))
    , tok(mv$(tok))
    , type(PAT_TOKEN)
{
}

MacroPatEnt::MacroPatEnt(Span sp, RcString name, unsigned int nameIndex, Type type)
    : sp(mv$(sp))
    , name(mv$(name))
    , nameIndex(nameIndex)
    , tok()
    , type(type)
{
}

MacroPatEnt::MacroPatEnt(Span sp, Token sep, const char* op, unsigned index, std::vector<MacroPatEnt> ents)
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

MacroRulesArm::MacroRulesArm(std::vector<SimplePatEnt> pattern, std::vector<MacroExpansionEnt> contents)
    : pattern(mv$(pattern))
    , contents(mv$(contents))
{
}

MacroRules::MacroRules(u32& id, RcString sourceCrate, ASTEdition edition)
    : definitionId(++id)
    , sourceCrate(std::move(sourceCrate))
    , edition(edition)
{
}



#include "macro_rules_capture_tu.cpp"

ParameterMappings::ParameterMappings()
    : layerCount_(0)
{
}

auto ParameterMappings::mappings() const -> const std::vector<CapturedVar>& {
    return mappings_;
    DEBUG(StringView("m_mappings = {") << mappings_ << StringView("}"));
}

auto ParameterMappings::layerCount() const -> size_t {
    return layerCount_ + 1;
}

auto ParameterMappings::setLoopCounts(loopCountsT loopCounts) -> void {
    this->loopCounts = std::move(loopCounts);
}

MacroPatternStream::MacroPatternStream(const std::vector<SimplePatEnt>& ents, const std::vector<bool>* conditionReplay)
    : simpleEnts(ents)
    , endEnt(SimplePatEnt::make_End({}))
    , curPos_(0)
    , lastWasCond(false)
    , conditionReplay(conditionReplay)
    , conditionReplayPos(0)
{
}

auto MacroPatternStream::curPos() const -> size_t {
    return curPos_;
}

auto MacroPatternStream::peek() -> const SimplePatEnt& {
    if (!peekCacheValid) {
        peekCache = &next();
        peekCacheValid = true;
    }
    return *peekCache;
}

auto MacroPatternStream::getLoopIters() const -> const std::vector<unsigned int>& {
    return loopIterations;
}

auto MacroPatternStream::takeHistory() -> std::vector<bool> {
    return std::move(conditionHistory);
}

auto MacroPatternStream::takeLoopCounts() -> loopCountsT {
    return std::move(loopCounts);
}

MacroExpandState::MacroExpandState(const std::vector<MacroExpansionEnt>& contents, const ParameterMappings& mappings)
    : rootContents(contents)
    , mappings_(mappings)
    , offsets({{0, 0, 0}})
    , curEnts(&rootContents)
{
}

auto MacroExpandState::iterations() const -> const std::vector<unsigned int> {
    return iterations_;
}

auto MacroExpandState::loopIndexAt(unsigned int depth) const -> unsigned int {
    if (depth >= iterations_.size()) {
        return ~0u;
    }
    return iterations_[iterations_.size() - 1 - depth];
}

auto MacroExpandState::loopLengthAt(unsigned int depth) const -> unsigned int {
    if (depth + 1 >= offsets.size()) {
        return ~0u;
    }
    return offsets[offsets.size() - 1 - depth].maxIndex;
}

auto MacroExpandState::topPos() const -> unsigned int {
    if (offsets.empty()) {
        return 0;
    }
    return offsets[0].readPos;
}

MacroExpander::MacroExpander(u32& id, ObjPool& pool, const RcString& macroName, const Span& sp, ASTEdition edition, bool isMacroItem, bool transparent, unsigned int definitionId, const Ident::Hygiene& parentHygiene, const std::vector<MacroExpansionEnt>& contents, ParameterMappings mappings, RcString crateName, ASTEdition sourceEdition)
    : TokenStream(ParseState())
    , pool(pool)
    , thisSpan(sp, crateName, macroName)
    , crateName(mv$(crateName))
    , invocationSpan(sp)
    , invocationEdition(edition)
    , mappings_(mv$(mappings))
    , state(contents, mappings_)
    , sourceEdition(sourceEdition)
    , isMacroItem(isMacroItem)
    , transparent(transparent)
    , hygiene_(Ident::Hygiene::newScopeChained(id, pool, parentHygiene, definitionId, isMacroItem))
    , lastHygiene(hygiene_)
{
}

auto MacroExpander::outerSpan() const -> Span {
    return invocationSpan;
}

TokenStreamRO::TokenStreamRO(const TokenTree& tt)
    : tt(tt)
    , activeOffset(0)
    , eofToken(TOK_EOF)
    , consumeCount(0)
{
    BUG_ASSERT(!tt.isToken());
    if (tt.size() == 0) {
        activeOffset = 0;
        DEBUG(StringView("TOK_EOF"));
    } else {
        const auto* curTree = &tt;
        while (!curTree->isToken()) {
            curTree = &(*curTree)[0];
            offsets.push_back(0);
        }
        BUG_ASSERT(offsets.size() > 0);
        offsets.pop_back();
        activeOffset = 0;
        DEBUG(nextTok());
    }
}

auto TokenStreamRO::clone() const -> TokenStreamRO {
    return TokenStreamRO(*this);
}

auto TokenStreamRO::next() const -> enum eTokenType {
    return nextTok().type();
}

auto TokenStreamRO::nextTok() const -> const Token& {
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

auto TokenStreamRO::consume() -> void {
    if (fakedNext.type() != TOK_NULL) {
        fakedNext = Token(TOK_NULL);
        return;
    }

    if (offsets.empty() && activeOffset == tt.size()) {
        BUG(Span(), StringView("Attempting to consume EOS"));
    }
    DEBUG(consumeCount << StringView(" ") << nextTok());
    consumeCount++;
    for (;;) {
        const auto* curTree = &tt;
        for (auto idx : offsets) {
            curTree = &(*curTree)[idx];
        }

        activeOffset++;
        if (activeOffset == curTree->size()) {
            if (offsets.empty()) {
                return;
            }
            activeOffset = offsets.back();
            offsets.pop_back();
        } else {
            while (!(*curTree)[activeOffset].isToken()) {
                curTree = &(*curTree)[activeOffset];
                offsets.push_back(activeOffset);
                activeOffset = 0;
            }
            DEBUG(StringView("-> ") << nextTok());
            return;
        }
    }
}

auto TokenStreamRO::consumeAndPush(eTokenType ty) -> void {
    consume();
    fakedNext = Token(ty);
}

auto TokenStreamRO::consumeIf(eTokenType ty) -> bool {
    if (next() == ty) {
        consume();
        return true;
    } else {
        return false;
    }
}

auto TokenStreamRO::position() const -> size_t {
    return consumeCount;
}

MacroRule::MacroRule() {
}

RuleParseState::RuleParseState()
    : names()
    , nextNameIndex(0)
    , nextLoopIndex(0)
    , loopStack()
{
}

auto RuleParseState::addName(const Ident& ident) -> unsigned {
    unsigned idx = this->nextNameIndex++;
    auto& list = this->names[ident.name];
    for (const auto& e : list) {
        BUG_ASSERT(e.hygiene != ident.hygiene);
    }
    DEBUG(ident.name << StringView(" #") << idx << StringView(" @ [") << loopStack << StringView("]"));
    list.push_back(NameState{idx, this->loopStack, ident.hygiene});
    return idx;
}

auto RuleParseState::findName(const Ident& ident) const -> const NameState* {
    auto it = this->names.find(ident.name);
    if (it == this->names.end() || it->second.empty()) {
        return nullptr;
    }
    for (const auto& e : it->second) {
        if (e.hygiene == ident.hygiene) {
            return &e;
        }
    }
    return &it->second.front();
}

auto RuleParseState::openLoop() -> unsigned {
    auto rv = nextLoopIndex++;
    loopStack.push_back(rv);
    return rv;
}

auto RuleParseState::closeLoop() -> void {
    BUG_ASSERT(!loopStack.empty());
    loopStack.pop_back();
}

ContentLoopVariableUse::ContentLoopVariableUse(std::vector<unsigned> loopStack)
    : loopStack(std::move(loopStack))
    , isOptional(true)
{
}

ExpTok::ExpTok(MacroPatEnt::Type ty, const Token& tok)
    : ty(ty)
    , tok(tok)
{
}

auto ExpTok::operator==(const ExpTok& t) const -> bool {
    return this->ty == t.ty && (this->ty != MacroPatEnt::PAT_TOKEN || this->tok == t.tok);
}

auto ExpTok::operator!=(const ExpTok& t) const -> bool {
    return !(*this == t);
}

auto ExpTok::operator==(eTokenType tt) const -> bool {
    return this->ty == MacroPatEnt::PAT_TOKEN && this->tok == tt;
}

namespace stl {
template <>
void output<ZeroCopyOutput, CapturedVal>(ZeroCopyOutput& out, const CapturedVal& value) {
    operator<<(out, value);
}

template <>
void output<ZeroCopyOutput, CaptureLayer>(ZeroCopyOutput& out, const CaptureLayer& value) {
    operator<<(out, value);
}

template <>
void output<ZeroCopyOutput, ParameterMappings::CapturedVar>(ZeroCopyOutput& out, const ParameterMappings::CapturedVar& value) {
    operator<<(out, value);
}

template <>
void output<ZeroCopyOutput, ContentLoopVariableUse>(ZeroCopyOutput& out, const ContentLoopVariableUse& value) {
    operator<<(out, value);
}

template <>
void output<ZeroCopyOutput, ExpTok>(ZeroCopyOutput& os, const ExpTok& t) {
        os << StringView("ExpTok(") << t.ty << StringView(" ") << t.tok << StringView(")");
        return;
    }

template <>
void output<ZeroCopyOutput, MacroPatEnt>(ZeroCopyOutput& os, const MacroPatEnt& x) {
    switch (x.type) {
        case MacroPatEnt::PAT_TOKEN:
            os << StringView("=") << x.tok;
            break;
        case MacroPatEnt::PAT_LOOP:
            os << StringView("loop #") << x.nameIndex << x.name << StringView(" w/ ") << x.tok << StringView(" [") << x.subpats << StringView("]");
            break;
        default:
            os << StringView("$") << x.name << StringView(":");
            switch (x.type) {
                case MacroPatEnt::PAT_TOKEN:
                    UNREACHABLE();
                case MacroPatEnt::PAT_LOOP:
                    UNREACHABLE();
                case MacroPatEnt::PAT_TT:
                    os << StringView("tt");
                    break;
                case MacroPatEnt::PAT_PAT:
                    os << StringView("pat");
                    break;
                case MacroPatEnt::PAT_PAT_PARAM:
                    os << StringView("pat_param");
                    break;
                case MacroPatEnt::PAT_IDENT:
                    os << StringView("ident");
                    break;
                case MacroPatEnt::PAT_PATH:
                    os << StringView("path");
                    break;
                case MacroPatEnt::PAT_TYPE:
                    os << StringView("type");
                    break;
                case MacroPatEnt::PAT_EXPR:
                    os << StringView("expr");
                    break;
                case MacroPatEnt::PAT_STMT:
                    os << StringView("stmt");
                    break;
                case MacroPatEnt::PAT_BLOCK:
                    os << StringView("block");
                    break;
                case MacroPatEnt::PAT_META:
                    os << StringView("meta");
                    break;
                case MacroPatEnt::PAT_ITEM:
                    os << StringView("item");
                    break;
                case MacroPatEnt::PAT_VIS:
                    os << StringView("vis");
                    break;
                case MacroPatEnt::PAT_LIFETIME:
                    os << StringView("lifetime");
                    break;
                case MacroPatEnt::PAT_LITERAL:
                    os << StringView("literal");
                    break;
            }
            break;
    }
    return;
}

template <>
void output<ZeroCopyOutput, MacroPatEnt::Type>(ZeroCopyOutput& os, MacroPatEnt::Type x) {
    switch (x) {
        case MacroPatEnt::PAT_TOKEN:
            os << StringView("PAT_TOKEN");
            break;
        case MacroPatEnt::PAT_LOOP:
            os << StringView("PAT_LOOP");
            break;
        case MacroPatEnt::PAT_TT:
            os << StringView("PAT_TT");
            break;
        case MacroPatEnt::PAT_PAT:
            os << StringView("PAT_PAT");
            break;
        case MacroPatEnt::PAT_PAT_PARAM:
            os << StringView("PAT_PAT_PARAM");
            break;
        case MacroPatEnt::PAT_IDENT:
            os << StringView("PAT_IDENT");
            break;
        case MacroPatEnt::PAT_PATH:
            os << StringView("PAT_PATH");
            break;
        case MacroPatEnt::PAT_TYPE:
            os << StringView("PAT_TYPE");
            break;
        case MacroPatEnt::PAT_EXPR:
            os << StringView("PAT_EXPR");
            break;
        case MacroPatEnt::PAT_STMT:
            os << StringView("PAT_STMT");
            break;
        case MacroPatEnt::PAT_BLOCK:
            os << StringView("PAT_BLOCK");
            break;
        case MacroPatEnt::PAT_META:
            os << StringView("PAT_META");
            break;
        case MacroPatEnt::PAT_ITEM:
            os << StringView("PAT_ITEM");
            break;
        case MacroPatEnt::PAT_VIS:
            os << StringView("PAT_VIS");
            break;
        case MacroPatEnt::PAT_LIFETIME:
            os << StringView("PAT_LIFETIME");
            break;
        case MacroPatEnt::PAT_LITERAL:
            os << StringView("PAT_LITERAL");
            break;
    }
    return;
}

template <>
void output<ZeroCopyOutput, SimplePatEnt>(ZeroCopyOutput& os, const SimplePatEnt& x) {
    switch (x.tag()) {
        case SimplePatEnt::TAG_End: {
            os << StringView("End");
            break;
        }
        case SimplePatEnt::TAG_LoopStart: {
            auto& e = x.as_LoopStart();
            os << StringView("LoopStart(") << e.index << StringView(")");
            break;
        }
        case SimplePatEnt::TAG_LoopNext: {
            os << StringView("LoopNext");
            break;
        }
        case SimplePatEnt::TAG_LoopEnd: {
            os << StringView("LoopEnd");
            break;
        }
        case SimplePatEnt::TAG_Jump: {
            auto& e = x.as_Jump();
            os << StringView("Jump(->") << e.jumpTarget << StringView(")");
            break;
        }
        case SimplePatEnt::TAG_ExpectTok: {
            auto& e = x.as_ExpectTok();
            os << StringView("Expect(") << e << StringView(")");
            break;
        }
        case SimplePatEnt::TAG_ExpectPat: {
            auto& e = x.as_ExpectPat();
            os << StringView("Expect($") << e.idx << StringView(" = ") << e.type << StringView(")");
            break;
        }
        case SimplePatEnt::TAG_If: {
            auto& e = x.as_If();
            os << StringView("If(") << (e.isEqual ? "=" : "!=") << StringView("[");
            for (const auto& p : e.ents) {
                if (p.ty == MacroPatEnt::PAT_TOKEN) {
                    os << p.tok;
                } else {
                    os << p.ty;
                }
                os << StringView(", ");
            }
            os << StringView("] ->") << e.jumpTarget << StringView(")");
            break;
        }
    }
    return;
}

template <>
void output<ZeroCopyOutput, MacroExpansionEnt>(ZeroCopyOutput& os, const MacroExpansionEnt& x) {
    switch (x.tag()) {
        case MacroExpansionEnt::TAG_Token: {
            auto& e = x.as_Token();
            os << StringView("=") << e;
            break;
        }
        case MacroExpansionEnt::TAG_NamedValue: {
            auto& e = x.as_NamedValue();
            switch (e & ~NAMEDVALUE_VALMASK) {
                case 0:
                    os << StringView("$") << e;
                    break;
                case NAMEDVALUE_TY_COUNT:
                    os << StringView("${count(...)}");
                    break;
                default:
                    os << StringView("$?") << e;
            }
            break;
        }
        case MacroExpansionEnt::TAG_Concat: {
            os << StringView("${concat(...)}");
            break;
        }
        case MacroExpansionEnt::TAG_Loop: {
            auto& e = x.as_Loop();
            os << StringView("${") << e.controllingInputLoops << StringView("}(") << e.entries << StringView(") ") << e.joiner;
            break;
        }
    }
    return;
}

template <>
void output<ZeroCopyOutput, SimplePatIfCheck>(ZeroCopyOutput& os, const SimplePatIfCheck& x) {
    os << x.ty;
    if (x.ty == MacroPatEnt::PAT_TOKEN) {
        os << StringView(":") << x.tok;
    }
    return;
}

template <>
void output<ZeroCopyOutput, std::vector<MacroPatEnt>>(ZeroCopyOutput& out, const std::vector<MacroPatEnt>& values) {
    outCont(out, values);
}

template <>
void output<ZeroCopyOutput, std::vector<SimplePatIfCheck>>(ZeroCopyOutput& out, const std::vector<SimplePatIfCheck>& values) {
    outCont(out, values);
}

template <>
void output<ZeroCopyOutput, std::vector<MacroExpansionEnt>>(ZeroCopyOutput& out, const std::vector<MacroExpansionEnt>& values) {
    outCont(out, values);
}

template <>
void output<ZeroCopyOutput, std::vector<SimplePatEnt>>(ZeroCopyOutput& out, const std::vector<SimplePatEnt>& values) {
    outCont(out, values);
}

template <>
void output<ZeroCopyOutput, std::vector<CapturedVal>>(ZeroCopyOutput& out, const std::vector<CapturedVal>& values) {
    outCont(out, values);
}

template <>
void output<ZeroCopyOutput, std::vector<CaptureLayer>>(ZeroCopyOutput& out, const std::vector<CaptureLayer>& values) {
    outCont(out, values);
}

template <>
void output<ZeroCopyOutput, std::vector<ParameterMappings::CapturedVar>>(ZeroCopyOutput& out, const std::vector<ParameterMappings::CapturedVar>& values) {
    outCont(out, values);
}

template <>
void output<ZeroCopyOutput, std::pair<const unsigned int, ContentLoopVariableUse>>(ZeroCopyOutput& out, const std::pair<const unsigned int, ContentLoopVariableUse>& value) {
    out << value.first << StringView(": ") << value.second;
}

template <>
void output<ZeroCopyOutput, std::map<unsigned int, ContentLoopVariableUse>>(ZeroCopyOutput& out, const std::map<unsigned int, ContentLoopVariableUse>& values) {
    outCont(out, values);
}

template <>
void output<ZeroCopyOutput, std::vector<ExpTok>>(ZeroCopyOutput& out, const std::vector<ExpTok>& values) {
    outCont(out, values);
}
}
