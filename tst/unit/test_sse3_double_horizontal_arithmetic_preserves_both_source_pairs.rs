//@ compile-variant: -O
use std::arch::x86_64::*;

#[target_feature(enable = "sse3")]
unsafe fn check() {
    let left: __m128d = std::mem::transmute([20.0f64, 3.0]);
    let right: __m128d = std::mem::transmute([41.0f64, 7.0]);
    let sum: [f64; 2] = std::mem::transmute(_mm_hadd_pd(left, right));
    let difference: [f64; 2] = std::mem::transmute(_mm_hsub_pd(left, right));
    assert_eq!(sum, [23.0, 48.0]);
    assert_eq!(difference, [17.0, 34.0]);
}

fn main() { if is_x86_feature_detected!("sse3") { unsafe { check(); } } }
