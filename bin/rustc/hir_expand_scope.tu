# Local to hir_expand_main_bindings.cpp: the closure/coroutine scope stack
# entry.

local()

generate(
    name="Scope",
    default="None",
    clone=False,
    variants=[
        v("None"),
        v("Closure", "ClosureScope", copy=False),
        v("Coroutine", "CoroutineScope", copy=False),
    ],
)
