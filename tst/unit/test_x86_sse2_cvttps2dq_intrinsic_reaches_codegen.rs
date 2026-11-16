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
pub struct F32x4([f32; 4]);
#[repr(simd)]
pub struct I32x4([i32; 4]);

unsafe extern "unadjusted" {
    #[link_name = "llvm.x86.sse2.cvttps2dq"]
    fn cvttps2dq(input: F32x4) -> I32x4;
}

#[unsafe(no_mangle)]
#[target_feature(enable = "sse2")]
pub unsafe fn truncate_f32x4_to_i32x4(input: F32x4) -> I32x4 {
    unsafe { cvttps2dq(input) }
}
