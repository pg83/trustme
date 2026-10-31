//@ compile-fail: Invalid cast
fn invalid<T>(value: u8) -> T { value as T }
fn main() {}
