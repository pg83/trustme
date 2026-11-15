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
pub trait Copy: Sized {}

#[lang = "RangeFull"]
pub struct RangeFull;

#[lang = "Range"]
pub struct Range<T> {
    pub start: T,
    pub end: T,
}

#[lang = "RangeFrom"]
pub struct RangeFrom<T> {
    pub start: T,
}

#[lang = "RangeTo"]
pub struct RangeTo<T> {
    pub end: T,
}

#[lang = "RangeInclusive"]
pub struct RangeInclusive<T> {
    pub start: T,
    pub end: T,
    exhausted: bool,
}

#[lang = "RangeToInclusive"]
pub struct RangeToInclusive<T> {
    pub end: T,
}
