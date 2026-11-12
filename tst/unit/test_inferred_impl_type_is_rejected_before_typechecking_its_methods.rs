//@ crate-type: lib
//@ compile-fail: not allowed in impl signatures

#![no_std]

impl _ {
    fn unused() {}
}
