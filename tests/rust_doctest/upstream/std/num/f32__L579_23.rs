// Extracted from library/std/src/num/f32.rs:579
#![allow(unused)]
fn main() {
    let x = 3.0f32;
    let y = -3.0f32;
    
    let abs_difference_x = (x.abs_sub(1.0) - 2.0).abs();
    let abs_difference_y = (y.abs_sub(1.0) - 0.0).abs();
    
    assert!(abs_difference_x <= f32::EPSILON);
    assert!(abs_difference_y <= f32::EPSILON);
}
