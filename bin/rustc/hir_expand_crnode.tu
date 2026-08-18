# Local to hir_expand_main_bindings.cpp: which coroutine-ish node a scope
# tracks.

local()

generate(
    name="CRNode",
    default="Generator",
    clone=False,
    variants=[
        v("Generator", "HIRExprNodeGenerator*"),
        v("Async", "HIRExprNodeAsyncBlock*"),
    ],
)
