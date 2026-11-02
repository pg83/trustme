//@ compile-fail: generic arguments are not allowed
static VALUE: u32 = 37;
fn consume<const N: usize>() { let _ = VALUE::<N>; }
fn main() { consume::<3>(); }
