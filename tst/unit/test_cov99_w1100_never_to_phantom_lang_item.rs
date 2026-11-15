//@ crate-type: lib
//@ compile-flags: -Z dump-mir -C panic=abort
#![feature(lang_items, no_core, never_type)]
#![no_core]

#[lang = "pointee_sized"]
pub trait PointeeSized {}
#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}
#[lang = "sized"]
pub trait Sized: MetaSized {}

#[lang = "phantom_data"]
pub struct Marker<T>(pub T);

#[no_mangle]
pub fn cast_from_never(value: !) -> Marker<u8> {
    value as Marker<u8>
}
