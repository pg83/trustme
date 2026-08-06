// Extracted from library/core/src/num/f128.rs:1592
#![allow(unused)]
#![feature(f128)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f128_math)] {
    
    let x = 3.6_f128;
    let y = -3.6_f128;
    let abs_difference_x = (x.fract() - 0.6).abs();
    let abs_difference_y = (y.fract() - (-0.6)).abs();
    
    assert!(abs_difference_x <= f128::EPSILON);
    assert!(abs_difference_y <= f128::EPSILON);
    }
}
