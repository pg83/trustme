#pragma once

#include <functional>
#include <iosfwd>
#include <string>
#include <vector>

struct Span;
class TokenStream;

enum class CfgLintLevel {
    Allow,
    Warn,
    ForceWarn,
    Deny,
    Forbid,
};

namespace AST {
    class Attribute;
    class AttributeList;
}

extern void CfgDump(::std::ostream& os);
extern void CfgSetFlag(::std::string name);
extern void CfgSetValue(::std::string name, ::std::string val);
extern void CfgSetValueCb(::std::string name, ::std::function<bool(const ::std::string&)> cb);
extern bool CfgParseOption(const ::std::string& spec, ::std::string& name, bool& has_value, ::std::string& value, ::std::string& error);
extern bool CfgSetCheckSpec(const ::std::string& spec, ::std::string& error);
extern void CfgSetLintLevel(::std::string name, CfgLintLevel level);
extern void CfgSetLintCap(CfgLintLevel level);
extern bool check_cfg_attrs(const ::AST::AttributeList& attrs);
extern bool check_cfg(const Span& sp, const ::AST::Attribute& mi);
/// Check a parenthesised list of cfg rules (treated as `all()`)
extern bool check_cfg_stream(TokenStream& lex);
/// Parse an attribute from a `cfg_attr()` attribute. Returns with an empty name if check failed
extern std::vector<AST::Attribute> check_cfg_attr(const ::AST::Attribute& mi);
