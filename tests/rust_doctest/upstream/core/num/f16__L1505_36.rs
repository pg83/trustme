// Extracted from library/core/src/num/f16.rs:1505
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f16_math)] {
    
    let f = 3.3_f16;
    let g = -3.3_f16;
    let h = 3.5_f16;
    let i = 4.5_f16;
    
    assert_eq!(f.round_ties_even(), 3.0);
    assert_eq!(g.round_ties_even(), -3.0);
    assert_eq!(h.round_ties_even(), 4.0);
    assert_eq!(i.round_ties_even(), 4.0);
    }
}
