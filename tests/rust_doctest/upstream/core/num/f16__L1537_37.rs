// Extracted from library/core/src/num/f16.rs:1537
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f16_math)] {
    
    let f = 3.7_f16;
    let g = 3.0_f16;
    let h = -3.7_f16;
    
    assert_eq!(f.trunc(), 3.0);
    assert_eq!(g.trunc(), 3.0);
    assert_eq!(h.trunc(), -3.0);
    }
}
