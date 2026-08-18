# Attribute payload data, in-place storage.

generate(
    name="ASTAttributeData",
    default="None",
    clone=False,  # ASTExprNodeP::clone() returns ASTExpr, not a new node ptr
    variants=[
        v("None"),
        v("ValueUnexpanded", "ASTExprNodeP", copy=False),
        v("String", fields=[("::std::string", "val")]),
        v("List", fields=[("::std::vector<ASTAttribute>", "subItems")]),
    ],
)
