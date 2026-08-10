// Extracted from src/expressions/operator-expr.md:655
#![allow(unused)]
fn main() {
    enum Enum { A, B, C }
    assert_eq!(Enum::A as i32, 0);
    assert_eq!(Enum::B as i32, 1);
    assert_eq!(Enum::C as i32, 2);
}
