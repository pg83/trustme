# First hir_pattern.h include point: the literal-value union and the path
# binding.  Sub-unit of hir_pattern.h.

context("hir_pattern.h")

generate(
    name="HIRPatternValue",
    default="String",
    output=True,
    variants=[
        v("Integer", fields=[
            ("HIRCoreType", "type"),
            ("U128", "value"),
        ], doc="type Str means _; signed numbers are encoded as 2's complement"),
        v("Float", fields=[
            ("HIRCoreType", "type"),
            ("FloatValue", "value"),
        ], doc="type Str means _"),
        v("String", "std::string"),
        v("ByteString", fields=[("std::string", "v")]),
        v("Named", fields=[
            ("HIRPath", "path"),
            ("const HIRConstant*", "binding"),
        ], copy=False),
    ],
)

generate(
    name="HIRPatternPathBinding",
    default="Unbound",
    variants=[
        v("Unbound"),
        v("Struct", "const HIRStruct*"),
        v("Union", "const HIRUnion*"),
        v("Enum", fields=[
            ("const HIREnum*", "ptr"),
            ("unsigned", "varIdx"),
        ]),
    ],
)
