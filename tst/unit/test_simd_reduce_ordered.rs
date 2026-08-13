#![feature(core_intrinsics, repr_simd)]
#![allow(internal_features)]

#[repr(simd)]
#[derive(Copy, Clone)]
struct F32x4([f32; 4]);

#[repr(simd)]
#[derive(Copy, Clone)]
struct U32x4([u32; 4]);

#[repr(simd)]
#[derive(Copy, Clone)]
struct I32x2([i32; 2]);

fn main() {
    let floats = F32x4([1.0, 2.0, 4.0, 8.0]);
    let sum = unsafe { core::intrinsics::simd::simd_reduce_add_ordered(floats, 16.0f32) };
    assert_eq!(sum, 31.0);

    let integers = U32x4([1, 2, 3, 4]);
    let product = unsafe { core::intrinsics::simd::simd_reduce_mul_ordered(integers, 2u32) };
    assert_eq!(product, 48);

    let overflowing = I32x2([i32::MAX, 1]);
    let wrapped = unsafe { core::intrinsics::simd::simd_reduce_add_ordered(overflowing, 0i32) };
    assert_eq!(wrapped, i32::MIN);
}
