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
pub struct I16x16([i16; 16]);
#[repr(simd)]
pub struct I32x8([i32; 8]);

unsafe extern "C" {
    #[link_name = "llvm.x86.avx2.pmadd.wd"]
    fn pmadd_wd(a: I16x16, b: I16x16) -> I32x8;
}

#[unsafe(no_mangle)]
#[target_feature(enable = "avx2")]
pub unsafe extern "C" fn multiply_add_words(a: I16x16, b: I16x16) -> I32x8 {
    unsafe { pmadd_wd(a, b) }
}
