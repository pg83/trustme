// Extracted from library/core/src/num/f128.rs:1494
#![allow(unused)]
#![feature(f128)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f128_math)] {
    
    let f = 3.3_f128;
    let g = -3.3_f128;
    let h = -3.7_f128;
    let i = 3.5_f128;
    let j = 4.5_f128;
    
    assert_eq!(f.round(), 3.0);
    assert_eq!(g.round(), -3.0);
    assert_eq!(h.round(), -4.0);
    assert_eq!(i.round(), 4.0);
    assert_eq!(j.round(), 5.0);
    }
}
