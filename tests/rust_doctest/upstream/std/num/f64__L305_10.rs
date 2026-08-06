// Extracted from library/std/src/num/f64.rs:305
#![allow(unused)]
fn main() {
    let x = 2.0_f64;
    let abs_difference = (x.powi(2) - (x * x)).abs();
    assert!(abs_difference <= 1e-14);
    
    assert_eq!(f64::powi(f64::NAN, 0), 1.0);
}
