// llvm.x86.avx.vzeroupper (_mm256_zeroupper) had no lowering and aborted on
// `assert(!"Extern LLVM: ...")`; memchr's AVX2 searchers call it on their
// way out, so every program that searched a string with memchr on an AVX2
// machine aborted. It only clears the upper halves of the YMM registers for
// speed, which no Rust value can observe, so it lowers to nothing.
#[cfg(target_arch = "x86_64")]
use std::arch::x86_64::{_mm256_set1_epi32, _mm256_storeu_si256, _mm256_zeroall, _mm256_zeroupper, __m256i};

#[cfg(target_arch = "x86_64")]
#[target_feature(enable = "avx")]
unsafe fn keep_value() -> [i32; 8] {
    let value = _mm256_set1_epi32(5);
    _mm256_zeroupper();
    _mm256_zeroall();
    let mut out = [0i32; 8];
    _mm256_storeu_si256(out.as_mut_ptr() as *mut __m256i, value);
    out
}

fn main() {
    #[cfg(target_arch = "x86_64")]
    if std::is_x86_feature_detected!("avx") {
        assert_eq!(unsafe { keep_value() }, [5; 8]);
        return;
    }
    println!("skip");
}
