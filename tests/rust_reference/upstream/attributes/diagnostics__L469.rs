// Extracted from src/attributes/diagnostics.md:469
#![allow(unused)]
#![deny(unused_must_use)]
fn main() {
    #[must_use]
    fn f() {}
    
    { f() };        // ERROR: The lint looks through block expressions.
    unsafe { f() }; // ERROR: The lint looks through `unsafe` blocks.
    { { f() } };    // ERROR: The lint looks through nested blocks.
}
