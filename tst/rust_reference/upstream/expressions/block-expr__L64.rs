// Extracted from src/expressions/block-expr.md:64
#![allow(unused)]
fn main() {
    let x: () = {}; // Has no final operand.
    assert_eq!(x, ());
    let x: () = { 0u8; }; // As above.
    assert_eq!(x, ());
}
