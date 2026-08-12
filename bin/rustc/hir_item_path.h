#pragma once

#include "hir_type.h"

namespace HIR {

    class ItemPath {
    public:
        const ItemPath* parent = nullptr;
        const ::HIR::TypeData* ty = nullptr;
        const ::HIR::SimplePath* trait = nullptr;
        const ::HIR::PathParams* traitParams = nullptr;
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
            return traitParams;
        }

        ::HIR::SimplePath getSimplePath() const;

        ::HIR::Path getFullPath() const;

        const char* getName() const {
            return name ? name : "";
        }

        const ItemPath& getTopIp() const;

        ItemPath operator+(const ::std::string& name) const {
            return ItemPath(*this, name.c_str());
        }

        ItemPath operator+(const RcString& name) const {
            return ItemPath(*this, name.c_str());
        }

        bool operator==(const ::HIR::SimplePath& sp) const;

        friend ::std::ostream& operator<<(::std::ostream& os, const ItemPath& x);
    };

}
