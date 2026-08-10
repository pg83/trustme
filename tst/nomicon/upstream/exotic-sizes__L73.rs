// Extracted from src/exotic-sizes.md:73
#![allow(unused)]
fn main() {
    struct Nothing; // No fields = no size
    
    // All fields have no size = no size
    struct LotsOfNothing {
        foo: Nothing,
        qux: (),      // empty tuple has no size
        baz: [u8; 0], // empty array has no size
    }
}
