# hir_hir.h include point before HIREnum: the enum body union.
# Sub-unit of hir_hir.h.

context("hir_hir.h")

generate(
    name="HIREnumClass",
    default="Data",
    clone=False,
    variants=[
        v("Data", "::std::vector<HIREnumDataVariant>", copy=False),
        v("Value", fields=[
            ("::std::vector<HIREnumValueVariant>", "variants"),
        ], copy=False),
    ],
)
