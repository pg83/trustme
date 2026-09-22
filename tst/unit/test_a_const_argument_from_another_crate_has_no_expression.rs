//@ aux-build: sse2_lanes.rs
// A const argument of a function from another crate arrives as MIR only:
// `_mm_srli_epi16(self, BITS)` in the aux crate is
// `_mm_srli_epi16::<{ BITS }>(self)`, and the anonymous constant's
// expression tree stays in the crate that wrote it (upstream loads an anon
// const's body, never its HIR). The bind pass read the root of that
// missing tree to classify the constant and crashed; regex-automata
// reaches aho-corasick's `impl Vector for __m256i` so.
#[cfg(target_arch = "x86_64")]
use std::arch::x86_64::{__m128i, _mm_set1_epi16, _mm_storeu_si128};

#[cfg(target_arch = "x86_64")]
fn main() {
    use sse2_lanes::Lanes;
    let mut out = [0u16; 8];
    unsafe {
        let lanes: __m128i = _mm_set1_epi16(256);
        _mm_storeu_si128(out.as_mut_ptr() as *mut __m128i, lanes.shift_right::<4>());
    }
    assert_eq!(out, [16u16; 8]);
}

#[cfg(not(target_arch = "x86_64"))]
fn main() {}
