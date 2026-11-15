//@ crate-type: lib
//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort
//@ compile-fail: Failed to find an impl of

#![feature(no_core, lang_items)]
#![no_core]

#[lang = "sized"]
pub trait Sized {}

#[lang = "eq"]
pub trait Equal<Rhs = Self, Extra = ()> {}

pub fn compare(left: u8, right: u8) -> bool {
    left == right
}
