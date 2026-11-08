# ast_path.h include point after ASTPathNode: a single path parameter entry.
# clone() stays hand-written (it deep-clones the ASTType pointers).

generate(
    name="ASTPathParamEnt",
    default="Lifetime",
    clone=False,
    variants=[
        v("Lifetime", "ASTLifetimeRef"),
        v("Type", "ASTType*"),
        v("Value", "ASTExprNode*", copy=False),
        v("AssociatedTyEqual", "std::pair<ASTPathNode, ASTType*>", copy=False),
        v("AssociatedTyBound", "std::pair<ASTPathNode, std::vector<TypeTraitPath>>", copy=False),
        v("AssociatedValueEqual", "std::pair<ASTPathNode, ASTExprNode*>", copy=False),
    ],
    extra="""
        ASTPathParamEnt clone() const;
        void fmt(stl::ZeroCopyOutput& os) const;
    """,
)
