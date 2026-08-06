// Extracted from library/core/src/num/f16.rs:1254
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(all(target_arch = "x86_64", target_os = "linux"))] {
    
    let x = 3.5_f16;
    let y = -3.5_f16;
    
    assert_eq!(x.abs(), x);
    assert_eq!(y.abs(), -y);
    
    assert!(f16::NAN.abs().is_nan());
    }
}
