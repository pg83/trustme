//@ compile-fail: UFCS trait was not a trait
struct S;
fn main() { <u8 as S>::f(); }
