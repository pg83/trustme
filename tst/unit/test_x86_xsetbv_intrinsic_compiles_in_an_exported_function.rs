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
impl Copy for u32 {}

unsafe extern "C" {
    #[link_name = "llvm.x86.xsetbv"]
    fn xsetbv(index: u32, hi: u32, lo: u32);
}

#[target_feature(enable = "xsave")]
pub unsafe fn set(index: u32, hi: u32, lo: u32) {
    unsafe { xsetbv(index, hi, lo) }
}
