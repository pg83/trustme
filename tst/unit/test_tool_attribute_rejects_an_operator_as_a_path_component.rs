//@ compile-fail: Unexpected token
#![feature(register_tool)]
#![register_tool(marker)]

#[marker::+]
fn main() {}
