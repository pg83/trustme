#pragma once

#include "hir_type.h"

class HIRItemPath {
public:
    const HIRItemPath* parent = nullptr;
    const HIRType* ty = nullptr;
    const HIRSimplePath* trait = nullptr;
    const HIRPathParams* traitParams = nullptr;
    const char* name = nullptr;
    const char* crateName = nullptr;
    const HIRPath* wrapped = nullptr;

    HIRItemPath(const char* crate);

    HIRItemPath(const std::string& crate);

    HIRItemPath(const RcString& crate);

    HIRItemPath(const HIRItemPath& p, const char* n);

    HIRItemPath(const HIRPath& p);

    HIRItemPath(const HIRType* type);

    HIRItemPath(const HIRType* type, const HIRSimplePath& path, const HIRPathParams& params);

    HIRItemPath(const HIRSimplePath& path);

    const HIRSimplePath* traitPath() const {
        return trait;
    }

    const HIRPathParams* traitArgs() const {
        return traitParams;
    }

    HIRSimplePath getSimplePath() const;

    HIRPath getFullPath() const;

    const char* getName() const {
        return name ? name : "";
    }

    const HIRItemPath& getTopIp() const;

    HIRItemPath operator+(const std::string& name) const {
        return HIRItemPath(*this, name.c_str());
    }

    HIRItemPath operator+(const RcString& name) const {
        return HIRItemPath(*this, name.c_str());
    }

    bool operator==(const HIRSimplePath& sp) const;

};
