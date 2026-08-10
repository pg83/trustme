// Extracted from src/expressions/range-expr.md:54
#![allow(unused)]
fn main() {
    let x = std::ops::Range {start: 0, end: 10};
    let y = 0..10;
    
    assert_eq!(x, y);
}
