// Extracted from library/core/src/num/f128.rs:420
#![allow(unused)]
#![feature(f128)]
fn main() {
    // FIXME(f16_f128): remove when `eqtf2` is available
    #[cfg(all(target_arch = "x86_64", target_os = "linux"))] {

    use std::num::FpCategory;

    let num = 12.4_f128;
    let inf = f128::INFINITY;

    assert_eq!(num.classify(), FpCategory::Normal);
    assert_eq!(inf.classify(), FpCategory::Infinite);
    }
}
