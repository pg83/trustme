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
        bool hasNiche = false;
        TypeReprNiche niche;
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
        TargetArch rv{
            "x86_64",
            64,
            false,
            TargetArch::Atomics(/*atomic(u8)=*/true, /*atomic(u16)=*/true, /*atomic(u32)=*/true, true, true),
            TargetArch::Alignments(2, 4, 8, 16, 4, 8, 8)
            //TargetArch::Alignments(2, 4, 8, 8, 4, 8, 8) // TODO: Alignment of u128 is 8 with rustc, but gcc uses 16
        };
        rv.features.pushBack("fxsr");
        rv.features.pushBack("sse");
        rv.features.pushBack("sse2");
        return rv;
    }

    TargetArch archX32() {
        TargetArch rv{"x86_64", 32, false, archX86_64().atomics, TargetArch::Alignments(2, 4, 8, 16, 4, 8, 4)};
        rv.features = archX86_64().features;
        return rv;
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

    bool osHasThreadLocal(const TargetSpec& spec) {
        return spec.osName == "linux" || spec.osName == "freebsd" || spec.osName == "netbsd" || spec.osName == "dragonfly" || spec.osName == "macos";
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

    U128 nicheMask(size_t size) {
        return size >= 16 ? U128::max() : (U128(1) << static_cast<unsigned>(size * 8)) - U128(1);
    }

    U128 nicheAvailable(const TypeReprNiche& niche) {
        return (niche.start - (niche.end + U128(1))) & nicheMask(niche.size);
    }

    bool nicheReserve(const TypeReprNiche& niche, U128 count, U128& nicheStart, TypeReprNiche& reserved) {
        if (count > nicheAvailable(niche)) {
            return false;
        }
        const U128 mask = nicheMask(niche.size);
        reserved = TypeReprNiche(niche);
        const auto moveStart = [&]() {
            nicheStart = (niche.start - count) & mask;
            reserved.start = nicheStart;
        };
        const auto moveEnd = [&]() {
            nicheStart = (niche.end + U128(1)) & mask;
            reserved.end = (niche.end + count) & mask;
        };
        if (niche.start > niche.end) {
            moveEnd();
        } else if (niche.start <= mask - niche.end) {
            if (count <= niche.start) {
                moveStart();
            } else {
                moveEnd();
            }
        } else {
            const U128 end = (niche.end + count) & mask;
            if (U128(1) <= end && end <= niche.end) {
                moveStart();
            } else {
                moveEnd();
            }
        }
        return true;
    }

    TypeReprNiche nicheWithin(const TypeReprNiche& inner, size_t fieldIndex, size_t fieldOffset) {
        TypeReprNiche rv;
        rv.path.pushBack(fieldIndex);
        for (const auto index : inner.path) {
            rv.path.pushBack(index);
        }
        rv.offset = fieldOffset + inner.offset;
        rv.size = inner.size;
        rv.start = inner.start;
        rv.end = inner.end;
        return rv;
    }

    bool typeNiche(const Span& sp, const StaticTraitResolve& resolve, const HIRType* ty, TypeReprNiche& out);

    bool makeFieldEnt(const Span& sp, const StaticTraitResolve& resolve, unsigned idx, const HIRType* ty, Ent& out) {
        size_t size, align;
        if (!TargetGetSizeAndAlignOf(sp, resolve, ty, size, align)) {
            DEBUG(StringView("Can't get size/align of ") << ty);
            return false;
        }
        out = Ent{idx, size, align, nullptr, false};
        out.userAlign = TargetTypeHasUserAlignment(sp, resolve, ty);
        out.hasNiche = size != SIZE_MAX && typeNiche(sp, resolve, ty, out.niche);
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
        path.params = path.params.withType(0, dropeeTy);
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

    enum class StructKind {
        AlwaysSized,
        MaybeUnsized,
        Prefixed,
    };

    enum class NicheBias {
        Start,
        End,
    };

    struct UnivariantLayout {
        Vector<size_t> offsets;
        size_t size = 0;
        size_t align = 1;
        bool hasNiche = false;
        TypeReprNiche niche;
    };

    size_t trailingZeros(size_t value) {
        size_t rv = 0;
        while (value != 0 && (value & 1) == 0) {
            value >>= 1;
            rv++;
        }
        return rv;
    }

    U128 entNicheSize(const Ent& e) {
        return e.hasNiche ? nicheAvailable(e.niche) : U128(0);
    }

    UnivariantLayout univariantBiased(const Ent* ents, size_t count, StructKind kind, size_t prefixSize, size_t prefixAlign, bool optimize, unsigned pack, unsigned forcedAlignment, NicheBias bias) {
        UnivariantLayout rv;
        Vector<size_t> order;
        for (size_t i = 0; i < count; i++) {
            order.pushBack(i);
            rv.offsets.pushBack(0);
        }
        const size_t end = kind == StructKind::MaybeUnsized && count > 0 ? count - 1 : count;
        if (optimize && count > 1) {
            size_t maxFieldAlign = 1;
            U128 largestNicheSize = U128(0);
            for (size_t i = 0; i < end; i++) {
                maxFieldAlign = maxFieldAlign < ents[i].align ? ents[i].align : maxFieldAlign;
                const auto available = entNicheSize(ents[i]);
                largestNicheSize = largestNicheSize < available ? available : largestNicheSize;
            }
            const auto groupKey = [&](const Ent& e) -> size_t {
                if (pack > 0) {
                    return e.align < pack ? e.align : pack;
                }
                const size_t sizeAsAlign = trailingZeros(e.size < e.align ? e.align : e.size);
                if (largestNicheSize != U128(0)) {
                    if (bias == NicheBias::Start) {
                        const size_t maxAlignKey = trailingZeros(maxFieldAlign);
                        return maxAlignKey < sizeAsAlign ? maxAlignKey : sizeAsAlign;
                    }
                    if (entNicheSize(e) == largestNicheSize) {
                        return trailingZeros(e.align);
                    }
                }
                return sizeAsAlign;
            };
            const auto innerKey = [&](const Ent& e) -> size_t {
                if (!e.hasNiche) {
                    return 0;
                }
                if (bias == NicheBias::Start) {
                    return e.niche.offset;
                }
                return ~(e.size - e.niche.size - e.niche.offset);
            };
            const auto before = [&](const Ent& a, const Ent& b) {
                const size_t groupA = groupKey(a);
                const size_t groupB = groupKey(b);
                const U128 nicheA = entNicheSize(a);
                const U128 nicheB = entNicheSize(b);
                if (kind == StructKind::Prefixed) {
                    if (groupA != groupB) {
                        return groupA < groupB;
                    }
                    return nicheA < nicheB;
                }
                if (groupA != groupB) {
                    return groupA > groupB;
                }
                if (nicheA != nicheB) {
                    return bias == NicheBias::Start ? nicheA > nicheB : nicheA < nicheB;
                }
                return innerKey(a) < innerKey(b);
            };
            for (size_t i = 1; i < end; i++) {
                const size_t current = order[i];
                size_t j = i;
                while (j > 0 && before(ents[current], ents[order[j - 1]])) {
                    order.mut(j) = order[j - 1];
                    j--;
                }
                order.mut(j) = current;
            }
        }

        size_t offset = 0;
        size_t align = 1;
        if (kind == StructKind::Prefixed) {
            const size_t effectivePrefixAlign = pack > 0 && pack < prefixAlign ? pack : prefixAlign;
            align = align < effectivePrefixAlign ? effectivePrefixAlign : align;
            offset = alignTo(prefixSize, effectivePrefixAlign);
        }
        U128 largestAvailable = U128(0);
        bool isFirstField = true;
        for (size_t k = 0; k < count; k++) {
            const size_t i = order[k];
            const auto& e = ents[i];
            size_t fieldAlign = e.align;
            if (TargetCapsMemberAlignment() && e.size > 0) {
                if (!isFirstField && !e.userAlign && fieldAlign >= 4 && fieldAlign <= 8) {
                    fieldAlign = 4;
                }
                isFirstField = false;
            }
            if (pack > 0 && pack < fieldAlign) {
                fieldAlign = pack;
            }
            if (fieldAlign > 0) {
                offset = alignTo(offset, fieldAlign);
            }
            align = align < fieldAlign ? fieldAlign : align;
            rv.offsets.mut(i) = offset;
            if (e.hasNiche && e.field != ~0u) {
                const auto available = nicheAvailable(e.niche);
                const bool prefer = bias == NicheBias::Start ? available > largestAvailable : available >= largestAvailable;
                if (prefer) {
                    largestAvailable = available;
                    rv.niche = nicheWithin(e.niche, e.field, offset);
                    rv.hasNiche = true;
                }
            }
            if (e.size == SIZE_MAX) {
                offset = SIZE_MAX;
            } else {
                offset += e.size;
            }
        }
        if (forcedAlignment > 0 && align < forcedAlignment) {
            align = forcedAlignment;
        }
        rv.align = align;
        rv.size = offset == SIZE_MAX ? SIZE_MAX : alignTo(offset, align);
        return rv;
    }

    UnivariantLayout univariant(const Ent* ents, size_t count, StructKind kind, size_t prefixSize, size_t prefixAlign, bool optimize, unsigned pack, unsigned forcedAlignment) {
        auto layout = univariantBiased(ents, count, kind, prefixSize, prefixAlign, optimize, pack, forcedAlignment, NicheBias::Start);
        if (kind != StructKind::MaybeUnsized && layout.hasNiche && count > 1 && layout.size != SIZE_MAX) {
            const size_t headSpace = layout.niche.offset;
            const size_t tailSpace = layout.size - headSpace - layout.niche.size;
            if (headSpace != 0 && tailSpace > 0) {
                auto alternative = univariantBiased(ents, count, kind, prefixSize, prefixAlign, optimize, pack, forcedAlignment, NicheBias::End);
                if (alternative.hasNiche && alternative.niche.offset > headSpace && alternative.niche.offset > tailSpace) {
                    return alternative;
                }
            }
        }
        return layout;
    }

    std::unique_ptr<TypeRepr> typeReprFromLayout(const Span& sp, const HIRType* ty, const Ent* ents, size_t count, const UnivariantLayout& layout, size_t shift, unsigned forcedAlignment) {
        unsigned maxField = 0;
        bool anyField = false;
        for (size_t i = 0; i < count; i++) {
            if (ents[i].field != ~0u) {
                maxField = maxField < ents[i].field ? ents[i].field : maxField;
                anyField = true;
            }
        }
        std::vector<TypeRepr::Field> fields(anyField ? maxField + 1 : 0);

        TypeRepr rv;
        for (size_t i = 0; i < count; i++) {
            const auto& e = ents[i];
            if (e.userAlign) {
                rv.userAlign = true;
            }
            if (e.field != ~0u) {
                ASSERT_BUG(sp, e.field < fields.size(), StringView("Field index out of range"));
                ASSERT_BUG(sp, fields[e.field].ty == nullptr, StringView("Dupliate field index"));
                fields[e.field].offset = layout.offsets[i] + shift;
                fields[e.field].ty = e.ty;
            }
        }
        if (forcedAlignment > 0) {
            rv.userAlign = true;
        }
        for (const auto& f : fields) {
            ASSERT_BUG(sp, f.ty != nullptr, StringView("Uninitialised field found - ") << (&f - &fields[0]));
        }
        rv.align = layout.align;
        rv.size = layout.size == SIZE_MAX ? SIZE_MAX : layout.size + shift;
        rv.fields = mv$(fields);
        rv.hasNiche = layout.hasNiche;
        if (layout.hasNiche) {
            rv.niche = nicheWithin(layout.niche, 0, shift);
            rv.niche.path.clear();
            for (const auto index : layout.niche.path) {
                rv.niche.path.pushBack(index);
            }
        }
        DEBUG(ty << StringView(": size = ") << rv.size << StringView(", align = ") << rv.align);
        return box$(rv);
    }

    std::unique_ptr<TypeRepr> makeTypeReprStructInner(const Span& sp, const HIRType* ty, std::vector<Ent>& ents, StructSorting sorting, unsigned forcedAlignment, unsigned maxAlignment) {
        const auto kind = sorting == StructSorting::AllButFinal ? StructKind::MaybeUnsized : StructKind::AlwaysSized;
        const auto layout = univariant(ents.data(), ents.size(), kind, 0, 1, sorting != StructSorting::None, maxAlignment, forcedAlignment);
        return typeReprFromLayout(sp, ty, ents.data(), ents.size(), layout, 0, forcedAlignment);
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
            sorting = ents.empty() ? StructSorting::All : StructSorting::AllButFinal;
        } else {
            BUG(sp, StringView("Unexpected type in creating type repr - ") << ty);
        }

        auto repr = makeTypeReprStructInner(sp, ty, ents, sorting, forcedAlignment, maxAlignment);
        if (ty->is_Path() && ty->as_Path().binding.is_Struct()) {
            const auto& te = ty->as_Path();
            const auto& str = *te.binding.as_Struct();
            if (str.repr == HIRStruct::Repr::Simd && str.maxFieldAlignment == 0 && repr->size != SIZE_MAX) {
                size_t vectorAlign = 1;
                while (vectorAlign < repr->size) {
                    vectorAlign <<= 1;
                }
                if (vectorAlign > repr->align) {
                    repr->align = vectorAlign;
                    repr->userAlign = true;
                }
                while (repr->size % repr->align != 0) {
                    repr->size++;
                }
            }
            if (str.structMarkings.isAsyncDropGlue && !monomorphiseTypeNeeded(ty) && !extendAsyncDropGlueRepr(sp, resolve, ty, *repr)) {
                return nullptr;
            }
            if (str.structMarkings.isNoNiche) {
                repr->hasNiche = false;
            } else if ((str.structMarkings.isNonzero || str.structMarkings.boundedMax) && !repr->fields.empty() && repr->fields[0].offset == 0) {
                const auto* fieldTy = repr->fields[0].ty;
                size_t scalarSize = 0;
                if (fieldTy->is_Pointer() || fieldTy->is_Borrow()) {
                    scalarSize = TargetGetPointerBits() / 8;
                } else {
                    TargetGetSizeOf(sp, resolve, fieldTy, scalarSize);
                }
                if (scalarSize > 0 && scalarSize <= 16) {
                    TypeReprNiche niche{{}, 0, scalarSize, U128(0), nicheMask(scalarSize)};
                    TypeReprNiche fieldNiche;
                    if (typeNiche(sp, resolve, fieldTy, fieldNiche) && fieldNiche.offset == 0 && fieldNiche.size == scalarSize) {
                        niche.start = fieldNiche.start;
                        niche.end = fieldNiche.end;
                    }
                    niche.path.pushBack(0);
                    if (str.structMarkings.isNonzero) {
                        niche.start = U128(1);
                    }
                    if (str.structMarkings.boundedMax) {
                        niche.end = str.structMarkings.boundedMaxValue & nicheMask(scalarSize);
                    }
                    if (nicheAvailable(niche) != U128(0) && (!repr->hasNiche || nicheAvailable(repr->niche) <= nicheAvailable(niche))) {
                        repr->niche = mv$(niche);
                        repr->hasNiche = true;
                    }
                }
            }
        }
        return repr;
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

    bool typeNiche(const Span& sp, const StaticTraitResolve& resolve, const HIRType* ty, TypeReprNiche& out) {
        switch (ty->tag()) {
            case HIRType::TAG_Primitive:
                switch (ty->as_Primitive()) {
                    case HIRCoreType::Bool:
                        out = TypeReprNiche{{}, 0, 1, U128(0), U128(1)};
                        return true;
                    case HIRCoreType::Char:
                        out = TypeReprNiche{{}, 0, 4, U128(0), U128(0x10FFFF)};
                        return true;
                    default:
                        return false;
                }
            case HIRType::TAG_Borrow:
            case HIRType::TAG_Function: {
                const size_t pointerSize = TargetGetPointerBits() / 8;
                out = TypeReprNiche{{}, 0, pointerSize, U128(1), nicheMask(pointerSize)};
                return true;
            }
            case HIRType::TAG_Pattern: {
                size_t scalarSize = 0;
                std::vector<std::pair<size_t, size_t>> ranges;
                if (!getPatternValidRanges(ty->as_Pattern(), scalarSize, ranges) || ranges.empty()) {
                    return false;
                }
                out = TypeReprNiche{{}, 0, scalarSize, U128(static_cast<u64>(ranges.front().first)), U128(static_cast<u64>(ranges.back().second))};
                return nicheAvailable(out) != U128(0);
            }
            case HIRType::TAG_Array: {
                const auto& te = ty->as_Array();
                TypeReprNiche element;
                if (!te.size.is_Known() || te.size.as_Known() == 0 || !typeNiche(sp, resolve, te.inner, element)) {
                    return false;
                }
                out = nicheWithin(element, TypeRepr::FieldPath::ARRAY_ELEMENT, 0);
                return true;
            }
            case HIRType::TAG_Tuple:
            case HIRType::TAG_Path: {
                if (const auto* path = ty->opt_Path(); path && (path->isGenerator() || path->isFuture())) {
                    return false;
                }
                const auto* repr = TargetGetTypeRepr(sp, resolve, ty);
                if (!repr || !repr->hasNiche) {
                    return false;
                }
                out = TypeReprNiche(repr->niche);
                return true;
            }
            default:
                return false;
        }
    }

    const HIRType* unsignedOfSize(const StaticTraitResolve& resolve, size_t size) {
        switch (size) {
            case 1:
                return resolve.hirCrate().types.primitive(HIRCoreType::U8);
            case 2:
                return resolve.hirCrate().types.primitive(HIRCoreType::U16);
            case 4:
                return resolve.hirCrate().types.primitive(HIRCoreType::U32);
            case 8:
                return resolve.hirCrate().types.primitive(HIRCoreType::U64);
            default:
                return resolve.hirCrate().types.primitive(HIRCoreType::U128);
        }
    }

    bool coreTypeIsSigned(HIRCoreType ty) {
        switch (ty) {
            case HIRCoreType::I8:
            case HIRCoreType::I16:
            case HIRCoreType::I32:
            case HIRCoreType::I64:
            case HIRCoreType::I128:
            case HIRCoreType::Isize:
                return true;
            default:
                return false;
        }
    }

    bool discriminantNiche(Vector<U128> values, size_t tagSize, bool isSigned, size_t tagField, TypeReprNiche& out) {
        if (values.empty() || tagSize == 0 || tagSize > 16) {
            return false;
        }
        const U128 mask = nicheMask(tagSize);
        const U128 signBit = U128(1) << static_cast<unsigned>(tagSize * 8 - 1);
        const auto key = [&](U128 v) {
            return isSigned ? (v ^ signBit) : v;
        };
        for (size_t i = 0; i < values.length(); i++) {
            values.mut(i) = values[i] & mask;
        }
        for (size_t i = 1; i < values.length(); i++) {
            const U128 current = values[i];
            size_t j = i;
            while (j > 0 && key(current) < key(values[j - 1])) {
                values.mut(j) = values[j - 1];
                j--;
            }
            values.mut(j) = current;
        }
        Vector<U128> sorted;
        for (size_t i = 0; i < values.length(); i++) {
            if (sorted.empty() || sorted[sorted.length() - 1] != values[i]) {
                sorted.pushBack(values[i]);
            }
        }
        U128 bestStart = sorted[0];
        U128 bestEnd = sorted[0];
        U128 bestDistance = U128(0);
        for (size_t i = 0; i < sorted.length(); i++) {
            const U128 start = sorted[i];
            const U128 end = sorted[(i + 1) % sorted.length()];
            U128 distance;
            if (key(start) > key(end)) {
                distance = (isSigned ? (mask >> 1u) : mask) - ((start - end) & mask);
            } else {
                distance = (end - start) & mask;
            }
            if (i == 0 || distance >= bestDistance) {
                bestDistance = distance;
                bestStart = start;
                bestEnd = end;
            }
        }
        out = TypeReprNiche{{}, 0, tagSize, bestEnd, bestStart};
        out.path.pushBack(tagField);
        return nicheAvailable(out) != U128(0);
    }

    size_t unsignedSizeForAlign(const Span& sp, const StaticTraitResolve& resolve, size_t align) {
        for (size_t size = 1; size <= 16; size *= 2) {
            size_t candidateSize = 0;
            size_t candidateAlign = 0;
            TargetGetSizeAndAlignOf(sp, resolve, unsignedOfSize(resolve, size), candidateSize, candidateAlign);
            if (candidateAlign == align) {
                return size;
            }
        }
        return 0;
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
                    bool hasExplicitValue = false;
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
                        if (var.discriminantValue != U128(static_cast<u64>(&var - e.data()))) {
                            hasExplicitValue = true;
                        }
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
                    Vector<U128> discriminants;
                    for (const auto& v : e) {
                        discriminants.pushBack(v.discriminantValue);
                    }
                    rv.hasNiche = discriminantNiche(discriminants, tagSize, coreTypeIsSigned(tagTy), e.size(), rv.niche);
                    if (hasExplicitValue || tagSize > sizeof(u64)) {
                        rv.variants = TypeRepr::VariantMode::make_Values({{e.size(), tagSize, {}}, mv$(discriminants)});
                    } else {
                        rv.variants = TypeRepr::VariantMode::make_Linear({{e.size(), tagSize, {}}, 0, e.size()});
                    }
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
                        if (innerRepr->hasNiche) {
                            rv.niche = nicheWithin(innerRepr->niche, 0, 0);
                            rv.hasNiche = true;
                        }
                    } else {
                        rv.size = 0;
                        rv.align = 1;
                    }
                } else {
                    struct Variant {
                        const HIRType* type;
                        std::vector<Ent> ents;
                        unsigned forcedAlignment;
                        UnivariantLayout nicheLayout;
                        UnivariantLayout taggedLayout;
                    };

                    bool hasExplcitValue = false;
                    std::vector<Variant> variants;
                    variants.reserve(e.size());
                    for (const auto& var : e) {
                        if (var.discriminantValue != U128(static_cast<u64>(&var - e.data()))) {
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
                        const size_t variantCount = variants.size();
                        const auto* unitTy = resolve.hirCrate().types.unit();

                        size_t enumAlign = 1;
                        size_t largest = 0;
                        for (size_t i = 0; i < variantCount; i++) {
                            auto& v = variants[i];
                            if (e[i].type != unitTy) {
                                v.nicheLayout = univariant(v.ents.data(), v.ents.size(), StructKind::AlwaysSized, 0, 1, true, 0, v.forcedAlignment);
                            }
                            enumAlign = enumAlign < v.nicheLayout.align ? v.nicheLayout.align : enumAlign;
                            if (v.nicheLayout.size >= variants[largest].nicheLayout.size) {
                                largest = i;
                            }
                        }
                        const size_t nicheFirst = largest == 0 ? 1 : 0;
                        const size_t nicheLast = largest + 1 == variantCount ? variantCount - 2 : variantCount - 1;
                        const U128 nicheCount = U128(static_cast<u64>(nicheLast - nicheFirst + 1));
                        const auto& largestLayout = variants[largest].nicheLayout;
                        bool nicheFits = false;
                        U128 nicheStart;
                        TypeReprNiche reserved;
                        size_t nicheEnumSize = 0;
                        Vector<size_t> shifts;
                        if (largestLayout.hasNiche && nicheReserve(largestLayout.niche, nicheCount, nicheStart, reserved)) {
                            nicheEnumSize = alignTo(largestLayout.size, enumAlign);
                            nicheFits = true;
                            for (size_t i = 0; i < variantCount; i++) {
                                const auto& layout = variants[i].nicheLayout;
                                size_t shift = 0;
                                if (i != largest && layout.size > reserved.offset) {
                                    shift = alignTo(reserved.offset + reserved.size, layout.align);
                                    if (shift + layout.size > nicheEnumSize) {
                                        nicheFits = false;
                                    }
                                }
                                shifts.pushBack(shift);
                            }
                        }

                        const size_t minTagSize = variantCount <= 0x100 ? 1 : variantCount <= 0x10000 ? 2 : 4;
                        size_t startAlign = 256;
                        for (size_t i = 0; i < variantCount; i++) {
                            auto& v = variants[i];
                            v.taggedLayout = univariant(v.ents.data(), v.ents.size(), StructKind::Prefixed, minTagSize, minTagSize, true, 0, v.forcedAlignment);
                            size_t firstOffset = SIZE_MAX;
                            size_t firstAlign = 0;
                            for (size_t k = 0; k < v.ents.size(); k++) {
                                const auto& ent = v.ents[k];
                                if (ent.size == 0 && ent.align <= 1) {
                                    continue;
                                }
                                if (v.taggedLayout.offsets[k] < firstOffset) {
                                    firstOffset = v.taggedLayout.offsets[k];
                                    firstAlign = ent.align;
                                }
                            }
                            if (firstAlign != 0 && firstAlign < startAlign) {
                                startAlign = firstAlign;
                            }
                        }
                        size_t tagSize = minTagSize;
                        const size_t widenedTagSize = startAlign < 256 ? unsignedSizeForAlign(sp, resolve, startAlign) : 0;
                        if (widenedTagSize > minTagSize) {
                            tagSize = widenedTagSize;
                            for (auto& v : variants) {
                                for (size_t k = 0; k < v.taggedLayout.offsets.length(); k++) {
                                    if (v.taggedLayout.offsets[k] <= minTagSize) {
                                        ASSERT_BUG(sp, v.taggedLayout.offsets[k] == minTagSize, StringView("Field before the tag in ") << v.type);
                                        v.taggedLayout.offsets.mut(k) = tagSize;
                                    }
                                }
                                if (v.taggedLayout.size <= minTagSize) {
                                    v.taggedLayout.size = tagSize;
                                }
                            }
                        }
                        size_t taggedSize = 0;
                        size_t taggedAlign = 1;
                        for (const auto& v : variants) {
                            taggedSize = taggedSize < v.taggedLayout.size ? v.taggedLayout.size : taggedSize;
                            taggedAlign = taggedAlign < v.taggedLayout.align ? v.taggedLayout.align : taggedAlign;
                        }
                        taggedSize = alignTo(taggedSize, taggedAlign);
                        TypeReprNiche tagNiche{{}, 0, tagSize, U128(0), U128(static_cast<u64>(variantCount - 1)) & nicheMask(tagSize)};
                        tagNiche.path.pushBack(variantCount);

                        const bool zeroNiche = nicheFits && nicheCount == U128(1) && nicheStart == U128(0) && variants[nicheFirst].nicheLayout.size == 0;
                        if (nicheFits && reserved.size > 8 && !zeroNiche) {
                            nicheFits = false;
                        }
                        bool useNiche = false;
                        if (nicheFits) {
                            if (taggedSize > nicheEnumSize) {
                                useNiche = true;
                            } else if (taggedSize == nicheEnumSize && nicheAvailable(tagNiche) < nicheAvailable(reserved)) {
                                useNiche = true;
                            }
                        }
                        DEBUG(StringView("tagged ") << taggedSize << StringView(" niche ") << (nicheFits ? nicheEnumSize : 0) << StringView(" use niche ") << useNiche);

                        if (useNiche) {
                            const auto* nicheTy = unsignedOfSize(resolve, reserved.size);
                            size_t nicheTySize = 0;
                            size_t nicheTyAlign = 1;
                            TargetGetSizeAndAlignOf(sp, resolve, nicheTy, nicheTySize, nicheTyAlign);
                            for (size_t i = 0; i < variantCount; i++) {
                                auto& v = variants[i];
                                if (e[i].type != unitTy) {
                                    auto repr = typeReprFromLayout(sp, v.type, v.ents.data(), v.ents.size(), v.nicheLayout, shifts[i], v.forcedAlignment);
                                    if (i != largest && !zeroNiche) {
                                        repr->fields.push_back(TypeRepr::Field{reserved.offset, nicheTy});
                                        repr->align = repr->align < nicheTyAlign ? nicheTyAlign : repr->align;
                                        if (repr->size < reserved.offset + reserved.size) {
                                            repr->size = reserved.offset + reserved.size;
                                        }
                                        repr->size = alignTo(repr->size, repr->align);
                                        repr->hasNiche = false;
                                        ASSERT_BUG(sp, repr->size <= nicheEnumSize, StringView("Variant ") << i << StringView(" of ") << ty << StringView(" outgrows its enum"));
                                    }
                                    setTypeRepr(resolve, sp, v.type, mv$(repr));
                                }
                                rv.fields.push_back(TypeRepr::Field{0, v.type});
                            }
                            rv.size = nicheEnumSize;
                            rv.align = enumAlign;
                            TypeRepr::FieldPath field{largest, reserved.size, {}};
                            for (const auto index : largestLayout.niche.path) {
                                field.subFields.pushBack(index);
                            }
                            if (zeroNiche) {
                                rv.variants = TypeRepr::VariantMode::make_NonZero({mv$(field), static_cast<unsigned>(nicheFirst)});
                            } else {
                                rv.variants = TypeRepr::VariantMode::make_Linear({mv$(field), static_cast<size_t>(nicheStart.truncateU64()), variantCount});
                            }
                            if (nicheAvailable(reserved) != U128(0)) {
                                rv.niche = nicheWithin(reserved, largest, 0);
                                rv.hasNiche = true;
                            }
                        } else {
                            const auto* tagTy = unsignedOfSize(resolve, tagSize);
                            size_t tagTySize = 0;
                            size_t tagTyAlign = 1;
                            TargetGetSizeAndAlignOf(sp, resolve, tagTy, tagTySize, tagTyAlign);
                            for (size_t i = 0; i < variantCount; i++) {
                                auto& v = variants[i];
                                if (e[i].type != unitTy) {
                                    auto repr = typeReprFromLayout(sp, v.type, v.ents.data(), v.ents.size(), v.taggedLayout, 0, v.forcedAlignment);
                                    repr->fields.push_back(TypeRepr::Field{0, tagTy});
                                    repr->align = repr->align < tagTyAlign ? tagTyAlign : repr->align;
                                    repr->size = alignTo(repr->size < tagTySize ? tagTySize : repr->size, repr->align);
                                    ASSERT_BUG(sp, repr->size <= taggedSize, StringView("Variant ") << i << StringView(" of ") << ty << StringView(" outgrows its enum"));
                                    repr->hasNiche = false;
                                    setTypeRepr(resolve, sp, v.type, mv$(repr));
                                }
                                rv.fields.push_back(TypeRepr::Field{0, v.type});
                            }
                            rv.fields.push_back(TypeRepr::Field{0, tagTy});
                            rv.size = taggedSize;
                            rv.align = taggedAlign;
                            rv.variants = TypeRepr::VariantMode::make_Linear({{variantCount, tagSize, {}}, 0, variantCount});
                            rv.hasNiche = nicheAvailable(tagNiche) != U128(0);
                            rv.niche = mv$(tagNiche);
                        }
                    } else {
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

                        Vector<U128> discriminants;
                        for (const auto& v : e) {
                            discriminants.pushBack(v.discriminantValue);
                        }
                        rv.hasNiche = discriminantNiche(discriminants, tagSize, coreTypeIsSigned(tagTy->as_Primitive()), e.size(), rv.niche);
                        if (hasExplcitValue || tagSize > sizeof(u64)) {
                            DEBUG(StringView("vals = ") << discriminants);
                            rv.variants = TypeRepr::VariantMode::make_Values({{e.size(), tagSize, {}}, mv$(discriminants)});
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
                        if (e.variants.size() == 1 && !enm.isCRepr) {
                        } else if (!e.variants.empty()) {
                            i64 minValue = INT64_MAX;
                            i64 maxValue = INT64_MIN;
                            for (const auto& variant : e.variants) {
                                const auto value = S128(variant.val).truncateI64();
                                minValue = std::min(minValue, value);
                                maxValue = std::max(maxValue, value);
                            }

                            const unsigned atLeast = enm.isCRepr ? 4 : 1;
                            HIRCoreType tagType;
                            if (minValue >= 0) {
                                const auto maxUnsigned = static_cast<u64>(maxValue);
                                if (maxUnsigned <= UINT8_MAX && atLeast <= 1) {
                                    tagType = HIRCoreType::U8;
                                } else if (maxUnsigned <= UINT16_MAX && atLeast <= 2) {
                                    tagType = HIRCoreType::U16;
                                } else if (maxUnsigned <= UINT32_MAX && atLeast <= 4) {
                                    tagType = HIRCoreType::U32;
                                } else {
                                    tagType = HIRCoreType::U64;
                                }
                            } else if (minValue >= INT8_MIN && maxValue <= INT8_MAX && atLeast <= 1) {
                                tagType = HIRCoreType::I8;
                            } else if (minValue >= INT16_MIN && maxValue <= INT16_MAX && atLeast <= 2) {
                                tagType = HIRCoreType::I16;
                            } else if (minValue >= INT32_MIN && maxValue <= INT32_MAX && atLeast <= 4) {
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
                    rv.hasNiche = discriminantNiche(vals, rv.size, coreTypeIsSigned(rv.fields.back().ty->as_Primitive()), 0, rv.niche);
                    rv.variants = TypeRepr::VariantMode::make_Values({{0, static_cast<u8>(rv.size), {}}, std::move(vals)});
                } else {
                    rv.size = 0;
                    rv.align = 1;
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
                if (const auto* closure = ty->as_NodeType().opt_Closure(); closure && closureHasNoCaptures(resolve, *closure)) {
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

    std::unordered_map<RcString, CachedTypeRepr> encoded;
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
    spec->hasThreadLocal = osHasThreadLocal(*spec);
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
    if (tgt.hasThreadLocal) {
        CfgSetFlag(settings, "target_thread_local");
    }
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
    for (const auto* feature : tgt.arch.features) {
        CfgSetValue(settings, "target_feature", feature);
    }
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
            if (const auto* closure = te.opt_Closure(); closure && closureHasNoCaptures(resolve, *closure)) {
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

    auto symbol = TransMangle(resolve.board(), ty);
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
    return static_cast<size_t>(((U128(static_cast<u64>(this->offset)) + U128(static_cast<u64>(varIdx - start))) & nicheMask(this->field.size)).truncateU64());
}

unsigned TypeRepr::VariantMode::Data_Linear::decodeTag(U128 tag) const {
    if (!this->usesNiche()) {
        return (tag - U128(this->offset)).truncateU64();
    }
    const auto relative = (tag - U128(static_cast<u64>(this->offset))) & nicheMask(this->field.size);
    if (relative < U128(static_cast<u64>(this->nicheVariantCount()))) {
        return static_cast<unsigned>(this->nicheVariantStart() + relative.truncateU64());
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
