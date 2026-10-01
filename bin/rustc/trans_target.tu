# How an enum encodes its active variant, hoisted out of TypeRepr
# (using-aliases keep the nested spellings).

generate(
    name="TypeReprVariantMode",
    default="None",
    clone=False,
    variants=[
        v("None"),
        v("Single", fields=[
            ("unsigned", "index"),
        ], doc="The enum is laid out as its one present variant"),
        v("Linear", "TypeReprVariantLinear",
          doc="Variants numbered 0 to N (potentially offset)"),
        v("Values", "TypeReprVariantValues",
          doc="Tag is a fixed set of values in a field"),
        v("NonZero", fields=[
            ("TypeReprFieldPath", "field"),
            ("unsigned", "zeroVariant"),
        ], doc="Tag is a boolean based on if a region is zero/non-zero."
               " Only valid for two-element enums"),
    ],
)
