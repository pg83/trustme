//@ crate-type: lib
//@ compile-only
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
pub struct I32x8([i32; 8]);

unsafe extern "C" {
    #[link_name = "llvm.x86.avx2.psrav.d.256"]
    fn psrav_d_256(values: I32x8, counts: I32x8) -> I32x8;
}

#[unsafe(no_mangle)]
#[target_feature(enable = "avx2")]
pub unsafe extern "C" fn shift_signed_lanes(values: I32x8, counts: I32x8) -> I32x8 {
    unsafe { psrav_d_256(values, counts) }
}
