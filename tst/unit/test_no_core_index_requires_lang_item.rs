//@ compile-fail: Undefined language item 'index' required
#![feature(lang_items, no_core)]
#![no_core]
#![no_main]

#[lang = "pointee_sized"]
pub trait PointeeSized {}

#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}

#[lang = "sized"]
pub trait Sized: MetaSized {}

#[no_mangle]
extern "C" fn main() -> i32 {
    let values = [0i32, 1];
    values[0]
}
