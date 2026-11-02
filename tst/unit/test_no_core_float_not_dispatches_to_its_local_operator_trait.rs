//@ compile-variant: -O
//@ compile-flags: -Clink-arg=-lc
#![feature(lang_items, no_core)]
#![no_core]
#![no_main]

#[lang = "pointee_sized"] pub trait PointeeSized {}
#[lang = "meta_sized"] pub trait MetaSized: PointeeSized {}
#[lang = "sized"] pub trait Sized: MetaSized {}
#[lang = "copy"] pub trait Copy {}
impl Copy for f32 {}
impl Copy for u32 {}

#[lang = "not"]
pub trait Not { type Output; fn not(self) -> Self::Output; }
impl Not for f32 {
    type Output = u32;
    fn not(self) -> u32 { 37 }
}

#[no_mangle]
extern "C" fn main() -> i32 {
    let value = !1.5f32;
    match value { 37 => 0, _ => 1 }
}
