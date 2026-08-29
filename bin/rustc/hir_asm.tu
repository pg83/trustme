# An asm! register specifier, in-place storage.

generate(
    name="AsmRegisterSpec",
    default="Explicit",
    output=True,
    variants=[
        v("Class", "AsmRegisterClass"),
        v("Explicit", "std::string"),
    ],
)
