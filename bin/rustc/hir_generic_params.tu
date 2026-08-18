# A bound on a HIR generic.  clone() stays hand-written: it deliberately
# shares the interned HIRTypeRef values instead of cloning through them.

generate(
    name="HIRGenericBound",
    default="TypeEquality",
    clone=False,
    variants=[
        v("TraitBound", fields=[
            ("HIRTypeRef", "type"),
            ("HIRTraitPath", "trait"),
            ("HIRBoundConstness", "constness", "HIRBoundConstness::Never"),
        ], copy=False),
        v("TypeEquality", fields=[
            ("HIRTypeRef", "type"),
            ("HIRTypeRef", "otherType"),
        ]),
    ],
    extra="""
        HIRGenericBound clone() const;
        Ordering ord(const HIRGenericBound& x) const;
    """,
)
