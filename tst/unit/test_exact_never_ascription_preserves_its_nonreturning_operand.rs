//@ compile-variant: -O
#![feature(type_ascription, builtin_syntax, never_type)]
fn leave() -> u32 { builtin # type_ascribe(return 79, !) }
fn main() { assert_eq!(leave(), 79); }
