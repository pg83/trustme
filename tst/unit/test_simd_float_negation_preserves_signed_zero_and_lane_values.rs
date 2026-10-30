#![feature(core_intrinsics, repr_simd)]
#![allow(internal_features)]
#[repr(simd)]
#[derive(Clone, Copy)]
struct Single([f32; 8]);
#[repr(simd)]
#[derive(Clone, Copy)]
struct Double([f64; 8]);
fn main() {
    let values = [0.0f32, -0.0, f32::INFINITY, f32::NEG_INFINITY, 3.5, -2.25, f32::from_bits(1), f32::NAN];
    let result: [f32; 8] = unsafe {
        std::mem::transmute(std::intrinsics::simd::simd_neg(std::hint::black_box(Single(values))))
    };
    for lane in 0..8 {
        if values[lane].is_nan() { assert!(result[lane].is_nan()); }
        else { assert_eq!(result[lane].to_bits(), values[lane].to_bits() ^ (1u32 << 31)); }
    }
    let values = [0.0f64, -0.0, f64::INFINITY, f64::NEG_INFINITY, 9.25, -0.125, f64::from_bits(1), f64::NAN];
    let result: [f64; 8] = unsafe {
        std::mem::transmute(std::intrinsics::simd::simd_neg(std::hint::black_box(Double(values))))
    };
    for lane in 0..8 {
        if values[lane].is_nan() { assert!(result[lane].is_nan()); }
        else { assert_eq!(result[lane].to_bits(), values[lane].to_bits() ^ (1u64 << 63)); }
    }
}
