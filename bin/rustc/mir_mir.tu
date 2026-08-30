# The MIR value/statement/terminator unions, in dependency order behind one
# include point.  Every clone() here stays hand-written (mir_mir.cpp): they
# share interned refs and re-wrap pointers in ways a memberwise clone must
# not guess at.

generate(
    name="MIRConstant",
    default="Int",
    clone=False,
    output=True,
    doc="Compile-time known values",
    variants=[
        v("Int", fields=[("S128", "v"), ("HIRCoreType", "t")]),
        v("Uint", fields=[("U128", "v"), ("HIRCoreType", "t")]),
        v("Float", fields=[("FloatValue", "v"), ("HIRCoreType", "t")]),
        v("Bool", fields=[("bool", "v")],
          doc="The dedicated struct is defensive: it prevents implicit casts"),
        v("Bytes", "stl::Vector<u8>", doc="Byte string"),
        v("StaticString", "std::string", doc="String"),
        v("Const", fields=[("std::unique_ptr<HIRPath>", "p")], copy=False,
          doc="`const`. Behind a pointer to save inline space (HIRPath is ~11"
              " words, compared to 4 for MIRConstant without it)"),
        v("Generic", "HIRGenericRef"),
        v("Function", fields=[("std::unique_ptr<HIRPath>", "p")], copy=False,
          doc="ZST function type, NOT its address"),
        v("ItemAddr", "ItemAddress", copy=False,
          doc="Address within a named allocation"),
        v("Encoded", fields=[
            ("const HIRType*", "type"),
            ("EncodedLiteral", "value"),
        ], copy=False),
    ],
    extra="""
        ::Ordering ord(const MIRConstant& b) const;
        bool operator==(const MIRConstant& b) const {
            return ord(b) == ::OrdEqual;
        }
        bool operator!=(const MIRConstant& b) const {
            return ord(b) != ::OrdEqual;
        }
        bool operator<(const MIRConstant& b) const {
            return ord(b) == ::OrdLess;
        }
        bool operator<=(const MIRConstant& b) const {
            return ord(b) != ::OrdGreater;
        }
        bool operator>(const MIRConstant& b) const {
            return ord(b) == ::OrdGreater;
        }
        bool operator>=(const MIRConstant& b) const {
            return ord(b) != ::OrdLess;
        }
        MIRConstant clone() const;
    """,
)

generate(
    name="MIRParam",
    default="Constant",
    clone=False,
    output=True,
    doc="""
        Parameter - A value used when a rvalue just reads (doesn't require
        a lvalue).  Can be either a lvalue (memory address), or a constant.
    """,
    variants=[
        v("LValue", "MIRLValue", copy=False),
        v("Borrow", fields=[
            ("HIRBorrowType", "type"),
            ("MIRLValue", "val"),
        ], copy=False),
        v("Constant", "MIRConstant", copy=False),
    ],
    extra="""
        MIRParam clone() const;
        bool operator==(const MIRParam& b) const;
        bool operator!=(const MIRParam& b) const {
            return !(*this == b);
        }
    """,
)

generate(
    name="MIRRValue",
    default="Tuple",
    clone=False,
    output=True,
    variants=[
        v("Use", "MIRLValue", copy=False),
        v("Borrow", fields=[
            ("HIRBorrowType", "type"),
            ("bool", "isRaw"),
            ("MIRLValue", "val"),
        ], copy=False),
        v("Constant", "MIRConstant", copy=False),
        v("SizedArray", fields=[
            ("MIRParam", "val"),
            ("HIRArraySize", "count"),
        ], copy=False),
        v("Cast", fields=[
            ("MIRLValue", "val"),
            ("const HIRType*", "type"),
        ], copy=False, doc="Cast on primitives (thin pointers, integers, floats)"),
        v("BinOp", fields=[
            ("MIRParam", "valL"),
            ("MIRBinOp", "op"),
            ("MIRParam", "valR"),
        ], copy=False, doc="Binary operation on primitives"),
        v("UniOp", fields=[
            ("MIRLValue", "val"),
            ("MIRUniOp", "op"),
        ], copy=False, doc="Unary operation on primitives. The value is not a"
                           " param, because UniOps can be const propagated"),
        v("DstMeta", fields=[("MIRLValue", "val")], copy=False,
          doc="Extract the metadata from a DST pointer. If used on an array,"
              " this yields the array size (for generics)"),
        v("DstPtr", fields=[("MIRLValue", "val")], copy=False,
          doc="Extract the pointer from a DST pointer (as *const ())"),
        v("MakeDst", fields=[
            ("MIRParam", "ptrVal"),
            ("MIRParam", "metaVal"),
        ], copy=False, doc="Construct a DST pointer from a thin pointer and"
                           " metadata OR (if `metaVal` is"
                           " `Constant::ItemAddr(nullptr)`) a still-to-be-"
                           "resolved unsizing coercion"),
        v("Tuple", fields=[("std::vector<MIRParam>", "vals")], copy=False),
        v("Array", fields=[("std::vector<MIRParam>", "vals")], copy=False,
          doc="Array literal"),
        v("UnionVariant", fields=[
            ("HIRGenericPath", "path"),
            ("unsigned int", "index"),
            ("MIRParam", "val"),
        ], copy=False, doc="Create a new instance of a union"),
        v("EnumVariant", fields=[
            ("HIRGenericPath", "path"),
            ("unsigned int", "index"),
            ("std::vector<MIRParam>", "vals"),
        ], copy=False, doc="Create a new instance of an enum. Separate from"
                           " UnionVariant, as the contents is needed when"
                           " creating the body"),
        v("Struct", fields=[
            ("HIRGenericPath", "path"),
            ("std::vector<MIRParam>", "vals"),
        ], copy=False, doc="Create a new instance of a struct"),
    ],
    extra="""
        MIRRValue clone() const;
    """,
)

generate(
    name="MIRCallTarget",
    default="Intrinsic",
    clone=False,
    output=True,
    variants=[
        v("Value", "MIRLValue", copy=False),
        v("Path", "HIRPath", copy=False),
        v("Intrinsic", fields=[
            ("RcString", "name"),
            ("HIRPathParams", "params"),
        ], copy=False),
    ],
)

generate(
    name="MIRSwitchValues",
    default="Unsigned",
    clone=False,
    output=True,
    variants=[
        v("Unsigned", "stl::Vector<u64>"),
        v("Signed", "stl::Vector<i64>"),
        v("String", "std::vector<std::string>"),
        v("ByteString", "std::vector<stl::Vector<u8>>"),
    ],
    extra="""
        MIRSwitchValues clone() const;
        bool operator==(const MIRSwitchValues& x) const;
        bool operator!=(const MIRSwitchValues& x) const {
            return !(*this == x);
        }
    """,
)

generate(
    name="MIRUnwindAction",
    default="Continue",
    clone=False,
    output=True,
    variants=[
        v("Continue"),
        v("Cleanup", "MIRBasicBlockId"),
        v("Terminate"),
        v("Unreachable"),
    ],
)

generate(
    name="MIRAsmParam",
    default="Const",
    clone=False,
    output=True,
    variants=[
        v("Const", "MIRConstant", copy=False),
        v("Sym", "HIRPath", copy=False),
        v("Reg", fields=[
            ("AsmDirection", "dir"),
            ("AsmRegisterSpec", "spec"),
            ("std::unique_ptr<MIRParam>", "input"),
            ("std::unique_ptr<MIRLValue>", "output"),
        ], copy=False),
        v("Label", "MIRBasicBlockId"),
    ],
)

generate(
    name="MIRTerminator",
    default="Incomplete",
    clone=False,
    output=True,
    variants=[
        v("Incomplete", doc="Block isn't complete (ERROR in output)"),
        v("Return", doc="Return cleanly to caller"),
        v("UnwindResume", doc="Resume the currently caught exception"),
        v("UnwindTerminate", doc="Abort if unwinding reaches this point"),
        v("Unreachable", doc="This control-flow edge cannot be reached"),
        v("Goto", "MIRBasicBlockId", doc="Jump to another block"),
        v("If", fields=[
            ("MIRLValue", "cond"),
            ("MIRBasicBlockId", "bbTrue"),
            ("MIRBasicBlockId", "bbFalse"),
        ], copy=False),
        v("Switch", fields=[
            ("MIRLValue", "val"),
            ("stl::Vector<MIRBasicBlockId>", "targets"),
            ("unsigned int", "validFlag", "~0u"),
            ("MIRBasicBlockId", "invalidTarget", "~0u"),
        ], copy=False),
        v("SwitchValue", fields=[
            ("MIRLValue", "val"),
            ("MIRBasicBlockId", "defTarget"),
            ("stl::Vector<MIRBasicBlockId>", "targets"),
            ("MIRSwitchValues", "values"),
        ], copy=False),
        v("Drop", fields=[
            ("MIRDropKind", "kind"),
            ("MIRLValue", "slot"),
            ("unsigned int", "flagIdx"),
            ("MIRBasicBlockId", "target"),
            ("MIRUnwindAction", "unwind"),
        ], copy=False),
        v("Call", fields=[
            ("MIRBasicBlockId", "retBlock"),
            ("MIRUnwindAction", "unwind"),
            ("MIRLValue", "retVal"),
            ("MIRCallTarget", "fcn"),
            ("std::vector<MIRParam>", "args"),
            ("SourceLocation", "source"),
            ("bool", "tracksCaller", "false"),
        ], copy=False),
        v("TailCall", fields=[
            ("MIRCallTarget", "fcn"),
            ("std::vector<MIRParam>", "args"),
            ("SourceLocation", "source"),
            ("bool", "tracksCaller", "false"),
        ], copy=False),
        v("Asm2", fields=[
            ("AsmOptions", "options"),
            ("std::vector<AsmLine>", "lines"),
            ("std::vector<MIRAsmParam>", "params"),
            ("MIRBasicBlockId", "retBlock"),
        ], copy=False, doc="Inline assembly with label operands. Unlike"
                           " statement-form Asm2, this is a terminator because"
                           " the assembly can branch to any Label parameter"),
    ],
)

generate(
    name="MIRStatement",
    default="Asm",
    clone=False,
    output=True,
    variants=[
        v("Assign", fields=[
            ("MIRLValue", "dst"),
            ("MIRRValue", "src"),
        ], copy=False, doc="Value assignment"),
        v("Asm", fields=[
            ("std::string", "tpl"),
            ("std::vector<std::pair<std::string, MIRLValue>>", "outputs"),
            ("std::vector<std::pair<std::string, MIRLValue>>", "inputs"),
            ("std::vector<std::string>", "clobbers"),
            ("std::vector<std::string>", "flags"),
        ], copy=False, doc="Inline assembly (`llvm_asm!`)"),
        v("Asm2", fields=[
            ("AsmOptions", "options"),
            ("std::vector<AsmLine>", "lines"),
            ("std::vector<MIRAsmParam>", "params"),
        ], copy=False, doc="Inline assembly (stabilised)"),
        v("SetDropFlag", fields=[
            ("unsigned int", "idx"),
            ("bool", "newVal"),
            ("unsigned int", "other"),
        ], doc="Update the state of a drop flag. If `other` is not `~0u`, it"
               " names another flag whose (possibly negated via `newVal`)"
               " value is copied; otherwise `newVal` is stored"),
        v("SaveDropFlag", fields=[
            ("MIRLValue", "slot"),
            ("unsigned int", "bitIndex"),
            ("unsigned int", "idx"),
        ], copy=False, doc="Save drop flag `idx` into bit `bitIndex` of the"
                           " bit-set array `slot` (nominally `u8`s)"),
        v("LoadDropFlag", fields=[
            ("unsigned int", "idx"),
            ("MIRLValue", "slot"),
            ("unsigned int", "bitIndex"),
        ], copy=False, doc="Load drop flag `idx` from bit `bitIndex` of the"
                           " bit-set array `slot` (nominally `u8`s)"),
        v("ScopeEnd", fields=[("stl::Vector<unsigned>", "slots")]),
    ],
)
