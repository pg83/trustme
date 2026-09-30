// llvm.x86.avx2.psrlv.d.256 and its nine siblings had no lowering, so the
// generated C aborted on `assert(!"Extern LLVM: llvm.x86.avx2.psrlv.d.256")`
// the first time curve25519-dalek's AVX2 backend ran.
#[cfg(target_arch = "x86_64")] use core::arch::x86_64::*;
#[cfg(target_arch = "x86_64")]
#[target_feature(enable = "avx2")]
unsafe fn check() -> bool {
    // Each lane shifts by the count in the same lane of the second vector; a
    // logical shift by the lane width or more (a negative count is a large
    // unsigned one) gives zero, an arithmetic one fills with the sign bit.
    let a = _mm256_setr_epi32(-16, 0x7000_0001, 256, -1, 1, 0x4000_0000, -256, 3);
    let c = _mm256_setr_epi32(4, 1, 8, 31, 32, 30, -1, 0);
    let r: [i32; 8] = core::mem::transmute(_mm256_srlv_epi32(a, c));
    if r != [0x0fff_ffff, 0x3800_0000, 1, 1, 0, 1, 0, 3] { return false; }
    let r: [i32; 8] = core::mem::transmute(_mm256_sllv_epi32(a, c));
    if r != [-256, 0xe000_0002u32 as i32, 0x10000, i32::MIN, 0, 0, 0, 3] { return false; }
    let r: [i32; 8] = core::mem::transmute(_mm256_srav_epi32(a, c));
    if r != [-1, 0x3800_0000, 1, -1, 0, 1, -1, 3] { return false; }
    let a4 = _mm_setr_epi32(-16, 5, 6, 7);
    let c4 = _mm_setr_epi32(2, 33, 1, 0);
    let r: [i32; 4] = core::mem::transmute(_mm_srlv_epi32(a4, c4));
    if r != [0x3fff_fffc, 0, 3, 7] { return false; }
    let r: [i32; 4] = core::mem::transmute(_mm_sllv_epi32(a4, c4));
    if r != [-64, 0, 12, 7] { return false; }
    let r: [i32; 4] = core::mem::transmute(_mm_srav_epi32(a4, c4));
    if r != [-4, 0, 3, 7] { return false; }
    let q = _mm256_setr_epi64x(-1, 1 << 40, 12, 7);
    let cq = _mm256_setr_epi64x(60, 40, 64, -1);
    let r: [i64; 4] = core::mem::transmute(_mm256_srlv_epi64(q, cq));
    if r != [15, 1, 0, 0] { return false; }
    let r: [i64; 4] = core::mem::transmute(_mm256_sllv_epi64(q, cq));
    if r != [-1i64 << 60, 0, 0, 0] { return false; }
    let q2 = _mm_set_epi64x(1 << 63, 3);
    let cq2 = _mm_set_epi64x(63, 1);
    let r: [i64; 2] = core::mem::transmute(_mm_srlv_epi64(q2, cq2));
    if r != [1, 1] { return false; }
    let r: [i64; 2] = core::mem::transmute(_mm_sllv_epi64(q2, cq2));
    if r != [6, 0] { return false; }
    true
}
fn main() {
    #[cfg(target_arch = "x86_64")]
    if std::is_x86_feature_detected!("avx2") { assert!(unsafe { check() }); return; }
    println!("skip");
}
