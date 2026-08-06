// Extracted from library/core/src/num/f16.rs:1440
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f16_math)] {
    
    let f = 3.01_f16;
    let g = 4.0_f16;
    
    assert_eq!(f.ceil(), 4.0);
    assert_eq!(g.ceil(), 4.0);
    }
}
