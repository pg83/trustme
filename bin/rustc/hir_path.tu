# A const generic value.  clone() stays hand-written: every variant copies
# plainly, Unevaluated as the pointer to its interned node.

generate(
    name="HIRConstGeneric",
    default="Infer",
    clone=False,
    output=True,
    variants=[
        v("Infer", "HIRInferData", doc="To be inferred"),
        v("Unevaluated", "const HIRConstGenericUnevaluated*",
          doc="Unevaluated (or evaluation deferred): an interned, immutable"
              " node"),
        v("Generic", "HIRGenericRef", doc="A single generic reference"),
        v("Evaluated", "const EncodedLiteral*", doc="A fully known literal, frozen in the literal pool"),
    ],
    extra="""
        HIRConstGeneric clone() const;
        bool operator==(const HIRConstGeneric& x) const;
        bool operator!=(const HIRConstGeneric& x) const {
            return !(*this == x);
        }
        Ordering ord(const HIRConstGeneric& x) const;
    """,
)
