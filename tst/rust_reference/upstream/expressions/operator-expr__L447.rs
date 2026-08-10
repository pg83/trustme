// Extracted from src/expressions/operator-expr.md:447
#![allow(unused)]
fn main() {
    let a = 1;
    let b = 1;
    a == b;
    // is equivalent to
    ::std::cmp::PartialEq::eq(&a, &b);
}
