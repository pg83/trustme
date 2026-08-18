# ast_path.h include point after ASTPathNode: a single path parameter entry.
# clone() stays hand-written (it deep-clones the ASTType pointers).

generate(
    name="ASTPathParamEnt",
    default="Null",
    clone=False,
    variants=[
        v("Null"),
        v("Lifetime", "ASTLifetimeRef"),
        v("Type", "ASTType*"),
        v("Value", "ASTExprNodeP", copy=False),
        v("AssociatedTyEqual", "::std::pair<ASTPathNode, ASTType*>", copy=False),
        v("AssociatedTyBound", "::std::pair<ASTPathNode, std::vector<TypeTraitPath>>", copy=False),
        v("AssociatedValueEqual", "::std::pair<ASTPathNode, ASTExprNodeP>", copy=False),
    ],
    extra="""
        ASTPathParamEnt clone() const;
        Ordering ord(const ASTPathParamEnt& x) const;
        void fmt(::std::ostream& os) const;
    """,
)
