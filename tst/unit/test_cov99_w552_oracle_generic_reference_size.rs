//@ crate-type: lib
//@ env: TRUSTME_CAPS_ORACLE=1
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

pub fn probe<T>(_: [u8; core::mem::size_of::<&T>()]) {}
