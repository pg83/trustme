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

#[repr(simd)]
pub struct I64x2([i64; 2]);

unsafe extern "unadjusted" {
    #[link_name = "llvm.x86.sse2.psll.q"]
    fn psll_q(input: I64x2, count: I64x2) -> I64x2;
}

#[unsafe(no_mangle)]
#[target_feature(enable = "sse2")]
pub unsafe extern "unadjusted" fn sse2_psll_q(input: I64x2, count: I64x2) -> I64x2 {
    unsafe { psll_q(input, count) }
}
