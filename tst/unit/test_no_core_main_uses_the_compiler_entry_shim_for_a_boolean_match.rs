#![feature(no_core, lang_items)]
#![allow(internal_features)]
#![no_core]

#[lang = "pointee_sized"]
trait PointeeSized {}
#[lang = "meta_sized"]
trait MetaSized: PointeeSized {}
#[lang = "sized"]
trait Sized: MetaSized {}
#[lang = "copy"]
trait Copy: Sized {}
impl Copy for bool {}

extern "C" {
    fn abort() -> !;
}

fn identity(value: bool) -> bool {
    value
}

fn main() {
    match identity(true) {
        true => (),
        false => unsafe { abort() },
    }
}
