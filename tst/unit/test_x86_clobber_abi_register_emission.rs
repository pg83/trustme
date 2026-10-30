//@ compile-flags: --target=$DIR/support/targets/x86_clobber_codegen.toml -Cemit-cpp-only
//@ crate-type: lib
//@ input: support/targets/x86_clobber_codegen.toml
//@ output-contains: .cpp "eax", "ecx", "edx"
//@ output-contains: .cpp "xmm7", "k7", "mm7", "st(7)"
#![feature(no_core, lang_items, rustc_attrs)]
#![allow(internal_features)]
#![no_core]
#[lang = "pointee_sized"] pub trait PointeeSized {}
#[lang = "meta_sized"] pub trait MetaSized: PointeeSized {}
#[lang = "sized"] pub trait Sized: MetaSized {}
#[rustc_builtin_macro]
macro_rules! asm { ($($tokens:tt)*) => {}; }
pub unsafe fn c() { asm!("", clobber_abi("C"), options(nomem, nostack)); }
pub unsafe fn system() { asm!("", clobber_abi("system"), options(nomem, nostack)); }
pub unsafe fn efi() { asm!("", clobber_abi("efiapi"), options(nomem, nostack)); }
pub unsafe fn cdecl() { asm!("", clobber_abi("cdecl"), options(nomem, nostack)); }
pub unsafe fn stdcall() { asm!("", clobber_abi("stdcall"), options(nomem, nostack)); }
pub unsafe fn fastcall() { asm!("", clobber_abi("fastcall"), options(nomem, nostack)); }
