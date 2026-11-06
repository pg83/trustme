//@ compile-fail: Unexpected = or : after non-trivial type path
trait Bound {}
mod nested { pub type Item = u8; }
fn reject<T: Bound<nested::Item = u8>>() {}
fn main() {}
