// simd_saturating_add and simd_saturating_sub had no lowering, so the
// generated C asserted `TODO: Platform intrinsic "simd_saturating_sub"`
// the first time base64's SIMD engine subtracted a vector.
#[cfg(target_arch = "x86_64")] use core::arch::x86_64::*;
#[cfg(target_arch = "x86_64")]
fn tile<const N: usize, T: Copy + Default>(pattern: [T; 8]) -> [T; N] {
    let mut out = [T::default(); N];
    for i in 0..N { out[i] = pattern[i % 8]; }
    out
}
#[cfg(target_arch = "x86_64")]
#[target_feature(enable = "avx2")]
unsafe fn check() {
    let au: [u8; 32] = tile([0, 1, 127, 128, 200, 255, 254, 3]);
    let bu: [u8; 32] = tile([0, 10, 100, 200, 50, 1, 2, 250]);
    let a: __m256i = core::mem::transmute(au);
    let b: __m256i = core::mem::transmute(bu);
    let got: [u8; 32] = core::mem::transmute(_mm256_adds_epu8(a, b));
    assert_eq!(got, tile::<32, u8>([0, 11, 227, 255, 250, 255, 255, 253]));
    let got: [u8; 32] = core::mem::transmute(_mm256_subs_epu8(a, b));
    assert_eq!(got, tile::<32, u8>([0, 0, 27, 0, 150, 254, 252, 0]));

    let ai: [i8; 32] = tile([0, 1, 127, -128, 100, -100, 127, -128]);
    let bi: [i8; 32] = tile([0, 127, 1, -1, 100, -100, -128, 127]);
    let a: __m256i = core::mem::transmute(ai);
    let b: __m256i = core::mem::transmute(bi);
    let got: [i8; 32] = core::mem::transmute(_mm256_adds_epi8(a, b));
    assert_eq!(got, tile::<32, i8>([0, 127, 127, -128, 127, -128, -1, -1]));
    let got: [i8; 32] = core::mem::transmute(_mm256_subs_epi8(a, b));
    assert_eq!(got, tile::<32, i8>([0, -126, 126, -127, 0, 0, 127, -128]));

    let aw: [u16; 16] = tile([0, 1, 32767, 32768, 60000, 65535, 65534, 3]);
    let bw: [u16; 16] = tile([0, 10, 100, 40000, 10000, 1, 2, 65000]);
    let a: __m256i = core::mem::transmute(aw);
    let b: __m256i = core::mem::transmute(bw);
    let got: [u16; 16] = core::mem::transmute(_mm256_adds_epu16(a, b));
    assert_eq!(got, tile::<16, u16>([0, 11, 32867, 65535, 65535, 65535, 65535, 65003]));
    let got: [u16; 16] = core::mem::transmute(_mm256_subs_epu16(a, b));
    assert_eq!(got, tile::<16, u16>([0, 0, 32667, 0, 50000, 65534, 65532, 0]));

    let as_: [i16; 16] = tile([0, 1, 32767, -32768, 20000, -20000, 32767, -32768]);
    let bs: [i16; 16] = tile([0, 32767, 1, -1, 20000, -20000, -32768, 32767]);
    let a: __m256i = core::mem::transmute(as_);
    let b: __m256i = core::mem::transmute(bs);
    let got: [i16; 16] = core::mem::transmute(_mm256_adds_epi16(a, b));
    assert_eq!(got, tile::<16, i16>([0, 32767, 32767, -32768, 32767, -32768, -1, -1]));
    let got: [i16; 16] = core::mem::transmute(_mm256_subs_epi16(a, b));
    assert_eq!(got, tile::<16, i16>([0, -32766, 32766, -32767, 0, 0, 32767, -32768]));
}
fn main() {
    #[cfg(target_arch = "x86_64")]
    if std::is_x86_feature_detected!("avx2") { unsafe { check() }; return; }
    println!("skip");
}
