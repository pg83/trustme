# Second hir_type.h include point: the erased-type inner union, needed by
# struct HIRTypeDataErasedType right after it.  Sub-unit of hir_type.h.

context("hir_type.h")

generate(
    name="TypeDataErasedTypeInner",
    default="Alias",
    clone=False,
    output=True,
    variants=[
        v("Fcn", fields=[
            ("HIRPath", "origin"),
            ("unsigned int", "index"),
        ], copy=False),
        v("Known", "HIRTypeRef"),
        v("Alias", fields=[
            ("HIRPathParams", "params"),
            ("std::shared_ptr<HIRTypeDataErasedTypeAliasInner>", "inner"),
        ], copy=False),
    ],
)

generate(
    name="HIRTypeDataNamedFunctionTy",
    default="Function",
    clone=False,
    output=True,
    variants=[
        v("Function", "const HIRFunction*"),
        v("EnumConstructor", fields=[
            ("const HIREnum*", "e"),
            ("size_t", "v"),
        ]),
        v("StructConstructor", "const HIRStruct*"),
    ],
    extra="""
        HIRTypeDataNamedFunctionTy clone() const;
    """,
)

generate(
    name="HIRTypeDataNodeType",
    default="Closure",
    clone=False,
    output=True,
    doc='"magic structs": any type generated from a node',
    variants=[
        v("Closure", "const HIRExprNodeClosure*"),
        v("Generator", "const HIRExprNodeGenerator*", doc="Aka a coroutine"),
        v("Async", "const HIRExprNodeAsyncBlock*"),
    ],
    extra="""
        bool operator==(const HIRTypeDataNodeType& x) const;
        bool operator!=(const HIRTypeDataNodeType& x) const {
            return !(*this == x);
        }
        Ordering ord(const HIRTypeDataNodeType& x) const;
        HIRTypeDataNodeType clone() const;
        void fmt(stl::ZeroCopyOutput& os) const;
    """,
)
