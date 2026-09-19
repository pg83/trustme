// llvm.x86.avx2.permps is the float sibling of llvm.x86.avx2.permd and was
// missing the same lowering, aborting on `assert(!"Extern LLVM: ...")`.
#[cfg(target_arch = "x86_64")] use core::arch::x86_64::*;
#[cfg(target_arch = "x86_64")]
#[target_feature(enable = "avx2")]
unsafe fn check() -> bool {
    // dst[i] = a[idx[i] & 7]; `a` is the data, `idx` the index vector.
    let a = _mm256_setr_ps(1., 2., 3., 4., 5., 6., 7., 8.);
    let idx = _mm256_setr_epi32(5, 0, 5, 1, 7, 6, 3, 4);
    let r: [f32; 8] = core::mem::transmute(_mm256_permutevar8x32_ps(a, idx));
    if r != [6., 1., 6., 2., 8., 7., 4., 5.] { return false; }
    // Only the low three bits of each index select a lane.
    let wide = _mm256_setr_epi32(8, -1, 11, 0x7fff_ffff, 2, 14, 100, -8);
    let r2: [f32; 8] = core::mem::transmute(_mm256_permutevar8x32_ps(a, wide));
    if r2 != [1., 8., 4., 8., 3., 7., 5., 1.] { return false; }
    true
}
fn main() {
    #[cfg(target_arch = "x86_64")]
    if std::is_x86_feature_detected!("avx2") { assert!(unsafe { check() }); return; }
    println!("skip");
}
