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
pub struct U8x16([u8; 16]);
#[repr(simd)]
pub struct U16x8([u16; 8]);

unsafe extern "unadjusted" {
    #[link_name = "llvm.x86.sse41.mpsadbw"]
    fn mpsadbw(lhs: U8x16, rhs: U8x16, control: i8) -> U16x8;
}

#[unsafe(no_mangle)]
#[target_feature(enable = "sse4.1")]
pub unsafe extern "unadjusted" fn multiple_sad_bytes(lhs: U8x16, rhs: U8x16) -> U16x8 {
    unsafe { mpsadbw(lhs, rhs, 0b110) }
}
