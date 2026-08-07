// Extracted from library/core/src/num/wrapping.rs:657
#![allow(unused)]
#![feature(wrapping_int_impl)]
fn main() {
    use std::num::Wrapping;

    let n: Wrapping<i16> = Wrapping(0b0000000_01010101);
    assert_eq!(n, Wrapping(85));

    let m = n.swap_bytes();

    assert_eq!(m, Wrapping(0b01010101_00000000));
    assert_eq!(m, Wrapping(21760));
}
