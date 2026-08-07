// Extracted from library/std/src/num/f32.rs:811
#![allow(unused)]
fn main() {
    let f = 1.0f32;

    // atan(tan(1))
    let abs_difference = (f.tan().atan() - 1.0).abs();

    assert!(abs_difference <= f32::EPSILON);
}
