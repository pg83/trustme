#pragma once

#include "tagged_union.h"
#include "ast_expr_ptr.h"
#include "parse_tokentree.h"


    class ASTCrate;
    class ASTModule;

    //
    class ASTAttribute;
    ::std::ostream& operator<<(::std::ostream& os, const ASTAttribute& x);

    /// A list of attributes on an item (searchable by the attribute name)
    class ASTAttributeList {
    public:
        ::std::vector<ASTAttribute> mItems;

        ASTAttributeList();
        ASTAttributeList(::std::vector<ASTAttribute> items);
        ~ASTAttributeList();

        // Move present
        ASTAttributeList(ASTAttributeList&&);
        ASTAttributeList& operator=(ASTAttributeList&&);
        // No copy assign, but explicit copy
        explicit ASTAttributeList(const ASTAttributeList&);
        ASTAttributeList& operator=(const ASTAttributeList&) = delete;
        // Explicit clone
        ASTAttributeList clone() const;

        void push_back(ASTAttribute i);

        const ASTAttribute* get(const char* name) const;

        ASTAttribute* get(const char* name) {
            return const_cast<ASTAttribute*>(const_cast<const ASTAttributeList*>(this)->get(name));
        }

        bool has(const char* name) const {
            return get(name) != 0;
        }

        friend ::std::ostream& operator<<(::std::ostream& os, const ASTAttributeList& x);
    };

    struct ASTAttributeName {
        bool hasLeading = false;
        ::std::vector<RcString> elems;

        bool isTrivial() const {
            return elems.size() == 1;
        }

        const RcString& asTrivial() const {
            return elems.at(0);
        }

        bool operator==(const char* s) const {
            return elems.size() == 1 && elems[0] == s;
        }

        bool operator==(const RcString& x) const {
            return elems.size() == 1 && elems[0] == x;
        }

        template <typename T>
        bool operator!=(const T& x) const {
            return !(*this == x);
        }

        friend std::ostream& operator<<(std::ostream& os, const ASTAttributeName& x);
    };

    // An attribute can has a name, and optional data:
    // Data can be:
    // - A parenthesised token tree
    //   > In 1.19 this was actually just sub-attributes
    // - an associated (string) literal

    class ASTAttribute {
        Span mSpan;
        ASTAttributeName mName;
        TokenTree mData;
        /// @brief Indicates that this attribute has been used by a derive, and shouldn't be otherwise resolved
        mutable bool mIsInert;
        // TODO: Parse as a TT then expand?
    public:
        ASTAttribute(Span sp, ASTAttributeName name, TokenTree data);

        explicit ASTAttribute(const ASTAttribute& x);
        ASTAttribute& operator=(const ASTAttribute&) = delete;
        ASTAttribute(ASTAttribute&&) = default;
        ASTAttribute& operator=(ASTAttribute&&) = default;
        ASTAttribute clone() const;

        void fmt(std::ostream& os) const;

        void markInert() const {
            mIsInert = true;
        }

        bool isInert() const {
            return mIsInert;
        }

        const Span& span() const {
            return mSpan;
        }

        const ASTAttributeName& name() const {
            return mName;
        }

        const TokenTree& data() const {
            return mData;
        }

        TokenTree& dataMut() {
            return mData;
        }

        /// Parses the data as a `="string"` and returns the string
        std::string parseEqualsString(const ASTCrate& crate, const ASTModule& mod) const;
        /// Parses the data as a `("string")` and returns the string
        std::string parseParenString() const;

        void parseParenIdentList(std::function<void(const Span& sp, RcString ident)> itemCb) const;

        friend ::std::ostream& operator<<(::std::ostream& os, const ASTAttribute& x);
    };

    TAGGED_UNION(ASTAttributeData, None, (None, struct {}), (ValueUnexpanded, ASTExprNodeP), (String, struct { ::std::string val; }), (List, struct { ::std::vector<ASTAttribute> subItems; }));

