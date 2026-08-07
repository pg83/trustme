// Extracted from library/core/src/num/f64.rs:1485
#![allow(unused)]
fn main() {
    let f = 3.5_f64;

    assert_eq!(f.copysign(0.42), 3.5_f64);
    assert_eq!(f.copysign(-0.42), -3.5_f64);
    assert_eq!((-f).copysign(0.42), 3.5_f64);
    assert_eq!((-f).copysign(-0.42), -3.5_f64);

    assert!(f64::NAN.copysign(1.0).is_nan());
}
