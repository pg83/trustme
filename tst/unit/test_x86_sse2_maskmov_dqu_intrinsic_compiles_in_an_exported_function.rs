//@ crate-type: lib
//@ edition: 2024
//@ compile-flags: --target=x86_64-unknown-linux-gnu --emit=link -Copt-level=0 -Cpanic=abort
#![feature(no_core, lang_items, link_llvm_intrinsics, repr_simd, simd_ffi)]
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

#[repr(simd)]
pub struct I8x16([i8; 16]);

unsafe extern "C" {
    #[link_name = "llvm.x86.sse2.maskmov.dqu"]
    fn maskmovdqu(a: I8x16, mask: I8x16, mem_addr: *mut i8);
}

#[unsafe(no_mangle)]
#[target_feature(enable = "sse2")]
pub unsafe fn sse2_maskmov_dqu(a: I8x16, mask: I8x16, mem_addr: *mut i8) {
    unsafe { maskmovdqu(a, mask, mem_addr) }
}
