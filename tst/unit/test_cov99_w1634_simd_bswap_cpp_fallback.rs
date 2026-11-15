//@ crate-type: lib
//@ compile-only
//@ compile-flags: -C emit-cpp-only
//@ output-contains: .cpp assert(!"TODO: Platform intrinsic \"simd_bswap\"")
#![feature(intrinsics, repr_simd)]
#![allow(internal_features)]

#[repr(simd)]
#[derive(Clone, Copy)]
pub struct Lanes(pub [u32; 4]);

#[rustc_intrinsic]
unsafe fn simd_bswap<T>(value: T) -> T;

#[no_mangle]
pub unsafe fn swap_lane_bytes(value: Lanes) -> Lanes {
    simd_bswap(value)
}
