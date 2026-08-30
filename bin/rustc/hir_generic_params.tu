# A bound on a HIR generic.  clone() stays hand-written: it deliberately
# shares the interned const HIRTypeData* values instead of cloning through them.

generate(
    name="HIRGenericBound",
    default="TypeEquality",
    clone=False,
    output=True,
    variants=[
        v("TraitBound", fields=[
            ("const HIRTypeData*", "type"),
            ("HIRTraitPath", "trait"),
            ("HIRBoundConstness", "constness", "HIRBoundConstness::Never"),
            ("bool", "isTrivial", "false"),
        ], copy=False),
        v("TypeEquality", fields=[
            ("const HIRTypeData*", "type"),
            ("const HIRTypeData*", "otherType"),
        ]),
    ],
    extra="""
        HIRGenericBound clone() const;
        Ordering ord(const HIRGenericBound& x) const;
    """,
)
