// Extracted from src/expressions/grouped-expr.md:24
#![allow(unused)]
fn main() {
    let x: i32 = 2 + 3 * 4; // not parenthesized
    let y: i32 = (2 + 3) * 4; // parenthesized
    assert_eq!(x, 14);
    assert_eq!(y, 20);
}
