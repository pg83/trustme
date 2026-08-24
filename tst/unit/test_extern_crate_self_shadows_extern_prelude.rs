//@ crate-type: lib
//@ compile-flags: --extern alloc

extern crate self as alloc;

pub trait LocalTrait {
    fn value() -> usize;
}

pub struct Local;

impl ::alloc::LocalTrait for Local {
    fn value() -> usize {
        42
    }
}

pub fn value() -> usize {
    Local::value()
}
