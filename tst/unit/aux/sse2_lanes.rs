#[cfg(target_arch = "x86_64")]
use std::arch::x86_64::{__m128i, _mm_srli_epi16};

pub trait Lanes: Copy {
    fn shift_right<const BITS: i32>(self) -> Self;
}

#[cfg(target_arch = "x86_64")]
impl Lanes for __m128i {
    #[inline(always)]
    fn shift_right<const BITS: i32>(self) -> Self {
        unsafe { _mm_srli_epi16(self, BITS) }
    }
}
