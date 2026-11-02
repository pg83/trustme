//@ compile-variant: -O
#![feature(core_intrinsics, repr_simd, f16)]
#![allow(internal_features)]
#[repr(simd)]
#[derive(Clone, Copy)]
struct Half([f16; 8]);
fn main() {
    let bits = [0x3c00u16, 0xc000, 0x0000, 0x8000, 0x7c00, 0xfc00, 0x0001, 0x3555];
    let input: Half = unsafe { core::mem::transmute(bits) };
    let actual: [u16; 8] = unsafe {
        core::mem::transmute(core::intrinsics::simd::simd_neg(std::hint::black_box(input)))
    };
    for lane in 0..8 { assert_eq!(actual[lane], bits[lane] ^ 0x8000); }
}
