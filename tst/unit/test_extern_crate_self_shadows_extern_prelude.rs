//@ crate-type: lib
//@ compile-flags: --extern alloc

extern crate self as alloc;

pub mod macro_util {
    #[macro_export]
    macro_rules! local_macro {
        () => {
            7usize
        };
    }
}

pub const MACRO_VALUE: usize = ::alloc::local_macro!();

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
