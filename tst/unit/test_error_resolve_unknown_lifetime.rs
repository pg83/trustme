//@ compile-fail: Couldn't find lifetime
type Borrow = &'missing u8;
fn main() {}
