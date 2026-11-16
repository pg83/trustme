//@ crate-type: lib
//@ compile-only
//@ edition: 2024
//@ compile-flags: --target=x86_64-unknown-linux-gnu --emit=link -Copt-level=0 -Cpanic=abort
#![feature(no_core, lang_items, abi_unadjusted, link_llvm_intrinsics, repr_simd, simd_ffi)]
#![no_core]
#![allow(internal_features, improper_ctypes, improper_ctypes_definitions)]

#[lang = "pointee_sized"]
pub trait PointeeSized {}
#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}
#[lang = "sized"]
pub trait Sized: MetaSized {}
#[lang = "copy"]
pub trait Copy {}

#[repr(simd)]
pub struct F32x4([f32; 4]);

unsafe extern "unadjusted" {
    #[link_name = "llvm.x86.sse41.dpps"]
    fn dpps(lhs: F32x4, rhs: F32x4, mask: i8) -> F32x4;
}

#[unsafe(no_mangle)]
#[target_feature(enable = "sse4.1")]
pub unsafe extern "unadjusted" fn dpps_all_products_all_lanes(lhs: F32x4, rhs: F32x4) -> F32x4 {
    unsafe { dpps(lhs, rhs, 0xff_u8 as i8) }
}

#[unsafe(no_mangle)]
#[target_feature(enable = "sse4.1")]
pub unsafe extern "unadjusted" fn dpps_odd_products_even_lanes(lhs: F32x4, rhs: F32x4) -> F32x4 {
    unsafe { dpps(lhs, rhs, 0xa5_u8 as i8) }
}

#[unsafe(no_mangle)]
#[target_feature(enable = "sse4.1")]
pub unsafe extern "unadjusted" fn dpps_even_products_odd_lanes(lhs: F32x4, rhs: F32x4) -> F32x4 {
    unsafe { dpps(lhs, rhs, 0x5a) }
}

#[unsafe(no_mangle)]
#[target_feature(enable = "sse4.1")]
pub unsafe extern "unadjusted" fn dpps_all_products_no_lanes(lhs: F32x4, rhs: F32x4) -> F32x4 {
    unsafe { dpps(lhs, rhs, 0xf0_u8 as i8) }
}

#[unsafe(no_mangle)]
#[target_feature(enable = "sse4.1")]
pub unsafe extern "unadjusted" fn dpps_no_products_all_lanes(lhs: F32x4, rhs: F32x4) -> F32x4 {
    unsafe { dpps(lhs, rhs, 0x0f) }
}

#[unsafe(no_mangle)]
#[target_feature(enable = "sse4.1")]
pub unsafe extern "unadjusted" fn dpps_no_products_no_lanes(lhs: F32x4, rhs: F32x4) -> F32x4 {
    unsafe { dpps(lhs, rhs, 0x00) }
}
