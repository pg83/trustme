// Both lanes of simd_shuffle read operand 1, so shuffles pulling from the first
// vector (palignr, blends) produced garbage.
#[cfg(target_arch = "x86_64")] use core::arch::x86_64::*;
#[cfg(target_arch = "x86_64")]
#[target_feature(enable = "avx2")]
unsafe fn check() -> bool {
    let a: __m256i = core::mem::transmute([1u8; 32]);
    let b: __m256i = core::mem::transmute([2u8; 32]);
    // blend takes bytes from a where the mask bit is 0 -> must see operand 0.
    let r: [u8; 32] = core::mem::transmute(_mm256_blend_epi32::<0>(a, b));
    r.iter().all(|&x| x == 1)
}
fn main() {
    #[cfg(target_arch = "x86_64")]
    if std::is_x86_feature_detected!("avx2") { assert!(unsafe { check() }); return; }
    println!("skip");
}
