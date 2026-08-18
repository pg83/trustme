# First hir_type.h include point: the array size and the path binding, both
# in-place (their payloads are pointer-sized).  Sub-unit of hir_type.h.

context("hir_type.h")

generate(
    name="HIRArraySize",
    default="Unevaluated",
    clone=False,
    doc="Array size used for types AND array literals",
    variants=[
        v("Unevaluated", "HIRConstGeneric", copy=False, doc="Un-evaluated size"),
        v("Known", "uint64_t", doc="Fully known"),
    ],
    extra="""
        HIRArraySize clone() const;
        Ordering ord(const HIRArraySize& x) const;
        bool operator==(const HIRArraySize& x) const {
            return ord(x) == OrdEqual;
        }
        bool operator!=(const HIRArraySize& x) const {
            return !operator==(x);
        }
    """,
)

generate(
    name="HIRTypePathBinding",
    default="Unbound",
    clone=False,
    variants=[
        v("Unbound", doc="Not yet bound, either during lowering OR during"
                         " resolution (when associated and still being resolved)"),
        v("Opaque", doc="Opaque, i.e. an associated type of a generic"
                        " (or Self in a trait)"),
        v("ExternType", "const HIRExternType*"),
        v("Struct", "const HIRStruct*"),
        v("Union", "const HIRUnion*"),
        v("Enum", "const HIREnum*"),
    ],
    extra="""
        HIRTypePathBinding clone() const;

        const HIRGenericParams* getGenerics() const;
        const HIRTraitMarkings* getTraitMarkings() const;

        bool operator==(const HIRTypePathBinding& x) const;
        bool operator!=(const HIRTypePathBinding& x) const {
            return !(*this == x);
        }
    """,
)
