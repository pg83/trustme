// Extracted from library/core/src/num/f16.rs:1470
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f16_math)] {
    
    let f = 3.3_f16;
    let g = -3.3_f16;
    let h = -3.7_f16;
    let i = 3.5_f16;
    let j = 4.5_f16;
    
    assert_eq!(f.round(), 3.0);
    assert_eq!(g.round(), -3.0);
    assert_eq!(h.round(), -4.0);
    assert_eq!(i.round(), 4.0);
    assert_eq!(j.round(), 5.0);
    }
}
