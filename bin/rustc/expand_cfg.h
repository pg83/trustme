#pragma once

#include "settings.h"

#include <iosfwd>
#include <string>
#include <vector>
#include <std/mem/obj_pool.h>

struct Span;
struct CfgState;

class TokenStream;


class ASTAttribute;
class ASTAttributeList;

extern CfgState* CfgCreateState(stl::ObjPool& pool);

using CfgString = ::std::string;

struct CfgValueCallback {
    virtual bool matches(const CfgString& value) = 0;
    virtual CfgValueCallback* cloneIn(stl::ObjPool& pool) const = 0;
};

template <typename F>
struct CfgValueCb final: CfgValueCallback {
    F f;

    explicit CfgValueCb(F f)
        : f(f)
    {
    }

    bool matches(const CfgString& value) override {
        return f(value);
    }

    CfgValueCallback* cloneIn(stl::ObjPool& pool) const override {
        return pool.make<CfgValueCb<F>>(f);
    }
};

extern void CfgSetValueCallback(Settings& settings, CfgString name, const CfgValueCallback& cb);

template <typename F>
void CfgSetValueCb(Settings& settings, CfgString name, F f) {
    CfgValueCb<F> cb(f);
    CfgSetValueCallback(settings, name, cb);
}

extern void CfgDump(const Settings& settings, ::std::ostream& os);
extern void CfgSetFlag(Settings& settings, ::std::string name);
extern void CfgSetValue(Settings& settings, ::std::string name, ::std::string val);
extern void CfgParseOption(const ::std::string& spec, ::std::string& name, bool& hasValue, ::std::string& value);
extern bool CfgSetCheckSpec(Settings& settings, const ::std::string& spec, ::std::string& error);
extern void CfgSetLintLevel(Settings& settings, ::std::string name, CfgLintLevel level);
extern void CfgSetLintCap(Settings& settings, CfgLintLevel level);
extern bool checkCfgAttrs(const Settings& settings, const ASTAttributeList& attrs);
extern bool checkCfg(const Settings& settings, const Span& sp, const ASTAttribute& mi);
/// Check a parenthesised list of cfg rules (treated as `all()`)
extern bool checkCfgStream(const Settings& settings, TokenStream& lex);
/// Parse an attribute from a `cfg_attr()` attribute. Returns with an empty name if check failed
extern std::vector<ASTAttribute> checkCfgAttr(const Settings& settings, const ASTAttribute& mi);
