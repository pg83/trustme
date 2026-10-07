// What libc does under `rustc-dep-of-std`: no prelude, core reached as an
// extern crate, its formatting macros called by path.
#![feature(no_core)]
#![no_core]

extern crate core;

pub fn checked_div(a: u32, b: u32) -> u32 {
    core::assert!(b != 0, "division of {} by zero", a);
    a / b
}
