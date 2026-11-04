//@ compile-fail: Unexpected enum in path
enum Signal<T> { Value(T) }
fn main() { let _ = crate::Signal::ghost::Value(7u8); }
