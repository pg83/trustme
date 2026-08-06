// Extracted from library/std/src/num/f64.rs:357
#![allow(unused)]
fn main() {
    let positive = 4.0_f64;
    let negative = -4.0_f64;
    let negative_zero = -0.0_f64;
    
    assert_eq!(positive.sqrt(), 2.0);
    assert!(negative.sqrt().is_nan());
    assert!(negative_zero.sqrt() == negative_zero);
}
