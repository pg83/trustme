# hir_hir.h include point before HIRStruct: the struct body union.
# Sub-unit of hir_hir.h.

context("hir_hir.h")

generate(
    name="HIRStructData",
    default="Unit",
    clone=False,
    variants=[
        v("Unit"),
        v("Tuple", "tTupleFields", copy=False),
        v("Named", "tStructFields", copy=False),
    ],
)
