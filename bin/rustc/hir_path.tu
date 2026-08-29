# A const generic value.  clone() stays hand-written: Unevaluated re-wraps
# into a fresh unique_ptr; every other variant copies plainly.

generate(
    name="HIRConstGeneric",
    default="Infer",
    clone=False,
    output=True,
    variants=[
        v("Infer", "HIRInferData", doc="To be inferred"),
        v("Unevaluated", "std::unique_ptr<HIRConstGenericUnevaluated>",
          copy=False,
          doc="Unevaluated (or evaluation deferred). A unique_ptr because the"
              " payload holds two PathParams and a shared ptr; every other"
              " variant is two pointers"),
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
