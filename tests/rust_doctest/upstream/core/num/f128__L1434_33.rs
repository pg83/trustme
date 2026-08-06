// Extracted from library/core/src/num/f128.rs:1434
#![allow(unused)]
#![feature(f128)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f128_math)] {
    
    let f = 3.7_f128;
    let g = 3.0_f128;
    let h = -3.7_f128;
    
    assert_eq!(f.floor(), 3.0);
    assert_eq!(g.floor(), 3.0);
    assert_eq!(h.floor(), -4.0);
    }
}
