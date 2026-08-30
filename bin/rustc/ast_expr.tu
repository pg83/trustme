# The asm! operand union, hoisted out of ASTExprNodeAsm2 (a using-alias
# keeps the nested spelling).

generate(
    name="ASTAsmParam",
    default="Const",
    clone=False,
    variants=[
        v("Const", "ASTExprNode*", copy=False),
        v("Sym", "ASTPath"),
        v("Label", fields=[("ASTExprNode*", "code")], copy=False),
        v("RegSingle", fields=[
            ("AsmDirection", "dir"),
            ("AsmRegisterSpec", "spec"),
            ("ASTExprNode*", "val"),
        ], copy=False),
        v("Reg", fields=[
            ("AsmDirection", "dir"),
            ("AsmRegisterSpec", "spec"),
            ("ASTExprNode*", "valIn"),
            ("ASTExprNode*", "valOut"),
        ], copy=False),
    ],
)
