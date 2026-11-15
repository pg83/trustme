//@ crate-type: lib
//@ compile-flags: --emit=metadata
#![no_std]
#![feature(decl_macro)]

macro erased_method() {
    #[cfg(any())]
    fn omitted() {}
}

pub struct Subject;

impl core::marker::Unpin for Subject {
    erased_method!();
}
