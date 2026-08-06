// Extracted from library/std/src/num/f32.rs:1210
#![allow(unused)]
#![feature(float_erf)]
fn main() {
    /// The error function relates what percent of a normal distribution lies
    /// within `x` standard deviations (scaled by `1/sqrt(2)`).
    fn within_standard_deviations(x: f32) -> f32 {
        (x * std::f32::consts::FRAC_1_SQRT_2).erf() * 100.0
    }
    
    // 68% of a normal distribution is within one standard deviation
    assert!((within_standard_deviations(1.0) - 68.269).abs() < 0.01);
    // 95% of a normal distribution is within two standard deviations
    assert!((within_standard_deviations(2.0) - 95.450).abs() < 0.01);
    // 99.7% of a normal distribution is within three standard deviations
    assert!((within_standard_deviations(3.0) - 99.730).abs() < 0.01);
}
