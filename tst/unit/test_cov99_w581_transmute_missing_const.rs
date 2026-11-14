//@ crate-type: lib
//@ compile-fail: Failed to find an impl of
//@ compile-flags: -Cpanic=abort
#![feature(no_core, lang_items)]
#![no_core]

#[lang = "sized"]
pub trait Sized {}

#[lang = "transmute_trait"]
pub unsafe trait TransmuteFrom<Src> {}

fn demand<T: TransmuteFrom<u8>>() {}

pub fn trigger() {
    demand::<u8>();
}
