// Extracted from library/core/src/num/f128.rs:1342
#![allow(unused)]
#![feature(f128)]
fn main() {
    #[cfg(all(target_arch = "x86_64", target_os = "linux"))] {
    
    let f = 3.5_f128;
    
    assert_eq!(f.copysign(0.42), 3.5_f128);
    assert_eq!(f.copysign(-0.42), -3.5_f128);
    assert_eq!((-f).copysign(0.42), 3.5_f128);
    assert_eq!((-f).copysign(-0.42), -3.5_f128);
    
    assert!(f128::NAN.copysign(1.0).is_nan());
    }
}
