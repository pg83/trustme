// Extracted from library/core/src/num/saturating.rs:640
#![allow(unused)]
fn main() {
    use std::num::Saturating;
    
    let n: Saturating<i16> = Saturating(0b0000000_01010101);
    assert_eq!(n, Saturating(85));
    
    let m = n.swap_bytes();
    
    assert_eq!(m, Saturating(0b01010101_00000000));
    assert_eq!(m, Saturating(21760));
}
