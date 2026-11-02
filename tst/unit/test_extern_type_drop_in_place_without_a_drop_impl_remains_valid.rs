//@ compile-variant: -O
#![feature(extern_types)]
unsafe extern "C" { type Foreign; }
unsafe fn destroy(pointer: *mut Foreign) { core::ptr::drop_in_place(pointer); }
fn main() { std::hint::black_box(destroy as unsafe fn(*mut Foreign)); }
