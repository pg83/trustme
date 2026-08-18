# Local to trans_main_bindings.cpp: what an enumerated path resolved to.

local()

generate(
    name="EntPtr",
    default="NotFound",
    clone=False,
    variants=[
        v("NotFound"),
        v("AutoGenerate"),
        v("Function", "const HIRFunction*"),
        v("Static", "const HIRStatic*"),
        v("Constant", "const HIRConstant*"),
    ],
)
