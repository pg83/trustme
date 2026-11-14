//@ crate-type: lib
//@ compile-flags: -Z stop-after=typeck
#![feature(no_core, lang_items, auto_traits)]
#![no_core]

#[lang = "pointee_sized"]
pub trait PointeeSized {}

#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}

#[lang = "sized"]
pub trait Sized: MetaSized {}

#[lang = "tuple_trait"]
pub auto trait Tuple {}

pub fn require_tuple<T: Tuple>(_: T) {}

pub fn check() {
    require_tuple(0);
}
