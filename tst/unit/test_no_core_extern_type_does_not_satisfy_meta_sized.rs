//@ crate-type: lib
//@ compile-fail: error:0:
#![feature(no_core, lang_items, extern_types)]
#![no_core]

#[lang = "pointee_sized"]
pub trait PointeeSized {}
#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}
#[lang = "sized"]
pub trait Sized: MetaSized {}

extern "C" {
    pub type Foreign;
}

pub fn require_metadata<T: MetaSized + ?Sized>() {}

pub fn foreign_metadata_is_unknown() {
    require_metadata::<Foreign>();
}
