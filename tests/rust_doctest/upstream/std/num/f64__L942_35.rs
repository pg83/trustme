// Extracted from library/std/src/num/f64.rs:942
#![allow(unused)]
fn main() {
    let x = 1e-16_f64;
    
    // for very small x, ln(1 + x) is approximately x - x^2 / 2
    let approx = x - x * x / 2.0;
    let abs_difference = (x.ln_1p() - approx).abs();
    
    assert!(abs_difference < 1e-20);
}
