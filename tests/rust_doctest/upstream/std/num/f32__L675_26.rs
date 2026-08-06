// Extracted from library/std/src/num/f32.rs:675
#![allow(unused)]
fn main() {
    let x = std::f32::consts::FRAC_PI_2;
    
    let abs_difference = (x.sin() - 1.0).abs();
    
    assert!(abs_difference <= 1e-6);
}
