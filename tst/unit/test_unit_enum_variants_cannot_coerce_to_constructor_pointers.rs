//@ aux-build: enum_value_path_exports.rs
//@ compile-fail: Type mismatch
use enum_value_path_exports::Flag;
fn main() { let _: fn() -> Flag = Flag::On; }
