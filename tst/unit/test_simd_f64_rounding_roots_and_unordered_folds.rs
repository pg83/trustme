//@ compile-variant: -O
#![feature(core_intrinsics, repr_simd)]
#![allow(internal_features)]
use core::intrinsics::simd::*;
#[repr(simd)]
#[derive(Clone, Copy)]
struct Lanes([f64; 4]);
fn main() {
    let input = [-3.25f64, -0.0, 2.5, 7.125];
    let vector = std::hint::black_box(Lanes(input));
    unsafe {
        let ceiling: [f64; 4] = core::mem::transmute(simd_ceil(vector));
        let floor: [f64; 4] = core::mem::transmute(simd_floor(vector));
        assert_eq!(ceiling, [-3.0, -0.0, 3.0, 8.0]);
        assert_eq!(floor, [-4.0, -0.0, 2.0, 7.0]);
        assert_eq!(ceiling[1].to_bits(), (-0.0f64).to_bits());
        assert_eq!(floor[1].to_bits(), (-0.0f64).to_bits());
        let roots: [f64; 4] = core::mem::transmute(simd_fsqrt(std::hint::black_box(Lanes([0.0, 4.0, 25.0, 81.0]))));
        assert_eq!(roots, [0.0, 2.0, 5.0, 9.0]);
        let exact = std::hint::black_box(Lanes([2.0, 4.0, 8.0, 16.0]));
        assert_eq!(simd_reduce_add_unordered::<_, f64>(exact), 30.0);
        assert_eq!(simd_reduce_mul_unordered::<_, f64>(exact), 1024.0);
    }
}
