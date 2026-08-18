# The asm! operand union, hoisted out of ASTExprNodeAsm2 (a using-alias
# keeps the nested spelling).

generate(
    name="ASTAsmParam",
    default="Const",
    clone=False,
    variants=[
        v("Const", "ASTExprNodeP", copy=False),
        v("Sym", "ASTPath"),
        v("Label", fields=[("ASTExprNodeP", "code")], copy=False),
        v("RegSingle", fields=[
            ("AsmDirection", "dir"),
            ("AsmRegisterSpec", "spec"),
            ("ASTExprNodeP", "val"),
        ], copy=False),
        v("Reg", fields=[
            ("AsmDirection", "dir"),
            ("AsmRegisterSpec", "spec"),
            ("ASTExprNodeP", "valIn"),
            ("ASTExprNodeP", "valOut"),
        ], copy=False),
    ],
)
