//@ compile-fail: generic
mod source { pub static VALUE: u32 = 19; }
use source::VALUE as ALIAS;
fn main() { let _ = ALIAS::<{ 3 + 4 }>; }
