// Extracted from library/core/src/num/f32.rs:843
#![allow(unused)]
fn main() {
    let angle = std::f32::consts::PI;
    
    let abs_difference = (angle.to_degrees() - 180.0).abs();
    #[cfg(any(not(target_arch = "x86"), target_feature = "sse2"))]
    assert!(abs_difference <= f32::EPSILON);
}
