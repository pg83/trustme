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
pub struct F64x2([f64; 2]);

unsafe extern "unadjusted" {
    #[link_name = "llvm.x86.sse41.round.pd"]
    fn round_pd(input: F64x2, mode: i32) -> F64x2;
}

#[unsafe(no_mangle)]
#[target_feature(enable = "sse4.1")]
pub unsafe extern "unadjusted" fn round_nearest(input: F64x2) -> F64x2 {
    unsafe { round_pd(input, 0x00) }
}

#[unsafe(no_mangle)]
#[target_feature(enable = "sse4.1")]
pub unsafe extern "unadjusted" fn round_down(input: F64x2) -> F64x2 {
    unsafe { round_pd(input, 0x01) }
}

#[unsafe(no_mangle)]
#[target_feature(enable = "sse4.1")]
pub unsafe extern "unadjusted" fn round_up(input: F64x2) -> F64x2 {
    unsafe { round_pd(input, 0x02) }
}

#[unsafe(no_mangle)]
#[target_feature(enable = "sse4.1")]
pub unsafe extern "unadjusted" fn round_toward_zero(input: F64x2) -> F64x2 {
    unsafe { round_pd(input, 0x03) }
}

#[unsafe(no_mangle)]
#[target_feature(enable = "sse4.1")]
pub unsafe extern "unadjusted" fn round_current(input: F64x2) -> F64x2 {
    unsafe { round_pd(input, 0x04) }
}

#[unsafe(no_mangle)]
#[target_feature(enable = "sse4.1")]
pub unsafe extern "unadjusted" fn round_nearest_suppress_precision(input: F64x2) -> F64x2 {
    unsafe { round_pd(input, 0x08) }
}

#[unsafe(no_mangle)]
#[target_feature(enable = "sse4.1")]
pub unsafe extern "unadjusted" fn round_down_suppress_precision(input: F64x2) -> F64x2 {
    unsafe { round_pd(input, 0x09) }
}

#[unsafe(no_mangle)]
#[target_feature(enable = "sse4.1")]
pub unsafe extern "unadjusted" fn round_up_suppress_precision(input: F64x2) -> F64x2 {
    unsafe { round_pd(input, 0x0a) }
}

#[unsafe(no_mangle)]
#[target_feature(enable = "sse4.1")]
pub unsafe extern "unadjusted" fn round_toward_zero_suppress_precision(input: F64x2) -> F64x2 {
    unsafe { round_pd(input, 0x0b) }
}

#[unsafe(no_mangle)]
#[target_feature(enable = "sse4.1")]
pub unsafe extern "unadjusted" fn round_current_suppress_precision(input: F64x2) -> F64x2 {
    unsafe { round_pd(input, 0x0c) }
}
