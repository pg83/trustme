// Extracted from library/std/src/num/f64.rs:725
#![allow(unused)]
fn main() {
    let x = std::f64::consts::FRAC_PI_4;
    let abs_difference = (x.tan() - 1.0).abs();
    
    assert!(abs_difference < 1e-14);
}
