# Resolver item references: plain pointer payloads, in-place storage.

generate(
    name="ResolveModuleRef",
    default="None",
    variants=[
        v("None"),
        v("ImplicitPrelude"),
        v("Ast", "const ASTModule*"),
        v("Hir", "const HIRModule*"),
    ],
)

generate(
    name="ResolveItemRefMacro",
    default="None",
    variants=[
        v("None"),
        v("InternalMacro", "ExpandProcMacro*"),
        v("ProcMacro", "const HIRProcMacro*"),
        v("MacroRules", "const MacroRules*"),
    ],
)

generate(
    name="ResolveItemRefType",
    default="None",
    variants=[
        v("None"),
        v("Ast", "const ASTItem*"),
        v("Hir", "const HIRTypeItem*"),
        v("HirRoot", "const HIRCrate*"),
        v("AstRoot", "const ASTModule*"),
    ],
)

generate(
    name="ResolveItemRefValue",
    default="None",
    variants=[
        v("None"),
        v("Ast", "const ASTItem*"),
        v("Hir", "const HIRValueItem*"),
    ],
)

generate(
    name="ResolveItemRef",
    default="None",
    variants=[
        v("None"),
        v("Namespace", "ResolveItemRefType", copy=False),
        v("Value", "ResolveItemRefValue", copy=False),
        v("Macro", "ResolveItemRefMacro", copy=False),
    ],
)
