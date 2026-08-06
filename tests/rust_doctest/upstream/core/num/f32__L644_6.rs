// Extracted from library/core/src/num/f32.rs:644
#![allow(unused)]
fn main() {
    use std::num::FpCategory;
    
    let num = 12.4_f32;
    let inf = f32::INFINITY;
    
    assert_eq!(num.classify(), FpCategory::Normal);
    assert_eq!(inf.classify(), FpCategory::Infinite);
}
