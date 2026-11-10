//@ compile-fail: Undefined language item 'neg' required
#![feature(no_core, lang_items)]
#![no_core]
#![no_main]

#[lang = "pointee_sized"]
trait PointeeSized {}
#[lang = "meta_sized"]
trait MetaSized: PointeeSized {}
#[lang = "sized"]
trait Sized: MetaSized {}
#[lang = "copy"]
trait Copy {}
impl Copy for u8 {}
impl Copy for i32 {}

#[no_mangle]
extern "C" fn main() -> i32 {
    let input = 5u8;
    match -input {
        0 => 0,
        _ => 1,
    }
}
