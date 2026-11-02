//@ aux-build: enum_value_path_exports.rs
//@ compile-fail: Unable to find an implementation of Fn*
use enum_value_path_exports::NONE;
fn main() { let _ = NONE::<u16>(); }
