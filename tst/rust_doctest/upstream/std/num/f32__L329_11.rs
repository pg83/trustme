// Extracted from library/std/src/num/f32.rs:329
#![allow(unused)]
fn main() {
    let x = 2.0_f32;
    let abs_difference = (x.powf(2.0) - (x * x)).abs();
    assert!(abs_difference <= 1e-5);

    assert_eq!(f32::powf(1.0, f32::NAN), 1.0);
    assert_eq!(f32::powf(f32::NAN, 0.0), 1.0);
}
