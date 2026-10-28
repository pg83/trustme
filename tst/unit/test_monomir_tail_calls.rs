//@ crate-type: lib
//@ compile-flags: -Ccodegen-type=monomir -Cemit-cpp-only -Zmir-opt-level=0 -Zdump-mir
//@ output-contains: .mir TAILCALL
//@ output-contains: _3_mir.rs tailcall
#![feature(explicit_tail_calls)]
#![allow(incomplete_features)]

pub fn step(value: u32) -> u32 { value + 3 }
pub fn direct(value: u32) -> u32 { become step(value) }
pub fn indirect(value: u32) -> u32 {
    let next: fn(u32) -> u32 = step;
    become next(value)
}
