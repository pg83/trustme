#![feature(register_tool)]
#![register_tool(held)]
#[held::note]
#[link(name = "c")]
extern "C" { fn abs(value: i32) -> i32; }
fn main() { assert_eq!(unsafe { abs(-41) }, 41); }
