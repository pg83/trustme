//@ compile-fail: panic in constant evaluation
const VALUE: i32 = 1 / 0;
fn main() {}
