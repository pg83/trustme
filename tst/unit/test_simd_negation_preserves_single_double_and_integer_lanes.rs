//@ compile-variant: -O
#![feature(core_intrinsics, repr_simd)]
#![allow(internal_features)]
#[repr(simd)]
#[derive(Clone, Copy)]
struct Single([f32; 4]);
#[repr(simd)]
#[derive(Clone, Copy)]
struct Double([f64; 2]);
#[repr(simd)]
#[derive(Clone, Copy)]
struct Integers([i16; 8]);
fn main() {
    unsafe {
        let single = [0.0f32, -0.0, 0.75, -9.5];
        let negated: [f32; 4] = core::mem::transmute(core::intrinsics::simd::simd_neg(std::hint::black_box(Single(single))));
        for lane in 0..4 { assert_eq!(negated[lane].to_bits(), single[lane].to_bits() ^ (1u32 << 31)); }
        let double = [f64::INFINITY, f64::NEG_INFINITY];
        let negated: [f64; 2] = core::mem::transmute(core::intrinsics::simd::simd_neg(std::hint::black_box(Double(double))));
        assert_eq!(negated, [f64::NEG_INFINITY, f64::INFINITY]);
        let integers = [i16::MIN, i16::MAX, -1, 0, 1, 71, -273, 8192];
        let negated: [i16; 8] = core::mem::transmute(core::intrinsics::simd::simd_neg(std::hint::black_box(Integers(integers))));
        for lane in 0..8 { assert_eq!(negated[lane], integers[lane].wrapping_neg()); }
    }
}
