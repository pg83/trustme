# mir_from_hir.h include point after the split/arm structs: what a lowering
# scope owns.  Sub-unit of mir_from_hir.h.

context("mir_from_hir.h")

generate(
    name="ScopeType",
    default="Owning",
    clone=False,
    variants=[
        v("Owning", fields=[
            ("bool", "isTemporary"),
            ("stl::Vector<unsigned int>", "slots"),
            ("stl::Vector<ScopeDropSlot>", "dropSlots"),
        ], doc="slots are the locals whose state is owned by this scope;"
               " dropSlots the locals and arguments in scheduled drop order"),
        v("Split", fields=[
            ("bool", "endStateValid", "false"),
            ("SplitEnd", "condState"),
            ("SplitEnd", "endState"),
            ("std::vector<SplitArm>", "arms"),
        ], copy=False),
        v("Loop", fields=[
            ("std::map<unsigned int, VarState>", "changedSlots"),
            ("std::map<unsigned int, VarState>", "changedArgs"),
            ("bool", "exitStateValid"),
            ("SplitEnd", "exitState"),
            ("MIRBasicBlockId", "entryBb"),
            ("stl::Vector<unsigned>", "dropFlags"),
        ], copy=False, doc="changedSlots/changedArgs hold the original state"
                           " for variables changed after exitStateValid is"
                           " true. Any drop flags allocated in the loop must"
                           " be re-initialised at the start of the loop (or"
                           " before a loopback)"),
        v("Freeze", fields=[
            ("bool", "unfrozen", "false"),
        ], doc="unfrozen records whether early exits may update outer state"),
    ],
)
