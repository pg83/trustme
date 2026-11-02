//@ compile-variant: -O
#![feature(core_intrinsics, repr_simd, f128)]
#![allow(internal_features)]
#[repr(simd)]
#[derive(Clone, Copy)]
struct Quad([f128; 2]);
fn main() {
    let sign = 1u128 << 127;
    for bits in [[0u128, sign], [0x3fff_u128 << 112, (0x4001_u128 << 112) | sign], [1u128, 0x7fff_u128 << 112]] {
        let input: Quad = unsafe { core::mem::transmute(bits) };
        let actual: [u128; 2] = unsafe {
            core::mem::transmute(core::intrinsics::simd::simd_neg(std::hint::black_box(input)))
        };
        assert_eq!(actual, [bits[0] ^ sign, bits[1] ^ sign]);
    }
}
