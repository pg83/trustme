//@ compile-fail: requires `sized` lang_item
#![feature(lang_items, no_core)]
#![no_core]
#![no_main]

#[lang = "pointee_sized"]
trait PointeeSized {}

#[lang = "meta_sized"]
trait MetaSized: PointeeSized {}

#[no_mangle]
extern "C" fn main() -> i32 {
    0
}
