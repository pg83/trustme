//@ compile-fail: Incorrect number of arguments
fn first<T>(left: T, _right: T) -> T { left }
fn main() { let _ = first(13_u8); }
