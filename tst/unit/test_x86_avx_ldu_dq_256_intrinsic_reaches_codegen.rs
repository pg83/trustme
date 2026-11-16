//@ crate-type: lib
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

impl Copy for *const i8 {}

#[repr(simd)]
pub struct I8x32([i8; 32]);

unsafe extern "unadjusted" {
    #[link_name = "llvm.x86.avx.ldu.dq.256"]
    fn ldu_dq(pointer: *const i8) -> I8x32;
}

#[unsafe(no_mangle)]
#[target_feature(enable = "avx")]
pub unsafe extern "unadjusted" fn avx_ldu_dq_256(pointer: *const i8) -> I8x32 {
    unsafe { ldu_dq(pointer) }
}
