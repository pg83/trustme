//@ crate-type: lib
//@ compile-flags: -Zunpretty=expanded
#![feature(guard_patterns)]
#![allow(incomplete_features)]

fn consume((_ if true): u8) {}
