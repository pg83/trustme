//@ crate-type: lib
//@ compile-flags: -Z stop-after=mir -Z dump-mir
#![feature(never_type)]

pub unsafe fn never_register_input(value: !) {
    core::arch::asm!("/* {0} */", in(reg) value);
}
