//@ crate-type: lib
//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(rustc_attrs, pattern_types)]
#![allow(internal_features)]

#[rustc_builtin_macro]
macro_rules! pattern_type {
    ($($tokens:tt)*) => {};
}

type PositiveWord = pattern_type!(usize is 1..);

pub struct Word(pub PositiveWord);

const _: [(); core::mem::size_of::<usize>()] = [(); core::mem::size_of::<Word>()];
