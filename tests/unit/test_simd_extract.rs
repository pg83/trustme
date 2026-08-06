// _mm256_extracti128_si256 lowers to simd_shuffle with a 2-element map over an
// i64x4; splitting on the map length instead of the input length returned zero.
#[cfg(target_arch = "x86_64")] use core::arch::x86_64::*;
#[cfg(target_arch = "x86_64")]
#[target_feature(enable = "avx2")]
unsafe fn check() -> bool {
    let v: __m256i = core::mem::transmute([1i32, 2, 3, 4, 5, 6, 7, 8]);
    let hi: [i32; 4] = core::mem::transmute(_mm256_extracti128_si256::<1>(v));
    hi == [5, 6, 7, 8]
}
fn main() {
    #[cfg(target_arch = "x86_64")]
    if std::is_x86_feature_detected!("avx2") { assert!(unsafe { check() }); return; }
    println!("skip");
}
