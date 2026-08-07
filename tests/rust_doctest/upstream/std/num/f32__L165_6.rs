// Extracted from library/std/src/num/f32.rs:165
#![allow(unused)]
fn main() {
    let x = 3.6_f32;
    let y = -3.6_f32;
    let abs_difference_x = (x.fract() - 0.6).abs();
    let abs_difference_y = (y.fract() - (-0.6)).abs();

    assert!(abs_difference_x <= f32::EPSILON);
    assert!(abs_difference_y <= f32::EPSILON);
}
