//@ crate-type: lib
//@ compile-only
//@ edition: 2024
//@ compile-flags: --target=x86_64-unknown-linux-gnu --emit=link -Copt-level=0 -Cpanic=abort
#![feature(no_core, lang_items, abi_unadjusted, link_llvm_intrinsics)]
#![no_core]
#![allow(internal_features)]

#[lang = "pointee_sized"]
pub trait PointeeSized {}
#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}
#[lang = "sized"]
pub trait Sized: MetaSized {}
#[lang = "copy"]
pub trait Copy {}

unsafe extern "unadjusted" {
    #[link_name = "llvm.x86.avx.vzeroall"]
    fn vzeroall();
}

#[unsafe(no_mangle)]
#[target_feature(enable = "avx")]
pub unsafe extern "C" fn avx_vzeroall(before: *mut u8, after: *mut u8) {
    unsafe {
        *before = 1;
        vzeroall();
        *after = 2;
    }
}
