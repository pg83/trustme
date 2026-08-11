#![feature(lang_items, never_type, no_core)]
#![no_core]
#![no_main]
//@ compile-flags: -Clink-arg=-lc

#[lang = "pointee_sized"]
pub trait PointeeSized {}

#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}

#[lang = "sized"]
pub trait Sized: MetaSized {}

#[lang = "legacy_receiver"]
pub trait LegacyReceiver: PointeeSized {}

impl<T: PointeeSized> LegacyReceiver for &T {}
impl<T: PointeeSized> LegacyReceiver for &mut T {}

#[lang = "copy"]
pub trait Copy {}

impl Copy for ! {}

#[lang = "eq"]
pub trait PartialEq<Rhs = Self> {
    fn eq(&self, other: &Rhs) -> bool;
}

impl PartialEq for ! {
    fn eq(&self, _other: &!) -> bool {
        *self
    }
}

#[no_mangle]
extern "C-unwind" fn main() -> i32 {
    0
}
