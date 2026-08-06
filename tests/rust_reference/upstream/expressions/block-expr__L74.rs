// Extracted from src/expressions/block-expr.md:74
#![allow(unused)]
fn main() {
    fn f() -> ! { loop {}; } // Diverges and has no final operand.
    //          ^^^^^^^^^^^^
    // The body of a function is a block expression.
}
