#![feature(repr_simd, platform_intrinsics)]
use std::hint::black_box;
#[repr(simd)]
#[derive(Clone, Copy)]
struct Input([f32; 8]);
const BITS: [u32; 16] = [0x3f800000, 0xbf800000, 0x7fc01234, 0x80000000, 0x7f800000, 0xff800000, 0x00000001, 0x3f000000, 0x40000000, 0xc0000000, 0xffc04321, 0x00000000, 0x00800000, 0x7f7fffff, 0x40400000, 0xc0400000];
const EXPECTED: [u32; 16] = [0x80000000, 0xffc04321, 0xbf800000, 0x40000000, 0xc0400000, 0x00000001, 0x7f7fffff, 0x7f800000, 0x00000000, 0x7fc01234, 0xc0000000, 0x3f800000, 0x3f000000, 0x40400000, 0xff800000, 0x00800000];
const fn indices<const N: usize>() -> [u32; N] {
    let mut map = [0; N];
    let mut i = 0;
    while i < N { map[i] = ((i * 7 + 3) % 16) as u32; i += 1; }
    map
}
macro_rules! verify {
    ($width:literal, $intrinsic:ident, $output:ident, $check:ident) => {
        #[repr(simd)]
        #[derive(Clone, Copy)]
        struct $output([f32; $width]);
        extern "platform-intrinsic" {
            fn $intrinsic<T, U>(left: T, right: T, indices: [u32; $width]) -> U;
        }
        fn $check(left: Input, right: Input) {
            let result: $output = unsafe { $intrinsic(left, right, const { indices::<$width>() }) };
            let actual: [u32; $width] = unsafe { std::mem::transmute(result) };
            for i in 0..$width { assert_eq!(actual[i], EXPECTED[i % 16]); }
        }
    };
}
verify!(8, simd_shuffle8, Eight, eight);
verify!(16, simd_shuffle16, Sixteen, sixteen);
verify!(32, simd_shuffle32, ThirtyTwo, thirty_two);
verify!(64, simd_shuffle64, SixtyFour, sixty_four);
verify!(128, simd_shuffle128, OneTwentyEight, one_twenty_eight);
fn main() {
    let left = Input(black_box(std::array::from_fn(|i| f32::from_bits(BITS[i]))));
    let right = Input(black_box(std::array::from_fn(|i| f32::from_bits(BITS[i + 8]))));
    eight(left, right); sixteen(left, right); thirty_two(left, right);
    sixty_four(left, right); one_twenty_eight(left, right);
}
