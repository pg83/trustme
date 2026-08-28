# AST item-side unions.  One include point (before ASTEnum): the two
# struct-shape unions and the asm operand store in place, ASTItem stores
# through a pointer (allow_incomplete) so its item-class payloads only need
# forward declarations at the include.

generate(
    name="ASTEnumVariantData",
    default="Unit",
    variants=[
        v("Unit"),
        v("Tuple", fields=[("std::vector<ASTTupleItem>", "items")]),
        v("Struct", fields=[("std::vector<ASTStructItem>", "fields")]),
    ],
)

generate(
    name="ASTStructData",
    default="Struct",
    variants=[
        v("Unit"),
        v("Tuple", fields=[("std::vector<ASTTupleItem>", "ents")]),
        v("Struct", fields=[("std::vector<ASTStructItem>", "ents")]),
    ],
)

generate(
    name="ASTGlobalAsmOperand",
    default="Const",
    clone=False,  # ASTExprNodeP::clone() returns ASTExpr, not a new node ptr
    variants=[
        v("Const", "ASTExprNodeP", copy=False),
        v("Sym", "ASTPath"),
    ],
)

generate(
    name="ASTItem",
    default="None",
    allow_incomplete=True,
    clone=False,  # several variants deliberately TODO on clone; body is hand-written
    variants=[
        v("None"),
        v("MacroInv", "ASTMacroInvocation", copy=False),
        v("Use", "ASTUseItem", copy=False),
        v("ExternBlock", "ASTExternBlock", copy=False, doc="Nameless item: extern block"),
        v("GlobalAsm", "ASTGlobalAsm", copy=False, doc="Nameless item: global asm"),
        v("Impl", "ASTImpl", copy=False, doc="Nameless item: impl"),
        v("NegImpl", "ASTImplDef", copy=False, doc="Nameless item: negative impl"),
        v("Macro", "MacroRulesPtr", copy=False),
        v("Module", "ASTModule", copy=False),
        v("Crate", fields=[("RcString", "name")]),
        v("Type", "ASTTypeAlias", copy=False),
        v("Struct", "ASTStruct", copy=False),
        v("Enum", "ASTEnum", copy=False),
        v("Union", "ASTUnion", copy=False),
        v("Trait", "ASTTrait", copy=False),
        v("TraitAlias", "ASTTraitAlias", copy=False),
        v("Function", "ASTFunction", copy=False),
        v("Static", "ASTStatic", copy=False),
    ],
    extra="""
        ASTItem clone() const;
    """,
)
