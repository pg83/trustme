# Sample unions exercising every dev/tu_gen.py feature.  Compiled only into
# the rustc_ut runner (see build.py), never into the compiler; the paired
# tagged_union_sample_ut.cpp asserts the generated semantics.

generate(
    name="SampleValue",
    default="Empty",
    clone=False,  # the Owner payload cannot be cloned
    doc="In-place storage: every payload type is complete at the include.",
    variants=[
        v("Empty"),
        v("Name", "::std::string"),
        v("Point", fields=[("int", "x"), ("int", "y", "7")]),
        v("Owner", "::std::unique_ptr<int>", copy=False,
          doc="Move-only payload: the copy constructor pair is not emitted."),
        v("Counted", "SampleCounted",
          doc="Instance-counting payload from the context header."),
    ],
    extra_fields=[("u8", "flags", "0")],
    extra="""
        int extraProbe() const;
    """,
)

generate(
    name="SampleTree",
    default="Nil",
    allow_incomplete=True,
    doc="""
        Pointer storage: SampleTreeNode is only declared before the include
        and holds this union by value — direct recursion.
    """,
    variants=[
        v("Nil"),
        v("Node", "SampleTreeNode", copy=False,
          doc="The node holds a SampleTree, so it is move-only."),
        v("Mark", "SampleCounted"),
    ],
)
