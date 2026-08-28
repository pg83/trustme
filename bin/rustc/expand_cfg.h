#pragma once

#include "settings.h"

#include <std/mem/obj_pool.h>

#include <iosfwd>
#include <string>
#include <vector>

struct Span;

class TokenStream;

class ASTAttribute;
class ASTAttributeList;

Settings::CfgState* CfgCreateState(stl::ObjPool& pool);

using CfgString = std::string;

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

void CfgSetValueCallback(Settings& settings, CfgString name, const CfgValueCallback& cb);

template <typename F>
void CfgSetValueCb(Settings& settings, CfgString name, F f) {
    CfgValueCb<F> cb(f);
    CfgSetValueCallback(settings, name, cb);
}

void CfgDump(const Settings& settings, std::ostream& os);
void CfgSetFlag(Settings& settings, std::string name);
void CfgSetValue(Settings& settings, std::string name, std::string val);
void CfgParseOption(const std::string& spec, std::string& name, bool& hasValue, std::string& value);
bool CfgSetCheckSpec(Settings& settings, const std::string& spec, std::string& error);
void CfgSetLintLevel(Settings& settings, std::string name, CfgLintLevel level);
void CfgSetLintCap(Settings& settings, CfgLintLevel level);
bool checkCfgAttrs(const Settings& settings, const ASTAttributeList& attrs);
bool checkCfg(const Settings& settings, const Span& sp, const ASTAttribute& mi);

bool checkCfgStream(const Settings& settings, TokenStream& lex);

std::vector<ASTAttribute> checkCfgAttr(const Settings& settings, const ASTAttribute& mi);
