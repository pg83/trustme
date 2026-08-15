//@ crate-type: lib
#![feature(extern_types)]
#![allow(improper_ctypes, private_interfaces)]

unsafe extern "C" {
    type Opaque;
}

#[unsafe(no_mangle)]
pub unsafe fn opaque_layout(value: &Opaque) -> (usize, usize) {
    (core::mem::size_of_val(value), core::mem::align_of_val(value))
}
