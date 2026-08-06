// Extracted from library/core/src/num/saturating.rs:667
#![allow(unused)]
fn main() {
    use std::num::Saturating;
    
    let n = Saturating(0b0000000_01010101i16);
    assert_eq!(n, Saturating(85));
    
    let m = n.reverse_bits();
    
    assert_eq!(m.0 as u16, 0b10101010_00000000);
    assert_eq!(m, Saturating(-22016));
}
