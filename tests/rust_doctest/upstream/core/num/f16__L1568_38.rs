// Extracted from library/core/src/num/f16.rs:1568
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f16_math)] {
    
    let x = 3.6_f16;
    let y = -3.6_f16;
    let abs_difference_x = (x.fract() - 0.6).abs();
    let abs_difference_y = (y.fract() - (-0.6)).abs();
    
    assert!(abs_difference_x <= f16::EPSILON);
    assert!(abs_difference_y <= f16::EPSILON);
    }
}
