//@ crate-type: lib
//@ compile-only
//@ compile-flags: -Z mir-opt-level=2 -Z dump-mir -Z stop-after=mir
#![no_std]

pub fn preserved_component(initial: ((u32, u32), u32), replacement: u32) -> u64 {
    let mut tuple = initial;
    tuple = (tuple.0, replacement);
    tuple.0.0 as u64
}
