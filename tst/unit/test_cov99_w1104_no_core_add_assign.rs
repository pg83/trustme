//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(no_core, lang_items)]
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
impl Copy for i32 {}

pub fn add_without_add_assign_trait(mut value: i32) -> i32 {
    value += 2;
    value
}
