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

#[lang = "not"] pub trait Not { type Output; fn not(self) -> Self::Output; }
impl Not for f32 { type Output = u32; fn not(self) -> u32 { 37 } }
struct Wrapped(f32);
impl Not for Wrapped { type Output = u32; fn not(self) -> u32 { Not::not(self.0) } }

#[lang = "panic_cannot_unwind"]
fn cannot_unwind() -> ! { loop {} }

#[lang = "eh_personality"]
unsafe extern "C" fn rust_eh_personality(_version: i32, _actions: i32, _class: u64, _exception: *mut (), _context: *mut ()) -> i32 { 8 }

#[no_mangle]
extern "C" fn main() -> i32 {
    let method = Not::not(1.5f32);
    let operator = !Wrapped(1.5f32);
    match method { 37 => match operator { 37 => 0, _ => 1 }, _ => 1 }
}
