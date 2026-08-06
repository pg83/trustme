// Extracted from src/attributes/diagnostics.md:425
#![allow(unused)]
#![deny(unused_must_use)]
fn main() {
    #[must_use]
    trait Tr {}
    impl Tr for () {}
    fn f() -> impl Tr {}
    f(); // ERROR: Unused implementor that must be used.
}
