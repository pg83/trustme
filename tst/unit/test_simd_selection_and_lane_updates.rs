#![feature(core_intrinsics, repr_simd)]
#![allow(internal_features)]

use core::intrinsics::simd::{simd_extract, simd_insert, simd_select_bitmask};

#[repr(simd)]
#[derive(Clone, Copy)]
struct Lanes([i32; 4]);

#[inline(never)]
fn select(mask: u8, left: Lanes, right: Lanes) -> Lanes {
    unsafe { simd_select_bitmask(mask, left, right) }
}

fn main() {
    let left = std::hint::black_box(Lanes([11, -22, 33, -44]));
    let right = std::hint::black_box(Lanes([-5, 6, -7, 8]));
    for mask in 0u8..16 {
        let selected = select(mask, left, right);
        for lane in 0..4 {
            let expected = if mask & (1 << lane) != 0 { left.0[lane] } else { right.0[lane] };
            assert_eq!(selected.0[lane], expected);
        }
    }
    let changed: Lanes = unsafe { simd_insert(left, 2, -99i32) };
    let extracted: i32 = unsafe { simd_extract(changed, 2) };
    assert_eq!(extracted, -99);
    assert_eq!(changed.0, [11, -22, -99, -44]);
    assert_eq!(left.0, [11, -22, 33, -44]);
}
