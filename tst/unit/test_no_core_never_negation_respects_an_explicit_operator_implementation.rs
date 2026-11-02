//@ compile-variant: -O
//@ compile-flags: -Clink-arg=-lc
#![feature(lang_items, never_type, no_core)]
#![no_core]
#![no_main]

#[lang = "pointee_sized"] pub trait PointeeSized {}
#[lang = "meta_sized"] pub trait MetaSized: PointeeSized {}
#[lang = "sized"] pub trait Sized: MetaSized {}
#[lang = "copy"] pub trait Copy {}

#[lang = "neg"] pub trait Neg { type Output; fn neg(self) -> Self::Output; }
impl Neg for ! {
    type Output = u32;
    fn neg(self) -> u32 { match self {} }
}

fn negate(value: !) -> u32 { -value }

#[lang = "panic_cannot_unwind"]
fn cannot_unwind() -> ! { loop {} }

#[lang = "eh_personality"]
unsafe extern "C" fn rust_eh_personality(_version: i32, _actions: i32, _class: u64, _exception: *mut (), _context: *mut ()) -> i32 { 8 }

#[no_mangle]
extern "C" fn main() -> i32 {
    let _: fn(!) -> u32 = negate;
    0
}
