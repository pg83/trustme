// Extracted from src/expressions/operator-expr.md:354
#![allow(unused)]
fn main() {
    let x = 6;
    assert_eq!(-x, -6);
    assert_eq!(!x, -7);
    assert_eq!(true, !false);
}
