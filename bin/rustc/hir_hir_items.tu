# hir_hir.h include point after HIRModule: the three item namespaces.
# Sub-unit of hir_hir.h.

context("hir_hir.h")

generate(
    name="HIRTypeItem",
    default="Import",
    clone=False,
    variants=[
        v("Import", fields=[
            ("HIRSimplePath", "path"),
            ("bool", "isVariant"),
            ("unsigned int", "idx"),
        ], copy=False),
        v("Module", "HIRModule", copy=False),
        v("TypeAlias", "HIRTypeAlias", copy=False,
          doc="These don't introduce new values"),
        v("TraitAlias", "HIRTraitAlias", copy=False),
        v("ExternType", "HIRExternType", copy=False),
        v("Enum", "HIREnum", copy=False),
        v("Struct", "HIRStruct", copy=False),
        v("Union", "HIRUnion", copy=False),
        v("Trait", "HIRTrait", copy=False),
    ],
)

generate(
    name="HIRValueItem",
    default="Import",
    clone=False,
    variants=[
        v("Import", fields=[
            ("HIRSimplePath", "path"),
            ("bool", "isVariant"),
            ("unsigned int", "idx"),
        ], copy=False),
        v("Constant", "HIRConstant", copy=False),
        v("Static", "HIRStatic", copy=False),
        v("StructConstant", fields=[("HIRSimplePath", "ty")], copy=False),
        v("Function", "HIRFunction", copy=False),
        v("StructConstructor", fields=[("HIRSimplePath", "ty")], copy=False),
    ],
)

generate(
    name="HIRMacroItem",
    default="Import",
    clone=False,
    variants=[
        v("Import", fields=[("HIRSimplePath", "path")], copy=False),
        v("MacroRules", "MacroRulesPtr", copy=False),
        v("ProcMacro", "HIRProcMacro"),
    ],
)
