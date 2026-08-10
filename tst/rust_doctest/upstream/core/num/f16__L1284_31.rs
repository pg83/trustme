// Extracted from library/core/src/num/f16.rs:1284
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(all(target_arch = "x86_64", target_os = "linux"))] {

    let f = 3.5_f16;

    assert_eq!(f.signum(), 1.0);
    assert_eq!(f16::NEG_INFINITY.signum(), -1.0);

    assert!(f16::NAN.signum().is_nan());
    }
}
