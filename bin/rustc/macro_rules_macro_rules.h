#pragma once

#include "parse_lex.h"
#include "parse_tokentree.h"
#include "common.h"
#include <map>
#include <memory>
#include <cstring>
#include "macro_rules_macro_rules_ptr.h"
#include <set>

class MacroExpander;
class SimplePatEnt;

TAGGED_UNION(MacroExpansionConcatEnt, Named, (Named, unsigned int), (Ident, Ident));
TAGGED_UNION(
    MacroExpansionEnt,
    Token,
    // TODO: have a "raw" stream instead of just tokens
    (Token, Token),
    // TODO: Have a flag on `NamedValue` that indicates that it is the only/last usage of this particular value (at this level)
    // NOTE: This is a 2:30 bitfield - with the high range indicating $crate
    (NamedValue, unsigned int),
    (Concat, std::vector<MacroExpansionConcatEnt>),
    (Loop, struct {
        /// Contained entries
        ::std::vector<MacroExpansionEnt> entries;
        /// Token used to join iterations
        Token joiner;
        /// List of loop indexes that control this loop
        ::std::set<unsigned int> controllingInputLoops;
    })
);
extern ::std::ostream& operator<<(::std::ostream& os, const MacroExpansionEnt& x);
static const unsigned int NAMEDVALUE_VALMASK = ((1 << 30) - 1);
static const unsigned int NAMEDVALUE_TY_MAGIC = 1 << 30;
static const unsigned int NAMEDVALUE_MAGIC_CRATE = NAMEDVALUE_TY_MAGIC | 0;
static const unsigned int NAMEDVALUE_MAGIC_INDEX = NAMEDVALUE_TY_MAGIC | 1;
static const unsigned int NAMEDVALUE_TY_IGNORE = 2 << 30;
static const unsigned int NAMEDVALUE_TY_COUNT = 3 << 30;

/// Matching pattern entry
struct MacroPatEnt {
    Span sp;
    RcString name;
    unsigned int name_index = 0;
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
    } type;

    MacroPatEnt();

    // Literal token
    MacroPatEnt(Span sp, Token tok);

    // Variable reference
    MacroPatEnt(Span sp, RcString name, unsigned int name_index, Type type);

    // Loop/optional
    MacroPatEnt(Span sp, Token sep, const char* op, unsigned index, ::std::vector<MacroPatEnt> ents);

    friend ::std::ostream& operator<<(::std::ostream& os, const MacroPatEnt& x);
    friend ::std::ostream& operator<<(::std::ostream& os, const MacroPatEnt::Type& x);
};

struct SimplePatIfCheck {
    MacroPatEnt::Type ty; // If PAT_TOKEN, token is checked
    Token tok;

    bool operator==(const SimplePatIfCheck& x) const {
        return this->ty == x.ty && this->tok == x.tok;
    }

    friend ::std::ostream& operator<<(::std::ostream& os, const SimplePatIfCheck& x);
};

/// Simple pattern entry for macro_rules! arm patterns
TAGGED_UNION(
    SimplePatEnt,
    End,
    // End of the pattern stream (expects EOF, and terminates the match process)
    (End, struct {}),
    // Start a loop (pushes a zero count to the loop stack)
    (LoopStart, struct { unsigned index; }),
    // Increment loop iteration counter
    (LoopNext, struct {/*unsigned index;*/}),
    // Pop from the loop stack
    (LoopEnd, struct {/*unsigned index;*/}),
    // Jump to a new point of execution
    (Jump, struct { size_t jump_target; }),
    // Expect a specific token, erroring/failing the arm if nt met
    (ExpectTok, Token),
    // Expect a pattern match
    (ExpectPat,
     struct {
         MacroPatEnt::Type type;
         unsigned int idx;
     }),
    // Compare the head of the input stream and poke the pattern stream
    (If, struct {
        bool is_equal;
        size_t jump_target;
        ::std::vector<SimplePatIfCheck> ents;
    })
);

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
    static unsigned int gNextDefinitionId;

public:
    unsigned int definitionId;

    /// Marks if this macro should be exported from the defining crate
    bool exported = false;

    bool isMacroItem = false;

    /// Crate that defined this macro
    /// - Populated on deserialise if not already set
    RcString sourceCrate;
    AST::Edition edition;

    Ident::Hygiene mHygiene;
    // Lexical context at the macro definition, before the parser enters the
    // token-tree scope used to distinguish literal RHS tokens.
    Ident::Hygiene definitionHygiene;

    /// Expansion rules
    ::std::vector<MacroRulesArm> rules;

    MacroRules(RcString source_crate, AST::Edition edition);

    virtual ~MacroRules();
    MacroRules(MacroRules&&) = default;
};

extern ::std::unique_ptr<TokenStream> MacroInvokeRules(const RcString& name, const MacroRules& rules, const Span& sp, TokenTree input, const AST::Crate& crate, AST::Module& mod);

/// Parse a full `macro_rules` block
extern MacroRulesPtr ParseMacroRules(TokenStream& lex);
/// Parse a single-arm `macro` item ( `macro foo($name:ident) { $name }`)
extern MacroRulesPtr ParseMacroRulesSingleArm(TokenStream& lex);
