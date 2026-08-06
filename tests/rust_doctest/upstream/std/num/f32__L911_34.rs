// Extracted from library/std/src/num/f32.rs:911
#![allow(unused)]
fn main() {
    let x = 1e-8_f32;
    
    // for very small x, e^x is approximately 1 + x + x^2 / 2
    let approx = x + x * x / 2.0;
    let abs_difference = (x.exp_m1() - approx).abs();
    
    assert!(abs_difference < 1e-10);
}
