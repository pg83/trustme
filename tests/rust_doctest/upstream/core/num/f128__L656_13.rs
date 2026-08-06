// Extracted from library/core/src/num/f128.rs:656
#![allow(unused)]
#![feature(f128)]
fn main() {
    // FIXME(f16_f128): remove when `eqtf2` is available
    #[cfg(all(target_arch = "x86_64", target_os = "linux"))] {
    
    let angle = 180.0f128;
    
    let abs_difference = (angle.to_radians() - std::f128::consts::PI).abs();
    
    assert!(abs_difference <= 1e-30);
    }
}
