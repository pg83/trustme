// llvm.x86.avx2.permd had no lowering, so the generated C aborted on
// `assert(!"Extern LLVM: llvm.x86.avx2.permd")` the first time base64 ran it.
#[cfg(target_arch = "x86_64")] use core::arch::x86_64::*;
#[cfg(target_arch = "x86_64")]
#[target_feature(enable = "avx2")]
unsafe fn check() -> bool {
    // dst[i] = a[idx[i] & 7]; `a` is the data, `idx` the index vector.
    let a = _mm256_setr_epi32(100, 200, 300, 400, 500, 600, 700, 800);
    let idx = _mm256_setr_epi32(5, 0, 5, 1, 7, 6, 3, 4);
    let r: [i32; 8] = core::mem::transmute(_mm256_permutevar8x32_epi32(a, idx));
    if r != [600, 100, 600, 200, 800, 700, 400, 500] { return false; }
    // Reading the arguments the other way round would give [7,5,7,5,7,5,7,5].
    // Only the low three bits of each index select a lane.
    let wide = _mm256_setr_epi32(8, -1, 11, 0x7fff_ffff, 2, 14, 100, -8);
    let r2: [i32; 8] = core::mem::transmute(_mm256_permutevar8x32_epi32(a, wide));
    if r2 != [100, 800, 400, 800, 300, 700, 500, 100] { return false; }
    true
}
fn main() {
    #[cfg(target_arch = "x86_64")]
    if std::is_x86_feature_detected!("avx2") { assert!(unsafe { check() }); return; }
    println!("skip");
}
