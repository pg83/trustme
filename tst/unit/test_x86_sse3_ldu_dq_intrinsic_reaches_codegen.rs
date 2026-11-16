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
pub struct I8x16([i8; 16]);

unsafe extern "unadjusted" {
    #[link_name = "llvm.x86.sse3.ldu.dq"]
    fn ldu_dq(pointer: *const i8) -> I8x16;
}

#[unsafe(no_mangle)]
#[target_feature(enable = "sse3")]
pub unsafe extern "unadjusted" fn sse3_ldu_dq(pointer: *const i8) -> I8x16 {
    unsafe { ldu_dq(pointer) }
}
