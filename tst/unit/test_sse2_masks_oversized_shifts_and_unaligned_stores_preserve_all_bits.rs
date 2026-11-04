//@ compile-variant: -O
use std::arch::x86_64::*;

unsafe fn check() {
    let signs: __m128i = std::mem::transmute([
        0i8, -1, 0, -1, -1, 0, -1, 0, -1, 0, -1, 0, 0, -1, 0, -1,
    ]);
    assert_eq!(_mm_movemask_epi8(signs), 0xa55a);
    let input: __m128i = std::mem::transmute([0x80000001u32, 3, 0x100, 0xffffffff]);
    let right: [u32; 4] = std::mem::transmute(_mm_srli_epi32::<4>(input));
    let left: [u32; 4] = std::mem::transmute(_mm_slli_epi32::<4>(input));
    let full: [u32; 4] = std::mem::transmute(_mm_srli_epi32::<32>(input));
    let oversized: [u32; 4] = std::mem::transmute(_mm_slli_epi32::<255>(input));
    assert_eq!(right, [0x08000000, 0, 0x10, 0x0fffffff]);
    assert_eq!(left, [0x10, 0x30, 0x1000, 0xfffffff0]);
    assert_eq!(full, [0; 4]);
    assert_eq!(oversized, [0; 4]);
    let mut bytes = [0x5au8; 18];
    _mm_storeu_si128(bytes.as_mut_ptr().add(1) as *mut __m128i, input);
    assert_eq!(&bytes[1..17], &[1, 0, 0, 128, 3, 0, 0, 0, 0, 1, 0, 0, 255, 255, 255, 255]);
    assert_eq!((bytes[0], bytes[17]), (0x5a, 0x5a));
}

fn main() { unsafe { check(); } }
