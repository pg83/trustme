#include "macro_rules_pattern_checks.h"

#include "common.h"
#include "macro_rules_macro_rules.h"

#include <std/lib/vector.h>

using namespace stl;

namespace {
    using Ent = MacroPatEnt;
    using Ents = Vector<const Ent*>;

    bool isFragment(const Ent& e) {
        return e.type != Ent::PAT_TOKEN && e.type != Ent::PAT_LOOP;
    }

    bool firstOf(const Ent* ents, size_t count, Ents& out) {
        for (size_t i = 0; i < count; i++) {
            const auto& e = ents[i];
            if (e.type != Ent::PAT_LOOP) {
                out.pushBack(&e);
                return false;
            }
            const bool bodyIsEmpty = firstOf(e.subpats.data(), e.subpats.size(), out);
            if (e.name == "+" && !bodyIsEmpty) {
                return false;
            }
        }
        return true;
    }

    void followOf(const Ent* ents, size_t count, size_t i, const Ents& outer, Ents& out) {
        if (firstOf(ents + i + 1, count - i - 1, out)) {
            for (size_t k = 0; k < outer.length(); k++) {
                out.pushBack(outer[k]);
            }
        }
    }

    bool followAllowed(Ent::Type fragment, const Ent& next) {
        const bool nextIsFragment = isFragment(next);
        const auto tt = next.tok.type();
        if (!nextIsFragment && (tt == TOK_PAREN_CLOSE || tt == TOK_BRACE_CLOSE || tt == TOK_SQUARE_CLOSE)) {
            return true;
        }
        switch (fragment) {
            case Ent::PAT_EXPR:
            case Ent::PAT_STMT:
                return !nextIsFragment && (tt == TOK_FATARROW || tt == TOK_COMMA || tt == TOK_SEMICOLON);
            case Ent::PAT_PAT:
                return !nextIsFragment && (tt == TOK_FATARROW || tt == TOK_COMMA || tt == TOK_EQUAL || tt == TOK_RWORD_IF || tt == TOK_RWORD_IN);
            case Ent::PAT_PAT_PARAM:
                return !nextIsFragment && (tt == TOK_FATARROW || tt == TOK_COMMA || tt == TOK_EQUAL || tt == TOK_PIPE || tt == TOK_RWORD_IF || tt == TOK_RWORD_IN);
            case Ent::PAT_PATH:
            case Ent::PAT_TYPE:
                if (nextIsFragment) {
                    return next.type == Ent::PAT_BLOCK;
                }
                switch (tt) {
                    case TOK_FATARROW:
                    case TOK_COMMA:
                    case TOK_EQUAL:
                    case TOK_PIPE:
                    case TOK_SEMICOLON:
                    case TOK_COLON:
                    case TOK_GT:
                    case TOK_DOUBLE_GT:
                    case TOK_SQUARE_OPEN:
                    case TOK_BRACE_OPEN:
                    case TOK_RWORD_AS:
                    case TOK_RWORD_WHERE:
                        return true;
                    default:
                        return false;
                }
            case Ent::PAT_VIS:
                if (nextIsFragment) {
                    return next.type == Ent::PAT_IDENT || next.type == Ent::PAT_TYPE || next.type == Ent::PAT_PATH;
                }
                if (tt == TOK_COMMA) {
                    return true;
                }
                if (tt == TOK_IDENT || (TOK_RWORD_PUB <= tt && tt <= TOK_RWORD_TRY)) {
                    return tt != TOK_RWORD_PRIV;
                }
                return isTokenType(tt);
            default:
                return true;
        }
    }

    const char* fragmentName(Ent::Type type) {
        switch (type) {
            case Ent::PAT_TOKEN:
            case Ent::PAT_LOOP:
                return "";
            case Ent::PAT_TT:
                return "tt";
            case Ent::PAT_PAT:
                return "pat";
            case Ent::PAT_PAT_PARAM:
                return "pat_param";
            case Ent::PAT_IDENT:
                return "ident";
            case Ent::PAT_PATH:
                return "path";
            case Ent::PAT_TYPE:
                return "ty";
            case Ent::PAT_EXPR:
                return "expr";
            case Ent::PAT_STMT:
                return "stmt";
            case Ent::PAT_BLOCK:
                return "block";
            case Ent::PAT_META:
                return "meta";
            case Ent::PAT_ITEM:
                return "item";
            case Ent::PAT_VIS:
                return "vis";
            case Ent::PAT_LIFETIME:
                return "lifetime";
            case Ent::PAT_LITERAL:
                return "literal";
        }
        return "";
    }

    bool isRestricted(Ent::Type type) {
        switch (type) {
            case Ent::PAT_EXPR:
            case Ent::PAT_STMT:
            case Ent::PAT_PAT:
            case Ent::PAT_PAT_PARAM:
            case Ent::PAT_PATH:
            case Ent::PAT_TYPE:
            case Ent::PAT_VIS:
                return true;
            default:
                return false;
        }
    }

    const char* allowedAfter(Ent::Type type) {
        switch (type) {
            case Ent::PAT_EXPR:
            case Ent::PAT_STMT:
                return "`=>`, `,` or `;`";
            case Ent::PAT_PAT:
                return "`=>`, `,`, `=`, `if` or `in`";
            case Ent::PAT_PAT_PARAM:
                return "`=>`, `,`, `=`, `|`, `if` or `in`";
            case Ent::PAT_PATH:
            case Ent::PAT_TYPE:
                return "`=>`, `,`, `=`, `|`, `;`, `:`, `>`, `>>`, `[`, `{`, `as`, `where` or a `block` fragment";
            case Ent::PAT_VIS:
                return "`,`, an identifier other than `priv`, a token that begins a type, or an "
                       "`ident`, `ty` or `path` fragment";
            default:
                return "";
        }
    }

    struct Described {
        const Ent& e;
    };

    ::std::ostream& operator<<(::std::ostream& os, const Described& x) {
        if (isFragment(x.e)) {
            return os << "`$" << x.e.name << ":" << fragmentName(x.e.type) << "`";
        }
        return os << "`" << x.e.tok << "`";
    }

    void checkRun(const Ent* ents, size_t count, const Ents& outer) {
        for (size_t i = 0; i < count; i++) {
            const auto& e = ents[i];
            Ents follow;
            followOf(ents, count, i, outer, follow);
            if (e.type == Ent::PAT_LOOP) {
                Ent separator(e.sp, e.tok.clone());
                Ents inner;
                if (e.tok.type() != TOK_NULL) {
                    inner.pushBack(&separator);
                }
                for (size_t k = 0; k < follow.length(); k++) {
                    inner.pushBack(follow[k]);
                }
                checkRun(e.subpats.data(), e.subpats.size(), inner);
                continue;
            }
            if (!isRestricted(e.type)) {
                continue;
            }
            for (size_t k = 0; k < follow.length(); k++) {
                if (!followAllowed(e.type, *follow[k])) {
                    ERROR(e.sp, E0000, "`$" << e.name << ":" << fragmentName(e.type) << "` may be followed by " << Described{*follow[k]} << ", which is not allowed for `" << fragmentName(e.type) << "` fragments -- allowed there are " << allowedAfter(e.type));
                }
            }
        }
    }
}

void MacroRulesCheckFollowSets(const MacroPatEnt* ents, size_t count) {
    Ents none;
    checkRun(ents, count, none);
}
