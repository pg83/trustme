// Extracted from library/std/src/num/f32.rs:619
#![allow(unused)]
fn main() {
    let x = 8.0f32;

    // x^(1/3) - 2 == 0
    let abs_difference = (x.cbrt() - 2.0).abs();

    assert!(abs_difference <= f32::EPSILON);
}
