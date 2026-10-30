//@ compile-fail: Too many type parameters
fn identity<T>(value: T) -> T { value }
fn main() { let _ = identity::<u8, u16>(7); }
