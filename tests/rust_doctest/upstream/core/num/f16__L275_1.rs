// Extracted from library/core/src/num/f16.rs:275
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(all(target_arch = "x86_64", target_os = "linux"))] {
    
    let nan = f16::NAN;
    let f = 7.0_f16;
    
    assert!(nan.is_nan());
    assert!(!f.is_nan());
    }
}
