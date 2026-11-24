//@ edition: 2024
//@ compile-flags: --target=x86_64-unknown-linux-gnu -Copt-level=0 -Cpanic=abort
//@ output-contains: .cpp __builtin_ia32_maxps256(lhs, rhs)
//@ output-contains: .cpp __builtin_ia32_maxpd256(lhs, rhs)
#![feature(abi_unadjusted, link_llvm_intrinsics, repr_simd, simd_ffi)]
#![allow(internal_features, improper_ctypes)]

use std::{hint::black_box, mem::transmute};

#[repr(simd)]
struct Samples([f32; 8]);
#[repr(simd)]
struct Precise([f64; 4]);

unsafe extern "unadjusted" {
    #[link_name = "llvm.x86.avx.max.ps.256"]
    fn max_samples(left: Samples, right: Samples) -> Samples;
    #[link_name = "llvm.x86.avx.max.pd.256"]
    fn max_precise(left: Precise, right: Precise) -> Precise;
}

#[inline(never)]
#[target_feature(enable = "avx")]
unsafe fn check_operand_bits() {
    let left = Samples([2.0, -8.0, 0.0, -0.0, f32::from_bits(0x7fc01234), 5.0,
                        f32::NEG_INFINITY, f32::INFINITY]);
    let right = Samples([1.0, -3.0, -0.0, 0.0, 6.0, f32::from_bits(0x7fc05678), -7.0, 9.0]);
    let result: [f32; 8] = unsafe { transmute(max_samples(black_box(left), black_box(right))) };
    assert_eq!(result.map(f32::to_bits),
               [0x40000000, 0xc0400000, 0x80000000, 0, 0x40c00000, 0x7fc05678,
                0xc0e00000, 0x7f800000]);

    let left = Precise([-6.0, -0.0, f64::from_bits(0x7ff8000000001234), 9.0]);
    let right = Precise([-4.0, 0.0, 7.0, f64::from_bits(0x7ff8000000005678)]);
    let result: [f64; 4] = unsafe { transmute(max_precise(black_box(left), black_box(right))) };
    assert_eq!(result.map(f64::to_bits),
               [0xc010000000000000, 0, 0x401c000000000000, 0x7ff8000000005678]);
}

fn main() {
    if std::is_x86_feature_detected!("avx") {
        unsafe { check_operand_bits() };
    }
}
