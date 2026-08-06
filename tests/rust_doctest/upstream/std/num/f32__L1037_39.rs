// Extracted from library/std/src/num/f32.rs:1037
#![allow(unused)]
fn main() {
    let e = std::f32::consts::E;
    let x = 1.0f32;
    
    let f = x.tanh();
    // Solving tanh() at 1 gives `(1 - e^(-2))/(1 + e^(-2))`
    let g = (1.0 - e.powi(-2)) / (1.0 + e.powi(-2));
    let abs_difference = (f - g).abs();
    
    assert!(abs_difference <= f32::EPSILON);
}
