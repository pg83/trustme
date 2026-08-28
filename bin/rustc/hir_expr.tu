# The HIR expression-side unions, hoisted out of their node structs
# (using-aliases keep the nested spellings).

generate(
    name="HIRAsmParam",
    default="Const",
    clone=False,
    variants=[
        v("Const", "HIRExprNodeP", copy=False),
        v("Sym", "HIRPath", copy=False),
        v("Label", fields=[("HIRExprNodeP", "code")], copy=False),
        v("RegSingle", fields=[
            ("AsmDirection", "dir"),
            ("AsmRegisterSpec", "spec"),
            ("HIRExprNodeP", "val"),
        ], copy=False),
        v("Reg", fields=[
            ("AsmDirection", "dir"),
            ("AsmRegisterSpec", "spec"),
            ("HIRExprNodeP", "valIn"),
            ("HIRExprNodeP", "valOut"),
        ], copy=False),
    ],
)

generate(
    name="HIRExprLiteral",
    default="Integer",
    clone=False,
    variants=[
        v("Integer", fields=[
            ("HIRCoreType", "type"),
            ("U128", "value"),
        ], doc="if the type is not an integer type, it's unknown"),
        v("Float", fields=[
            ("HIRCoreType", "type"),
            ("FloatValue", "value"),
        ], doc="if the type is not a float type, it's unknown"),
        v("Boolean", "bool"),
        v("String", "std::string"),
        v("CString", fields=[("std::string", "v")]),
        v("ByteString", "std::vector<char>"),
    ],
)
