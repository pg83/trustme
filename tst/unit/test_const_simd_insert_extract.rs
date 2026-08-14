#![feature(core_intrinsics, repr_simd)]
#![allow(internal_features)]

#[repr(simd)]
#[derive(Copy, Clone)]
struct U32x4([u32; 4]);

const EXTRACTED: u32 = unsafe {
    core::intrinsics::simd::simd_extract(U32x4([10, 20, 30, 40]), 2)
};

const INSERTED: U32x4 = unsafe {
    core::intrinsics::simd::simd_insert(U32x4([10, 20, 30, 40]), 1, 99u32)
};

fn main() {
    assert_eq!(EXTRACTED, 30);
    assert_eq!(INSERTED.0, [10, 99, 30, 40]);
}
