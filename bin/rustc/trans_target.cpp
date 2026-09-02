#include "trans_target.h"

#include "toml.h"
#include "output.h"
#include "hir_hir.h"
#include "settings.h"
#include "expand_cfg.h"
#include "wire_board.h"
#include "output_file.h"
#include "trans_mangling.h"
#include "hir_typeck_common.h"
#include "hir_typeck_helpers.h"
#include "hir_typeck_monomorph.h"
#include "hir_conv_main_bindings.h"
#include "hir_conv_constant_evaluation.h"

#include <std/alg/qsort.h>
#include <std/alg/range.h>
#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>

#include <map>
#include <array>
#include <bitset>
#include <climits>
#include <fstream>
#include <algorithm>
#include <unordered_map>

using namespace stl;

static void setTypeRepr(const StaticTraitResolve& resolve, const Span& sp, const HIRType* ty, std::unique_ptr<TypeRepr> repr);

namespace {
    constexpr size_t TRANSMUTE_BYTE_VALUES = 257;

    struct VectorLess {
        bool operator()(const Vector<unsigned>& left, const Vector<unsigned>& right) const {
            return std::lexicographical_compare(left.begin(), left.end(), right.begin(), right.end());
        }
    };

    constexpr size_t TRANSMUTE_UNINITIALISED = 256;

    using TransmuteByteSet = std::bitset<TRANSMUTE_BYTE_VALUES>;

    void appendReverse(Vector<size_t>& output, const Vector<size_t>& input) {
        for (size_t i = input.length(); i > 0; i--) {
            output.pushBack(input[i - 1]);
        }
    }

    struct Ent {
        unsigned int field;
        size_t size;
        size_t align;
        const HIRType* ty;
        bool userAlign = false;
    };

    struct AsyncDropFieldLayout {
        struct Future {
            size_t size;
            size_t align;
        };

        Vector<Future> futures;

        bool empty() const;
    };

    struct TransmuteReference {
        bool isMutable;
        const HIRType* referent;
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
            Vector<unsigned> epsilon;
            Vector<ByteEdge> bytes;
            Vector<ReferenceEdge> references;
        };

        struct Fragment {
            unsigned start;
            unsigned accept;
        };

        std::vector<State> states;

        unsigned addState();

        Fragment empty();

        Fragment uninhabited();

        Fragment byte(const TransmuteByteSet& values);

        Fragment reference(TransmuteReference reference);

        Fragment then(Fragment left, Fragment right);

        Fragment alternative(Vector<Fragment> alternatives);
    };

    struct TransmuteDfa {
        using Transitions = std::array<int, TRANSMUTE_BYTE_VALUES>;

        std::vector<Transitions> transitions;
        std::vector<std::vector<std::pair<TransmuteReference, unsigned>>> references;
        Vector<bool> accepting;

        bool inhabited() const;
    };

    struct TransmuteLayoutBuilder {
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

        static TransmuteByteSet byteRange(unsigned first, unsigned last);

        Built bytes(size_t count, const TransmuteByteSet& values);

        Built padding(size_t count);

        Built number(size_t count);

        Built exact(U128 value, size_t count);

        Built character();

        Built combine(std::vector<Segment> segments, size_t totalSize);

        Built aggregate(const TypeRepr& repr, int skipField = -1);

        void addVariantPayload(std::vector<Segment>& segments, const TypeRepr& outerRepr, unsigned variant, bool skipSyntheticTag, size_t tagOffset, size_t tagSize);

        Built taggedVariant(const TypeRepr& repr, unsigned variant, size_t tagOffset, size_t tagSize, U128 tag, bool skipSyntheticTag);

        Built enumLayout(const HIRType* ty, const TypeRepr& repr, const HIREnum& enm);

        Built build(const HIRType* ty);

        Vector<unsigned> epsilonClosure(Vector<unsigned> states) const;

        TransmuteNfa nfa;

        TransmuteLayoutBuilder(const Span& sp, const StaticTraitResolve& resolve, bool destination, bool assumeSafety);

        bool makeDfa(const HIRType* ty, TransmuteDfa& out);
    };

    struct TransmuteTypeChecker {
        const Span& sp;
        const StaticTraitResolve& resolve;
        bool assumeAlignment;
        bool assumeSafety;
        bool assumeValidity;
        std::map<std::pair<const HIRType*, const HIRType*>, int> cache;

        TransmuteTypeChecker(const Span& sp, const StaticTraitResolve& resolve, bool assumeAlignment, bool assumeSafety, bool assumeValidity);

        bool check(const HIRType* sourceType, const HIRType* destinationType);

        bool referencesCompatible(const TransmuteReference& source, const TransmuteReference& destination);

        bool validityIsAssumed() const;
    };

    struct TransmuteRelation {
        const TransmuteDfa& source;
        const TransmuteDfa& destination;
        TransmuteTypeChecker& typeChecker;
        bool assumeValidity;
        std::map<std::pair<unsigned, unsigned>, int> cache;

        bool check(unsigned sourceState, unsigned destinationState);

        TransmuteRelation(const TransmuteDfa& source, const TransmuteDfa& destination, TransmuteTypeChecker& typeChecker);

        bool check();
    };

    using TargetLayoutContext = WireBoard::TargetLayoutContext;

    TargetArch archX86_64() {
        return {
            "x86_64",
            64,
            false,
            TargetArch::Atomics(/*atomic(u8)=*/true, /*atomic(u16)=*/true, /*atomic(u32)=*/true, true, true),
            TargetArch::Alignments(2, 4, 8, 16, 4, 8, 8)
            //TargetArch::Alignments(2, 4, 8, 8, 4, 8, 8) // TODO: Alignment of u128 is 8 with rustc, but gcc uses 16
        };
    }

    TargetArch archX32() {
        return {"x86_64", 32, false, archX86_64().atomics, TargetArch::Alignments(2, 4, 8, 16, 4, 8, 4)};
    }

    TargetArch archX86() {
        return {"x86", 32, false, {/*atomic(u8)=*/true, /*u16=*/true, /*u32=*/true, /*u64=*/true, /*ptr=*/true}, TargetArch::Alignments(2, 4, /*u64*/ 4, /*u128*/ 4, 4, 4, /*ptr*/ 4)};
    }

    TargetArch archArm64() {
        return {"aarch64", 64, false, {/*atomic(u8)=*/true, true, true, true, true}, TargetArch::Alignments(2, 4, 8, 16, 4, 8, 8)};
    }

    TargetArch archArm32() {
        return {"arm", 32, false, {/*atomic(u8)=*/true, false, true, false, true}, TargetArch::Alignments(2, 4, 8, 16, 4, 8, 4)};
    }

    TargetArch archM68k() {
        return {"m68k", 32, true, {/*atomic(u8)=*/true, false, true, false, true}, TargetArch::Alignments(2, 2, 2, 2, 2, 2, 2)};
    }

    TargetArch archPowerpc64() {
        return {"powerpc64", 64, true, {/*atomic(u8)=*/true, true, true, true, true}, TargetArch::Alignments(2, 4, 8, 16, 4, 8, 8)};
    }

    TargetArch archPowerpc64le() {
        return {"powerpc64", 64, false, {/*atomic(u8)=*/true, true, true, true, true}, TargetArch::Alignments(2, 4, 8, 16, 4, 8, 8)};
    }

    TargetArch archPowerpc() {
        return {"powerpc", 32, true, {/*atomic(u8)=*/true, true, true, true, true}, TargetArch::Alignments(2, 4, 8, 8, 4, 8, 4)};
    }

    TargetArch archRiscv64() {
        return {"riscv64", 64, false, {/*atomic(u8)=*/true, true, true, true, true}, TargetArch::Alignments(2, 4, 8, 16, 4, 8, 8)};
    }

    TargetSpec loadSpecFromFile(const std::string& filename) {
        TargetSpec rv;

        TomlFile tomlFile(filename);
        for (auto keyVal : tomlFile) {
            BUG_ASSERT(keyVal.path.size() > 1);

            DEBUG(keyVal.path << StringView(" = ") << keyVal.value);
            auto checkPathLength = [&](const TomlKeyValue& kv, unsigned len) {
                if (kv.path.size() != len) {
                    if (kv.path.size() > len) {
                        sysE << StringView("ERROR: Unexpected sub-node to  ") << kv.path << StringView(" in ") << filename << endL;
                    } else {
                        sysE << StringView("ERROR: Expected sub-nodes in  ") << kv.path << StringView(" in ") << filename << endL;
                    }
                    exit(1);
                }
            };
            auto checkPathLengthMin = [&](const TomlKeyValue& kv, unsigned len) {
                if (kv.path.size() < len) {
                    sysE << StringView("ERROR: Expected sub-nodes in ") << kv.path << StringView(" in ") << filename << endL;
                }
            };

            {
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
                        if (keyVal.value.asString() == archArm32().name) {
                            rv.arch = archArm32();
                        } else if (keyVal.value.asString() == archArm64().name) {
                            rv.arch = archArm64();
                        } else if (keyVal.value.asString() == archX86().name) {
                            rv.arch = archX86();
                        } else if (keyVal.value.asString() == archX86_64().name) {
                            rv.arch = archX86_64();
                        } else if (keyVal.value.asString() == archM68k().name) {
                            rv.arch = archM68k();
                        } else if (keyVal.value.asString() == archPowerpc().name) {
                            rv.arch = archPowerpc();
                        } else if (keyVal.value.asString() == archPowerpc64().name) {
                            rv.arch = archPowerpc64();
                        } else if (keyVal.value.asString() == archPowerpc64le().name) {
                            rv.arch = archPowerpc64le();
                        } else if (keyVal.value.asString() == archRiscv64().name) {
                            rv.arch = archRiscv64();
                        } else {
                            sysE << StringView("ERROR: Unknown architecture name '") << keyVal.value.asString() << StringView("' in ") << filename << endL;
                            exit(1);
                        }
                    } else {
                        sysE << StringView("Warning: Unknown configuration item ") << keyVal.path[0] << StringView(".") << keyVal.path[1] << StringView(" in ") << filename << endL;
                    }
                } else if (keyVal.path[0] == "backend") {
                    checkPathLengthMin(keyVal, 2);
                    if (keyVal.path[1] == "c") {
                        checkPathLengthMin(keyVal, 3);

                        if (keyVal.path[2] == "variant") {
                            checkPathLength(keyVal, 3);
                            if (keyVal.value.asString() != "gnu") {
                                sysE << StringView("ERROR: Unknown C variant name '") << keyVal.value.asString() << StringView("' in ") << filename << endL;
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
                            sysE << StringView("WARNING: Unknown field backend.c.") << keyVal.path[2] << StringView(" in ") << filename << endL;
                        }
                    } else {
                        sysE << StringView("WARNING: Unknown configuration item backend.") << keyVal.path[1] << StringView(" in ") << filename << endL;
                    }
                } else if (keyVal.path[0] == "arch") {
                    checkPathLengthMin(keyVal, 2);
                    if (keyVal.path[1] == "name") {
                        checkPathLength(keyVal, 2);
                        if (rv.arch.name != "") {
                            sysE << StringView("ERROR: Architecture already specified to be '") << rv.arch.name << StringView("'") << endL;
                            exit(1);
                        }
                        rv.arch.name = keyVal.value.asString();
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
                            sysE << StringView("WARNING: Unknown field arch.alignments.") << keyVal.path[1] << StringView(" in ") << filename << endL;
                        }
                    } else {
                        sysE << StringView("WARNING: Unknown field arch.") << keyVal.path[1] << StringView(" in ") << filename << endL;
                    }
                } else {
                    sysE << StringView("WARNING: Unknown configuration item ") << keyVal.path[0] << StringView(" in ") << filename << endL;
                }
            }
        }

        // TODO: Ensure that everything is set
        if (rv.arch.name == "") {
            sysE << StringView("ERROR: Architecture not specified in ") << filename << endL;
            exit(1);
        }
        if (rv.family == "windows" || rv.osName == "windows") {
            sysE << StringView("ERROR: Windows targets are not supported in ") << filename << endL;
            exit(1);
        }

        return rv;
    }

    void saveSpecToFile(ObjPool& pool, const std::string& filename, const TargetSpec& spec) {
        // TODO: Have a round-trip unit test
        auto& of = *outputFile(pool, filename.c_str());

        struct H {
            static const char* tfstr(bool v) {
                return v ? "true" : "false";
            }
        };

        of << StringView("[target]\n") << StringView("family = \"") << spec.family << StringView("\"\n") << StringView("os-name = \"") << spec.osName << StringView("\"\n") << StringView("env-name = \"") << spec.envName << StringView("\"\n") << StringView("\n") << StringView("[backend.c]\n") << StringView("variant = \"gnu\"\n") << StringView("target = \"") << spec.backendC.cCompiler << StringView("\"\n") << StringView("compiler-opts = [");
        for (const auto& s : spec.backendC.compilerOpts) {
            of << StringView("\"") << s << StringView("\",");
        }
        of << StringView("]\n") << StringView("linker-opts-pre = [");
        for (const auto& s : spec.backendC.linkerOptsPre) {
            of << StringView("\"") << s << StringView("\",");
        }
        of << StringView("]\n") << StringView("linker-opts-post = [");
        for (const auto& s : spec.backendC.linkerOptsPost) {
            of << StringView("\"") << s << StringView("\",");
        }
        of << StringView("]\n") << StringView("\n") << StringView("[arch]\n") << StringView("name = \"") << spec.arch.name << StringView("\"\n") << StringView("pointer-bits = ") << spec.arch.pointerBits << StringView("\n") << StringView("is-big-endian = ") << H::tfstr(spec.arch.bigEndian) << StringView("\n") << StringView("has-atomic-u8 = ") << H::tfstr(spec.arch.atomics.u8) << StringView("\n") << StringView("has-atomic-u16 = ") << H::tfstr(spec.arch.atomics.u16) << StringView("\n") << StringView("has-atomic-u32 = ") << H::tfstr(spec.arch.atomics.u32) << StringView("\n") << StringView("has-atomic-u64 = ") << H::tfstr(spec.arch.atomics.u64) << StringView("\n") << StringView("has-atomic-ptr = ") << H::tfstr(spec.arch.atomics.ptr) << StringView("\n") << StringView("alignments = {") << StringView(" u16 = ") << static_cast<int>(spec.arch.alignments.u16) << StringView(",") << StringView(" u32 = ") << static_cast<int>(spec.arch.alignments.u32) << StringView(",") << StringView(" u64 = ") << static_cast<int>(spec.arch.alignments.u64) << StringView(",") << StringView(" u128 = ") << static_cast<int>(spec.arch.alignments.u128) << StringView(",") << StringView(" f32 = ") << static_cast<int>(spec.arch.alignments.f32) << StringView(",") << StringView(" f64 = ") << static_cast<int>(spec.arch.alignments.f64) << StringView(",") << StringView(" ptr = ") << static_cast<int>(spec.arch.alignments.ptr) << StringView(" }\n") << StringView("\n");
        of.finish();
    }

    TargetSpec initFromSpecName(const std::string& targetName) {
#define BACKEND_C_OPTS_GNU {"-ffunction-sections", "-pthread"}, {"-Wl,--start-group"}, {"-Wl,--end-group", "-Wl,--gc-sections", "-l", "atomic"}
        if (targetName.find('/') != std::string::npos) {
            return loadSpecFromFile(targetName);
        } else if (targetName == "i586-linux-gnu" || targetName == "i586-unknown-linux-gnu") {
            return TargetSpec{"unix", "linux", "gnu", {true, "i586-linux-gnu", BACKEND_C_OPTS_GNU}, archX86()};
        } else if (targetName == "x86_64-linux-gnu" || targetName == "x86_64-unknown-linux-gnu") {
            return TargetSpec{"unix", "linux", "gnu", {true /*false*/, "x86_64-linux-gnu", BACKEND_C_OPTS_GNU}, archX86_64()};
        } else if (targetName == "x86_64-linux-musl" || targetName == "x86_64-unknown-linux-musl") {
            return TargetSpec{"unix", "linux", "musl", {true /*false*/, "x86_64-linux-musl", BACKEND_C_OPTS_GNU}, archX86_64()};
        } else if (targetName == "x86_64-unknown-linux-gnux32") {
            return TargetSpec{"unix", "linux", "gnu", {true, "x86_64-unknown-linux-gnux32", BACKEND_C_OPTS_GNU}, archX32()};
        } else if (targetName == "arm-linux-gnu" || targetName == "arm-unknown-linux-gnu") {
            return TargetSpec{"unix", "linux", "gnu", {true, "arm-elf-eabi", BACKEND_C_OPTS_GNU}, archArm32()};
        } else if (targetName == "aarch64-linux-gnu" || targetName == "aarch64-unknown-linux-gnu") {
            return TargetSpec{"unix", "linux", "gnu", {false, "aarch64-linux-gnu", BACKEND_C_OPTS_GNU}, archArm64()};
        } else if (targetName == "m68k-linux-gnu" || targetName == "m68k-unknown-linux-gnu") {
            return TargetSpec{"unix", "linux", "gnu", {true, "m68k-linux-gnu", BACKEND_C_OPTS_GNU}, archM68k()};
        } else if (targetName == "powerpc64-unknown-linux-gnu") {
            return TargetSpec{"unix", "linux", "gnu", {false, "powerpc64-unknown-linux-gnu", BACKEND_C_OPTS_GNU}, archPowerpc64()};
        } else if (targetName == "powerpc64le-unknown-linux-gnu") {
            return TargetSpec{"unix", "linux", "gnu", {false, "powerpc64le-unknown-linux-gnu", BACKEND_C_OPTS_GNU}, archPowerpc64le()};
        } else if (targetName == "riscv64-unknown-linux-gnu") {
            return TargetSpec{"unix", "linux", "gnu", {false, "riscv64-unknown-linux-gnu", BACKEND_C_OPTS_GNU}, archRiscv64()};
        } else if (targetName == "riscv64-unknown-linux-musl") {
            return TargetSpec{"unix", "linux", "musl", {false, "riscv64-unknown-linux-musl", BACKEND_C_OPTS_GNU}, archRiscv64()};
        } else if (targetName == "i686-unknown-freebsd") {
            return TargetSpec{"unix", "freebsd", "gnu", {true, "i686-unknown-freebsd", BACKEND_C_OPTS_GNU}, archX86()};
        } else if (targetName == "x86_64-unknown-freebsd") {
            return TargetSpec{"unix", "freebsd", "gnu", {false, "x86_64-unknown-freebsd", BACKEND_C_OPTS_GNU}, archX86_64()};
        } else if (targetName == "arm-unknown-freebsd") {
            return TargetSpec{"unix", "freebsd", "gnu", {true, "arm-unknown-freebsd", BACKEND_C_OPTS_GNU}, archArm32()};
        } else if (targetName == "aarch64-unknown-freebsd") {
            return TargetSpec{"unix", "freebsd", "gnu", {false, "aarch64-unknown-freebsd", BACKEND_C_OPTS_GNU}, archArm64()};
        } else if (targetName == "x86_64-unknown-netbsd") {
            return TargetSpec{"unix", "netbsd", "gnu", {false, "x86_64-unknown-netbsd", BACKEND_C_OPTS_GNU}, archX86_64()};
        } else if (targetName == "i686-unknown-openbsd") {
            return TargetSpec{"unix", "openbsd", "gnu", {true, "i686-unknown-openbsd", BACKEND_C_OPTS_GNU}, archX86()};
        } else if (targetName == "x86_64-unknown-openbsd") {
            return TargetSpec{"unix", "openbsd", "gnu", {false, "x86_64-unknown-openbsd", BACKEND_C_OPTS_GNU}, archX86_64()};
        } else if (targetName == "arm-unknown-openbsd") {
            return TargetSpec{"unix", "openbsd", "gnu", {true, "arm-unknown-openbsd", BACKEND_C_OPTS_GNU}, archArm32()};
        } else if (targetName == "aarch64-unknown-openbsd") {
            return TargetSpec{"unix", "openbsd", "gnu", {false, "aarch64-unknown-openbsd", BACKEND_C_OPTS_GNU}, archArm64()};
        } else if (targetName == "x86_64-unknown-dragonfly") {
            return TargetSpec{"unix", "dragonfly", "gnu", {false, "x86_64-unknown-dragonfly", BACKEND_C_OPTS_GNU}, archX86_64()};
        } else if (targetName == "i686-apple-darwin") {
            return TargetSpec{"unix", "macos", "", {false, "x86_64-apple-darwin", {"-march=yonah"}, {}}, archX86_64()};
        } else if (targetName == "x86_64-apple-darwin") {
            return TargetSpec{"unix", "macos", "", {false, "x86_64-apple-darwin", {"-march=core2"}, {}}, archX86_64()};
        } else if (targetName == "aarch64-apple-darwin") {
            return TargetSpec{"unix", "macos", "", {false, "aarch64-apple-darwin", {}, {}}, archArm64()};
        } else if (targetName == "powerpc-apple-darwin") {
            return TargetSpec{"unix", "macos", "", {true, "powerpc-apple-darwin", {}, {}, {"-l", "atomic"}}, archPowerpc()};
        } else if (targetName == "powerpc64-apple-darwin") {
            return TargetSpec{"unix", "macos", "", {false, "powerpc64-apple-darwin", {}, {}}, archPowerpc64()};
        } else if (targetName == "arm-unknown-haiku") {
            return TargetSpec{"unix", "haiku", "gnu", {true, "arm-unknown-haiku", {}, {}}, archArm32()};
        } else if (targetName == "x86_64-unknown-haiku") {
            return TargetSpec{"unix", "haiku", "gnu", {false, "x86_64-unknown-haiku", {}, {}}, archX86_64()};
        } else {
            sysE << StringView("Unknown target name '") << targetName << StringView("'") << endL;
            abort();
        }
        UNREACHABLE();
    }

    bool closureHasNoCaptures(const StaticTraitResolve& resolve, const HIRExprNodeClosure& closure) {
        if (closure.cls == HIRExprNodeClosure::Class::NoCapture) {
            return true;
        }
        if (closure.cls != HIRExprNodeClosure::Class::Unknown) {
            return false;
        }

        struct CaptureVisitor: HIRExprVisitorDef {
            Vector<unsigned int> definitions;
            Vector<unsigned int> uses;

            explicit CaptureVisitor(HIRTypeInterner& types)
                : HIRExprVisitorDef(types)
            {
            }

            void visitPattern(const Span& sp, HIRPattern& pattern) override {
                for (const auto& binding : pattern.bindings) {
                    definitions.pushBack(binding.slot);
                }
                if (const auto* split = pattern.data.opt_SplitSlice(); split && split->extraBind.isValid()) {
                    definitions.pushBack(split->extraBind.slot);
                }
                HIRExprVisitorDef::visitPattern(sp, pattern);
            }

            void visit(HIRExprNodeVariable& node) override {
                uses.pushBack(node.slot);
            }
        } visitor(resolve.hirCrate().types);

        auto* closureMut = cast<HIRExprNodeClosure>(&resolve.hirCrateMut().findExprNodeMut(Span(), closure));
        ASSERT_BUG(Span(), closureMut, StringView("Closure owner lookup returned the wrong node type"));
        closureMut->visit(visitor);
        std::sort(visitor.definitions.mutBegin(), visitor.definitions.mutEnd());
        const auto* uniqueEnd = std::unique(visitor.definitions.mutBegin(), visitor.definitions.mutEnd());
        while (visitor.definitions.end() != uniqueEnd) {
            visitor.definitions.popBack();
        }
        return std::all_of(visitor.uses.begin(), visitor.uses.end(), [&](unsigned int slot) {
            return std::binary_search(visitor.definitions.begin(), visitor.definitions.end(), slot);
        });
    }

    bool makeFieldEnt(const Span& sp, const StaticTraitResolve& resolve, unsigned idx, const HIRType* ty, Ent& out) {
        size_t size, align;
        if (!TargetGetSizeAndAlignOf(sp, resolve, ty, size, align)) {
            DEBUG(StringView("Can't get size/align of ") << ty);
            return false;
        }
        out = Ent{idx, size, align, nullptr, false};
        out.userAlign = TargetTypeHasUserAlignment(sp, resolve, ty);
        out.ty = mv$(ty);
        return true;
    }

    bool structEnumerateFields(const Span& sp, const StaticTraitResolve& resolve, const HIRType* ty, std::vector<Ent>& ents) {
        const auto& te = ty->as_Path();
        const auto& str = *te.binding.as_Struct();
        auto monomorphCb = MonomorphStatePtr(resolve.hirCrate().types, ty, &te.path.data.as_Generic().params, nullptr);
        auto monomorph = [&](const auto& tpl) {
            return resolve.monomorphExpand(sp, tpl, monomorphCb);
        };
        switch (str.data.tag()) {
            case HIRStructData::TAG_Unit: {
                break;
            }
            case HIRStructData::TAG_Tuple: {
                auto& se = str.data.as_Tuple();
                unsigned int idx = 0;
                for (const auto& e : se) {
                    Ent ent;
                    if (!makeFieldEnt(sp, resolve, idx, monomorph(e.ent), ent)) {
                        return false;
                    }
                    DEBUG(StringView("#") << idx << StringView(": ") << ent);
                    idx++;
                    ents.push_back(mv$(ent));
                }
                break;
            }
            case HIRStructData::TAG_Named: {
                auto& se = str.data.as_Named();
                unsigned int idx = 0;
                for (const auto& e : se) {
                    Ent ent;
                    if (!makeFieldEnt(sp, resolve, idx, monomorph(e.ty), ent)) {
                        return false;
                    }
                    DEBUG(StringView("#") << idx << StringView(" ") << e.name << StringView(": ") << ent);
                    idx++;
                    ents.push_back(mv$(ent));
                }
                break;
            }
        }
        return true;
    }

    enum class StructSorting {
        None,
        AllButFinal,
        All,
    };

    size_t alignTo(size_t offset, size_t align) {
        return (offset + align - 1) / align * align;
    }

    const HIRType* asyncDropGlueType(const Span& sp, const StaticTraitResolve& resolve, const HIRType* outerTy, const HIRType* dropeeTy) {
        const auto* outerPath = outerTy->opt_Path();
        ASSERT_BUG(sp, outerPath && outerPath->binding.is_Struct() && outerPath->path.data.is_Generic(), StringView("invalid async-drop glue type ") << outerTy);
        auto path = outerPath->path.data.as_Generic().clone();
        ASSERT_BUG(sp, !path.params.types.empty(), StringView("async-drop glue type without its dropee argument: ") << outerTy);
        path.params.types[0] = dropeeTy;
        return resolve.hirCrate().types.path(std::move(path), outerPath->binding.as_Struct());
    }

    bool addAsyncDropFieldLayout(const Span& sp, const StaticTraitResolve& resolve, const HIRType* outerTy, const HIRType* fieldTy, AsyncDropFieldLayout& out) {
        if (!resolve.typeNeedsAsyncDrop(sp, fieldTy)) {
            return true;
        }
        auto glueTy = asyncDropGlueType(sp, resolve, outerTy, fieldTy);
        size_t size = 0;
        size_t align = 0;
        if (!TargetGetSizeAndAlignOf(sp, resolve, glueTy, size, align)) {
            return false;
        }
        out.futures.pushBack({size, align});
        return true;
    }

    bool asyncDropStructFieldsLayout(const Span& sp, const StaticTraitResolve& resolve, const HIRType* outerTy, const HIRType* ty, AsyncDropFieldLayout& out) {
        if (const auto* tuple = ty->opt_Tuple()) {
            for (const auto& fieldTy : *tuple) {
                if (!addAsyncDropFieldLayout(sp, resolve, outerTy, fieldTy, out)) {
                    return false;
                }
            }
            return true;
        }

        const auto* pathTy = ty->opt_Path();
        if (!pathTy || !pathTy->binding.is_Struct() || !pathTy->path.data.is_Generic()) {
            return true;
        }
        const auto& generic = pathTy->path.data.as_Generic();
        if (generic.path == resolve.hirCrate().getLangItemPathOpt("manually_drop")) {
            return true;
        }

        const auto& str = *pathTy->binding.as_Struct();
        auto monomorph = MonomorphStatePtr(resolve.hirCrate().types, ty, &generic.params, nullptr);
        switch (str.data.tag()) {
            case HIRStructData::TAG_Unit:
                break;
            case HIRStructData::TAG_Tuple:
                for (const auto& field : str.data.as_Tuple()) {
                    auto fieldTy = resolve.monomorphExpand(sp, field.ent, monomorph);
                    if (!addAsyncDropFieldLayout(sp, resolve, outerTy, fieldTy, out)) {
                        return false;
                    }
                }
                break;
            case HIRStructData::TAG_Named:
                for (const auto& field : str.data.as_Named()) {
                    auto fieldTy = resolve.monomorphExpand(sp, field.ty, monomorph);
                    if (!addAsyncDropFieldLayout(sp, resolve, outerTy, fieldTy, out)) {
                        return false;
                    }
                }
                break;
        }
        return true;
    }

    bool addAsyncDropCoroutineStateFieldLayout(const Span& sp, const StaticTraitResolve& resolve, const HIRType* outerTy, const HIRType* fieldTy, AsyncDropFieldLayout& out) {
        const auto* path = fieldTy->opt_Path();
        if (!path || !path->binding.is_Union() || !path->path.data.is_Generic()) {
            return addAsyncDropFieldLayout(sp, resolve, outerTy, fieldTy, out);
        }

        const auto& generic = path->path.data.as_Generic();
        auto monomorph = MonomorphStatePtr(resolve.hirCrate().types, fieldTy, &generic.params, nullptr);
        for (const auto& variant : path->binding.as_Union()->variants) {
            auto variantTy = resolve.monomorphExpand(sp, variant.ty, monomorph);
            if (!addAsyncDropFieldLayout(sp, resolve, outerTy, variantTy, out)) {
                return false;
            }
        }
        return true;
    }

    bool asyncDropCoroutineStateLayout(const Span& sp, const StaticTraitResolve& resolve, const HIRType* outerTy, const HIRType* stateTy, AsyncDropFieldLayout& out) {
        const auto* path = stateTy->opt_Path();
        ASSERT_BUG(sp, path && path->binding.is_Struct() && path->path.data.is_Generic(), StringView("invalid coroutine state type ") << stateTy);
        const auto& generic = path->path.data.as_Generic();
        const auto& str = *path->binding.as_Struct();
        auto monomorph = MonomorphStatePtr(resolve.hirCrate().types, stateTy, &generic.params, nullptr);
        switch (str.data.tag()) {
            case HIRStructData::TAG_Unit:
                break;
            case HIRStructData::TAG_Tuple:
                for (const auto& field : str.data.as_Tuple()) {
                    auto fieldTy = resolve.monomorphExpand(sp, field.ent, monomorph);
                    if (!addAsyncDropCoroutineStateFieldLayout(sp, resolve, outerTy, fieldTy, out)) {
                        return false;
                    }
                }
                break;
            case HIRStructData::TAG_Named:
                for (const auto& field : str.data.as_Named()) {
                    auto fieldTy = resolve.monomorphExpand(sp, field.ty, monomorph);
                    if (!addAsyncDropCoroutineStateFieldLayout(sp, resolve, outerTy, fieldTy, out)) {
                        return false;
                    }
                }
                break;
        }
        return true;
    }

    bool asyncDropCoroutineFieldsLayout(const Span& sp, const StaticTraitResolve& resolve, const HIRType* outerTy, const HIRType* ty, AsyncDropFieldLayout& out) {
        const auto& pathTy = ty->as_Path();
        ASSERT_BUG(sp, (pathTy.isFuture() || pathTy.isGenerator()) && pathTy.binding.is_Struct() && pathTy.path.data.is_Generic(), StringView("invalid coroutine type ") << ty);
        const auto* fields = pathTy.binding.as_Struct()->data.opt_Tuple();
        ASSERT_BUG(sp, fields && !fields->empty(), StringView("coroutine without its state field: ") << ty);
        auto monomorph = MonomorphStatePtr(resolve.hirCrate().types, ty, &pathTy.path.data.as_Generic().params, nullptr);
        for (size_t i = 0; i < fields->size(); i++) {
            auto fieldTy = resolve.monomorphExpand(sp, fields->at(i).ent, monomorph);
            if (i == 0) {
                const auto* fieldPath = fieldTy->opt_Path();
                ASSERT_BUG(sp, fieldPath && fieldPath->path.data.is_Generic() && fieldPath->path.data.as_Generic().path == resolve.hirCrate().getLangItemPath(sp, "maybe_uninit") && fieldPath->path.data.as_Generic().params.types.size() == 1, StringView("coroutine state is not MaybeUninit<State>: ") << fieldTy);
                if (!asyncDropCoroutineStateLayout(sp, resolve, outerTy, fieldPath->path.data.as_Generic().params.types[0], out)) {
                    return false;
                }
            } else if (!addAsyncDropFieldLayout(sp, resolve, outerTy, fieldTy, out)) {
                return false;
            }
        }
        return true;
    }

    size_t appendAsyncDropFields(size_t offset, size_t& align, const AsyncDropFieldLayout& fields, bool hasDropline) {
        size_t slotSize = 0;
        size_t slotAlign = 1;
        for (const auto& future : fields.futures) {
            offset = alignTo(offset, future.align);
            offset += future.size;
            align = std::max(align, future.align);
            slotSize = std::max(slotSize, future.size);
            slotAlign = std::max(slotAlign, future.align);
        }
        if (hasDropline) {
            offset = alignTo(offset, slotAlign);
            offset += alignTo(slotSize, slotAlign);
            align = std::max(align, slotAlign);
        }
        return offset;
    }

    bool extendAsyncDropGlueRepr(const Span& sp, const StaticTraitResolve& resolve, const HIRType* ty, TypeRepr& repr) {
        const auto& pathTy = ty->as_Path();
        const auto& path = pathTy.path.data.as_Generic();
        ASSERT_BUG(sp, !path.params.types.empty(), StringView("async-drop glue type without its dropee argument: ") << ty);
        const auto* dropeeTy = path.params.types[0];

        HIRPath dropPath{HIRSimplePath()};
        const HIRType* customFutureTy;
        const bool hasCustom = (customFutureTy = resolve.findAsyncDrop(sp, dropeeTy, dropPath));
        size_t customSize = 0;
        size_t customAlign = 1;
        if (hasCustom && !TargetGetSizeAndAlignOf(sp, resolve, customFutureTy, customSize, customAlign)) {
            return false;
        }

        size_t offset = repr.size;
        size_t align = repr.align;
        bool hasAsyncFields = false;
        if (const auto* array = dropeeTy->opt_Array()) {
            if (!array->size.is_Known()) {
                return false;
            }
            if (array->size.as_Known() != 0 && resolve.typeNeedsAsyncDrop(sp, array->inner)) {
                AsyncDropFieldLayout element;
                if (!addAsyncDropFieldLayout(sp, resolve, ty, array->inner, element)) {
                    return false;
                }
                ASSERT_BUG(sp, element.futures.length() == 1, StringView("async array element did not produce one glue future"));
                hasAsyncFields = true;
                const size_t pointerSize = TargetGetPointerBits() / 8;
                offset = alignTo(offset, pointerSize);
                offset += pointerSize * 3;
                align = std::max(align, pointerSize);
                offset = appendAsyncDropFields(offset, align, element, true);
            }
        } else if (const auto* path = dropeeTy->opt_Path(); path && path->binding.is_Enum() && path->path.data.is_Generic()) {
            const auto& enm = *path->binding.as_Enum();
            if (const auto* variants = enm.data.opt_Data()) {
                auto monomorph = MonomorphStatePtr(resolve.hirCrate().types, dropeeTy, &path->path.data.as_Generic().params, nullptr);
                size_t largestOffset = offset;
                size_t largestAlign = align;
                for (const auto& variant : *variants) {
                    auto variantTy = resolve.monomorphExpand(sp, variant.type, monomorph);
                    AsyncDropFieldLayout fields;
                    if (!asyncDropStructFieldsLayout(sp, resolve, ty, variantTy, fields)) {
                        return false;
                    }
                    if (fields.empty()) {
                        continue;
                    }
                    hasAsyncFields = true;
                    size_t variantOffset = offset;
                    size_t variantAlign = align;
                    const bool hasDropline = hasCustom || fields.futures.length() > 1;
                    if (hasDropline) {
                        const size_t pointerSize = TargetGetPointerBits() / 8;
                        variantOffset = alignTo(variantOffset, pointerSize) + pointerSize;
                        variantAlign = std::max(variantAlign, pointerSize);
                    }
                    variantOffset = appendAsyncDropFields(variantOffset, variantAlign, fields, hasDropline);
                    if (alignTo(variantOffset, variantAlign) > alignTo(largestOffset, largestAlign)) {
                        largestOffset = variantOffset;
                        largestAlign = variantAlign;
                    }
                }
                offset = largestOffset;
                align = largestAlign;
            }
        } else {
            AsyncDropFieldLayout fields;
            const auto* coroutinePath = dropeeTy->opt_Path();
            const bool ok = coroutinePath && (coroutinePath->isFuture() || coroutinePath->isGenerator()) ? asyncDropCoroutineFieldsLayout(sp, resolve, ty, dropeeTy, fields) : asyncDropStructFieldsLayout(sp, resolve, ty, dropeeTy, fields);
            if (!ok) {
                return false;
            }
            if (!fields.empty()) {
                hasAsyncFields = true;
                const bool hasDropline = hasCustom || fields.futures.length() > 1;
                if (hasDropline) {
                    const size_t pointerSize = TargetGetPointerBits() / 8;
                    offset = alignTo(offset, pointerSize) + pointerSize;
                    align = std::max(align, pointerSize);
                }
                offset = appendAsyncDropFields(offset, align, fields, hasDropline);
            }
        }

        if (!hasCustom && !hasAsyncFields) {
            return true;
        }
        if (hasCustom) {
            offset = alignTo(offset, customAlign);
            offset += customSize;
            align = std::max(align, customAlign);
        }
        const size_t outerSize = alignTo(offset, align);
        const size_t storageOffset = alignTo(repr.size, align);
        ASSERT_BUG(sp, storageOffset < outerSize, StringView("async-drop glue has no suspension storage: ") << ty);
        auto storageTy = resolve.hirCrate().types.array(resolve.hirCrate().types.primitive(HIRCoreType::U8), outerSize - storageOffset);
        repr.fields.push_back(TypeRepr::Field{storageOffset, std::move(storageTy)});
        repr.align = align;
        repr.size = outerSize;
        return true;
    }

    size_t structFieldAlignmentGroup(const Ent& e, unsigned maxAlignment) {
        if (maxAlignment > 0) {
            return std::min<size_t>(e.align, maxAlignment);
        }

        const size_t sizeAsAlign = std::max(e.size, e.align);
        return sizeAsAlign & (~sizeAsAlign + 1);
    }

    bool sortfnEnumVariantFields(const Ent& a, const Ent& b) {
        return a.align != b.align ? a.align < b.align : a.size < b.size;
    }

    std::unique_ptr<TypeRepr> makeTypeReprStructInner(const Span& sp, const HIRType* ty, std::vector<Ent>& ents, StructSorting sorting, unsigned forcedAlignment, unsigned maxAlignment) {
        if (ents.size() > 0) {
            auto sortFields = [&](auto first, auto last) {
                std::stable_sort(first, last, [&](const Ent& a, const Ent& b) {
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
        std::vector<TypeRepr::Field> fields(ents.size() > 0 ? maxField + 1 : 0);

        TypeRepr rv;
        size_t curOfs = 0;
        size_t maxAlign = 1;
        bool isFirstField = true;
        for (auto& e : ents) {
            auto align = e.align;

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

            align = maxAlignment > 0 ? std::min<size_t>(align, maxAlignment) : align;
            if (align > 0) {
                while (curOfs % align != 0) {
                    curOfs++;
                }
            }
            maxAlign = std::max(maxAlign, align);

            if (e.field != ~0u) {
                ASSERT_BUG(sp, e.field < fields.size(), StringView("Field index out of range"));
                ASSERT_BUG(sp, fields[e.field].ty == nullptr, StringView("Dupliate field index"));
                fields[e.field].offset = curOfs;
                fields[e.field].ty = e.ty;
            }
            DEBUG(StringView("#") << e.field << StringView(" @") << curOfs << StringView("+") << e.size << StringView(" : ") << e.ty);
            if (e.size == SIZE_MAX) {
                ASSERT_BUG(sp, &e == &ents.back(), StringView("Unsized item isn't the last item in ") << ty);
                curOfs = SIZE_MAX;
            } else {
                curOfs += e.size;
            }
        }
        if (forcedAlignment > 0) {
            maxAlign = std::max(maxAlign, static_cast<size_t>(forcedAlignment));
            rv.userAlign = true;
        }
        if (curOfs != SIZE_MAX) {
            while (curOfs % maxAlign != 0) {
                curOfs++;
            }
        }
        for (const auto& f : fields) {
            ASSERT_BUG(sp, f.ty != nullptr, StringView("Uninitialised field found - ") << (&f - &fields[0]));
        }
        rv.align = maxAlign;
        rv.size = curOfs;
        rv.fields = std::move(fields);
        DEBUG(ty << StringView(": size = ") << rv.size << StringView(", align = ") << rv.align);
        return box$(rv);
    }

    std::unique_ptr<TypeRepr> makeTypeReprStruct(const Span& sp, const StaticTraitResolve& resolve, const HIRType* ty) {
        TRACE_FUNCTION_F(ty);
        std::vector<Ent> ents;
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
            sorting = StructSorting::None;
            switch (str.repr) {
                case HIRStruct::Repr::C:
                case HIRStruct::Repr::Simd:
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
            DEBUG(StringView("Tuple ") << ty);
            unsigned int idx = 0;
            for (const auto& t : *te) {
                Ent ent;
                if (!makeFieldEnt(sp, resolve, idx, t, ent)) {
                    return nullptr;
                }
                idx++;
                ents.push_back(mv$(ent));
            }
            sorting = (!ents.empty() && ents.back().size == SIZE_MAX) ? StructSorting::AllButFinal : StructSorting::All;
        } else {
            BUG(sp, StringView("Unexpected type in creating type repr - ") << ty);
        }

        auto repr = makeTypeReprStructInner(sp, ty, ents, sorting, forcedAlignment, maxAlignment);
        if (ty->is_Path() && ty->as_Path().binding.is_Struct()) {
            const auto& te = ty->as_Path();
            const auto& str = *te.binding.as_Struct();
            if (str.structMarkings.isAsyncDropGlue && !monomorphiseTypeNeeded(ty) && !extendAsyncDropGlueRepr(sp, resolve, ty, *repr)) {
                return nullptr;
            }
        }
        return repr;
    }

    bool boundedMaxIsFullRange(const HIRType* ty, U128 boundedMax) {
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

    bool getPatternValidRanges(const HIRType::Data_Pattern& pattern, size_t& scalarSize, std::vector<std::pair<size_t, size_t>>& ranges) {
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
                if (sizeof(size_t) < 8) {
                    return false;
                }
                scalarSize = 8;
                defaultMax = SIZE_MAX;
                break;
            case HIRCoreType::Usize:
                scalarSize = TargetGetPointerBits() / 8;
                if (scalarSize > sizeof(size_t)) {
                    return false;
                }
                defaultMax = scalarSize == sizeof(size_t) ? SIZE_MAX : (size_t(1) << (scalarSize * 8)) - 1;
                break;
            case HIRCoreType::Char:
                scalarSize = 4;
                defaultMax = 0x10FFFF;
                break;
            default:
                return false;
        }

        ranges.clear();
        ranges.reserve(pattern.pattern.alternatives.size());
        for (const auto& range : pattern.pattern.alternatives) {
            size_t start = 0;
            size_t end = defaultMax;
            if (range.hasStart) {
                const auto* value = range.start.opt_Evaluated();
                if (!value) {
                    return false;
                }
                const auto encoded = EncodedLiteralSlice(**value).readUint();
                if (!encoded.isU64() || encoded.truncateU64() > SIZE_MAX) {
                    return false;
                }
                start = static_cast<size_t>(encoded.truncateU64());
            }
            if (range.hasEnd) {
                const auto* value = range.end.opt_Evaluated();
                if (!value) {
                    return false;
                }
                const auto encoded = EncodedLiteralSlice(**value).readUint();
                if (!encoded.isU64() || encoded.truncateU64() > SIZE_MAX) {
                    return false;
                }
                end = static_cast<size_t>(encoded.truncateU64());
                if (!range.endInclusive) {
                    if (end == 0) {
                        return false;
                    }
                    end--;
                }
            }
            if (start > end || end > defaultMax) {
                return false;
            }
            ranges.push_back({start, end});
        }
        if (ranges.empty()) {
            return false;
        }

        std::sort(ranges.begin(), ranges.end());
        size_t out = 0;
        for (const auto& range : ranges) {
            if (out != 0 && range.first <= ranges[out - 1].second + (ranges[out - 1].second != SIZE_MAX)) {
                ranges[out - 1].second = std::max(ranges[out - 1].second, range.second);
            } else {
                ranges[out++] = range;
            }
        }
        ranges.resize(out);
        return true;
    }

    bool getNonzeroPath(const Span& sp, const StaticTraitResolve& resolve, const HIRType* ty, TypeRepr::FieldPath& outPath) {
        switch (ty->tag()) {
            break;
            case HIRType::TAG_Tuple: {
                const TypeRepr* repr = TargetGetTypeRepr(sp, resolve, ty);
                if (!repr) {
                    return false;
                }
                for (size_t i = 0; i < repr->fields.size(); i++) {
                    if (getNonzeroPath(sp, resolve, repr->fields[i].ty, outPath)) {
                        outPath.subFields.pushBack(i);
                        return true;
                    }
                }
            } break;
                break;
            case HIRType::TAG_Array: {
                auto& te = (*ty).as_Array();
                if (te.size.is_Known() && te.size.as_Known() > 0 && getNonzeroPath(sp, resolve, te.inner, outPath)) {
                    outPath.subFields.pushBack(TypeRepr::FieldPath::ARRAY_ELEMENT);
                    return true;
                }
            } break;
                break;
            case HIRType::TAG_Path: {
                auto& te = (*ty).as_Path();
                if (te.isGenerator() || te.isFuture()) {
                    return false;
                }
                if (te.binding.is_Struct()) {
                    const auto* str = te.binding.as_Struct();
                    const TypeRepr* r = TargetGetTypeRepr(sp, resolve, ty);
                    if (!r) {
                        return false;
                    }
                    if (str->structMarkings.isNoNiche) {
                        return false;
                    }
                    if (str->structMarkings.boundedMax && (r->fields.size() != 1 || !boundedMaxIsFullRange(r->fields[0].ty, str->structMarkings.boundedMaxValue))) {
                        return false;
                    }
                    for (size_t i = 0; i < r->fields.size(); i++) {
                        if (getNonzeroPath(sp, resolve, r->fields[i].ty, outPath)) {
                            outPath.subFields.pushBack(i);
                            return true;
                        }
                    }
                    if (str->structMarkings.isNonzero) {
                        DEBUG(ty << StringView(" tagged NonZero"));
                        outPath.subFields.pushBack(0);
                        outPath.size = r->size;
                        if ((r->fields[0].ty->is_Pointer() || r->fields[0].ty->is_Borrow()) && outPath.size > TargetGetPointerBits() / 8) {
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
                            appendReverse(outPath.subFields, values->field.subFields);
                            outPath.subFields.pushBack(values->field.index);
                            outPath.size = values->field.size;
                            return true;
                        }
                    }
                }
            } break;
                break;
            case HIRType::TAG_Borrow: {
                // TODO: Only return a single-pointer size
                outPath.size = TargetGetPointerBits() / 8;
                return true;
            } break;
                break;
            case HIRType::TAG_Function: {
                auto& _te = (*ty).as_Function();
                (void)_te;
            }
                TargetGetSizeOf(sp, resolve, ty, outPath.size);
                return true;
                break;
            case HIRType::TAG_Pattern: {
                auto& te = (*ty).as_Pattern();
                std::vector<std::pair<size_t, size_t>> ranges;
                if (getPatternValidRanges(te, outPath.size, ranges) && ranges.front().first != 0) {
                    return true;
                }
            } break;
            default:
                break;
        }
        return false;
    }

    size_t getSizeOrZero(const Span& sp, const StaticTraitResolve& resolve, const HIRType* ty) {
        size_t size = 0;
        TargetGetSizeOf(sp, resolve, ty, size);
        return size;
    }

    size_t getOffset(const Span& sp, const StaticTraitResolve& resolve, const TypeRepr* r, const TypeRepr::FieldPath& outPath) {
        BUG_ASSERT(outPath.index < r->fields.size());
        size_t ofs = r->fields[outPath.index].offset;

        const auto* ty = &r->fields[outPath.index].ty;
        for (const auto& f : outPath.subFields) {
            if (f == TypeRepr::FieldPath::ARRAY_ELEMENT) {
                const auto* array = (*ty)->opt_Array();
                BUG_ASSERT(array && array->size.is_Known() && array->size.as_Known() > 0);
                ty = &array->inner;
                continue;
            }
            r = TargetGetTypeRepr(sp, resolve, *ty);
            BUG_ASSERT(f < r->fields.size());
            ofs += r->fields[f].offset;
            ty = &r->fields[f].ty;
        }

        return ofs;
    }

    bool getVariantNichePath(const Span& sp, const StaticTraitResolve& resolve, const HIRType* ty, size_t minOffset, size_t maxOffset, size_t requiredCount, TypeRepr::FieldPath& outPath, size_t& nicheStart) {
        TRACE_FUNCTION_F(ty << StringView(" min_offset=") << minOffset << StringView(" max_offset=") << maxOffset << StringView(" required_count=") << requiredCount);
        switch (ty->tag()) {
            break;
            case HIRType::TAG_Tuple: {
                const TypeRepr* r = TargetGetTypeRepr(sp, resolve, ty);
                if (!r) {
                    return false;
                }

                for (size_t i = 0; i < r->fields.size(); i++) {
                    const auto& f = r->fields[i];
                    auto size = getSizeOrZero(sp, resolve, f.ty);
                    DEBUG(i << StringView(": ") << f.offset << StringView(" + ") << size);
                    if (f.offset >= maxOffset) {
                        continue;
                    } else if (f.offset + size > minOffset) {
                        if (getVariantNichePath(sp, resolve, f.ty, (f.offset < minOffset ? minOffset - f.offset : 0), maxOffset - f.offset, requiredCount, outPath, nicheStart)) {
                            outPath.subFields.pushBack(i);
                            return true;
                        }
                    }
                }
            } break;
            case HIRType::TAG_Array: {
                auto& te = (*ty).as_Array();
                if (te.size.is_Known() && te.size.as_Known() > 0 && getVariantNichePath(sp, resolve, te.inner, minOffset, maxOffset, requiredCount, outPath, nicheStart)) {
                    outPath.subFields.pushBack(TypeRepr::FieldPath::ARRAY_ELEMENT);
                    return true;
                }
            } break;
            case HIRType::TAG_Path: {
                auto& te = (*ty).as_Path();
                if (te.isGenerator() || te.isFuture()) {
                    return false;
                }
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
                        BUG_ASSERT(r->fields.size() >= 1);
                        BUG_ASSERT(r->fields[0].offset == 0);
                        auto size = getSizeOrZero(sp, resolve, r->fields[0].ty);
                        if ((r->fields[0].ty->is_Pointer() || r->fields[0].ty->is_Borrow()) && size > TargetGetPointerBits() / 8) {
                            size = TargetGetPointerBits() / 8;
                        }
                        if (size <= maxOffset) {
                            outPath.subFields.pushBack(0);
                            outPath.size = size;
                            nicheStart = 0;
                            return true;
                        }
                    }

                    if (minOffset == 0 && str->structMarkings.boundedMax) {
                        BUG_ASSERT(r->fields.size() >= 1);
                        BUG_ASSERT(r->fields[0].offset == 0);
                        auto size = getSizeOrZero(sp, resolve, r->fields[0].ty);
                        if (size <= maxOffset && size <= sizeof(size_t)) {
                            const size_t scalarMax = size == sizeof(size_t) ? SIZE_MAX : (size_t(1) << (size * 8)) - 1;
                            const auto boundedMax = str->structMarkings.boundedMaxValue.truncateU64();
                            if (boundedMax < scalarMax && requiredCount <= scalarMax - boundedMax) {
                                outPath.subFields.pushBack(0);
                                outPath.size = size;
                                nicheStart = boundedMax + 1;
                                return true;
                            }
                        }
                    }

                    for (size_t i = 0; i < r->fields.size(); i++) {
                        const auto& f = r->fields[i];
                        auto size = getSizeOrZero(sp, resolve, f.ty);
                        DEBUG(i << StringView(": ") << f.offset << StringView(" + ") << size);
                        if (f.offset >= maxOffset) {
                            continue;
                        } else if (f.offset + size > minOffset) {
                            if (getVariantNichePath(sp, resolve, f.ty, (f.offset < minOffset ? minOffset - f.offset : 0), maxOffset - f.offset, requiredCount, outPath, nicheStart)) {
                                outPath.subFields.pushBack(i);
                                return true;
                            }
                        }
                    }
                } else if (te.binding.is_Enum()) {
                    const TypeRepr* r = TargetGetTypeRepr(sp, resolve, ty);
                    if (!r) {
                        return false;
                    }

                    switch (r->variants.tag()) {
                        case TypeReprVariantMode::TAG_None: {
                            if (r->fields.empty()) {
                                return false;
                            } else {
                                if (getVariantNichePath(sp, resolve, r->fields[0].ty, minOffset, maxOffset, requiredCount, outPath, nicheStart)) {
                                    outPath.subFields.pushBack(0);
                                    return true;
                                }
                                return false;
                            }
                            break;
                        }
                        case TypeReprVariantMode::TAG_Linear: {
                            auto& ve = r->variants.as_Linear();
                            if (ve.usesNiche()) {
                                const auto& field = r->fields.at(ve.field.index);
                                const size_t fieldSize = getSizeOrZero(sp, resolve, field.ty);
                                if (field.offset < maxOffset && field.offset + fieldSize > minOffset) {
                                    const size_t occupiedCount = ve.nicheVariantCount();
                                    if (requiredCount <= SIZE_MAX - occupiedCount) {
                                        TypeRepr::FieldPath candidate;
                                        size_t candidateStart = 0;
                                        if (getVariantNichePath(sp, resolve, field.ty, field.offset < minOffset ? minOffset - field.offset : 0, maxOffset - field.offset, requiredCount + occupiedCount, candidate, candidateStart)) {
                                            auto candidateSubFields = candidate.subFields;
                                            std::reverse(candidateSubFields.mutBegin(), candidateSubFields.mutEnd());
                                            const bool sameScalar = candidate.size == ve.field.size && ::ord(candidateSubFields, ve.field.subFields) == OrdEqual;
                                            if (!sameScalar) {
                                                return false;
                                            }
                                            const size_t candidateEnd = candidateStart + requiredCount + occupiedCount - 1;
                                            const size_t occupiedStart = ve.offset;
                                            const size_t occupiedEnd = occupiedStart + occupiedCount - 1;
                                            if (!(candidateEnd < occupiedStart || occupiedEnd < candidateStart)) {
                                                const size_t beforeCount = occupiedStart > candidateStart ? occupiedStart - candidateStart : 0;
                                                const size_t afterStart = occupiedEnd == SIZE_MAX ? SIZE_MAX : std::max(candidateStart, occupiedEnd + 1);
                                                const size_t afterCount = occupiedEnd == SIZE_MAX || candidateEnd < afterStart ? 0 : candidateEnd - afterStart + 1;
                                                if (requiredCount <= beforeCount) {
                                                } else if (requiredCount <= afterCount) {
                                                    candidateStart = afterStart;
                                                } else {
                                                    return false;
                                                }
                                            }
                                            candidate.subFields.pushBack(ve.field.index);
                                            outPath = std::move(candidate);
                                            nicheStart = candidateStart;
                                            return true;
                                        }
                                    }
                                }
                                return false;
                            }
                            auto ofs = getOffset(sp, resolve, r, ve.field);
                            DEBUG(StringView("Linear - Tag offset: ") << ofs);
                            if (minOffset <= ofs && ofs + ve.field.size <= maxOffset && ve.field.size <= sizeof(size_t)) {
                                const size_t scalarMax = ve.field.size == sizeof(size_t) ? SIZE_MAX : (size_t(1) << (ve.field.size * 8)) - 1;
                                const size_t validEnd = ve.offset + ve.numVariants - 1;
                                if (validEnd >= scalarMax || requiredCount > scalarMax - validEnd) {
                                    return false;
                                }
                                outPath.size = ve.field.size;
                                outPath.subFields.clear();
                                appendReverse(outPath.subFields, ve.field.subFields);
                                outPath.subFields.pushBack(ve.field.index);
                                nicheStart = validEnd + 1;
                                return true;
                            }
                            break;
                        }
                        case TypeReprVariantMode::TAG_Values: {
                            auto& ve = r->variants.as_Values();
                            auto ofs = getOffset(sp, resolve, r, ve.field);
                            DEBUG(StringView("Values - Tag offset: ") << ofs);
                            if (minOffset <= ofs && ofs + ve.field.size <= maxOffset && ve.field.size <= sizeof(size_t) && !ve.values.empty()) {
                                const size_t scalarMax = ve.field.size == sizeof(size_t) ? SIZE_MAX : (size_t(1) << (ve.field.size * 8)) - 1;
                                Vector<size_t> values;
                                values.grow(ve.values.length());
                                for (const auto& value : ve.values) {
                                    values.pushBack(value.truncateU64() & scalarMax);
                                }
                                quickSort(mutRange(values));
                                const auto* uniqueEnd = std::unique(values.mutBegin(), values.mutEnd());
                                while (values.end() != uniqueEnd) {
                                    values.popBack();
                                }

                                size_t bestStart = 0;
                                size_t bestCount = values[0];
                                for (size_t i = 1; i < values.length(); i++) {
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
                                    appendReverse(outPath.subFields, ve.field.subFields);
                                    outPath.subFields.pushBack(ve.field.index);
                                    nicheStart = bestStart;
                                    return true;
                                }
                            }
                            return false;
                        }
                        case TypeReprVariantMode::TAG_NonZero: {
                            DEBUG(StringView("Non-zero enum, can't niche"));
                            return false;
                        }
                    }
                }
            } break;
                break;
            case HIRType::TAG_Primitive: {
                auto& te = (*ty).as_Primitive();
                switch (te) {
                    case HIRCoreType::Char:
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
            } break;
            case HIRType::TAG_Pattern: {
                auto& te = (*ty).as_Pattern();
                size_t scalarSize;
                std::vector<std::pair<size_t, size_t>> ranges;
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
            } break;
            case HIRType::TAG_Borrow: {
                if (minOffset == 0 && maxOffset >= TargetGetPointerBits() / 8 && requiredCount == 1) {
                    outPath.size = TargetGetPointerBits() / 8;
                    nicheStart = 0;
                    return true;
                }
            } break;
            case HIRType::TAG_Function: {
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

    std::unique_ptr<TypeRepr> makeTypeReprEnum(const Span& sp, const StaticTraitResolve& resolve, const HIRType* ty) {
        TRACE_FUNCTION_F(ty);
        const auto& te = ty->as_Path();
        const auto& enm = *te.binding.as_Enum();

        auto monomorphCb = MonomorphStatePtr(resolve.hirCrate().types, ty, &te.path.data.as_Generic().params, nullptr);
        auto monomorph = [&](const auto& tpl) {
            return resolve.monomorphExpand(sp, tpl, monomorphCb);
        };

        if (!enm.discriminantsEvaluated) {
            auto& crate = resolve.hirCrateMut();
            ConvertHIRConstantEvaluateEnum(resolve.board(), crate, te.path.data.as_Generic().path, crate.getEnumByPathMut(sp, te.path.data.as_Generic().path));
            BUG_ASSERT(enm.discriminantsEvaluated);
        }

        TypeRepr rv;
        switch (enm.data.tag()) {
            break;
            case HIREnumClass::TAG_Data: {
                auto& e = enm.data.as_Data();
                if (enm.isCRepr) {
                    size_t maxSize = 0;
                    size_t maxAlign = 0;
                    for (const auto& var : e) {
                        auto t = monomorph(var.type);
                        size_t size, align;
                        if (!TargetGetSizeAndAlignOf(sp, resolve, t, size, align)) {
                            DEBUG(StringView("Generic type in enum - ") << t);
                            return nullptr;
                        }
                        if (size == SIZE_MAX) {
                            BUG(sp, StringView("Unsized type in enum - ") << t);
                        }
                        maxSize = std::max(maxSize, size);
                        maxAlign = std::max(maxAlign, align);
                        rv.fields.push_back(TypeRepr::Field{0, mv$(t)});

                        ASSERT_BUG(sp, !var.discriminantExpr, StringView("TODO: Handle explicit discriminants with repr(C) data"));
                    }

                    DEBUG(StringView("max_size = ") << maxSize << StringView(", max_align = ") << maxAlign);
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
                    if (e.size() == 1) {
                        auto t = monomorph(e[0].type);
                        const auto* innerRepr = TargetGetTypeRepr(sp, resolve, t);
                        if (!innerRepr) {
                            DEBUG(StringView("Generic type in enum - ") << t);
                            return nullptr;
                        }
                        rv.fields.push_back(TypeRepr::Field{0, mv$(t)});
                        rv.size = innerRepr->size;
                        rv.align = innerRepr->align;
                    } else {
                        rv.size = 0;
                        rv.align = 0;
                    }
                } else {
                    struct Variant {
                        const HIRType* type;
                        std::vector<Ent> ents;
                        unsigned forcedAlignment;
                    };

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
                        TRACE_FUNCTION_F(StringView("Variant #") << (&var - e.data()));
                        if (var.type == resolve.hirCrate().types.unit()) {
                            continue;
                        }
                        if (!structEnumerateFields(sp, resolve, variants.back().type, variants.back().ents)) {
                            DEBUG(StringView("Generic type in enum - ") << variants.back().type);
                            return nullptr;
                        }
                        DEBUG(variants.back().type << StringView(": ") << variants.back().ents);
                    }

                    if (enm.tagRepr == HIREnum::Repr::Auto) {
                        ASSERT_BUG(sp, !hasExplcitValue, StringView("Explicit tag without a repr"));
                        if (rv.variants.is_None() && variants.size() == 2) {
                            size_t sizes[2] = {0, 0};
                            for (size_t i = 0; i < 2; i++) {
                                for (const auto& ent : variants[i].ents) {
                                    sizes[i] += ent.size;
                                }
                            }
                            DEBUG(StringView("sizes = {") << sizes[0] << StringView(",") << sizes[1] << StringView("}"));
                            auto minSize = std::min(sizes[0], sizes[1]);
                            auto maxSize = std::max(sizes[0], sizes[1]);
                            if (minSize == 0 && maxSize > 0) {
                                unsigned nzVar = (sizes[0] == 0 ? 1 : 0);
                                DEBUG(StringView("Variant #") << nzVar << StringView(" is populated, checking for NonZero"));
                                for (size_t i = 0; i < variants[nzVar].ents.size(); i++) {
                                    TypeRepr::FieldPath nzPath;
                                    if (getNonzeroPath(sp, resolve, variants[nzVar].ents[i].ty, nzPath)) {
                                        nzPath.subFields.pushBack(i);
                                        nzPath.index = nzVar;
                                        std::reverse(nzPath.subFields.mutBegin(), nzPath.subFields.mutEnd());

                                        DEBUG(StringView("nz_path = ") << nzPath.subFields);
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
                        }

                        if (rv.variants.is_None()) {
                            bool nicheBeforeData = false;
                            size_t nicheOffset = 0;
                            size_t nonNicheOffset = 0;
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
                                if (varSize > maxVarSize) {
                                    minOffset = maxVarSize;
                                    maxVarSize = varSize;
                                    biggestVar = i;
                                    nMatch = 1;
                                } else if (varSize == maxVarSize) {
                                    nMatch += 1;
                                } else {
                                    minOffset = std::max(minOffset, varSize);
                                }
                            }

                            DEBUG(StringView("Niche optimisation: max_var_size=") << maxVarSize << StringView(" n_match=") << nMatch << StringView(" biggest_var=") << biggestVar << StringView(" min_offset=") << minOffset);
                            if (nMatch == 1) {
                                const size_t nicheVariantStart = biggestVar == 0 ? 1 : 0;
                                const size_t nicheVariantEnd = biggestVar + 1 == variants.size() ? biggestVar - 1 : variants.size() - 1;
                                const size_t requiredNicheCount = nicheVariantEnd - nicheVariantStart + 1;
                                for (size_t i = 0; i < reprs[biggestVar]->fields.size(); i++) {
                                    const auto& fld = reprs[biggestVar]->fields[i];

                                    TypeRepr::FieldPath nzPath;
                                    size_t nicheStart = 0;
                                    if (getVariantNichePath(sp, resolve, fld.ty, (minOffset > fld.offset ? minOffset - fld.offset : 0), maxVarSize - fld.offset, requiredNicheCount, nzPath, nicheStart)) {
                                        nzPath.index = i;
                                        std::reverse(nzPath.subFields.mutBegin(), nzPath.subFields.mutEnd());
                                        nicheOffset = getOffset(sp, resolve, &*reprs[biggestVar], nzPath);
                                        std::reverse(nzPath.subFields.mutBegin(), nzPath.subFields.mutEnd());

                                        nzPath.subFields.pushBack(i);
                                        nzPath.index = biggestVar;
                                        std::reverse(nzPath.subFields.mutBegin(), nzPath.subFields.mutEnd());
                                        DEBUG(StringView("Niche optimisation (trailing): value offset=") << nicheStart << StringView(" path=") << nzPath << StringView(" (@") << nicheOffset << StringView(")"));

                                        BUG_ASSERT(rv.variants.is_None());
                                        rv.variants = TypeRepr::VariantMode::make_Linear({std::move(nzPath), nicheStart, e.size()});
                                        break;
                                    }

                                    if (fld.offset == 0) {
                                        TypeRepr::FieldPath nzPath;
                                        size_t nicheStart = 0;
                                        if (getVariantNichePath(sp, resolve, fld.ty, 0, maxVarSize - minOffset, requiredNicheCount, nzPath, nicheStart)) {
                                            nzPath.index = i;
                                            std::reverse(nzPath.subFields.mutBegin(), nzPath.subFields.mutEnd());
                                            nicheOffset = getOffset(sp, resolve, &*reprs[biggestVar], nzPath);
                                            if (nicheOffset != 0) {
                                                DEBUG(StringView("Ignore niche not at the start of the struture"));
                                                continue;
                                            }
                                            std::reverse(nzPath.subFields.mutBegin(), nzPath.subFields.mutEnd());

                                            nzPath.subFields.pushBack(i);
                                            nzPath.index = biggestVar;
                                            std::reverse(nzPath.subFields.mutBegin(), nzPath.subFields.mutEnd());

                                            DEBUG(StringView("Niche optimisation (leading): linear offset=") << nicheStart << StringView(" path=") << nzPath << StringView(" @byte ") << nicheOffset);
                                            nicheBeforeData = true;
                                            nonNicheOffset = nzPath.size;
                                            BUG_ASSERT(rv.variants.is_None());
                                            rv.variants = TypeRepr::VariantMode::make_Linear({std::move(nzPath), nicheStart, e.size()});
                                            break;
                                        }
                                    }
                                }
                            }

                            size_t maxSize = maxVarSize;
                            while (maxSize % maxAlign != 0) {
                                maxSize++;
                            }

                            if (!rv.variants.is_None()) {
                                const auto& nichePath = rv.variants.as_Linear().field;

                                const HIRType* nicheTy;
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
                                        BUG(sp, StringView("Unknown niche size: ") << nichePath);
                                }
                                BUG_ASSERT(reprs.size() == variants.size());
                                size_t finalSize = 0;
                                size_t finalAlign = 1;
                                for (size_t i = 0; i < reprs.size(); i++) {
                                    if (e[i].type != resolve.hirCrate().types.unit()) {
                                        if (i == biggestVar) {
                                        } else if (nicheBeforeData) {
                                            if (nicheOffset > 0) {
                                                variants[i].ents.insert(variants[i].ents.begin(), Ent());
                                                variants[i].ents[0].align = 1;
                                                variants[i].ents[0].size = nicheOffset;
                                                variants[i].ents[0].field = ~0u;
                                                TODO(sp, StringView("Handle adding padding"));
                                            }
                                            variants[i].ents.insert(variants[i].ents.begin(), Ent());
                                            variants[i].ents[0].align = nichePath.size;
                                            variants[i].ents[0].size = nichePath.size;
                                            variants[i].ents[0].field = variants[i].ents.size() - 1;
                                            variants[i].ents[0].ty = nicheTy;
                                            reprs[i] = makeTypeReprStructInner(sp, variants[i].type, variants[i].ents, StructSorting::None, variants[i].forcedAlignment, 0);
                                            BUG_ASSERT(reprs[i]->size <= maxSize);
                                            BUG_ASSERT(reprs[i]->align <= maxAlign);
                                        } else {
                                            auto tagFldIdx = variants[i].ents.size();
                                            size_t maxOfs = 0;
                                            for (const auto& f : reprs[i]->fields) {
                                                maxOfs = std::max(maxOfs, f.offset + getSizeOrZero(sp, resolve, f.ty));
                                            }
                                            if (maxOfs % nichePath.size != 0) {
                                                maxOfs += nichePath.size - (maxOfs % nichePath.size);
                                            }
                                            BUG_ASSERT(nicheOffset % nichePath.size == 0);
                                            BUG_ASSERT(maxOfs % nichePath.size == 0);
                                            ASSERT_BUG(sp, nicheOffset >= maxOfs, StringView("Niche offset (") << nicheOffset << StringView(") overlaps with variant data (") << maxOfs << StringView(")"));
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
                                            reprs[i] = makeTypeReprStructInner(sp, variants[i].type, variants[i].ents, StructSorting::None, variants[i].forcedAlignment, 0);
                                            BUG_ASSERT(reprs[i]->size <= maxSize);
                                            BUG_ASSERT(reprs[i]->align <= maxAlign);
                                        }
                                        finalSize = std::max(finalSize, reprs[i]->size);
                                        finalAlign = std::max(finalAlign, reprs[i]->align);
                                        setTypeRepr(resolve, sp, variants[i].type, std::move(reprs[i]));
                                    } else {
                                        if (const auto* r = TargetGetTypeRepr(sp, resolve, variants[i].type)) {
                                            finalSize = std::max(finalSize, r->size);
                                            finalAlign = std::max(finalAlign, r->align);
                                        }
                                    }
                                    rv.fields.push_back(TypeRepr::Field{0, mv$(variants[i].type)});
                                }

                                rv.size = maxSize;
                                rv.align = maxAlign;

                                if (TargetCapsMemberAlignment() && finalSize > 0) {
                                    size_t sz = finalSize;
                                    while (sz % finalAlign != 0) {
                                        sz++;
                                    }
                                    if (sz != rv.size || finalAlign != rv.align) {
                                        DEBUG(StringView("Capping ABI: ") << ty << StringView(" ") << rv.size << StringView("/") << rv.align << StringView(" -> ") << sz << StringView("/") << finalAlign << StringView(" (union of the final variants)"));
                                        rv.size = sz;
                                        rv.align = finalAlign;
                                    }
                                }

                                auto tagOffset = getOffset(sp, resolve, &rv, nichePath);
                                if (nonNicheOffset != 0) {
                                    ASSERT_BUG(sp, tagOffset < nonNicheOffset, StringView("Niche offset invalid: ") << tagOffset << StringView(" >= ") << nonNicheOffset);
                                } else {
                                    ASSERT_BUG(sp, tagOffset >= minOffset, StringView("Niche offset invalid: ") << tagOffset << StringView(" < ") << minOffset);
                                }
                            }
                        }
                    }

                    if (rv.variants.is_None()) {
                        const HIRType* tagTy;
                        if (enm.tagRepr != HIREnum::Repr::Auto) {
                            tagTy = resolve.hirCrate().types.primitive(enm.getReprType(enm.tagRepr));
                        } else {
                            ASSERT_BUG(sp, !hasExplcitValue, StringView("Explicit tag without a repr"));
                            if (e.size() <= 1) {
                                BUG(sp, StringView("Reached auto tag type logic with zero/one-sized enum"));
                            } else if (e.size() <= 255) {
                                tagTy = resolve.hirCrate().types.primitive(HIRCoreType::U8);
                                DEBUG(StringView("u8 data tag"));
                            } else if (e.size() <= UINT16_MAX) {
                                tagTy = resolve.hirCrate().types.primitive(HIRCoreType::U16);
                            } else {
                                ASSERT_BUG(sp, e.size() <= UINT32_MAX, StringView(""));
                                tagTy = resolve.hirCrate().types.primitive(HIRCoreType::U32);
                            }
                        }

                        size_t tagSize;
                        size_t tagAlign;
                        TargetGetSizeAndAlignOf(sp, resolve, tagTy, tagSize, tagAlign);
                        size_t maxSize = tagSize;
                        size_t maxAlign = tagAlign;
                        for (size_t varI = 0; varI < variants.size(); varI++) {
                            auto& ents = variants[varI].ents;
                            auto& varTy = variants[varI].type;
                            if (e[varI].type != resolve.hirCrate().types.unit()) {
                                if (enm.tagRepr == HIREnum::Repr::Auto) {
                                    std::sort(ents.begin(), ents.end(), sortfnEnumVariantFields);
                                }
                                ents.insert(ents.begin(), Ent());
                                ents[0].align = tagAlign;
                                ents[0].size = tagSize;
                                ents[0].field = ents.size() - 1;
                                ents[0].ty = tagTy;

                                auto repr = makeTypeReprStructInner(sp, varTy, ents, StructSorting::None, variants[varI].forcedAlignment, 0);
                                maxSize = std::max(maxSize, repr->size);
                                maxAlign = std::max(maxAlign, repr->align);
                                setTypeRepr(resolve, sp, varTy, std::move(repr));
                            }

                            rv.fields.push_back(TypeRepr::Field{0, mv$(varTy)});
                        }
                        rv.fields.push_back(TypeRepr::Field{0, mv$(tagTy)});

                        rv.size = maxSize;
                        while (rv.size % maxAlign != 0) {
                            rv.size++;
                        }
                        rv.align = maxAlign;

                        if (hasExplcitValue) {
                            Vector<U128> vals;
                            for (const auto& v : e) {
                                vals.pushBack(v.discriminantValue);
                            }
                            DEBUG(StringView("vals = ") << vals);
                            rv.variants = TypeRepr::VariantMode::make_Values({{e.size(), tagSize, {}}, std::move(vals)});
                        } else {
                            rv.variants = TypeRepr::VariantMode::make_Linear({{e.size(), tagSize, {}}, 0, e.size()});
                        }
                    }
                }
            } break;
                break;
            case HIREnumClass::TAG_Value: {
                auto& e = enm.data.as_Value();
                // TODO: If the values aren't yet populated, force const evaluation
                switch (enm.tagRepr) {
                    case HIREnum::Repr::Auto:
                        if (enm.isCRepr) {
                            rv.fields.push_back(TypeRepr::Field{0, resolve.hirCrate().types.primitive(HIRCoreType::U32)});
                        } else if (e.variants.size() == 1) {
                        } else if (!e.variants.empty()) {
                            i64 minValue = INT64_MAX;
                            i64 maxValue = INT64_MIN;
                            for (const auto& variant : e.variants) {
                                const auto value = S128(variant.val).truncateI64();
                                minValue = std::min(minValue, value);
                                maxValue = std::max(maxValue, value);
                            }

                            HIRCoreType tagType;
                            if (minValue >= 0) {
                                const auto maxUnsigned = static_cast<u64>(maxValue);
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
                    TargetGetSizeAndAlignOf(sp, resolve, rv.fields.back().ty, rv.size, rv.align);

                    Vector<U128> vals;
                    for (const auto& v : e.variants) {
                        vals.pushBack(v.val);
                    }
                    DEBUG(StringView("vals = ") << vals);
                    rv.variants = TypeRepr::VariantMode::make_Values({{0, static_cast<u8>(rv.size), {}}, std::move(vals)});
                }
            } break;
        }

        if (enm.forcedAlignment > 0 && enm.data.is_Value()) {
            rv.align = std::max(rv.align, static_cast<size_t>(enm.forcedAlignment));
            while (rv.size % rv.align != 0) {
                rv.size++;
            }
            rv.userAlign = true;
        }

        switch (rv.variants.tag()) {
            case TypeReprVariantMode::TAG_None: {
                DEBUG(StringView("rv.variants = None"));
                break;
            }
            case TypeReprVariantMode::TAG_Linear: {
                auto& e = rv.variants.as_Linear();
                DEBUG(StringView("rv.variants = Linear {") << StringView(" field=") << e.field << StringView(" value ") << e.offset << StringView("+") << e.numVariants << StringView(" }"));
                break;
            }
            case TypeReprVariantMode::TAG_Values: {
                auto& e = rv.variants.as_Values();
                DEBUG(StringView("rv.variants = Values {") << StringView(" field=") << e.field << StringView(" values ") << e.values << StringView(" }"));
                break;
            }
            case TypeReprVariantMode::TAG_NonZero: {
                auto& e = rv.variants.as_NonZero();
                DEBUG(StringView("rv.variants = NonZero {") << StringView(" field=") << e.field << StringView(" zero_variant=") << e.zeroVariant << StringView(" }"));
                break;
            }
        }

        for (const auto& f : rv.fields) {
            if (TargetTypeHasUserAlignment(sp, resolve, f.ty)) {
                rv.userAlign = true;
                break;
            }
        }
        return box$(rv);
    }

    std::unique_ptr<TypeRepr> makeTypeReprUnion(const Span& sp, const StaticTraitResolve& resolve, const HIRType* ty) {
        const auto& te = ty->as_Path();
        const auto& unn = *te.binding.as_Union();

        auto monomorphCb = MonomorphStatePtr(resolve.hirCrate().types, ty, &te.path.data.as_Generic().params, nullptr);
        auto monomorph = [&](const auto& tpl) {
            return resolve.monomorphExpand(sp, tpl, monomorphCb);
        };

        TypeRepr rv;
        rv.userAlign = true;
        for (const auto& var : unn.variants) {
            rv.fields.push_back({0, monomorph(var.ty)});
            size_t size, align;
            if (!TargetGetSizeAndAlignOf(sp, resolve, rv.fields.back().ty, size, align)) {
                DEBUG(StringView("Generic type encounterd after monomorphise in union - ") << rv.fields.back().ty);
                return nullptr;
            }
            if (size == SIZE_MAX) {
                BUG(sp, StringView("Unsized type in union"));
            }
            rv.size = std::max(rv.size, size);
            rv.align = std::max(rv.align, align);
            if (TargetTypeHasUserAlignment(sp, resolve, rv.fields.back().ty)) {
                rv.userAlign = true;
            }
        }
        if (unn.maxFieldAlignment > 0) {
            rv.align = std::min(rv.align, static_cast<size_t>(unn.maxFieldAlignment));
        }
        if (unn.forcedAlignment > 0) {
            rv.align = std::max(rv.align, static_cast<size_t>(unn.forcedAlignment));
        }
        if (rv.size % rv.align != 0) {
            rv.size += rv.align - rv.size % rv.align;
        }
        return box$(rv);
    }

    std::unique_ptr<TypeRepr> make_type_repr_(const Span& sp, const StaticTraitResolve& resolve, const HIRType* ty) {
        switch (ty->tag()) {
            case HIRType::TAG_Tuple:
                return makeTypeReprStruct(sp, resolve, ty);
            case HIRType::TAG_Path:
                switch (ty->as_Path().binding.tag()) {
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
                        BUG(sp, StringView("Encountered invalid type in make_type_repr - ") << ty);
                }
                UNREACHABLE();
            case HIRType::TAG_NodeType:
                if (const auto* closure = ty->as_NodeType().opt_Closure(); closure && closureHasNoCaptures(resolve, **closure)) {
                    auto repr = box$(TypeRepr());
                    repr->align = 1;
                    return repr;
                }
                TODO(sp, StringView("Type repr for ") << ty);
            // TODO: Why is `make_type_repr` being called on these?
            case HIRType::TAG_Primitive:
            case HIRType::TAG_Borrow:
            case HIRType::TAG_Pointer:
            case HIRType::TAG_Pattern:
                return nullptr;
            default:
                TODO(sp, StringView("Type repr for ") << ty);
        }
    }

    std::unique_ptr<TypeRepr> makeTypeRepr(const Span& sp, const StaticTraitResolve& resolve, const HIRType* ty) {
        std::unique_ptr<TypeRepr> rv;
        TRACE_FUNCTION_FR(ty, ty << StringView(" ") << FMT_CB(ss, if (rv) { ss << StringView("size=") << rv->size << StringView(", align=") << rv->align; } else { ss << StringView("NONE"); }));
        rv = make_type_repr_(sp, resolve, ty);
        return rv;
    }

    bool hasAbiIdentity(const HIRType* ty) {
        return !monomorphiseTypeNeeded(ty) && !ty->is_Infer() && !ty->is_ErasedType() && !ty->is_NodeType();
    }

    bool TransmuteTypeChecker::check(const HIRType* sourceType, const HIRType* destinationType) {
        const auto key = std::make_pair(sourceType, destinationType);
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

struct WireBoard::TargetLayoutContext {
    struct CachedTypeRepr {
        const HIRType* canonical;
        std::unique_ptr<TypeRepr> repr;
    };

    std::unordered_map<std::string, CachedTypeRepr> encoded;
    std::unordered_map<const HIRType*, std::unique_ptr<TypeRepr>> unencoded;
    std::unordered_map<const HIRType*, const TypeRepr*> exact;
};

static void setTypeRepr(const StaticTraitResolve& resolve, const Span& sp, const HIRType* ty, std::unique_ptr<TypeRepr> repr) {
    auto& cache = *resolve.board().targetLayouts;
    if (!hasAbiIdentity(ty)) {
        const auto* reprPtr = repr.get();
        auto ires = cache.unencoded.emplace(ty, mv$(repr));
        ASSERT_BUG(sp, ires.second, StringView("set_type_repr called for type that already has a repr: ") << ty);
        cache.exact.emplace(ty, reprPtr);
        DEBUG(StringView("Set temporary repr for ") << ty);
        return;
    }
    auto symbol = FMT(TransMangle(resolve.board(), ty));
    auto ires = cache.encoded.emplace(mv$(symbol), TargetLayoutContext::CachedTypeRepr{ty, mv$(repr)});
    ASSERT_BUG(sp, ires.second, StringView("set_type_repr called for type that already has a repr: ") << ty);
    cache.exact.emplace(ty, ires.first->second.repr.get());
    DEBUG(StringView("Set repr for ") << ty);
}

bool TargetGetSizeAndAlignOf(const Span& sp, const StaticTraitResolve& resolve, const HIRType* ty, size_t& outSize, size_t& outAlign);

void TargetCreateLayoutContext(WireBoard& wb, ObjPool& pool) {
    wb.targetLayouts = pool.make<TargetLayoutContext>();
}

const TargetSpec& TargetGetCurSpec(const WireBoard& wb) {
    return *wb.target;
}

void TargetExportCurSpec(const WireBoard& wb, const std::string& filename) {
    saveSpecToFile(*wb.pool, filename, *wb.target);
}

void TargetSetCfg(WireBoard& wb, const std::string& targetName) {
    auto& settings = *wb.settings;
    auto* spec = wb.pool->make<TargetSpec>(initFromSpecName(targetName));
    wb.target = spec;
    if (spec->arch.pointerBits != 64 || spec->arch.bigEndian) {
        sysE << StringView("error: unsupported target `") << targetName << StringView("`: only 64-bit little-endian targets are supported") << endL;
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

    CfgSetValue(settings, "target_vendor", "");
    CfgSetValue(settings, "target_env", tgt.envName);
    CfgSetValue(settings, "target_os", tgt.osName);
    CfgSetValue(settings, "target_pointer_width", FMT(tgt.arch.pointerBits));
    CfgSetValue(settings, "target_endian", tgt.arch.bigEndian ? "big" : "little");
    CfgSetValue(settings, "target_arch", tgt.arch.name);
    CfgSetValue(settings, "target_abi", "llvm");
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
    CfgSetValueCb(settings, "target_feature", [](const std::string& s) {
        return false;
    });
}

bool TargetGetSizeAndAlignOf(const Span& sp, const StaticTraitResolve& resolve, const HIRType* ty, size_t& outSize, size_t& outAlign) {
    switch ((*ty).tag()) {
        case HIRType::TAG_Infer: {
            return false;
        }
        case HIRType::TAG_Diverge: {
            outSize = 0;
            outAlign = 1;
            return true;
        }
        case HIRType::TAG_Primitive: {
            auto& te = (*ty).as_Primitive();
            switch (te) {
                case HIRCoreType::Bool:
                case HIRCoreType::U8:
                case HIRCoreType::I8:
                    outSize = 1;
                    outAlign = 1;
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
                    outAlign = 2;
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
                    DEBUG(StringView("sizeof on a `str` - unsized"));
                    outSize = SIZE_MAX;
                    outAlign = 1;
                    return true;
            }
            break;
        }
        case HIRType::TAG_Path: {
            auto& te = (*ty).as_Path();
            if (te.binding.is_Opaque()) {
                return false;
            }
            if (te.binding.is_ExternType()) {
                DEBUG(StringView("sizeof on extern type - unsized"));
                outAlign = 0;
                outSize = SIZE_MAX;
                return true;
            }
            const auto* repr = TargetGetTypeRepr(sp, resolve, ty);
            if (!repr) {
                DEBUG(StringView("Cannot get type repr for ") << ty);
                return false;
            }
            outSize = repr->size;
            outAlign = repr->align;
            return true;
        }
        case HIRType::TAG_Generic: {
            DEBUG(StringView("No repr for Generic - ") << ty);
            return false;
        }
        case HIRType::TAG_TraitObject: {
            outAlign = 0;
            outSize = SIZE_MAX;
            DEBUG(StringView("sizeof on a trait object - unsized"));
            return true;
        }
        case HIRType::TAG_ErasedType: {
            BUG(sp, StringView("sizeof on an erased type - shouldn't exist"));
            break;
        }
        case HIRType::TAG_Array: {
            auto& te = (*ty).as_Array();
            if (!TargetGetSizeAndAlignOf(sp, resolve, te.inner, outSize, outAlign)) {
                return false;
            }
            if (outSize == SIZE_MAX) {
                return false;
            }
            if (!te.size.is_Known()) {
                DEBUG(StringView("Size unknown - ") << ty);
                return false;
            }
            if (te.size.as_Known() == 0 || outSize == 0) {
                outSize = 0;
            } else {
                if (SIZE_MAX / te.size.as_Known() <= outSize) {
                    BUG(sp, StringView("Integer overflow calculating array size"));
                }
                outSize *= te.size.as_Known();
            }
            return true;
        }
        case HIRType::TAG_Slice: {
            auto& te = (*ty).as_Slice();
            if (!TargetGetAlignOf(sp, resolve, te.inner, outAlign)) {
                return false;
            }
            outSize = SIZE_MAX;
            DEBUG(StringView("sizeof on a slice - unsized"));
            return true;
        }
        case HIRType::TAG_Tuple: {
            const auto* repr = TargetGetTypeRepr(sp, resolve, ty);
            if (!repr) {
                DEBUG(StringView("Cannot get type repr for ") << ty);
                return false;
            }
            outSize = repr->size;
            outAlign = repr->align;
            return true;
        }
        case HIRType::TAG_Borrow: {
            auto& te = (*ty).as_Borrow();
            outAlign = TargetGetCurSpec(resolve.board()).arch.pointerBits / 8;

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
        case HIRType::TAG_Pointer: {
            auto& te = (*ty).as_Pointer();
            outAlign = TargetGetCurSpec(resolve.board()).arch.pointerBits / 8;
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
        case HIRType::TAG_NamedFunction: {
            outSize = 0;
            outAlign = 1;
            return true;
        }
        case HIRType::TAG_Function: {
            outSize = TargetGetCurSpec(resolve.board()).arch.pointerBits / 8;
            outAlign = TargetGetCurSpec(resolve.board()).arch.pointerBits / 8;
            return true;
        }
        case HIRType::TAG_NodeType: {
            auto& te = (*ty).as_NodeType();
            if (const auto* closure = te.opt_Closure(); closure && closureHasNoCaptures(resolve, **closure)) {
                outSize = 0;
                outAlign = 1;
                return true;
            }
            return false;
        }
        case HIRType::TAG_Pattern: {
            auto& te = (*ty).as_Pattern();
            return TargetGetSizeAndAlignOf(sp, resolve, te.inner, outSize, outAlign);
        }
    }
    return false;
}

bool TargetGetSizeOf(const Span& sp, const StaticTraitResolve& resolve, const HIRType* ty, size_t& outSize) {
    size_t ignoreAlign;
    bool rv = TargetGetSizeAndAlignOf(sp, resolve, ty, outSize, ignoreAlign);
    if (rv && outSize == SIZE_MAX) {
        BUG(sp, StringView("Getting size of Unsized type - ") << ty);
    }
    return rv;
}

bool TargetGetAlignOf(const Span& sp, const StaticTraitResolve& resolve, const HIRType* ty, size_t& outAlign) {
    size_t ignoreSize;
    bool rv = TargetGetSizeAndAlignOf(sp, resolve, ty, ignoreSize, outAlign);
    if (rv && ignoreSize == SIZE_MAX) {
        BUG(sp, StringView("Getting alignment of Unsized type - ") << ty);
    }
    return rv;
}

bool TargetCapsMemberAlignment() {
    return false;
}

bool TargetTypeHasUserAlignment(const Span& sp, const StaticTraitResolve& resolve, const HIRType* ty) {
    if (const auto* te = ty->opt_Array()) {
        return TargetTypeHasUserAlignment(sp, resolve, te->inner);
    }
    if (const auto* te = ty->opt_Slice()) {
        return TargetTypeHasUserAlignment(sp, resolve, te->inner);
    }
    if (const auto* te = ty->opt_Pattern()) {
        return TargetTypeHasUserAlignment(sp, resolve, te->inner);
    }
    if (ty->is_Tuple() || (ty->is_Path() && (ty->as_Path().binding.is_Struct() || ty->as_Path().binding.is_Union() || ty->as_Path().binding.is_Enum()))) {
        const auto* repr = TargetGetTypeRepr(sp, resolve, ty);
        return repr && repr->userAlign;
    }
    return false;
}

const TypeRepr* TargetGetTypeRepr(const Span& sp, const StaticTraitResolve& resolve, const HIRType* ty) {
    auto& cache = *resolve.board().targetLayouts;
    auto exact = cache.exact.find(ty);
    if (exact != cache.exact.end()) {
        return exact->second;
    }

    if (visitTyWith(ty, [](const HIRType* inner) {
        return inner->is_ErasedType();
    })) {
        const HIRType* revealed = ty;
        revealed = resolve.revealOpaqueTypes(sp, revealed);
        if (revealed != ty) {
            const auto* repr = TargetGetTypeRepr(sp, resolve, revealed);
            cache.exact.emplace(ty, repr);
            return repr;
        }
    }

    if (!hasAbiIdentity(ty)) {
        auto repr = makeTypeRepr(sp, resolve, ty);
        const auto* rv = repr.get();
        auto ires = cache.unencoded.emplace(ty, mv$(repr));
        ASSERT_BUG(sp, ires.second, StringView("Type representation was created recursively for ") << ty);
        cache.exact.emplace(ty, rv);
        DEBUG(StringView("Created temporary repr for ") << ty);
        return rv;
    }

    auto symbol = FMT(TransMangle(resolve.board(), ty));
    auto existing = cache.encoded.find(symbol);
    if (existing != cache.encoded.end()) {
        ASSERT_BUG(sp, existing->second.canonical == ty || existing->second.canonical->equalsIgnoringRegions(ty), StringView("Distinct types have the same mangled name: ") << existing->second.canonical << StringView(" and ") << ty);
        const auto* repr = existing->second.repr.get();
        cache.exact.emplace(ty, repr);
        return repr;
    }

    auto repr = makeTypeRepr(sp, resolve, ty);
    const auto* rv = repr.get();
    auto ires = cache.encoded.emplace(mv$(symbol), TargetLayoutContext::CachedTypeRepr{ty, mv$(repr)});
    ASSERT_BUG(sp, ires.second, StringView("Type representation was created recursively for ") << ty);
    cache.exact.emplace(ty, rv);
    DEBUG(StringView("Created repr for ") << ty);
    return rv;
}

const HIRType* TargetGetInnerType(const Span& sp, const StaticTraitResolve& resolve, const TypeRepr& repr, size_t idx, const Vector<size_t>& subFields, size_t ofs) {
    const auto* ty = &repr.fields.at(idx).ty;
    while (ofs < subFields.length()) {
        const auto field = subFields[ofs++];
        if (field == TypeRepr::FieldPath::ARRAY_ELEMENT) {
            const auto* array = (*ty)->opt_Array();
            ASSERT_BUG(sp, array && array->size.is_Known() && array->size.as_Known() > 0, StringView("Array field path on non-array ") << *ty);
            ty = &array->inner;
        } else {
            const auto* innerRepr = TargetGetTypeRepr(sp, resolve, *ty);
            ASSERT_BUG(sp, innerRepr, StringView("No inner repr for ") << *ty);
            ty = &innerRepr->fields.at(field).ty;
        }
    }
    return *ty;
}

size_t TypeRepr::getOffset(const Span& sp, const StaticTraitResolve& resolve, const TypeRepr::FieldPath& path) const {
    const auto* r = this;
    BUG_ASSERT(path.index < r->fields.size());
    size_t ofs = r->fields[path.index].offset;

    const auto* ty = &r->fields[path.index].ty;
    for (const auto& f : path.subFields) {
        if (f == TypeRepr::FieldPath::ARRAY_ELEMENT) {
            const auto* array = (*ty)->opt_Array();
            BUG_ASSERT(array && array->size.is_Known() && array->size.as_Known() > 0);
            ty = &array->inner;
            continue;
        }
        r = TargetGetTypeRepr(sp, resolve, *ty);
        BUG_ASSERT(r);
        BUG_ASSERT(f < r->fields.size());
        ofs += r->fields[f].offset;
        ty = &r->fields[f].ty;
    }

    return ofs;
}

size_t TypeRepr::VariantMode::Data_Linear::nicheVariantStart() const {
    BUG_ASSERT(this->usesNiche());
    return this->field.index == 0 ? 1 : 0;
}

size_t TypeRepr::VariantMode::Data_Linear::nicheVariantCount() const {
    BUG_ASSERT(this->usesNiche());
    const size_t start = this->nicheVariantStart();
    const size_t end = this->field.index + 1 == this->numVariants ? this->field.index - 1 : this->numVariants - 1;
    return end - start + 1;
}

size_t TypeRepr::VariantMode::Data_Linear::tagValue(unsigned varIdx) const {
    if (!this->usesNiche()) {
        return this->offset + varIdx;
    }
    BUG_ASSERT(varIdx < this->numVariants);
    BUG_ASSERT(varIdx != this->field.index);
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
    switch (this->variants.tag()) {
        case TypeReprVariantMode::TAG_None: {
            break;
        }
        case TypeReprVariantMode::TAG_Linear: {
            auto& ve = this->variants.as_Linear();
            auto v = lit.slice(this->getOffset(sp, resolve, ve.field), ve.field.size).readUint(ve.field.size);
            varIdx = ve.decodeTag(v);
            if (ve.isNiche(varIdx)) {
                subHasTag = false;
                DEBUG(StringView("VariantMode::Linear - Niche #") << varIdx);
            } else {
                subHasTag = true;
                DEBUG(StringView("VariantMode::Linear - Other #") << varIdx);
            }
            break;
        }
        case TypeReprVariantMode::TAG_Values: {
            auto& ve = this->variants.as_Values();
            auto v = lit.slice(this->getOffset(sp, resolve, ve.field), ve.field.size).readUint(ve.field.size);
            const U128 mask = ve.field.size >= 16 ? U128::max() : (U128(1) << static_cast<unsigned>(ve.field.size * 8)) - U128(1);
            auto it = std::find_if(ve.values.begin(), ve.values.end(), [&](const U128& candidate) {
                return (candidate & mask) == v;
            });
            ASSERT_BUG(sp, it != ve.values.end(), StringView("Invalid enum tag: ") << v);
            varIdx = it - ve.values.begin();
            DEBUG(StringView("VariantMode::Values - #") << varIdx);
            break;
        }
        case TypeReprVariantMode::TAG_NonZero: {
            auto& ve = this->variants.as_NonZero();
            size_t ofs = this->getOffset(sp, resolve, ve.field);
            bool isNonzero = false;
            for (size_t i = 0; i < ve.field.size; i++) {
                if (lit.slice(ofs + i, 1).readUint(1) != 0) {
                    isNonzero = true;
                    break;
                }
            }

            varIdx = (isNonzero ? 1 - ve.zeroVariant : ve.zeroVariant);
            DEBUG(StringView("VariantMode::NonZero - #") << varIdx);
            break;
        }
    }
    return std::make_pair(varIdx, subHasTag);
}

bool TargetTypesAreTransmutable(const Span& sp, const StaticTraitResolve& resolve, const HIRType* src, const HIRType* dst, bool assumeAlignment, bool assumeLifetimes, bool assumeSafety, bool assumeValidity) {
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

TargetArch::Alignments::Alignments(u8 u16, u8 u32, u8 u64, u8 u128, u8 f32, u8 f64, u8 ptr)
    : u16(u16)
    , u32(u32)
    , u64(u64)
    , u128(u128)
    , f32(f32)
    , f64(f64)
    , ptr(ptr)
{
}

auto AsyncDropFieldLayout::empty() const -> bool {
    return futures.empty();
}

auto TransmuteNfa::addState() -> unsigned {
    states.push_back({});
    return states.size() - 1;
}

auto TransmuteNfa::empty() -> Fragment {
    auto state = addState();
    return {state, state};
}

auto TransmuteNfa::uninhabited() -> Fragment {
    return {addState(), addState()};
}

auto TransmuteNfa::byte(const TransmuteByteSet& values) -> Fragment {
    auto start = addState();
    auto accept = addState();
    states[start].bytes.pushBack({values, accept});
    return {start, accept};
}

auto TransmuteNfa::reference(TransmuteReference reference) -> Fragment {
    auto start = addState();
    auto accept = addState();
    states[start].references.pushBack({reference, accept});
    return {start, accept};
}

auto TransmuteNfa::then(Fragment left, Fragment right) -> Fragment {
    states[left.accept].epsilon.pushBack(right.start);
    return {left.start, right.accept};
}

auto TransmuteNfa::alternative(Vector<Fragment> alternatives) -> Fragment {
    if (alternatives.empty()) {
        return uninhabited();
    }
    if (alternatives.length() == 1) {
        return alternatives[0];
    }
    auto start = addState();
    auto accept = addState();
    for (const auto& alternative : alternatives) {
        states[start].epsilon.pushBack(alternative.start);
        states[alternative.accept].epsilon.pushBack(accept);
    }
    return {start, accept};
}

auto TransmuteDfa::inhabited() const -> bool {
    return std::find(accepting.begin(), accepting.end(), true) != accepting.end();
}

auto TransmuteLayoutBuilder::byteRange(unsigned first, unsigned last) -> TransmuteByteSet {
    TransmuteByteSet rv;
    for (unsigned value = first; value <= last; value++) {
        rv.set(value);
    }
    return rv;
}

auto TransmuteLayoutBuilder::bytes(size_t count, const TransmuteByteSet& values) -> Built {
    auto rv = nfa.empty();
    for (size_t i = 0; i < count; i++) {
        rv = nfa.then(rv, nfa.byte(values));
    }
    return {rv, count};
}

auto TransmuteLayoutBuilder::padding(size_t count) -> Built {
    TransmuteByteSet values;
    values.set();
    return bytes(count, values);
}

auto TransmuteLayoutBuilder::number(size_t count) -> Built {
    return bytes(count, byteRange(0, 255));
}

auto TransmuteLayoutBuilder::exact(U128 value, size_t count) -> Built {
    if (count > 16) {
        supported = false;
        return {nfa.uninhabited(), count};
    }
    u8 raw[16] = {};
    value.toLeBytes(raw, count);
    auto rv = nfa.empty();
    for (size_t i = 0; i < count; i++) {
        TransmuteByteSet values;
        values.set(raw[i]);
        rv = nfa.then(rv, nfa.byte(values));
    }
    return {rv, count};
}

auto TransmuteLayoutBuilder::character() -> Built {
    const auto any = byteRange(0, 255);
    const auto zero = byteRange(0, 0);
    auto make = [&](const std::array<TransmuteByteSet, 4>& values) {
        auto rv = nfa.empty();
        for (const auto& value : values) {
            rv = nfa.then(rv, nfa.byte(value));
        }
        return rv;
    };
    Vector<TransmuteNfa::Fragment> alternatives;
    alternatives.pushBack(make({any, byteRange(0x00, 0xD7), zero, zero}));
    alternatives.pushBack(make({any, byteRange(0xE0, 0xFF), zero, zero}));
    alternatives.pushBack(make({any, any, byteRange(0x01, 0x10), zero}));
    return {nfa.alternative(std::move(alternatives)), 4};
}

auto TransmuteLayoutBuilder::combine(std::vector<Segment> segments, size_t totalSize) -> Built {
    std::stable_sort(segments.begin(), segments.end(), [](const Segment& left, const Segment& right) {
        return left.offset < right.offset;
    });

    auto rv = nfa.empty();
    size_t offset = 0;
    for (const auto& segment : segments) {
        if (segment.offset > totalSize || segment.value.size > totalSize - segment.offset || (segment.value.size != 0 && segment.offset < offset)) {
            supported = false;
            return {nfa.uninhabited(), totalSize};
        }
        if (segment.offset > offset) {
            rv = nfa.then(rv, padding(segment.offset - offset).fragment);
        }
        rv = nfa.then(rv, segment.value.fragment);
        offset = std::max(offset, segment.offset + segment.value.size);
    }
    rv = nfa.then(rv, padding(totalSize - offset).fragment);
    return {rv, totalSize};
}

auto TransmuteLayoutBuilder::aggregate(const TypeRepr& repr, int skipField) -> Built {
    std::vector<Segment> fields;
    for (size_t i = 0; i < repr.fields.size(); i++) {
        if (static_cast<int>(i) == skipField) {
            continue;
        }
        auto field = build(repr.fields[i].ty);
        fields.push_back({repr.fields[i].offset, field});
    }
    return combine(std::move(fields), repr.size);
}

auto TransmuteLayoutBuilder::addVariantPayload(std::vector<Segment>& segments, const TypeRepr& outerRepr, unsigned variant, bool skipSyntheticTag, size_t tagOffset, size_t tagSize) -> void {
    ASSERT_BUG(sp, variant < outerRepr.fields.size(), StringView("Enum variant field is missing"));
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

auto TransmuteLayoutBuilder::taggedVariant(const TypeRepr& repr, unsigned variant, size_t tagOffset, size_t tagSize, U128 tag, bool skipSyntheticTag) -> Built {
    std::vector<Segment> segments;
    addVariantPayload(segments, repr, variant, skipSyntheticTag, tagOffset, tagSize);
    segments.push_back({tagOffset, exact(tag, tagSize)});
    return combine(std::move(segments), repr.size);
}

auto TransmuteLayoutBuilder::enumLayout(const HIRType* ty, const TypeRepr& repr, const HIREnum& enm) -> Built {
    if (enm.numVariants() == 0) {
        return {nfa.uninhabited(), repr.size};
    }

    if (repr.variants.is_None()) {
        if (repr.fields.empty()) {
            return padding(repr.size);
        }
        return combine({Segment{repr.fields[0].offset, build(repr.fields[0].ty)}}, repr.size);
    }

    Vector<TransmuteNfa::Fragment> alternatives;
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
            alternatives.pushBack(value.fragment);
        }
    } else if (const auto* values = repr.variants.opt_Values()) {
        const auto tagOffset = repr.getOffset(sp, resolve, values->field);
        for (unsigned variant = 0; variant < values->values.length(); variant++) {
            auto value = enm.data.is_Value() ? combine({Segment{tagOffset, exact(values->values[variant], values->field.size)}}, repr.size) : taggedVariant(repr, variant, tagOffset, values->field.size, values->values[variant], true);
            alternatives.pushBack(value.fragment);
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
            alternatives.pushBack(value.fragment);
        }
    } else {
        BUG(sp, StringView("Unhandled enum representation for ") << ty);
    }
    return {nfa.alternative(std::move(alternatives)), repr.size};
}

auto TransmuteLayoutBuilder::build(const HIRType* ty) -> Built {
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
        for (u64 i = 0; i < array->size.as_Known(); i++) {
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
        if (!TargetGetSizeAndAlignOf(sp, resolve, borrow->inner, referentSize, referentAlign) || !TargetGetSizeOf(sp, resolve, ty, referenceSize)) {
            supported = false;
            return {nfa.uninhabited(), 0};
        }
        return {nfa.reference({borrow->type == HIRBorrowType::Unique, borrow->inner, referentSize, referentAlign}), referenceSize};
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
            if (str.structMarkings.isNonzero || str.structMarkings.boundedMax) {
                supported = false;
                return {nfa.uninhabited(), repr->size};
            }
            return aggregate(*repr);
        }
        if (path->binding.is_Union()) {
            Vector<TransmuteNfa::Fragment> alternatives;
            for (const auto& field : repr->fields) {
                alternatives.pushBack(combine({Segment{0, build(field.ty)}}, repr->size).fragment);
            }
            return {nfa.alternative(std::move(alternatives)), repr->size};
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

auto TransmuteLayoutBuilder::epsilonClosure(Vector<unsigned> states) const -> Vector<unsigned> {
    Vector<bool> seen;
    seen.zero(nfa.states.size());
    for (auto state : states) {
        seen.mut(state) = true;
    }
    for (size_t i = 0; i < states.length(); i++) {
        for (auto next : nfa.states[states[i]].epsilon) {
            if (!seen[next]) {
                seen.mut(next) = true;
                states.pushBack(next);
            }
        }
    }
    quickSort(mutRange(states));
    return states;
}

TransmuteLayoutBuilder::TransmuteLayoutBuilder(const Span& sp, const StaticTraitResolve& resolve, bool destination, bool assumeSafety)
    : sp(sp)
    , resolve(resolve)
    , destination(destination)
    , assumeSafety(assumeSafety)
{
}

auto TransmuteLayoutBuilder::makeDfa(const HIRType* ty, TransmuteDfa& out) -> bool {
    auto root = build(ty);
    if (!supported) {
        return false;
    }

    std::map<Vector<unsigned>, unsigned, VectorLess> indexes;
    std::vector<Vector<unsigned>> states;
    auto intern = [&](Vector<unsigned> state) {
        auto result = indexes.emplace(state, indexes.size());
        if (result.second) {
            states.push_back(std::move(state));
            TransmuteDfa::Transitions transitions;
            transitions.fill(-1);
            out.transitions.push_back(transitions);
            out.references.push_back({});
            out.accepting.pushBack(false);
        }
        return result.first->second;
    };

    intern(epsilonClosure({root.fragment.start}));
    for (size_t stateIndex = 0; stateIndex < states.size(); stateIndex++) {
        const auto state = states[stateIndex];
        out.accepting.mut(stateIndex) = std::binary_search(state.begin(), state.end(), root.fragment.accept);

        std::array<Vector<unsigned>, TRANSMUTE_BYTE_VALUES> destinations;
        for (auto nfaState : state) {
            for (const auto& edge : nfa.states[nfaState].bytes) {
                for (size_t value = 0; value < TRANSMUTE_BYTE_VALUES; value++) {
                    if (edge.values[value]) {
                        destinations[value].pushBack(edge.destination);
                    }
                }
            }
        }
        for (size_t value = 0; value < TRANSMUTE_BYTE_VALUES; value++) {
            auto& destination = destinations[value];
            if (destination.empty()) {
                continue;
            }
            quickSort(mutRange(destination));
            size_t uniqueLength = 1;
            for (size_t i = 1; i < destination.length(); i++) {
                if (destination[i] != destination[uniqueLength - 1]) {
                    destination.mut(uniqueLength++) = destination[i];
                }
            }
            while (destination.length() > uniqueLength) {
                destination.popBack();
            }
            out.transitions[stateIndex][value] = intern(epsilonClosure(std::move(destination)));
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

TransmuteTypeChecker::TransmuteTypeChecker(const Span& sp, const StaticTraitResolve& resolve, bool assumeAlignment, bool assumeSafety, bool assumeValidity)
    : sp(sp)
    , resolve(resolve)
    , assumeAlignment(assumeAlignment)
    , assumeSafety(assumeSafety)
    , assumeValidity(assumeValidity)
{
}

auto TransmuteTypeChecker::referencesCompatible(const TransmuteReference& source, const TransmuteReference& destination) -> bool {
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
    return resolve.typeIsInteriorMutable(sp, destination.referent) == InteriorMutability::No;
}

auto TransmuteTypeChecker::validityIsAssumed() const -> bool {
    return assumeValidity;
}

auto TransmuteRelation::check(unsigned sourceState, unsigned destinationState) -> bool {
    const auto key = std::make_pair(sourceState, destinationState);
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
            const bool edgeResult = destinationNext >= 0 && check(static_cast<unsigned>(sourceNext), static_cast<unsigned>(destinationNext));
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
                if (typeChecker.referencesCompatible(sourceEdge.first, destinationEdge.first) && check(sourceEdge.second, destinationEdge.second)) {
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

TransmuteRelation::TransmuteRelation(const TransmuteDfa& source, const TransmuteDfa& destination, TransmuteTypeChecker& typeChecker)
    : source(source)
    , destination(destination)
    , typeChecker(typeChecker)
    , assumeValidity(typeChecker.validityIsAssumed())
{
}

auto TransmuteRelation::check() -> bool {
    if (!source.inhabited()) {
        return true;
    }
    if (!destination.inhabited()) {
        return false;
    }
    return check(0, 0);
}

template <>
void stl::output<ZeroCopyOutput, Ent>(ZeroCopyOutput& os, const Ent& e) {
    os << StringView("Ent { #") << e.field << StringView(": s=") << e.size << StringView(" a=") << e.align << (e.userAlign ? "!" : "") << StringView(" : ") << e.ty << StringView(" }");
    return;
}

template <>
void stl::output<ZeroCopyOutput, std::vector<Ent>>(ZeroCopyOutput& out, const std::vector<Ent>& values) {
    outCont(out, values);
}

template <>
void stl::output<ZeroCopyOutput, TypeRepr::FieldPath>(ZeroCopyOutput& os, const TypeRepr::FieldPath& x) {
    os << x.size << StringView("@") << x.index;
    for (auto idx : x.subFields) {
        if (idx == TypeRepr::FieldPath::ARRAY_ELEMENT) {
            os << StringView("[0]");
        } else {
            os << StringView(".") << idx;
        }
    }
    return;
}
