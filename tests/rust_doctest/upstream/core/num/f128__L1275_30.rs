// Extracted from library/core/src/num/f128.rs:1275
#![allow(unused)]
#![feature(f128)]
fn main() {
    #[cfg(all(target_arch = "x86_64", target_os = "linux"))] {

    let x = 3.5_f128;
    let y = -3.5_f128;

    assert_eq!(x.abs(), x);
    assert_eq!(y.abs(), -y);

    assert!(f128::NAN.abs().is_nan());
    }
}
