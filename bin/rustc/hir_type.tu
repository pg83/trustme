# Third hir_type.h include point: the type-data unions, after every payload
# struct is complete.  HIRTypeData stays in place deliberately: instances are
# interned into the pool by HIRTypeInterner, and pointer storage would put a
# heap hop between the pool node and the data.

generate(
    name="HIRTypeData",
    default="Diverge",
    clone=False,
    output=True,
    variants=[
        v("Infer", "HIRTypeDataInfer"),
        v("Diverge"),
        v("Primitive", "HIRCoreType"),
        v("Path", "HIRTypeDataPath", copy=False),
        v("Generic", "HIRGenericRef"),
        v("TraitObject", "HIRTypeDataTraitObject", copy=False),
        v("ErasedType", "HIRTypeDataErasedType", copy=False),
        v("Array", fields=[
            ("HIRTypeRef", "inner"),
            ("HIRArraySize", "size"),
        ], copy=False),
        v("Slice", fields=[("HIRTypeRef", "inner")]),
        v("Tuple", "stl::Vector<HIRTypeRef>"),
        v("Borrow", fields=[
            ("HIRBorrowType", "type"),
            ("HIRTypeRef", "inner"),
        ]),
        v("Pointer", fields=[
            ("HIRBorrowType", "type"),
            ("HIRTypeRef", "inner"),
        ]),
        v("NamedFunction", "HIRTypeDataNamedFunction", copy=False),
        v("Function", "HIRTypeDataFunctionPointer", copy=False),
        v("NodeType", "HIRTypeDataNodeType", copy=False),
        v("Pattern", fields=[
            ("HIRTypeRef", "inner"),
            ("HIRTypePattern", "pattern"),
        ], copy=False),
    ],
    extra_fields=[
        ("u32", "flags", "0"),
        # Assigned by HIRTypeInterner::intern in creation order; 0 means
        # "not interned". The deterministic ordering key for interned types
        # (see ord(const HIRTypeData*, const HIRTypeData*)).
        ("u32", "uid", "0"),
    ],
    extra="""
        enum HIRTypeFlags : u32 {
            HAS_TYPE_INFER = 1u << 0,
            HAS_TYPE_PARAM = 1u << 1,
            HAS_UNEVALUATED_CONST = 1u << 3,
            HAS_ASSOCIATED_TYPE = 1u << 4,
            HAS_DEFERRED_CONST = 1u << 5,
        };

        bool hasTypeInfer() const {
            return flags & HAS_TYPE_INFER;
        }
        bool needsMonomorphisation() const {
            return flags & (HAS_TYPE_PARAM | HAS_UNEVALUATED_CONST | HAS_DEFERRED_CONST);
        }
        bool mayHaveAssociatedType() const {
            return flags & (HAS_ASSOCIATED_TYPE | HAS_TYPE_INFER);
        }

        HIRTypeData cloneData() const;
        void fmt(stl::ZeroCopyOutput& os) const;

        // Deliberately semantic relations. Plain ASTType* equality is pointer identity.
        bool equalsIgnoringRegions(HIRTypeRef x) const;
        Ordering ordIgnoringRegions(HIRTypeRef x) const;
        bool matchTestGenerics(const Span& sp, HIRTypeRef x, tCbResolveType resolvePlaceholder, HIRMatchGenerics& callback) const;
        HIRCompare matchTestGenericsFuzz(const Span& sp, HIRTypeRef x, tCbResolveType resolvePlaceholder, HIRMatchGenerics& callback) const;
        HIRCompare compareWithPlaceholders(const Span& sp, HIRTypeRef x, tCbResolveType resolvePlaceholder) const;
        const HIRSimplePath* getSortPath() const;
    """,
)
