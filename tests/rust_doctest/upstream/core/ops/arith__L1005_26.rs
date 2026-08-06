// Extracted from library/core/src/ops/arith.rs:1005
#![allow(unused)]
fn main() {
    let mut x: u32 = 12;
    x %= 10;
    assert_eq!(x, 2);
}
