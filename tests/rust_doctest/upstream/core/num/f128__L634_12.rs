// Extracted from library/core/src/num/f128.rs:634
#![allow(unused)]
#![feature(f128)]
fn main() {
    // FIXME(f16_f128): remove when `eqtf2` is available
    #[cfg(all(target_arch = "x86_64", target_os = "linux"))] {
    
    let angle = std::f128::consts::PI;
    
    let abs_difference = (angle.to_degrees() - 180.0).abs();
    assert!(abs_difference <= f128::EPSILON);
    }
}
