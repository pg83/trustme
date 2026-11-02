//@ compile-fail: generic arguments are not allowed
static VALUE: u32 = 31;
fn main() { let _ = VALUE::<3>; }
