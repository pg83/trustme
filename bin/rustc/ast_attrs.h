#pragma once

#include "output.h"
#include "ast_expr_ptr.h"
#include "parse_tokentree.h"

#include <std/lib/vector.h>

class ASTCrate;
struct WireBoard;
class ASTModule;

struct ASTAttributeIdentCallback {
    virtual void visit(const Span& sp, RcString ident) = 0;
};

template <typename F>
struct ASTAttributeIdentCb final: ASTAttributeIdentCallback {
    F f;

    explicit ASTAttributeIdentCb(F f)
        : f(f)
    {
    }

    void visit(const Span& sp, RcString ident) override {
        f(sp, ident);
    }
};

class ASTAttribute;

class ASTAttributeList {
public:
    std::vector<ASTAttribute> items;

    ASTAttributeList();
    ASTAttributeList(std::vector<ASTAttribute> items);
    ~ASTAttributeList();

    ASTAttributeList(ASTAttributeList&&);
    ASTAttributeList& operator=(ASTAttributeList&&);

    explicit ASTAttributeList(const ASTAttributeList&);
    ASTAttributeList& operator=(const ASTAttributeList&) = delete;

    ASTAttributeList clone() const;

    void push_back(ASTAttribute i);

    const ASTAttribute* get(const char* name) const;

    ASTAttribute* get(const char* name) {
        return const_cast<ASTAttribute*>(const_cast<const ASTAttributeList*>(this)->get(name));
    }

    bool has(const char* name) const {
        return get(name) != 0;
    }
};

struct ASTAttributeName {
    bool hasLeading = false;
    stl::Vector<RcString> elems;

    bool isTrivial() const {
        return elems.length() == 1;
    }

    const RcString& asTrivial() const {
        return elems[0];
    }

    bool operator==(const char* s) const {
        return elems.length() == 1 && elems[0] == s;
    }

    bool operator==(const RcString& x) const {
        return elems.length() == 1 && elems[0] == x;
    }

    template <typename T>
    bool operator!=(const T& x) const {
        return !(*this == x);
    }
};

class ASTAttribute {
    Span span_;
    ASTAttributeName name_;
    TokenTree data_;

    mutable bool isInert_;
    // TODO: Parse as a TT then expand?
public:
    ASTAttribute(Span sp, ASTAttributeName name, TokenTree data);

    explicit ASTAttribute(const ASTAttribute& x);
    ASTAttribute& operator=(const ASTAttribute&) = delete;
    ASTAttribute(ASTAttribute&&) = default;
    ASTAttribute& operator=(ASTAttribute&&) = default;
    ASTAttribute clone() const;

    void fmt(stl::ZeroCopyOutput& os) const;

    void markInert() const {
        isInert_ = true;
    }

    bool isInert() const {
        return isInert_;
    }

    const Span& span() const {
        return span_;
    }

    const ASTAttributeName& name() const {
        return name_;
    }

    const TokenTree& data() const {
        return data_;
    }

    TokenTree& dataMut() {
        return data_;
    }

    std::string parseEqualsString(const WireBoard& wb, const ASTCrate& crate, const ASTModule& mod) const;

    std::string parseParenString() const;

    void parseParenIdentListCb(ASTAttributeIdentCallback& itemCb) const;

    template <typename F>
    void parseParenIdentList(F f) const {
        ASTAttributeIdentCb<F> cb(f);
        parseParenIdentListCb(cb);
    }
};

#include "ast_attrs_tu.h"
