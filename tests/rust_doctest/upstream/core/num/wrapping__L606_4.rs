// Extracted from library/core/src/num/wrapping.rs:606
#![allow(unused)]
#![feature(wrapping_int_impl)]
fn main() {
    use std::num::Wrapping;
    
    let n: Wrapping<i64> = Wrapping(0x0123456789ABCDEF);
    let m: Wrapping<i64> = Wrapping(-0x76543210FEDCBA99);
    
    assert_eq!(n.rotate_left(32), m);
}
