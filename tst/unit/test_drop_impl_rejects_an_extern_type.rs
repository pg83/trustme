//@ compile-fail: Drop can only be implemented for structs, enums, and unions
#![feature(extern_types)]
unsafe extern "C" { type Foreign; }
impl Drop for Foreign { fn drop(&mut self) {} }
unsafe fn destroy(pointer: *mut Foreign) { core::ptr::drop_in_place(pointer); }
fn main() { std::hint::black_box(destroy as unsafe fn(*mut Foreign)); }
