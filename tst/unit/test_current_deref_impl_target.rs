#![feature(lang_items, no_core)]
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

#[lang = "deref"]
pub trait Deref {
    type Target;

    fn deref(&self) -> &Self::Target;
}

struct Smart<T>(*const T);

impl<T> Deref for Smart<T> {
    type Target = T;

    fn deref(&self) -> &T {
        &**self
    }
}

#[no_mangle]
extern "C-unwind" fn main() -> i32 {
    0
}
