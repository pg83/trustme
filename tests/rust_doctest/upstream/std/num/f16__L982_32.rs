// Extracted from library/std/src/num/f16.rs:982
#![allow(unused)]
#![feature(f16)]
#![feature(float_erf)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f16_math)] {
    /// The error function relates what percent of a normal distribution lies
    /// within `x` standard deviations (scaled by `1/sqrt(2)`).
    fn within_standard_deviations(x: f16) -> f16 {
        (x * std::f16::consts::FRAC_1_SQRT_2).erf() * 100.0
    }
    
    // 68% of a normal distribution is within one standard deviation
    assert!((within_standard_deviations(1.0) - 68.269).abs() < 0.1);
    // 95% of a normal distribution is within two standard deviations
    assert!((within_standard_deviations(2.0) - 95.450).abs() < 0.1);
    // 99.7% of a normal distribution is within three standard deviations
    assert!((within_standard_deviations(3.0) - 99.730).abs() < 0.1);
    }
}
