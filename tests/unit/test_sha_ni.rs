// The sha2 crate takes the SHA-NI path under runtime detection; sha256rnds2 and
// the message-schedule intrinsics were missing (abort / wrong output).
#[cfg(target_arch = "x86_64")] use core::arch::x86_64::*;
#[cfg(target_arch = "x86_64")]
#[target_feature(enable = "sha,sse2,ssse3,sse4.1")]
unsafe fn check() -> bool {
    // msg1 is a pure function of its inputs; compare against the spec formula.
    let a = [1u32, 2, 3, 4];
    let b = [5u32, 6, 7, 8];
    let va: __m128i = core::mem::transmute(a);
    let vb: __m128i = core::mem::transmute(b);
    let r: [u32; 4] = core::mem::transmute(_mm_sha256msg1_epu32(va, vb));
    let s0 = |x: u32| x.rotate_right(7) ^ x.rotate_right(18) ^ (x >> 3);
    for i in 0..4 { let x = if i < 3 { a[i+1] } else { b[0] }; if r[i] != a[i].wrapping_add(s0(x)) { return false; } }
    true
}
fn main() {
    #[cfg(target_arch = "x86_64")]
    if std::is_x86_feature_detected!("sha") { assert!(unsafe { check() }); return; }
    println!("skip");
}
