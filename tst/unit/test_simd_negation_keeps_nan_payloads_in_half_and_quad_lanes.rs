//@ compile-variant: -O
#![feature(core_intrinsics, repr_simd, f16, f128)]
#![allow(internal_features)]
#[repr(simd)]
#[derive(Clone, Copy)]
struct Half([f16; 4]);
#[repr(simd)]
#[derive(Clone, Copy)]
struct Quad([f128; 2]);
fn main() {
    unsafe {
        let half_bits = [0x7e31u16, 0xfe27, 0x7c01, 0xfc03];
        let half: Half = core::mem::transmute(half_bits);
        let actual: [u16; 4] = core::mem::transmute(core::intrinsics::simd::simd_neg(std::hint::black_box(half)));
        assert_eq!(actual, [0xfe31, 0x7e27, 0xfc01, 0x7c03]);
        let quiet = (0x7fff_u128 << 112) | (1u128 << 111) | 0x29;
        let signaling = (0xffff_u128 << 112) | 0x53;
        let quad: Quad = core::mem::transmute([quiet, signaling]);
        let actual: [u128; 2] = core::mem::transmute(core::intrinsics::simd::simd_neg(std::hint::black_box(quad)));
        assert_eq!(actual, [quiet ^ (1u128 << 127), signaling ^ (1u128 << 127)]);
    }
}
