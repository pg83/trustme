#pragma once

#include "rc_string.h"

#include <std/sym/i_map.h>
#include <std/lib/vector.h>

#include <map>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <optional>

enum class CfgLintLevel {
    Allow,
    Warn,
    ForceWarn,
    Deny,
    Forbid,
};

struct LintLevelOverrides {
    struct Entry {
        RcString name;
        CfgLintLevel level;
        bool isGroup;
    };

    stl::Vector<Entry> entries;

    void set(RcString name, bool isGroup, CfgLintLevel level) {
        for (size_t i = 0; i < entries.length(); ++i) {
            auto& entry = entries.mut(i);
            if (entry.name == name && entry.isGroup == isGroup) {
                entry.level = level;
                return;
            }
        }
        entries.pushBack(Entry{name, level, isGroup});
    }

    const Entry* find(RcString name, bool isGroup) const {
        for (const auto& entry : entries) {
            if (entry.name == name && entry.isGroup == isGroup) {
                return &entry;
            }
        }
        return nullptr;
    }
};

struct CfgState;

struct CrateOverride {
    RcString name;
    RcString metadataPath;
    RcString objectPath;
    RcString procMacroPath;
    RcString target;
    bool isExtern = false;

    explicit CrateOverride(RcString name)
        : name(name)
    {
    }
};

struct Settings {
    explicit Settings(stl::ObjPool* pool)
        : crateOverrides(pool)
    {
    }

    CrateOverride& crateOverride(RcString name) {
        if (auto* entry = crateOverrides.find(name.rawId())) {
            return *entry;
        }
        return *crateOverrides.insert(name.rawId(), name);
    }

    CrateOverride* findCrateOverride(RcString name) const {
        return crateOverrides.find(name.rawId());
    }

    bool overflowChecks = false;

    bool ubChecks = false;

    bool linkDirectives = true;

    unsigned int recursionLimit = 128;

    enum class FmtDebug {
        Full,
        Shallow,
        None,
    } fmtDebug = FmtDebug::Full;

    std::vector<std::string> crateLoadDirs;
    stl::IntMap<CrateOverride> crateOverrides;
    std::map<RcString, RcString> implicitCrates;

    RcString coreCrate;
    RcString crateName;

    CfgState* cfg = nullptr;

    std::map<std::string, CfgLintLevel> lintLevels;
    std::optional<CfgLintLevel> lintCap;

    static bool lintGroupContains(const std::string& group, const std::string& name) {
        if (group == "warnings") {
            return true;
        }
        if (group == "unused") {
            return name == "unused_must_use" || name == "unused_variables" || name == "unused_imports" || name == "unused_mut" || name == "unused_parens";
        }
        return false;
    }

    CfgLintLevel lintLevel(const std::string& name, CfgLintLevel builtin) const {
        auto it = lintLevels.find(name);
        auto level = (it != lintLevels.end() ? it->second : builtin);
        if (lintCap && level > *lintCap && level != CfgLintLevel::ForceWarn) {
            level = *lintCap;
        }
        return level;
    }
};
