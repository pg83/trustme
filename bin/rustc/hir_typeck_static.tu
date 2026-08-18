# The result of resolving a value path, hoisted out of StaticTraitResolve
# (a using-alias keeps the nested spelling).

generate(
    name="TypeckValuePtr",
    default="NotFound",
    clone=False,
    variants=[
        v("NotFound"),
        v("NotYetKnown"),
        v("Constant", "const HIRConstant*"),
        v("Static", "const HIRStatic*"),
        v("Function", "const HIRFunction*"),
        v("EnumConstructor", fields=[
            ("const HIREnum*", "e"),
            ("size_t", "v"),
        ]),
        v("EnumValue", fields=[
            ("const HIREnum*", "e"),
            ("size_t", "v"),
        ]),
        v("StructConstructor", fields=[
            ("const HIRSimplePath*", "p"),
            ("const HIRStruct*", "s"),
        ]),
        v("StructConstant", fields=[
            ("const HIRSimplePath*", "p"),
            ("const HIRStruct*", "s"),
        ]),
    ],
)
