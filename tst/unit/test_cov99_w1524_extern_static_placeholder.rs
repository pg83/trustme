//@ crate-type: lib
//@ compile-fail: placeholder
#![no_std]

extern "C" {
    pub static UNUSED: _;
}
