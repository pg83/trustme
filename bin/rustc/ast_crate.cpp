#include "ast_crate.h"

#include "ast_ast.h"
#include "hir_hir.h"
#include "settings.h"
#include "expand_cfg.h"
#include "wire_board.h"
#include "parse_parseerror.h"
#include "hir_main_bindings.h"

#include <fstream>
#include <dirent.h>

using namespace stl;

namespace {
    bool checkAttributeCfg(const Settings& settings, const ASTAttribute& attr) {
        if (attr.name() == "cfg") {
            return checkCfg(settings, attr.span(), attr);
        }
        if (attr.name() == "cfg_attr") {
            for (const auto& expanded : checkCfgAttr(settings, attr)) {
                if (!checkAttributeCfg(settings, expanded)) {
                    return false;
                }
            }
        }
        return true;
    }

    bool checkItemCfg(const Settings& settings, const ASTAttributeList& attrs) {
        for (const auto& at : attrs.items) {
            if (!checkAttributeCfg(settings, at)) {
                return false;
            }
        }
        return true;
    }

    template <typename F>
    void iterateModule(const Settings& settings, ASTModule& mod, F fcn) {
        fcn(mod);
        for (auto& sm : mod.items) {
            if (auto* e = sm->data.opt_Module()) {
                if (checkItemCfg(settings, sm->attrs)) {
                    iterateModule(settings, *e, fcn);
                }
            }
        }
        // TODO: What about if an anon mod has been #[cfg]-d out?
    }
}

ASTCrate::ASTCrate(const WireBoard& wb, ObjPool* pool, ObjPool* hirPool, HIRTypeInterner& types)
    : wb(wb)
    , pool(pool)
    , hirPool(hirPool)
    , types(types)
    , rootModule_(ASTAbsolutePath())
    , loadStd(LOAD_STD)
{
}

void ASTCrate::loadExterns(Settings& settings) {
    auto cb = [this, &settings](ASTModule& mod) {
        for (/*const*/ auto& it : mod.items) {
            if (auto* c = it->data.opt_Crate()) {
                if (checkItemCfg(settings, it->attrs)) {
                    if (c->name == "") {
                    } else {
                        c->name = loadExternCrate(settings, it->span, c->name);
                    }
                }
            }
        }
    };
    if (checkItemCfg(settings, attrs)) {
        iterateModule(settings, rootModule_, cb);
    }

    bool noStd = false;
    bool noCore = false;

    for (const auto& a : this->attrs.items) {
        if (a.name() == "no_std") {
            noStd = true;
        }
        if (a.name() == "no_core") {
            noCore = true;
        }
        if (a.name() == "cfg_attr") {
            for (const auto& a2 : checkCfgAttr(settings, a)) {
                if (a2.name() == "no_std") {
                    noStd = true;
                }
                if (a2.name() == "no_core") {
                    noCore = true;
                }
            }
        }
    }

    if (noCore) {
    } else if (noStd) {
        auto n = this->loadExternCrate(settings, Span(), "core");
    } else {
        auto n = this->loadExternCrate(settings, Span(), "std");
    }

    DEBUG(StringView("Load from --extern"));
    settings.crateOverrides.visit([&](const CrateOverride& entry) {
        if (!entry.isExtern) {
            return;
        }
        if (entry.target != "") {
            const auto* target = settings.findCrateOverride(entry.target);
            const auto path = target && target->metadataPath != "" ? target->metadataPath : entry.target;
            if (!std::ifstream(path.c_str()).good()) {
                DEBUG(StringView("Skipping --extern ") << entry.name << StringView(": ") << path << StringView(" does not exist"));
                return;
            }
        }
        auto realName = this->loadExternCrate(settings, Span(), entry.name);
        settings.implicitCrates.insert(std::make_pair(entry.name, realName));
    });
    if (this->extCratenameCore != "") {
        settings.implicitCrates.insert(std::make_pair(RcString::newInterned("core"), this->extCratenameCore));
    }
}

// TODO: Handle disambiguating crates with the same name (e.g. libc in std and crates.io libc)

RcString ASTCrate::loadExternCrate(Settings& settings, Span sp, const RcString& name, const std::string& basename /*=""*/) {
    TRACE_FUNCTION_F(StringView("Loading crate '") << name << StringView("' (basename='") << basename << StringView("')"));
    std::string path;
    auto* entry = settings.findCrateOverride(name);
    const CrateOverride* artifacts = nullptr;
    RcString expectedName;

    if (entry && entry->metadataPath != "") {
        path = entry->metadataPath.c_str();
        artifacts = entry;
        expectedName = name;
        if (!std::ifstream(path).good()) {
            ERROR(sp, E0000, StringView("Unable to open crate '") << name << StringView("' at path ") << path);
        }
        DEBUG(StringView("path = ") << path << StringView(" (--crate)"));
    } else if (entry && entry->target != "") {
        auto* target = settings.findCrateOverride(entry->target);
        if (target && target->metadataPath != "") {
            path = target->metadataPath.c_str();
            artifacts = target;
            expectedName = entry->target;
        } else {
            path = entry->target.c_str();
        }
        if (!std::ifstream(path).good()) {
            ERROR(sp, E0000, StringView("Unable to open crate '") << name << StringView("' at path ") << path);
        }
        DEBUG(StringView("path = ") << path << StringView(" (--extern)"));
    } else if (basename != "") {
        bool hasExactCrates = false;
        settings.crateOverrides.visit([&](const CrateOverride& entry) {
            hasExactCrates |= entry.metadataPath != "";
        });
        if (hasExactCrates) {
            ERROR(sp, E0000, StringView("Crate '") << name << StringView("' is missing from the explicit --crate table"));
        }
        for (const auto& p : settings.crateLoadDirs) {
            path = p + "/" + basename;

            if (std::ifstream(path).good()) {
                auto n = HIRDeserialiseJustName(path);
                if (n == name) {
                    break;
                }
            }
        }
        if (!std::ifstream(path).good()) {
            ERROR(sp, E0000, StringView("Unable to locate crate '") << name << StringView("' with filename ") << basename << StringView(" in search directories"));
        }
        DEBUG(StringView("path = ") << path << StringView(" (basename)"));
    } else {
        std::vector<std::string> paths;
#define RLIB_SUFFIX ".rlib"
#define RDYLIB_SUFFIX ".so"
#define PLUGIN_SUFFIX "-plugin"
        auto directFilename = FMT(StringView("lib") << name << StringView(RLIB_SUFFIX));
        auto directFilenameSo = FMT(StringView("lib") << name << StringView(RDYLIB_SUFFIX));
        auto namePrefix = FMT(StringView("lib") << name << StringView("-"));
        for (const auto& p : settings.crateLoadDirs) {
            DEBUG(StringView("Searching in ") << p);
            path = p + "/" + directFilename;
            if (std::ifstream(path).good()) {
                paths.push_back(path);
                break;
            }
            path = p + "/" + directFilenameSo;
            if (std::ifstream(path).good()) {
                paths.push_back(path);
                break;
            }
            path = "";

            auto dp = opendir(p.c_str());
            if (!dp) {
                DEBUG(StringView("Unable to opendir `") << p << StringView("`"));
                continue;
            }
            struct dirent* ent;
            while ((ent = readdir(dp)) != nullptr && path == "") {
                const auto* fname = ent->d_name;

                size_t len = strlen(fname);
                if (len > (sizeof(RLIB_SUFFIX) - 1) && strcmp(fname + len - (sizeof(RLIB_SUFFIX) - 1), RLIB_SUFFIX) == 0) {
                    if (len > strlen(PLUGIN_SUFFIX RLIB_SUFFIX) && strcmp(fname + len - strlen(PLUGIN_SUFFIX RLIB_SUFFIX), PLUGIN_SUFFIX RLIB_SUFFIX) == 0) {
                        auto pluginPath = p + "/" + fname;
                        pluginPath.resize(pluginPath.size() - strlen(RLIB_SUFFIX));
                        if (std::ifstream(pluginPath).good()) {
                            continue;
                        }
                    }
                    if (len > strlen(RDYLIB_SUFFIX RLIB_SUFFIX) && strcmp(fname + len - strlen(RDYLIB_SUFFIX RLIB_SUFFIX), RDYLIB_SUFFIX RLIB_SUFFIX) == 0) {
                        auto dylibPath = p + "/" + fname;
                        dylibPath.resize(dylibPath.size() - strlen(RLIB_SUFFIX));
                        if (std::ifstream(dylibPath).good()) {
                            continue;
                        }
                    }
                } else if (len > (sizeof(RDYLIB_SUFFIX) - 1) && strcmp(fname + len - (sizeof(RDYLIB_SUFFIX) - 1), RDYLIB_SUFFIX) == 0) {
                } else if (len > (sizeof(PLUGIN_SUFFIX) - 1) && strcmp(fname + len - (sizeof(PLUGIN_SUFFIX) - 1), PLUGIN_SUFFIX) == 0) {
                } else {
                    continue;
                }

                DEBUG(fname << StringView(" vs ") << namePrefix);
                if (strncmp(namePrefix.c_str(), fname, namePrefix.size()) != 0) {
                    continue;
                }

                paths.push_back(p + "/" + fname);
            }
            closedir(dp);
            if (paths.size() > 0) {
                break;
            }
        }
        if (paths.size() > 1) {
            ERROR(sp, E0000, StringView("Multiple options for crate '") << name << StringView("' in search directories - ") << paths);
        }
        if (paths.size() == 0) {
            ERROR(sp, E0000, StringView("Unable to locate crate '") << name << StringView("' in search directories"));
        }
        path = paths.front();
        DEBUG(StringView("path = ") << path << StringView(" (search)"));
    }

    auto ec = ASTExternCrate{wb.id, hirPool, types, name, path};
    auto realName = ec.hir->crateName;
    BUG_ASSERT(realName != "");
    if (expectedName != "" && realName != expectedName) {
        ERROR(sp, E0000, StringView("Crate artifact ") << path << StringView(" contains '") << realName << StringView("', expected '") << expectedName << StringView("'"));
    }
    if (!artifacts) {
        if (auto* realArtifacts = settings.findCrateOverride(realName)) {
            artifacts = realArtifacts;
        }
    }
    if (artifacts) {
        ec.objectFilename = artifacts->objectPath;
        ec.procMacroFilename = artifacts->procMacroPath;
        ec.isProcMacro = artifacts->procMacroPath != "";
    }
    auto res = externCrates.insert(std::make_pair(realName, mv$(ec)));
    if (!res.second) {
        DEBUG(StringView("Duplicate load of '") << realName);
        return realName;
    }
    auto& extCrate = res.first->second;
    const auto& crateExtList = extCrate.hir->extCrates;

    for (const auto& ext : crateExtList) {
        if (externCrates.count(ext.first) == 0) {
            const auto loadName = this->loadExternCrate(settings, sp, ext.first, ext.second.basename);
            if (loadName != ext.first) {
                ERROR(sp, E0000, StringView("The crate file `") << ext.second.basename << StringView("` didn't load the expected crate - have ") << loadName << StringView(" != exp ") << ext.first);
            }
        }
    }
    externCratesOrd.pushBack(realName);

    if (extCrate.shortName == "core") {
        if (this->extCratenameCore == "") {
            this->extCratenameCore = extCrate.name;
        }
    }
    if (extCrate.shortName == "std") {
        if (this->extCratenameStd == "") {
            this->extCratenameStd = extCrate.name;
        }
    }
    if (extCrate.shortName == "proc_macro") {
        if (this->extCratenameProcmacro == "") {
            this->extCratenameProcmacro = extCrate.name;
        }
    }
    if (extCrate.shortName == "test") {
        if (this->extCratenameTest == "") {
            this->extCratenameTest = extCrate.name;
        }
    }

    DEBUG(StringView("Loaded '") << name << StringView("' from '") << basename << StringView("' (actual name is '") << realName << StringView("' aka `") << extCrate.shortName << StringView("`)"));
    return realName;
}

ASTExternCrate::ASTExternCrate(u32& id, ObjPool* pool, HIRTypeInterner& types, const RcString& name, const std::string& path)
    : name(name)
    , shortName(name)
    , filename(path)
{
    TRACE_FUNCTION_F(StringView("name=") << name << StringView(", path='") << path << StringView("'"));
    hir = HIRDeserialise(id, pool, types, path);

    hir->postLoadUpdate(name);
    this->name = hir->crateName;
    if (const auto* e = strchr(this->name.c_str(), '-')) {
        shortName = RcString::newInterned(this->name.c_str(), e - this->name.c_str());
    } else {
    }
}

void ASTCrate::setCrateName(std::string name) {
    crateNameSet = name;
    if (crateType == Type::Executable) {
        crateNameReal = "";
    } else {
        crateNameReal = crateNameSuffix != "" ? RcString::newInterned(name + "-" + crateNameSuffix) : RcString::newInterned(name);
    }
}
