// Extracted from library/core/src/num/f16.rs:1410
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f16_math)] {
    
    let f = 3.7_f16;
    let g = 3.0_f16;
    let h = -3.7_f16;
    
    assert_eq!(f.floor(), 3.0);
    assert_eq!(g.floor(), 3.0);
    assert_eq!(h.floor(), -4.0);
    }
}
