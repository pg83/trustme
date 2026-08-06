// Extracted from library/core/src/num/f16.rs:629
#![allow(unused)]
#![feature(f16)]
fn main() {
    // FIXME(f16_f128): extendhfsf2, truncsfhf2, __gnu_h2f_ieee, __gnu_f2h_ieee missing for many platforms
    #[cfg(all(target_arch = "x86_64", target_os = "linux"))] {
    
    let angle = std::f16::consts::PI;
    
    let abs_difference = (angle.to_degrees() - 180.0).abs();
    assert!(abs_difference <= 0.5);
    }
}
