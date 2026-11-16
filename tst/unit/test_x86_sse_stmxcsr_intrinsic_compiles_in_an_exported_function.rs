//@ crate-type: lib
//@ edition: 2024
//@ compile-flags: --target=x86_64-unknown-linux-gnu --emit=link -Copt-level=0 -Cpanic=abort
#![feature(no_core, lang_items, link_llvm_intrinsics)]
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

impl Copy for *mut i8 {}

unsafe extern "C" {
    #[link_name = "llvm.x86.sse.stmxcsr"]
    fn stmxcsr(p: *mut i8);
}

#[target_feature(enable = "sse")]
pub unsafe fn store_mxcsr(p: *mut i8) {
    unsafe { stmxcsr(p) }
}
