// AVX2 `_mm256_packs_epi32`, `_mm256_packus_epi32`, `_mm256_packs_epi16`,
// `_mm256_packus_epi16` and AVX `_mm256_test{z,c,nzc}_si256`
// (`llvm.x86.avx2.pack*`, `llvm.x86.avx.ptest*.256`). The 256-bit packs
// work per 128-bit lane, `[pack(a.lo, b.lo), pack(a.hi, b.hi)]`, and the
// tests read all four quadwords. The backend spelled only the 128-bit forms,
// so these aborted at run time with "Extern LLVM": zune-jpeg's AVX2 colour
// conversion, under image.
#[cfg(target_arch = "x86_64")]
mod x86 {
    use std::arch::x86_64::*;

    unsafe fn lanes_i16(value: __m256i) -> [i16; 16] {
        unsafe { std::mem::transmute(value) }
    }

    unsafe fn lanes_u16(value: __m256i) -> [u16; 16] {
        unsafe { std::mem::transmute(value) }
    }

    unsafe fn lanes_i8(value: __m256i) -> [i8; 32] {
        unsafe { std::mem::transmute(value) }
    }

    unsafe fn lanes_u8(value: __m256i) -> [u8; 32] {
        unsafe { std::mem::transmute(value) }
    }

    #[target_feature(enable = "avx2")]
    pub unsafe fn run() {
        let a = _mm256_setr_epi32(1, -70000, 70000, 5, 6, -7, 40000, -40000);
        let b = _mm256_setr_epi32(-1, 2, -3, 4, 100000, 9, -9, 8);
        assert_eq!(lanes_i16(_mm256_packs_epi32(a, b)), [1, -32768, 32767, 5, -1, 2, -3, 4, 6, -7, 32767, -32768, 32767, 9, -9, 8]);
        assert_eq!(lanes_u16(_mm256_packus_epi32(a, b)), [1, 0, 65535, 5, 0, 2, 0, 4, 6, 0, 40000, 0, 65535, 9, 0, 8]);

        let c = _mm256_setr_epi16(1, -300, 300, 4, 5, -6, 7, 8, 9, 10, -11, 200, 13, 14, 15, 16);
        let d = _mm256_setr_epi16(-1, 2, 3, 4, 5, 6, 7, 500, 17, 18, 19, 20, 21, 22, 23, -24);
        assert_eq!(lanes_i8(_mm256_packs_epi16(c, d)), [1, -128, 127, 4, 5, -6, 7, 8, -1, 2, 3, 4, 5, 6, 7, 127, 9, 10, -11, 127, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, -24]);
        assert_eq!(lanes_u8(_mm256_packus_epi16(c, d)), [1, 0, 255, 4, 5, 0, 7, 8, 0, 2, 3, 4, 5, 6, 7, 255, 9, 10, 0, 200, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 0]);

        let zero = _mm256_setzero_si256();
        let high = _mm256_setr_epi64x(0, 0, 0, 1);
        let ones = _mm256_set1_epi64x(-1);
        assert_eq!(_mm256_testz_si256(high, zero), 1);
        assert_eq!(_mm256_testz_si256(high, high), 0);
        assert_eq!(_mm256_testc_si256(ones, high), 1);
        assert_eq!(_mm256_testc_si256(zero, high), 0);
        assert_eq!(_mm256_testnzc_si256(high, ones), 1);
        assert_eq!(_mm256_testnzc_si256(ones, high), 0);
    }
}

fn main() {
    #[cfg(target_arch = "x86_64")]
    if is_x86_feature_detected!("avx2") {
        unsafe { x86::run() }
    }
}
