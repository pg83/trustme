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
pub struct U8x16([u8; 16]);
#[repr(simd)]
pub struct U64x2([u64; 2]);

unsafe extern "C" {
    #[link_name = "llvm.x86.sse2.psad.bw"]
    fn psadbw(a: U8x16, b: U8x16) -> U64x2;
}

#[unsafe(no_mangle)]
#[target_feature(enable = "sse2")]
pub unsafe fn sse2_psad_bw(a: U8x16, b: U8x16) -> U64x2 {
    unsafe { psadbw(a, b) }
}
