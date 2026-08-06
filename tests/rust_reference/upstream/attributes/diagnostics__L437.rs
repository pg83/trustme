// Extracted from src/attributes/diagnostics.md:437
#![allow(unused)]
#![deny(unused_must_use)]
fn main() {
    trait Tr {
        #[must_use]
        fn use_me(&self);
    }
    
    impl Tr for () {
        fn use_me(&self) {}
    }
    
    ().use_me(); // ERROR: Unused return value that must be used.
}
