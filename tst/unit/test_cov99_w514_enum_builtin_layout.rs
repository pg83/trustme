//@ crate-type: lib
//@ compile-flags: --crate-name "#builtins"
//@ compile-fail: MaybeTrait on unknown trait
#![no_std]

pub enum u16 { Variant }
use core::primitive::u16 as Builtin;
struct Target;
impl<T: ?Builtin> Target {}
