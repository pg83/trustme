// Extracted from library/std/src/num/f128.rs:576
#![allow(unused)]
#![feature(f128)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f128_math)] {
    
    // Positive angles measured counter-clockwise
    // from positive x axis
    // -pi/4 radians (45 deg clockwise)
    let x1 = 3.0f128;
    let y1 = -3.0f128;
    
    // 3pi/4 radians (135 deg counter-clockwise)
    let x2 = -3.0f128;
    let y2 = 3.0f128;
    
    let abs_difference_1 = (y1.atan2(x1) - (-std::f128::consts::FRAC_PI_4)).abs();
    let abs_difference_2 = (y2.atan2(x2) - (3.0 * std::f128::consts::FRAC_PI_4)).abs();
    
    assert!(abs_difference_1 <= f128::EPSILON);
    assert!(abs_difference_2 <= f128::EPSILON);
    }
}
