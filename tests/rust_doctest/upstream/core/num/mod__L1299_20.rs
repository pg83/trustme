// Extracted from library/core/src/num/mod.rs:1299
#![allow(unused)]
fn main() {
    use std::num::FpCategory;
    
    let num = 12.4_f32;
    let inf = f32::INFINITY;
    let zero = 0f32;
    let sub: f32 = 1.1754942e-38;
    let nan = f32::NAN;
    
    assert_eq!(num.classify(), FpCategory::Normal);
    assert_eq!(inf.classify(), FpCategory::Infinite);
    assert_eq!(zero.classify(), FpCategory::Zero);
    assert_eq!(sub.classify(), FpCategory::Subnormal);
    assert_eq!(nan.classify(), FpCategory::Nan);
}
