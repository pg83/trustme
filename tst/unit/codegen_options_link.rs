#![feature(lang_items, no_core)]
#![no_core]
#![no_main]

#[lang = "pointee_sized"]
trait PointeeSized {}

#[lang = "meta_sized"]
trait MetaSized: PointeeSized {}

#[lang = "sized"]
trait Sized: MetaSized {}

#[no_mangle]
extern "C-unwind" fn main() -> i32 {
    0
}
