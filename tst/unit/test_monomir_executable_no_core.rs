//@ compile-only
//@ compile-flags: -Ccodegen-type=monomir
//@ output-contains: .mir fn main#(isize, *const *const i8): isize
//@ output-contains: .mir let direct_main_result: ()
#![feature(no_core, lang_items)]
#![no_core]

#[lang = "pointee_sized"]
pub trait PointeeSized {}
#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}
#[lang = "sized"]
pub trait Sized: MetaSized {}

fn main() {}
