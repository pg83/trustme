# Attribute payload data, in-place storage.

generate(
    name="ASTAttributeData",
    default="None",
    clone=False,  # AST expression pointers require a deep clone
    variants=[
        v("None"),
        v("ValueUnexpanded", "ASTExprNode*", copy=False),
        v("String", fields=[("std::string", "val")]),
        v("List", fields=[("std::vector<ASTAttribute>", "subItems")]),
    ],
)
