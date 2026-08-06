// Extracted from src/expressions/loop-expr.md:186
#![allow(unused)]
fn main() {
    let mut sum = 0;
    for n in 1..11 {
        sum += n;
    }
    assert_eq!(sum, 55);
}
