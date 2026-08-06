// Extracted from library/core/src/num/f128.rs:1529
#![allow(unused)]
#![feature(f128)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f128_math)] {
    
    let f = 3.3_f128;
    let g = -3.3_f128;
    let h = 3.5_f128;
    let i = 4.5_f128;
    
    assert_eq!(f.round_ties_even(), 3.0);
    assert_eq!(g.round_ties_even(), -3.0);
    assert_eq!(h.round_ties_even(), 4.0);
    assert_eq!(i.round_ties_even(), 4.0);
    }
}
