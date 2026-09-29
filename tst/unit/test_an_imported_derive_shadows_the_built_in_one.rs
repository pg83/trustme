//@ run-pass
//@ proc-macro-aux-build: imported_debug_derive.rs
// derive_more's tests write `use derive_more::{Eq, PartialEq};` and then
// `#[derive(Eq, PartialEq)]` with `#[eq(skip)]` fields. A derive's path is
// resolved like any macro path, and an import shadows the built-in derive of
// the standard prelude; the built-in `Eq` was taken whenever one had the
// name, and asserted `Eq` of a skipped field's type.
mod imported {
    use imported_debug_derive::Debug;

    #[derive(Debug)]
    pub struct Shadowed;

    pub fn which() -> &'static str {
        Shadowed::derived_by()
    }
}

#[derive(Debug)]
struct BuiltIn;

fn main() {
    assert_eq!(imported::which(), "imported");
    assert_eq!(format!("{:?}", BuiltIn), "BuiltIn");
}
