//@ compile-fail: generic
static VALUE: u32 = 23;
fn main() { let _ = VALUE::<u16>; }
