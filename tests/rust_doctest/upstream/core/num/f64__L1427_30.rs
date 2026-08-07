// Extracted from library/core/src/num/f64.rs:1427
#![allow(unused)]
fn main() {
    let x = 3.5_f64;
    let y = -3.5_f64;

    assert_eq!(x.abs(), x);
    assert_eq!(y.abs(), -y);

    assert!(f64::NAN.abs().is_nan());
}
