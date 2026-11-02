//@ compile-variant: -O
//@ compile-flags: -Clink-arg=-lc
#![feature(lang_items, no_core)]
#![no_core]
#![no_main]

#[lang = "pointee_sized"] pub trait PointeeSized {}
#[lang = "meta_sized"] pub trait MetaSized: PointeeSized {}
#[lang = "sized"] pub trait Sized: MetaSized {}
#[lang = "copy"] pub trait Copy {}
impl Copy for bool {}
impl Copy for u16 {}

#[lang = "neg"]
pub trait Neg { type Output; fn neg(self) -> Self::Output; }
impl Neg for bool {
    type Output = u16;
    fn neg(self) -> u16 { 419 }
}

#[no_mangle]
extern "C" fn main() -> i32 {
    let value = -true;
    match value { 419 => 0, _ => 1 }
}
