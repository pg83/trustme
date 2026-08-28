# Local to resolve_main_bindings.cpp: one entry of the name-resolution
# context stack.

local()

generate(
    name="Ent",
    default="Module",
    clone=False,
    variants=[
        v("Module", fields=[("const ASTModule*", "mod")]),
        v("ConcreteSelf", "ASTType* const*"),
        v("VarBlock", fields=[
            ("unsigned int", "level"),
            ("std::vector<std::pair<Ident, unsigned int>>", "variables"),
        ], copy=False, doc="variables maps names to function-level variable"
                           " slots"),
        v("MacroDefinition", fields=[
            ("unsigned int", "level"),
            ("unsigned int", "definitionId"),
            ("Ident::Hygiene", "tokenHygiene"),
            ("Ident::Hygiene", "definitionHygiene"),
        ], copy=False),
        v("Generic", fields=[
            ("GenericSlot::Level", "level"),
            ("ASTGenericParams*", "paramsDef"),
            ("std::vector<Named<GenericSlot>>", "types"),
            ("std::vector<NamedI<GenericSlot>>", "constants"),
            ("std::vector<NamedI<GenericSlot>>", "lifetimes"),
        ], copy=False, doc="Maps names to generic slots. paramsDef: what if"
                           " it's HRBs? they have a different type"),
    ],
)
