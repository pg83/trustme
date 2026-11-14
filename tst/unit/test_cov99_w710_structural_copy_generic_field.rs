//@ crate-type: lib
//@ compile-only
//@ compile-flags: -Z stop-after=typeck
#![feature(no_core, lang_items)]
#![no_core]

#[lang = "pointee_sized"]
pub trait PointeeSized {}
#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}
#[lang = "sized"]
pub trait Sized: MetaSized {}

pub fn take_field<T>(value: (T,)) -> T {
    value.0
}
