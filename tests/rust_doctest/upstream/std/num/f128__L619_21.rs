// Extracted from library/std/src/num/f128.rs:619
#![allow(unused)]
#![feature(f128)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f128_math)] {
    
    let x = std::f128::consts::FRAC_PI_4;
    let f = x.sin_cos();
    
    let abs_difference_0 = (f.0 - x.sin()).abs();
    let abs_difference_1 = (f.1 - x.cos()).abs();
    
    assert!(abs_difference_0 <= f128::EPSILON);
    assert!(abs_difference_1 <= f128::EPSILON);
    }
}
