#pragma once

#include <iosfwd>
#include <string>
#include <vector>
#include <functional>

struct Span;
struct Settings;
struct CfgState;
namespace stl {
    class ObjPool;
}
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

extern CfgState* CfgCreateState(stl::ObjPool& pool);
extern void CfgDump(const Settings& settings, ::std::ostream& os);
extern void CfgSetFlag(Settings& settings, ::std::string name);
extern void CfgSetValue(Settings& settings, ::std::string name, ::std::string val);
extern void CfgSetValueCb(Settings& settings, ::std::string name, ::std::function<bool(const ::std::string&)> cb);
extern bool CfgParseOption(const ::std::string& spec, ::std::string& name, bool& hasValue, ::std::string& value, ::std::string& error);
extern bool CfgSetCheckSpec(Settings& settings, const ::std::string& spec, ::std::string& error);
extern void CfgSetLintLevel(Settings& settings, ::std::string name, CfgLintLevel level);
extern void CfgSetLintCap(Settings& settings, CfgLintLevel level);
extern bool checkCfgAttrs(const Settings& settings, const ASTAttributeList& attrs);
extern bool checkCfg(const Settings& settings, const Span& sp, const ASTAttribute& mi);
/// Check a parenthesised list of cfg rules (treated as `all()`)
extern bool checkCfgStream(const Settings& settings, TokenStream& lex);
/// Parse an attribute from a `cfg_attr()` attribute. Returns with an empty name if check failed
extern std::vector<ASTAttribute> checkCfgAttr(const Settings& settings, const ASTAttribute& mi);
