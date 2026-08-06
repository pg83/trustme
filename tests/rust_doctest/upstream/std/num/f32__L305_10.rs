// Extracted from library/std/src/num/f32.rs:305
#![allow(unused)]
fn main() {
    let x = 2.0_f32;
    let abs_difference = (x.powi(2) - (x * x)).abs();
    assert!(abs_difference <= 1e-5);
    
    assert_eq!(f32::powi(f32::NAN, 0), 1.0);
}
