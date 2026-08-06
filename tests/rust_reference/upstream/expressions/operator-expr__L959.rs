// Extracted from src/expressions/operator-expr.md:959
#![allow(unused)]
fn main() {
    let (mut a, mut b) = (0, 1);
    // Swap `a` and `b` using destructuring assignment.
    (b, a) = (a, b);
}
