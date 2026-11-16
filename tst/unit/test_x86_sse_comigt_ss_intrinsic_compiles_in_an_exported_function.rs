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
    #[link_name = "llvm.x86.sse.comigt.ss"]
    fn comigt_ss(a: F32x4, b: F32x4) -> i32;
}

#[target_feature(enable = "sse")]
pub unsafe fn compare_greater_than(a: F32x4, b: F32x4) -> i32 {
    unsafe { comigt_ss(a, b) }
}
