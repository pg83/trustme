# Second hir_pattern.h include point: the pattern data union itself.
# clone() is not generated - whole patterns clone via HIRPattern::clone().

generate(
    name="HIRPatternData",
    default="Any",
    clone=False,
    variants=[
        v("Any", doc="Irrefutable / destructuring"),
        v("Box", fields=[
            ("std::unique_ptr<HIRPattern>", "sub"),
        ], copy=False),
        v("Deref", fields=[
            ("HIRPatternDerefKind", "kind"),
            ("const HIRType*", "targetType"),
            ("std::unique_ptr<HIRPattern>", "sub"),
        ], copy=False),
        v("Ref", fields=[
            ("HIRBorrowType", "type"),
            ("bool", "isSkipped"),
            ("std::unique_ptr<HIRPattern>", "sub"),
        ], copy=False),
        v("Tuple", fields=[
            ("std::vector<HIRPattern>", "subPatterns"),
        ], copy=False),
        v("SplitTuple", fields=[
            ("std::vector<HIRPattern>", "leading"),
            ("std::vector<HIRPattern>", "trailing"),
            ("unsigned int", "totalSize"),
        ], copy=False),
        v("PathValue", fields=[
            ("HIRPath", "path"),
            ("HIRPatternPathBinding", "binding"),
        ], copy=False, doc="Maybe refutable: can be converted into `Value`,"
                           " or resolved to be an enum/struct value"),
        v("PathTuple", fields=[
            ("HIRPath", "path"),
            ("HIRPatternPathBinding", "binding"),
            ("std::vector<HIRPattern>", "leading"),
            ("bool", "isSplit"),
            ("std::vector<HIRPattern>", "trailing"),
            ("unsigned int", "totalSize"),
        ], copy=False, doc="Tuple-like enum/struct value. totalSize caches"
                           " the arity, making MIR gen easier for split"
                           " patterns"),
        v("PathNamed", "HIRPatternPathNamedData", copy=False,
          doc="Struct-like enum/struct value"),
        v("Or", "std::vector<HIRPattern>", copy=False,
          doc="Split/or patterns"),
        v("Value", fields=[("HIRPatternValue", "val")], copy=False,
          doc="Always refutable"),
        v("Range", fields=[
            ("std::unique_ptr<HIRPatternValue>", "start"),
            ("std::unique_ptr<HIRPatternValue>", "end"),
            ("bool", "isInclusive"),
        ], copy=False),
        v("Slice", fields=[
            ("std::vector<HIRPattern>", "subPatterns"),
        ], copy=False),
        v("SplitSlice", fields=[
            ("std::vector<HIRPattern>", "leading"),
            ("HIRPatternBinding", "extraBind"),
            ("std::vector<HIRPattern>", "trailing"),
        ], copy=False),
    ],
)
