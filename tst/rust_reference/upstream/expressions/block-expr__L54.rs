// Extracted from src/expressions/block-expr.md:54
#![allow(unused)]
fn main() {
    let x: u8 = { 0u8 }; // `0u8` is the final operand.
    assert_eq!(x, 0);
    let x: u8 = { (); 0u8 }; // As above.
    assert_eq!(x, 0);
}
