#![no_std]

extern crate std;

macro_rules! make_generated_module {
    () => {
        mod generated {
            use self::fmt::Debug;
            use mystd::prelude::v1::*;
            use std::fmt;

            extern crate std as mystd;

            pub fn assert_debug<T: Debug>(_: &T) {}
        }
    };
}

make_generated_module!();

fn main() {
    generated::assert_debug(&1_u8);
}
