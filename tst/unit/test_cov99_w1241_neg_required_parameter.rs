//@ crate-type: lib
//@ compile-fail: Failed to find an impl of
#![feature(no_core, lang_items)]
#![no_core]

#[lang = "sized"]
pub trait Sized {}

#[lang = "neg"]
pub trait Neg<T> {
    type Output;
}

pub struct Value;

pub fn probe() -> u8 {
    -Value
}
