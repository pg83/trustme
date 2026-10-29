//@ compile-flags: -Cpanic=abort -Clink-arg=-lc
#![no_main]
#![feature(no_core, lang_items, auto_traits)]
#![no_core]
#![allow(internal_features)]
#[lang = "pointee_sized"] pub trait PointeeSized {}
#[lang = "meta_sized"] pub trait MetaSized: PointeeSized {}
#[lang = "sized"] pub trait Sized: MetaSized {}
#[lang = "freeze"] pub unsafe auto trait Freeze {}
#[lang = "copy"] pub trait Copy {}
impl Copy for u8 {}
#[lang = "legacy_receiver"] pub trait LegacyReceiver: PointeeSized {}
impl<T: PointeeSized> LegacyReceiver for &T {}
#[lang = "clone"] pub trait Clone: Sized { fn clone(&self) -> Self; }
#[lang = "drop_in_place"] pub unsafe fn drop_in_place<T: PointeeSized>(_value: *mut T) {}
pub struct Byte(u8);
impl Clone for Byte {
    fn clone(&self) -> Self {
        Byte(match self.0 { 1 => 11, 2 => 22, _ => 99 })
    }
}
#[no_mangle]
extern "C" fn main() -> i32 {
    let original = (Byte(1), Byte(2));
    let copied = Clone::clone(&original);
    match (original.0.0, original.1.0, copied.0.0, copied.1.0) {
        (1, 2, 11, 22) => 0,
        _ => 1,
    }
}
