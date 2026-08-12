#include "ast_crate.h"
#include "ast_ast.h"
#include "parse_parseerror.h"
#include "expand_cfg.h"
#include "hir_hir.h"           // HIR::Crate
#include "hir_main_bindings.h" // HIR_Deserialise
#include <fstream>
#include <dirent.h>

::std::vector<::std::string> AST::g_crate_load_dirs = {};
::std::map<::std::string, ::std::string> AST::g_crate_overrides;
::std::map<RcString, RcString> AST::g_implicit_crates;

namespace {
    bool checkItemCfg(const ::AST::AttributeList& attrs) {
        for (const auto& at : attrs.mItems) {
            if (at.name() == "cfg" && !checkCfg(at.span(), at)) {
                return false;
            }
        }
        return true;
    }

    void iterate_module(::AST::Module& mod, ::std::function<void(::AST::Module& mod)> fcn) {
        fcn(mod);
        for (auto& sm : mod.mItems) {
            if (auto* e = sm->data.opt_Module()) {
                if (checkItemCfg(sm->attrs)) {
                    iterate_module(*e, fcn);
                }
            }
        }
        // TODO: What about if an anon mod has been #[cfg]-d out?
        // - For now, disable
        //for(const auto& anon : mod.anon_mods() ) {
        //    iterate_module(*anon, fcn);
        //}
    }
}

namespace AST {

    Crate::Crate(stl::ObjPool* pool, HIR::TypeInterner& types)
        : pool(pool)
        , types(types)
        , rootModule(AST::AbsolutePath())
        , loadStd(LOAD_STD)
    {
    }

    void Crate::load_externs() {
        auto cb = [this](Module& mod) {
            for (/*const*/ auto& it : mod.mItems) {
                if (auto* c = it->data.opt_Crate()) {
                    if (checkItemCfg(it->attrs)) {
                        if (c->name == "") {
                            // Leave for now
                        } else {
                            c->name = load_extern_crate(it->span, c->name);
                        }
                    }
                }
            }
        };
        iterate_module(rootModule, cb);

        // Check for no_std or no_core, and load libstd/libcore
        // - Duplicates some of the logic in "Expand", but also helps keep crate loading separate to most of expand
        // NOTE: Not all crates are loaded here, any crates loaded by macro invocations will be done during expand.
        bool no_std = false;
        bool no_core = false;

        for (const auto& a : this->mAttrs.mItems) {
            if (a.name() == "no_std") {
                no_std = true;
            }
            if (a.name() == "no_core") {
                no_core = true;
            }
            if (a.name() == "cfg_attr") {
                for (const auto& a2 : checkCfgAttr(a)) {
                    if (a2.name() == "no_std") {
                        no_std = true;
                    }
                    if (a2.name() == "no_core") {
                        no_core = true;
                    }
                }
            }
        }

        if (no_core) {
            // Don't load anything
        } else if (no_std) {
            auto n = this->load_extern_crate(Span(), "core");
            //if( n != "core" ) {
            //    WARNING(Span(), W0000, "libcore wasn't loaded as `core`, instead `" << n << "`");
            //}
        } else {
            auto n = this->load_extern_crate(Span(), "std");
            //if( n != "std" ) {
            //    WARNING(Span(), W0000, "libstd wasn't loaded as `std`, instead `" << n << "`");
            //}
        }

        // Ensure that all crates passed on the command line are loaded.
        DEBUG("Load from --crate");
        for (const auto& c : g_crate_overrides) {
            auto n = RcString::new_interned(c.first);
            auto real_name = this->load_extern_crate(Span(), n);
            g_implicit_crates.insert(std::make_pair(n, real_name));
        }
        if (this->extCratenameCore != "") {
            g_implicit_crates.insert(std::make_pair(RcString::new_interned("core"), this->extCratenameCore));
        }
    }

    // TODO: Handle disambiguating crates with the same name (e.g. libc in std and crates.io libc)
    // - Crates recorded in rlibs should specify a hash/tag that's passed in to this function.
    RcString Crate::load_extern_crate(Span sp, const RcString& name, const ::std::string& basename /*=""*/) {
        TRACE_FUNCTION_F("Loading crate '" << name << "' (basename='" << basename << "')");

        ::std::string path;
        auto it = g_crate_overrides.find(name.c_str());
        // If there's no filename, and this crate name is in the override list - use an the explicit path
        if (basename == "" && it != g_crate_overrides.end()) {
            path = it->second;
            if (!::std::ifstream(path).good()) {
                ERROR(sp, E0000, "Unable to open crate '" << name << "' at path " << path);
            }
            DEBUG("path = " << path << " (--extern)");
        }
        // If the filename is known, then search for that in the search directories
        // - Checks the crate name of each to ensure a match
        else if (basename != "") {
            // Search a list of load paths for the crate
            for (const auto& p : g_crate_load_dirs) {
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
            DEBUG("path = " << path << " (basename)");
        } else {
            ::std::vector<::std::string> paths;
#define RLIB_SUFFIX ".rlib"
#define RDYLIB_SUFFIX ".so"
#define PLUGIN_SUFFIX "-plugin"
            auto directFilename = FMT("lib" << name << RLIB_SUFFIX);
            auto directFilenameSo = FMT("lib" << name << RDYLIB_SUFFIX);
            auto name_prefix = FMT("lib" << name << "-");
            // Search a list of load paths for the crate
            for (const auto& p : g_crate_load_dirs) {
                DEBUG("Searching in " << p);
                path = p + "/" + directFilename;
                if (::std::ifstream(path).good()) {
                    paths.push_back(path);
                }
                path = p + "/" + directFilenameSo;
                if (::std::ifstream(path).good()) {
                    paths.push_back(path);
                }
                path = "";

                // Search for `p+"/lib"+name+"-*.rlib" (which would match e.g. libnum-0.11.rlib)
                auto dp = opendir(p.c_str());
                if (!dp) {
                    DEBUG("Unable to opendir `" << p << "`");
                    continue;
                }
                struct dirent* ent;
                while ((ent = readdir(dp)) != nullptr && path == "") {
                    const auto* fname = ent->d_name;

                    // AND the start is "lib"+name
                    size_t len = strlen(fname);
                    if (len > (sizeof(RLIB_SUFFIX) - 1) && strcmp(fname + len - (sizeof(RLIB_SUFFIX) - 1), RLIB_SUFFIX) == 0) {
                    } else if (len > (sizeof(RDYLIB_SUFFIX) - 1) && strcmp(fname + len - (sizeof(RDYLIB_SUFFIX) - 1), RDYLIB_SUFFIX) == 0) {
                    } else if (len > (sizeof(PLUGIN_SUFFIX) - 1) && strcmp(fname + len - (sizeof(PLUGIN_SUFFIX) - 1), PLUGIN_SUFFIX) == 0) {
                    } else {
                        continue;
                    }

                    DEBUG(fname << " vs " << name_prefix);
                    // Check if the entry ends with .rlib
                    if (strncmp(name_prefix.c_str(), fname, name_prefix.size()) != 0) {
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
            DEBUG("path = " << path << " (search)");
        }

        // NOTE: Creating `ExternCrate` loads the crate from the specified path
        auto ec = ExternCrate{pool, types, name, path};
        auto real_name = ec.hir->crateName;
        assert(real_name != "");
        auto res = externCrates.insert(::std::make_pair(real_name, mv$(ec)));
        if (!res.second) {
            // Crate already loaded?
            DEBUG("Duplicate load of '" << real_name);
            return real_name;
        } else {
        }
        auto& ext_crate = res.first->second;
        // Move the external list out (doesn't need to be kept in the nested crate)
        //auto crate_ext_list = mv$( ext_crate.m_hir->m_ext_crates );
        const auto& crateExtList = ext_crate.hir->extCrates;

        // Load referenced crates
        for (const auto& ext : crateExtList) {
            if (externCrates.count(ext.first) == 0) {
                const auto load_name = this->load_extern_crate(sp, ext.first, ext.second.basename);
                if (load_name != ext.first) {
                    // ERROR - The crate loaded wasn't the one that was used when compiling this crate.
                    ERROR(sp, E0000, "The crate file `" << ext.second.basename << "` didn't load the expected crate - have " << load_name << " != exp " << ext.first);
                }
            }
        }
        // NOTE: Add the crate to the ordered list AFTER its dependencies
        externCratesOrd.push_back(real_name);

        if (ext_crate.shortName == "core") {
            if (this->extCratenameCore == "") {
                this->extCratenameCore = ext_crate.mName;
            }
        }
        if (ext_crate.shortName == "std") {
            if (this->extCratenameStd == "") {
                this->extCratenameStd = ext_crate.mName;
            }
        }
        if (ext_crate.shortName == "proc_macro") {
            if (this->extCratenameProcmacro == "") {
                this->extCratenameProcmacro = ext_crate.mName;
            }
        }
        if (ext_crate.shortName == "test") {
            if (this->extCratenameTest == "") {
                this->extCratenameTest = ext_crate.mName;
            }
        }

        DEBUG("Loaded '" << name << "' from '" << basename << "' (actual name is '" << real_name << "' aka `" << ext_crate.shortName << "`)");
        return real_name;
    }

    ExternCrate::ExternCrate(stl::ObjPool* pool, HIR::TypeInterner& types, const RcString& name, const ::std::string& path)
        : mName(name)
        , shortName(name)
        , filename(path)
    {
        TRACE_FUNCTION_F("name=" << name << ", path='" << path << "'");
        hir = HIRDeserialise(pool, types, path);

        hir->post_load_update(name);
        mName = hir->crateName;
        if (const auto* e = strchr(mName.c_str(), '-')) {
            shortName = RcString::new_interned(mName.c_str(), e - mName.c_str());
        } else {
        }
    }

} // namespace AST

namespace AST {

void Crate::set_crate_name(std::string name) {
    crateNameSet = name;
    if (crateType == Type::Executable) {
        crateNameReal = "";
    } else {
        crateNameReal = crateNameSuffix != "" ? RcString::new_interned(name + "-" + crateNameSuffix) : RcString::new_interned(name);
    }
}
}
