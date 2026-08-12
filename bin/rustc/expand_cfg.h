#pragma once

#include <iosfwd>
#include <string>
#include <vector>
#include <functional>

struct Span;
class TokenStream;

enum class CfgLintLevel {
    Allow,
    Warn,
    ForceWarn,
    Deny,
    Forbid,
};

class ASTAttribute;
class ASTAttributeList;

extern void CfgDump(::std::ostream& os);
extern void CfgSetFlag(::std::string name);
extern void CfgSetValue(::std::string name, ::std::string val);
extern void CfgSetValueCb(::std::string name, ::std::function<bool(const ::std::string&)> cb);
extern bool CfgParseOption(const ::std::string& spec, ::std::string& name, bool& hasValue, ::std::string& value, ::std::string& error);
extern bool CfgSetCheckSpec(const ::std::string& spec, ::std::string& error);
extern void CfgSetLintLevel(::std::string name, CfgLintLevel level);
extern void CfgSetLintCap(CfgLintLevel level);
extern bool checkCfgAttrs(const ASTAttributeList& attrs);
extern bool checkCfg(const Span& sp, const ASTAttribute& mi);
/// Check a parenthesised list of cfg rules (treated as `all()`)
extern bool checkCfgStream(TokenStream& lex);
/// Parse an attribute from a `cfg_attr()` attribute. Returns with an empty name if check failed
extern std::vector<ASTAttribute> checkCfgAttr(const ASTAttribute& mi);
