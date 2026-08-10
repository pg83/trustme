// Extracted from library/std/src/num/f32.rs:1180
#![allow(unused)]
#![feature(float_gamma)]
fn main() {
    let x = 2.0f32;

    let abs_difference = (x.ln_gamma().0 - 0.0).abs();

    assert!(abs_difference <= f32::EPSILON);
}
