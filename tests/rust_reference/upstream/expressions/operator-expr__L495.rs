// Extracted from src/expressions/operator-expr.md:495
#![allow(unused)]
fn main() {
    let x = false || true; // true
    let y = false && panic!(); // false, doesn't evaluate `panic!()`
}
