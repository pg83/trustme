// Extracted from library/std/src/num/f16.rs:30
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f16_math)] {
    
    let x = 2.0_f16;
    let abs_difference = (x.powf(2.0) - (x * x)).abs();
    assert!(abs_difference <= f16::EPSILON);
    
    assert_eq!(f16::powf(1.0, f16::NAN), 1.0);
    assert_eq!(f16::powf(f16::NAN, 0.0), 1.0);
    }
}
