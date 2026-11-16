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
pub struct I16x8([i16; 8]);
#[repr(simd)]
pub struct U8x16([u8; 16]);

unsafe extern "unadjusted" {
    #[link_name = "llvm.x86.sse2.packuswb.128"]
    fn packuswb(lhs: I16x8, rhs: I16x8) -> U8x16;
}

#[unsafe(no_mangle)]
#[target_feature(enable = "sse2")]
pub unsafe extern "unadjusted" fn pack_unsigned_words(lhs: I16x8, rhs: I16x8) -> U8x16 {
    unsafe { packuswb(lhs, rhs) }
}
