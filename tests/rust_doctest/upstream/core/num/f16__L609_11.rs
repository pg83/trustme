// Extracted from library/core/src/num/f16.rs:609
#![allow(unused)]
#![feature(f16)]
fn main() {
    // FIXME(f16_f128): extendhfsf2, truncsfhf2, __gnu_h2f_ieee, __gnu_f2h_ieee missing for many platforms
    #[cfg(all(target_arch = "x86_64", target_os = "linux"))] {
    
    let x = 2.0_f16;
    let abs_difference = (x.recip() - (1.0 / x)).abs();
    
    assert!(abs_difference <= f16::EPSILON);
    }
}
