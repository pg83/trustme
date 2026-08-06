// Extracted from library/core/src/num/uint_macros.rs:2732
#![allow(unused)]
#![feature(bigint_helper_methods)]
fn main() {
    let r = u8::carrying_mul(7, 13, 0);
    assert_eq!((r.0, r.1 != 0), u8::overflowing_mul(7, 13));
    let r = u8::carrying_mul(13, 42, 0);
    assert_eq!((r.0, r.1 != 0), u8::overflowing_mul(13, 42));
}
