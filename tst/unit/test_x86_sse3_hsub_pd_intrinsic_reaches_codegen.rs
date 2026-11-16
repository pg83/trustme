//@ crate-type: lib
//@ compile-only
//@ edition: 2024
//@ compile-flags: --target=x86_64-unknown-linux-gnu --emit=link -Copt-level=0 -Cpanic=abort
#![feature(no_core, lang_items, link_llvm_intrinsics, abi_unadjusted, repr_simd, simd_ffi)]
#![no_core]
#![allow(internal_features, improper_ctypes)]

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
    #[link_name = "llvm.x86.sse3.hsub.pd"]
    fn hsub_pd(a: F64x2, b: F64x2) -> F64x2;
}

#[unsafe(no_mangle)]
#[target_feature(enable = "sse3")]
pub unsafe fn horizontal_subtract_f64x2(a: F64x2, b: F64x2) -> F64x2 {
    unsafe { hsub_pd(a, b) }
}
