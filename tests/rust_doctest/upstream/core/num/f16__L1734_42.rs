// Extracted from library/core/src/num/f16.rs:1734
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f16_math)] {
    
    let x = 2.0_f16;
    let abs_difference = (x.powi(2) - (x * x)).abs();
    assert!(abs_difference <= f16::EPSILON);
    
    assert_eq!(f16::powi(f16::NAN, 0), 1.0);
    }
}
