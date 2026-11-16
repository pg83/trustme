//@ crate-type: lib
//@ edition: 2024
//@ compile-flags: --target=x86_64-unknown-linux-gnu --emit=link -Copt-level=0 -Cpanic=abort
#![feature(no_core, lang_items, link_llvm_intrinsics, repr_simd, simd_ffi)]
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

unsafe extern "C" {
    #[link_name = "llvm.x86.sse.rsqrt.ps"]
    fn rsqrt_ps(input: F32x4) -> F32x4;
}

#[unsafe(no_mangle)]
#[target_feature(enable = "sse")]
pub unsafe extern "C" fn reciprocal_square_roots(input: F32x4) -> F32x4 {
    unsafe { rsqrt_ps(input) }
}
