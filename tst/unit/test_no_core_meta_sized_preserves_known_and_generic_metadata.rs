//@ crate-type: lib
#![feature(no_core, lang_items)]
#![no_core]

#[lang = "pointee_sized"]
pub trait PointeeSized {}
#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}
#[lang = "sized"]
pub trait Sized: MetaSized {}

pub trait Marker {}
pub struct Tail<T: ?Sized> {
    pub tag: u8,
    pub value: T,
}

pub fn require_metadata<T: MetaSized + ?Sized>() {}

pub fn forward_metadata<T: MetaSized + ?Sized>() {
    require_metadata::<T>();
}

pub fn known_metadata_witnesses() {
    require_metadata::<u16>();
    require_metadata::<str>();
    require_metadata::<Tail<str>>();
    require_metadata::<dyn Marker>();
    forward_metadata::<[u16]>();
}
