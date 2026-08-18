# The validity state of a value slot during MIR lowering.  Recursive by
# value through vectors/maps/unique_ptrs of itself; clone() stays
# hand-written.

generate(
    name="VarState",
    default="Invalid",
    clone=False,
    variants=[
        v("Invalid", "InvalidType", doc="Currently invalid"),
        v("Partial", fields=[
            ("::std::vector<VarState>", "innerStates"),
            ("unsigned int", "outerFlag"),
        ], copy=False, doc="Partially valid (map of field states). An"
                           " outerFlag of ~0u means the outer discriminant"
                           " is always valid"),
        v("PartialArray", fields=[
            ("::std::unique_ptr<VarState>", "fillState"),
            ("::std::map<unsigned, VarState>", "otherStates"),
            ("size_t", "count"),
        ], copy=False, doc="Partially valid large array: a shared state for"
                           " untouched elements plus per-index exceptions."
                           " Avoids materialising one state per element"
                           " (fatal for e.g."
                           " `let [a, ..] = [String::new(); 64_000_000]`)"),
        v("MovedOut", fields=[
            ("::std::unique_ptr<VarState>", "innerState"),
            ("unsigned int", "outerFlag"),
        ], copy=False, doc="An outerFlag of ~0u means the outer is always"
                           " valid; otherwise the outer may have been moved"
                           " (but the inner state can still be valid)"),
        v("Optional", "unsigned int",
          doc="Optionally valid (the drop flag index)"),
        v("Valid", doc="Fully valid"),
    ],
    extra="""
        VarState clone() const;
        bool operator==(const VarState& x) const;
        bool operator!=(const VarState& x) const {
            return !(*this == x);
        }
        /// Returns `true` if any drop flags were present (i.e. this is possibly optional)
        bool getUsedDropFlags(std::set<unsigned>* out) const;
    """,
)
