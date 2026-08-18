# ast_path.h include point before ASTPath: the path class union, hoisted out
# of the class (a using-alias keeps the nested spelling).

context("ast_path.h")

generate(
    name="ASTPathClass",
    default="Invalid",
    clone=False,
    variants=[
        v("Invalid"),
        v("Local", fields=[
            ("RcString", "name"),
        ], doc="Variable / type param (resolved)"),
        v("Relative", fields=[
            ("Ident::Hygiene", "hygiene"),
            ("::std::vector<ASTPathNode>", "nodes"),
        ], copy=False, doc="General relative"),
        v("Self", fields=[
            ("::std::vector<ASTPathNode>", "nodes"),
        ], copy=False, doc="Module-relative"),
        v("Super", fields=[
            ("unsigned int", "count"),
            ("::std::vector<ASTPathNode>", "nodes"),
        ], copy=False, doc="Parent-relative; count is the number of `super`"
                           " keywords, must be >= 1"),
        v("Absolute", fields=[
            ("RcString", "crate"),
            ("::std::vector<ASTPathNode>", "nodes"),
        ], copy=False),
        v("UFCS", fields=[
            ("ASTType*", "type"),
            ("::std::unique_ptr<ASTPath>", "trait"),
            ("::std::vector<ASTPathNode>", "nodes"),
        ], copy=False, doc="Type-relative; type is always non-null, a nullptr"
                           " trait means inherent and an Invalid one an"
                           " unknown trait"),
    ],
)
