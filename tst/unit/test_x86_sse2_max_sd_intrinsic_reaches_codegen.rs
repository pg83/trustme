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
    #[link_name = "llvm.x86.sse2.max.sd"]
    fn max_sd(a: F64x2, b: F64x2) -> F64x2;
}

#[unsafe(no_mangle)]
#[target_feature(enable = "sse2")]
pub unsafe extern "unadjusted" fn sse2_max_sd(a: F64x2, b: F64x2) -> F64x2 {
    unsafe { max_sd(a, b) }
}
