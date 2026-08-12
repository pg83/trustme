#pragma once

#include "hir_type.h"

namespace HIR {

    class ItemPath {
    public:
        const ItemPath* parent = nullptr;
        const ::HIR::TypeData* ty = nullptr;
        const ::HIR::SimplePath* trait = nullptr;
        const ::HIR::PathParams* trait_params = nullptr;
        const char* name = nullptr;
        const char* crate_name = nullptr;
        const ::HIR::Path* wrapped = nullptr;

        ItemPath(const char* crate);

        ItemPath(const ::std::string& crate);

        ItemPath(const RcString& crate);

        ItemPath(const ItemPath& p, const char* n);

        ItemPath(const ::HIR::Path& p);

        ItemPath(const ::HIR::TypeData* type);

        ItemPath(const ::HIR::TypeData* type, const ::HIR::SimplePath& path, const ::HIR::PathParams& params);

        ItemPath(const ::HIR::SimplePath& path);

        const ::HIR::SimplePath* trait_path() const {
            return trait;
        }

        const ::HIR::PathParams* trait_args() const {
            return trait_params;
        }

        ::HIR::SimplePath get_simple_path() const;

        ::HIR::Path get_full_path() const;

        const char* get_name() const {
            return name ? name : "";
        }

        const ItemPath& get_top_ip() const;

        ItemPath operator+(const ::std::string& name) const {
            return ItemPath(*this, name.c_str());
        }

        ItemPath operator+(const RcString& name) const {
            return ItemPath(*this, name.c_str());
        }

        bool operator==(const ::HIR::SimplePath& sp) const;

        friend ::std::ostream& operator<<(::std::ostream& os, const ItemPath& x) {
            if (x.wrapped) {
                return os << *x.wrapped;
            }
            if (x.parent) {
                os << *x.parent;
            }
            if (x.name) {
                os << "::" << x.name;
            } else if (x.ty) {
                os << "<" << *x.ty;
                if (x.trait) {
                    os << " as " << *x.trait;
                    if (x.trait_params) {
                        os << *x.trait_params;
                    }
                }
                os << ">";
            } else if (x.trait) {
                os << "<* as " << *x.trait << ">";
            } else if (x.crate_name) {
                os << "::\"" << x.crate_name << "\"";
            }
            return os;
        }
    };

}
