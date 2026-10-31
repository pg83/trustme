//@ compile-fail: invalid left-hand side of assignment
fn main() { (17u32.count_ones()) = 2u32; }
