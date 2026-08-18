// The one-argument SIMD reductions fold a vector to a scalar, and `repr(Rust)`
// names the representation a type has with no `repr` at all.
#![feature(portable_simd)]

use std::simd::num::{SimdFloat, SimdInt};
use std::simd::{i32x4, f32x4, StdFloat, cmp::SimdPartialOrd};

#[repr(Rust)]
struct A;

#[repr(Rust, align(16))]
struct B(u8);

#[repr(Rust, packed)]
struct C(u8, u32);

fn main() {
    let _ = A;
    assert_eq!(std::mem::align_of::<B>(), 16);
    assert_eq!(std::mem::size_of::<C>(), 5);

    let v = i32x4::from_array([1, 2, 3, 4]);
    assert_eq!(v.reduce_sum(), 10);
    assert_eq!(v.reduce_product(), 24);
    assert_eq!(v.reduce_min(), 1);
    assert_eq!(v.reduce_max(), 4);
    assert_eq!(v.reduce_and(), 0);
    assert_eq!(v.reduce_or(), 7);
    assert_eq!(v.reduce_xor(), 4);

    let mask = v.simd_gt(i32x4::splat(0));
    assert!(mask.all());
    assert!(mask.any());
    assert!(!v.simd_gt(i32x4::splat(9)).any());

    let f = f32x4::from_array([-1.5, 0.25, 2.75, -0.5]);
    assert_eq!(f.abs().to_array(), [1.5, 0.25, 2.75, 0.5]);
    assert_eq!(f.round().to_array(), [-2.0, 0.0, 3.0, -1.0]);
    assert_eq!(f.trunc().to_array(), [-1.0, 0.0, 2.0, -0.0]);
}
