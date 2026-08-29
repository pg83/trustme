# Generic parameters and bounds of an AST item, in-place storage.

generate(
    name="GenericParam",
    default="None",
    output=True,
    variants=[
        v("None"),
        v("Lifetime", "ASTLifetimeParam"),
        v("Type", "ASTTypeParam"),
        v("Value", "ASTValueParam"),
    ],
    extra_fields=[
        ("size_t", "boundsStart", "0"),
        ("size_t", "boundsEnd", "0"),
    ],
)

generate(
    name="ASTGenericBound",
    default="None",
    output=True,
    variants=[
        v("None"),
        v("Lifetime", fields=[
            ("ASTLifetimeRef", "test"),
            ("ASTLifetimeRef", "bound"),
        ], doc="Lifetime bound: 'test must be valid for 'bound"),
        v("TypeLifetime", fields=[
            ("ASTType*", "type"),
            ("ASTLifetimeRef", "bound"),
        ], deep=["type"], doc="Type lifetime bound"),
        v("IsTrait", fields=[
            ("Span", "span"),
            ("ASTHigherRankedBounds", "outerHrbs"),
            ("ASTType*", "type"),
            ("ASTHigherRankedBounds", "innerHrbs"),
            ("ASTPath", "trait"),
            ("ASTBoundConstness", "constness", "ASTBoundConstness::Never"),
        ], deep=["type"], doc="Standard trait bound: \"Type: [for<'a>] Trait\""),
        v("MaybeTrait", fields=[
            ("ASTType*", "type"),
            ("ASTPath", "trait"),
        ], deep=["type"], doc="Removed trait bound: \"Type: ?Trait\""),
        v("NotTrait", fields=[
            ("ASTType*", "type"),
            ("ASTPath", "trait"),
        ], deep=["type"], doc="Negative trait bound: \"Type: !Trait\""),
        v("Equality", fields=[
            ("ASTType*", "type"),
            ("ASTType*", "replacement"),
        ], deep=["type", "replacement"], doc="Type equality: \"Type = Replacement\""),
    ],
    extra_fields=[
        ("Span", "span"),
    ],
)
