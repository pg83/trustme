//@ crate-type: lib
//@ edition: 2024

#![no_std]

pub fn force_eval() {
    unsafe {
        core::ptr::read_volatile(&(0.0 / 0.0));
    }
}
