//@ aux-build: enum_value_path_exports.rs
//@ compile-fail: Unable to find an implementation of Fn*
use enum_value_path_exports::Packet;
fn main() { let _ = Packet::<u8>::Empty(); }
