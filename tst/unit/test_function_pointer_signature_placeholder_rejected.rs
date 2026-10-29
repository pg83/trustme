//@ compile-fail: the placeholder `_` is not allowed in function signatures
fn invoke(_: fn(_) -> u32) {}
fn main() {}
