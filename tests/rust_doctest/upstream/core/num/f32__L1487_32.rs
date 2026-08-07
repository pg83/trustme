// Extracted from library/core/src/num/f32.rs:1487
#![allow(unused)]
fn main() {
    let f = 3.5_f32;

    assert_eq!(f.copysign(0.42), 3.5_f32);
    assert_eq!(f.copysign(-0.42), -3.5_f32);
    assert_eq!((-f).copysign(0.42), 3.5_f32);
    assert_eq!((-f).copysign(-0.42), -3.5_f32);

    assert!(f32::NAN.copysign(1.0).is_nan());
}
