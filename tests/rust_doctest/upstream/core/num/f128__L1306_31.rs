// Extracted from library/core/src/num/f128.rs:1306
#![allow(unused)]
#![feature(f128)]
fn main() {
    #[cfg(all(target_arch = "x86_64", target_os = "linux"))] {

    let f = 3.5_f128;

    assert_eq!(f.signum(), 1.0);
    assert_eq!(f128::NEG_INFINITY.signum(), -1.0);

    assert!(f128::NAN.signum().is_nan());
    }
}
