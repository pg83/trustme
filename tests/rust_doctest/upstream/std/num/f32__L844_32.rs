// Extracted from library/std/src/num/f32.rs:844
#![allow(unused)]
fn main() {
    // Positive angles measured counter-clockwise
    // from positive x axis
    // -pi/4 radians (45 deg clockwise)
    let x1 = 3.0f32;
    let y1 = -3.0f32;

    // 3pi/4 radians (135 deg counter-clockwise)
    let x2 = -3.0f32;
    let y2 = 3.0f32;

    let abs_difference_1 = (y1.atan2(x1) - (-std::f32::consts::FRAC_PI_4)).abs();
    let abs_difference_2 = (y2.atan2(x2) - (3.0 * std::f32::consts::FRAC_PI_4)).abs();

    assert!(abs_difference_1 <= f32::EPSILON);
    assert!(abs_difference_2 <= f32::EPSILON);
}
