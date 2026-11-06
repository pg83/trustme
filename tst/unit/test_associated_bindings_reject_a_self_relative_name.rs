//@ compile-fail: Unexpected = or : after non-trivial type path
trait Bound {}
type Item = u8;
fn reject<T: Bound<self::Item = u8>>() {}
fn main() {}
