// Extracted from library/core/src/num/f32.rs:827
#![allow(unused)]
fn main() {
    let x = 2.0_f32;
    let abs_difference = (x.recip() - (1.0 / x)).abs();
    
    assert!(abs_difference <= f32::EPSILON);
}
