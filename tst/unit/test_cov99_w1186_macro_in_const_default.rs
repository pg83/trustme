//@ crate-type: lib
//@ compile-flags: --emit=metadata

#![feature(decl_macro)]
#![allow(unused_macros)]

pub struct ConstDefault<const N: usize = {
    macro in_default() {}
    7
}>;
