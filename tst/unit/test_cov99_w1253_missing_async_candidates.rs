//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
//@ compile-fail: Unable to find an implementation of Fn*
#![feature(no_core, lang_items, unboxed_closures)]
#![no_core]
#![allow(internal_features)]

#[lang = "pointee_sized"]
pub trait PointeeSized {}
#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}
#[lang = "sized"]
pub trait Sized: MetaSized {}

#[lang = "fn_once"]
pub trait FnOnce<Args> {
    type Output;
}

pub fn call(value: u8) {
    value();
}
