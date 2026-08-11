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

#[lang = "unsize"]
pub trait Unsize<T: PointeeSized>: PointeeSized {}

#[lang = "coerce_unsized"]
pub trait CoerceUnsized<T: PointeeSized> {}

impl<'a, 'b: 'a, T: PointeeSized + Unsize<U>, U: PointeeSized> CoerceUnsized<&'a U>
    for &'b T
{}

fn accept(_bytes: &[u8]) {}

#[no_mangle]
extern "C-unwind" fn main() -> i32 {
    accept(&[37u8]);
    0
}
