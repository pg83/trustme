//@ compile-fail: #[unsafe(ffi_const)] on non-function
#![feature(ffi_const)]

#[unsafe(ffi_const)]
static VALUE: u32 = 17;

fn main() {}
