//@ crate-type: lib
#![feature(no_core, lang_items)]
#![allow(internal_features)]
#![no_core]

#[lang = "pointee_sized"]
pub trait PointeeSized {}

#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}

#[lang = "sized"]
pub trait Sized: MetaSized {}

#[lang = "reborrow"]
pub trait Reborrow {}

#[lang = "coerce_shared"]
pub trait CoerceShared: Reborrow {
    type Target;
}

#[lang = "RangeToInclusiveCopy"]
pub struct RangeToInclusive<Idx> {
    pub last: Idx,
}
