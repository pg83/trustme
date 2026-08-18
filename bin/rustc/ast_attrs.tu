# Attribute payload data, in-place storage.

generate(
    name="ASTAttributeData",
    default="None",
    variants=[
        v("None"),
        v("ValueUnexpanded", "ASTExprNodeP", copy=False),
        v("String", fields=[("::std::string", "val")]),
        v("List", fields=[("::std::vector<ASTAttribute>", "subItems")]),
    ],
)
