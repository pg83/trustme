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
    // `${index(depth)}` and `${len(depth)}` encode depth in the low byte.
    NAMEDVALUE_MAGIC_INDEX_AT = NAMEDVALUE_TY_MAGIC | 0x100,
    NAMEDVALUE_MAGIC_LEN_AT = NAMEDVALUE_TY_MAGIC | 0x200,
    NAMEDVALUE_MAGIC_DEPTHMASK = 0xFF,
    NAMEDVALUE_TY_IGNORE = 2u << 30,
    NAMEDVALUE_TY_COUNT = 3u << 30,
    // `${count($x, depth)}` keeps the depth above the variable index.
    NAMEDVALUE_COUNT_DEPTHSHIFT = 22,
    NAMEDVALUE_COUNT_IDXMASK = (1u << NAMEDVALUE_COUNT_DEPTHSHIFT) - 1,
};

/// Matching pattern entry
struct MacroPatEnt {
    Span sp;
    RcString name;
    unsigned int nameIndex = 0;
    // TODO: Include a point span for the token?
    Token tok;

    ::std::vector<MacroPatEnt> subpats;

    enum Type {
        PAT_TOKEN, // A token
        PAT_LOOP,  // $() Enables use of subpats

        PAT_TT,  // :tt
        PAT_PAT, // :pat
        PAT_IDENT,
        PAT_PATH,
        PAT_TYPE,
        PAT_EXPR,
        PAT_STMT,
        PAT_BLOCK,
        PAT_META,
        PAT_ITEM, // :item
        PAT_VIS,
        PAT_LIFETIME,
        PAT_LITERAL,
        // Kept at the end because these values are stored in crate metadata.
        PAT_PAT_PARAM, // :pat_param, and :pat in pre-2021 macro definitions
    } type;

    MacroPatEnt();

    // Literal token
    MacroPatEnt(Span sp, Token tok);

    // Variable reference
    MacroPatEnt(Span sp, RcString name, unsigned int nameIndex, Type type);

    // Loop/optional
    MacroPatEnt(Span sp, Token sep, const char* op, unsigned index, ::std::vector<MacroPatEnt> ents);

    friend ::std::ostream& operator<<(::std::ostream& os, const MacroPatEnt& x);
    friend ::std::ostream& operator<<(::std::ostream& os, const MacroPatEnt::Type& x);
};

struct SimplePatIfCheck {
    MacroPatEnt::Type ty; // If PAT_TOKEN, token is checked
    Token tok;

    // Fragment checks otherwise carry TOK_NULL. EOF marks a fragment that can
    // start both a repetition and its continuation at the same input token.
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

/// Simple pattern entry for macro_rules! arm patterns
// Definitions generated from macro_rules_macro_rules.tu.
#include "macro_rules_macro_rules_tu.h"

extern ::std::ostream& operator<<(::std::ostream& os, const SimplePatEnt& x);

/// An expansion arm within a macro_rules! blcok
struct MacroRulesArm {
    /// Names for the parameters
    ::std::vector<RcString> paramNames;

    /// Patterns
    ::std::vector<SimplePatEnt> pattern;

    /// Rule contents
    ::std::vector<MacroExpansionEnt> contents;

    ~MacroRulesArm();

    MacroRulesArm();

    MacroRulesArm(::std::vector<SimplePatEnt> pattern, ::std::vector<MacroExpansionEnt> contents);

    MacroRulesArm(const MacroRulesArm&) = delete;
    MacroRulesArm& operator=(const MacroRulesArm&) = delete;
    MacroRulesArm(MacroRulesArm&&) = default;
    MacroRulesArm& operator=(MacroRulesArm&&) = default;
};

/// A sigle 'macro_rules!' block
class MacroRules {
public:
    unsigned int definitionId;

    /// Marks if this macro should be exported from the defining crate
    bool exported = false;

    bool isMacroItem = false;

    /// `#[rustc_macro_transparency = "transparent"]`: the expansion's own names
    /// behave as if written at the call site, so `mir!` can declare a local that
    /// the caller's tokens then assign to.
    bool transparent = false;

    /// Crate that defined this macro
    /// - Populated on deserialise if not already set
    RcString sourceCrate;
    ASTEdition edition;

    Ident::Hygiene hygiene;
    // Lexical context at the macro definition, before the parser enters the
    // token-tree scope used to distinguish literal RHS tokens.
    Ident::Hygiene definitionHygiene;

    // Source position of a local definition. Deserialised macros leave this
    // empty: their visibility is determined by the importing module instead.
    Span definitionSpan;

    /// Expansion rules
    ::std::vector<MacroRulesArm> rules;

    MacroRules(u32& id, RcString sourceCrate, ASTEdition edition);

    virtual ~MacroRules();
    MacroRules(MacroRules&&) = default;
};

extern ::std::unique_ptr<TokenStream> MacroInvokeRules(const RcString& name, const MacroRules& rules, const Span& sp, const WireBoard& wb, TokenTree input, const ASTCrate& crate, ASTModule& mod);

/// Parse a full `macro_rules` block
extern MacroRulesPtr ParseMacroRules(TokenStream& lex);
/// Parse a single-arm `macro` item ( `macro foo($name:ident) { $name }`)
extern MacroRulesPtr ParseMacroRulesSingleArm(TokenStream& lex);
