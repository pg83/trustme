//@ compile-fail: requires `panic_const_add_overflow` lang_item
//@ compile-flags: -Coverflow-checks=on -Clink-arg=-lc
#![feature(lang_items, no_core)]
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
impl Copy for i32 {}
impl Copy for bool {}
#[lang = "add"]
trait Add<Rhs = Self> {
    type Output;
    fn add(self, rhs: Rhs) -> Self::Output;
}
impl Add for i32 {
    type Output = i32;
    fn add(self, _: i32) -> i32 { self }
}
fn select(pair: (i32, i32)) -> i32 {
    match pair {
        (value, 0) | (0, value) if { let _checked = 17i32 + value; true } => value,
        _ => 3,
    }
}
#[no_mangle]
extern "C-unwind" fn main(argc: i32, _: *const *const u8) -> i32 {
    match select((0, argc)) { 7 => 0, _ => 1 }
}
