// Extracted from library/core/src/num/saturating.rs:619
#![allow(unused)]
fn main() {
    use std::num::Saturating;
    
    let n: Saturating<i64> = Saturating(0x0123456789ABCDEF);
    let m: Saturating<i64> = Saturating(-0xFEDCBA987654322);
    
    assert_eq!(n.rotate_right(4), m);
}
