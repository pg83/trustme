//@ compile-fail: Unexpected = or : after non-trivial type path
trait Bound {}
fn reject<T: Bound<() = u8>>() {}
fn main() {}
