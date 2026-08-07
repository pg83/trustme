// Extracted from library/core/src/num/f128.rs:301
#![allow(unused)]
#![feature(f128)]
fn main() {
    // FIXME(f16_f128): remove when `eqtf2` is available
    #[cfg(all(target_arch = "x86_64", target_os = "linux"))] {

    let f = 7.0f128;
    let inf = f128::INFINITY;
    let neg_inf = f128::NEG_INFINITY;
    let nan = f128::NAN;

    assert!(!f.is_infinite());
    assert!(!nan.is_infinite());

    assert!(inf.is_infinite());
    assert!(neg_inf.is_infinite());
    }
}
