// `_mm256_stream_si256` (an `asm!` with `in(ymm_reg)` under
// `#[target_feature(enable = "avx")]` in stdarch) and an `inout("ymm0")`
// operand of a `__m256`. rustc gives a `#[target_feature]` function the
// features as its LLVM `target-features`, closures inheriting them, so a
// 256-bit operand in a vector register is valid there. We ignored the
// attribute, so clang rejected the `x` constraint of a 32-byte operand in a
// function compiled without AVX, and an explicit `ymm0` was bound to a
// `uintptr_t`. pulp, under exr under image.
#[cfg(target_arch = "x86_64")]
mod x86 {
    use std::arch::x86_64::*;

    #[target_feature(enable = "avx")]
    pub unsafe fn stream(dst: *mut __m256i, value: __m256i) {
        _mm256_stream_si256(dst, value);
        _mm_sfence();
    }

    #[target_feature(enable = "avx")]
    pub unsafe fn doubled(value: __m256) -> __m256 {
        let out: __m256;
        core::arch::asm!("vaddps ymm0, ymm0, ymm0", inout("ymm0") value => out, options(nomem, nostack));
        out
    }

    #[target_feature(enable = "avx")]
    pub unsafe fn run() {
        #[repr(align(32))]
        struct Aligned([i64; 4]);
        let mut slot = Aligned([0; 4]);
        stream(slot.0.as_mut_ptr() as *mut __m256i, _mm256_set_epi64x(4, 3, 2, 1));
        assert_eq!(slot.0, [1, 2, 3, 4]);

        let mut lanes = [0f32; 8];
        _mm256_storeu_ps(lanes.as_mut_ptr(), doubled(_mm256_setr_ps(1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0)));
        assert_eq!(lanes, [2.0, 4.0, 6.0, 8.0, 10.0, 12.0, 14.0, 16.0]);
    }
}

fn main() {
    #[cfg(target_arch = "x86_64")]
    if is_x86_feature_detected!("avx") {
        unsafe { x86::run() }
    }
}
