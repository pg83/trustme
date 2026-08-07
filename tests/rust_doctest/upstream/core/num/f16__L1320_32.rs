// Extracted from library/core/src/num/f16.rs:1320
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(all(target_arch = "x86_64", target_os = "linux"))] {

    let f = 3.5_f16;

    assert_eq!(f.copysign(0.42), 3.5_f16);
    assert_eq!(f.copysign(-0.42), -3.5_f16);
    assert_eq!((-f).copysign(0.42), 3.5_f16);
    assert_eq!((-f).copysign(-0.42), -3.5_f16);

    assert!(f16::NAN.copysign(1.0).is_nan());
    }
}
