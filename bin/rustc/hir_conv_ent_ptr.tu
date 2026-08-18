# Local to hir_conv_constant_evaluation.cpp: what a value path resolved to.

local()

generate(
    name="EntPtr",
    default="NotFound",
    clone=False,
    variants=[
        v("NotFound"),
        v("Function", "const HIRFunction*"),
        v("Static", "const HIRStatic*"),
        v("Constant", "const HIRConstant*"),
        v("Struct", "const HIRStruct*"),
        v("Enum", fields=[
            ("const HIREnum*", "p"),
            ("size_t", "idx"),
        ]),
    ],
)
