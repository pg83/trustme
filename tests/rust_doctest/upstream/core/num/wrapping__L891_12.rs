// Extracted from library/core/src/num/wrapping.rs:891
#![allow(unused)]
#![feature(wrapping_int_impl)]
fn main() {
    use std::num::Wrapping;
    
    
    
    
    assert_eq!(Wrapping(-128i8).abs().0 as u8, 128u8);
}
