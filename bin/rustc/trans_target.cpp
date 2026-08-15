#include "trans_target.h"
#include "wire_board.h"
#include "settings.h"
#include <std/mem/obj_pool.h>

#include "toml.h" // tools/common
#include "hir_hir.h"
#include "expand_cfg.h"
#include "trans_mangling.h"
#include "hir_typeck_helpers.h"
#include "hir_typeck_monomorph.h"
#include "hir_conv_main_bindings.h" // ConvertHIR_ConstantEvaluate_Enum

#include <map>
#include <array>
#include <bitset>
#include <climits> // UINT_MAX
#include <fstream>
#include <algorithm>
#include <unordered_map>

const TargetArch ARCH_X86_64 = {
    "x86_64",
    64,
    false,
    TargetArch::Atomics(/*atomic(u8)=*/true, /*atomic(u16)=*/true, /*atomic(u32)=*/true, true, true),
    TargetArch::Alignments(2, 4, 8, 16, 4, 8, 8)
    //TargetArch::Alignments(2, 4, 8, 8, 4, 8, 8) // TODO: Alignment of u128 is 8 with rustc, but gcc uses 16
};
const TargetArch ARCH_X32 = {
    "x86_64",
    32,
    false,
    ARCH_X86_64.atomics,
    TargetArch::Alignments(2, 4, 8, 16, 4, 8, 4) // x86_64 w/4-byte ptr
};
const TargetArch ARCH_X86 = {
    "x86",
    32,
    false,
    // i586+ baseline: 8/16/32-bit atomics via LOCK prefix, 64-bit via cmpxchg8b
    {/*atomic(u8)=*/true, /*u16=*/true, /*u32=*/true, /*u64=*/true, /*ptr=*/true},
    TargetArch::Alignments(2, 4, /*u64*/ 4, /*u128*/ 4, 4, 4, /*ptr*/ 4) // u128 has the same alignment as u64, which is u32's alignment. And f64 is 4 byte aligned
};
const TargetArch ARCH_ARM64 = {"aarch64", 64, false, {/*atomic(u8)=*/true, true, true, true, true}, TargetArch::Alignments(2, 4, 8, 16, 4, 8, 8)};
const TargetArch ARCH_ARM32 = {
    "arm",
    32,
    false,
    {/*atomic(u8)=*/true, false, true, false, true},
    TargetArch::Alignments(2, 4, 8, 16, 4, 8, 4) // Note, all types are natively aligned (but i128 will be emulated)
};
const TargetArch ARCH_M68K = {"m68k", 32, true, {/*atomic(u8)=*/true, false, true, false, true}, TargetArch::Alignments(2, 2, 2, 2, 2, 2, 2)};
const TargetArch ARCH_POWERPC64 = {"powerpc64", 64, true, {/*atomic(u8)=*/true, true, true, true, true}, TargetArch::Alignments(2, 4, 8, 16, 4, 8, 8)};
const TargetArch ARCH_POWERPC64LE = {"powerpc64", 64, false, {/*atomic(u8)=*/true, true, true, true, true}, TargetArch::Alignments(2, 4, 8, 16, 4, 8, 8)};
const TargetArch ARCH_POWERPC = {
    "powerpc",
    32,
    true,
    // 8-byte atomics are lock-based via libatomic here, but still available: cfg'ing out AtomicU64 breaks libstd.
    {/*atomic(u8)=*/true, true, true, true, true},
    TargetArch::Alignments(2, 4, 8, 8, 4, 8, 4)
};
const TargetArch ARCH_RISCV64 = {"riscv64", 64, false, {/*atomic(u8)=*/true, true, true, true, true}, TargetArch::Alignments(2, 4, 8, 16, 4, 8, 8)};

bool TargetGetSizeAndAlignOf(const Span& sp, const StaticTraitResolve& resolve, const HIRTypeData* ty, size_t& outSize, size_t& outAlign);

namespace {
    TargetSpec loadSpecFromFile(const ::std::string& filename) {
        TargetSpec rv;

        TomlFile tomlFile(filename);
        for (auto keyVal : tomlFile) {
            // Assertion: The way toml works, there has to be at least two entries in every path.
            assert(keyVal.path.size() > 1);
            DEBUG(keyVal.path << " = " << keyVal.value);

            auto checkPathLength = [&](const TomlKeyValue& kv, unsigned len) {
                if (kv.path.size() != len) {
                    if (kv.path.size() > len) {
                        ::std::cerr << "ERROR: Unexpected sub-node to  " << kv.path << " in " << filename << ::std::endl;
                    } else {
                        ::std::cerr << "ERROR: Expected sub-nodes in  " << kv.path << " in " << filename << ::std::endl;
                    }
                    exit(1);
                }
            };
            auto checkPathLengthMin = [&](const TomlKeyValue& kv, unsigned len) {
                if (kv.path.size() < len) {
                    ::std::cerr << "ERROR: Expected sub-nodes in " << kv.path << " in " << filename << ::std::endl;
                }
            };

            try {
                if (keyVal.path[0] == "target") {
                    checkPathLengthMin(keyVal, 2);
                    if (keyVal.path[1] == "family") {
                        checkPathLength(keyVal, 2);
                        rv.family = keyVal.value.asString();
                    } else if (keyVal.path[1] == "os-name") {
                        checkPathLength(keyVal, 2);
                        rv.osName = keyVal.value.asString();
                    } else if (keyVal.path[1] == "env-name") {
                        checkPathLength(keyVal, 2);
                        rv.envName = keyVal.value.asString();
                    } else if (keyVal.path[1] == "arch") {
                        checkPathLength(keyVal, 2);
                        if (keyVal.value.asString() == ARCH_ARM32.mName) {
                            rv.arch = ARCH_ARM32;
                        } else if (keyVal.value.asString() == ARCH_ARM64.mName) {
                            rv.arch = ARCH_ARM64;
                        } else if (keyVal.value.asString() == ARCH_X86.mName) {
                            rv.arch = ARCH_X86;
                        } else if (keyVal.value.asString() == ARCH_X86_64.mName) {
                            rv.arch = ARCH_X86_64;
                        } else if (keyVal.value.asString() == ARCH_M68K.mName) {
                            rv.arch = ARCH_M68K;
                        } else if (keyVal.value.asString() == ARCH_POWERPC.mName) {
                            rv.arch = ARCH_POWERPC;
                        } else if (keyVal.value.asString() == ARCH_POWERPC64.mName) {
                            rv.arch = ARCH_POWERPC64;
                        } else if (keyVal.value.asString() == ARCH_POWERPC64LE.mName) {
                            rv.arch = ARCH_POWERPC64LE;
                        } else if (keyVal.value.asString() == ARCH_RISCV64.mName) {
                            rv.arch = ARCH_RISCV64;
                        } else {
                            // Error.
                            ::std::cerr << "ERROR: Unknown architecture name '" << keyVal.value.asString() << "' in " << filename << ::std::endl;
                            exit(1);
                        }
                    } else {
                        // Warning
                        ::std::cerr << "Warning: Unknown configuration item " << keyVal.path[0] << "." << keyVal.path[1] << " in " << filename << ::std::endl;
                    }
                } else if (keyVal.path[0] == "backend") {
                    checkPathLengthMin(keyVal, 2);
                    if (keyVal.path[1] == "c") {
                        checkPathLengthMin(keyVal, 3);

                        if (keyVal.path[2] == "variant") {
                            checkPathLength(keyVal, 3);
                            if (keyVal.value.asString() != "gnu") {
                                ::std::cerr << "ERROR: Unknown C variant name '" << keyVal.value.asString() << "' in " << filename << ::std::endl;
                                exit(1);
                            }
                        } else if (keyVal.path[2] == "target") {
                            checkPathLength(keyVal, 3);
                            rv.backendC.cCompiler = keyVal.value.asString();
                        } else if (keyVal.path[2] == "emulate-i128") {
                            checkPathLength(keyVal, 3);
                            rv.backendC.emulatedI128 = keyVal.value.asBool();
                        } else if (keyVal.path[2] == "compiler-opts") {
                            checkPathLength(keyVal, 3);
                            for (const auto& v : keyVal.value.asList()) {
                                rv.backendC.compilerOpts.push_back(v.asString());
                            }
                        } else if (keyVal.path[2] == "linker-opts-pre") {
                            checkPathLength(keyVal, 3);
                            for (const auto& v : keyVal.value.asList()) {
                                rv.backendC.linkerOptsPre.push_back(v.asString());
                            }
                        } else if (keyVal.path[2] == "linker-opts" || keyVal.path[2] == "linker-opts-post") {
                            checkPathLength(keyVal, 3);
                            for (const auto& v : keyVal.value.asList()) {
                                rv.backendC.linkerOptsPost.push_back(v.asString());
                            }
                        } else {
                            ::std::cerr << "WARNING: Unknown field backend.c." << keyVal.path[2] << " in " << filename << ::std::endl;
                        }
                    }
                    // Does MMIR need configuration?
                    else {
                        ::std::cerr << "WARNING: Unknown configuration item backend." << keyVal.path[1] << " in " << filename << ::std::endl;
                    }
                } else if (keyVal.path[0] == "arch") {
                    checkPathLengthMin(keyVal, 2);
                    if (keyVal.path[1] == "name") {
                        checkPathLength(keyVal, 2);
                        if (rv.arch.mName != "") {
                            ::std::cerr << "ERROR: Architecture already specified to be '" << rv.arch.mName << "'" << ::std::endl;
                            exit(1);
                        }
                        rv.arch.mName = keyVal.value.asString();
                    } else if (keyVal.path[1] == "pointer-bits") {
                        checkPathLength(keyVal, 2);
                        rv.arch.pointerBits = keyVal.value.asInt();
                    } else if (keyVal.path[1] == "is-big-endian") {
                        checkPathLength(keyVal, 2);
                        rv.arch.bigEndian = keyVal.value.asBool();
                    } else if (keyVal.path[1] == "has-atomic-u8") {
                        checkPathLength(keyVal, 2);
                        rv.arch.atomics.u8 = keyVal.value.asBool();
                    } else if (keyVal.path[1] == "has-atomic-u16") {
                        checkPathLength(keyVal, 2);
                        rv.arch.atomics.u16 = keyVal.value.asBool();
                    } else if (keyVal.path[1] == "has-atomic-u32") {
                        checkPathLength(keyVal, 2);
                        rv.arch.atomics.u32 = keyVal.value.asBool();
                    } else if (keyVal.path[1] == "has-atomic-u64") {
                        checkPathLength(keyVal, 2);
                        rv.arch.atomics.u64 = keyVal.value.asBool();
                    } else if (keyVal.path[1] == "has-atomic-ptr") {
                        checkPathLength(keyVal, 2);
                        rv.arch.atomics.ptr = keyVal.value.asBool();
                    } else if (keyVal.path[1] == "alignments") {
                        checkPathLength(keyVal, 3);
                        if (keyVal.path[2] == "u16") {
                            rv.arch.alignments.u16 = keyVal.value.asInt();
                        } else if (keyVal.path[2] == "u32") {
                            rv.arch.alignments.u32 = keyVal.value.asInt();
                        } else if (keyVal.path[2] == "u64") {
                            rv.arch.alignments.u64 = keyVal.value.asInt();
                        } else if (keyVal.path[2] == "u128") {
                            rv.arch.alignments.u128 = keyVal.value.asInt();
                        } else if (keyVal.path[2] == "f32") {
                            rv.arch.alignments.f32 = keyVal.value.asInt();
                        } else if (keyVal.path[2] == "f64") {
                            rv.arch.alignments.f64 = keyVal.value.asInt();
                        } else if (keyVal.path[2] == "ptr") {
                            rv.arch.alignments.ptr = keyVal.value.asInt();
                        } else {
                            ::std::cerr << "WARNING: Unknown field arch.alignments." << keyVal.path[1] << " in " << filename << ::std::endl;
                        }
                    } else {
                        ::std::cerr << "WARNING: Unknown field arch." << keyVal.path[1] << " in " << filename << ::std::endl;
                    }
                } else {
                    ::std::cerr << "WARNING: Unknown configuration item " << keyVal.path[0] << " in " << filename << ::std::endl;
                }
            } catch (const TomlValue::TypeError& e) {
                ::std::cerr << "ERROR: Invalid type for " << keyVal.path << " - " << e << ::std::endl;
                exit(1);
            }
        }

        // TODO: Ensure that everything is set
        if (rv.arch.mName == "") {
            ::std::cerr << "ERROR: Architecture not specified in " << filename << ::std::endl;
            exit(1);
        }
        if (rv.family == "windows" || rv.osName == "windows") {
            ::std::cerr << "ERROR: Windows targets are not supported in " << filename << ::std::endl;
            exit(1);
        }

        return rv;
    }

    void saveSpecToFile(const ::std::string& filename, const TargetSpec& spec) {
        // TODO: Have a round-trip unit test
        ::std::ofstream of(filename);

        struct H {
            static const char* tfstr(bool v) {
                return v ? "true" : "false";
            }
        };

        of << "[target]\n"
           << "family = \"" << spec.family << "\"\n"
           << "os-name = \"" << spec.osName << "\"\n"
           << "env-name = \"" << spec.envName
           << "\"\n"
           //<< "arch = \"" << spec.m_arch.m_name << "\"\n"
           << "\n"
           << "[backend.c]\n"
           << "variant = \"gnu\"\n"
           << "target = \"" << spec.backendC.cCompiler << "\"\n"
           << "compiler-opts = [";
        for (const auto& s : spec.backendC.compilerOpts) {
            of << "\"" << s << "\",";
        }
        of << "]\n"
           << "linker-opts-pre = [";
        for (const auto& s : spec.backendC.linkerOptsPre) {
            of << "\"" << s << "\",";
        }
        of << "]\n"
           << "linker-opts-post = [";
        for (const auto& s : spec.backendC.linkerOptsPost) {
            of << "\"" << s << "\",";
        }
        of << "]\n"
           << "\n"
           << "[arch]\n"
           << "name = \"" << spec.arch.mName << "\"\n"
           << "pointer-bits = " << spec.arch.pointerBits << "\n"
           << "is-big-endian = " << H::tfstr(spec.arch.bigEndian) << "\n"
           << "has-atomic-u8 = " << H::tfstr(spec.arch.atomics.u8) << "\n"
           << "has-atomic-u16 = " << H::tfstr(spec.arch.atomics.u16) << "\n"
           << "has-atomic-u32 = " << H::tfstr(spec.arch.atomics.u32) << "\n"
           << "has-atomic-u64 = " << H::tfstr(spec.arch.atomics.u64) << "\n"
           << "has-atomic-ptr = " << H::tfstr(spec.arch.atomics.ptr) << "\n"
           << "alignments = {"
           << " u16 = " << static_cast<int>(spec.arch.alignments.u16) << ","
           << " u32 = " << static_cast<int>(spec.arch.alignments.u32) << ","
           << " u64 = " << static_cast<int>(spec.arch.alignments.u64) << ","
           << " u128 = " << static_cast<int>(spec.arch.alignments.u128) << ","
           << " f32 = " << static_cast<int>(spec.arch.alignments.f32) << ","
           << " f64 = " << static_cast<int>(spec.arch.alignments.f64) << ","
           << " ptr = " << static_cast<int>(spec.arch.alignments.ptr) << " }\n"
           << "\n";
    }

    TargetSpec initFromSpecName(const ::std::string& targetName) {
// Options for all the fully-GNU environments
#define BACKEND_C_OPTS_GNU {"-ffunction-sections", "-pthread"}, {"-Wl,--start-group"}, {"-Wl,--end-group", "-Wl,--gc-sections", "-l", "atomic"}
        // If there's a '/' in the filename, open it as a path, otherwise assume it's a triple.
        if (targetName.find('/') != ::std::string::npos) {
            return loadSpecFromFile(targetName);
        } else if (targetName == "i586-linux-gnu" || targetName == "i586-unknown-linux-gnu") {
            return TargetSpec{"unix", "linux", "gnu", {true, "i586-linux-gnu", BACKEND_C_OPTS_GNU}, ARCH_X86};
        } else if (targetName == "x86_64-linux-gnu" || targetName == "x86_64-unknown-linux-gnu") {
            return TargetSpec{"unix", "linux", "gnu", {true /*false*/, "x86_64-linux-gnu", BACKEND_C_OPTS_GNU}, ARCH_X86_64};
        } else if (targetName == "x86_64-linux-musl" || targetName == "x86_64-unknown-linux-musl") {
            return TargetSpec{"unix", "linux", "musl", {true /*false*/, "x86_64-linux-musl", BACKEND_C_OPTS_GNU}, ARCH_X86_64};
        } else if (targetName == "x86_64-unknown-linux-gnux32") {
            return TargetSpec{"unix", "linux", "gnu", {true, "x86_64-unknown-linux-gnux32", BACKEND_C_OPTS_GNU}, ARCH_X32};
        } else if (targetName == "arm-linux-gnu" || targetName == "arm-unknown-linux-gnu") {
            return TargetSpec{"unix", "linux", "gnu", {true, "arm-elf-eabi", BACKEND_C_OPTS_GNU}, ARCH_ARM32};
        } else if (targetName == "aarch64-linux-gnu" || targetName == "aarch64-unknown-linux-gnu") {
            return TargetSpec{"unix", "linux", "gnu", {false, "aarch64-linux-gnu", BACKEND_C_OPTS_GNU}, ARCH_ARM64};
        } else if (targetName == "m68k-linux-gnu" || targetName == "m68k-unknown-linux-gnu") {
            return TargetSpec{"unix", "linux", "gnu", {true, "m68k-linux-gnu", BACKEND_C_OPTS_GNU}, ARCH_M68K};
        } else if (targetName == "powerpc64-unknown-linux-gnu") {
            return TargetSpec{"unix", "linux", "gnu", {false, "powerpc64-unknown-linux-gnu", BACKEND_C_OPTS_GNU}, ARCH_POWERPC64};
        } else if (targetName == "powerpc64le-unknown-linux-gnu") {
            return TargetSpec{"unix", "linux", "gnu", {false, "powerpc64le-unknown-linux-gnu", BACKEND_C_OPTS_GNU}, ARCH_POWERPC64LE};
        } else if (targetName == "riscv64-unknown-linux-gnu") {
            return TargetSpec{"unix", "linux", "gnu", {false, "riscv64-unknown-linux-gnu", BACKEND_C_OPTS_GNU}, ARCH_RISCV64};
        } else if (targetName == "riscv64-unknown-linux-musl") {
            return TargetSpec{"unix", "linux", "musl", {false, "riscv64-unknown-linux-musl", BACKEND_C_OPTS_GNU}, ARCH_RISCV64};
        } else if (targetName == "i686-unknown-freebsd") {
            return TargetSpec{"unix", "freebsd", "gnu", {true, "i686-unknown-freebsd", BACKEND_C_OPTS_GNU}, ARCH_X86};
        } else if (targetName == "x86_64-unknown-freebsd") {
            return TargetSpec{"unix", "freebsd", "gnu", {false, "x86_64-unknown-freebsd", BACKEND_C_OPTS_GNU}, ARCH_X86_64};
        } else if (targetName == "arm-unknown-freebsd") {
            return TargetSpec{"unix", "freebsd", "gnu", {true, "arm-unknown-freebsd", BACKEND_C_OPTS_GNU}, ARCH_ARM32};
        } else if (targetName == "aarch64-unknown-freebsd") {
            return TargetSpec{"unix", "freebsd", "gnu", {false, "aarch64-unknown-freebsd", BACKEND_C_OPTS_GNU}, ARCH_ARM64};
        } else if (targetName == "x86_64-unknown-netbsd") {
            return TargetSpec{"unix", "netbsd", "gnu", {false, "x86_64-unknown-netbsd", BACKEND_C_OPTS_GNU}, ARCH_X86_64};
        } else if (targetName == "i686-unknown-openbsd") {
            return TargetSpec{"unix", "openbsd", "gnu", {true, "i686-unknown-openbsd", BACKEND_C_OPTS_GNU}, ARCH_X86};
        } else if (targetName == "x86_64-unknown-openbsd") {
            return TargetSpec{"unix", "openbsd", "gnu", {false, "x86_64-unknown-openbsd", BACKEND_C_OPTS_GNU}, ARCH_X86_64};
        } else if (targetName == "arm-unknown-openbsd") {
            return TargetSpec{"unix", "openbsd", "gnu", {true, "arm-unknown-openbsd", BACKEND_C_OPTS_GNU}, ARCH_ARM32};
        } else if (targetName == "aarch64-unknown-openbsd") {
            return TargetSpec{"unix", "openbsd", "gnu", {false, "aarch64-unknown-openbsd", BACKEND_C_OPTS_GNU}, ARCH_ARM64};
        } else if (targetName == "x86_64-unknown-dragonfly") {
            return TargetSpec{"unix", "dragonfly", "gnu", {false, "x86_64-unknown-dragonfly", BACKEND_C_OPTS_GNU}, ARCH_X86_64};
        }
        // `*-apple-darwin` has an empty target_env (as in rustc); "gnu" selects glibc-only code on a platform with no glibc.
        else if (targetName == "i686-apple-darwin") {
            // NOTE: OSX uses Mach-O binaries, which don't fully support the defaults used for GNU targets
            // The first 32bit Intel Mac was Core Solo aka yonah. It allows to use `-march=yonah` like Rust.
            return TargetSpec{"unix", "macos", "", {false, "x86_64-apple-darwin", {"-march=yonah"}, {}}, ARCH_X86_64};
        } else if (targetName == "x86_64-apple-darwin") {
            // NOTE: OSX uses Mach-O binaries, which don't fully support the defaults used for GNU targets
            // The first 64bit Intel Mac was Core Duo. It allows to use `-march=core2` like Rust.
            return TargetSpec{"unix", "macos", "", {false, "x86_64-apple-darwin", {"-march=core2"}, {}}, ARCH_X86_64};
        } else if (targetName == "aarch64-apple-darwin") {
            // NOTE: OSX uses Mach-O binaries, which don't fully support the defaults used for GNU targets
            return TargetSpec{"unix", "macos", "", {false, "aarch64-apple-darwin", {}, {}}, ARCH_ARM64};
        } else if (targetName == "powerpc-apple-darwin") {
            // NOTE: OSX uses Mach-O binaries, which don't fully support the defaults used for GNU targets
            // NOTE: 32-bit PowerPC needs libatomic for the 8-byte atomics (see ARCH_POWERPC)
            return TargetSpec{"unix", "macos", "", {true, "powerpc-apple-darwin", {}, {}, {"-l", "atomic"}}, ARCH_POWERPC};
        } else if (targetName == "powerpc64-apple-darwin") {
            // NOTE: OSX uses Mach-O binaries, which don't fully support the defaults used for GNU targets
            return TargetSpec{"unix", "macos", "", {false, "powerpc64-apple-darwin", {}, {}}, ARCH_POWERPC64};
        } else if (targetName == "arm-unknown-haiku") {
            return TargetSpec{"unix", "haiku", "gnu", {true, "arm-unknown-haiku", {}, {}}, ARCH_ARM32};
        } else if (targetName == "x86_64-unknown-haiku") {
            return TargetSpec{"unix", "haiku", "gnu", {false, "x86_64-unknown-haiku", {}, {}}, ARCH_X86_64};
        } else {
            ::std::cerr << "Unknown target name '" << targetName << "'" << ::std::endl;
            abort();
        }
        throw "";
    }
}

const TargetSpec& TargetGetCurSpec(const WireBoard& wb) {
    return *wb.target;
}

void TargetExportCurSpec(const WireBoard& wb, const ::std::string& filename) {
    saveSpecToFile(filename, *wb.target);
}

void TargetSetCfg(WireBoard& wb, const ::std::string& targetName) {
    auto& settings = *wb.settings;
    auto* spec = wb.pool->make<TargetSpec>(initFromSpecName(targetName));
    wb.target = spec;
    if (spec->arch.pointerBits != 64 || spec->arch.bigEndian) {
        ::std::cerr << "error: unsupported target `" << targetName
                    << "`: only 64-bit little-endian targets are supported" << ::std::endl;
        abort();
    }
    const TargetSpec& tgt = *spec;

    if (tgt.family == "unix") {
        CfgSetFlag(settings, "unix");
    }
    CfgSetValue(settings, "target_family", tgt.family);

    if (tgt.osName == "linux") {
        CfgSetFlag(settings, "linux");
        CfgSetValue(settings, "target_vendor", "gnu");
    }

    if (tgt.osName == "macos") {
        CfgSetFlag(settings, "apple");
        CfgSetValue(settings, "target_vendor", "apple");
    }

    if (tgt.osName == "freebsd") {
        CfgSetFlag(settings, "freebsd");
        CfgSetValue(settings, "target_vendor", "unknown");
    }

    if (tgt.osName == "netbsd") {
        CfgSetFlag(settings, "netbsd");
        CfgSetValue(settings, "target_vendor", "unknown");
    }

    if (tgt.osName == "openbsd") {
        CfgSetFlag(settings, "openbsd");
        CfgSetValue(settings, "target_vendor", "unknown");
    }

    if (tgt.osName == "dragonfly") {
        CfgSetFlag(settings, "dragonfly");
        CfgSetValue(settings, "target_vendor", "unknown");
    }

    CfgSetValue(settings, "target_vendor", ""); // NOTE: Doesn't override a pre-set value
    CfgSetValue(settings, "target_env", tgt.envName);
    CfgSetValue(settings, "target_os", tgt.osName);
    CfgSetValue(settings, "target_pointer_width", FMT(tgt.arch.pointerBits));
    CfgSetValue(settings, "target_endian", tgt.arch.bigEndian ? "big" : "little");
    CfgSetValue(settings, "target_arch", tgt.arch.mName);
    CfgSetValue(settings, "target_abi", "llvm"); // This is a lie, but hopefully works?
    // target_has_atomic_equal_alignment="N" means align_of::<AtomicN>() == align_of::<N>().
    // Since libcore declares AtomicN with repr(align(sizeof(N))), only set it when the
    // primitive's natural alignment already matches its size (e.g. u64 on x86 has align 4,
    // so target_has_atomic_equal_alignment="64" must be unset there even with cmpxchg8b).
    if (tgt.arch.atomics.u8) {
        CfgSetValue(settings, "target_has_atomic", "8");
        CfgSetValue(settings, "target_has_atomic_load_store", "8");
        CfgSetValue(settings, "target_has_atomic_equal_alignment", "8");
    }
    if (tgt.arch.atomics.u16) {
        CfgSetValue(settings, "target_has_atomic", "16");
        CfgSetValue(settings, "target_has_atomic_load_store", "16");
        if (tgt.arch.alignments.u16 >= 2) {
            CfgSetValue(settings, "target_has_atomic_equal_alignment", "16");
        }
    }
    if (tgt.arch.atomics.u32) {
        CfgSetValue(settings, "target_has_atomic", "32");
        CfgSetValue(settings, "target_has_atomic_load_store", "32");
        if (tgt.arch.alignments.u32 >= 4) {
            CfgSetValue(settings, "target_has_atomic_equal_alignment", "32");
        }
    }
    if (tgt.arch.atomics.u64) {
        CfgSetValue(settings, "target_has_atomic", "64");
        CfgSetValue(settings, "target_has_atomic_load_store", "64");
        if (tgt.arch.alignments.u64 >= 8) {
            CfgSetValue(settings, "target_has_atomic_equal_alignment", "64");
        }
    }
    if (tgt.arch.atomics.ptr) {
        CfgSetValue(settings, "target_has_atomic", "ptr");
        CfgSetValue(settings, "target_has_atomic_load_store", "ptr");
        if (tgt.arch.alignments.ptr * 8u >= tgt.arch.pointerBits) {
            CfgSetValue(settings, "target_has_atomic_equal_alignment", "ptr");
        }
    }
    // TODO: Atomic compare-and-set option
    if (tgt.arch.atomics.ptr) {
        CfgSetValue(settings, "target_has_atomic", "cas");
    }
    CfgSetValueCb(settings, "target_feature", [](const ::std::string& s) {
        //if(g_target.m_arch.m_name == "x86_64" && s == "sse2") return true;    // 1.39 ppv-lite86 requires sse2 (x86_64 always has it)
        return false;
    });
}

namespace {
    bool closureHasNoCaptures(const StaticTraitResolve& resolve, const HIRExprNodeClosure& closure) {
        if (closure.cls == HIRExprNodeClosure::Class::NoCapture) {
            return true;
        }
        if (closure.cls != HIRExprNodeClosure::Class::Unknown) {
            return false;
        }

        struct CaptureVisitor: HIRExprVisitorDef {
            ::std::vector<unsigned int> definitions;
            ::std::vector<unsigned int> uses;

            explicit CaptureVisitor(HIRTypeInterner& types)
                : HIRExprVisitorDef(types)
            {
            }

            void visitPattern(const Span& sp, HIRPattern& pattern) override {
                for (const auto& binding : pattern.mBindings) {
                    definitions.push_back(binding.slot);
                }
                if (const auto* split = pattern.mData.opt_SplitSlice(); split && split->extraBind.isValid()) {
                    definitions.push_back(split->extraBind.slot);
                }
                HIRExprVisitorDef::visitPattern(sp, pattern);
            }

            void visit(HIRExprNodeVariable& node) override {
                uses.push_back(node.slot);
            }
        } visitor(resolve.hirCrate().types);

        // The expression visitor predates const traversal. This pass is read-only;
        // it only uses the mutable interface to enumerate the closure's patterns
        // and variable nodes before the normal capture-annotation phase.
        const_cast<HIRExprNodeClosure&>(closure).visit(visitor);
        ::std::sort(visitor.definitions.begin(), visitor.definitions.end());
        visitor.definitions.erase(::std::unique(visitor.definitions.begin(), visitor.definitions.end()), visitor.definitions.end());
        return ::std::all_of(visitor.uses.begin(), visitor.uses.end(), [&](unsigned int slot) {
            return ::std::binary_search(visitor.definitions.begin(), visitor.definitions.end(), slot);
        });
    }
}

bool TargetGetSizeAndAlignOf(const Span& sp, const StaticTraitResolve& resolve, const HIRTypeData* ty, size_t& outSize, size_t& outAlign) {
    TU_MATCH_HDRA( (*ty), {)
    TU_ARMA(Infer, te) {
            BUG(sp, "sizeof on _ type");
        }
        TU_ARMA(Diverge, te) {
            outSize = 0;
            outAlign = 1;
            return true;
        }
        TU_ARMA(Primitive, te) {
            switch (te) {
                case HIRCoreType::Bool:
                case HIRCoreType::U8:
                case HIRCoreType::I8:
                    outSize = 1;
                    outAlign = 1; // u8 is always 1 aligned
                    return true;
                case HIRCoreType::U16:
                case HIRCoreType::I16:
                    outSize = 2;
                    outAlign = TargetGetCurSpec(resolve.board()).arch.alignments.u16;
                    return true;
                case HIRCoreType::U32:
                case HIRCoreType::I32:
                case HIRCoreType::Char:
                    outSize = 4;
                    outAlign = TargetGetCurSpec(resolve.board()).arch.alignments.u32;
                    return true;
                case HIRCoreType::U64:
                case HIRCoreType::I64:
                    outSize = 8;
                    outAlign = TargetGetCurSpec(resolve.board()).arch.alignments.u64;
                    return true;
                case HIRCoreType::U128:
                case HIRCoreType::I128:
                    outSize = 16;
                    // TODO: If i128 is emulated, this can be 8 (as it is on x86, where it's actually 4 due to the above comment)
                    if (TargetGetCurSpec(resolve.board()).backendC.emulatedI128) {
                        outAlign = TargetGetCurSpec(resolve.board()).arch.alignments.u64;
                    } else {
                        outAlign = TargetGetCurSpec(resolve.board()).arch.alignments.u128;
                    }
                    return true;
                case HIRCoreType::Usize:
                case HIRCoreType::Isize:
                    outSize = TargetGetCurSpec(resolve.board()).arch.pointerBits / 8;
                    outAlign = TargetGetCurSpec(resolve.board()).arch.alignments.ptr;
                    return true;
                case HIRCoreType::F16:
                    outSize = 2;
                    outAlign = 2; //g_target.m_arch.m_alignments.f32; //f16;
                    return true;
                case HIRCoreType::F32:
                    outSize = 4;
                    outAlign = TargetGetCurSpec(resolve.board()).arch.alignments.f32;
                    return true;
                case HIRCoreType::F64:
                    outSize = 8;
                    outAlign = TargetGetCurSpec(resolve.board()).arch.alignments.f64;
                    return true;
                case HIRCoreType::F128:
                    outSize = 16;
                    outAlign = TargetGetCurSpec(resolve.board()).arch.alignments.u128;
                    return true;
                case HIRCoreType::Str:
                    DEBUG("sizeof on a `str` - unsized");
                    outSize = SIZE_MAX;
                    outAlign = 1;
                    return true;
            }
        }
        TU_ARMA(Path, te) {
            if (te.binding.is_Opaque()) {
                return false;
            }
            if (te.binding.is_ExternType()) {
                DEBUG("sizeof on extern type - unsized");
                outAlign = 0;
                outSize = SIZE_MAX;
                return true;
            }
            const auto* repr = TargetGetTypeRepr(sp, resolve, ty);
            if (!repr) {
                DEBUG("Cannot get type repr for " << ty);
                return false;
            }
            outSize = repr->size;
            outAlign = repr->align;
            return true;
        }
        TU_ARMA(Generic, te) {
            // Unknown - return false
            DEBUG("No repr for Generic - " << ty);
            return false;
        }
        TU_ARMA(TraitObject, te) {
            outAlign = 0;
            outSize = SIZE_MAX;
            DEBUG("sizeof on a trait object - unsized");
            return true;
        }
        TU_ARMA(ErasedType, te) {
            BUG(sp, "sizeof on an erased type - shouldn't exist");
        }
        TU_ARMA(Array, te) {
            if (!TargetGetSizeAndAlignOf(sp, resolve, te.inner, outSize, outAlign)) {
                return false;
            }
            if (outSize == SIZE_MAX) {
                // An inconsistent but accepted parameter environment can make
                // an otherwise-unsized element usable here (e.g. `str: Copy`
                // under `trivial_bounds`).  MIR construction must preserve the
                // symbolic layout just as it does for a generic element.
                return false;
            }
            if (!te.size.is_Known()) {
                DEBUG("Size unknown - " << ty);
                return false;
            }
            if (te.size.as_Known() == 0 || outSize == 0) {
                outSize = 0;
            } else {
                if (SIZE_MAX / te.size.as_Known() <= outSize) {
                    BUG(sp, "Integer overflow calculating array size");
                }
                outSize *= te.size.as_Known();
            }
            return true;
        }
        TU_ARMA(Slice, te) {
            if (!TargetGetAlignOf(sp, resolve, te.inner, outAlign)) {
                return false;
            }
            outSize = SIZE_MAX;
            DEBUG("sizeof on a slice - unsized");
            return true;
        }
        TU_ARMA(Tuple, te) {
            const auto* repr = TargetGetTypeRepr(sp, resolve, ty);
            if (!repr) {
                DEBUG("Cannot get type repr for " << ty);
                return false;
            }
            outSize = repr->size;
            outAlign = repr->align;
            return true;
        }
        TU_ARMA(Borrow, te) {
            // - Alignment is machine native
            outAlign = TargetGetCurSpec(resolve.board()).arch.pointerBits / 8;
            // - Size depends on Sized-nes of the parameter
            // TODO: Handle different types of Unsized (ones with different pointer sizes)
            switch (resolve.metadataType(sp, te.inner)) {
                case MetadataType::Unknown:
                    return false;
                case MetadataType::None:
                case MetadataType::Zero:
                    outSize = TargetGetCurSpec(resolve.board()).arch.pointerBits / 8;
                    break;
                case MetadataType::Slice:
                case MetadataType::TraitObject:
                    outSize = TargetGetCurSpec(resolve.board()).arch.pointerBits / 8 * 2;
                    break;
            }
            return true;
        }
        TU_ARMA(Pointer, te) {
            // - Alignment is machine native
            outAlign = TargetGetCurSpec(resolve.board()).arch.pointerBits / 8;
            // - Size depends on Sized-nes of the parameter
            switch (resolve.metadataType(sp, te.inner)) {
                case MetadataType::Unknown:
                    return false;
                case MetadataType::None:
                case MetadataType::Zero:
                    outSize = TargetGetCurSpec(resolve.board()).arch.pointerBits / 8;
                    break;
                case MetadataType::Slice:
                case MetadataType::TraitObject:
                    outSize = TargetGetCurSpec(resolve.board()).arch.pointerBits / 8 * 2;
                    break;
            }
            return true;
        }
        TU_ARMA(NamedFunction, te) {
            // Zero size
            outSize = 0;
            outAlign = 1;
            return true;
        }
        TU_ARMA(Function, te) {
            // Pointer size
            outSize = TargetGetCurSpec(resolve.board()).arch.pointerBits / 8;
            outAlign = TargetGetCurSpec(resolve.board()).arch.pointerBits / 8;
            return true;
        }
        TU_ARMA(NodeType, te) {
            if (const auto* closure = te.opt_Closure(); closure && closureHasNoCaptures(resolve, **closure)) {
                outSize = 0;
                outAlign = 1;
                return true;
            }
            return false;
        }
        TU_ARMA(Pattern, te) {
            return TargetGetSizeAndAlignOf(sp, resolve, te.inner, outSize, outAlign);
        }
    }
    return false;
}

bool TargetGetSizeOf(const Span& sp, const StaticTraitResolve& resolve, const HIRTypeData* ty, size_t& outSize) {
    size_t ignoreAlign;
    bool rv = TargetGetSizeAndAlignOf(sp, resolve, ty, outSize, ignoreAlign);
    if (rv && outSize == SIZE_MAX) {
        BUG(sp, "Getting size of Unsized type - " << ty);
    }
    return rv;
}

bool TargetGetAlignOf(const Span& sp, const StaticTraitResolve& resolve, const HIRTypeData* ty, size_t& outAlign) {
    size_t ignoreSize;
    bool rv = TargetGetSizeAndAlignOf(sp, resolve, ty, ignoreSize, outAlign);
    if (rv && ignoreSize == SIZE_MAX) {
        BUG(sp, "Getting alignment of Unsized type - " << ty);
    }
    return rv;
}

namespace {
    void setTypeRepr(const Span& sp, const HIRTypeData* ty, ::std::unique_ptr<TypeRepr> repr);

    struct Ent {
        unsigned int field;
        size_t size;
        size_t align;
        HIRTypeRef ty;
        /// `align` came from an explicit `repr(align(N))` somewhere inside `ty`.
        bool userAlign = false;
    };

    ::std::ostream& operator<<(std::ostream& os, const Ent& e) {
        os << "Ent { #" << e.field << ": s=" << e.size << " a=" << e.align << (e.userAlign ? "!" : "") << " : " << e.ty << " }";
        return os;
    }

    bool makeFieldEnt(const Span& sp, const StaticTraitResolve& resolve, unsigned idx, HIRTypeRef ty, Ent& out) {
        size_t size, align;
        if (!TargetGetSizeAndAlignOf(sp, resolve, ty, size, align)) {
            DEBUG("Can't get size/align of " << ty);
            return false;
        }
        out = Ent{idx, size, align, HIRTypeRef(), false};
        out.userAlign = TargetTypeHasUserAlignment(sp, resolve, ty);
        out.ty = mv$(ty);
        return true;
    }

    bool structEnumerateFields(const Span& sp, const StaticTraitResolve& resolve, const HIRTypeData* ty, ::std::vector<Ent>& ents) {
        const auto& te = ty->as_Path();
        const auto& str = *te.binding.as_Struct();
        // TODO: Wipe lifetimes?
        auto monomorphCb = MonomorphStatePtr(resolve.hirCrate().types, ty, &te.path.mData.as_Generic().mParams, nullptr);
        auto monomorph = [&](const auto& tpl) {
            return resolve.monomorphExpand(sp, tpl, monomorphCb);
        };
        TU_MATCH_HDRA( (str.mData), {)
        TU_ARMA(Unit, se) {
            }
            TU_ARMA(Tuple, se) {
                unsigned int idx = 0;
                for (const auto& e : se) {
                    Ent ent;
                    if (!makeFieldEnt(sp, resolve, idx, monomorph(e.ent), ent)) {
                        return false;
                    }
                    DEBUG("#" << idx << ": " << ent);
                    idx++;
                    ents.push_back(mv$(ent));
                }
            }
            TU_ARMA(Named, se) {
                unsigned int idx = 0;
                for (const auto& e : se) {
                    Ent ent;
                    if (!makeFieldEnt(sp, resolve, idx, monomorph(e.ty), ent)) {
                        return false;
                    }
                    DEBUG("#" << idx << " " << e.name << ": " << ent);
                    idx++;
                    ents.push_back(mv$(ent));
                }
            }
        }
        return true;
    }

    enum class StructSorting {
        None,
        AllButFinal,
        All,
    };

    size_t structFieldAlignmentGroup(const Ent& e, unsigned maxAlignment) {
        if (maxAlignment > 0) {
            return ::std::min<size_t>(e.align, maxAlignment);
        }

        // Match rustc's default layout grouping: an aggregate whose size has a
        // stronger power-of-two factor than its ABI alignment is grouped with
        // fields of that effective alignment. For example, [u8; 4] belongs to
        // the align-4 group, while [u8; 6] belongs to align-2.
        const size_t sizeAsAlign = ::std::max(e.size, e.align);
        return sizeAsAlign & (~sizeAsAlign + 1);
    }

    bool sortfnEnumVariantFields(const Ent& a, const Ent& b) {
        return a.align != b.align ? a.align < b.align : a.size < b.size;
    }

    /// Generate a struct representation using the provided entries
    ///
    /// - Handles (optional) sorting and packing
    ::std::unique_ptr<TypeRepr> makeTypeReprStructInner(const Span& sp, const HIRTypeData* ty, ::std::vector<Ent>& ents, StructSorting sorting, unsigned forcedAlignment, unsigned maxAlignment) {
        if (ents.size() > 0) {
            auto sortFields = [&](auto first, auto last) {
                ::std::stable_sort(first, last, [&](const Ent& a, const Ent& b) {
                    return structFieldAlignmentGroup(a, maxAlignment) > structFieldAlignmentGroup(b, maxAlignment);
                });
            };
            switch (sorting) {
                case StructSorting::None:
                    break;
                case StructSorting::AllButFinal:
                    sortFields(ents.begin(), ents.end() - 1);
                    break;
                case StructSorting::All:
                    sortFields(ents.begin(), ents.end());
                    break;
            }
        }

        unsigned maxField = 0;
        for (const auto& e : ents) {
            if (e.field != ~0u) {
                maxField = std::max(maxField, e.field);
            }
        }
        ::std::vector<TypeRepr::Field> fields(ents.size() > 0 ? maxField + 1 : 0);

        TypeRepr rv;
        size_t curOfs = 0;
        size_t maxAlign = 1;
        bool isFirstField = true;
        for (auto& e : ents) {
            auto align = e.align;

            // PowerPC 32-bit ABI
            // First element uses natural alignment, subsequent elements with natural alignment
            // >= 4 and up to 8 use embedding = 4. Skip ZST.
            // The cap is on natural alignment only: an explicitly aligned member keeps it, as in gcc.
            if (TargetCapsMemberAlignment()) {
                if (e.size > 0) {
                    if (!isFirstField && !e.userAlign && align >= 4 && align <= 8) {
                        align = 4;
                    }
                    isFirstField = false;
                }
            }
            if (e.userAlign) {
                rv.userAlign = true;
            }

            // Increase offset to fit alignment
            align = maxAlignment > 0 ? std::min<size_t>(align, maxAlignment) : align;
            if (align > 0) {
                while (curOfs % align != 0) {
                    curOfs++;
                }
            }
            maxAlign = ::std::max(maxAlign, align);

            // Forced padding is indicated by setting the field index to -1
            if (e.field != ~0u) {
                ASSERT_BUG(sp, e.field < fields.size(), "Field index out of range");
                ASSERT_BUG(sp, fields[e.field].ty == HIRTypeRef(), "Dupliate field index");
                fields[e.field].offset = curOfs;
                fields[e.field].ty = e.ty;
            }
            DEBUG("#" << e.field << " @" << curOfs << "+" << e.size << " : " << e.ty);
            if (e.size == SIZE_MAX) {
                // Ensure that this is the last item
                ASSERT_BUG(sp, &e == &ents.back(), "Unsized item isn't the last item in " << ty);
                curOfs = SIZE_MAX;
            } else {
                curOfs += e.size;
            }
        }
        if (forcedAlignment > 0) {
            maxAlign = std::max(maxAlign, static_cast<size_t>(forcedAlignment));
            // `repr(align(N))` - this is the root of a user-alignment chain.
            rv.userAlign = true;
        }
        // If not packing (and the size isn't infinite/unsized) then round the size up to the alignment
        if (curOfs != SIZE_MAX) {
            // Size must be a multiple of alignment
            while (curOfs % maxAlign != 0) {
                curOfs++;
            }
        }
        for (const auto& f : fields) {
            ASSERT_BUG(sp, f.ty != HIRTypeRef(), "Uninitialised field found - " << (&f - &fields[0]));
        }
        // Aligment is 1 for packed structs, and `max_align` otherwise
        rv.align = maxAlign;
        rv.size = curOfs;
        rv.fields = ::std::move(fields);
        DEBUG(ty << ": size = " << rv.size << ", align = " << rv.align);
        return box$(rv);
    }

    // Returns NULL when the repr can't be determined
    ::std::unique_ptr<TypeRepr> makeTypeReprStruct(const Span& sp, const StaticTraitResolve& resolve, const HIRTypeData* ty) {
        TRACE_FUNCTION_F(ty);
        ::std::vector<Ent> ents;
        StructSorting sorting;
        unsigned forcedAlignment = 0;
        unsigned maxAlignment = 0;
        if (ty->is_Path() && ty->as_Path().binding.is_Struct()) {
            const auto& te = ty->as_Path();
            const auto& str = *te.binding.as_Struct();

            if (!structEnumerateFields(sp, resolve, ty, ents)) {
                return nullptr;
            }

            forcedAlignment = str.forcedAlignment;
            maxAlignment = str.maxFieldAlignment;
            sorting = StructSorting::None; // Defensive default for if repr is invalid
            switch (str.repr) {
                case HIRStruct::Repr::C:
                case HIRStruct::Repr::Simd:
                    // No sorting, no packing
                    sorting = StructSorting::None;
                    break;
                case HIRStruct::Repr::Transparent:
                case HIRStruct::Repr::Rust:
                    if (str.structMarkings.dstType != HIRStructMarkings::DstType::None) {
                        sorting = StructSorting::AllButFinal;
                    } else {
                        sorting = StructSorting::All;
                    }
                    break;
            }
            if (maxAlignment == 1) {
                sorting = StructSorting::None;
            }
        } else if (const auto* te = ty->opt_Tuple()) {
            DEBUG("Tuple " << ty);
            unsigned int idx = 0;
            for (const auto& t : *te) {
                Ent ent;
                if (!makeFieldEnt(sp, resolve, idx, t, ent)) {
                    return nullptr;
                }
                idx++;
                ents.push_back(mv$(ent));
            }
            sorting = StructSorting::All;
        } else {
            BUG(sp, "Unexpected type in creating type repr - " << ty);
        }

        return makeTypeReprStructInner(sp, ty, ents, sorting, forcedAlignment, maxAlignment);
    }

    bool boundedMaxIsFullRange(const HIRTypeData* ty, U128 boundedMax) {
        if (const auto* primitive = ty->opt_Primitive()) {
            switch (*primitive) {
                case HIRCoreType::U8:
                case HIRCoreType::I8:
                    return boundedMax == U128(UINT8_MAX);
                case HIRCoreType::U16:
                case HIRCoreType::I16:
                    return boundedMax == U128(UINT16_MAX);
                case HIRCoreType::U32:
                case HIRCoreType::I32:
                    return boundedMax == U128(UINT32_MAX);
                case HIRCoreType::U64:
                case HIRCoreType::I64:
                    return boundedMax == U128(UINT64_MAX);
                case HIRCoreType::U128:
                case HIRCoreType::I128:
                    return boundedMax == U128(UINT64_MAX, UINT64_MAX);
                case HIRCoreType::Usize:
                case HIRCoreType::Isize:
                    return boundedMax == (TargetGetPointerBits() == 64 ? U128(UINT64_MAX) : U128(UINT32_MAX));
                default:
                    return false;
            }
        }
        if (ty->is_Pointer()) {
            return boundedMax == (TargetGetPointerBits() == 64 ? U128(UINT64_MAX) : U128(UINT32_MAX));
        }
        return false;
    }

    bool getPatternValidRanges(const HIRTypeData::Data_Pattern& pattern, size_t& scalarSize, ::std::vector<::std::pair<size_t, size_t>>& ranges) {
        const auto* primitive = pattern.inner->opt_Primitive();
        if (!primitive) {
            return false;
        }

        size_t defaultMax;
        switch (*primitive) {
            case HIRCoreType::Bool:
                scalarSize = 1;
                defaultMax = 1;
                break;
            case HIRCoreType::U8:
                scalarSize = 1;
                defaultMax = UINT8_MAX;
                break;
            case HIRCoreType::U16:
                scalarSize = 2;
                defaultMax = UINT16_MAX;
                break;
            case HIRCoreType::U32:
                scalarSize = 4;
                defaultMax = UINT32_MAX;
                break;
            case HIRCoreType::U64:
                if (sizeof(size_t) < 8) return false;
                scalarSize = 8;
                defaultMax = SIZE_MAX;
                break;
            case HIRCoreType::Usize:
                scalarSize = TargetGetPointerBits() / 8;
                if (scalarSize > sizeof(size_t)) return false;
                defaultMax = scalarSize == sizeof(size_t) ? SIZE_MAX : (size_t(1) << (scalarSize * 8)) - 1;
                break;
            case HIRCoreType::Char:
                scalarSize = 4;
                defaultMax = 0x10FFFF;
                break;
            default:
                // Signed ranges need signed ordering, and 128-bit scalars cannot
                // be represented by TypeRepr's size_t niche value.
                return false;
        }

        ranges.clear();
        ranges.reserve(pattern.pattern.alternatives.size());
        for (const auto& range : pattern.pattern.alternatives) {
            size_t start = 0;
            size_t end = defaultMax;
            if (range.hasStart) {
                const auto* value = range.start.opt_Evaluated();
                if (!value) return false;
                const auto encoded = EncodedLiteralSlice(**value).readUint();
                if (!encoded.isU64() || encoded.truncateU64() > SIZE_MAX) return false;
                start = static_cast<size_t>(encoded.truncateU64());
            }
            if (range.hasEnd) {
                const auto* value = range.end.opt_Evaluated();
                if (!value) return false;
                const auto encoded = EncodedLiteralSlice(**value).readUint();
                if (!encoded.isU64() || encoded.truncateU64() > SIZE_MAX) return false;
                end = static_cast<size_t>(encoded.truncateU64());
                if (!range.endInclusive) {
                    if (end == 0) return false;
                    end--;
                }
            }
            if (start > end || end > defaultMax) return false;
            ranges.push_back({start, end});
        }
        if (ranges.empty()) return false;

        ::std::sort(ranges.begin(), ranges.end());
        size_t out = 0;
        for (const auto& range : ranges) {
            if (out != 0 && range.first <= ranges[out - 1].second + (ranges[out - 1].second != SIZE_MAX)) {
                ranges[out - 1].second = ::std::max(ranges[out - 1].second, range.second);
            } else {
                ranges[out++] = range;
            }
        }
        ranges.resize(out);
        return true;
    }

    bool getNonzeroPath(const Span& sp, const StaticTraitResolve& resolve, const HIRTypeData* ty, TypeRepr::FieldPath& outPath) {
        switch (ty->tag()) {
            TU_ARM(*ty, Tuple, te) {
                const TypeRepr* repr = TargetGetTypeRepr(sp, resolve, ty);
                if (!repr) {
                    return false;
                }
                for (size_t i = 0; i < repr->fields.size(); i++) {
                    if (getNonzeroPath(sp, resolve, repr->fields[i].ty, outPath)) {
                        outPath.subFields.push_back(i);
                        return true;
                    }
                }
            }
            break;
            TU_ARM(*ty, Array, te) {
                if (te.size.is_Known() && te.size.as_Known() > 0 && getNonzeroPath(sp, resolve, te.inner, outPath)) {
                    outPath.subFields.push_back(TypeRepr::FieldPath::ARRAY_ELEMENT);
                    return true;
                }
            }
            break;
            TU_ARM(*ty, Path, te) {
                if (te.binding.is_Struct()) {
                    const auto* str = te.binding.as_Struct();
                    const TypeRepr* r = TargetGetTypeRepr(sp, resolve, ty);
                    if (!r) {
                        return false;
                    }
                    if (str->structMarkings.isNoNiche) {
                        return false;
                    }
                    // Preserve the full invalid range for the general niche
                    // layout instead of collapsing it to the zero value.
                    if (str->structMarkings.boundedMax && (r->fields.size() != 1 || !boundedMaxIsFullRange(r->fields[0].ty, str->structMarkings.boundedMaxValue))) {
                        return false;
                    }
                    for (size_t i = 0; i < r->fields.size(); i++) {
                        if (getNonzeroPath(sp, resolve, r->fields[i].ty, outPath)) {
                            outPath.subFields.push_back(i);
                            return true;
                        }
                    }
                    // 1.39 marks these with #[rustc_nonnull_optimization_guaranteed] instead
                    if (str->structMarkings.isNonzero) {
                        DEBUG(ty << " tagged NonZero");
                        outPath.subFields.push_back(0);
                        outPath.size = r->size;
                        if ((r->fields[0].ty->is_Pointer() || r->fields[0].ty->is_Borrow()) && outPath.size > TargetGetPointerBits() / 8) {
                            // A wide pointer's validity niche is only its data
                            // address; metadata occupies the following word.
                            outPath.size = TargetGetPointerBits() / 8;
                        }
                        return true;
                    }

                } else if (te.binding.is_Enum()) {
                    const TypeRepr* repr = TargetGetTypeRepr(sp, resolve, ty);
                    if (!repr) {
                        return false;
                    }
                    if (const auto* values = repr->variants.opt_Values()) {
                        if (std::find(values->values.begin(), values->values.end(), 0) == values->values.end()) {
                            outPath.subFields.insert(outPath.subFields.end(), values->field.subFields.rbegin(), values->field.subFields.rend());
                            outPath.subFields.push_back(values->field.index);
                            outPath.size = values->field.size;
                            return true;
                        }
                    }
                }
            }
            break;
            TU_ARM(*ty, Borrow, _te) {
                (void)_te;
                // TODO: Only return a single-pointer size
                outPath.size = TargetGetPointerBits() / 8;
                return true;
            }
            break;
            TU_ARM(*ty, Function, _te)(void) _te;
            TargetGetSizeOf(sp, resolve, ty, outPath.size);
            return true;
            TU_ARM(*ty, Pattern, te) {
                ::std::vector<::std::pair<size_t, size_t>> ranges;
                if (getPatternValidRanges(te, outPath.size, ranges) && ranges.front().first != 0) {
                    return true;
                }
            }
            break;
            default:
                break;
        }
        return false;
    }

    size_t getSizeOrZero(const Span& sp, const StaticTraitResolve& resolve, const HIRTypeData* ty) {
        size_t size = 0;
        TargetGetSizeOf(sp, resolve, ty, size);
        return size;
    }

    size_t getOffset(const Span& sp, const StaticTraitResolve& resolve, const TypeRepr* r, const TypeRepr::FieldPath& outPath) {
        assert(outPath.index < r->fields.size());
        size_t ofs = r->fields[outPath.index].offset;

        const auto* ty = &r->fields[outPath.index].ty;
        for (const auto& f : outPath.subFields) {
            if (f == TypeRepr::FieldPath::ARRAY_ELEMENT) {
                const auto* array = (*ty)->opt_Array();
                assert(array && array->size.is_Known() && array->size.as_Known() > 0);
                ty = &array->inner;
                continue;
            }
            r = TargetGetTypeRepr(sp, resolve, *ty);
            assert(f < r->fields.size());
            ofs += r->fields[f].offset;
            ty = &r->fields[f].ty;
        }

        return ofs;
    }

    /// <summary>
    /// Locate a suitable niche location in the given path (an enum that has space in its tag)
    /// </summary>
    /// <param name="sp"></param>
    /// <param name="resolve"></param>
    /// <param name="ty"></param>
    /// <param name="out_path">Path to the variant field</param>
    /// <param name="requiredCount">Number of discriminants that must fit in the niche</param>
    /// <param name="nicheStart">First scalar value reserved for the outer enum</param>
    bool getVariantNichePath(const Span& sp, const StaticTraitResolve& resolve, const HIRTypeData* ty, size_t minOffset, size_t maxOffset, size_t requiredCount, TypeRepr::FieldPath& outPath, size_t& nicheStart) {
        TRACE_FUNCTION_F(ty << " min_offset=" << minOffset << " max_offset=" << maxOffset << " required_count=" << requiredCount);
        switch (ty->tag()) {
            TU_ARM(*ty, Tuple, te) {
                const TypeRepr* r = TargetGetTypeRepr(sp, resolve, ty);
                if (!r) {
                    return false;
                }

                for (size_t i = 0; i < r->fields.size(); i++) {
                    const auto& f = r->fields[i];
                    auto size = getSizeOrZero(sp, resolve, f.ty);
                    DEBUG(i << ": " << f.offset << " + " << size);
                    if (f.offset >= maxOffset) {
                        continue;
                    } else if (f.offset + size > minOffset) {
                        if (getVariantNichePath(sp, resolve, f.ty, (f.offset < minOffset ? minOffset - f.offset : 0), maxOffset - f.offset, requiredCount, outPath, nicheStart)) {
                            outPath.subFields.push_back(i);
                            return true;
                        }
                    }
                }
            }
            TU_ARM(*ty, Array, te) {
                if (te.size.is_Known() && te.size.as_Known() > 0 && getVariantNichePath(sp, resolve, te.inner, minOffset, maxOffset, requiredCount, outPath, nicheStart)) {
                    outPath.subFields.push_back(TypeRepr::FieldPath::ARRAY_ELEMENT);
                    return true;
                }
            }
            TU_ARM(*ty, Path, te) {
                if (te.binding.is_Struct()) {
                    const auto* str = te.binding.as_Struct();
                    const TypeRepr* r = TargetGetTypeRepr(sp, resolve, ty);
                    if (!r) {
                        return false;
                    }
                    if (str->structMarkings.isNoNiche) {
                        return false;
                    }

                    if (minOffset == 0 && requiredCount == 1 && str->structMarkings.isNonzero) {
                        assert(r->fields.size() >= 1);
                        assert(r->fields[0].offset == 0);
                        auto size = getSizeOrZero(sp, resolve, r->fields[0].ty);
                        if ((r->fields[0].ty->is_Pointer() || r->fields[0].ty->is_Borrow()) && size > TargetGetPointerBits() / 8) {
                            // A wide pointer's null niche is in the data word,
                            // not in the data+metadata pair as a whole.
                            size = TargetGetPointerBits() / 8;
                        }
                        if (size <= maxOffset) {
                            outPath.subFields.push_back(0);
                            outPath.size = size;
                            nicheStart = 0;
                            return true;
                        }
                    }

                    if (minOffset == 0 && str->structMarkings.boundedMax) {
                        assert(r->fields.size() >= 1);
                        assert(r->fields[0].offset == 0);
                        auto size = getSizeOrZero(sp, resolve, r->fields[0].ty);
                        if (size <= maxOffset && size <= sizeof(size_t)) {
                            const size_t scalarMax = size == sizeof(size_t) ? SIZE_MAX : (size_t(1) << (size * 8)) - 1;
                            const auto boundedMax = str->structMarkings.boundedMaxValue.truncateU64();
                            if (boundedMax < scalarMax && requiredCount <= scalarMax - boundedMax) {
                                outPath.subFields.push_back(0);
                                outPath.size = size;
                                nicheStart = boundedMax + 1;
                                return true;
                            }
                        }
                    }

                    for (size_t i = 0; i < r->fields.size(); i++) {
                        const auto& f = r->fields[i];
                        auto size = getSizeOrZero(sp, resolve, f.ty);
                        DEBUG(i << ": " << f.offset << " + " << size);
                        if (f.offset >= maxOffset) {
                            continue;
                        } else if (f.offset + size > minOffset) {
                            if (getVariantNichePath(sp, resolve, f.ty, (f.offset < minOffset ? minOffset - f.offset : 0), maxOffset - f.offset, requiredCount, outPath, nicheStart)) {
                                outPath.subFields.push_back(i);
                                return true;
                            }
                        }
                    }
                } else if (te.binding.is_Enum()) {
                    const TypeRepr* r = TargetGetTypeRepr(sp, resolve, ty);
                    if (!r) {
                        return false;
                    }

                TU_MATCH_HDRA( (r->variants), { )
                TU_ARMA(None, ve) {
                            // If there is no discriminator, recurse into the only field
                            if (r->fields.empty()) {
                                return false;
                            } else {
                                if (getVariantNichePath(sp, resolve, r->fields[0].ty, minOffset, maxOffset, requiredCount, outPath, nicheStart)) {
                                    outPath.subFields.push_back(0);
                                    return true;
                                }
                                return false;
                            }
                        }
                        TU_ARMA(Linear, ve) {
                            if (ve.usesNiche()) {
                                // The inner enum made its niche values valid,
                                // but the scalar carrying the tag can still
                                // have another invalid range.  Search the
                                // populated variant again while reserving the
                                // values consumed by this enum.  For example,
                                // Option<Scalar<1..=100>> consumes zero and
                                // Option<Option<Scalar<1..=100>>> uses 101.
                                const auto& field = r->fields.at(ve.field.index);
                                const size_t fieldSize = getSizeOrZero(sp, resolve, field.ty);
                                if (field.offset < maxOffset && field.offset + fieldSize > minOffset) {
                                    const size_t occupiedCount = ve.nicheVariantCount();
                                    if (requiredCount <= SIZE_MAX - occupiedCount) {
                                        TypeRepr::FieldPath candidate;
                                        size_t candidateStart = 0;
                                        if (getVariantNichePath(
                                                sp,
                                                resolve,
                                                field.ty,
                                                field.offset < minOffset ? minOffset - field.offset : 0,
                                                maxOffset - field.offset,
                                                requiredCount + occupiedCount,
                                                candidate,
                                                candidateStart)) {
                                            auto candidateSubFields = candidate.subFields;
                                            ::std::reverse(candidateSubFields.begin(), candidateSubFields.end());
                                            const bool sameScalar = candidate.size == ve.field.size && candidateSubFields == ve.field.subFields;
                                            if (sameScalar) {
                                                const size_t candidateEnd = candidateStart + requiredCount + occupiedCount - 1;
                                                const size_t occupiedStart = ve.offset;
                                                const size_t occupiedEnd = occupiedStart + occupiedCount - 1;
                                                if (!(candidateEnd < occupiedStart || occupiedEnd < candidateStart)) {
                                                    const size_t beforeCount = occupiedStart > candidateStart ? occupiedStart - candidateStart : 0;
                                                    const size_t afterStart = occupiedEnd == SIZE_MAX ? SIZE_MAX : ::std::max(candidateStart, occupiedEnd + 1);
                                                    const size_t afterCount = occupiedEnd == SIZE_MAX || candidateEnd < afterStart ? 0 : candidateEnd - afterStart + 1;
                                                    if (requiredCount <= beforeCount) {
                                                        // The requested values fit before the
                                                        // range used by the inner enum.
                                                    } else if (requiredCount <= afterCount) {
                                                        candidateStart = afterStart;
                                                    } else {
                                                        return false;
                                                    }
                                                }
                                            }
                                            candidate.subFields.push_back(ve.field.index);
                                            outPath = ::std::move(candidate);
                                            nicheStart = candidateStart;
                                            return true;
                                        }
                                    }
                                }
                                return false;
                            }
                            // Check that the offset of this tag field is >= min_offset
                            auto ofs = getOffset(sp, resolve, r, ve.field);
                            DEBUG("Linear - Tag offset: " << ofs);
                            if (minOffset <= ofs && ofs + ve.field.size <= maxOffset && ve.field.size <= sizeof(size_t)) {
                                const size_t scalarMax = ve.field.size == sizeof(size_t) ? SIZE_MAX : (size_t(1) << (ve.field.size * 8)) - 1;
                                const size_t validEnd = ve.offset + ve.numVariants - 1;
                                if (validEnd >= scalarMax || requiredCount > scalarMax - validEnd) {
                                    return false;
                                }
                                outPath.size = ve.field.size;
                                outPath.subFields.clear();
                                outPath.subFields.insert(outPath.subFields.begin(), ve.field.subFields.rbegin(), ve.field.subFields.rend());
                                outPath.subFields.push_back(ve.field.index);
                                nicheStart = validEnd + 1;
                                return true;
                            }
                        }
                        TU_ARMA(Values, ve) {
                            auto ofs = getOffset(sp, resolve, r, ve.field);
                            DEBUG("Values - Tag offset: " << ofs);
                            if (minOffset <= ofs && ofs + ve.field.size <= maxOffset && ve.field.size <= sizeof(size_t) && !ve.values.empty()) {
                                const size_t scalarMax = ve.field.size == sizeof(size_t) ? SIZE_MAX : (size_t(1) << (ve.field.size * 8)) - 1;
                                std::vector<size_t> values;
                                values.reserve(ve.values.size());
                                for (const auto& value : ve.values) {
                                    values.push_back(value.truncateU64() & scalarMax);
                                }
                                std::sort(values.begin(), values.end());
                                values.erase(std::unique(values.begin(), values.end()), values.end());

                                size_t bestStart = 0;
                                size_t bestCount = values.front();
                                for (size_t i = 1; i < values.size(); i++) {
                                    const size_t count = values[i] - values[i - 1] - 1;
                                    if (count > bestCount) {
                                        bestStart = values[i - 1] + 1;
                                        bestCount = count;
                                    }
                                }
                                const size_t trailingCount = scalarMax - values.back();
                                if (trailingCount > bestCount) {
                                    bestStart = values.back() + 1;
                                    bestCount = trailingCount;
                                }
                                if (requiredCount <= bestCount) {
                                    outPath.size = ve.field.size;
                                    outPath.subFields.clear();
                                    outPath.subFields.insert(outPath.subFields.begin(), ve.field.subFields.rbegin(), ve.field.subFields.rend());
                                    outPath.subFields.push_back(ve.field.index);
                                    nicheStart = bestStart;
                                    return true;
                                }
                            }
                            return false;
                        }
                        TU_ARMA(NonZero, _ve) {
                            DEBUG("Non-zero enum, can't niche");
                            return false;
                        }
                }
                }
            }
            break;
            TU_ARM(*ty, Primitive, te) {
                switch (te) {
                    case HIRCoreType::Char:
                        // Only valid if the min offset is zero
                        if (minOffset == 0 && maxOffset >= 4 && requiredCount <= UINT32_MAX - 0x10FFFF) {
                            outPath.size = 4;
                            nicheStart = 0x10FFFF + 1;
                            return true;
                        }
                        break;
                    case HIRCoreType::Bool:
                        if (minOffset == 0 && maxOffset >= 1 && requiredCount <= UINT8_MAX - 1) {
                            outPath.size = 1;
                            nicheStart = 2;
                            return true;
                        }
                        break;
                    default:
                        break;
                }
            }
            TU_ARM(*ty, Pattern, te) {
                size_t scalarSize;
                ::std::vector<::std::pair<size_t, size_t>> ranges;
                if (minOffset != 0 || !getPatternValidRanges(te, scalarSize, ranges) || scalarSize > maxOffset) {
                    return false;
                }
                const size_t scalarMax = scalarSize == sizeof(size_t) ? SIZE_MAX : (size_t(1) << (scalarSize * 8)) - 1;
                size_t bestStart = 0;
                size_t bestCount = ranges.front().first;
                for (size_t i = 1; i < ranges.size(); i++) {
                    const size_t count = ranges[i].first - ranges[i - 1].second - 1;
                    if (count > bestCount) {
                        bestStart = ranges[i - 1].second + 1;
                        bestCount = count;
                    }
                }
                const size_t trailingCount = scalarMax - ranges.back().second;
                if (trailingCount > bestCount) {
                    bestStart = ranges.back().second + 1;
                    bestCount = trailingCount;
                }
                if (requiredCount <= bestCount) {
                    outPath.size = scalarSize;
                    outPath.subFields.clear();
                    nicheStart = bestStart;
                    return true;
                }
                return false;
            }
            TU_ARM(*ty, Borrow, te) {
                (void)te;
                if (minOffset == 0 && maxOffset >= TargetGetPointerBits() / 8 && requiredCount == 1) {
                    outPath.size = TargetGetPointerBits() / 8;
                    nicheStart = 0;
                    return true;
                }
            }
            TU_ARM(*ty, Function, te) {
                (void)te;
                if (minOffset == 0 && maxOffset >= TargetGetPointerBits() / 8 && requiredCount == 1) {
                    outPath.size = TargetGetPointerBits() / 8;
                    nicheStart = 0;
                    return true;
                }
            }
            default:
                break;
        }
        return false;
    }

    ::std::unique_ptr<TypeRepr> makeTypeReprEnum(const Span& sp, const StaticTraitResolve& resolve, const HIRTypeData* ty) {
        TRACE_FUNCTION_F(ty);
        const auto& te = ty->as_Path();
        const auto& enm = *te.binding.as_Enum();

        auto monomorphCb = MonomorphStatePtr(resolve.hirCrate().types, ty, &te.path.mData.as_Generic().mParams, nullptr);
        auto monomorph = [&](const auto& tpl) {
            return resolve.monomorphExpand(sp, tpl, monomorphCb);
        };

        if (!enm.discriminantsEvaluated) {
            ConvertHIRConstantEvaluateEnum(resolve.board(), resolve.hirCrate(), te.path.mData.as_Generic().mPath, enm);
            assert(enm.discriminantsEvaluated);
        }

        TypeRepr rv;
        switch (enm.mData.tag()) {
            case HIREnum::Class::TAGDEAD:
                throw "";
                TU_ARM(enm.mData, Data, e) {
                    // repr(C) enums - they have different rules
                    // - A data enum with `repr(C)` puts the tag before the data
                    if (enm.isCRepr) {
                        size_t maxSize = 0;
                        size_t maxAlign = 0;
                        for (const auto& var : e) {
                            auto t = monomorph(var.type);
                            size_t size, align;
                            if (!TargetGetSizeAndAlignOf(sp, resolve, t, size, align)) {
                                DEBUG("Generic type in enum - " << t);
                                return nullptr;
                            }
                            if (size == SIZE_MAX) {
                                BUG(sp, "Unsized type in enum - " << t);
                            }
                            maxSize = ::std::max(maxSize, size);
                            maxAlign = ::std::max(maxAlign, align);
                            rv.fields.push_back(TypeRepr::Field{0, mv$(t)});

                            ASSERT_BUG(sp, !var.discriminantExpr, "TODO: Handle explicit discriminants with repr(C) data");
                        }
                        DEBUG("max_size = " << maxSize << ", max_align = " << maxAlign);

                        auto tagTy = enm.tagRepr == HIREnum::Repr::Auto ? HIRCoreType::U32 : enm.getReprType(enm.tagRepr);
                        rv.fields.push_back(TypeRepr::Field{0, resolve.hirCrate().types.primitive(tagTy)});
                        size_t tagSize, tagAlign;
                        TargetGetSizeAndAlignOf(sp, resolve, rv.fields.back().ty, tagSize, tagAlign);
                        size_t dataOfs = tagSize;

                        while (dataOfs % maxAlign != 0) {
                            dataOfs++;
                        }

                        for (size_t i = 0; i < e.size(); i++) {
                            rv.fields[i].offset = dataOfs;
                        }
                        rv.size = dataOfs + maxSize;
                        rv.align = std::max(tagAlign, maxAlign);
                        while (rv.size % rv.align != 0) {
                            rv.size++;
                        }
                        rv.variants = TypeRepr::VariantMode::make_Linear({{e.size(), tagSize, {}}, 0, e.size()});
                    } else if (enm.tagRepr == HIREnum::Repr::Auto && e.size() <= 1) {
                        // If there are not multiple variants, then only include the one body
                        if (e.size() == 1) {
                            auto t = monomorph(e[0].type);
                            const auto* innerRepr = TargetGetTypeRepr(sp, resolve, t);
                            if (!innerRepr) {
                                DEBUG("Generic type in enum - " << t);
                                return nullptr;
                            }
                            rv.fields.push_back(TypeRepr::Field{0, mv$(t)});
                            rv.size = innerRepr->size;
                            rv.align = innerRepr->align;
                        } else {
                            rv.size = 0;
                            rv.align = 0;
                        }
                        // Just leave it as None
                    } else {
                        TRACE_FUNCTION_F("repr(Rust)");

                        // repr(Rust) - allows interesting optimisations
                        struct Variant {
                            HIRTypeRef type;
                            ::std::vector<Ent> ents;
                            unsigned forcedAlignment;
                        };

                        /// Is there an explicitly specified discriminant value provided?
                        bool hasExplcitValue = false;
                        std::vector<Variant> variants;
                        variants.reserve(e.size());
                        for (const auto& var : e) {
                            if (var.discriminantExpr) {
                                hasExplcitValue = true;
                            }

                            auto variantType = monomorph(var.type);
                            auto forcedAlignment = variantType->is_Path() && variantType->as_Path().binding.is_Struct() ? variantType->as_Path().binding.as_Struct()->forcedAlignment : 0;
                            variants.push_back({mv$(variantType), {}, forcedAlignment});
                            TRACE_FUNCTION_F("Variant #" << (&var - e.data()));
                            if (var.type == resolve.hirCrate().types.unit()) {
                                continue;
                            }
                            if (!structEnumerateFields(sp, resolve, variants.back().type, variants.back().ents)) {
                                DEBUG("Generic type in enum - " << variants.back().type);
                                return nullptr;
                            }
                            DEBUG(variants.back().type << ": " << variants.back().ents);
                        }

                        if (enm.tagRepr == HIREnum::Repr::Auto) {
                            ASSERT_BUG(sp, !hasExplcitValue, "Explicit tag without a repr");
                            // Non-zero optimisation
                            if (rv.variants.is_None() && variants.size() == 2) {
                                // If only one variant has a size of 0, then look for a nonzero in the variant list
                                size_t sizes[2] = {0, 0};
                                for (size_t i = 0; i < 2; i++) {
                                    for (const auto& ent : variants[i].ents) {
                                        sizes[i] += ent.size;
                                    }
                                }
                                DEBUG("sizes = {" << sizes[0] << "," << sizes[1] << "}");
                                auto minSize = ::std::min(sizes[0], sizes[1]);
                                auto maxSize = ::std::max(sizes[0], sizes[1]);
                                // If one is zero and one is non-zero
                                if (minSize == 0 && maxSize > 0) {
                                    // Check for a non-zero path in any of those
                                    unsigned nzVar = (sizes[0] == 0 ? 1 : 0);
                                    DEBUG("Variant #" << nzVar << " is populated, checking for NonZero");
                                    for (size_t i = 0; i < variants[nzVar].ents.size(); i++) {
                                        TypeRepr::FieldPath nzPath;
                                        if (getNonzeroPath(sp, resolve, variants[nzVar].ents[i].ty, nzPath)) {
                                            nzPath.subFields.push_back(i);
                                            nzPath.index = nzVar;
                                            ::std::reverse(nzPath.subFields.begin(), nzPath.subFields.end());
                                            DEBUG("nz_path = " << nzPath.subFields);

                                            size_t size0, size1;
                                            size_t align0, align1;
                                            TargetGetSizeAndAlignOf(sp, resolve, variants[0].type, size0, align0);
                                            TargetGetSizeAndAlignOf(sp, resolve, variants[1].type, size1, align1);
                                            rv.size = std::max(size0, size1);
                                            rv.align = std::max(align0, align1);
                                            rv.fields.push_back({0, std::move(variants[0].type)});
                                            rv.fields.push_back({0, std::move(variants[1].type)});
                                            rv.variants = TypeRepr::VariantMode::make_NonZero({nzPath, 1 - nzVar});
                                            break;
                                        }
                                    }
                                }
                                // DISABLED: This doesn't work properly
                                // - Downstream assumes `NonZero` means that one element is zero-sized
                                // - Calling `Target_GetTypeRepr` generates the variant early - too lazy to reimplement logic
                            } // non-zero

                            // Niche optimisation
                            // - Find an inner enum or char, and use high values for the variant
                            if (rv.variants.is_None()) {
                                bool nicheBeforeData = false;
                                size_t nicheOffset = 0;
                                size_t nonNicheOffset = 0;
                                // Find the largest variant
                                // - Also get the next-largest size to use as the minimum tag offset
                                unsigned nMatch = 0;
                                size_t biggestVar = variants.size();
                                size_t maxVarSize = 0;
                                size_t minOffset = 0;
                                size_t maxAlign = 1;
                                std::vector<std::unique_ptr<TypeRepr>> reprs;
                                for (size_t i = 0; i < variants.size(); i++) {
                                    reprs.push_back(makeTypeReprStructInner(sp, e[i].type, variants[i].ents, StructSorting::All, variants[i].forcedAlignment, 0));
                                    maxAlign = std::max(maxAlign, reprs.back()->align);
                                    size_t varSize = reprs.back()->size;
                                    // If larger than current max, update current max and reset
                                    if (varSize > maxVarSize) {
                                        minOffset = maxVarSize; // Downgrade the previous to min_offset
                                        maxVarSize = varSize;
                                        biggestVar = i;
                                        nMatch = 1;
                                    }
                                    // If equal to current max, increment count
                                    else if (varSize == maxVarSize) {
                                        nMatch += 1;
                                    }
                                    // Otherwise (smaller) update the min offset
                                    else {
                                        minOffset = std::max(minOffset, varSize);
                                    }
                                }
                                DEBUG("Niche optimisation: max_var_size=" << maxVarSize << " n_match=" << nMatch << " biggest_var=" << biggestVar << " min_offset=" << minOffset);

                                if (nMatch == 1) {
                                    const size_t nicheVariantStart = biggestVar == 0 ? 1 : 0;
                                    const size_t nicheVariantEnd = biggestVar + 1 == variants.size() ? biggestVar - 1 : variants.size() - 1;
                                    const size_t requiredNicheCount = nicheVariantEnd - nicheVariantStart + 1;
                                    for (size_t i = 0; i < reprs[biggestVar]->fields.size(); i++) {
                                        const auto& fld = reprs[biggestVar]->fields[i];

                                        // 1. Look for a tag at the end
                                        // - Prefer the end-of-struct version, as it avoids adding fields to the other variants
                                        TypeRepr::FieldPath nzPath;
                                        size_t nicheStart = 0;
                                        if (getVariantNichePath(sp, resolve, fld.ty, (minOffset > fld.offset ? minOffset - fld.offset : 0), maxVarSize - fld.offset, requiredNicheCount, nzPath, nicheStart)) {
                                            nzPath.index = i;
                                            ::std::reverse(nzPath.subFields.begin(), nzPath.subFields.end());
                                            nicheOffset = getOffset(sp, resolve, &*reprs[biggestVar], nzPath);
                                            ::std::reverse(nzPath.subFields.begin(), nzPath.subFields.end());

                                            nzPath.subFields.push_back(i);
                                            nzPath.index = biggestVar;
                                            ::std::reverse(nzPath.subFields.begin(), nzPath.subFields.end());
                                            DEBUG("Niche optimisation (trailing): value offset=" << nicheStart << " path=" << nzPath << " (@" << nicheOffset << ")");

                                            assert(rv.variants.is_None());
                                            rv.variants = TypeRepr::VariantMode::make_Linear({std::move(nzPath), nicheStart, e.size()});
                                            break;
                                        }

                                        // Note: rustc doesn't do this.
                                        // 2. Look for a possible tag at the start?
                                        // - Prepending the tag might change the next-largest variant too much?
                                        if (fld.offset == 0) {
                                            TypeRepr::FieldPath nzPath;
                                            size_t nicheStart = 0;
                                            if (getVariantNichePath(sp, resolve, fld.ty, 0, maxVarSize - minOffset, requiredNicheCount, nzPath, nicheStart)) {
                                                nzPath.index = i;
                                                ::std::reverse(nzPath.subFields.begin(), nzPath.subFields.end());
                                                nicheOffset = getOffset(sp, resolve, &*reprs[biggestVar], nzPath);
                                                if (nicheOffset != 0) {
                                                    // - For now, only accept zero offsets
                                                    DEBUG("Ignore niche not at the start of the struture");
                                                    continue;
                                                }
                                                ::std::reverse(nzPath.subFields.begin(), nzPath.subFields.end());

                                                nzPath.subFields.push_back(i);
                                                nzPath.index = biggestVar;
                                                ::std::reverse(nzPath.subFields.begin(), nzPath.subFields.end());
                                                DEBUG("Niche optimisation (leading): linear offset=" << nicheStart << " path=" << nzPath << " @byte " << nicheOffset);

                                                nicheBeforeData = true;
                                                nonNicheOffset = nzPath.size;
                                                assert(rv.variants.is_None());
                                                rv.variants = TypeRepr::VariantMode::make_Linear({std::move(nzPath), nicheStart, e.size()});
                                                break;
                                            }
                                        }
                                    }
                                }

                                // Fix overall size
                                size_t maxSize = maxVarSize;
                                while (maxSize % maxAlign != 0) {
                                    maxSize++;
                                }

                                if (!rv.variants.is_None()) {
                                    const auto& nichePath = rv.variants.as_Linear().field;

                                    HIRTypeRef nicheTy;
                                    switch (nichePath.size) {
                                        case 1:
                                            nicheTy = resolve.hirCrate().types.primitive(HIRCoreType::U8);
                                            break;
                                        case 2:
                                            nicheTy = resolve.hirCrate().types.primitive(HIRCoreType::U16);
                                            break;
                                        case 4:
                                            nicheTy = resolve.hirCrate().types.primitive(HIRCoreType::U32);
                                            break;
                                        case 8:
                                            nicheTy = resolve.hirCrate().types.primitive(HIRCoreType::U64);
                                            break;
                                        case 16:
                                            nicheTy = resolve.hirCrate().types.primitive(HIRCoreType::U128);
                                            break;
                                        default:
                                            BUG(sp, "Unknown niche size: " << nichePath);
                                    }
                                    // Generate raw struct reprs for all variants
                                    // - Add `non_niche_offset` to all variants
                                    assert(reprs.size() == variants.size());
                                    // Size/alignment of the union of the *final* variant layouts, which is what codegen emits.
                                    size_t finalSize = 0;
                                    size_t finalAlign = 1;
                                    for (size_t i = 0; i < reprs.size(); i++) {
                                        if (e[i].type != resolve.hirCrate().types.unit()) {
                                            // If the tag is leading, then add to all other variants and update reprs
                                            if (i == biggestVar) {
                                            } else if (nicheBeforeData) {
                                                // Add padding (if needed)
                                                if (nicheOffset > 0) {
                                                    variants[i].ents.insert(variants[i].ents.begin(), Ent());
                                                    variants[i].ents[0].align = 1;
                                                    variants[i].ents[0].size = nicheOffset;
                                                    variants[i].ents[0].field = ~0u;
                                                    // Leave no type
                                                    TODO(sp, "Handle adding padding");
                                                }
                                                // Add the tag
                                                variants[i].ents.insert(variants[i].ents.begin(), Ent());
                                                variants[i].ents[0].align = nichePath.size;
                                                variants[i].ents[0].size = nichePath.size;
                                                variants[i].ents[0].field = variants[i].ents.size() - 1;
                                                variants[i].ents[0].ty = nicheTy;
                                                // Create the new repr
                                                reprs[i] = makeTypeReprStructInner(sp, variants[i].type, variants[i].ents, StructSorting::None, variants[i].forcedAlignment, 0);
                                                // Make sure that the newly calculated repr doesn't change the size/alignment
                                                assert(reprs[i]->size <= maxSize);
                                                assert(reprs[i]->align <= maxAlign);
                                            } else {
                                                auto tagFldIdx = variants[i].ents.size();
                                                size_t maxOfs = 0;
                                                for (const auto& f : reprs[i]->fields) {
                                                    maxOfs = std::max(maxOfs, f.offset + getSizeOrZero(sp, resolve, f.ty));
                                                }
                                                // - Increase alignment to the niche size
                                                if (maxOfs % nichePath.size != 0) {
                                                    maxOfs += nichePath.size - (maxOfs % nichePath.size);
                                                }
                                                assert(nicheOffset % nichePath.size == 0);
                                                assert(maxOfs % nichePath.size == 0);
                                                ASSERT_BUG(sp, nicheOffset >= maxOfs, "Niche offset (" << nicheOffset << ") overlaps with variant data (" << maxOfs << ")");
                                                auto reqPadding = nicheOffset - maxOfs;
                                                if (reqPadding > 0) {
                                                    variants[i].ents.push_back(Ent());
                                                    variants[i].ents.back().align = 1;
                                                    variants[i].ents.back().size = reqPadding;
                                                    variants[i].ents.back().field = ~0u;
                                                }
                                                variants[i].ents.push_back(Ent());
                                                variants[i].ents.back().align = nichePath.size;
                                                variants[i].ents.back().size = nichePath.size;
                                                variants[i].ents.back().field = tagFldIdx;
                                                variants[i].ents.back().ty = nicheTy;
                                                // Create the new repr
                                                reprs[i] = makeTypeReprStructInner(sp, variants[i].type, variants[i].ents, StructSorting::None, variants[i].forcedAlignment, 0);
                                                // Make sure that the newly calculated repr doesn't change the size/alignment
                                                assert(reprs[i]->size <= maxSize);
                                                assert(reprs[i]->align <= maxAlign);
                                            }
                                            finalSize = std::max(finalSize, reprs[i]->size);
                                            finalAlign = std::max(finalAlign, reprs[i]->align);
                                            setTypeRepr(sp, variants[i].type, std::move(reprs[i]));
                                        } else {
                                            // Note: unit type (any empty type) doesn't need the tag added
                                            // NOTE: Unit type should already have a repr, but make sure
                                            if (const auto* r = TargetGetTypeRepr(sp, resolve, variants[i].type)) {
                                                finalSize = std::max(finalSize, r->size);
                                                finalAlign = std::max(finalAlign, r->align);
                                            }
                                        }
                                        rv.fields.push_back(TypeRepr::Field{0, mv$(variants[i].type)});
                                    }

                                    rv.size = maxSize;
                                    rv.align = maxAlign;

                                    // Under a capping ABI take size/align from the final variant layouts - `max_align` predates the tag field, so it over-states them
                                    if (TargetCapsMemberAlignment() && finalSize > 0) {
                                        size_t sz = finalSize;
                                        while (sz % finalAlign != 0) {
                                            sz++;
                                        }
                                        if (sz != rv.size || finalAlign != rv.align) {
                                            DEBUG("Capping ABI: " << ty << " " << rv.size << "/" << rv.align << " -> " << sz << "/" << finalAlign << " (union of the final variants)");
                                            rv.size = sz;
                                            rv.align = finalAlign;
                                        }
                                    }

                                    // Ensure that the tag offset is still valid
                                    auto tagOffset = getOffset(sp, resolve, &rv, nichePath);
                                    if (nonNicheOffset != 0) {
                                        ASSERT_BUG(sp, tagOffset < nonNicheOffset, "Niche offset invalid: " << tagOffset << " >= " << nonNicheOffset);
                                    } else {
                                        ASSERT_BUG(sp, tagOffset >= minOffset, "Niche offset invalid: " << tagOffset << " < " << minOffset);
                                    }
                                }
                            } // Niche optimisation
                        } // All optimisations

                        // rustc-compatible enum repr
                        // ```
                        // union {
                        //   struct {
                        //      TagType tag;
                        //      ...data
                        // }
                        // ```
                        if (rv.variants.is_None()) {
                            HIRTypeRef tagTy;
                            // If the tag size is specified, then force that
                            if (enm.tagRepr != HIREnum::Repr::Auto) {
                                tagTy = resolve.hirCrate().types.primitive(enm.getReprType(enm.tagRepr));
                            } else {
                                ASSERT_BUG(sp, !hasExplcitValue, "Explicit tag without a repr");
                                if (e.size() <= 1) {
                                    // Unreachable
                                    BUG(sp, "Reached auto tag type logic with zero/one-sized enum");
                                } else if (e.size() <= 255) {
                                    tagTy = resolve.hirCrate().types.primitive(HIRCoreType::U8);
                                    DEBUG("u8 data tag");
                                } else if (e.size() <= UINT16_MAX) {
                                    tagTy = resolve.hirCrate().types.primitive(HIRCoreType::U16);
                                } else {
                                    ASSERT_BUG(sp, e.size() <= UINT32_MAX, "");
                                    tagTy = resolve.hirCrate().types.primitive(HIRCoreType::U32);
                                }
                            }

                            size_t tagSize;
                            size_t tagAlign;
                            TargetGetSizeAndAlignOf(sp, resolve, tagTy, tagSize, tagAlign);
                            size_t maxSize = tagSize;
                            size_t maxAlign = tagAlign;
                            // Sort all varaint fields (fully)
                            // Add the tag to the start of all variants
                            // Generate a struct repr (with sorting off)
                            for (size_t varI = 0; varI < variants.size(); varI++) {
                                auto& ents = variants[varI].ents;
                                auto& varTy = variants[varI].type;
                                if (e[varI].type != resolve.hirCrate().types.unit()) {
                                    if (enm.tagRepr == HIREnum::Repr::Auto) {
                                        ::std::sort(ents.begin(), ents.end(), sortfnEnumVariantFields);
                                    }
                                    // - Add tag
                                    ents.insert(ents.begin(), Ent());
                                    ents[0].align = tagAlign;
                                    ents[0].size = tagSize;
                                    ents[0].field = ents.size() - 1;
                                    ents[0].ty = tagTy;

                                    // - Create repr and assign
                                    auto repr = makeTypeReprStructInner(sp, varTy, ents, StructSorting::None, variants[varI].forcedAlignment, 0);
                                    maxSize = std::max(maxSize, repr->size);
                                    maxAlign = std::max(maxAlign, repr->align);
                                    setTypeRepr(sp, varTy, std::move(repr));
                                }

                                // - Push the field
                                rv.fields.push_back(TypeRepr::Field{0, mv$(varTy)});
                            }
                            rv.fields.push_back(TypeRepr::Field{0, mv$(tagTy)});

                            // Size must be a multiple of alignment
                            rv.size = maxSize;
                            while (rv.size % maxAlign != 0) {
                                rv.size++;
                            }
                            rv.align = maxAlign;

                            if (hasExplcitValue) {
                                ::std::vector<U128> vals;
                                for (const auto& v : e) {
                                    vals.push_back(v.discriminantValue);
                                }
                                DEBUG("vals = " << vals);
                                rv.variants = TypeRepr::VariantMode::make_Values({{e.size(), tagSize, {}}, ::std::move(vals)});
                            } else {
                                rv.variants = TypeRepr::VariantMode::make_Linear({{e.size(), tagSize, {}}, 0, e.size()});
                            }
                        }
                    }
                }
                break;
                TU_ARM(enm.mData, Value, e) {
                    // TODO: If the values aren't yet populated, force const evaluation
                    switch (enm.tagRepr) {
                        case HIREnum::Repr::Auto:
                            if (enm.isCRepr) {
                                // No auto-sizing, just i32?
                                rv.fields.push_back(TypeRepr::Field{0, resolve.hirCrate().types.primitive(HIRCoreType::U32)});
                            } else if (!e.variants.empty()) {
                                int64_t minValue = INT64_MAX;
                                int64_t maxValue = INT64_MIN;
                                for (const auto& variant : e.variants) {
                                    const auto value = S128(variant.val).truncateI64();
                                    minValue = std::min(minValue, value);
                                    maxValue = std::max(maxValue, value);
                                }

                                HIRCoreType tagType;
                                if (minValue >= 0) {
                                    const auto maxUnsigned = static_cast<uint64_t>(maxValue);
                                    if (maxUnsigned <= UINT8_MAX) {
                                        tagType = HIRCoreType::U8;
                                    } else if (maxUnsigned <= UINT16_MAX) {
                                        tagType = HIRCoreType::U16;
                                    } else if (maxUnsigned <= UINT32_MAX) {
                                        tagType = HIRCoreType::U32;
                                    } else {
                                        tagType = HIRCoreType::U64;
                                    }
                                } else if (minValue >= INT8_MIN && maxValue <= INT8_MAX) {
                                    tagType = HIRCoreType::I8;
                                } else if (minValue >= INT16_MIN && maxValue <= INT16_MAX) {
                                    tagType = HIRCoreType::I16;
                                } else if (minValue >= INT32_MIN && maxValue <= INT32_MAX) {
                                    tagType = HIRCoreType::I32;
                                } else {
                                    tagType = HIRCoreType::I64;
                                }
                                rv.fields.push_back(TypeRepr::Field{0, resolve.hirCrate().types.primitive(tagType)});
                            }
                            break;
                        default:
                            rv.fields.push_back(TypeRepr::Field{0, resolve.hirCrate().types.primitive(enm.getReprType(enm.tagRepr))});
                            break;
                    }
                    if (rv.fields.size() > 0) {
                        // Can't return false or unsized
                        TargetGetSizeAndAlignOf(sp, resolve, rv.fields.back().ty, rv.size, rv.align);

                        ::std::vector<U128> vals;
                        for (const auto& v : e.variants) {
                            vals.push_back(v.val);
                        }
                        DEBUG("vals = " << vals);
                        rv.variants = TypeRepr::VariantMode::make_Values({{0, static_cast<uint8_t>(rv.size), {}}, ::std::move(vals)});
                    }
                }
                break;
        }

        TU_MATCH_HDRA( (rv.variants), { )
        TU_ARMA(None, e) {
                DEBUG("rv.variants = None");
            }
            TU_ARMA(Linear, e) {
                DEBUG("rv.variants = Linear {" << " field=" << e.field << " value " << e.offset << "+" << e.numVariants << " }");
            }
            TU_ARMA(Values, e) {
                DEBUG("rv.variants = Values {" << " field=" << e.field << " values " << e.values << " }");
            }
            TU_ARMA(NonZero, e) {
                DEBUG("rv.variants = NonZero {" << " field=" << e.field << " zero_variant=" << e.zeroVariant << " }");
            }
        }

        // An enum inherits user-alignment from any variant, as in gcc; every variant repr is already cached here, so this cannot recurse.
        for(const auto& f : rv.fields) {
            if (TargetTypeHasUserAlignment(sp, resolve, f.ty)) {
                rv.userAlign = true;
                break;
            }
        }
        return box$(rv);
    }

    ::std::unique_ptr<TypeRepr> makeTypeReprUnion(const Span& sp, const StaticTraitResolve& resolve, const HIRTypeData* ty) {
        const auto& te = ty->as_Path();
        const auto& unn = *te.binding.as_Union();

        auto monomorphCb = MonomorphStatePtr(resolve.hirCrate().types, ty, &te.path.mData.as_Generic().mParams, nullptr);
        auto monomorph = [&](const auto& tpl) {
            return resolve.monomorphExpand(sp, tpl, monomorphCb);
        };

        TypeRepr rv;
        // codegen_c pins union alignment with an explicit `__attribute__((aligned))`, which gcc counts as user-alignment - so a union, and anything containing it, is exempt from the cap.
        rv.userAlign = true;
        for (const auto& var : unn.mVariants) {
            rv.fields.push_back({0, monomorph(var.ty)});
            size_t size, align;
            if (!TargetGetSizeAndAlignOf(sp, resolve, rv.fields.back().ty, size, align)) {
                // Generic? - Not good.
                DEBUG("Generic type encounterd after monomorphise in union - " << rv.fields.back().ty);
                return nullptr;
            }
            if (size == SIZE_MAX) {
                BUG(sp, "Unsized type in union");
            }
            rv.size = ::std::max(rv.size, size);
            rv.align = ::std::max(rv.align, align);
            // A union inherits user-alignment from any member, as in gcc.
            if (TargetTypeHasUserAlignment(sp, resolve, rv.fields.back().ty)) {
                rv.userAlign = true;
            }
        }
        // Round the size to be a multiple of align
        if (rv.size % rv.align != 0) {
            rv.size += rv.align - rv.size % rv.align;
        }
        return box$(rv);
    }

    ::std::unique_ptr<TypeRepr> make_type_repr_(const Span& sp, const StaticTraitResolve& resolve, const HIRTypeData* ty) {
        switch (ty->tag()) {
            case HIRTypeData::TAGDEAD:
                abort();
            case HIRTypeData::TAG_Tuple:
                return makeTypeReprStruct(sp, resolve, ty);
            case HIRTypeData::TAG_Path:
                switch (ty->as_Path().binding.tag()) {
                    case HIRTypePathBinding::TAGDEAD:
                        abort();
                    case HIRTypePathBinding::TAG_Struct:
                        return makeTypeReprStruct(sp, resolve, ty);
                    case HIRTypePathBinding::TAG_Union:
                        return makeTypeReprUnion(sp, resolve, ty);
                    case HIRTypePathBinding::TAG_Enum:
                        return makeTypeReprEnum(sp, resolve, ty);
                    case HIRTypePathBinding::TAG_ExternType:
                        // TODO: Do extern types need anything?
                        return nullptr;
                    case HIRTypePathBinding::TAG_Opaque:
                    case HIRTypePathBinding::TAG_Unbound:
                        BUG(sp, "Encountered invalid type in make_type_repr - " << ty);
                }
                throw "unreachable";
            case HIRTypeData::TAG_NodeType:
                if (const auto* closure = ty->as_NodeType().opt_Closure(); closure && closureHasNoCaptures(resolve, **closure)) {
                    auto repr = box$(TypeRepr());
                    repr->align = 1;
                    return repr;
                }
                TODO(sp, "Type repr for " << ty);
            // TODO: Why is `make_type_repr` being called on these?
            case HIRTypeData::TAG_Primitive:
            case HIRTypeData::TAG_Borrow:
            case HIRTypeData::TAG_Pointer:
            case HIRTypeData::TAG_Pattern:
                return nullptr;
            default:
                TODO(sp, "Type repr for " << ty);
        }
    }

    ::std::unique_ptr<TypeRepr> makeTypeRepr(const Span& sp, const StaticTraitResolve& resolve, const HIRTypeData* ty) {
        ::std::unique_ptr<TypeRepr> rv;
        TRACE_FUNCTION_FR(ty, ty << " " << FMT_CB(ss, if (rv) { ss << "size=" << rv->size << ", align=" << rv->align; } else { ss << "NONE"; }));
        rv = make_type_repr_(sp, resolve, ty);
        return rv;
    }

    struct CachedTypeRepr {
        HIRTypeRef canonical;
        ::std::unique_ptr<TypeRepr> repr;
    };

    // Layout is a codegen property: regions are erased before rustc asks its
    // layout engine too. Keep one representation per emitted type, with an
    // exact-pointer alias so repeated queries stay O(1).
    static ::std::unordered_map<::std::string, CachedTypeRepr> sCache;
    static ::std::unordered_map<HIRTypeRef, ::std::unique_ptr<TypeRepr>> sUnencodedCache;
    static ::std::unordered_map<HIRTypeRef, const TypeRepr*> sCacheExact;

    bool hasAbiIdentity(HIRTypeRef ty) {
        return !monomorphiseTypeNeeded(ty) && !ty->is_Infer() && !ty->is_ErasedType() && !ty->is_NodeType();
    }

    void setTypeRepr(const Span& sp, const HIRTypeData* ty, ::std::unique_ptr<TypeRepr> repr) {
        if (!hasAbiIdentity(ty)) {
            const auto* reprPtr = repr.get();
            auto ires = sUnencodedCache.emplace(ty, mv$(repr));
            ASSERT_BUG(sp, ires.second, "set_type_repr called for type that already has a repr: " << ty);
            sCacheExact.emplace(ty, reprPtr);
            DEBUG("Set temporary repr for " << ty);
            return;
        }
        auto symbol = FMT(TransMangle(ty));
        auto ires = sCache.emplace(mv$(symbol), CachedTypeRepr{ty, mv$(repr)});
        ASSERT_BUG(sp, ires.second, "set_type_repr called for type that already has a repr: " << ty);
        sCacheExact.emplace(ty, ires.first->second.repr.get());
        DEBUG("Set repr for " << ty);
    }
}

void TargetForceTypeRepr(const Span& sp, const HIRTypeData* ty, TypeRepr repr) {
    setTypeRepr(sp, ty, box$(repr));
}

bool TargetCapsMemberAlignment() {
    return false; // Darwin/PowerPC "power" alignment: powerpc is unsupported (big-endian)
}

bool TargetTypeHasUserAlignment(const Span& sp, const StaticTraitResolve& resolve, const HIRTypeData* ty) {
    // Arrays and slices inherit it from the element type, as in gcc's `layout_type`
    if (const auto* te = ty->opt_Array()) {
        return TargetTypeHasUserAlignment(sp, resolve, te->inner);
    }
    if (const auto* te = ty->opt_Slice()) {
        return TargetTypeHasUserAlignment(sp, resolve, te->inner);
    }
    if (const auto* te = ty->opt_Pattern()) {
        return TargetTypeHasUserAlignment(sp, resolve, te->inner);
    }
    // Aggregates cache it on their repr; everything else is naturally aligned by definition
    if (ty->is_Tuple() || (ty->is_Path() && (ty->as_Path().binding.is_Struct() || ty->as_Path().binding.is_Union() || ty->as_Path().binding.is_Enum()))) {
        const auto* repr = TargetGetTypeRepr(sp, resolve, ty);
        return repr && repr->userAlign;
    }
    return false;
}

const TypeRepr* TargetGetTypeRepr(const Span& sp, const StaticTraitResolve& resolve, const HIRTypeData* ty) {
    auto exact = sCacheExact.find(ty);
    if (exact != sCacheExact.end()) {
        return exact->second;
    }

    if (!hasAbiIdentity(ty)) {
        auto repr = makeTypeRepr(sp, resolve, ty);
        const auto* rv = repr.get();
        auto ires = sUnencodedCache.emplace(ty, mv$(repr));
        ASSERT_BUG(sp, ires.second, "Type representation was created recursively for " << ty);
        sCacheExact.emplace(ty, rv);
        DEBUG("Created temporary repr for " << ty);
        return rv;
    }

    auto symbol = FMT(TransMangle(ty));
    auto existing = sCache.find(symbol);
    if (existing != sCache.end()) {
        ASSERT_BUG(sp, existing->second.canonical == ty || existing->second.canonical->equalsIgnoringRegions(ty), "Distinct types have the same mangled name: " << existing->second.canonical << " and " << ty);
        const auto* repr = existing->second.repr.get();
        sCacheExact.emplace(ty, repr);
        return repr;
    }

    auto repr = makeTypeRepr(sp, resolve, ty);
    const auto* rv = repr.get();
    auto ires = sCache.emplace(mv$(symbol), CachedTypeRepr{ty, mv$(repr)});
    ASSERT_BUG(sp, ires.second, "Type representation was created recursively for " << ty);
    sCacheExact.emplace(ty, rv);
    DEBUG("Created repr for " << ty);
    return rv;
}

const HIRTypeData* TargetGetInnerType(const Span& sp, const StaticTraitResolve& resolve, const TypeRepr& repr, size_t idx, const ::std::vector<size_t>& subFields, size_t ofs) {
    const auto* ty = &repr.fields.at(idx).ty;
    while (ofs < subFields.size()) {
        const auto field = subFields[ofs++];
        if (field == TypeRepr::FieldPath::ARRAY_ELEMENT) {
            const auto* array = (*ty)->opt_Array();
            ASSERT_BUG(sp, array && array->size.is_Known() && array->size.as_Known() > 0, "Array field path on non-array " << *ty);
            ty = &array->inner;
        } else {
            const auto* innerRepr = TargetGetTypeRepr(sp, resolve, *ty);
            ASSERT_BUG(sp, innerRepr, "No inner repr for " << *ty);
            ty = &innerRepr->fields.at(field).ty;
        }
    }
    return *ty;
}

size_t TypeRepr::getOffset(const Span& sp, const StaticTraitResolve& resolve, const TypeRepr::FieldPath& path) const {
    const auto* r = this;
    assert(path.index < r->fields.size());
    size_t ofs = r->fields[path.index].offset;

    const auto* ty = &r->fields[path.index].ty;
    for (const auto& f : path.subFields) {
        if (f == TypeRepr::FieldPath::ARRAY_ELEMENT) {
            const auto* array = (*ty)->opt_Array();
            assert(array && array->size.is_Known() && array->size.as_Known() > 0);
            ty = &array->inner;
            continue;
        }
        r = TargetGetTypeRepr(sp, resolve, *ty);
        assert(r); // We have an outer repr, so inner must exist
        assert(f < r->fields.size());
        ofs += r->fields[f].offset;
        ty = &r->fields[f].ty;
    }

    return ofs;
}

size_t TypeRepr::VariantMode::Data_Linear::nicheVariantStart() const {
    assert(this->usesNiche());
    return this->field.index == 0 ? 1 : 0;
}

size_t TypeRepr::VariantMode::Data_Linear::nicheVariantCount() const {
    assert(this->usesNiche());
    const size_t start = this->nicheVariantStart();
    const size_t end = this->field.index + 1 == this->numVariants ? this->field.index - 1 : this->numVariants - 1;
    return end - start + 1;
}

size_t TypeRepr::VariantMode::Data_Linear::tagValue(unsigned varIdx) const {
    if (!this->usesNiche()) {
        return this->offset + varIdx;
    }
    assert(varIdx < this->numVariants);
    assert(varIdx != this->field.index);
    const size_t start = this->nicheVariantStart();
    return this->offset + varIdx - start;
}

unsigned TypeRepr::VariantMode::Data_Linear::decodeTag(U128 tag) const {
    if (!this->usesNiche()) {
        return (tag - U128(this->offset)).truncateU64();
    }
    const auto start = U128(this->offset);
    const auto end = start + U128(this->nicheVariantCount());
    if (start <= tag && tag < end) {
        return static_cast<unsigned>(this->nicheVariantStart() + (tag - start).truncateU64());
    }
    return this->field.index;
}

std::pair<unsigned, bool> TypeRepr::getEnumVariant(const Span& sp, const StaticTraitResolve& resolve, const EncodedLiteralSlice& lit) const {
    unsigned varIdx = 0;
    bool subHasTag = false;
    TU_MATCH_HDRA( (this->variants), {)
    TU_ARMA(None, ve) {
        }
        TU_ARMA(Linear, ve) {
            auto v = lit.slice(this->getOffset(sp, resolve, ve.field), ve.field.size).readUint(ve.field.size);
            varIdx = ve.decodeTag(v);
            if (ve.isNiche(varIdx)) {
                subHasTag = false;
                DEBUG("VariantMode::Linear - Niche #" << varIdx);
            } else {
                subHasTag = true;
                DEBUG("VariantMode::Linear - Other #" << varIdx);
            }
        }
        TU_ARMA(Values, ve) {
            auto v = lit.slice(this->getOffset(sp, resolve, ve.field), ve.field.size).readUint(ve.field.size);
            const U128 mask = ve.field.size >= 16
                ? U128::max()
                : (U128(1) << static_cast<unsigned>(ve.field.size * 8)) - U128(1);
            auto it = std::find_if(ve.values.begin(), ve.values.end(), [&](const U128& candidate) {
                return (candidate & mask) == v;
            });
            ASSERT_BUG(sp, it != ve.values.end(), "Invalid enum tag: " << v);
            varIdx = it - ve.values.begin();
            DEBUG("VariantMode::Values - #" << varIdx);
        }
        TU_ARMA(NonZero, ve) {
            size_t ofs = this->getOffset(sp, resolve, ve.field);
            bool isNonzero = false;
            for (size_t i = 0; i < ve.field.size; i++) {
                if (lit.slice(ofs + i, 1).readUint(1) != 0) {
                    isNonzero = true;
                    break;
                }
            }

            varIdx = (isNonzero ? 1 - ve.zeroVariant : ve.zeroVariant);
            DEBUG("VariantMode::NonZero - #" << varIdx);
        }
    }
    return std::make_pair(varIdx, subHasTag);
}

namespace {
    constexpr size_t TRANSMUTE_BYTE_VALUES = 257;
    constexpr size_t TRANSMUTE_UNINITIALISED = 256;

    using TransmuteByteSet = ::std::bitset<TRANSMUTE_BYTE_VALUES>;

    struct TransmuteReference {
        bool isMutable;
        const HIRTypeData* referent;
        size_t referentSize;
        size_t referentAlign;
    };

    struct TransmuteNfa {
        struct ByteEdge {
            TransmuteByteSet values;
            unsigned destination;
        };
        struct ReferenceEdge {
            TransmuteReference reference;
            unsigned destination;
        };
        struct State {
            ::std::vector<unsigned> epsilon;
            ::std::vector<ByteEdge> bytes;
            ::std::vector<ReferenceEdge> references;
        };
        struct Fragment {
            unsigned start;
            unsigned accept;
        };

        ::std::vector<State> states;

        unsigned addState() {
            states.push_back({});
            return states.size() - 1;
        }

        Fragment empty() {
            auto state = addState();
            return {state, state};
        }

        Fragment uninhabited() {
            return {addState(), addState()};
        }

        Fragment byte(const TransmuteByteSet& values) {
            auto start = addState();
            auto accept = addState();
            states[start].bytes.push_back({values, accept});
            return {start, accept};
        }

        Fragment reference(TransmuteReference reference) {
            auto start = addState();
            auto accept = addState();
            states[start].references.push_back({reference, accept});
            return {start, accept};
        }

        Fragment then(Fragment left, Fragment right) {
            states[left.accept].epsilon.push_back(right.start);
            return {left.start, right.accept};
        }

        Fragment alternative(::std::vector<Fragment> alternatives) {
            if (alternatives.empty()) {
                return uninhabited();
            }
            if (alternatives.size() == 1) {
                return alternatives.front();
            }
            auto start = addState();
            auto accept = addState();
            for (const auto& alternative : alternatives) {
                states[start].epsilon.push_back(alternative.start);
                states[alternative.accept].epsilon.push_back(accept);
            }
            return {start, accept};
        }
    };

    struct TransmuteDfa {
        using Transitions = ::std::array<int, TRANSMUTE_BYTE_VALUES>;

        ::std::vector<Transitions> transitions;
        ::std::vector<::std::vector<::std::pair<TransmuteReference, unsigned>>> references;
        ::std::vector<bool> accepting;

        bool inhabited() const {
            return ::std::find(accepting.begin(), accepting.end(), true) != accepting.end();
        }
    };

    class TransmuteLayoutBuilder {
        struct Built {
            TransmuteNfa::Fragment fragment;
            size_t size;
        };
        struct Segment {
            size_t offset;
            Built value;
        };

        const Span& sp;
        const StaticTraitResolve& resolve;
        bool destination;
        bool assumeSafety;
        bool supported = true;

        static TransmuteByteSet byteRange(unsigned first, unsigned last) {
            TransmuteByteSet rv;
            for (unsigned value = first; value <= last; value++) {
                rv.set(value);
            }
            return rv;
        }

        Built bytes(size_t count, const TransmuteByteSet& values) {
            auto rv = nfa.empty();
            for (size_t i = 0; i < count; i++) {
                rv = nfa.then(rv, nfa.byte(values));
            }
            return {rv, count};
        }

        Built padding(size_t count) {
            TransmuteByteSet values;
            values.set();
            return bytes(count, values);
        }

        Built number(size_t count) {
            return bytes(count, byteRange(0, 255));
        }

        Built exact(U128 value, size_t count) {
            if (count > 16) {
                supported = false;
                return {nfa.uninhabited(), count};
            }
            uint8_t raw[16] = {};
            value.toLeBytes(raw, count);
            auto rv = nfa.empty();
            for (size_t i = 0; i < count; i++) {
                TransmuteByteSet values;
                values.set(raw[i]);
                rv = nfa.then(rv, nfa.byte(values));
            }
            return {rv, count};
        }

        Built character() {
            const auto any = byteRange(0, 255);
            const auto zero = byteRange(0, 0);
            auto make = [&](const ::std::array<TransmuteByteSet, 4>& values) {
                auto rv = nfa.empty();
                for (const auto& value : values) {
                    rv = nfa.then(rv, nfa.byte(value));
                }
                return rv;
            };
            ::std::vector<TransmuteNfa::Fragment> alternatives;
            alternatives.push_back(make({any, byteRange(0x00, 0xD7), zero, zero}));
            alternatives.push_back(make({any, byteRange(0xE0, 0xFF), zero, zero}));
            alternatives.push_back(make({any, any, byteRange(0x01, 0x10), zero}));
            return {nfa.alternative(::std::move(alternatives)), 4};
        }

        Built combine(::std::vector<Segment> segments, size_t totalSize) {
            ::std::stable_sort(segments.begin(), segments.end(), [](const Segment& left, const Segment& right) {
                return left.offset < right.offset;
            });

            auto rv = nfa.empty();
            size_t offset = 0;
            for (const auto& segment : segments) {
                if (segment.offset < offset || segment.offset > totalSize || segment.value.size > totalSize - segment.offset) {
                    supported = false;
                    return {nfa.uninhabited(), totalSize};
                }
                rv = nfa.then(rv, padding(segment.offset - offset).fragment);
                rv = nfa.then(rv, segment.value.fragment);
                offset = segment.offset + segment.value.size;
            }
            rv = nfa.then(rv, padding(totalSize - offset).fragment);
            return {rv, totalSize};
        }

        Built aggregate(const TypeRepr& repr, int skipField = -1) {
            ::std::vector<Segment> fields;
            for (size_t i = 0; i < repr.fields.size(); i++) {
                if (static_cast<int>(i) == skipField) {
                    continue;
                }
                auto field = build(repr.fields[i].ty);
                fields.push_back({repr.fields[i].offset, field});
            }
            return combine(::std::move(fields), repr.size);
        }

        void addVariantPayload(
            ::std::vector<Segment>& segments,
            const TypeRepr& outerRepr,
            unsigned variant,
            bool skipSyntheticTag,
            size_t tagOffset,
            size_t tagSize
        ) {
            ASSERT_BUG(sp, variant < outerRepr.fields.size(), "Enum variant field is missing");
            const auto& outerField = outerRepr.fields[variant];
            const auto* payloadRepr = TargetGetTypeRepr(sp, resolve, outerField.ty);
            if (!payloadRepr) {
                supported = false;
                return;
            }

            int skipField = -1;
            if (skipSyntheticTag && !payloadRepr->fields.empty()) {
                const auto& candidate = payloadRepr->fields.back();
                size_t candidateSize = 0;
                if (!TargetGetSizeOf(sp, resolve, candidate.ty, candidateSize)) {
                    supported = false;
                    return;
                }
                if (outerField.offset + candidate.offset == tagOffset && candidateSize == tagSize) {
                    skipField = payloadRepr->fields.size() - 1;
                }
            }

            if (skipField < 0) {
                segments.push_back({outerField.offset, build(outerField.ty)});
                return;
            }

            for (size_t i = 0; i < payloadRepr->fields.size(); i++) {
                if (static_cast<int>(i) == skipField) {
                    continue;
                }
                const auto& field = payloadRepr->fields[i];
                segments.push_back({outerField.offset + field.offset, build(field.ty)});
            }
        }

        Built taggedVariant(
            const TypeRepr& repr,
            unsigned variant,
            size_t tagOffset,
            size_t tagSize,
            U128 tag,
            bool skipSyntheticTag
        ) {
            ::std::vector<Segment> segments;
            addVariantPayload(segments, repr, variant, skipSyntheticTag, tagOffset, tagSize);
            segments.push_back({tagOffset, exact(tag, tagSize)});
            return combine(::std::move(segments), repr.size);
        }

        Built enumLayout(const HIRTypeData* ty, const TypeRepr& repr, const HIREnum& enm) {
            if (enm.numVariants() == 0) {
                return {nfa.uninhabited(), repr.size};
            }

            if (repr.variants.is_None()) {
                if (repr.fields.empty()) {
                    return padding(repr.size);
                }
                return combine({Segment{repr.fields[0].offset, build(repr.fields[0].ty)}}, repr.size);
            }

            ::std::vector<TransmuteNfa::Fragment> alternatives;
            if (const auto* linear = repr.variants.opt_Linear()) {
                const auto tagOffset = repr.getOffset(sp, resolve, linear->field);
                for (unsigned variant = 0; variant < linear->numVariants; variant++) {
                    Built value;
                    if (linear->usesNiche() && variant == linear->field.index) {
                        const auto& field = repr.fields.at(variant);
                        value = combine({Segment{field.offset, build(field.ty)}}, repr.size);
                    } else {
                        value = taggedVariant(repr, variant, tagOffset, linear->field.size, U128(linear->tagValue(variant)), true);
                    }
                    alternatives.push_back(value.fragment);
                }
            } else if (const auto* values = repr.variants.opt_Values()) {
                const auto tagOffset = repr.getOffset(sp, resolve, values->field);
                for (unsigned variant = 0; variant < values->values.size(); variant++) {
                    auto value = enm.mData.is_Value()
                        ? combine({Segment{tagOffset, exact(values->values[variant], values->field.size)}}, repr.size)
                        : taggedVariant(repr, variant, tagOffset, values->field.size, values->values[variant], true);
                    alternatives.push_back(value.fragment);
                }
            } else if (const auto* nonzero = repr.variants.opt_NonZero()) {
                const auto tagOffset = repr.getOffset(sp, resolve, nonzero->field);
                const auto nonzeroVariant = 1 - nonzero->zeroVariant;
                for (unsigned variant = 0; variant < 2; variant++) {
                    Built value;
                    if (variant == nonzeroVariant) {
                        const auto& field = repr.fields.at(variant);
                        value = combine({Segment{field.offset, build(field.ty)}}, repr.size);
                    } else {
                        value = combine({Segment{tagOffset, exact(U128(0), nonzero->field.size)}}, repr.size);
                    }
                    alternatives.push_back(value.fragment);
                }
            } else {
                BUG(sp, "Unhandled enum representation for " << ty);
            }
            return {nfa.alternative(::std::move(alternatives)), repr.size};
        }

        Built build(const HIRTypeData* ty) {
            if (ty->is_Diverge()) {
                return {nfa.uninhabited(), 0};
            }
            if (const auto* primitive = ty->opt_Primitive()) {
                size_t size = 0;
                if (!TargetGetSizeOf(sp, resolve, ty, size)) {
                    supported = false;
                    return {nfa.uninhabited(), 0};
                }
                if (*primitive == HIRCoreType::Bool) {
                    return bytes(1, byteRange(0, 1));
                }
                if (*primitive == HIRCoreType::Char) {
                    return character();
                }
                if (*primitive == HIRCoreType::Str) {
                    supported = false;
                    return {nfa.uninhabited(), size};
                }
                return number(size);
            }
            if (const auto* tuple = ty->opt_Tuple()) {
                (void)tuple;
                const auto* repr = TargetGetTypeRepr(sp, resolve, ty);
                if (!repr) {
                    supported = false;
                    return {nfa.uninhabited(), 0};
                }
                return aggregate(*repr);
            }
            if (const auto* array = ty->opt_Array()) {
                if (!array->size.is_Known()) {
                    supported = false;
                    return {nfa.uninhabited(), 0};
                }
                auto rv = nfa.empty();
                size_t size = 0;
                for (uint64_t i = 0; i < array->size.as_Known(); i++) {
                    auto element = build(array->inner);
                    if (element.size > SIZE_MAX - size) {
                        supported = false;
                        return {nfa.uninhabited(), 0};
                    }
                    size += element.size;
                    rv = nfa.then(rv, element.fragment);
                }
                return {rv, size};
            }
            if (const auto* borrow = ty->opt_Borrow()) {
                if (borrow->type == HIRBorrowType::Owned) {
                    supported = false;
                    return {nfa.uninhabited(), 0};
                }

                size_t referentSize = 0;
                size_t referentAlign = 0;
                size_t referenceSize = 0;
                if (!TargetGetSizeAndAlignOf(sp, resolve, borrow->inner, referentSize, referentAlign)
                    || !TargetGetSizeOf(sp, resolve, ty, referenceSize)) {
                    supported = false;
                    return {nfa.uninhabited(), 0};
                }
                return {
                    nfa.reference({borrow->type == HIRBorrowType::Unique, borrow->inner, referentSize, referentAlign}),
                    referenceSize
                };
            }
            if (const auto* path = ty->opt_Path()) {
                if (destination && !assumeSafety) {
                    supported = false;
                    return {nfa.uninhabited(), 0};
                }
                const auto* repr = TargetGetTypeRepr(sp, resolve, ty);
                if (!repr) {
                    supported = false;
                    return {nfa.uninhabited(), 0};
                }
                if (path->binding.is_Struct()) {
                    const auto& str = *path->binding.as_Struct();
                    // rustc 1.90's transmutability layout does not yet model
                    // scalar valid ranges (including the NonZero wrappers).
                    if (str.structMarkings.isNonzero || str.structMarkings.boundedMax) {
                        supported = false;
                        return {nfa.uninhabited(), repr->size};
                    }
                    return aggregate(*repr);
                }
                if (path->binding.is_Union()) {
                    ::std::vector<TransmuteNfa::Fragment> alternatives;
                    for (const auto& field : repr->fields) {
                        alternatives.push_back(combine({Segment{0, build(field.ty)}}, repr->size).fragment);
                    }
                    return {nfa.alternative(::std::move(alternatives)), repr->size};
                }
                if (path->binding.is_Enum()) {
                    return enumLayout(ty, *repr, *path->binding.as_Enum());
                }
                supported = false;
                return {nfa.uninhabited(), repr->size};
            }
            supported = false;
            return {nfa.uninhabited(), 0};
        }

        ::std::vector<unsigned> epsilonClosure(::std::vector<unsigned> states) const {
            ::std::vector<bool> seen(nfa.states.size(), false);
            for (auto state : states) {
                seen.at(state) = true;
            }
            for (size_t i = 0; i < states.size(); i++) {
                for (auto next : nfa.states[states[i]].epsilon) {
                    if (!seen[next]) {
                        seen[next] = true;
                        states.push_back(next);
                    }
                }
            }
            ::std::sort(states.begin(), states.end());
            return states;
        }

    public:
        TransmuteNfa nfa;

        TransmuteLayoutBuilder(const Span& sp, const StaticTraitResolve& resolve, bool destination, bool assumeSafety)
            : sp(sp)
            , resolve(resolve)
            , destination(destination)
            , assumeSafety(assumeSafety)
        {
        }

        bool makeDfa(const HIRTypeData* ty, TransmuteDfa& out) {
            auto root = build(ty);
            if (!supported) {
                return false;
            }

            ::std::map<::std::vector<unsigned>, unsigned> indexes;
            ::std::vector<::std::vector<unsigned>> states;
            auto intern = [&](::std::vector<unsigned> state) {
                auto result = indexes.emplace(state, indexes.size());
                if (result.second) {
                    states.push_back(::std::move(state));
                    TransmuteDfa::Transitions transitions;
                    transitions.fill(-1);
                    out.transitions.push_back(transitions);
                    out.references.push_back({});
                    out.accepting.push_back(false);
                }
                return result.first->second;
            };

            intern(epsilonClosure({root.fragment.start}));
            for (size_t stateIndex = 0; stateIndex < states.size(); stateIndex++) {
                const auto state = states[stateIndex];
                out.accepting[stateIndex] = ::std::binary_search(state.begin(), state.end(), root.fragment.accept);

                ::std::array<::std::vector<unsigned>, TRANSMUTE_BYTE_VALUES> destinations;
                for (auto nfaState : state) {
                    for (const auto& edge : nfa.states[nfaState].bytes) {
                        for (size_t value = 0; value < TRANSMUTE_BYTE_VALUES; value++) {
                            if (edge.values[value]) {
                                destinations[value].push_back(edge.destination);
                            }
                        }
                    }
                }
                for (size_t value = 0; value < TRANSMUTE_BYTE_VALUES; value++) {
                    auto& destination = destinations[value];
                    if (destination.empty()) {
                        continue;
                    }
                    ::std::sort(destination.begin(), destination.end());
                    destination.erase(::std::unique(destination.begin(), destination.end()), destination.end());
                    out.transitions[stateIndex][value] = intern(epsilonClosure(::std::move(destination)));
                }
                for (auto nfaState : state) {
                    for (const auto& edge : nfa.states[nfaState].references) {
                        auto destination = intern(epsilonClosure({edge.destination}));
                        out.references[stateIndex].push_back({edge.reference, destination});
                    }
                }
            }
            return true;
        }
    };

    class TransmuteRelation;

    class TransmuteTypeChecker {
        const Span& sp;
        const StaticTraitResolve& resolve;
        bool assumeAlignment;
        bool assumeSafety;
        bool assumeValidity;
        ::std::map<::std::pair<const HIRTypeData*, const HIRTypeData*>, int> cache;

    public:
        TransmuteTypeChecker(const Span& sp, const StaticTraitResolve& resolve, bool assumeAlignment, bool assumeSafety, bool assumeValidity)
            : sp(sp)
            , resolve(resolve)
            , assumeAlignment(assumeAlignment)
            , assumeSafety(assumeSafety)
            , assumeValidity(assumeValidity)
        {
        }

        bool check(const HIRTypeData* sourceType, const HIRTypeData* destinationType);

        bool referencesCompatible(const TransmuteReference& source, const TransmuteReference& destination) {
            if (!source.isMutable && destination.isMutable) {
                return false;
            }
            if (!assumeAlignment && source.referentAlign < destination.referentAlign) {
                return false;
            }
            if (destination.referentSize > source.referentSize) {
                return false;
            }
            if (!check(source.referent, destination.referent)) {
                return false;
            }
            if (destination.isMutable) {
                return check(destination.referent, source.referent);
            }
            return resolve.typeIsInteriorMutable(sp, destination.referent) == HIRCompare::Unequal;
        }

        bool validityIsAssumed() const {
            return assumeValidity;
        }
    };

    class TransmuteRelation {
        const TransmuteDfa& source;
        const TransmuteDfa& destination;
        TransmuteTypeChecker& typeChecker;
        bool assumeValidity;
        ::std::map<::std::pair<unsigned, unsigned>, int> cache;

        bool check(unsigned sourceState, unsigned destinationState) {
            const auto key = ::std::make_pair(sourceState, destinationState);
            auto existing = cache.find(key);
            if (existing != cache.end()) {
                return existing->second > 0;
            }
            cache.insert({key, 0});

            bool result;
            if (destination.accepting[destinationState]) {
                result = true;
            } else if (source.accepting[sourceState]) {
                const auto next = destination.transitions[destinationState][TRANSMUTE_UNINITIALISED];
                result = next >= 0 && check(sourceState, static_cast<unsigned>(next));
            } else {
                bool bytesResult = !assumeValidity;
                for (size_t value = 0; value < TRANSMUTE_BYTE_VALUES; value++) {
                    const auto sourceNext = source.transitions[sourceState][value];
                    if (sourceNext < 0) {
                        continue;
                    }
                    const auto destinationNext = destination.transitions[destinationState][value];
                    const bool edgeResult = destinationNext >= 0
                        && check(static_cast<unsigned>(sourceNext), static_cast<unsigned>(destinationNext));
                    if (assumeValidity) {
                        bytesResult |= edgeResult;
                        if (bytesResult) {
                            break;
                        }
                    } else {
                        bytesResult &= edgeResult;
                        if (!bytesResult) {
                            break;
                        }
                    }
                }

                bool referencesResult = !assumeValidity;
                for (const auto& sourceEdge : source.references[sourceState]) {
                    bool edgeResult = false;
                    for (const auto& destinationEdge : destination.references[destinationState]) {
                        if (typeChecker.referencesCompatible(sourceEdge.first, destinationEdge.first)
                            && check(sourceEdge.second, destinationEdge.second)) {
                            edgeResult = true;
                            break;
                        }
                    }
                    if (assumeValidity) {
                        referencesResult |= edgeResult;
                        if (referencesResult) {
                            break;
                        }
                    } else {
                        referencesResult &= edgeResult;
                        if (!referencesResult) {
                            break;
                        }
                    }
                }
                result = assumeValidity ? bytesResult || referencesResult : bytesResult && referencesResult;
            }
            cache[key] = result ? 1 : -1;
            return result;
        }

    public:
        TransmuteRelation(const TransmuteDfa& source, const TransmuteDfa& destination, TransmuteTypeChecker& typeChecker)
            : source(source)
            , destination(destination)
            , typeChecker(typeChecker)
            , assumeValidity(typeChecker.validityIsAssumed())
        {
        }

        bool check() {
            if (!source.inhabited()) {
                return true;
            }
            if (!destination.inhabited()) {
                return false;
            }
            return check(0, 0);
        }
    };

    bool TransmuteTypeChecker::check(const HIRTypeData* sourceType, const HIRTypeData* destinationType) {
        const auto key = ::std::make_pair(sourceType, destinationType);
        auto existing = cache.find(key);
        if (existing != cache.end()) {
            return existing->second >= 0;
        }
        auto inserted = cache.insert({key, 0}).first;

        TransmuteDfa source;
        TransmuteLayoutBuilder sourceBuilder(sp, resolve, false, assumeSafety);
        if (!sourceBuilder.makeDfa(sourceType, source)) {
            inserted->second = -1;
            return false;
        }

        TransmuteDfa destination;
        TransmuteLayoutBuilder destinationBuilder(sp, resolve, true, assumeSafety);
        if (!destinationBuilder.makeDfa(destinationType, destination)) {
            inserted->second = -1;
            return false;
        }

        const bool result = TransmuteRelation(source, destination, *this).check();
        inserted->second = result ? 1 : -1;
        return result;
    }
}

bool TargetTypesAreTransmutable(
    const Span& sp,
    const StaticTraitResolve& resolve,
    const HIRTypeData* src,
    const HIRTypeData* dst,
    bool assumeAlignment,
    bool assumeLifetimes,
    bool assumeSafety,
    bool assumeValidity
) {
    (void)assumeLifetimes;
    return TransmuteTypeChecker(sp, resolve, assumeAlignment, assumeSafety, assumeValidity).check(src, dst);
}

TargetArch::Atomics::Atomics(bool u8, bool u16, bool u32, bool u64, bool ptr)
    : u8(u8)
    , u16(u16)
    , u32(u32)
    , u64(u64)
    , ptr(ptr)
{
}

TargetArch::Alignments::Alignments(uint8_t u16, uint8_t u32, uint8_t u64, uint8_t u128, uint8_t f32, uint8_t f64, uint8_t ptr)
    : u16(u16)
    , u32(u32)
    , u64(u64)
    , u128(u128)
    , f32(f32)
    , f64(f64)
    , ptr(ptr)
{
}

std::ostream& operator<<(std::ostream& os, const TypeRepr::FieldPath& x) {
    os << x.size << "@" << x.index;
    for (auto idx : x.subFields) {
        if (idx == TypeRepr::FieldPath::ARRAY_ELEMENT) {
            os << "[0]";
        } else {
            os << "." << idx;
        }
    }
    return os;
}
