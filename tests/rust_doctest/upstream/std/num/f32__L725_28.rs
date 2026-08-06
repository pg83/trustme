// Extracted from library/std/src/num/f32.rs:725
#![allow(unused)]
fn main() {
    let x = std::f32::consts::FRAC_PI_4;
    let abs_difference = (x.tan() - 1.0).abs();
    
    assert!(abs_difference <= f32::EPSILON);
}
