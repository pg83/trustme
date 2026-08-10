// Extracted from src/expressions.md:327
#![allow(unused)]
fn main() {
    let c = [1, 2, 3];
    let d = vec![1, 2, 3];
    let a: &[i32];
    let b: &[i32];
    a = &c;
    b = &d;
    // ...
    *a == *b;
    // Equivalent form:
    ::std::cmp::PartialEq::eq(&*a, &*b);
}
