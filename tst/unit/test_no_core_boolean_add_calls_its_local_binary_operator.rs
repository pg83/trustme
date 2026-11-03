//@ compile-flags: -Clink-arg=-lc -Coverflow-checks=off
//@ compile-variant: -O
#![feature(lang_items, no_core)]
#![no_core]
#![no_main]
#[lang = "pointee_sized"] trait PointeeSized {}
#[lang = "meta_sized"] trait MetaSized: PointeeSized {}
#[lang = "sized"] trait Sized: MetaSized {}
#[lang = "copy"] trait Copy {}
impl Copy for bool {}
impl Copy for u16 {}
impl Copy for i32 {}
#[lang = "add"]
trait Add<Rhs = Self> {
    type Output;
    fn add(self, rhs: Rhs) -> Self::Output;
}
impl Add for bool {
    type Output = u16;
    fn add(self, _: bool) -> u16 { 397 }
}
#[no_mangle]
extern "C-unwind" fn main() -> i32 {
    match (true + false, <bool as Add>::add(true, false)) {
        (397, 397) => 0,
        _ => 1,
    }
}
