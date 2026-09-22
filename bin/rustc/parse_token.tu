# The token payload, hoisted out of Token (a private using-alias keeps the
# nested spelling; the data member itself stays private).

generate(
    name="TokenData",
    default="None",
    clone=False,
    variants=[
        v("None"),
        v("Ident", "Ident"),
        v("String", fields=[
            ("std::string", "value"),
            ("RcString", "spelling"),
        ]),
        v("Integer", fields=[
            ("enum eCoreType", "datatype"),
            ("U128", "intval"),
            ("RcString", "spelling"),
        ]),
        v("Float", fields=[
            ("enum eCoreType", "datatype"),
            ("FloatValue", "floatval"),
            ("RcString", "spelling"),
        ]),
        v("Fragment", "void*"),
    ],
)
