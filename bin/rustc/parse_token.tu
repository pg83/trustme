# The token payload, hoisted out of Token (a private using-alias keeps the
# nested spelling; the data member itself stays private).

generate(
    name="TokenData",
    default="None",
    clone=False,
    variants=[
        v("None"),
        v("Ident", "Ident"),
        v("String", "std::string"),
        v("Integer", fields=[
            ("enum eCoreType", "datatype"),
            ("U128", "intval"),
        ]),
        v("Float", fields=[
            ("enum eCoreType", "datatype"),
            ("FloatValue", "floatval"),
        ]),
        v("Fragment", "void*"),
    ],
)
