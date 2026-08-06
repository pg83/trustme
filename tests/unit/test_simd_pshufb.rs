// pshufb copied src[i] instead of src[mask[i] & 0xF]; the AVX2 form shuffles
// within each 16-byte lane.
#[cfg(target_arch = "x86_64")] use core::arch::x86_64::*;
#[cfg(target_arch = "x86_64")]
#[target_feature(enable = "avx2")]
unsafe fn check() -> bool {
    let mut src = [0u8; 32];
    for i in 0..32 { src[i] = i as u8; }
    let v: __m256i = core::mem::transmute(src);
    // reverse each 16-byte lane
    let mut m = [0u8; 32];
    for i in 0..16 { m[i] = (15 - i) as u8; m[16 + i] = (15 - i) as u8; }
    let mask: __m256i = core::mem::transmute(m);
    let r: [u8; 32] = core::mem::transmute(_mm256_shuffle_epi8(v, mask));
    for i in 0..16 { if r[i] != (15 - i) as u8 { return false; } if r[16 + i] != (16 + 15 - i) as u8 { return false; } }
    true
}
fn main() {
    #[cfg(target_arch = "x86_64")]
    if std::is_x86_feature_detected!("avx2") { assert!(unsafe { check() }); return; }
    println!("skip");
}
