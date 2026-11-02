//@ aux-build: enum_value_path_exports.rs
//@ compile-fail: function or static
core::arch::global_asm!("/* {symbol} */", symbol = sym enum_value_path_exports::NONE::<u16>);
fn main() {}
