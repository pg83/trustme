# macro_rules! expansion and matcher entries, in-place storage.  All three
# unions live behind one include point, after MacroPatEnt and
# SimplePatIfCheck are complete.

generate(
    name="MacroExpansionConcatEnt",
    default="Named",
    variants=[
        v("Named", "unsigned int"),
        v("Ident", "Ident"),
    ],
)

generate(
    name="MacroExpansionEnt",
    default="Token",
    variants=[
        v("Token", "Token"),
        v("NamedValue", "unsigned int",
          doc="A 2:30 bitfield - with the high range indicating $crate"),
        v("Concat", "std::vector<MacroExpansionConcatEnt>", copy=False),
        v("Loop", fields=[
            ("::std::vector<MacroExpansionEnt>", "entries"),
            ("Token", "joiner"),
            ("::std::set<unsigned int>", "controllingInputLoops"),
        ], copy=False, doc="A repetition: contained entries, the joiner token, and the"
               " loop indexes that control the iteration"),
    ],
)

generate(
    name="SimplePatEnt",
    default="End",
    variants=[
        v("End", doc="End of the pattern stream (expects EOF, and terminates"
                     " the match process)"),
        v("LoopStart", fields=[("unsigned", "index")],
          doc="Start a loop (pushes a zero count to the loop stack)"),
        v("LoopNext", doc="Increment loop iteration counter"),
        v("LoopEnd", doc="Pop from the loop stack"),
        v("Jump", fields=[("size_t", "jumpTarget")],
          doc="Jump to a new point of execution"),
        v("ExpectTok", "Token",
          doc="Expect a specific token, erroring/failing the arm if not met"),
        v("ExpectPat", fields=[
            ("MacroPatEnt::Type", "type"),
            ("unsigned int", "idx"),
        ], doc="Expect a pattern match"),
        v("If", fields=[
            ("bool", "isEqual"),
            ("size_t", "jumpTarget"),
            ("::std::vector<SimplePatIfCheck>", "ents"),
        ], doc="Compare the head of the input stream and poke the pattern"
               " stream"),
    ],
)
