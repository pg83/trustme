// Extracted from library/std/src/num/f64.rs:329
#![allow(unused)]
fn main() {
    let x = 2.0_f64;
    let abs_difference = (x.powf(2.0) - (x * x)).abs();
    assert!(abs_difference <= 1e-14);

    assert_eq!(f64::powf(1.0, f64::NAN), 1.0);
    assert_eq!(f64::powf(f64::NAN, 0.0), 1.0);
}
