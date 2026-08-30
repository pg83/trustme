# A bound on a HIR generic.  clone() stays hand-written: it deliberately
# shares the interned const HIRType* values instead of cloning through them.

generate(
    name="HIRGenericBound",
    default="TypeEquality",
    clone=False,
    output=True,
    variants=[
        v("TraitBound", fields=[
            ("const HIRType*", "type"),
            ("HIRTraitPath", "trait"),
            ("HIRBoundConstness", "constness", "HIRBoundConstness::Never"),
            ("bool", "isTrivial", "false"),
        ], copy=False),
        v("TypeEquality", fields=[
            ("const HIRType*", "type"),
            ("const HIRType*", "otherType"),
        ]),
    ],
    extra="""
        HIRGenericBound clone() const;
        Ordering ord(const HIRGenericBound& x) const;
    """,
)
