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

impl Copy for *const u8 {}

unsafe extern "C" {
    #[link_name = "llvm.x86.sse2.clflush"]
    fn clflush(p: *const u8);
}

#[target_feature(enable = "sse2")]
pub unsafe fn flush_cache_line(p: *const u8) {
    unsafe { clflush(p) }
}
