//@ compile-fail: Type mismatch between str
#![feature(lang_items, no_core)]
#![no_core]
#![no_main]

#[lang = "pointee_sized"]
pub trait PointeeSized {}

#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}

#[lang = "sized"]
pub trait Sized: MetaSized {}

fn accept(_text: &str) {}

#[no_mangle]
extern "C" fn main() -> i32 {
    accept(&[37u8]);
    0
}
