# hir_hir.h include point before HIRGlobalAssembly.
# Sub-unit of hir_hir.h.

context("hir_hir.h")

generate(
    name="HIRGlobalAsmOperand",
    default="Const",
    clone=False,
    variants=[
        v("Const", fields=[
            ("HIRConstGeneric", "value"),
            ("const HIRTypeData*", "type"),
        ], copy=False),
        v("Sym", "HIRPath", copy=False),
    ],
)
