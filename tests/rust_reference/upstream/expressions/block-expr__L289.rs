// Extracted from src/expressions/block-expr.md:289
#![allow(unused)]
fn main() {
    if false {
        // The panic may or may not occur when the program is built.
        const { panic!(); }
    }
}
