//@ compile-fail: Incorrect number of parameters
use std::arch::global_asm;
static VALUE: u32 = 101;
global_asm!(".quad {value}", value = sym VALUE::<u16>);
fn main() {}
