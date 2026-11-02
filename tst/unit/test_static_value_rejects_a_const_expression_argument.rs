//@ compile-fail: generic
static VALUE: u32 = 17;
fn main() { let _ = VALUE::<{ 2 + 3 }>; }
