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
    #[link_name = "llvm.x86.sse41.dppd"]
    fn dppd(lhs: F64x2, rhs: F64x2, mask: i8) -> F64x2;
}

#[unsafe(no_mangle)]
#[target_feature(enable = "sse4.1")]
pub unsafe extern "unadjusted" fn dot_both_to_low(lhs: F64x2, rhs: F64x2) -> F64x2 {
    unsafe { dppd(lhs, rhs, 0x31) }
}

#[unsafe(no_mangle)]
#[target_feature(enable = "sse4.1")]
pub unsafe extern "unadjusted" fn dot_low_to_high(lhs: F64x2, rhs: F64x2) -> F64x2 {
    unsafe { dppd(lhs, rhs, 0x12) }
}

#[unsafe(no_mangle)]
#[target_feature(enable = "sse4.1")]
pub unsafe extern "unadjusted" fn dot_high_to_both(lhs: F64x2, rhs: F64x2) -> F64x2 {
    unsafe { dppd(lhs, rhs, 0x23) }
}

#[unsafe(no_mangle)]
#[target_feature(enable = "sse4.1")]
pub unsafe extern "unadjusted" fn dot_neither_to_neither(lhs: F64x2, rhs: F64x2) -> F64x2 {
    unsafe { dppd(lhs, rhs, 0x00) }
}
