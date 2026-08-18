# hir_hir.h include point after the value item classes.
# Sub-unit of hir_hir.h.

context("hir_hir.h")

generate(
    name="HIRTraitValueItem",
    default="Constant",
    clone=False,
    variants=[
        v("Constant", "HIRConstant", copy=False),
        v("Static", "HIRStatic", copy=False),
        v("Function", "HIRFunction", copy=False),
    ],
)
