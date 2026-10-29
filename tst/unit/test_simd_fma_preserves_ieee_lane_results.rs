#![feature(core_intrinsics, repr_simd)]
#![allow(internal_features)]
use std::hint::black_box;
#[repr(simd)]
#[derive(Clone, Copy)]
struct Wide([f32; 8]);
#[repr(simd)]
#[derive(Clone, Copy)]
struct Precise([f64; 4]);
#[inline(never)]
fn fused32(a: Wide, b: Wide, c: Wide) -> Wide {
    unsafe { core::intrinsics::simd::simd_fma(a, b, c) }
}
#[inline(never)]
fn fused64(a: Precise, b: Precise, c: Precise) -> Precise {
    unsafe { core::intrinsics::simd::simd_fma(a, b, c) }
}
fn main() {
    let a = Wide([f32::from_bits(0x3f800001), -0.0, f32::INFINITY, 0.0, f32::NAN, f32::NEG_INFINITY, 3.0, f32::MIN_POSITIVE]);
    let b = Wide([f32::from_bits(0x3f7ffffe), 2.0, 2.0, f32::INFINITY, 1.0, 2.0, -4.0, 0.5]);
    let c = Wide([-1.0, -0.0, f32::NEG_INFINITY, 1.0, 5.0, f32::INFINITY, 12.0, 0.0]);
    let result: [f32; 8] = unsafe { std::mem::transmute(fused32(black_box(a), black_box(b), black_box(c))) };
    assert_eq!(result[0].to_bits(), 0xa8800000);
    assert_eq!(result[1].to_bits(), 0x80000000);
    for lane in 2..6 { assert!(result[lane].is_nan()); }
    assert_eq!(result[6].to_bits(), 0);
    assert_eq!(result[7].to_bits(), 0x00400000);
    let a = Precise([f64::from_bits(0x3ff0000000000001), -0.0, f64::MIN_POSITIVE, f64::INFINITY]);
    let b = Precise([f64::from_bits(0x3feffffffffffffe), 2.0, 0.5, 0.0]);
    let c = Precise([-1.0, -0.0, 0.0, 7.0]);
    let result: [f64; 4] = unsafe { std::mem::transmute(fused64(black_box(a), black_box(b), black_box(c))) };
    assert_eq!(result[0].to_bits(), 0xb970000000000000);
    assert_eq!(result[1].to_bits(), 0x8000000000000000);
    assert_eq!(result[2].to_bits(), 0x0008000000000000);
    assert!(result[3].is_nan());
}
