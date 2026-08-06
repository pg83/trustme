// Extracted from library/core/src/num/wrapping.rs:835
#![allow(unused)]
#![feature(wrapping_int_impl)]
fn main() {
    use std::num::Wrapping;
    
    assert_eq!(Wrapping(3i8).pow(5), Wrapping(-13));
    assert_eq!(Wrapping(3i8).pow(6), Wrapping(-39));
}
