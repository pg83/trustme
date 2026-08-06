// simd-adler32 uses the multiply-add family; these were unimplemented (abort).
#[cfg(target_arch = "x86_64")] use core::arch::x86_64::*;
#[cfg(target_arch = "x86_64")]
#[target_feature(enable = "avx2")]
unsafe fn check() -> bool {
    let mut a = [0u8; 32];
    for i in 0..32 { a[i] = (i * 3 + 1) as u8; }
    let va: __m256i = core::mem::transmute(a);
    // psadbw vs zero: sum of |a - 0| per 8-byte lane
    let sad: [u64; 4] = core::mem::transmute(_mm256_sad_epu8(va, _mm256_setzero_si256()));
    for k in 0..4 { let s: u64 = (0..8).map(|j| a[k*8+j] as u64).sum(); if sad[k] != s { return false; } }
    // pmaddwd with ones: pairwise add of i16 lanes
    let w: [i16; 16] = core::mem::transmute(a);
    let md: [i32; 8] = core::mem::transmute(_mm256_madd_epi16(va, _mm256_set1_epi16(1)));
    for i in 0..8 { if md[i] != w[2*i] as i32 + w[2*i+1] as i32 { return false; } }
    true
}
fn main() {
    #[cfg(target_arch = "x86_64")]
    if std::is_x86_feature_detected!("avx2") { assert!(unsafe { check() }); return; }
    println!("skip");
}
