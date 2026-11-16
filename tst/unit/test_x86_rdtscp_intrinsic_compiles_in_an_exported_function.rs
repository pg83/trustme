//@ crate-type: lib
//@ edition: 2024
//@ compile-flags: --target=x86_64-unknown-linux-gnu --emit=link -Copt-level=0 -Cpanic=abort
#![feature(no_core, lang_items, link_llvm_intrinsics, abi_unadjusted)]
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
impl Copy for u64 {}
impl Copy for u32 {}

unsafe extern "unadjusted" {
    #[link_name = "llvm.x86.rdtscp"]
    fn rdtscp() -> (u64, u32);
}

pub unsafe fn timestamp_and_aux() -> (u64, u32) {
    unsafe { rdtscp() }
}
