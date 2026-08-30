# hir_path.h include point before HIRPath: the path data union, hoisted out
# of the class (a using-alias keeps the nested spelling).

context("hir_path.h")

generate(
    name="HIRPathData",
    default="Generic",
    clone=False,
    doc="Two possibilities: a UFCS path or a generic path",
    variants=[
        v("Generic", "HIRGenericPath", copy=False),
        v("UfcsInherent", fields=[
            ("const HIRType*", "type"),
            ("RcString", "item"),
            ("HIRPathParams", "params"),
            ("HIRPathParams", "implParams"),
        ], copy=False),
        v("UfcsKnown", fields=[
            ("const HIRType*", "type"),
            ("HIRGenericPath", "trait"),
            ("RcString", "item"),
            ("HIRPathParams", "params"),
        ], copy=False),
        v("UfcsUnknown", fields=[
            ("const HIRType*", "type"),
            ("RcString", "item"),
            ("HIRPathParams", "params"),
        ], copy=False),
    ],
)
