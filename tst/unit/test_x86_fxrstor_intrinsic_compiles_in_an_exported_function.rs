//@ crate-type: lib
//@ edition: 2024
//@ compile-flags: --target=x86_64-unknown-linux-gnu --emit=link -Copt-level=0 -Cpanic=abort
#![feature(no_core, lang_items, link_llvm_intrinsics)]
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
impl Copy for *const u8 {}

unsafe extern "C" {
    #[link_name = "llvm.x86.fxrstor"]
    fn fxrstor(pointer: *const u8);
}

#[target_feature(enable = "fxsr")]
pub unsafe fn restore(pointer: *const u8) {
    unsafe { fxrstor(pointer) }
}
