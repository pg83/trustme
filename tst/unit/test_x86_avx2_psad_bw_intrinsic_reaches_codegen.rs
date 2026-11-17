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
pub struct U8x32([u8; 32]);
#[repr(simd)]
pub struct U64x4([u64; 4]);

unsafe extern "unadjusted" {
    #[link_name = "llvm.x86.avx2.psad.bw"]
    fn psadbw(a: U8x32, b: U8x32) -> U64x4;
}

#[unsafe(no_mangle)]
#[target_feature(enable = "avx2")]
pub unsafe extern "unadjusted" fn avx2_psad_bw(a: U8x32, b: U8x32) -> U64x4 {
    unsafe { psadbw(a, b) }
}
