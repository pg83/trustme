// Extracted from library/core/src/num/f16.rs:410
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(all(target_arch = "x86_64", target_os = "linux"))] {
    
    use std::num::FpCategory;
    
    let num = 12.4_f16;
    let inf = f16::INFINITY;
    
    assert_eq!(num.classify(), FpCategory::Normal);
    assert_eq!(inf.classify(), FpCategory::Infinite);
    }
}
