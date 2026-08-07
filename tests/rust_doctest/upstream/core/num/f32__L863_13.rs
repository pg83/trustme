// Extracted from library/core/src/num/f32.rs:863
#![allow(unused)]
fn main() {
    let angle = 180.0f32;

    let abs_difference = (angle.to_radians() - std::f32::consts::PI).abs();

    assert!(abs_difference <= f32::EPSILON);
}
