#pragma once

#include <cstddef>
#include "hir_type.h"
#include "hir_typeck_static.h"

// NOTE: The default architecture is an unnamed 32-bit little-endian arch with all types natively aligned
struct TargetArch {
    ::std::string mName;
    unsigned pointerBits;
    bool bigEndian;

    struct Atomics {
        bool u8 = true;
        bool u16 = true;
        bool u32 = true;
        bool u64 = false;
        bool ptr = true;

        Atomics(bool u8 = true, bool u16 = true, bool u32 = true, bool u64 = false, bool ptr = true);
    } atomics;

    struct Alignments {
        uint8_t u16;
        uint8_t u32;
        uint8_t u64;
        uint8_t u128;
        uint8_t f32;
        uint8_t f64;
        uint8_t ptr;

        Alignments(uint8_t u16 = 2, uint8_t u32 = 4, uint8_t u64 = 8, uint8_t u128 = 16, uint8_t f32 = 4, uint8_t f64 = 8, uint8_t ptr = 4);
    } alignments;
};

struct BackendOptsC {
    bool emulatedI128;       // Influences the chosen alignment for i128/u128
    ::std::string cCompiler; // GNU target triplet
    ::std::vector<::std::string> compilerOpts;
    ::std::vector<::std::string> linkerOptsPre;
    ::std::vector<::std::string> linkerOptsPost;
};

struct TargetSpec {
    ::std::string family;
    ::std::string osName;
    ::std::string envName;

    BackendOptsC backendC;
    TargetArch arch;
};

struct TypeRepr {
    size_t align = 0;
    size_t size = 0;
    /// gcc's `TYPE_USER_ALIGN`: `align` came from an explicit `repr(align(N))` somewhere inside, so it's exempt from a member-alignment cap
    bool user_align = false;

    struct FieldPath {
        static constexpr size_t ARRAY_ELEMENT = static_cast<size_t>(-1);

        size_t index;
        size_t size;
        ::std::vector<size_t> sub_fields;
    };

    TAGGED_UNION(
        VariantMode,
        None,
        (None, struct {}),
        // Variants numbered 0 to N (potentially offset)
        (Linear,
         struct {
             // Note: If `field.sub_fields` has entries, then this is a niche optimisation.
             // Path of the variant
             FieldPath field;
             // Offset for variants (when in a niche)
             size_t offset;
             size_t num_variants;

             bool uses_niche() const {
                 return !field.sub_fields.empty();
             }
             bool isNiche(unsigned var_idx) const {
                 return uses_niche() && var_idx == field.index;
             }
             bool isTag(unsigned var_idx) const {
                 return !uses_niche() && var_idx == field.index;
             }
         }),
        // Tag is a fixed set of values in a field.
        // TODO: Encode niche in here too?
        (Values,
         struct {
             // NOTE: `field.sub_path` should always be empty?
             FieldPath field;
             ::std::vector<U128> values;
             bool isTag(unsigned var_idx) const {
                 return var_idx == field.index;
             }
         }),
        // Tag is based on a range of values
        //(Ranges, struct {
        //    size_t  offset;
        //    size_t  size;
        //    ::std::vector<::std::pair<uint64_t,uint64_t>> values;
        //    }),
        // Tag is a boolean based on if a region is zero/non-zero
        // Only valid for two-element enums
        (NonZero, struct {
            FieldPath field;
            unsigned zero_variant;
        })
    );
    VariantMode variants;

    struct Field {
        size_t offset;
        ::HIR::TypeRef ty;
    };

    ::std::vector<Field> fields;

    /// <summary>
    /// Get the byte offset of a field
    /// </summary>
    /// <param name="sp">Invocation span (for error messages)</param>
    /// <param name="resolve">Resolve structure (shouldn't be needed)</param>
    /// <param name="path">Path to the field</param>
    /// <returns>Byte offset</returns>
    size_t getOffset(const Span& sp, const StaticTraitResolve& resolve, const FieldPath& path) const;

    /// <summary>
    /// Determines which enum variant is stored in an encoded literal
    /// </summary>
    /// <param name="sp">Invocation span (for error messages)</param>
    /// <param name="resolve">Resolve structure (shouldn't be needed)</param>
    /// <param name="lit">Literal covering the entire enum</param>
    /// <returns>Variant index and if the variant's data includes a tag field</returns>
    std::pair<unsigned, bool> getEnumVariant(const Span& sp, const StaticTraitResolve& resolve, const EncodedLiteralSlice& lit) const;
};

std::ostream& operator<<(std::ostream& os, const TypeRepr::FieldPath& x);

extern const TargetSpec& TargetGetCurSpec();
extern void TargetSetCfg(const ::std::string& target_name);
extern void TargetExportCurSpec(const ::std::string& filename);

static inline unsigned TargetGetPointerBits() {
    return TargetGetCurSpec().arch.pointerBits;
}

extern bool TargetGetSizeOf(const Span& sp, const StaticTraitResolve& resolve, const ::HIR::TypeData* ty, size_t& out_size);
extern bool TargetGetAlignOf(const Span& sp, const StaticTraitResolve& resolve, const ::HIR::TypeData* ty, size_t& out_align);
extern bool TargetGetSizeAndAlignOf(const Span& sp, const StaticTraitResolve& resolve, const ::HIR::TypeData* ty, size_t& out_size, size_t& out_align);

/// Does this target's C ABI cap the alignment of a non-first struct member? (Darwin/PowerPC "power" alignment)
extern bool TargetCapsMemberAlignment();
/// gcc's `TYPE_USER_ALIGN`: such a type is exempt from the member-alignment cap above, wherever it appears.
extern bool TargetTypeHasUserAlignment(const Span& sp, const StaticTraitResolve& resolve, const ::HIR::TypeData* ty);

/// This function is for the MIR Optimisation tool, which has to be able to read and use existing layouts
extern void TargetForceTypeRepr(const Span& sp, const ::HIR::TypeData* ty, TypeRepr repr);
extern const TypeRepr* TargetGetTypeRepr(const Span& sp, const StaticTraitResolve& resolve, const ::HIR::TypeData* ty);

extern const ::HIR::TypeData* TargetGetInnerType(const Span& sp, const StaticTraitResolve& resolve, const TypeRepr& repr, size_t idx, const ::std::vector<size_t>& sub_fields = {}, size_t ofs = 0);
