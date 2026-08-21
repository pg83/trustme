//@ crate-type: lib
//@ compile-fail: type annotations needed

fn inferred_default() -> impl Sized {
    Default::default()
}
