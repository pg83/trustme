// Extracted from library/core/src/num/f16.rs:297
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(all(target_arch = "x86_64", target_os = "linux"))] {
    
    let f = 7.0f16;
    let inf = f16::INFINITY;
    let neg_inf = f16::NEG_INFINITY;
    let nan = f16::NAN;
    
    assert!(!f.is_infinite());
    assert!(!nan.is_infinite());
    
    assert!(inf.is_infinite());
    assert!(neg_inf.is_infinite());
    }
}
