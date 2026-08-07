// Extracted from library/core/src/num/f32.rs:1429
#![allow(unused)]
fn main() {
    let x = 3.5_f32;
    let y = -3.5_f32;

    assert_eq!(x.abs(), x);
    assert_eq!(y.abs(), -y);

    assert!(f32::NAN.abs().is_nan());
}
