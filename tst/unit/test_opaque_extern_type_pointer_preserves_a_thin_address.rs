//@ compile-variant: -O
#![feature(extern_types)]
extern "C" { type Foreign; }
const POINTER: *const Foreign = 37usize as *const Foreign;
#[inline(never)]
fn preserve(pointer: *const Foreign) -> *const Foreign { pointer }
fn main() { assert_eq!(preserve(POINTER) as *const () as usize, 37); }
