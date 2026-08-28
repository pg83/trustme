#pragma once

struct WireBoard;

#include "common.h"
#include "parse_lex.h"
#include "parse_tokentree.h"
#include "macro_rules_macro_rules_ptr.h"

#include <map>
#include <set>
#include <memory>
#include <cstring>

class MacroExpander;
class SimplePatEnt;
class MacroExpansionEnt;

extern ::std::ostream& operator<<(::std::ostream& os, const MacroExpansionEnt& x);
extern void MacroRulesNormaliseFragments(const WireBoard& wb, ::std::vector<MacroExpansionEnt>& contents);

enum : unsigned int {
    NAMEDVALUE_VALMASK = (1 << 30) - 1,
    NAMEDVALUE_TY_MAGIC = 1 << 30,
    NAMEDVALUE_MAGIC_CRATE = NAMEDVALUE_TY_MAGIC | 0,
    NAMEDVALUE_MAGIC_INDEX = NAMEDVALUE_TY_MAGIC | 1,

    NAMEDVALUE_MAGIC_INDEX_AT = NAMEDVALUE_TY_MAGIC | 0x100,
    NAMEDVALUE_MAGIC_LEN_AT = NAMEDVALUE_TY_MAGIC | 0x200,
    NAMEDVALUE_MAGIC_DEPTHMASK = 0xFF,
    NAMEDVALUE_TY_IGNORE = 2u << 30,
    NAMEDVALUE_TY_COUNT = 3u << 30,

    NAMEDVALUE_COUNT_DEPTHSHIFT = 22,
    NAMEDVALUE_COUNT_IDXMASK = (1u << NAMEDVALUE_COUNT_DEPTHSHIFT) - 1,
};

struct MacroPatEnt {
    Span sp;
    RcString name;
    unsigned int nameIndex = 0;
    // TODO: Include a point span for the token?
    Token tok;

    ::std::vector<MacroPatEnt> subpats;

    enum Type {
        PAT_TOKEN,
        PAT_LOOP,

        PAT_TT,
        PAT_PAT,
        PAT_IDENT,
        PAT_PATH,
        PAT_TYPE,
        PAT_EXPR,
        PAT_STMT,
        PAT_BLOCK,
        PAT_META,
        PAT_ITEM,
        PAT_VIS,
        PAT_LIFETIME,
        PAT_LITERAL,

        PAT_PAT_PARAM,
    } type;

    MacroPatEnt();

    MacroPatEnt(Span sp, Token tok);

    MacroPatEnt(Span sp, RcString name, unsigned int nameIndex, Type type);

    MacroPatEnt(Span sp, Token sep, const char* op, unsigned index, ::std::vector<MacroPatEnt> ents);

    friend ::std::ostream& operator<<(::std::ostream& os, const MacroPatEnt& x);
    friend ::std::ostream& operator<<(::std::ostream& os, const MacroPatEnt::Type& x);
};

struct SimplePatIfCheck {
    MacroPatEnt::Type ty;
    Token tok;

    bool isLocalAmbiguity() const {
        return ty != MacroPatEnt::PAT_TOKEN && tok == TOK_EOF;
    }

    void markLocalAmbiguity() {
        tok = Token(TOK_EOF);
    }

    bool operator==(const SimplePatIfCheck& x) const {
        return this->ty == x.ty && this->tok == x.tok;
    }

    friend ::std::ostream& operator<<(::std::ostream& os, const SimplePatIfCheck& x);
};

#include "macro_rules_macro_rules_tu.h"

extern ::std::ostream& operator<<(::std::ostream& os, const SimplePatEnt& x);

struct MacroRulesArm {
    ::std::vector<RcString> paramNames;

    ::std::vector<SimplePatEnt> pattern;

    ::std::vector<MacroExpansionEnt> contents;

    ~MacroRulesArm();

    MacroRulesArm();

    MacroRulesArm(::std::vector<SimplePatEnt> pattern, ::std::vector<MacroExpansionEnt> contents);

    MacroRulesArm(const MacroRulesArm&) = delete;
    MacroRulesArm& operator=(const MacroRulesArm&) = delete;
    MacroRulesArm(MacroRulesArm&&) = default;
    MacroRulesArm& operator=(MacroRulesArm&&) = default;
};

class MacroRules {
public:
    unsigned int definitionId;

    bool exported = false;

    bool isMacroItem = false;

    bool transparent = false;

    RcString sourceCrate;
    ASTEdition edition;

    Ident::Hygiene hygiene;

    Ident::Hygiene definitionHygiene;

    Span definitionSpan;

    ::std::vector<MacroRulesArm> rules;

    MacroRules(u32& id, RcString sourceCrate, ASTEdition edition);

    virtual ~MacroRules();
    MacroRules(MacroRules&&) = default;
};

extern ::std::unique_ptr<TokenStream> MacroInvokeRules(const RcString& name, const MacroRules& rules, const Span& sp, const WireBoard& wb, TokenTree input, const ASTCrate& crate, ASTModule& mod);

extern MacroRulesPtr ParseMacroRules(TokenStream& lex);

extern MacroRulesPtr ParseMacroRulesSingleArm(TokenStream& lex);
