#![feature(core_intrinsics, repr_simd)]
#![allow(internal_features)]
#[repr(simd)]
#[derive(Copy, Clone)]
struct Lanes([f64; 4]);
const VALUE: Lanes = unsafe { core::intrinsics::simd::simd_insert(Lanes([1.5, -2.5, 3.5, -0.0]), 2, -7.25f64) };
const EXTRACTED: f64 = unsafe { core::intrinsics::simd::simd_extract(VALUE, 2) };
const ARRAY: [f64; 4] = unsafe { core::mem::transmute(VALUE) };
fn main() {
    assert_eq!(ARRAY, [1.5, -2.5, -7.25, -0.0]);
    assert_eq!(ARRAY[3].to_bits(), (-0.0f64).to_bits());
    assert_eq!(EXTRACTED, -7.25);
}
