//@ crate-type: lib
//@ compile-flags: -Z dump-mir
#![feature(never_type)]

pub fn no_return() -> ! {
    loop {}
}

pub fn from_call() -> u32 {
    no_return()
}

pub fn from_value(value: !) -> u32 {
    value
}
