//@ compile-variant: -O
#![feature(core_intrinsics, repr_simd)]
#![allow(internal_features)]
use core::intrinsics::simd::*;
#[repr(simd)]
#[derive(Clone, Copy)]
struct Singles([f32; 4]);
#[repr(simd)]
#[derive(Clone, Copy)]
struct Words([u32; 4]);
fn main() {
    unsafe {
        let a = std::hint::black_box(Singles([1.25, -2.5, 4.0, -0.0]));
        let b = std::hint::black_box(Singles([2.0, 4.0, 8.0, 2.0]));
        let product: [f32; 4] = core::mem::transmute(simd_mul(a, b));
        let quotient: [f32; 4] = core::mem::transmute(simd_div(a, b));
        assert_eq!(product, [2.5, -10.0, 32.0, -0.0]);
        assert_eq!(quotient, [0.625, -0.625, 0.5, -0.0]);
        assert_eq!(product[3].to_bits(), 0x8000_0000);
        assert_eq!(quotient[3].to_bits(), 0x8000_0000);
        let x = [0xf0f0_000fu32, 0x8000_0000, 0, 0xffff_ffff];
        let y = [0x00ff_00ffu32, 0x8000_0001, 7, 0x5555_5555];
        let actual: [u32; 4] = core::mem::transmute(simd_xor(std::hint::black_box(Words(x)), Words(y)));
        for lane in 0..4 { assert_eq!(actual[lane], x[lane] ^ y[lane]); }
    }
}
