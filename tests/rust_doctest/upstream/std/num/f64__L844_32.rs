// Extracted from library/std/src/num/f64.rs:844
#![allow(unused)]
fn main() {
    // Positive angles measured counter-clockwise
    // from positive x axis
    // -pi/4 radians (45 deg clockwise)
    let x1 = 3.0_f64;
    let y1 = -3.0_f64;

    // 3pi/4 radians (135 deg counter-clockwise)
    let x2 = -3.0_f64;
    let y2 = 3.0_f64;

    let abs_difference_1 = (y1.atan2(x1) - (-std::f64::consts::FRAC_PI_4)).abs();
    let abs_difference_2 = (y2.atan2(x2) - (3.0 * std::f64::consts::FRAC_PI_4)).abs();

    assert!(abs_difference_1 < 1e-10);
    assert!(abs_difference_2 < 1e-10);
}
