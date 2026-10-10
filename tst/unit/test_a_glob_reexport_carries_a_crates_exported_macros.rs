//@ aux-build: glob_reexported_macros.rs
// const_format's `pub mod __cf_osRcTFl4A { pub use crate::*; }` re-exports the
// crate root, and its proc macros' output calls `__cf_osRcTFl4A::__concatcp_inner!`
// from the user's crate (oci-spec). A `#[macro_export]` macro is a public item of
// the crate root, so the glob re-exports it; we indexed a root `macro_rules!`
// with its textual, private visibility, the glob kept that, and the other crate
// skipped the re-export as not public.
fn main() {
    assert_eq!(glob_reexported_macros::outer!(3), 6);
    assert_eq!(glob_reexported_macros::__hidden::inner!(4), 8);
    let v = {
        use glob_reexported_macros::__hidden;
        __hidden::inner!(5)
    };
    assert_eq!(v, 10);
}
