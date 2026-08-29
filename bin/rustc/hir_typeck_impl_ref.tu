# The three shapes of a trait impl reference, hoisted out of ImplRef
# (a using-alias keeps the nested spelling).

generate(
    name="ImplRefData",
    default="TraitImpl",
    clone=False,
    variants=[
        v("TraitImpl", fields=[
            ("HIRPathParams", "implParams"),
            ("const HIRTrait*", "traitPtr"),
            ("const HIRSimplePath*", "traitPath"),
            ("const HIRTraitImpl*", "impl"),
        ], copy=False),
        v("BoundedPtr", fields=[
            ("const HIRTypeData*", "type"),
            ("const HIRPathParams*", "traitArgs"),
            ("const HIRTraitPath::assocListT*", "assoc"),
            ("HIRBoundConstness", "constness"),
        ]),
        v("Bounded", fields=[
            ("HIRTypeRef", "type"),
            ("HIRPathParams", "traitArgs"),
            ("HIRTraitPath::assocListT", "assoc"),
            ("HIRBoundConstness", "constness"),
        ], copy=False),
    ],
)
