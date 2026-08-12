#pragma once

#include "tagged_union.h"
#include "ast_expr_ptr.h"
#include "parse_tokentree.h"

namespace AST {

    class Crate;
    class Module;

    //
    class Attribute;
    ::std::ostream& operator<<(::std::ostream& os, const Attribute& x);

    /// A list of attributes on an item (searchable by the attribute name)
    class AttributeList {
    public:
        ::std::vector<Attribute> mItems;

        AttributeList();
        AttributeList(::std::vector<Attribute> items);
        ~AttributeList();

        // Move present
        AttributeList(AttributeList&&);
        AttributeList& operator=(AttributeList&&);
        // No copy assign, but explicit copy
        explicit AttributeList(const AttributeList&);
        AttributeList& operator=(const AttributeList&) = delete;
        // Explicit clone
        AttributeList clone() const;

        void push_back(Attribute i);

        const Attribute* get(const char* name) const;

        Attribute* get(const char* name) {
            return const_cast<Attribute*>(const_cast<const AttributeList*>(this)->get(name));
        }

        bool has(const char* name) const {
            return get(name) != 0;
        }

        friend ::std::ostream& operator<<(::std::ostream& os, const AttributeList& x);
    };

    struct AttributeName {
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

        friend std::ostream& operator<<(std::ostream& os, const AttributeName& x);
    };

    // An attribute can has a name, and optional data:
    // Data can be:
    // - A parenthesised token tree
    //   > In 1.19 this was actually just sub-attributes
    // - an associated (string) literal

    class Attribute {
        Span mSpan;
        AttributeName mName;
        TokenTree mData;
        /// @brief Indicates that this attribute has been used by a derive, and shouldn't be otherwise resolved
        mutable bool mIsInert;
        // TODO: Parse as a TT then expand?
    public:
        Attribute(Span sp, AttributeName name, TokenTree data);

        explicit Attribute(const Attribute& x);
        Attribute& operator=(const Attribute&) = delete;
        Attribute(Attribute&&) = default;
        Attribute& operator=(Attribute&&) = default;
        Attribute clone() const;

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

        const AttributeName& name() const {
            return mName;
        }

        const TokenTree& data() const {
            return mData;
        }

        TokenTree& dataMut() {
            return mData;
        }

        /// Parses the data as a `="string"` and returns the string
        std::string parseEqualsString(const AST::Crate& crate, const AST::Module& mod) const;
        /// Parses the data as a `("string")` and returns the string
        std::string parseParenString() const;

        void parseParenIdentList(std::function<void(const Span& sp, RcString ident)> itemCb) const;

        friend ::std::ostream& operator<<(::std::ostream& os, const Attribute& x);
    };

    TAGGED_UNION(AttributeData, None, (None, struct {}), (ValueUnexpanded, AST::ExprNodeP), (String, struct { ::std::string val; }), (List, struct { ::std::vector<Attribute> subItems; }));

} // namespace AST
