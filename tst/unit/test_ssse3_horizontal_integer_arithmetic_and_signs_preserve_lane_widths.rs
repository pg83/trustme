//@ compile-variant: -O
use std::arch::x86_64::*;

#[target_feature(enable = "ssse3")]
unsafe fn check() {
    let left: __m128i = std::mem::transmute([32767i16, 1, -19, 5, 71, 2, -83, -4]);
    let right: __m128i = std::mem::transmute([7i16, 11, 13, 17, 19, 23, 29, 31]);
    let sum: [i16; 8] = std::mem::transmute(_mm_hadd_epi16(left, right));
    let difference: [i16; 8] = std::mem::transmute(_mm_hsub_epi16(left, right));
    let signs: __m128i = std::mem::transmute([-1i16, 0, 1, -1, 1, 0, -1, 1]);
    let signed: [i16; 8] = std::mem::transmute(_mm_sign_epi16(left, signs));
    assert_eq!(sum, [-32768, -14, 73, -87, 18, 30, 42, 60]);
    assert_eq!(difference, [32766, -24, 69, -79, -4, -4, -4, -2]);
    assert_eq!(signed, [-32767, 0, -19, -5, 71, 0, 83, -4]);
    let wide: __m128i = std::mem::transmute([i32::MAX, 1, -89, 5]);
    let other: __m128i = std::mem::transmute([97i32, 7, -101, 3]);
    let wide_sum: [i32; 4] = std::mem::transmute(_mm_hadd_epi32(wide, other));
    let wide_difference: [i32; 4] = std::mem::transmute(_mm_hsub_epi32(wide, other));
    let wide_signs: __m128i = std::mem::transmute([-1i32, 0, 1, -1]);
    let wide_signed: [i32; 4] = std::mem::transmute(_mm_sign_epi32(wide, wide_signs));
    assert_eq!(wide_sum, [i32::MIN, -84, 104, -98]);
    assert_eq!(wide_difference, [i32::MAX - 1, -94, 90, -104]);
    assert_eq!(wide_signed, [-i32::MAX, 0, -89, -5]);
}

fn main() { if is_x86_feature_detected!("ssse3") { unsafe { check(); } } }
