#include "ast_crate.h"

#include "ast_ast.h"
#include "hir_hir.h" // HIR::Crate
#include "settings.h"
#include "expand_cfg.h"
#include "parse_parseerror.h"
#include "hir_main_bindings.h" // HIR_Deserialise
#include "wire_board.h"

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
        // - For now, disable
        //}
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
                        // Leave for now
                    } else {
                        c->name = loadExternCrate(settings, it->span, c->name);
                    }
                }
            }
        }
    };
    // Crate-level cfg is applied during expansion, but explicit extern crates
    // are loaded before expansion. Do not require externs from a crate whose
    // entire contents will be removed.
    if (checkItemCfg(settings, attrs)) {
        iterateModule(settings, rootModule_, cb);
    }

    // Check for no_std or no_core, and load libstd/libcore
    // - Duplicates some of the logic in "Expand", but also helps keep crate loading separate to most of expand
    // NOTE: Not all crates are loaded here, any crates loaded by macro invocations will be done during expand.
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
        // Don't load anything
    } else if (noStd) {
        auto n = this->loadExternCrate(settings, Span(), "core");
        //}
    } else {
        auto n = this->loadExternCrate(settings, Span(), "std");
        //}
    }

    // `--crate` populates the availability table but does not put a name in
    // the source extern prelude.  Only `--extern` aliases are loaded here.
    settings.crateOverrides.visit([&](const CrateOverride& entry) {
        if (!entry.isExtern) {
            return;
        }
        // A path that names no file is only an error if the crate is used:
        // `--extern Name=/nowhere` on a crate that never mentions `Name` is
        // accepted, and a mention of it fails to find the crate as usual.
        if (entry.target != "") {
            const auto* target = settings.findCrateOverride(entry.target);
            const auto path = target && target->metadataPath != ""
                ? target->metadataPath
                : entry.target;
            if (!::std::ifstream(path.c_str()).good()) {
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
// - Crates recorded in rlibs should specify a hash/tag that's passed in to this function.
RcString ASTCrate::loadExternCrate(Settings& settings, Span sp, const RcString& name, const ::std::string& basename /*=""*/) {

    ::std::string path;
    auto* entry = settings.findCrateOverride(name);
    const CrateOverride* artifacts = nullptr;
    RcString expectedName;

    // An exact `--crate unique=path` always wins, including for a recursive
    // dependency whose old metadata still carries a basename.
    if (entry && entry->metadataPath != "") {
        path = entry->metadataPath.c_str();
        artifacts = entry;
        expectedName = name;
        if (!::std::ifstream(path).good()) {
            ERROR(sp, E0000, "Unable to open crate '" << name << "' at path " << path);
        }
    }
    // Resolve a source alias through the exact table.  If there is no such
    // exact entry, the RHS is a legacy `--extern alias=path`.
    else if (entry && entry->target != "") {
        auto* target = settings.findCrateOverride(entry->target);
        if (target && target->metadataPath != "") {
            path = target->metadataPath.c_str();
            artifacts = target;
            expectedName = entry->target;
        } else {
            path = entry->target.c_str();
        }
        if (!::std::ifstream(path).good()) {
            ERROR(sp, E0000, "Unable to open crate '" << name << "' at path " << path);
        }
    }
    // If the filename is known, then search for that in the search directories
    // - Checks the crate name of each to ensure a match
    else if (basename != "") {
        bool hasExactCrates = false;
        settings.crateOverrides.visit([&](const CrateOverride& entry) {
            hasExactCrates |= entry.metadataPath != "";
        });
        if (hasExactCrates) {
            ERROR(sp, E0000, "Crate '" << name << "' is missing from the explicit --crate table");
        }
        // Search a list of load paths for the crate
        for (const auto& p : settings.crateLoadDirs) {
            path = p + "/" + basename;

            if (::std::ifstream(path).good()) {
                // Ensure that if this is loaded, it yields the right name (otherwise skip)
                auto n = HIRDeserialiseJustName(path);
                if (n == name) {
                    break;
                }
            }
        }
        if (!::std::ifstream(path).good()) {
            ERROR(sp, E0000, "Unable to locate crate '" << name << "' with filename " << basename << " in search directories");
        }
    } else {
        ::std::vector<::std::string> paths;
#define RLIB_SUFFIX ".rlib"
#define RDYLIB_SUFFIX ".so"
#define PLUGIN_SUFFIX "-plugin"
        auto directFilename = FMT("lib" << name << RLIB_SUFFIX);
        auto directFilenameSo = FMT("lib" << name << RDYLIB_SUFFIX);
        auto namePrefix = FMT("lib" << name << "-");
        // Search a list of load paths for the crate
        for (const auto& p : settings.crateLoadDirs) {
            path = p + "/" + directFilename;
            if (::std::ifstream(path).good()) {
                paths.push_back(path);
                break;
            }
            path = p + "/" + directFilenameSo;
            if (::std::ifstream(path).good()) {
                paths.push_back(path);
                break;
            }
            path = "";

            // Search for `p+"/lib"+name+"-*.rlib" (which would match e.g. libnum-0.11.rlib)
            auto dp = opendir(p.c_str());
            if (!dp) {
                continue;
            }
            struct dirent* ent;
            while ((ent = readdir(dp)) != nullptr && path == "") {
                const auto* fname = ent->d_name;

                // AND the start is "lib"+name
                size_t len = strlen(fname);
                if (len > (sizeof(RLIB_SUFFIX) - 1) && strcmp(fname + len - (sizeof(RLIB_SUFFIX) - 1), RLIB_SUFFIX) == 0) {
                    // A new proc-macro executable has a `.rlib` metadata
                    // companion. The executable is the legacy -L artifact;
                    // metadataFilename() will find its companion when loading.
                    if (len > strlen(PLUGIN_SUFFIX RLIB_SUFFIX)
                        && strcmp(fname + len - strlen(PLUGIN_SUFFIX RLIB_SUFFIX), PLUGIN_SUFFIX RLIB_SUFFIX) == 0) {
                        auto pluginPath = p + "/" + fname;
                        pluginPath.resize(pluginPath.size() - strlen(RLIB_SUFFIX));
                        if (::std::ifstream(pluginPath).good()) {
                            continue;
                        }
                    }
                    if (len > strlen(RDYLIB_SUFFIX RLIB_SUFFIX)
                        && strcmp(fname + len - strlen(RDYLIB_SUFFIX RLIB_SUFFIX), RDYLIB_SUFFIX RLIB_SUFFIX) == 0) {
                        auto dylibPath = p + "/" + fname;
                        dylibPath.resize(dylibPath.size() - strlen(RLIB_SUFFIX));
                        if (::std::ifstream(dylibPath).good()) {
                            continue;
                        }
                    }
                } else if (len > (sizeof(RDYLIB_SUFFIX) - 1) && strcmp(fname + len - (sizeof(RDYLIB_SUFFIX) - 1), RDYLIB_SUFFIX) == 0) {
                } else if (len > (sizeof(PLUGIN_SUFFIX) - 1) && strcmp(fname + len - (sizeof(PLUGIN_SUFFIX) - 1), PLUGIN_SUFFIX) == 0) {
                } else {
                    continue;
                }

                // Check if the entry ends with .rlib
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
            ERROR(sp, E0000, "Multiple options for crate '" << name << "' in search directories - " << paths);
        }
        if (paths.size() == 0) {
            ERROR(sp, E0000, "Unable to locate crate '" << name << "' in search directories");
        }
        path = paths.front();
    }

    // NOTE: Creating `ExternCrate` loads the crate from the specified path
    auto ec = ASTExternCrate{wb.id, hirPool, types, name, path};
    auto realName = ec.hir->crateName;
    assert(realName != "");
    if (expectedName != "" && realName != expectedName) {
        ERROR(sp, E0000, "Crate artifact " << path << " contains '" << realName << "', expected '" << expectedName << "'");
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
    auto res = externCrates.insert(::std::make_pair(realName, mv$(ec)));
    if (!res.second) {
        // Crate already loaded?
        return realName;
    }
    auto& extCrate = res.first->second;
    const auto& crateExtList = extCrate.hir->extCrates;

    // Load referenced crates
    for (const auto& ext : crateExtList) {
        if (externCrates.count(ext.first) == 0) {
            const auto loadName = this->loadExternCrate(settings, sp, ext.first, ext.second.basename);
            if (loadName != ext.first) {
                // ERROR - The crate loaded wasn't the one that was used when compiling this crate.
                ERROR(sp, E0000, "The crate file `" << ext.second.basename << "` didn't load the expected crate - have " << loadName << " != exp " << ext.first);
            }
        }
    }
    // NOTE: Add the crate to the ordered list AFTER its dependencies
    externCratesOrd.push_back(realName);

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

    return realName;
}

ASTExternCrate::ASTExternCrate(u32& id,
    ObjPool* pool, HIRTypeInterner& types, const RcString& name,
    const ::std::string& path)
    : name(name)
    , shortName(name)
    , filename(path)
{
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
