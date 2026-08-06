// Extracted from library/core/src/num/f128.rs:1464
#![allow(unused)]
#![feature(f128)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f128_math)] {
    
    let f = 3.01_f128;
    let g = 4.0_f128;
    
    assert_eq!(f.ceil(), 4.0);
    assert_eq!(g.ceil(), 4.0);
    }
}
