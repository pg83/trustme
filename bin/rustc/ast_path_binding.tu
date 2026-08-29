# ast_path.h include point: the three path-binding unions.  Payloads are
# pointers and indices, so the generated shallow clone replaces the old
# hand-written ones.  Sub-unit of ast_path.h.

context("ast_path.h")

generate(
    name="ASTPathBindingValue",
    default="Unbound",
    output=True,
    variants=[
        v("Unbound"),
        v("Struct", fields=[
            ("const ASTStruct*", "struct_"),
            ("const HIRStruct*", "hir"),
        ]),
        v("Static", fields=[
            ("const ASTStatic*", "static_"),
            ("const HIRStatic*", "hir"),
        ], doc="if hir is nullptr and static_ == nullptr, points to a `const`"),
        v("Function", fields=[("const ASTFunction*", "func_")]),
        v("EnumVar", fields=[
            ("const ASTEnum*", "enum_"),
            ("unsigned int", "idx"),
            ("const HIREnum*", "hir"),
        ]),
        v("Generic", fields=[("unsigned int", "index")]),
        v("Variable", fields=[("unsigned int", "slot")]),
    ],
)

generate(
    name="ASTPathBindingType",
    default="Unbound",
    output=True,
    variants=[
        v("Unbound"),
        v("Primitive", "eCoreType"),
        v("Crate", fields=[("const ASTExternCrate*", "crate_")]),
        v("Module", fields=[
            ("const ASTModule*", "module_"),
            ("ASTPathBindingModuleHir", "hir"),
        ]),
        v("Struct", fields=[
            ("const ASTStruct*", "struct_"),
            ("const HIRStruct*", "hir"),
        ]),
        v("Enum", fields=[
            ("const ASTEnum*", "enum_"),
            ("const HIREnum*", "hir"),
        ]),
        v("Union", fields=[
            ("const ASTUnion*", "union_"),
            ("const HIRUnion*", "hir"),
        ]),
        v("Trait", fields=[
            ("const ASTTrait*", "trait_"),
            ("const HIRTrait*", "hir"),
        ]),
        v("TraitAlias", fields=[
            ("const ASTTraitAlias*", "trait_"),
            ("const HIRTraitAlias*", "hir"),
        ]),
        v("EnumVar", fields=[
            ("const ASTEnum*", "enum_"),
            ("unsigned int", "idx"),
            ("const HIREnum*", "hir"),
        ]),
        v("TypeAlias", fields=[("const ASTTypeAlias*", "alias_")]),
        v("TypeParameter", fields=[("unsigned int", "slot")]),
    ],
)

generate(
    name="ASTPathBindingMacro",
    default="Unbound",
    output=True,
    variants=[
        v("Unbound"),
        v("ProcMacroDerive", fields=[
            ("const ASTExternCrate*", "crate_"),
            ("RcString", "macName"),
        ]),
        v("ProcMacroAttribute", fields=[
            ("const ASTExternCrate*", "crate_"),
            ("RcString", "macName"),
        ]),
        v("ProcMacro", fields=[
            ("const ASTExternCrate*", "crate_"),
            ("RcString", "macName"),
        ]),
        v("MacroRules", fields=[
            ("const ASTExternCrate*", "crate_", "nullptr"),
            ("const MacroRules*", "mac"),
        ], doc="crate_ can be NULL"),
    ],
)
