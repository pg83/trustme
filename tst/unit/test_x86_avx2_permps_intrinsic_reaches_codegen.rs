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
pub struct F32x8([f32; 8]);
#[repr(simd)]
pub struct I32x8([i32; 8]);

unsafe extern "unadjusted" {
    #[link_name = "llvm.x86.avx2.permps"]
    fn permps(data: F32x8, index: I32x8) -> F32x8;
}

#[unsafe(no_mangle)]
#[target_feature(enable = "avx2")]
pub unsafe extern "unadjusted" fn permute_float_lanes(data: F32x8, index: I32x8) -> F32x8 {
    unsafe { permps(data, index) }
}
