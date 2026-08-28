# The AST type node data.  Nodes are pool-allocated (see ASTType); cloning
# happens through ASTType::clone(), so the union itself has none.

generate(
    name="TypeData",
    default="None",
    clone=False,
    variants=[
        v("None"),
        v("Any"),
        v("Bang"),
        v("Unit"),
        v("Macro", fields=[("ASTMacroInvocation*", "inv")]),
        v("Primitive", fields=[("enum eCoreType", "coreType")]),
        v("Function", fields=[("TypeFunction", "info")]),
        v("Tuple", fields=[("std::vector<ASTType*>", "innerTypes")]),
        v("Borrow", fields=[
            ("ASTLifetimeRef", "lifetime"),
            ("bool", "isMut"),
            ("ASTType*", "inner"),
            ("bool", "isPin", "false"),
        ], doc="isPin covers `&pin mut T` and `&pin const T`, which are"
               " `Pin<&mut T>` and `Pin<&T>`. Expansion rewrites them, once"
               " the core crate is known."),
        v("Pointer", fields=[
            ("bool", "isMut"),
            ("ASTType*", "inner"),
        ]),
        v("Array", fields=[
            ("ASTType*", "inner"),
            ("ASTExprNode*", "size"),
        ], doc="A nullptr size means an inferred size"),
        v("Slice", fields=[("ASTType*", "inner")]),
        v("Pattern", fields=[
            ("ASTType*", "inner"),
            ("ASTPattern*", "pattern"),
        ]),
        v("Generic", fields=[
            ("RcString", "name"),
            ("unsigned int", "index"),
        ]),
        v("Path", "ASTPath*"),
        v("TraitObject", fields=[
            ("std::vector<TypeTraitPath>", "traits"),
            ("std::vector<ASTLifetimeRef>", "lifetimes"),
        ]),
        v("ErasedType", "TypeErasedType*"),
    ],
)
