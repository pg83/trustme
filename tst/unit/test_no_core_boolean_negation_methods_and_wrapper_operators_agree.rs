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

#[lang = "neg"] pub trait Neg { type Output; fn neg(self) -> Self::Output; }
impl Neg for bool { type Output = u16; fn neg(self) -> u16 { 419 } }
struct Wrapped(bool);
impl Neg for Wrapped { type Output = u16; fn neg(self) -> u16 { Neg::neg(self.0) } }

#[lang = "panic_cannot_unwind"]
fn cannot_unwind() -> ! { loop {} }

#[lang = "eh_personality"]
unsafe extern "C" fn rust_eh_personality(_version: i32, _actions: i32, _class: u64, _exception: *mut (), _context: *mut ()) -> i32 { 8 }

#[no_mangle]
extern "C" fn main() -> i32 {
    let method = Neg::neg(true);
    let operator = -Wrapped(true);
    match method { 419 => match operator { 419 => 0, _ => 1 }, _ => 1 }
}
