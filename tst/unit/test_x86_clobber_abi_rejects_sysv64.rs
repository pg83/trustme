//@ compile-flags: --target=$DIR/support/targets/x86_clobber_codegen.toml -Cemit-cpp-only
//@ crate-type: lib
//@ compile-fail: Unsupported clobber ABI `sysv64` for x86
//@ input: support/targets/x86_clobber_codegen.toml
#![feature(no_core, lang_items, rustc_attrs)]
#![allow(internal_features)]
#![no_core]
#[lang = "pointee_sized"] pub trait PointeeSized {}
#[lang = "meta_sized"] pub trait MetaSized: PointeeSized {}
#[lang = "sized"] pub trait Sized: MetaSized {}
#[rustc_builtin_macro]
macro_rules! asm { ($($tokens:tt)*) => {}; }
pub unsafe fn rejected() { asm!("", clobber_abi("sysv64")); }
