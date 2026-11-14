//@ crate-type: lib
//@ compile-flags: --crate-name "#builtins"
//@ compile-fail: MaybeTrait on unknown trait
#![no_std]

pub struct u8;
use core::primitive::u8 as Builtin;
struct Target;
impl<T: ?Builtin> Target {}
