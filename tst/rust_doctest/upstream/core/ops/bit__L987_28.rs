// Extracted from library/core/src/ops/bit.rs:987
#![allow(unused)]
fn main() {
    let mut x: u8 = 5;
    x >>= 1;
    assert_eq!(x, 2);

    let mut x: u8 = 2;
    x >>= 1;
    assert_eq!(x, 1);
}
