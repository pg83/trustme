//@ crate-type: lib
//@ edition: 2024
//@ compile-flags: --target=x86_64-unknown-linux-gnu --emit=link -Copt-level=0 -Cpanic=abort
#![feature(no_core, lang_items, link_llvm_intrinsics, repr_simd, simd_ffi)]
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
pub struct I16x8([i16; 8]);
#[repr(simd)]
pub struct I32x4([i32; 4]);

unsafe extern "C" {
    #[link_name = "llvm.x86.sse2.pmadd.wd"]
    fn pmadd_wd(a: I16x8, b: I16x8) -> I32x4;
}

#[unsafe(no_mangle)]
#[target_feature(enable = "sse2")]
pub unsafe extern "C" fn multiply_add_words(a: I16x8, b: I16x8) -> I32x4 {
    unsafe { pmadd_wd(a, b) }
}
