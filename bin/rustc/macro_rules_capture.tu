# Local to macro_rules_macro_rules.cpp: one layer of the capture set,
# recursive by value through vectors of itself.

local()

generate(
    name="CaptureLayer",
    default="Vals",
    clone=False,
    output=True,
    variants=[
        v("Vals", "std::vector<CapturedVal>", copy=False),
        v("Nested", "std::vector<CaptureLayer>", copy=False),
    ],
)
