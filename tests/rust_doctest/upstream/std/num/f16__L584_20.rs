// Extracted from library/std/src/num/f16.rs:584
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f16_math)] {
    
    let x = std::f16::consts::FRAC_PI_4;
    let f = x.sin_cos();
    
    let abs_difference_0 = (f.0 - x.sin()).abs();
    let abs_difference_1 = (f.1 - x.cos()).abs();
    
    assert!(abs_difference_0 <= f16::EPSILON);
    assert!(abs_difference_1 <= f16::EPSILON);
    }
}
