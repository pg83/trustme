#[cfg(target_arch = "x86_64")]
use core::arch::x86_64::{__m128i, _mm_clmulepi64_si128};

#[cfg(target_arch = "x86_64")]
#[target_feature(enable = "pclmulqdq")]
unsafe fn test_pclmulqdq() {
    let a: __m128i = core::mem::transmute([3_u64, 1_u64 << 63]);
    let b: __m128i = core::mem::transmute([5_u64, 2_u64]);

    let low: [u64; 2] = core::mem::transmute(_mm_clmulepi64_si128::<0x00>(a, b));
    assert_eq!(low, [15, 0]);

    let high: [u64; 2] = core::mem::transmute(_mm_clmulepi64_si128::<0x11>(a, b));
    assert_eq!(high, [0, 1]);
}

fn main() {
    #[cfg(target_arch = "x86_64")]
    unsafe {
        test_pclmulqdq();
    }
}
