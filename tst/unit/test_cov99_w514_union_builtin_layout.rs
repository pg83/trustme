//@ crate-type: lib
//@ compile-flags: --crate-name "#builtins"
//@ compile-fail: MaybeTrait on unknown trait
#![no_std]

pub union u32 { value: usize }
use core::primitive::u32 as Builtin;
struct Target;
impl<T: ?Builtin> Target {}
