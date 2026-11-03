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
#[lang = "bitand"]
trait BitAnd<Rhs = Self> {
    type Output;
    fn bitand(self, rhs: Rhs) -> Self::Output;
}
impl BitAnd for f32 {
    type Output = u16;
    fn bitand(self, _: f32) -> u16 { 389 }
}
#[no_mangle]
extern "C-unwind" fn main() -> i32 {
    match (1.25f32 & 2.5f32, <f32 as BitAnd>::bitand(1.25, 2.5)) {
        (389, 389) => 0,
        _ => 1,
    }
}
