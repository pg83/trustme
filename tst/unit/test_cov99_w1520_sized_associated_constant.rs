//@ crate-type: lib
//@ compile-flags: -Cpanic=abort
#![feature(no_core, lang_items)]
#![no_core]

#[lang = "pointee_sized"]
pub trait PointeeSized {}

#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}

#[lang = "sized"]
pub trait Sized: MetaSized {
    const VALUE: u8 = 37;
}

pub fn sized_value<T>() -> u8 {
    T::VALUE
}

#[no_mangle]
pub fn value() -> u8 {
    sized_value::<u32>()
}
