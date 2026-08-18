# A resolved macro reference: pointer payloads, in-place storage.

generate(
    name="MacroRef",
    default="None",
    variants=[
        v("None"),
        v("MacroRules", "const MacroRules*"),
        v("BuiltinProcMacro", "ExpandProcMacro*"),
        v("ExternalProcMacro", "const HIRProcMacro*"),
    ],
)
