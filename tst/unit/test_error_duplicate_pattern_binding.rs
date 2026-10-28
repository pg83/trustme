//@ compile-fail: Duplicate definition of
fn duplicate((value, value): (u32, u32)) {}
fn main() {}
