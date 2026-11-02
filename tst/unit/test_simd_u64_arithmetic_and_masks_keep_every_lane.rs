//@ compile-variant: -O
#![feature(core_intrinsics, repr_simd)]
#![allow(internal_features)]
use core::intrinsics::simd::*;
#[repr(simd)]
#[derive(Clone, Copy)]
struct Wide([u64; 4]);
#[repr(simd)]
#[derive(Clone, Copy)]
struct Mask([i64; 4]);
fn main() {
    let left = [24u64, 63, 1 << 40, u64::MAX - 7];
    let right = [3u64, 7, 16, 8];
    let a = std::hint::black_box(Wide(left));
    let b = std::hint::black_box(Wide(right));
    unsafe {
        let sub: [u64; 4] = core::mem::transmute(simd_sub(a, b));
        let mul: [u64; 4] = core::mem::transmute(simd_mul(a, b));
        let div: [u64; 4] = core::mem::transmute(simd_div(a, b));
        let and: [u64; 4] = core::mem::transmute(simd_and(a, b));
        let shift: [u64; 4] = core::mem::transmute(simd_shl(a, b));
        let ne: Mask = simd_ne(a, b);
        let le: Mask = simd_le(a, b);
        let ge: Mask = simd_ge(a, b);
        let ne: [i64; 4] = core::mem::transmute(ne);
        let le: [i64; 4] = core::mem::transmute(le);
        let ge: [i64; 4] = core::mem::transmute(ge);
        for lane in 0..4 {
            assert_eq!(sub[lane], left[lane].wrapping_sub(right[lane]));
            assert_eq!(mul[lane], left[lane].wrapping_mul(right[lane]));
            assert_eq!(div[lane], left[lane] / right[lane]);
            assert_eq!(and[lane], left[lane] & right[lane]);
            assert_eq!(shift[lane], left[lane].wrapping_shl(right[lane] as u32));
            assert_eq!(ne[lane], if left[lane] != right[lane] { -1 } else { 0 });
            assert_eq!(le[lane], if left[lane] <= right[lane] { -1 } else { 0 });
            assert_eq!(ge[lane], if left[lane] >= right[lane] { -1 } else { 0 });
        }
    }
}
