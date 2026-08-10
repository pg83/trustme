// Extracted from src/attributes/diagnostics.md:482
#![allow(unused)]
#![deny(unused_must_use)]
fn main() {
    trait Tr {
        fn f(&self);
    }
    
    impl Tr for () {
        #[must_use] // This has no effect.
        fn f(&self) {}
    }
    
    ().f(); // OK.
}
