#![feature(lang_items, no_core)]
#![no_core]
#![no_main]
//@ compile-flags: -Coverflow-checks=off -Clink-arg=-lc

#[lang = "pointee_sized"]
pub trait PointeeSized {}

#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}

#[lang = "sized"]
pub trait Sized: MetaSized {}

#[lang = "copy"]
pub trait Copy {}

impl Copy for i32 {}

#[lang = "add"]
pub trait Add<Rhs = Self> {
    type Output;

    fn add(self, rhs: Rhs) -> Self::Output;
}

impl Add for i32 {
    type Output = i32;

    fn add(self, _rhs: i32) -> i32 {
        self
    }
}

#[no_mangle]
extern "C-unwind" fn main() -> i32 {
    let _value: i32 = 1 + 2;
    0
}
