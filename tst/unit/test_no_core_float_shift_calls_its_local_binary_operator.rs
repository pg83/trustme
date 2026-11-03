//@ compile-flags: -Clink-arg=-lc -Coverflow-checks=off
//@ compile-variant: -O
#![feature(lang_items, no_core)]
#![no_core]
#![no_main]
#[lang = "pointee_sized"] trait PointeeSized {}
#[lang = "meta_sized"] trait MetaSized: PointeeSized {}
#[lang = "sized"] trait Sized: MetaSized {}
#[lang = "copy"] trait Copy {}
impl Copy for f32 {}
impl Copy for u16 {}
impl Copy for i32 {}
#[lang = "shl"]
trait Shl<Rhs = Self> {
    type Output;
    fn shl(self, rhs: Rhs) -> Self::Output;
}
impl Shl for f32 {
    type Output = u16;
    fn shl(self, _: f32) -> u16 { 401 }
}
#[no_mangle]
extern "C-unwind" fn main() -> i32 {
    match (1.25f32 << 2.5f32, <f32 as Shl>::shl(1.25, 2.5)) {
        (401, 401) => 0,
        _ => 1,
    }
}
