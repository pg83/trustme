# The AST pattern unions, hoisted out of ASTPattern (using-aliases keep the
# nested spellings).

generate(
    name="ASTPatternValue",
    default="Invalid",
    clone=False,
    output=True,
    variants=[
        v("Invalid"),
        v("Integer", fields=[
            ("enum eCoreType", "type"),
            ("U128", "value"),
        ], doc="Signed numbers are encoded as 2's complement"),
        v("Float", fields=[
            ("enum eCoreType", "type"),
            ("FloatValue", "value"),
        ]),
        v("String", "std::string"),
        v("ByteString", fields=[("std::string", "v")]),
        v("Named", "ASTPath"),
    ],
)

generate(
    name="ASTPatternData",
    default="Any",
    clone=False,
    variants=[
        v("MaybeBind", fields=[("Ident", "name")]),
        v("Macro", fields=[
            ("unique_ptr<ASTMacroInvocation>", "inv"),
        ], copy=False),
        v("Any"),
        v("Never", doc="`!`, which matches a value of an uninhabited type. No"
                       " such value exists, so the arm or binding it appears"
                       " in is never reached."),
        v("Box", fields=[("unique_ptr<ASTPattern>", "sub")], copy=False),
        v("Deref", fields=[("unique_ptr<ASTPattern>", "sub")], copy=False),
        v("Ref", fields=[
            ("bool", "mut"),
            ("unique_ptr<ASTPattern>", "sub"),
        ], copy=False),
        v("Guard", fields=[
            ("unique_ptr<ASTPattern>", "sub"),
            ("ASTExprNode*", "cond"),
        ], copy=False, doc="`pat if expr`, which matches only when the"
                           " expression holds. It is lifted into the arm's"
                           " guard before anything but expansion sees it."),
        v("Value", fields=[
            ("ASTPatternValue", "start"),
            ("ASTPatternValue", "end"),
        ], copy=False),
        v("ValueLeftInc", fields=[
            ("ASTPatternValue", "start"),
            ("ASTPatternValue", "end"),
        ], copy=False),
        v("Tuple", "ASTPatternTuplePat", copy=False),
        v("StructTuple", fields=[
            ("ASTPath", "path"),
            ("ASTPatternTuplePat", "tupPat"),
        ], copy=False),
        v("Struct", fields=[
            ("ASTPath", "path"),
            ("std::vector<ASTStructPatternEntry>", "subPatterns"),
            ("bool", "isExhaustive"),
        ], copy=False),
        v("Slice", fields=[
            ("std::vector<ASTPattern>", "subPats"),
        ], copy=False),
        v("SplitSlice", fields=[
            ("std::vector<ASTPattern>", "leading"),
            ("ASTPatternBinding", "extraBind"),
            ("std::vector<ASTPattern>", "trailing"),
            ("bool", "extraRest", "false"),
        ], copy=False, doc="extraRest records a second `..`, which parses but"
                           " means nothing. Only code that is kept has to be"
                           " rejected, so the diagnostic waits for lowering."),
        v("Or", "std::vector<ASTPattern>", copy=False),
    ],
)
