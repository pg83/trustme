//@ compile-fail: error
#![allow(dead_code, unused_imports)]
mod first { pub struct Shared { pub value: u8 } }
mod second { pub struct Shared { pub value: u16 } }
use first::*;
use second::*;
fn main() { let _: Option<Shared> = None; }
