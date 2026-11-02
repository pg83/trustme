//@ compile-fail: mismatch
#![feature(type_ascription, builtin_syntax)]
fn leave() -> u32 { builtin # type_ascribe(return 59, u32) }
fn main() { assert_eq!(leave(), 59); }
