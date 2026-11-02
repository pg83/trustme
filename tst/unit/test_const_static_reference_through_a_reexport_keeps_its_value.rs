//@ compile-variant: -O
mod source { pub static VALUE: u32 = 29; }
use source::VALUE as ALIAS;
const POINTER: &'static u32 = &ALIAS;
fn main() { assert_eq!(*POINTER, 29); assert!(core::ptr::eq(POINTER, &ALIAS)); }
