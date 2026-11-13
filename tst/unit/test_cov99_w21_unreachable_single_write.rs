//@ crate-type: lib
//@ compile-flags: -Z dump-mir
#![allow(unreachable_code, unused_variables, unused_assignments)]

pub fn unreachable_single_write(value: u32) {
    return;
    let destination: u32;
    let source = value ^ 5;
    let observed = destination ^ 9;
    destination = source;
}
