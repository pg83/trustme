# An asm! register specifier, in-place storage.

generate(
    name="AsmRegisterSpec",
    default="Explicit",
    variants=[
        v("Class", "AsmRegisterClass"),
        v("Explicit", "std::string"),
    ],
)
