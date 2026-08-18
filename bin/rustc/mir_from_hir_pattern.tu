# Local to mir_from_hir.cpp: one decision-tree rule derived from a pattern,
# recursive by value through vectors of itself.  clone() stays hand-written.

local()

generate(
    name="PatternRule",
    default="Any",
    clone=False,
    variants=[
        v("Variant", fields=[
            ("unsigned int", "idx"),
            ("::std::vector<PatternRule>", "subRules"),
        ], copy=False, doc="Enum variant"),
        v("Slice", fields=[
            ("unsigned int", "len"),
            ("::std::vector<PatternRule>", "subRules"),
        ], copy=False, doc="Slice (includes desired length)"),
        v("SplitSlice", fields=[
            ("unsigned int", "minLen"),
            ("unsigned int", "trailingLen"),
            ("::std::vector<PatternRule>", "leading"),
            ("::std::vector<PatternRule>", "trailing"),
        ], copy=False, doc="How can the negative offsets in the `trailing` be"
                           " handled correctly? (both here and in the"
                           " destructure)"),
        v("Bool", "bool",
          doc="Boolean (different to Constant because of how restricted it is)"),
        v("Value", "MIRConstant", copy=False, doc="General value"),
        v("ValueRange", fields=[
            ("MIRConstant", "first"),
            ("MIRConstant", "last"),
            ("bool", "isInclusive"),
        ], copy=False),
        v("Any", doc="_ pattern"),
    ],
    extra_fields=[
        ("fieldPathT", "fieldPath"),
        ("unsigned", "rootIndex", "0"),
    ],
    extra="""
        bool operator<(const PatternRule& x) const {
            return this->ord(x) == OrdLess;
        }
        bool operator==(const PatternRule& x) const {
            return this->ord(x) == OrdEqual;
        }
        bool operator!=(const PatternRule& x) const {
            return this->ord(x) != OrdEqual;
        }
        Ordering ord(const PatternRule& x) const;
        PatternRule clone() const;
    """,
)
