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
pub struct I8x32([i8; 32]);

unsafe extern "unadjusted" {
    #[link_name = "llvm.x86.avx2.pshuf.b"]
    fn pshuf_b(input: I8x32, mask: I8x32) -> I8x32;
}

#[unsafe(no_mangle)]
#[target_feature(enable = "avx2")]
pub unsafe extern "unadjusted" fn avx2_pshuf_b(input: I8x32, mask: I8x32) -> I8x32 {
    unsafe { pshuf_b(input, mask) }
}
