// Extracted from library/core/src/num/f64.rs:643
#![allow(unused)]
fn main() {
    use std::num::FpCategory;

    let num = 12.4_f64;
    let inf = f64::INFINITY;

    assert_eq!(num.classify(), FpCategory::Normal);
    assert_eq!(inf.classify(), FpCategory::Infinite);
}
