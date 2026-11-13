//@ crate-type: lib
#![feature(no_core, lang_items)]
#![no_core]

#[lang = "pointee_sized"]
pub trait PointeeSized {}
#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}
#[lang = "sized"]
pub trait Sized: MetaSized {}

pub fn require_metadata<T: MetaSized + ?Sized>() {}

pub fn slice_metadata_witness() {
    require_metadata::<[u16]>();
}
