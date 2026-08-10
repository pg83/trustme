// Extracted from library/core/src/num/f128.rs:278
#![allow(unused)]
#![feature(f128)]
fn main() {
    // FIXME(f16_f128): remove when `unordtf2` is available
    #[cfg(all(target_arch = "x86_64", target_os = "linux"))] {

    let nan = f128::NAN;
    let f = 7.0_f128;

    assert!(nan.is_nan());
    assert!(!f.is_nan());
    }
}
