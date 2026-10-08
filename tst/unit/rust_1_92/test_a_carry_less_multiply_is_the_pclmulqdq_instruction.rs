// zip's crc32 goes through crc32fast, which folds with
// `_mm_clmulepi64_si128` once `pclmulqdq` is detected. rustc gives LLVM
// the `llvm.x86.pclmulqdq` intrinsic, and LLVM emits the instruction. We
// computed the product bit by bit in C, and zip's 4 GB test ran 8 times
// as long as with rustc. Each of the four immediates picks a different
// pair of halves, so the instruction's operand order shows here.
#[cfg(target_arch = "x86_64")]
fn check() {
    use std::arch::x86_64::*;

    fn clmul(a: u64, b: u64) -> u128 {
        let mut result = 0u128;
        for i in 0..64 {
            if (b >> i) & 1 != 0 {
                result ^= (a as u128) << i;
            }
        }
        result
    }

    #[target_feature(enable = "pclmulqdq")]
    unsafe fn all(a: __m128i, b: __m128i) -> [u128; 4] {
        unsafe {
            [
                std::mem::transmute(_mm_clmulepi64_si128::<0x00>(a, b)),
                std::mem::transmute(_mm_clmulepi64_si128::<0x01>(a, b)),
                std::mem::transmute(_mm_clmulepi64_si128::<0x10>(a, b)),
                std::mem::transmute(_mm_clmulepi64_si128::<0x11>(a, b)),
            ]
        }
    }

    if !is_x86_feature_detected!("pclmulqdq") {
        return;
    }
    let (a0, a1, b0, b1) = (0x8000_0000_0000_0001u64, 0x1234_5678_9abc_def0u64, 0xffff_0000_ffff_0001u64, 0x0f0f_f0f0_3c3c_c3c3u64);
    let a = unsafe { _mm_set_epi64x(a1 as i64, a0 as i64) };
    let b = unsafe { _mm_set_epi64x(b1 as i64, b0 as i64) };
    let got = unsafe { all(a, b) };
    assert_eq!(got, [clmul(a0, b0), clmul(a1, b0), clmul(a0, b1), clmul(a1, b1)]);
}

#[cfg(not(target_arch = "x86_64"))]
fn check() {}

fn main() {
    check();
}
