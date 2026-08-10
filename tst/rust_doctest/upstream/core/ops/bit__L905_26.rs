// Extracted from library/core/src/ops/bit.rs:905
#![allow(unused)]
fn main() {
    let mut x: u8 = 5;
    x <<= 1;
    assert_eq!(x, 10);

    let mut x: u8 = 1;
    x <<= 1;
    assert_eq!(x, 2);
}
