// Extracted from library/std/src/num/f32.rs:1151
#![allow(unused)]
#![feature(float_gamma)]
fn main() {
    let x = 5.0f32;
    
    let abs_difference = (x.gamma() - 24.0).abs();
    
    assert!(abs_difference <= f32::EPSILON);
}
