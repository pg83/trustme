//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort

#![feature(no_core, lang_items)]
#![no_core]

#[lang = "pointee_sized"]
pub trait PointeeSized {}

#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}

pub trait Probe {}

pub type Object = dyn Probe;
