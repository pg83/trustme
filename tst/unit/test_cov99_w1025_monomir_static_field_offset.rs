//@ crate-type: lib
//@ compile-flags: -Ccodegen-type=monomir -Cemit-cpp-only -Zmir-opt-level=0
//@ output-contains: .mir + 4;
#![no_std]

#[repr(C)]
struct Pair {
    first: u32,
    second: u32,
}

static PAIR: Pair = Pair { first: 17, second: 29 };
const SECOND: &u32 = &PAIR.second;

pub fn selected() -> &'static u32 {
    SECOND
}
